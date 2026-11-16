#include "app/services/update_http.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <thread>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#endif

namespace app::services {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

#ifdef _WIN32
namespace {

struct WinHandle {
    HINTERNET h{nullptr};
    explicit WinHandle(HINTERNET handle = nullptr) : h(handle) {}
    ~WinHandle() { if (h) WinHttpCloseHandle(h); }
    WinHandle(const WinHandle&) = delete;
    WinHandle& operator=(const WinHandle&) = delete;
};

std::wstring widen(const std::string& s) {
    if (s.empty()) return L"";
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

HttpError map_win_error(DWORD e) {
    switch (e) {
        case ERROR_WINHTTP_TIMEOUT: return HttpError::Timeout;
        case ERROR_WINHTTP_CANNOT_CONNECT:
        case ERROR_WINHTTP_NAME_NOT_RESOLVED:
        case ERROR_WINHTTP_CONNECTION_ERROR:
        case ERROR_WINHTTP_LOGIN_FAILURE: return HttpError::NoNetwork;
        case ERROR_WINHTTP_SECURE_FAILURE:
        case ERROR_WINHTTP_SECURE_CERT_DATE_INVALID:
        case ERROR_WINHTTP_SECURE_CERT_CN_INVALID:
        case ERROR_WINHTTP_SECURE_INVALID_CA:
        case ERROR_WINHTTP_SECURE_CERT_REV_FAILED:
        case ERROR_WINHTTP_SECURE_CHANNEL_ERROR:
        case ERROR_WINHTTP_SECURE_INVALID_CERT:
        case ERROR_WINHTTP_SECURE_CERT_REVOKED:
        case ERROR_WINHTTP_SECURE_CERT_WRONG_USAGE: return HttpError::TlsFailure;
        default: return HttpError::Other;
    }
}

class WinHttpClient : public IHttpClient {
public:
    HttpResult get(const std::string& url, std::size_t max_bytes, int total_timeout_ms) override {
        HttpResult res;
        HttpError stop = HttpError::None;
        res = run(url, 0, total_timeout_ms,
                  [&](int status, std::uint64_t) { return status == 200; },
                  [&](const std::uint8_t* p, std::size_t n) {
                      if (res.body.size() + n > max_bytes) { stop = HttpError::TooLarge; return false; }
                      res.body.insert(res.body.end(), p, p + n);
                      return true;
                  },
                  stop);
        if (stop != HttpError::None && res.error == HttpError::None) res.error = stop;
        if (res.error == HttpError::None && res.status != 200) res.error = HttpError::BadStatus;
        return res;
    }

    HttpResult download(const std::string& url, const fs::path& part_file, std::uint64_t expected_total,
                        const std::function<bool(std::uint64_t)>& on_progress, int total_timeout_ms) override {
        std::error_code ec;
        std::uint64_t have = fs::exists(part_file, ec) ? fs::file_size(part_file, ec) : 0;
        if (ec || (expected_total > 0 && have > expected_total)) {
            fs::remove(part_file, ec);
            have = 0;
        }
        for (int attempt = 0; attempt < 2; ++attempt) {
            std::ofstream out;
            std::uint64_t written = have;
            bool opened = false;
            HttpError stop = HttpError::None;
            HttpResult res = run(
                url, have, total_timeout_ms,
                [&](int status, std::uint64_t content_length) {
                    if (status == 206) {
                        out.open(part_file, std::ios::binary | std::ios::app);
                    } else if (status == 200) {
                        out.open(part_file, std::ios::binary | std::ios::trunc);  // el servidor ignoró Range
                        written = 0;
                    } else {
                        return false;
                    }
                    opened = out.is_open();
                    if (expected_total > 0 && written + content_length > expected_total) { stop = HttpError::TooLarge; return false; }
                    return opened;
                },
                [&](const std::uint8_t* p, std::size_t n) {
                    if (expected_total > 0 && written + n > expected_total) { stop = HttpError::TooLarge; return false; }
                    out.write(reinterpret_cast<const char*>(p), static_cast<std::streamsize>(n));
                    if (!out) { stop = HttpError::Other; return false; }
                    written += n;
                    if (on_progress && !on_progress(written)) { stop = HttpError::Cancelled; return false; }
                    return true;
                },
                stop);
            if (out.is_open()) out.close();
            if (stop != HttpError::None && res.error == HttpError::None) res.error = stop;
            if (res.error == HttpError::None && res.status != 200 && res.status != 206) {
                // 416: el rango pedido ya no sirve; se descarta lo parcial y se reintenta desde cero una vez.
                if (res.status == 416 && attempt == 0 && have > 0) {
                    fs::remove(part_file, ec);
                    have = 0;
                    continue;
                }
                res.error = HttpError::BadStatus;
            }
            return res;
        }
        return HttpResult{HttpError::Other, 0, "descarga", {}};
    }

private:
    // Una petición GET con redirecciones HTTPS->HTTPS. Los datos llegan a on_data; on_headers decide si seguir.
    HttpResult run(const std::string& url, std::uint64_t range_start, int total_timeout_ms,
                   const std::function<bool(int, std::uint64_t)>& on_headers,
                   const std::function<bool(const std::uint8_t*, std::size_t)>& on_data,
                   HttpError& stop_reason) {
        HttpResult res;
        const auto deadline = Clock::now() + std::chrono::milliseconds(total_timeout_ms);
        auto timed_out = [&] { return Clock::now() >= deadline; };

        const std::wstring wurl = widen(url);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof(uc);
        wchar_t host[256] = {0};
        wchar_t path[2048] = {0};
        wchar_t extra[1024] = {0};
        uc.lpszHostName = host;      uc.dwHostNameLength = 255;
        uc.lpszUrlPath = path;       uc.dwUrlPathLength = 2047;
        uc.lpszExtraInfo = extra;    uc.dwExtraInfoLength = 1023;
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc)) {
            res.error = HttpError::Other;
            res.detail = "URL inválida";
            return res;
        }
        if (uc.nScheme != INTERNET_SCHEME_HTTPS) {
            res.error = HttpError::NotHttps;
            res.detail = "solo se permite https";
            return res;
        }

        WinHandle session(WinHttpOpen(L"CafeteriaITCH-Actualizador/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                      WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
        if (!session.h) { res.error = HttpError::Other; res.detail = "WinHttpOpen"; return res; }

        // TLS 1.2 como único protocolo. En Windows 7 sin la actualización KB3140245 WinHTTP rechaza esta opción.
        DWORD protocols = WINHTTP_FLAG_SECURE_PROTOCOL_TLS1_2;
        if (!WinHttpSetOption(session.h, WINHTTP_OPTION_SECURE_PROTOCOLS, &protocols, sizeof(protocols))) {
            res.error = HttpError::TlsUnavailable;
            res.detail = "TLS 1.2 no disponible (error " + std::to_string(GetLastError()) + ")";
            return res;
        }
        WinHttpSetTimeouts(session.h, 3000, 3000, 5000, 8000);

        WinHandle connect(WinHttpConnect(session.h, host, uc.nPort, 0));
        if (!connect.h) { res.error = map_win_error(GetLastError()); res.detail = "WinHttpConnect"; return res; }
        const std::wstring target = std::wstring(path) + extra;
        WinHandle request(WinHttpOpenRequest(connect.h, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE));
        if (!request.h) { res.error = map_win_error(GetLastError()); res.detail = "WinHttpOpenRequest"; return res; }

        // Nunca se baja la seguridad: no se tocan las opciones que ignoran errores de certificado. Redirecciones
        // permitidas solo de https a https y como máximo 5.
        DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
        WinHttpSetOption(request.h, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy));
        DWORD max_redirects = 5;
        WinHttpSetOption(request.h, WINHTTP_OPTION_MAX_HTTP_AUTOMATIC_REDIRECTS, &max_redirects, sizeof(max_redirects));

        if (range_start > 0) {
            const std::wstring hdr = L"Range: bytes=" + std::to_wstring(range_start) + L"-";
            WinHttpAddRequestHeaders(request.h, hdr.c_str(), static_cast<DWORD>(-1L), WINHTTP_ADDREQ_FLAG_ADD);
        }

        if (!WinHttpSendRequest(request.h, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.h, nullptr)) {
            res.error = timed_out() ? HttpError::Timeout : map_win_error(GetLastError());
            res.detail = "envío o recepción (error " + std::to_string(GetLastError()) + ")";
            return res;
        }

        DWORD status = 0;
        DWORD size = sizeof(status);
        if (!WinHttpQueryHeaders(request.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                                 WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX)) {
            res.error = HttpError::Other;
            res.detail = "sin código de respuesta";
            return res;
        }
        res.status = static_cast<int>(status);
        DWORD clen = 0;
        size = sizeof(clen);
        std::uint64_t content_length = 0;
        if (WinHttpQueryHeaders(request.h, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
                                WINHTTP_HEADER_NAME_BY_INDEX, &clen, &size, WINHTTP_NO_HEADER_INDEX)) {
            content_length = clen;
        }
        if (!on_headers(res.status, content_length)) return res;  // el llamador interpreta status / stop_reason

        std::vector<std::uint8_t> buffer(64 * 1024);
        while (true) {
            if (timed_out()) { res.error = HttpError::Timeout; res.detail = "tiempo total agotado"; return res; }
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(request.h, &available)) {
                res.error = timed_out() ? HttpError::Timeout : map_win_error(GetLastError());
                res.detail = "lectura";
                return res;
            }
            if (available == 0) break;
            DWORD read = 0;
            const DWORD want = std::min<DWORD>(available, static_cast<DWORD>(buffer.size()));
            if (!WinHttpReadData(request.h, buffer.data(), want, &read)) {
                res.error = timed_out() ? HttpError::Timeout : map_win_error(GetLastError());
                res.detail = "lectura";
                return res;
            }
            if (read == 0) break;
            if (!on_data(buffer.data(), read)) return res;
        }
        return res;
    }
};

} // namespace

std::shared_ptr<IHttpClient> make_winhttp_client() { return std::make_shared<WinHttpClient>(); }
#else
std::shared_ptr<IHttpClient> make_winhttp_client() { return nullptr; }
#endif

// ---------------------------------------------------------------------------------------------
// Solo para compilaciones con CAFETERIA_TEST_HOOKS: fuente local de actualizaciones para pruebas de la interfaz.
// ---------------------------------------------------------------------------------------------
#ifdef CAFETERIA_TEST_HOOKS
namespace {

class FolderHttpClient : public IHttpClient {
public:
    FolderHttpClient(fs::path folder, int delay_ms) : folder_(std::move(folder)), delay_ms_(delay_ms) {}

    HttpResult get(const std::string& url, std::size_t max_bytes, int) override {
        HttpResult res;
        fs::path file;
        if (!locate(url, file, res)) return res;
        std::ifstream in(file, std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (bytes.size() > max_bytes) { res.error = HttpError::TooLarge; return res; }
        res.status = 200;
        res.body = std::move(bytes);
        return res;
    }

    HttpResult download(const std::string& url, const fs::path& part_file, std::uint64_t expected_total,
                        const std::function<bool(std::uint64_t)>& on_progress, int) override {
        HttpResult res;
        fs::path file;
        if (!locate(url, file, res)) return res;
        std::error_code ec;
        std::uint64_t have = fs::exists(part_file, ec) ? fs::file_size(part_file, ec) : 0;
        const std::uint64_t total = fs::file_size(file, ec);
        if (have > total) { fs::remove(part_file, ec); have = 0; }
        if (expected_total > 0 && total > expected_total) { res.error = HttpError::TooLarge; return res; }
        std::ifstream in(file, std::ios::binary);
        in.seekg(static_cast<std::streamoff>(have));
        std::ofstream out(part_file, std::ios::binary | std::ios::app);
        std::vector<char> chunk(64 * 1024);
        std::uint64_t written = have;
        while (in) {
            in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const auto got = in.gcount();
            if (got <= 0) break;
            out.write(chunk.data(), got);
            out.flush();
            written += static_cast<std::uint64_t>(got);
            if (on_progress && !on_progress(written)) { res.error = HttpError::Cancelled; return res; }
            if (delay_ms_ > 0) std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms_));
        }
        res.status = 200;
        return res;
    }

private:
    bool locate(const std::string& url, fs::path& file, HttpResult& res) {
        std::error_code ec;
        if (!fs::is_directory(folder_, ec) || fs::is_empty(folder_, ec)) {
            res.error = HttpError::NoNetwork;
            res.detail = "carpeta de pruebas vacía o inexistente";
            return false;
        }
        const auto slash = url.find_last_of('/');
        const std::string name = slash == std::string::npos ? url : url.substr(slash + 1);
        file = folder_ / fs::u8path(name);
        if (name.empty() || name.find("..") != std::string::npos || !fs::is_regular_file(file, ec)) {
            res.error = HttpError::BadStatus;
            res.status = 404;
            res.detail = "no está en la carpeta de pruebas";
            return false;
        }
        return true;
    }

    fs::path folder_;
    int delay_ms_;
};

} // namespace
#endif

std::shared_ptr<IHttpClient> make_default_update_http_client() {
#ifdef CAFETERIA_TEST_HOOKS
    {
        const char* source = std::getenv("CAFETERIA_UPDATE_TEST_SOURCE");
        if (source && *source) {
            const char* delay = std::getenv("CAFETERIA_UPDATE_TEST_DELAY_MS");
            return std::make_shared<FolderHttpClient>(fs::u8path(source), delay ? std::atoi(delay) : 0);
        }
    }
#endif
    return make_winhttp_client();
}

} // namespace app::services
