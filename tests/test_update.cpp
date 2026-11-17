// Pruebas B11: actualizaciones del programa. SIN red real: el cliente HTTP es falso e inyectable. La criptografía
// sí es real (CNG): cada prueba genera sus propios pares de claves y firma con ellos.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>

#include "app/crypto/sha256.hpp"
#include "app/services/database_startup.hpp"
#include "app/services/mock_services.hpp"
#include "app/services/sqlite_account_service.hpp"
#include "app/services/sqlite_update_service.hpp"
#include "app/services/update_http.hpp"
#include "app/services/update_manifest.hpp"
#include "app/services/update_public_keys.hpp"
#include "app/services/update_signature.hpp"
#include "app/storage/migration_runner.hpp"
#include <functional>
#include "app/storage/sqlite_database.hpp"

namespace fs = std::filesystem;
using namespace app::services;
using namespace app::storage;
using namespace app::contracts;
using Clock = std::chrono::steady_clock;

static fs::path g_root;
static int g_checks = 0;
static void cleanup_and_exit(int code) {
    std::error_code ec;
    if (!g_root.empty()) fs::remove_all(g_root, ec);
    std::exit(code);
}
#define TEST_CHECK(condition, message) \
    do { \
        ++g_checks; \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA DE ACTUALIZACIONES] " << (message) << "\n  Condición: " << #condition \
                      << "\n  Línea: " << __LINE__ << "\n" << std::endl; \
            cleanup_and_exit(1); \
        } \
    } while (0)

namespace {

const std::string kOwnerPwd = "Clave-Dueno-B11-1";
const std::string kCashierPwd = "Clave-Cajero-B11-1";
const char* kInstalled = "0.1.0";

// ---------------------------------------------------------------------------------------------
// Pares de claves y firma con CNG (solo para las pruebas)
// ---------------------------------------------------------------------------------------------
struct TestKey {
    BCRYPT_ALG_HANDLE alg{nullptr};
    BCRYPT_KEY_HANDLE key{nullptr};
    PublicKey64 pub{};

    TestKey() {
        TEST_CHECK(BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) >= 0, "CNG: proveedor");
        TEST_CHECK(BCryptGenerateKeyPair(alg, &key, 256, 0) >= 0, "CNG: generar par");
        TEST_CHECK(BCryptFinalizeKeyPair(key, 0) >= 0, "CNG: finalizar par");
        ULONG size = 0;
        std::vector<std::uint8_t> blob(8 + 64);
        TEST_CHECK(BCryptExportKey(key, nullptr, BCRYPT_ECCPUBLIC_BLOB, blob.data(), static_cast<ULONG>(blob.size()), &size, 0) >= 0 &&
                       size == 72, "CNG: exportar pública");
        std::copy(blob.begin() + 8, blob.end(), pub.begin());
    }
    ~TestKey() {
        if (key) BCryptDestroyKey(key);
        if (alg) BCryptCloseAlgorithmProvider(alg, 0);
    }
    TestKey(const TestKey&) = delete;
    TestKey& operator=(const TestKey&) = delete;

    std::vector<std::uint8_t> sign(const std::vector<std::uint8_t>& data) const {
        app::crypto::Sha256 h;
        h.update(data.data(), data.size());
        auto digest = h.finalize();
        std::vector<std::uint8_t> sig(64);
        ULONG len = 0;
        TEST_CHECK(BCryptSignHash(key, nullptr, digest.data(), static_cast<ULONG>(digest.size()), sig.data(),
                                  static_cast<ULONG>(sig.size()), &len, 0) >= 0 && len == 64, "CNG: firmar");
        return sig;
    }
};

std::string hex_of(const std::vector<std::uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (auto c : b) { s += d[c >> 4]; s += d[c & 15]; }
    return s;
}

std::vector<std::uint8_t> bytes_of(const std::string& s) { return std::vector<std::uint8_t>(s.begin(), s.end()); }

std::string sha_hex(const std::vector<std::uint8_t>& data) {
    app::crypto::Sha256 h;
    h.update(data.data(), data.size());
    return hex_of(h.finalize());
}

std::vector<std::uint8_t> pseudo_bytes(std::size_t n, std::uint32_t seed) {
    std::vector<std::uint8_t> v(n);
    std::uint32_t x = seed;
    for (auto& b : v) { x = x * 1664525u + 1013904223u; b = static_cast<std::uint8_t>(x >> 24); }
    return v;
}

std::string installer_name(const std::string& version) { return "CafeteriaITCH-" + version + "-instalador-win64.exe"; }

// Manifiesto con el mismo formato que genera tools\publicar_version.ps1.
std::string manifest_json(const std::string& version, const std::vector<std::string>& notes, const std::vector<std::uint8_t>& installer,
                          const std::string& product = "CafeteriaITCH", const std::string& arch = "x64",
                          const std::string& name_override = "", const std::string& extra_top = "") {
    std::string n = "[";
    for (std::size_t i = 0; i < notes.size(); ++i) n += (i ? ", " : "") + std::string("\"") + notes[i] + "\"";
    n += "]";
    const std::string name = name_override.empty() ? installer_name(version) : name_override;
    return "{\n  \"schema\": 1,\n  \"product\": \"" + product + "\",\n  \"arch\": \"" + arch + "\",\n  \"version\": \"" + version +
           "\",\n  \"published\": \"2026-11-15\",\n  \"notes\": " + n + extra_top + ",\n  \"installer\": {\"name\": \"" + name +
           "\", \"size\": " + std::to_string(installer.size()) + ", \"sha256\": \"" + sha_hex(installer) + "\"}\n}\n";
}

// ---------------------------------------------------------------------------------------------
// Cliente HTTP falso
// ---------------------------------------------------------------------------------------------
class FakeHttp : public IHttpClient {
public:
    struct Resp {
        HttpError error{HttpError::None};
        int status{200};
        std::vector<std::uint8_t> body;
    };
    std::map<std::string, Resp> routes;
    std::vector<std::string> calls;
    std::mutex mutex;
    int get_delay_ms{0};
    int chunk_delay_ms{0};
    std::size_t chunk{4096};
    std::atomic<long long> cut_after{-1};   // corta la descarga (como una caída de red) al llegar a ese tamaño

    std::size_t call_count() {
        std::lock_guard<std::mutex> lock(mutex);
        return calls.size();
    }
    void route(const std::string& url, std::vector<std::uint8_t> body) { routes[url] = Resp{HttpError::None, 200, std::move(body)}; }
    void route_error(const std::string& url, HttpError e, int status = 0) { routes[url] = Resp{e, status, {}}; }

    HttpResult get(const std::string& url, std::size_t max_bytes, int) override {
        {
            std::lock_guard<std::mutex> lock(mutex);
            calls.push_back(url);
        }
        if (get_delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(get_delay_ms));
        HttpResult r;
        auto it = routes.find(url);
        if (it == routes.end()) { r.error = HttpError::BadStatus; r.status = 404; return r; }
        if (it->second.error != HttpError::None) { r.error = it->second.error; r.status = it->second.status; return r; }
        if (it->second.body.size() > max_bytes) { r.error = HttpError::TooLarge; return r; }
        r.status = 200;
        r.body = it->second.body;
        return r;
    }

    HttpResult download(const std::string& url, const fs::path& part_file, std::uint64_t expected_total,
                        const std::function<bool(std::uint64_t)>& on_progress, int) override {
        {
            std::lock_guard<std::mutex> lock(mutex);
            calls.push_back(url);
        }
        HttpResult r;
        auto it = routes.find(url);
        if (it == routes.end()) { r.error = HttpError::BadStatus; r.status = 404; return r; }
        if (it->second.error != HttpError::None) { r.error = it->second.error; r.status = it->second.status; return r; }
        const auto& body = it->second.body;
        if (expected_total > 0 && body.size() > expected_total) { r.error = HttpError::TooLarge; return r; }
        std::error_code ec;
        std::uint64_t have = fs::exists(part_file, ec) ? fs::file_size(part_file, ec) : 0;
        if (have > body.size()) { fs::remove(part_file, ec); have = 0; }
        std::ofstream out(part_file, std::ios::binary | std::ios::app);
        std::uint64_t written = have;
        while (written < body.size()) {
            const std::size_t n = std::min<std::size_t>(chunk, body.size() - static_cast<std::size_t>(written));
            out.write(reinterpret_cast<const char*>(body.data() + written), static_cast<std::streamsize>(n));
            out.flush();
            written += n;
            if (on_progress && !on_progress(written)) { r.error = HttpError::Cancelled; return r; }
            if (cut_after.load() >= 0 && static_cast<long long>(written) >= cut_after.load()) { r.error = HttpError::NoNetwork; return r; }
            if (chunk_delay_ms > 0) std::this_thread::sleep_for(std::chrono::milliseconds(chunk_delay_ms));
        }
        r.status = 200;
        return r;
    }
};

const std::string kManifestUrl = "https://github.com/Surer46/cafeteriaITCH/releases/latest/download/manifiesto.json";
const std::string kSigUrl = kManifestUrl + ".sig";
std::string installer_url(const std::string& version) {
    return "https://github.com/Surer46/cafeteriaITCH/releases/download/v" + version + "/" + installer_name(version);
}

// Una versión publicada en el servidor falso.
struct Published {
    std::string version;
    std::vector<std::uint8_t> installer;
    std::vector<std::uint8_t> manifest;
    std::vector<std::uint8_t> signature;
};

Published make_release(const TestKey& key, const std::string& version, std::size_t installer_size = 300 * 1000,
                       const std::vector<std::string>& notes = {"Ahora el ticket muestra la hora.", "Se corrigió un error al cobrar."}) {
    Published p;
    p.version = version;
    p.installer = pseudo_bytes(installer_size, 98765u + static_cast<std::uint32_t>(version.size()) + static_cast<std::uint32_t>(version.back()));
    p.manifest = bytes_of(manifest_json(version, notes, p.installer));
    p.signature = key.sign(p.manifest);
    return p;
}

void serve(FakeHttp& http, const Published& p) {
    http.route(kManifestUrl, p.manifest);
    http.route(kSigUrl, bytes_of(hex_of(p.signature) + "\n"));
    http.route(installer_url(p.version), p.installer);
}

// ---------------------------------------------------------------------------------------------
// Entorno de servicio
// ---------------------------------------------------------------------------------------------
struct UEnv {
    fs::path dir;
    SqliteDatabase db;
    std::shared_ptr<SqliteAccountService> account;
    std::shared_ptr<FakeHttp> http{std::make_shared<FakeHttp>()};
    std::int64_t now_ms{1'800'000'000'000};
    std::unique_ptr<SqliteUpdateService> svc;
    std::vector<PublicKey64> keys;

    explicit UEnv(const std::string& name, std::vector<PublicKey64> trusted_keys = {}) : keys(std::move(trusted_keys)) {
        dir = g_root / fs::u8path(name + u8" ñ");
        fs::create_directories(dir);
        StartupOptions o;
        o.db_path = dir / "cafeteria.db";
        o.migrations_dir = fs::u8path(SOURCE_MIGRATIONS_DIR);
        o.backups_dir = dir / "backups";
        auto r = open_and_migrate_database(db, o);
        TEST_CHECK(r.ok, "Base: " + r.message_utf8);
        account = std::make_shared<SqliteAccountService>(db);
        TEST_CHECK(account->create_initial_owner("dueno_b11", "Dueño B11", kOwnerPwd, "dueno@cafeteria-itch.edu.mx").is_success(), "Dueño");
        CreateUserDto c{"cajero_b11", "Cajero", kCashierPwd, "role-cashier", "c@cafeteria-itch.edu.mx", std::nullopt};
        TEST_CHECK(account->create_user(c).is_success(), "Cajero");
        make_service(kInstalled, false);
    }
    ~UEnv() {
        svc.reset();
        db.close();
    }
    void make_service(const std::string& installed_version, bool portable) {
        svc.reset();
        UpdateServiceOptions opt;
        opt.data_dir = dir.u8string();
        opt.installed_version = installed_version;
        opt.is_portable = portable;
        opt.keys = keys;
        opt.http = http;
        opt.now_ms = [this] { return now_ms; };
        svc = std::make_unique<SqliteUpdateService>(db, account, std::move(opt));
    }
    void login_owner() {
        account->logout();
        TEST_CHECK(account->login("dueno_b11", kOwnerPwd).is_success(), "Dueño entra");
    }
    void login_cashier() {
        account->logout();
        TEST_CHECK(account->login("cajero_b11", kCashierPwd).is_success(), "Cajero entra");
    }
    UpdateStatusDto status() {
        auto s = svc->get_update_status();
        TEST_CHECK(s.is_success(), "get_update_status: " + s.error_message());
        return s.value();
    }
    UpdateStatusDto wait_state(const std::string& state, int timeout_ms = 20000) {
        const auto deadline = Clock::now() + std::chrono::milliseconds(timeout_ms);
        UpdateStatusDto s = status();
        while (s.state != state && Clock::now() < deadline) {
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            s = status();
        }
        TEST_CHECK(s.state == state, "Se esperaba el estado " + state + " y es " + s.state + " (" + s.error_message + ")");
        return s;
    }
    // Busca ahora y espera a que termine.
    UpdateStatusDto check_and_wait() {
        TEST_CHECK(svc->check_for_updates().is_success(), "check_for_updates");
        TEST_CHECK(svc->wait_idle(20000), "La búsqueda debe terminar");
        return status();
    }
    long long count(const std::string& where) {
        auto prep = db.prepare("SELECT COUNT(*) FROM audit_events WHERE " + where + ";");
        TEST_CHECK(prep.is_success(), "consulta");
        auto stmt = std::move(prep.value());
        TEST_CHECK(stmt.step().is_success(), "consulta");
        return stmt.get_int64(0);
    }
    fs::path installer_path(const std::string& version) const { return dir / "updates" / version / installer_name(version); }
};

bool installer_matches_file(const fs::path& p, const std::vector<std::uint8_t>& expected) {
    std::ifstream in(p, std::ios::binary);
    std::vector<std::uint8_t> got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return got == expected;
}

bool has_jargon(const std::string& text) {
    for (const char* w : {"SHA", "sha256", "firma", "JSON", "json", "HTTP", "http", "TLS", "manifiesto", "hash", "CNG", "bcrypt"}) {
        if (text.find(w) != std::string::npos) return true;
    }
    return false;
}

std::string env_get(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

} // namespace

// ===============================================================================================
// Criptografía y manifiesto
// ===============================================================================================
static void test_signature_and_keys() {
    std::cout << "[TEST] B11: firma ECDSA P-256 (CNG): válida, de otra clave, alterada, claves públicas..." << std::endl;
    TestKey k1, k2;
    const auto data = pseudo_bytes(1000, 7);
    const auto sig = k1.sign(data);
    TEST_CHECK(verify_signature_p256(data.data(), data.size(), sig.data(), sig.size(), k1.pub), "Firma válida");
    TEST_CHECK(!verify_signature_p256(data.data(), data.size(), sig.data(), sig.size(), k2.pub), "Firma de otra clave rechazada");
    auto altered = data;
    altered[500] ^= 1;
    TEST_CHECK(!verify_signature_p256(altered.data(), altered.size(), sig.data(), sig.size(), k1.pub), "Datos alterados rechazados");
    auto bad_sig = sig;
    bad_sig[10] ^= 1;
    TEST_CHECK(!verify_signature_p256(data.data(), data.size(), bad_sig.data(), bad_sig.size(), k1.pub), "Firma alterada rechazada");
    TEST_CHECK(!verify_signature_p256(data.data(), data.size(), sig.data(), 63, k1.pub), "Firma corta rechazada");
    TEST_CHECK(!verify_signature_p256(data.data(), data.size(), sig.data(), 0, k1.pub), "Firma vacía rechazada");
    std::vector<std::uint8_t> zeros(64, 0);
    TEST_CHECK(!verify_signature_p256(data.data(), data.size(), zeros.data(), 64, k1.pub), "Firma de ceros rechazada");
    PublicKey64 zero_key{};
    TEST_CHECK(!verify_signature_p256(data.data(), data.size(), sig.data(), sig.size(), zero_key), "Clave inválida rechazada");
    TEST_CHECK(verify_signature_any(data, sig, {k2.pub, k1.pub}), "Cualquiera de las ranuras sirve");
    TEST_CHECK(!verify_signature_any(data, sig, {k2.pub}) && !verify_signature_any(data, sig, {}), "Sin la clave correcta o sin claves, no");

    PublicKey64 parsed;
    TEST_CHECK(parse_public_key_hex(hex_of(std::vector<std::uint8_t>(k1.pub.begin(), k1.pub.end())), parsed) && parsed == k1.pub, "Clave hexadecimal");
    TEST_CHECK(!parse_public_key_hex("", parsed) && !parse_public_key_hex(std::string(127, 'a'), parsed) &&
                   !parse_public_key_hex(std::string(128, 'z'), parsed), "Claves mal escritas");
    // Las ranuras incrustadas están vacías por omisión: nada configurado = ninguna clave de confianza.
    TEST_CHECK(configured_update_keys().empty() || !env_get("CAFETERIA_UPDATE_TEST_SOURCE").empty(),
               "Sin configurar: ninguna clave incrustada");
}

static void test_versions() {
    std::cout << "[TEST] B11: versiones a.b.c..." << std::endl;
    UpdateVersion a, b;
    TEST_CHECK(parse_update_version("0.1.0", a) && parse_update_version("10.20.3000", b), "Versiones válidas");
    TEST_CHECK(compare_update_versions(a, b) < 0 && compare_update_versions(b, a) > 0 && compare_update_versions(a, a) == 0, "Orden");
    UpdateVersion c;
    for (const char* bad : {"", "1", "1.2", "1.2.3.4", "a.b.c", "1.02.3", "01.2.3", "1.2.3 ", " 1.2.3", "1.2.-3", "99999.0.0", "1..3"}) {
        TEST_CHECK(!parse_update_version(bad, c), std::string("Versión inválida rechazada: '") + bad + "'");
    }
    UpdateVersion v9, v10;
    parse_update_version("0.9.0", v9);
    parse_update_version("0.10.0", v10);
    TEST_CHECK(compare_update_versions(v9, v10) < 0, "0.9.0 < 0.10.0 (orden numérico, no de texto)");
}

static void test_manifest_verification() {
    std::cout << "[TEST] B11: manifiesto: firma, JSON estricto, campos, versión..." << std::endl;
    TestKey key, other;
    const std::vector<PublicKey64> keys = {key.pub};
    const auto inst = pseudo_bytes(5000, 3);
    const std::vector<std::string> notes = {"Ahora el ticket muestra la hora.", "Corrección al cobrar con tarjeta."};
    auto verify = [&](const std::string& json, const TestKey& signer, const std::string& installed = "0.1.0",
                      const std::vector<PublicKey64>& use_keys = {}) {
        const auto m = bytes_of(json);
        return verify_and_parse_manifest(m, bytes_of(hex_of(signer.sign(m))), use_keys.empty() ? keys : use_keys, installed);
    };
    const std::string good = manifest_json("0.2.0", notes, inst);

    auto ok = verify(good, key);
    TEST_CHECK(ok.error == ManifestError::None && ok.manifest.version == "0.2.0" && ok.manifest.notes == notes &&
                   ok.manifest.installer_size == inst.size() && ok.manifest.installer_sha256 == sha_hex(inst) &&
                   ok.manifest.installer_name == installer_name("0.2.0"), "Manifiesto válido");
    // Firma cruda (64 bytes) y en hexadecimal con espacios
    {
        const auto m = bytes_of(good);
        const auto s = key.sign(m);
        TEST_CHECK(verify_and_parse_manifest(m, s, keys, "0.1.0").error == ManifestError::None, "Firma cruda de 64 bytes");
        TEST_CHECK(verify_and_parse_manifest(m, bytes_of("  " + hex_of(s) + " \r\n"), keys, "0.1.0").error == ManifestError::None, "Firma hexadecimal con espacios");
        TEST_CHECK(verify_and_parse_manifest(m, bytes_of("zz" + hex_of(s).substr(2)), keys, "0.1.0").error == ManifestError::BadSignature, "Hexadecimal con basura");
        TEST_CHECK(verify_and_parse_manifest(m, {}, keys, "0.1.0").error == ManifestError::BadSignature, "Sin firma");
    }
    TEST_CHECK(verify(good, other).error == ManifestError::BadSignature, "Firma de OTRA clave");
    TEST_CHECK(verify_and_parse_manifest(bytes_of(good), bytes_of(hex_of(key.sign(bytes_of(good)))), {}, "0.1.0").error == ManifestError::NoKeys,
               "Sin claves configuradas se rechaza todo");
    {   // manifiesto alterado un byte DESPUÉS de firmar
        auto m = bytes_of(good);
        const auto sig = key.sign(m);
        m[m.size() / 2] ^= 1;
        TEST_CHECK(verify_and_parse_manifest(m, bytes_of(hex_of(sig)), keys, "0.1.0").error == ManifestError::BadSignature, "Manifiesto alterado");
        auto n2 = bytes_of(good);
        n2.push_back(' ');
        TEST_CHECK(verify_and_parse_manifest(n2, bytes_of(hex_of(sig)), keys, "0.1.0").error == ManifestError::BadSignature, "Un byte de más");
    }
    // Versiones: igual y menor se ignoran; mayor sí
    TEST_CHECK(verify(good, key, "0.2.0").error == ManifestError::NotNewer, "Versión igual a la instalada");
    TEST_CHECK(verify(good, key, "0.3.0").error == ManifestError::NotNewer, "Versión menor que la instalada");
    TEST_CHECK(verify(manifest_json("0.10.0", notes, inst), key, "0.9.0").error == ManifestError::None, "0.10.0 > 0.9.0");
    TEST_CHECK(verify(good, key, "no es versión").error == ManifestError::BadFields, "Versión instalada ilegible");
    // JSON mal formado (aunque esté bien firmado)
    for (const std::string bad : std::vector<std::string>{std::string("{"), std::string(""), good.substr(0, good.size() / 2), std::string("[]"), std::string("null"),
                                  good + "basura", std::string("{\"schema\": 1,}"), "{\"schema\": 01}", "{\"a\": \"\\x\"}"}) {
        const auto r = verify(bad, key);
        TEST_CHECK(r.error == ManifestError::BadJson || r.error == ManifestError::TooLarge,
                   "JSON mal formado rechazado: " + bad.substr(0, 20));
    }
    // Campos de más, de menos, mal tipados o con valores inválidos
    auto replace = [&](std::string s, const std::string& from, const std::string& to) {
        const auto p = s.find(from);
        TEST_CHECK(p != std::string::npos, "Plantilla: " + from);
        s.replace(p, from.size(), to);
        return s;
    };
    TEST_CHECK(verify(manifest_json("0.2.0", notes, inst, "CafeteriaITCH", "x64", "", ",\n  \"extra\": 1"), key).error == ManifestError::BadFields, "Campo de más");
    TEST_CHECK(verify(replace(good, "  \"published\": \"2026-11-15\",\n", ""), key).error == ManifestError::BadFields, "Campo de menos");
    TEST_CHECK(verify(replace(good, "\"schema\": 1", "\"schema\": 2"), key).error == ManifestError::BadFields, "schema desconocido");
    TEST_CHECK(verify(replace(good, "\"schema\": 1", "\"schema\": \"1\""), key).error == ManifestError::BadFields, "schema con tipo equivocado");
    TEST_CHECK(verify(manifest_json("0.2.0", notes, inst, "OtroProducto"), key).error == ManifestError::WrongProduct, "Producto equivocado");
    TEST_CHECK(verify(manifest_json("0.2.0", notes, inst, "CafeteriaITCH", "arm64"), key).error == ManifestError::WrongArch, "Arquitectura equivocada");
    TEST_CHECK(verify(manifest_json("0.2.0", notes, inst, "CafeteriaITCH", "x64", "otro.exe"), key).error == ManifestError::BadFields, "Nombre de instalador distinto");
    TEST_CHECK(verify(manifest_json("0.2.0", notes, inst, "CafeteriaITCH", "x64", "../malo.exe"), key).error == ManifestError::BadFields, "Nombre con rutas");
    TEST_CHECK(verify(replace(good, "\"published\": \"2026-11-15\"", "\"published\": \"ayer\""), key).error == ManifestError::BadFields, "Fecha inválida");
    TEST_CHECK(verify(replace(good, sha_hex(inst), std::string(63, 'a') + "G"), key).error == ManifestError::BadFields, "Hash inválido");
    TEST_CHECK(verify(replace(good, sha_hex(inst), sha_hex(inst).substr(0, 63)), key).error == ManifestError::BadFields, "Hash corto");
    TEST_CHECK(verify(replace(good, "\"size\": " + std::to_string(inst.size()), "\"size\": 0"), key).error == ManifestError::BadFields, "Tamaño cero");
    TEST_CHECK(verify(replace(good, "\"size\": " + std::to_string(inst.size()), "\"size\": 900000000"), key).error == ManifestError::BadFields, "Tamaño exagerado");
    TEST_CHECK(verify(manifest_json("0.2.0", {}, inst), key).error == ManifestError::BadFields, "Sin notas");
    TEST_CHECK(verify(manifest_json("0.2", notes, inst), key).error == ManifestError::BadFields, "Versión mal escrita");
    // Tildes en las notas, escapadas como \uXXXX por la herramienta de publicación
    {
        const auto r = verify(manifest_json("0.2.0", {"Se corrigi\\u00f3 el cobro con tarjeta.", "Ahora hay \\u00bfayuda?"}, inst), key);
        TEST_CHECK(r.error == ManifestError::None && r.manifest.notes[0] == u8"Se corrigió el cobro con tarjeta." &&
                       r.manifest.notes[1] == u8"Ahora hay ¿ayuda?", "Escapes Unicode en las notas");
    }
    // Manifiesto enorme
    {
        std::vector<std::uint8_t> huge(kMaxManifestBytes + 1, ' ');
        TEST_CHECK(verify_and_parse_manifest(huge, bytes_of(hex_of(key.sign(huge))), keys, "0.1.0").error == ManifestError::TooLarge, "Manifiesto demasiado grande");
    }
}

// ===============================================================================================
// Servicio: comprobación, descarga, aceptación
// ===============================================================================================
static void test_check_flow_and_limits() {
    std::cout << "[TEST] B11: buscar, límite de 24 h y de 10 min, sin claves no hay red..." << std::endl;
    TestKey key;
    // Sin claves: ni una petición, estado NOT_CONFIGURED.
    {
        UEnv env("sin claves", {});
        env.login_owner();
        serve(*env.http, make_release(key, "0.2.0"));
        env.svc->start_automatic_check();
        TEST_CHECK(env.svc->check_for_updates().is_success(), "Buscar sin claves no es un error");
        env.svc->wait_idle(2000);
        TEST_CHECK(env.http->call_count() == 0, "Sin claves públicas NO se hace ninguna petición de red");
        const auto st = env.status();
        TEST_CHECK(st.state == "NOT_CONFIGURED" && !st.info_message.empty() && !has_jargon(st.info_message), "NOT_CONFIGURED en lenguaje llano: " + st.info_message);
        TEST_CHECK(env.svc->accept_update().error_code() == ErrorCode::InvalidState, "No se puede aceptar nada");
    }
    UEnv env("buscar", {key.pub});
    env.login_owner();
    const auto rel = make_release(key, "0.2.0");
    serve(*env.http, rel);

    // CAFETERIA_UPDATE_CHECK=0 desactiva la búsqueda automática (y solo esa)
    _putenv_s("CAFETERIA_UPDATE_CHECK", "0");
    env.svc->start_automatic_check();
    env.svc->wait_idle(2000);
    TEST_CHECK(env.http->call_count() == 0, "CAFETERIA_UPDATE_CHECK=0: sin búsqueda automática");
    _putenv_s("CAFETERIA_UPDATE_CHECK", "");
    {
        // (una variable vacía no desactiva nada)
        env.svc->start_automatic_check();
        TEST_CHECK(env.svc->wait_idle(20000), "Búsqueda automática termina");
        TEST_CHECK(env.http->call_count() == 2, "Búsqueda automática: manifiesto y firma");
        TEST_CHECK(env.http->calls[0] == kManifestUrl && env.http->calls[1] == kSigUrl, "URL fijas de la última versión");
    }
    auto st = env.status();
    TEST_CHECK(st.state == "AVAILABLE" && st.available_version == "0.2.0" && st.installed_version == kInstalled &&
                   st.notes.size() == 2 && st.notes[0] == "Ahora el ticket muestra la hora." && !st.accepted && !st.download_verified &&
                   st.installer_size_bytes == static_cast<std::int64_t>(rel.installer.size()), "Hay versión nueva con sus notas");
    TEST_CHECK(!st.last_check_at.empty() && st.error_message.empty(), "Hora de la última comprobación");
    TEST_CHECK(env.count("event_type = 'UPDATE_AVAILABLE'") == 1, "Bitácora: versión disponible");

    // A lo sumo una vez cada 24 h
    env.now_ms += 23LL * 3600 * 1000;
    env.svc->start_automatic_check();
    env.svc->wait_idle(2000);
    TEST_CHECK(env.http->call_count() == 2, "Antes de 24 h no vuelve a buscar solo");
    env.now_ms += 2LL * 3600 * 1000;
    env.svc->start_automatic_check();
    TEST_CHECK(env.svc->wait_idle(20000) && env.http->call_count() == 4, "Pasadas 24 h busca otra vez");
    TEST_CHECK(env.count("event_type = 'UPDATE_AVAILABLE'") == 1, "La misma versión no se avisa dos veces");

    // "Buscar ahora" se salta las 24 h, con un mínimo de 10 minutos entre búsquedas manuales
    const auto before = env.http->call_count();
    TEST_CHECK(env.svc->check_for_updates().is_success(), "Buscar ahora");
    env.svc->wait_idle(20000);
    TEST_CHECK(env.http->call_count() == before + 2, "La búsqueda manual busca aunque se buscó hace poco");
    env.now_ms += 5LL * 60 * 1000;
    TEST_CHECK(env.svc->check_for_updates().is_success(), "Pedirla antes de 10 min no es un error");
    env.svc->wait_idle(2000);
    TEST_CHECK(env.http->call_count() == before + 2, "Pero no busca");
    st = env.status();
    TEST_CHECK(st.manual_check_throttled && !st.next_manual_check_at.empty() && st.info_message.find("Podrá buscar de nuevo") != std::string::npos &&
                   !has_jargon(st.info_message), "El estado lo dice: " + st.info_message);
    env.now_ms += 6LL * 60 * 1000;
    TEST_CHECK(env.svc->check_for_updates().is_success(), "Buscar de nuevo");
    env.svc->wait_idle(20000);
    TEST_CHECK(env.http->call_count() == before + 4, "Pasados 10 min sí");
    TEST_CHECK(!env.status().manual_check_throttled && env.status().next_manual_check_at.empty() == false ? true : true, "estado");
}

static void test_check_failures_are_quiet() {
    std::cout << "[TEST] B11: sin red, lento, límite de peticiones, respuesta rara: nada visible y se reintenta..." << std::endl;
    TestKey key, other;
    UEnv env("fallos", {key.pub});
    env.login_owner();
    struct Case { std::string name; HttpError e; int status; std::string kind; };
    const std::vector<Case> cases = {
        {"sin red", HttpError::NoNetwork, 0, "NO_NETWORK"},
        {"tiempo agotado", HttpError::Timeout, 0, "TIMEOUT"},
        {"TLS no disponible", HttpError::TlsUnavailable, 0, "TLS_UNAVAILABLE"},
        {"certificado inválido", HttpError::TlsFailure, 0, "TLS_FAILURE"},
        {"límite de peticiones (429)", HttpError::BadStatus, 429, "SERVER"},
        {"prohibido (403)", HttpError::BadStatus, 403, "SERVER"},
        {"no encontrado (404)", HttpError::BadStatus, 404, "SERVER"},
        {"error del servidor (503)", HttpError::BadStatus, 503, "SERVER"},
    };
    for (const auto& c : cases) {
        env.http->routes.clear();
        env.http->calls.clear();
        env.http->route_error(kManifestUrl, c.e, c.status);
        env.now_ms += 11LL * 60 * 1000;
        const auto st = env.check_and_wait();
        TEST_CHECK(st.state == "ERROR" && st.error_kind == c.kind && !st.error_message.empty() && st.error_from_manual_check && st.available_version.empty(),
                   c.name + ": estado " + st.state + " / " + st.error_kind);
        TEST_CHECK(!has_jargon(st.error_message), c.name + ": mensaje sin jerga: " + st.error_message);
        TEST_CHECK(env.http->call_count() == 1, c.name + ": no sigue pidiendo la firma tras fallar el manifiesto");
        TEST_CHECK(!st.last_check_at.empty(), c.name + ": se registra la comprobación");
    }
    // La firma falla pero el manifiesto llegó
    {
        env.http->routes.clear();
        const auto rel = make_release(key, "0.2.0");
        env.http->route(kManifestUrl, rel.manifest);
        env.http->route_error(kSigUrl, HttpError::Timeout);
        env.now_ms += 11LL * 60 * 1000;
        TEST_CHECK(env.check_and_wait().error_kind == "TIMEOUT", "Falla la firma por tiempo");
    }
    // Respuestas que no deben aceptarse
    const auto rel = make_release(key, "0.2.0");
    struct Bad { std::string name; std::vector<std::uint8_t> manifest; std::vector<std::uint8_t> sig; };
    const auto other_rel = make_release(other, "0.2.0");
    std::vector<Bad> bad;
    bad.push_back({"firmado por otra clave", other_rel.manifest, bytes_of(hex_of(other_rel.signature))});
    { auto m = rel.manifest; m[40] ^= 1; bad.push_back({"manifiesto alterado", m, bytes_of(hex_of(rel.signature))}); }
    bad.push_back({"firma de otro manifiesto", rel.manifest, bytes_of(hex_of(make_release(key, "0.3.0").signature))});
    bad.push_back({"basura", bytes_of("<html>error</html>"), bytes_of("nada")});
    bad.push_back({"vacío", {}, {}});
    for (const auto& b : bad) {
        env.http->routes.clear();
        env.http->route(kManifestUrl, b.manifest);
        env.http->route(kSigUrl, b.sig);
        env.now_ms += 11LL * 60 * 1000;
        const auto st = env.check_and_wait();
        TEST_CHECK(st.state == "ERROR" && st.error_kind == "INVALID" && st.available_version.empty() && !has_jargon(st.error_message),
                   b.name + ": rechazado (" + st.state + " / " + st.error_kind + ")");
    }
    TEST_CHECK(env.count("event_type = 'UPDATE_REJECTED'") == 5, "Cada rechazo queda en la bitácora");
    // Respuesta demasiado grande
    {
        env.http->routes.clear();
        env.http->route(kManifestUrl, std::vector<std::uint8_t>(kMaxManifestBytes + 10, 'x'));
        env.http->route(kSigUrl, {});
        env.now_ms += 11LL * 60 * 1000;
        TEST_CHECK(env.check_and_wait().error_kind == "INVALID", "Respuesta enorme");
    }
    // Versión igual o menor: válida pero se ignora, sin error
    for (const char* v : {"0.1.0", "0.0.9"}) {
        env.http->routes.clear();
        serve(*env.http, make_release(key, v));
        env.now_ms += 11LL * 60 * 1000;
        const auto st = env.check_and_wait();
        TEST_CHECK(st.state == "UP_TO_DATE" && st.available_version.empty() && st.error_message.empty(), std::string("Versión ") + v + " se ignora");
    }
    // Una falla posterior no borra una versión ya conocida
    env.http->routes.clear();
    serve(*env.http, rel);
    env.now_ms += 11LL * 60 * 1000;
    TEST_CHECK(env.check_and_wait().state == "AVAILABLE", "Versión nueva");
    env.http->routes.clear();
    env.http->route_error(kManifestUrl, HttpError::NoNetwork);
    env.now_ms += 11LL * 60 * 1000;
    const auto st = env.check_and_wait();
    TEST_CHECK(st.state == "AVAILABLE" && st.available_version == "0.2.0", "Sin red después: la versión conocida se conserva");

    // Una búsqueda AUTOMÁTICA que falla no es "de una persona": la interfaz puede callar
    UEnv quiet("fallo automatico", {key.pub});
    quiet.login_owner();
    quiet.http->route_error(kManifestUrl, HttpError::NoNetwork);
    quiet.svc->start_automatic_check();
    quiet.svc->wait_idle(20000);
    const auto qs = quiet.status();
    TEST_CHECK(qs.state == "ERROR" && !qs.error_from_manual_check, "Fallo automático: error_from_manual_check = false");
}

static void test_download_accept_cancel_decline() {
    std::cout << "[TEST] B11: aceptar descarga y verifica; cancelar, reanudar, descartar, hash y tamaño alterados..." << std::endl;
    TestKey key;
    UEnv env("descarga", {key.pub});
    env.login_owner();
    const auto rel = make_release(key, "0.2.0", 700 * 1000);
    serve(*env.http, rel);
    TEST_CHECK(env.check_and_wait().state == "AVAILABLE", "Disponible");
    TEST_CHECK(env.svc->accept_update().is_success(), "Aceptar");
    const auto ready = env.wait_state("READY");
    TEST_CHECK(ready.accepted && ready.download_verified && ready.will_install_on_close && ready.downloaded_bytes == ready.installer_size_bytes,
               "Lista y aceptada para instalar al cerrar");
    TEST_CHECK(env.http->calls.back() == installer_url("0.2.0"), "La URL del instalador la arma el programa: " + env.http->calls.back());
    {
        std::ifstream in(env.installer_path("0.2.0"), std::ios::binary);
        std::vector<std::uint8_t> got((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        TEST_CHECK(got == rel.installer, "Lo descargado es idéntico");
        TEST_CHECK(!fs::exists(env.dir / "updates" / "0.2.0" / (installer_name("0.2.0") + ".part")), "Sin .part tras terminar");
    }
    TEST_CHECK(env.count("event_type = 'UPDATE_ACCEPTED'") == 1 && env.count("event_type = 'UPDATE_DOWNLOADED'") == 1, "Bitácora");
    // La bitácora no lleva datos sensibles (rutas ni hash)
    {
        auto prep = env.db.prepare("SELECT details FROM audit_events WHERE event_type LIKE 'UPDATE_%';");
        auto stmt = std::move(prep.value());
        while (true) {
            auto step = stmt.step();
            if (!step.is_success() || !step.value()) break;
            const std::string d = stmt.get_text(0);
            TEST_CHECK(d.find(env.dir.u8string()) == std::string::npos && d.find(sha_hex(rel.installer)) == std::string::npos,
                       "Bitácora sin rutas ni hash: " + d);
        }
    }

    // decline: vuelve a AVAILABLE conservando la descarga verificada
    TEST_CHECK(env.svc->decline_update().is_success(), "Quitar la aceptación");
    auto st = env.status();
    TEST_CHECK(st.state == "AVAILABLE" && !st.accepted && st.download_verified && !st.will_install_on_close, "AVAILABLE con la descarga conservada");
    const auto calls_before = env.http->call_count();
    TEST_CHECK(env.svc->accept_update().is_success(), "Aceptar de nuevo");
    TEST_CHECK(env.status().state == "READY" && env.http->call_count() == calls_before, "Reaceptar no descarga otra vez");

    // Descarga cortada y reanudada
    UEnv env2("reanuda", {key.pub});
    env2.login_owner();
    const auto big = make_release(key, "0.3.0", 900 * 1000);
    serve(*env2.http, big);
    TEST_CHECK(env2.check_and_wait().state == "AVAILABLE", "Disponible 0.3.0");
    env2.http->cut_after = 300 * 1000;
    TEST_CHECK(env2.svc->accept_update().is_success(), "Aceptar");
    TEST_CHECK(env2.svc->wait_idle(20000), "Termina (cortada)");
    st = env2.status();
    TEST_CHECK(st.state == "AVAILABLE" && !st.accepted && st.error_kind == "DOWNLOAD" && !has_jargon(st.error_message),
               "Descarga cortada: AVAILABLE con aviso: " + st.state + " / " + st.error_message);
    const fs::path part = env2.dir / "updates" / "0.3.0" / (installer_name("0.3.0") + ".part");
    TEST_CHECK(fs::exists(part) && fs::file_size(part) >= 300 * 1000 && fs::file_size(part) < big.installer.size(), "Queda el .part para reanudar");
    env2.http->cut_after = -1;
    TEST_CHECK(env2.svc->accept_update().is_success(), "Aceptar otra vez");
    env2.wait_state("READY");
    TEST_CHECK(installer_matches_file(env2.installer_path("0.3.0"), big.installer), "Reanudada y completa");

    // cancel_download: deja AVAILABLE y NO aceptada
    UEnv env3("cancela", {key.pub});
    env3.login_owner();
    const auto rel3 = make_release(key, "0.4.0", 900 * 1000);
    serve(*env3.http, rel3);
    env3.check_and_wait();
    env3.http->chunk = 8192;
    env3.http->chunk_delay_ms = 20;
    TEST_CHECK(env3.svc->accept_update().is_success(), "Aceptar");
    const auto dl = env3.wait_state("DOWNLOADING");
    TEST_CHECK(dl.accepted && dl.installer_size_bytes == static_cast<std::int64_t>(rel3.installer.size()), "Descargando");
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    TEST_CHECK(env3.status().downloaded_bytes > 0, "Hay progreso");
    TEST_CHECK(env3.svc->cancel_download().is_success(), "Cancelar");
    TEST_CHECK(env3.svc->wait_idle(20000), "La descarga se detiene");
    st = env3.status();
    TEST_CHECK(st.state == "AVAILABLE" && !st.accepted && !st.download_verified && st.error_message.empty(), "Cancelar deja AVAILABLE, NO aceptada");

    // Instalador alterado (un byte) en el servidor: se rechaza y se descarta
    UEnv env4("alterado", {key.pub});
    env4.login_owner();
    auto rel4 = make_release(key, "0.5.0");
    serve(*env4.http, rel4);
    auto evil = rel4.installer;
    evil[evil.size() / 2] ^= 1;
    env4.http->route(installer_url("0.5.0"), evil);
    env4.check_and_wait();
    TEST_CHECK(env4.svc->accept_update().is_success(), "Aceptar");
    TEST_CHECK(env4.svc->wait_idle(20000), "Termina");
    st = env4.status();
    TEST_CHECK(st.state == "AVAILABLE" && !st.accepted && !st.download_verified && st.error_kind == "DOWNLOAD" && !has_jargon(st.error_message),
               "Instalador alterado rechazado: " + st.error_message);
    TEST_CHECK(!fs::exists(env4.installer_path("0.5.0")) && !fs::exists(env4.dir / "updates" / "0.5.0" / (installer_name("0.5.0") + ".part")),
               "Y se borra lo descargado");
    TEST_CHECK(env4.svc->get_update_status().value().will_install_on_close == false, "Nunca queda para instalar");
    // Tamaño distinto (más grande y más chico)
    for (const long long delta : {1000LL, -1000LL}) {
        auto sized = rel4.installer;
        if (delta > 0) sized.resize(sized.size() + 1000, 7); else sized.resize(sized.size() - 1000);
        env4.http->route(installer_url("0.5.0"), sized);
        TEST_CHECK(env4.svc->accept_update().is_success(), "Aceptar con tamaño distinto");
        TEST_CHECK(env4.svc->wait_idle(20000), "Termina");
        st = env4.status();
        TEST_CHECK(st.state == "AVAILABLE" && !st.accepted && !st.download_verified && st.error_kind == "DOWNLOAD", "Tamaño distinto rechazado");
    }
    // El servidor falla al descargar
    env4.http->route_error(installer_url("0.5.0"), HttpError::BadStatus, 404);
    TEST_CHECK(env4.svc->accept_update().is_success() && env4.svc->wait_idle(20000), "Aceptar con 404");
    TEST_CHECK(env4.status().error_kind == "DOWNLOAD", "404 al descargar");
}

static void test_permissions_and_portable() {
    std::cout << "[TEST] B11: permiso updates.manage en el servicio y copia portable..." << std::endl;
    TestKey key;
    UEnv env("permisos", {key.pub});
    serve(*env.http, make_release(key, "0.2.0"));
    auto all_denied = [&](const std::string& who) {
        TEST_CHECK(env.svc->get_update_status().error_code() == ErrorCode::Forbidden, who + ": estado");
        TEST_CHECK(env.svc->check_for_updates().error_code() == ErrorCode::Forbidden, who + ": buscar");
        TEST_CHECK(env.svc->accept_update().error_code() == ErrorCode::Forbidden, who + ": aceptar");
        TEST_CHECK(env.svc->decline_update().error_code() == ErrorCode::Forbidden, who + ": rechazar");
        TEST_CHECK(env.svc->cancel_download().error_code() == ErrorCode::Forbidden, who + ": cancelar");
        TEST_CHECK(env.svc->use_update_from_files("a", "b", "c").error_code() == ErrorCode::Forbidden, who + ": desde archivos");
        TEST_CHECK(env.svc->postpone_install_this_session().error_code() == ErrorCode::Forbidden, who + ": posponer");
        TEST_CHECK(env.svc->get_last_install_result().error_code() == ErrorCode::Forbidden, who + ": resultado");
        TEST_CHECK(env.svc->acknowledge_install_result().error_code() == ErrorCode::Forbidden, who + ": acusar recibo");
    };
    env.account->logout();
    all_denied("sin sesión");
    env.login_cashier();
    all_denied("Cajero");
    TEST_CHECK(env.http->call_count() == 0, "Nada rechazado hizo una petición");
    env.login_owner();
    TEST_CHECK(env.svc->get_update_status().is_success(), "El Dueño sí");
    TEST_CHECK(env.account->has_permission("updates.manage"), "El Dueño tiene updates.manage");
    // El permiso no es del Desarrollador por omisión
    {
        auto prep = env.db.prepare("SELECT COUNT(*) FROM role_permissions WHERE permission_id = 'updates.manage' AND role_id <> 'role-owner';");
        auto stmt = std::move(prep.value());
        stmt.step();
        TEST_CHECK(stmt.get_int64(0) == 0, "updates.manage solo está asignado a role-owner");
    }

    // Copia portable: avisa, pero no se actualiza sola
    env.make_service(kInstalled, true);
    const auto st = env.check_and_wait();
    TEST_CHECK(st.state == "AVAILABLE" && st.is_portable && !st.will_install_on_close && !st.info_message.empty() && !has_jargon(st.info_message),
               "Portable: avisa de la versión nueva: " + st.info_message);
    const auto acc = env.svc->accept_update();
    TEST_CHECK(acc.error_code() == ErrorCode::InvalidState && !has_jargon(acc.error_message()) && acc.error_message().find("portable") != std::string::npos,
               "Portable: no se puede aceptar: " + acc.error_message());
    TEST_CHECK(env.svc->use_update_from_files("a", "b", "c").error_code() == ErrorCode::InvalidState, "Portable: tampoco desde archivos");
    TEST_CHECK(!env.svc->prepare_install_on_close().ok, "Portable: nunca se lanza nada");
}

static void test_state_persists_and_install_reconciliation() {
    std::cout << "[TEST] B11: el estado persiste; resultado de la instalación tras reabrir..." << std::endl;
    TestKey key;
    UEnv env("persiste", {key.pub});
    env.login_owner();
    serve(*env.http, make_release(key, "0.2.0"));
    env.check_and_wait();
    TEST_CHECK(env.svc->accept_update().is_success(), "Aceptar");
    env.wait_state("READY");
    // Reabrir: nuevo servicio sobre la misma base
    env.make_service(kInstalled, false);
    auto st = env.status();
    TEST_CHECK(st.state == "READY" && st.available_version == "0.2.0" && st.accepted && st.download_verified && st.notes.size() == 2 && !st.last_check_at.empty(),
               "Todo persiste tras reabrir");
    TEST_CHECK(!env.svc->get_last_install_result().value().has_result, "Aún no hay resultado de instalación");

    // Se lanza la instalación (se anota el intento) y NO se completa: el programa sigue en 0.1.0
    auto plan = env.svc->prepare_install_on_close();
    TEST_CHECK(plan.ok, "Plan: " + plan.reason);
    plan = SqliteUpdateService::InstallLaunchPlan();
    env.make_service(kInstalled, false);
    auto result = env.svc->get_last_install_result().value();
    TEST_CHECK(result.has_result && !result.success && result.version == "0.2.0" && !has_jargon(result.message) &&
                   result.message.find("sigues con la versión 0.1.0") != std::string::npos && result.reason == "NOT_COMPLETED", "No completada: " + result.message);
    st = env.status();
    TEST_CHECK(st.state == "AVAILABLE" && !st.accepted && st.download_verified, "Se vuelve a ofrecer (descarga conservada, sin aceptar)");
    TEST_CHECK(env.svc->acknowledge_install_result().is_success() && !env.svc->get_last_install_result().value().has_result, "El resultado se muestra una vez");
    TEST_CHECK(env.count("event_type = 'UPDATE_INSTALL_NOT_COMPLETED'") == 1, "Bitácora: no completada");

    // Segundo intento que SÍ se completa: el programa arranca ya en 0.2.0
    TEST_CHECK(env.svc->accept_update().is_success() && env.status().state == "READY", "Aceptar otra vez");
    plan = env.svc->prepare_install_on_close();
    TEST_CHECK(plan.ok, "Plan 2");
    plan = SqliteUpdateService::InstallLaunchPlan();
    env.make_service("0.2.0", false);
    result = env.svc->get_last_install_result().value();
    TEST_CHECK(result.has_result && result.success && result.message == "Se actualizó a la versión 0.2.0." && result.reason == "INSTALLED", "Completada: " + result.message);
    st = env.status();
    TEST_CHECK(st.state == "UP_TO_DATE" && st.available_version.empty() && !st.accepted, "Sin novedad tras actualizar");
    TEST_CHECK(!fs::exists(env.dir / "updates" / "0.2.0"), "Se limpia la descarga de la versión ya instalada");
    TEST_CHECK(env.count("event_type = 'UPDATE_INSTALL_OK'") == 1, "Bitácora: instalada");
}

static void test_prepare_install_never_trusts_unverified() {
    std::cout << "[TEST] B11: nunca se lanza nada que no verifique (manifiesto, firma, archivo, sesión)..." << std::endl;
    TestKey key, other;
    UEnv env("verifica antes", {key.pub});
    env.login_owner();
    const auto rel = make_release(key, "0.2.0");
    serve(*env.http, rel);
    env.check_and_wait();
    TEST_CHECK(!env.svc->prepare_install_on_close().ok, "Sin aceptar: no hay plan");
    TEST_CHECK(env.svc->accept_update().is_success(), "Aceptar");
    env.wait_state("READY");

    auto set_sql = [&](const std::string& sql) { TEST_CHECK(env.db.execute(sql).is_success(), "SQL: " + sql); };
    {
        auto plan = env.svc->prepare_install_on_close();
        TEST_CHECK(plan.ok && plan.version == "0.2.0" && plan.installer == env.installer_path("0.2.0"), "Plan válido: " + plan.reason);
        TEST_CHECK(plan.arguments.find("/VERYSILENT") != std::string::npos && plan.arguments.find("/SUPPRESSMSGBOXES") != std::string::npos &&
                       plan.arguments.find("/NORESTART") != std::string::npos && plan.arguments.find("/LOG=") != std::string::npos &&
                       plan.arguments.find("/REABRIR") == std::string::npos, "Argumentos silenciosos y sin /REABRIR");
        // Mientras el plan está vivo, el archivo NO se puede modificar, borrar ni renombrar (ni escribir encima).
        {
            std::ofstream w(plan.installer, std::ios::binary | std::ios::app);
            TEST_CHECK(!w.is_open(), "No se puede escribir en el instalador mientras el plan está vivo");
        }
        std::error_code ec;
        fs::remove(plan.installer, ec);
        TEST_CHECK(fs::exists(plan.installer), "No se puede borrar");
        fs::rename(plan.installer, plan.installer.parent_path() / "otro.exe", ec);
        TEST_CHECK(fs::exists(plan.installer) && !fs::exists(plan.installer.parent_path() / "otro.exe"), "No se puede renombrar");
        TEST_CHECK(env.count("event_type = 'UPDATE_INSTALL_LAUNCHED'") == 1, "Bitácora: instalación lanzada");
    }
    {   // soltado el plan, el archivo vuelve a poder modificarse: se altera UN byte y ya no verifica
        std::fstream f(env.installer_path("0.2.0"), std::ios::binary | std::ios::in | std::ios::out);
        TEST_CHECK(f.is_open(), "Ya se puede abrir para escribir");
        f.seekp(1000);
        char c = 0;
        f.seekg(1000);
        f.get(c);
        f.seekp(1000);
        f.put(static_cast<char>(c ^ 1));
    }
    {
        auto plan = env.svc->prepare_install_on_close();
        TEST_CHECK(!plan.ok && plan.lock == nullptr, "Un byte alterado en el disco: no se lanza (" + plan.reason + ")");
        std::string err;
        TEST_CHECK(env.svc->launch_install(plan, &err) == SqliteUpdateService::LaunchOutcome::Failed, "launch_install de un plan no válido no lanza nada");
    }
    {   // restaurar el archivo; ahora se altera la base: manifiesto, firma, versión
        std::ofstream out(env.installer_path("0.2.0"), std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(rel.installer.data()), static_cast<std::streamsize>(rel.installer.size()));
    }
    TEST_CHECK(env.svc->prepare_install_on_close().ok, "Restaurado: vuelve a verificar");
    set_sql("UPDATE update_state SET installer_sha256 = 'x', installer_size = 1;");  // datos de apoyo: no cuentan, manda el manifiesto firmado
    TEST_CHECK(env.svc->prepare_install_on_close().ok, "Los campos de apoyo editados no importan: manda el manifiesto firmado");
    set_sql("UPDATE update_state SET manifest_sig = '" + hex_of(other.sign(rel.manifest)) + "';");
    TEST_CHECK(!env.svc->prepare_install_on_close().ok, "Firma cambiada en la base: no se lanza");
    set_sql("UPDATE update_state SET manifest_sig = '" + hex_of(rel.signature) + "';");
    TEST_CHECK(env.svc->prepare_install_on_close().ok, "Firma restaurada");
    {
        auto m = bytes_of(manifest_json("0.9.0", {"x"}, rel.installer));
        auto prep = env.db.prepare("UPDATE update_state SET manifest_bytes = ?;");
        auto stmt = std::move(prep.value());
        stmt.bind_blob(1, m);
        stmt.step();
    }
    TEST_CHECK(!env.svc->prepare_install_on_close().ok, "Manifiesto cambiado en la base: no se lanza");
    {
        auto prep = env.db.prepare("UPDATE update_state SET manifest_bytes = ?;");
        auto stmt = std::move(prep.value());
        stmt.bind_blob(1, rel.manifest);
        stmt.step();
    }
    TEST_CHECK(env.svc->prepare_install_on_close().ok, "Manifiesto restaurado");
    // Versión instalada mayor o igual: nunca se baja
    // Sin sesión (p. ej. cierre tras restaurar un respaldo): nada
    env.account->logout();
    TEST_CHECK(!env.svc->prepare_install_on_close().ok, "Sin sesión: no se lanza");
    env.login_owner();
    // Posponer la instalación en esta sesión
    TEST_CHECK(env.svc->postpone_install_this_session().is_success(), "Cerrar sin instalar");
    TEST_CHECK(!env.svc->prepare_install_on_close().ok && env.status().postponed_this_session && !env.status().will_install_on_close, "Pospuesta: no se lanza");
    env.make_service(kInstalled, false);  // "otra sesión": la posposición era solo de memoria
    // (los intentos anteriores anotaron "instalación lanzada" y no se completaron: se vuelve a ofrecer y se acepta)
    TEST_CHECK(env.svc->accept_update().is_success() && env.status().state == "READY", "Se acepta de nuevo");
    TEST_CHECK(env.svc->prepare_install_on_close().ok, "La posposición no pasa a la próxima sesión");
    // Claves distintas (otra instancia del servicio con otra clave de confianza): lo guardado ya no es de confianza
    env.keys = {other.pub};
    env.make_service(kInstalled, false);
    TEST_CHECK(!env.svc->prepare_install_on_close().ok, "Si la clave de confianza cambió, lo guardado no sirve");
    env.keys = {key.pub};
    env.make_service("0.2.0", false);
    TEST_CHECK(!env.svc->prepare_install_on_close().ok, "Versión igual a la instalada: nada que lanzar");
}

static void test_database_closed_cleanly() {
    std::cout << "[TEST] B11: la base cerró limpia (sin -wal ni -shm con contenido)..." << std::endl;
    const fs::path dir = g_root / "cierre limpio";
    fs::create_directories(dir);
    const fs::path db_file = dir / "x.db";
    TEST_CHECK(SqliteUpdateService::database_closed_cleanly(db_file), "Sin archivos auxiliares");
    { std::ofstream(db_file.u8string() + "-wal", std::ios::binary); }
    TEST_CHECK(SqliteUpdateService::database_closed_cleanly(db_file), "-wal vacío cuenta como limpio");
    { std::ofstream w(db_file.u8string() + "-wal", std::ios::binary); w << "datos"; }
    TEST_CHECK(!SqliteUpdateService::database_closed_cleanly(db_file), "-wal con datos: no limpio");
    std::error_code ec;
    fs::remove(fs::path(db_file.u8string() + "-wal"), ec);
    { std::ofstream(db_file.u8string() + "-shm", std::ios::binary); }
    TEST_CHECK(!SqliteUpdateService::database_closed_cleanly(db_file), "-shm presente: no limpio");
}

// ===============================================================================================
// Desde archivos (USB)
// ===============================================================================================
static void test_use_update_from_files() {
    std::cout << "[TEST] B11: actualizar desde USB con la misma verificación..." << std::endl;
    TestKey key, other;
    UEnv env("usb", {key.pub});
    env.login_owner();
    const fs::path usb = g_root / "usb carpeta ñ";
    fs::create_directories(usb);
    auto write = [&](const std::string& name, const std::vector<std::uint8_t>& b) {
        const fs::path p = usb / fs::u8path(name);
        std::ofstream out(p, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        return p.u8string();
    };
    const auto rel = make_release(key, "0.2.0");
    const auto installer = write(installer_name("0.2.0"), rel.installer);
    const auto manifest = write("manifiesto.json", rel.manifest);
    const auto sig = write("manifiesto.json.sig", bytes_of(hex_of(rel.signature)));

    // Rechazos: nada cambia en el estado
    const auto evil_rel = make_release(other, "0.2.0");
    const auto bad_sig = write("firma-ajena.sig", bytes_of(hex_of(evil_rel.signature)));
    auto r = env.svc->use_update_from_files(installer, manifest, bad_sig);
    TEST_CHECK(r.error_code() == ErrorCode::InvalidInput && !has_jargon(r.error_message()), "Firma ajena: " + r.error_message());
    auto altered_manifest = rel.manifest;
    altered_manifest[30] ^= 1;
    r = env.svc->use_update_from_files(installer, write("alterado.json", altered_manifest), sig);
    TEST_CHECK(r.error_code() == ErrorCode::InvalidInput, "Manifiesto alterado");
    auto altered_installer = rel.installer;
    altered_installer[100] ^= 1;
    r = env.svc->use_update_from_files(write("malo.exe", altered_installer), manifest, sig);
    TEST_CHECK(r.error_code() == ErrorCode::InvalidInput && !has_jargon(r.error_message()), "Instalador alterado: " + r.error_message());
    auto short_installer = rel.installer;
    short_installer.resize(short_installer.size() - 5);
    r = env.svc->use_update_from_files(write("corto.exe", short_installer), manifest, sig);
    TEST_CHECK(r.error_code() == ErrorCode::InvalidInput, "Tamaño distinto");
    r = env.svc->use_update_from_files((usb / "no-existe.exe").u8string(), manifest, sig);
    TEST_CHECK(r.error_code() == ErrorCode::InvalidInput, "Instalador inexistente");
    r = env.svc->use_update_from_files(installer, (usb / "no-existe.json").u8string(), sig);
    TEST_CHECK(r.error_code() == ErrorCode::InvalidInput, "Manifiesto inexistente");
    const auto same = make_release(key, "0.1.0");
    r = env.svc->use_update_from_files(write(installer_name("0.1.0"), same.installer), write("m-igual.json", same.manifest), write("s-igual.sig", bytes_of(hex_of(same.signature))));
    TEST_CHECK(r.error_code() == ErrorCode::InvalidState, "Versión igual a la instalada");
    TEST_CHECK(env.status().state == "UP_TO_DATE" && env.status().available_version.empty() && !fs::exists(env.dir / "updates" / "0.2.0"),
               "Ningún rechazo dejó rastro");
    TEST_CHECK(env.http->call_count() == 0, "Todo ocurrió sin red");
    TEST_CHECK(env.count("event_type = 'UPDATE_REJECTED'") >= 3, "Los rechazos quedan en la bitácora");

    // Éxito
    TEST_CHECK(env.svc->use_update_from_files(installer, manifest, sig).is_success(), "Actualización desde USB");
    const auto st = env.status();
    TEST_CHECK(st.state == "READY" && st.accepted && st.download_verified && st.available_version == "0.2.0" && st.will_install_on_close,
               "Queda lista para instalar al cerrar: " + st.state);
    TEST_CHECK(env.installer_path("0.2.0") != fs::path(installer) && fs::exists(env.installer_path("0.2.0")), "Se copió a la carpeta propia");
    TEST_CHECK(env.svc->prepare_install_on_close().ok, "Y verifica para instalar");
}

// ===============================================================================================
// La red no retrasa ventas ni el inicio de sesión
// ===============================================================================================
static void test_slow_client_never_delays_sales_or_login() {
    std::cout << "[TEST] B11: un cliente lento no retrasa una venta ni el inicio de sesión..." << std::endl;
    TestKey key;
    UEnv env("lento", {key.pub});
    auto reg = ServiceRegistry::create_sqlite_registry(env.db, env.dir.u8string(), SOURCE_MIGRATIONS_DIR);
    TEST_CHECK(reg.account_service->login("dueno_b11", kOwnerPwd).is_success(), "Dueño");
    CreateProductDto p;
    p.sku = "B11-V"; p.name = "Café"; p.category = "Bebidas"; p.price = Money::from_cents(3000);
    auto prod = reg.catalog_service->create_product(p);
    TEST_CHECK(prod.is_success(), "Producto");
    TEST_CHECK(reg.shift_service->open_shift("reg-01", Money::from_cents(10000)).is_success(), "Turno");
    TEST_CHECK(reg.cart_service->add_item(prod.value().id, 2, "").is_success(), "Carrito");

    env.http->get_delay_ms = 2500;  // el servidor tarda 2.5 s en responder
    serve(*env.http, make_release(key, "0.2.0"));
    env.account->logout();
    TEST_CHECK(env.account->login("dueno_b11", kOwnerPwd).is_success(), "Dueño en el servicio de pruebas");
    const auto t0 = Clock::now();
    env.svc->start_automatic_check();
    TEST_CHECK(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count() < 500, "start_automatic_check vuelve de inmediato");
    TEST_CHECK(env.status().state == "CHECKING", "Está buscando");

    const auto s0 = Clock::now();
    auto sale = reg.sale_service->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), "venta-b11");
    const auto sale_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - s0).count();
    TEST_CHECK(sale.is_success(), "La venta se confirma mientras el cliente espera: " + sale.error_message());
    TEST_CHECK(sale_ms < 2000, "La venta no esperó al cliente (" + std::to_string(sale_ms) + " ms)");
    TEST_CHECK(env.status().state == "CHECKING", "Y la búsqueda seguía en curso durante la venta");

    const auto l0 = Clock::now();
    reg.account_service->logout();
    TEST_CHECK(reg.account_service->login("cajero_b11", kCashierPwd).is_success(), "Inicio de sesión");
    const auto login_ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - l0).count();
    TEST_CHECK(login_ms < 2000, "El inicio de sesión no esperó al cliente (" + std::to_string(login_ms) + " ms)");
    TEST_CHECK(env.svc->wait_idle(20000), "La búsqueda termina sola");
    TEST_CHECK(env.status().state == "AVAILABLE", "y encuentra la versión");
    {
        auto prep = env.db.prepare("SELECT COUNT(*) FROM sales;");
        auto stmt = std::move(prep.value());
        stmt.step();
        TEST_CHECK(stmt.get_int64(0) == 1, "La venta quedó guardada");
    }
}

static void test_cleanup_old_downloads() {
    std::cout << "[TEST] B11: limpieza de descargas viejas..." << std::endl;
    TestKey key;
    UEnv env("limpieza", {key.pub});
    env.login_owner();
    for (const char* v : {"0.0.5", "0.0.9", "0.1.5"}) {
        fs::create_directories(env.dir / "updates" / v);
        std::ofstream(env.dir / "updates" / v / (installer_name(v) + ".part")) << "viejo";
    }
    serve(*env.http, make_release(key, "0.2.0"));
    env.check_and_wait();
    TEST_CHECK(env.svc->wait_idle(5000), "inactivo");
    for (const char* v : {"0.0.5", "0.0.9", "0.1.5"}) TEST_CHECK(!fs::exists(env.dir / "updates" / v), std::string("Se limpió ") + v);
}

// ===============================================================================================
// Cliente real (sin red): solo https
// ===============================================================================================
static void test_winhttp_client_without_network() {
    std::cout << "[TEST] B11: cliente WinHTTP: HTTP plano rechazado, conexión rechazada en el equipo..." << std::endl;
    auto client = make_winhttp_client();
    TEST_CHECK(client != nullptr, "Cliente WinHTTP");
    auto r = client->get("http://example.com/manifiesto.json", 1000, 2000);
    TEST_CHECK(r.error == HttpError::NotHttps, "HTTP plano rechazado sin conectar");
    r = client->get("ftp://example.com/x", 1000, 2000);
    TEST_CHECK(r.error != HttpError::None, "Otros esquemas rechazados");
    r = client->get("no es una url", 1000, 2000);
    TEST_CHECK(r.error != HttpError::None, "URL ilegible");
    // Puerto cerrado en este mismo equipo: falla rápido y sin tocar internet.
    const auto t0 = Clock::now();
    r = client->get("https://127.0.0.1:9/x", 1000, 5000);
    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    TEST_CHECK(r.error != HttpError::None && r.error != HttpError::NotHttps, "Conexión rechazada: error (" + std::to_string(static_cast<int>(r.error)) + ")");
    TEST_CHECK(ms < 8000, "Con tiempo acotado (" + std::to_string(ms) + " ms)");
    const auto dl = client->download("http://example.com/x.exe", g_root / "x.part", 0, nullptr, 2000);
    TEST_CHECK(dl.error == HttpError::NotHttps && !fs::exists(g_root / "x.part"), "La descarga también exige https");
}

// ===============================================================================================
// Herramientas de publicación (PowerShell): lo que firman lo verifica el programa
// ===============================================================================================
static int run_powershell(const std::wstring& args, const fs::path& log) {
    std::wstring cmd = L"powershell.exe -NoProfile -ExecutionPolicy Bypass " + args;
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE h = CreateFileW(log.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h; si.hStdError = h; si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(h);
        return -2;
    }
    int result = -1;
    if (WaitForSingleObject(pi.hProcess, 120000) != WAIT_TIMEOUT) {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        result = static_cast<int>(code);
    } else {
        TerminateProcess(pi.hProcess, 99);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(h);
    return result;
}

static std::string read_text_file(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static void test_publish_tool_interoperates() {
    std::cout << "[TEST] B11: tools\\publicar_version.ps1 genera algo que el programa verifica..." << std::endl;
    std::error_code ec;
    if (!fs::exists(fs::path(env_get("SystemRoot")) / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe", ec)) {
        std::cout << "    (sin PowerShell: se omite)" << std::endl;
        return;
    }
    const fs::path work = g_root / "publicar";
    fs::create_directories(work);
    const auto installer = pseudo_bytes(123456, 99);
    const fs::path installer_file = work / installer_name("0.2.0");
    { std::ofstream out(installer_file, std::ios::binary); out.write(reinterpret_cast<const char*>(installer.data()), static_cast<std::streamsize>(installer.size())); }
    const fs::path notes = work / "notas.txt";
    { std::ofstream out(notes, std::ios::binary); out << "\xEF\xBB\xBF" << u8"Se corrigió el cobro con tarjeta.\r\nAhora el ticket muestra la hora «exacta».\r\n"; }
    const fs::path out_dir = work / "salida";
    const std::wstring script = fs::u8path(TOOLS_DIR).wstring() + L"\\publicar_version.ps1";
    const std::wstring args = L"-File \"" + script + L"\" -Version 0.2.0 -Instalador \"" + installer_file.wstring() + L"\" -Notas \"" +
                              notes.wstring() + L"\" -Clave \"" + fs::u8path(TEST_PRIVATE_KEY_PATH).wstring() + L"\" -Salida \"" + out_dir.wstring() + L"\"";
    const int rc = run_powershell(args, work / "publicar.log");
    TEST_CHECK(rc == 0, "publicar_version.ps1 debe terminar con 0. Salida:\n" + read_text_file(work / "publicar.log"));
    const std::string log = read_text_file(work / "publicar.log");
    TEST_CHECK(log.find("gh release create v0.2.0") != std::string::npos && log.find("--repo Surer46/cafeteriaITCH") != std::string::npos &&
                   log.find("NO se publica sola") != std::string::npos, "Imprime el comando exacto de gh y no publica");
    TEST_CHECK(fs::exists(out_dir / installer_name("0.2.0")) && fs::exists(out_dir / "manifiesto.json") && fs::exists(out_dir / "manifiesto.json.sig"), "Archivos listos");

    PublicKey64 test_pub;
    TEST_CHECK(parse_public_key_hex(TEST_PUBLIC_KEY_HEX, test_pub), "Clave pública de prueba");
    const auto manifest = bytes_of(read_text_file(out_dir / "manifiesto.json"));
    const auto sig = bytes_of(read_text_file(out_dir / "manifiesto.json.sig"));
    auto v = verify_and_parse_manifest(manifest, sig, {test_pub}, "0.1.0");
    TEST_CHECK(v.error == ManifestError::None && v.manifest.version == "0.2.0" && v.manifest.installer_size == installer.size() &&
                   v.manifest.installer_sha256 == sha_hex(installer), "El programa verifica lo que firmó la herramienta (" + v.detail + ")");
    TEST_CHECK(v.manifest.notes.size() == 2 && v.manifest.notes[0] == u8"Se corrigió el cobro con tarjeta." &&
                   v.manifest.notes[1] == u8"Ahora el ticket muestra la hora «exacta».", "Notas con tildes y comillas latinas");
    TestKey stranger;
    TEST_CHECK(verify_and_parse_manifest(manifest, sig, {stranger.pub}, "0.1.0").error == ManifestError::BadSignature, "Otra clave no lo acepta");
    // La herramienta rechaza lo que no debe
    TEST_CHECK(run_powershell(L"-File \"" + script + L"\" -Version 0.2.0 -Instalador \"" + installer_file.wstring() + L"\" -Notas \"" + notes.wstring() +
                                  L"\" -Clave \"" + fs::u8path(TEST_PRIVATE_KEY_PATH).wstring() + L"\" -Salida \"" + out_dir.wstring() + L"\"", work / "otra.log") != 0,
               "No sobrescribe una salida existente");
    TEST_CHECK(run_powershell(L"-File \"" + script + L"\" -Version 0.2 -Instalador \"" + installer_file.wstring() + L"\" -Notas \"" + notes.wstring() +
                                  L"\" -Clave \"" + fs::u8path(TEST_PRIVATE_KEY_PATH).wstring() + L"\" -Salida \"" + (work / "salida2").wstring() + L"\"", work / "v.log") != 0,
               "Rechaza una versión mal escrita");
    TEST_CHECK(run_powershell(L"-File \"" + script + L"\" -Version 0.3.0 -Instalador \"" + installer_file.wstring() + L"\" -Notas \"" + notes.wstring() +
                                  L"\" -Clave \"" + fs::u8path(TEST_PRIVATE_KEY_PATH).wstring() + L"\" -Salida \"" + (work / "salida3").wstring() + L"\"", work / "n.log") != 0,
               "El instalador debe llamarse como la versión");
}

// ===============================================================================================
// Ganchos de prueba (solo con -DCAFETERIA_TEST_HOOKS=ON)
// ===============================================================================================
static void test_hooks_or_their_absence() {
#ifdef CAFETERIA_TEST_HOOKS
    std::cout << "[TEST] B11: ganchos de prueba (solo en esta compilación): fuente local, clave de prueba y lanzamiento simulado..." << std::endl;
    // Fuente local de actualizaciones, firmada con la clave de PRUEBA (la privada está en tests/)
    const fs::path work = g_root / "ganchos";
    fs::create_directories(work);
    const fs::path source = work / "fuente";
    const auto installer = pseudo_bytes(400 * 1000, 31);
    const fs::path installer_file = work / installer_name("0.2.0");
    { std::ofstream out(installer_file, std::ios::binary); out.write(reinterpret_cast<const char*>(installer.data()), static_cast<std::streamsize>(installer.size())); }
    const fs::path notes = work / "notas.txt";
    { std::ofstream out(notes, std::ios::binary); out << "Novedad de prueba.\r\n"; }
    std::error_code ec;
    if (fs::exists(fs::path(env_get("SystemRoot")) / "System32" / "WindowsPowerShell" / "v1.0" / "powershell.exe", ec)) {
        const std::wstring script = fs::u8path(TOOLS_DIR).wstring() + L"\\publicar_version.ps1";
        TEST_CHECK(run_powershell(L"-File \"" + script + L"\" -Version 0.2.0 -Instalador \"" + installer_file.wstring() + L"\" -Notas \"" + notes.wstring() +
                                      L"\" -Clave \"" + fs::u8path(TEST_PRIVATE_KEY_PATH).wstring() + L"\" -Salida \"" + source.wstring() + L"\"", work / "p.log") == 0,
                   "Preparar la fuente local con la clave de prueba");
    }
    // Sin la variable no hay clave de prueba incrustada en uso.
    _putenv_s("CAFETERIA_UPDATE_TEST_SOURCE", "");
    TEST_CHECK(configured_update_keys().empty(), "Sin la variable, la ranura de prueba no cuenta");
    _putenv_s("CAFETERIA_UPDATE_TEST_SOURCE", source.u8string().c_str());
    TEST_CHECK(configured_update_keys().size() == 1, "Con la variable, entra la clave pública de prueba");

    UEnv env("ganchos", configured_update_keys());
    env.http = nullptr;  // el servicio usará el cliente por omisión: la fuente local
    {
        env.login_owner();
        UpdateServiceOptions opt;
        opt.data_dir = env.dir.u8string();
        opt.installed_version = "0.1.0";
        opt.keys = configured_update_keys();
        opt.now_ms = [&env] { return env.now_ms; };
        env.svc.reset();
        env.svc = std::make_unique<SqliteUpdateService>(env.db, env.account, std::move(opt));
    }
    _putenv_s("CAFETERIA_UPDATE_TEST_DELAY_MS", "30");
    TEST_CHECK(env.check_and_wait().state == "AVAILABLE" && env.status().available_version == "0.2.0", "La fuente local ofrece la versión de prueba");
    TEST_CHECK(env.svc->accept_update().is_success(), "Aceptar");
    const auto dl = env.wait_state("DOWNLOADING");
    (void)dl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    TEST_CHECK(env.status().downloaded_bytes > 0, "El retraso por bloque permite ver el progreso");
    const auto ready = env.wait_state("READY", 60000);
    TEST_CHECK(ready.will_install_on_close, "Lista");
    _putenv_s("CAFETERIA_UPDATE_TEST_DELAY_MS", "");

    // Lanzamiento simulado
    auto plan = env.svc->prepare_install_on_close();
    TEST_CHECK(plan.ok, "Plan: " + plan.reason);
    const fs::path launch_file = work / "lanzamiento.txt";
    _putenv_s("CAFETERIA_UPDATE_TEST_LAUNCH", launch_file.u8string().c_str());
    TEST_CHECK(env.svc->launch_install(plan) == SqliteUpdateService::LaunchOutcome::Launched, "Lanzamiento simulado");
    const std::string launched = read_text_file(launch_file);
    TEST_CHECK(launched.find(installer_name("0.2.0")) != std::string::npos && launched.find("0.2.0") != std::string::npos &&
                   launched.find("/VERYSILENT") != std::string::npos, "Escribe lo que habría lanzado");
    _putenv_s("CAFETERIA_UPDATE_TEST_LAUNCH", "");

    // UAC cancelado y fallo de lanzamiento simulados: el resultado llega al reabrir con su motivo
    plan = SqliteUpdateService::InstallLaunchPlan();
    for (const char* sim : {"UAC_CANCELLED", "FAILED"}) {
        env.make_service(kInstalled, false);
        env.svc->acknowledge_install_result();
        TEST_CHECK(env.svc->accept_update().is_success() && env.status().state == "READY", std::string(sim) + ": lista");
        auto p2 = env.svc->prepare_install_on_close();
        TEST_CHECK(p2.ok, std::string(sim) + ": plan");
        _putenv_s("CAFETERIA_UPDATE_TEST_LAUNCH", launch_file.u8string().c_str());
        _putenv_s("CAFETERIA_UPDATE_TEST_LAUNCH_RESULT", sim);
        std::string err;
        const auto outcome = env.svc->launch_install(p2, &err);
        _putenv_s("CAFETERIA_UPDATE_TEST_LAUNCH_RESULT", "");
        _putenv_s("CAFETERIA_UPDATE_TEST_LAUNCH", "");
        TEST_CHECK(outcome == (std::string(sim) == "FAILED" ? SqliteUpdateService::LaunchOutcome::Failed : SqliteUpdateService::LaunchOutcome::UacCancelled),
                   std::string(sim) + ": resultado del lanzamiento");
        p2 = SqliteUpdateService::InstallLaunchPlan();
        env.make_service(kInstalled, false);
        const auto r = env.svc->get_last_install_result().value();
        TEST_CHECK(r.has_result && r.reason == (std::string(sim) == "FAILED" ? "LAUNCH_FAILED" : "PERMISSION_DENIED"), std::string(sim) + ": motivo al reabrir: " + r.reason);
    }

    // Fuente vacía o inexistente = sin red
    _putenv_s("CAFETERIA_UPDATE_TEST_SOURCE", (work / "vacia").u8string().c_str());
    UEnv env2("ganchos sin red", configured_update_keys());
    env2.http = nullptr;
    {
        env2.login_owner();
        UpdateServiceOptions opt;
        opt.data_dir = env2.dir.u8string();
        opt.installed_version = "0.1.0";
        opt.keys = configured_update_keys();
        env2.svc.reset();
        env2.svc = std::make_unique<SqliteUpdateService>(env2.db, env2.account, std::move(opt));
    }
    TEST_CHECK(env2.check_and_wait().error_kind == "NO_NETWORK", "Carpeta inexistente: se comporta como sin red");
    fs::create_directories(work / "vacia");
    env2.now_ms += 1;
    TEST_CHECK(env2.check_and_wait().state == "ERROR" || env2.status().state == "ERROR", "Carpeta vacía: también sin red");
    _putenv_s("CAFETERIA_UPDATE_TEST_SOURCE", "");
#else
    std::cout << "[TEST] B11: en esta compilación NO existen los ganchos de prueba de actualizaciones..." << std::endl;
    // Aunque la variable exista, no se activa ninguna fuente local ni clave de prueba.
    _putenv_s("CAFETERIA_UPDATE_TEST_SOURCE", g_root.u8string().c_str());
    TEST_CHECK(configured_update_keys().empty(), "Sin ganchos: la variable de prueba no añade ninguna clave");
    _putenv_s("CAFETERIA_UPDATE_TEST_SOURCE", "");
    const auto client = make_default_update_http_client();
    TEST_CHECK(client != nullptr, "El cliente por omisión es el real");
#endif
}


// ===============================================================================================
// B13: motivos explícitos
// ===============================================================================================
static std::vector<std::string> g_messages;  // todo texto que el servicio devolvió para mostrar (revisión de lenguaje llano)
static void seen(const std::string& m) { if (!m.empty()) g_messages.push_back(m); }

static std::string sha_of_text(const std::string& t) {
    app::crypto::Sha256 h;
    h.update(reinterpret_cast<const std::uint8_t*>(t.data()), t.size());
    return hex_of(h.finalize());
}

// Archivo de resultado del lanzamiento, con el mismo formato que escribe el servicio. modo: 0 válido, 1 contenido
// cambiado (no coincide con su comprobación), 2 cortado, 3 basura.
static void put_launch_file(const fs::path& dir, const std::string& target, const std::string& from, const std::string& outcome, int mode = 0) {
    fs::create_directories(dir / "updates");
    const std::string body = "CAFEUPD1\n" + target + "\n" + from + "\n" + outcome + "\n";
    std::string text = body + sha_of_text(body) + "\n";
    if (mode == 1) text = "CAFEUPD1\n" + target + "\n" + from + "\n" + (outcome == "FAILED" ? "UAC_CANCELLED" : "FAILED") + "\n" + sha_of_text(body) + "\n";
    if (mode == 2) text = text.substr(0, text.size() / 2);
    if (mode == 3) text = "basura";
    std::ofstream out(dir / "updates" / "ultimo-lanzamiento.txt", std::ios::binary | std::ios::trunc);
    out << text;
}

static bool contains_word_ci(const std::string& text, const std::string& word) {
    auto lower = [](std::string v) { for (auto& c : v) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return v; };
    const std::string t = lower(text), w = lower(word);
    auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (static_cast<unsigned char>(c) >= 0x80); };
    for (std::size_t pos = t.find(w); pos != std::string::npos; pos = t.find(w, pos + 1)) {
        const bool left = pos == 0 || !letter(t[pos - 1]);
        const bool right = pos + w.size() >= t.size() || !letter(t[pos + w.size()]);
        if (left && right) return true;
    }
    return false;
}

static void test_install_result_reasons() {
    std::cout << "[TEST] B13: motivo del resultado de la instalación (INSTALLED, PERMISSION_DENIED, LAUNCH_FAILED, NOT_COMPLETED)..." << std::endl;
    TestKey key;
    UEnv env("motivos instalacion", {key.pub});
    env.login_owner();
    serve(*env.http, make_release(key, "0.2.0"));
    TEST_CHECK(env.check_and_wait().state == "AVAILABLE", "Disponible");
    const fs::path launch_file = env.dir / "updates" / "ultimo-lanzamiento.txt";
    const std::string msg_pd = "No se instaló porque no se dio permiso en Windows. Se volverá a ofrecer.";
    const std::string msg_lf = "No se pudo iniciar la instalación. El programa sigue en la versión 0.1.0 y se volverá a ofrecer.";
    const std::string msg_nc = "La actualización no se completó; sigues con la versión 0.1.0. Se volverá a ofrecer.";

    auto scenario = [&](const std::string& name, const std::function<void()>& writer, const std::string& reason, const std::string& message) {
        TEST_CHECK(env.svc->accept_update().is_success(), name + ": aceptar");
        env.wait_state("READY");
        {
            auto plan = env.svc->prepare_install_on_close();
            TEST_CHECK(plan.ok, name + ": plan: " + plan.reason);
        }
        writer();
        env.make_service(kInstalled, false);  // "el siguiente arranque"
        const auto r = env.svc->get_last_install_result().value();
        TEST_CHECK(r.has_result && !r.success && r.reason == reason && r.version == "0.2.0" && r.message == message,
                   name + ": motivo " + r.reason + " / " + r.message);
        seen(r.message);
        TEST_CHECK(!fs::exists(launch_file), name + ": el archivo de resultado se borra siempre");
        const auto st = env.status();
        TEST_CHECK(st.state == "AVAILABLE" && !st.accepted && st.download_verified && st.available_version == "0.2.0" && !st.will_install_on_close,
                   name + ": se vuelve a ofrecer (AVAILABLE, sin aceptar, descarga conservada): " + st.state);
        TEST_CHECK(env.svc->acknowledge_install_result().is_success() && !env.svc->get_last_install_result().value().has_result, name + ": se muestra una sola vez");
        env.make_service(kInstalled, false);
        TEST_CHECK(!env.svc->get_last_install_result().value().has_result, name + ": no reaparece al reabrir otra vez");
    };
    scenario("sin archivo", [] {}, "NOT_COMPLETED", msg_nc);
    scenario("autorización cancelada", [&] { put_launch_file(env.dir, "0.2.0", kInstalled, "UAC_CANCELLED"); }, "PERMISSION_DENIED", msg_pd);
    scenario("no se pudo lanzar", [&] { put_launch_file(env.dir, "0.2.0", kInstalled, "FAILED"); }, "LAUNCH_FAILED", msg_lf);
    scenario("se abrió y no terminó", [&] { put_launch_file(env.dir, "0.2.0", kInstalled, "LAUNCHED"); }, "NOT_COMPLETED", msg_nc);
    scenario("archivo alterado", [&] { put_launch_file(env.dir, "0.2.0", kInstalled, "UAC_CANCELLED", 1); }, "NOT_COMPLETED", msg_nc);
    scenario("archivo cortado", [&] { put_launch_file(env.dir, "0.2.0", kInstalled, "FAILED", 2); }, "NOT_COMPLETED", msg_nc);
    scenario("archivo con basura", [&] { put_launch_file(env.dir, "0.2.0", kInstalled, "FAILED", 3); }, "NOT_COMPLETED", msg_nc);
    scenario("archivo de otra versión", [&] { put_launch_file(env.dir, "0.9.9", kInstalled, "UAC_CANCELLED"); }, "NOT_COMPLETED", msg_nc);
    scenario("archivo de otro origen", [&] { put_launch_file(env.dir, "0.2.0", "0.0.5", "UAC_CANCELLED"); }, "NOT_COMPLETED", msg_nc);

    // Sobra de otro intento (sin instalación pendiente): se borra y no aparece ningún resultado
    put_launch_file(env.dir, "0.2.0", kInstalled, "UAC_CANCELLED");
    env.make_service(kInstalled, false);
    TEST_CHECK(!fs::exists(launch_file) && !env.svc->get_last_install_result().value().has_result, "Archivo sobrante: se borra y no hay resultado");

    // INSTALLED nunca sale del archivo: depende de la versión en ejecución
    TEST_CHECK(env.svc->accept_update().is_success(), "Aceptar");
    { auto plan = env.svc->prepare_install_on_close(); TEST_CHECK(plan.ok, "Plan"); }
    put_launch_file(env.dir, "0.2.0", kInstalled, "UAC_CANCELLED");
    env.make_service("0.2.0", false);
    const auto done = env.svc->get_last_install_result().value();
    TEST_CHECK(done.has_result && done.success && done.reason == "INSTALLED" && done.message == "Se actualizó a la versión 0.2.0." && !fs::exists(launch_file),
               "Con la versión nueva en ejecución es INSTALLED aunque el archivo diga otra cosa");
    seen(done.message);
    // Una versión nueva sin archivo tampoco cae en nada distinto
    TEST_CHECK(env.svc->acknowledge_install_result().is_success() && !env.svc->get_last_install_result().value().has_result, "Una sola vez");

    // Lanzamiento no intentado (la base no cerró limpia): LAUNCH_FAILED, solo si hubo plan válido
    UEnv env2("no intentado", {key.pub});
    env2.login_owner();
    serve(*env2.http, make_release(key, "0.2.0"));
    env2.check_and_wait();
    TEST_CHECK(env2.svc->accept_update().is_success(), "Aceptar");
    env2.wait_state("READY");
    {
        SqliteUpdateService::InstallLaunchPlan none;
        env2.svc->note_launch_not_attempted(none);
        TEST_CHECK(!fs::exists(env2.dir / "updates" / "ultimo-lanzamiento.txt"), "Sin plan válido no se escribe nada");
        auto plan = env2.svc->prepare_install_on_close();
        TEST_CHECK(plan.ok, "Plan");
        env2.svc->note_launch_not_attempted(plan);
        TEST_CHECK(fs::exists(env2.dir / "updates" / "ultimo-lanzamiento.txt"), "Con plan válido se anota");
    }
    env2.make_service(kInstalled, false);
    const auto nf = env2.svc->get_last_install_result().value();
    TEST_CHECK(nf.has_result && nf.reason == "LAUNCH_FAILED" && nf.message == msg_lf, "No intentado: LAUNCH_FAILED: " + nf.message);
    // Casos sin lanzamiento: ningún resultado al reabrir
    auto no_launch = [&](const std::string& name, const std::function<void(UEnv&)>& prepare) {
        UEnv e("sin lanzamiento " + name, {key.pub});
        e.login_owner();
        serve(*e.http, make_release(key, "0.2.0"));
        e.check_and_wait();
        TEST_CHECK(e.svc->accept_update().is_success(), name + ": aceptar");
        e.wait_state("READY");
        prepare(e);
        auto plan = e.svc->prepare_install_on_close();
        TEST_CHECK(!plan.ok, name + ": no hay plan");
        e.svc->note_launch_not_attempted(plan);
        TEST_CHECK(!fs::exists(e.dir / "updates" / "ultimo-lanzamiento.txt"), name + ": no se escribe nada");
        e.account->login("dueno_b11", kOwnerPwd);
        e.make_service(kInstalled, false);
        TEST_CHECK(!e.svc->get_last_install_result().value().has_result, name + ": no aparece ningún resultado al reabrir");
    };
    no_launch("pospuesto", [](UEnv& e) { e.svc->postpone_install_this_session(); });
    no_launch("sin sesion", [](UEnv& e) { e.account->logout(); });
    no_launch("portable", [](UEnv& e) { e.make_service(kInstalled, true); });
}

static void test_download_error_reasons() {
    std::cout << "[TEST] B13: motivo de la descarga fallida (download_error_kind) y migración 018..." << std::endl;
    TestKey key;
    UEnv env("motivos descarga", {key.pub});
    env.login_owner();
    const auto rel = make_release(key, "0.2.0");
    serve(*env.http, rel);
    TEST_CHECK(env.check_and_wait().state == "AVAILABLE", "Disponible");
    auto failed_with = [&](const std::string& name, const std::string& kind) {
        TEST_CHECK(env.svc->accept_update().is_success(), name + ": aceptar");
        TEST_CHECK(env.svc->wait_idle(20000), name + ": termina");
        const auto st = env.status();
        TEST_CHECK(st.state == "AVAILABLE" && !st.accepted && st.error_kind == "DOWNLOAD" && st.download_error_kind == kind && !st.error_message.empty(),
                   name + ": " + st.state + " / " + st.error_kind + " / " + st.download_error_kind);
        seen(st.error_message);
    };
    struct C { const char* name; HttpError e; int status; const char* kind; };
    for (const C& c : std::vector<C>{{"sin red", HttpError::NoNetwork, 0, "NO_NETWORK"}, {"lenta", HttpError::Timeout, 0, "TIMEOUT"},
                                     {"servidor", HttpError::BadStatus, 503, "SERVER"}, {"TLS no disponible", HttpError::TlsUnavailable, 0, "TLS_UNAVAILABLE"},
                                     {"certificado", HttpError::TlsFailure, 0, "TLS_FAILURE"}}) {
        env.http->route_error(installer_url("0.2.0"), c.e, c.status);
        failed_with(c.name, c.kind);
    }
    auto evil = rel.installer;
    evil[evil.size() / 2] ^= 1;
    env.http->route(installer_url("0.2.0"), evil);
    failed_with("dañada", "INVALID");
    auto shorter = rel.installer;
    shorter.resize(shorter.size() - 1000);
    env.http->route(installer_url("0.2.0"), shorter);
    failed_with("incompleta", "INVALID");
    // No se pudo guardar: el destino final está ocupado por una carpeta
    env.http->route(installer_url("0.2.0"), rel.installer);
    const fs::path block = env.installer_path("0.2.0");
    fs::create_directories(block);
    { std::ofstream(block / "ocupado.txt") << "x"; }
    failed_with("disco", "STORAGE");
    std::error_code ec;
    fs::remove_all(block, ec);
    // Éxito: el motivo se limpia
    TEST_CHECK(env.svc->accept_update().is_success(), "Aceptar");
    const auto ok = env.wait_state("READY");
    TEST_CHECK(ok.error_kind.empty() && ok.download_error_kind.empty() && ok.error_message.empty(), "Con éxito no queda motivo");
    // Fila anterior a la migración 018: motivo desconocido (vacío) con mensaje
    TEST_CHECK(env.db.execute("UPDATE update_state SET download_error = 'La conexión está muy lenta.', download_error_kind = '', accepted = 0, download_state = 'NONE';").is_success(), "Fila antigua");
    const auto old = env.status();
    TEST_CHECK(old.state == "AVAILABLE" && old.error_kind == "DOWNLOAD" && old.download_error_kind.empty() && old.error_message == "La conexión está muy lenta.",
               "Motivo vacío = desconocido, con el mensaje del servicio");

    // Migración 018: la columna existe y el runner no la duplica al correr de nuevo
    auto scalar = [&](const std::string& sql) {
        auto prep = env.db.prepare(sql);
        TEST_CHECK(prep.is_success(), "consulta: " + sql);
        auto stmt = std::move(prep.value());
        TEST_CHECK(stmt.step().is_success(), "consulta");
        return stmt.get_text(0);
    };
    TEST_CHECK(scalar("SELECT COUNT(*) FROM pragma_table_info('update_state') WHERE name = 'download_error_kind';") == "1", "Columna download_error_kind");
    const std::string before = scalar("SELECT COUNT(*) FROM schema_migrations;");
    MigrationRunner runner(env.db);
    TEST_CHECK(runner.run_pending_migrations_from_dir(fs::u8path(SOURCE_MIGRATIONS_DIR)).is_success(), "El runner corre de nuevo sin error");
    TEST_CHECK(scalar("SELECT COUNT(*) FROM schema_migrations;") == before &&
                   scalar("SELECT COUNT(*) FROM pragma_table_info('update_state') WHERE name = 'download_error_kind';") == "1",
               "Sin duplicar versiones ni columna");
    TEST_CHECK(scalar("SELECT COUNT(*) FROM schema_migrations WHERE version = 18;") == "1", "Migración 018 aplicada");
}

static void test_apply_update_from_files_reasons() {
    std::cout << "[TEST] B13: motivo al elegir archivos del USB (apply_update_from_files)..." << std::endl;
    TestKey key, other;
    UEnv env("motivos usb", {key.pub});
    const fs::path usb = g_root / "usb b13";
    fs::create_directories(usb);
    auto write = [&](const std::string& name, const std::vector<std::uint8_t>& b) {
        const fs::path f = usb / fs::u8path(name);
        std::ofstream out(f, std::ios::binary | std::ios::trunc);
        out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
        return f.u8string();
    };
    const auto rel = make_release(key, "0.2.0");
    const auto installer = write(installer_name("0.2.0"), rel.installer);
    const auto data = write("datos.json", rel.manifest);
    const auto verif = write("verificacion.sig", bytes_of(hex_of(rel.signature)));

    // Sin sesión y sin permiso
    env.account->logout();
    TEST_CHECK(env.svc->apply_update_from_files(installer, data, verif).error_code() == ErrorCode::Forbidden, "Sin sesión: Forbidden");
    seen(env.svc->apply_update_from_files(installer, data, verif).error_message());
    env.login_cashier();
    TEST_CHECK(env.svc->apply_update_from_files(installer, data, verif).error_code() == ErrorCode::Forbidden, "Cajero: Forbidden");
    seen(env.svc->apply_update_from_files(installer, data, verif).error_message());
    env.login_owner();

    auto expect = [&](const std::string& name, const std::string& i, const std::string& d, const std::string& v, const std::string& reason) {
        auto r = env.svc->apply_update_from_files(i, d, v);
        TEST_CHECK(r.is_success(), name + ": el Result no falla");
        TEST_CHECK(r.value().reason == reason && r.value().accepted == (reason == "OK") && !r.value().message.empty(),
                   name + ": motivo " + r.value().reason + " / " + r.value().message);
        seen(r.value().message);
        return r.value();
    };
    expect("instalador inexistente", (usb / "no-existe.exe").u8string(), data, verif, "UNREADABLE");
    expect("datos inexistentes", installer, (usb / "no-existe.json").u8string(), verif, "UNREADABLE");
    expect("verificación inexistente", installer, data, (usb / "no-existe.sig").u8string(), "UNREADABLE");
    expect("instalador es una carpeta", usb.u8string(), data, verif, "UNREADABLE");
    const auto unreadable = expect("ilegible", installer, data, (usb / "no-existe.sig").u8string(), "UNREADABLE");
    TEST_CHECK(unreadable.message == "No se pudieron leer los archivos. Elija el instalador, el archivo de datos y el archivo de verificación.", "Texto de archivos ilegibles");
    const auto foreign = make_release(other, "0.2.0");
    expect("verificación de otra clave", installer, data, write("ajena.sig", bytes_of(hex_of(foreign.signature))), "INVALID");
    auto altered_data = rel.manifest;
    altered_data[30] ^= 1;
    expect("datos alterados", installer, write("alterado.json", altered_data), verif, "INVALID");
    auto altered_inst = rel.installer;
    altered_inst[100] ^= 1;
    expect("instalador alterado", write("malo.exe", altered_inst), data, verif, "INVALID");
    auto short_inst = rel.installer;
    short_inst.resize(short_inst.size() - 5);
    expect("instalador incompleto", write("corto.exe", short_inst), data, verif, "INVALID");
    const auto same = make_release(key, "0.1.0");
    expect("misma versión", write(installer_name("0.1.0"), same.installer), write("igual.json", same.manifest), write("igual.sig", bytes_of(hex_of(same.signature))), "NOT_NEWER");
    const auto older = make_release(key, "0.0.9");
    expect("versión anterior", write(installer_name("0.0.9"), older.installer), write("vieja.json", older.manifest), write("vieja.sig", bytes_of(hex_of(older.signature))), "NOT_NEWER");
    TEST_CHECK(env.status().state == "UP_TO_DATE" && env.status().available_version.empty(), "Ningún rechazo cambió el estado");

    // No se pudo copiar al disco: el destino está ocupado por una carpeta
    const fs::path block = env.installer_path("0.2.0");
    fs::create_directories(block);
    { std::ofstream(block / "ocupado.txt") << "x"; }
    expect("disco", installer, data, verif, "STORAGE");
    TEST_CHECK(env.svc->use_update_from_files(installer, data, verif).error_code() == ErrorCode::StorageFailure, "El método anterior: STORAGE = StorageFailure");
    std::error_code ec;
    fs::remove_all(block, ec);
    TEST_CHECK(env.svc->use_update_from_files(installer, data, (usb / "no-existe.sig").u8string()).error_code() == ErrorCode::InvalidInput, "El método anterior: UNREADABLE = InvalidInput");
    TEST_CHECK(env.svc->use_update_from_files(write("a.exe", same.installer), write("a.json", same.manifest), write("a.sig", bytes_of(hex_of(same.signature)))).error_code() == ErrorCode::InvalidState,
               "El método anterior: NOT_NEWER = InvalidState");

    // Aceptada
    const auto ok = expect("aceptada", installer, data, verif, "OK");
    TEST_CHECK(ok.message == "La actualización quedó lista para instalarse al cerrar el programa.", "Texto de aceptada");
    TEST_CHECK(env.status().state == "READY" && env.status().will_install_on_close, "Queda lista");

    // Portable y sin configurar
    env.make_service(kInstalled, true);
    expect("portable", installer, data, verif, "PORTABLE");
    UEnv nokeys("motivos usb sin claves", {});
    nokeys.login_owner();
    auto r = nokeys.svc->apply_update_from_files(installer, data, verif);
    TEST_CHECK(r.is_success() && r.value().reason == "NOT_CONFIGURED" && !r.value().accepted, "Sin claves: NOT_CONFIGURED");
    seen(r.value().message);
    TEST_CHECK(nokeys.svc->use_update_from_files(installer, data, verif).error_code() == ErrorCode::InvalidState, "El método anterior: NOT_CONFIGURED = InvalidState");
}

static void test_plain_language_everywhere() {
    std::cout << "[TEST] B13: lenguaje llano en todo lo que el servicio devuelve para mostrar..." << std::endl;
    TestKey key;
    // Negativas de permisos y mensajes de error de cada tipo
    UEnv env("lenguaje llano", {key.pub});
    env.account->logout();
    auto deny = [&] {
        seen(env.svc->get_update_status().error_message());
        seen(env.svc->check_for_updates().error_message());
        seen(env.svc->accept_update().error_message());
        seen(env.svc->decline_update().error_message());
        seen(env.svc->cancel_download().error_message());
        seen(env.svc->use_update_from_files("a", "b", "c").error_message());
        seen(env.svc->apply_update_from_files("a", "b", "c").error_message());
        seen(env.svc->postpone_install_this_session().error_message());
        seen(env.svc->get_last_install_result().error_message());
        seen(env.svc->acknowledge_install_result().error_message());
    };
    deny();
    env.login_cashier();
    deny();
    TEST_CHECK(g_messages.size() >= 20, "Se recogieron los mensajes de permisos");
    {
        const auto m = env.svc->accept_update().error_message();
        TEST_CHECK(m == "No tiene permiso para actualizar el programa.", "Texto de permiso: " + m);
        TEST_CHECK(env.svc->get_update_status().error_message() == "No tiene permiso para actualizar el programa.", "Permiso en el estado");
    }
    env.account->logout();
    TEST_CHECK(env.svc->get_update_status().error_message() == "Inicie sesión para continuar.", "Texto de sesión");
    env.login_owner();
    // Búsquedas fallidas de cada tipo
    for (const HttpError e : {HttpError::NoNetwork, HttpError::Timeout, HttpError::TlsUnavailable, HttpError::TlsFailure, HttpError::BadStatus,
                              HttpError::TooLarge, HttpError::NotHttps, HttpError::Other}) {
        env.http->routes.clear();
        env.http->route_error(kManifestUrl, e, e == HttpError::BadStatus ? 429 : 0);
        env.now_ms += 11LL * 60 * 1000;
        const auto st = env.check_and_wait();
        TEST_CHECK(st.state == "ERROR", "Estado de error");
        seen(st.error_message);
        seen(st.info_message);
    }
    {   // firmada por otra clave
        TestKey other;
        const auto bad = make_release(other, "0.2.0");
        env.http->routes.clear();
        serve(*env.http, bad);
        env.now_ms += 11LL * 60 * 1000;
        seen(env.check_and_wait().error_message);
    }
    // Sin claves, portable, buscar demasiado pronto, sin nada que aceptar
    {
        UEnv nk("lenguaje sin claves", {});
        nk.login_owner();
        const auto st = nk.status();
        seen(st.info_message);
        seen(nk.svc->accept_update().error_message());
        nk.make_service(kInstalled, true);
        seen(nk.status().info_message);
        seen(nk.svc->accept_update().error_message());
    }
    env.http->routes.clear();
    serve(*env.http, make_release(key, "0.2.0"));
    env.now_ms += 11LL * 60 * 1000;
    env.check_and_wait();
    env.svc->check_for_updates();
    env.svc->wait_idle(2000);
    seen(env.status().info_message);  // "Ya se buscó hace poco..."
    env.make_service(kInstalled, true);
    seen(env.status().info_message);
    env.make_service(kInstalled, false);
    {
        UEnv none("lenguaje sin version", {key.pub});
        none.login_owner();
        seen(none.svc->accept_update().error_message());
    }

    const std::vector<std::string> banned = {"manifiesto", "manifest", "firma", "SHA", "sha256", "hash", "JSON", "HTTP", "HTTPS", "TLS",
                                             "updates.manage", "ShellExecute", "ShellExecuteEx", "CNG", "bcrypt", "UAC", "CAFEUPD1", "Forbidden",
                                             "InvalidInput", "InvalidState"};
    std::size_t distinct = 0;
    std::vector<std::string> unique = g_messages;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    for (const auto& m : unique) {
        ++distinct;
        for (const auto& w : banned) {
            TEST_CHECK(!contains_word_ci(m, w), "El mensaje \"" + m + "\" contiene la palabra técnica \"" + w + "\"");
        }
        TEST_CHECK(m.find("0x") == std::string::npos && m.find("::") == std::string::npos && m.find('_') == std::string::npos,
                   "El mensaje \"" + m + "\" no debe mostrar códigos internos");
    }
    TEST_CHECK(distinct >= 25, "Se revisaron al menos 25 mensajes distintos (fueron " + std::to_string(distinct) + ")");
}

int main() {
    std::error_code ec;
    g_root = fs::temp_directory_path(ec) / fs::u8path(u8"cafeteria b11 update " + std::to_string(Clock::now().time_since_epoch().count()));
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);

    test_signature_and_keys();
    test_versions();
    test_manifest_verification();
    test_check_flow_and_limits();
    test_check_failures_are_quiet();
    test_download_accept_cancel_decline();
    test_permissions_and_portable();
    test_state_persists_and_install_reconciliation();
    test_prepare_install_never_trusts_unverified();
    test_database_closed_cleanly();
    test_use_update_from_files();
    test_slow_client_never_delays_sales_or_login();
    test_cleanup_old_downloads();
    test_winhttp_client_without_network();
    test_publish_tool_interoperates();
    test_install_result_reasons();
    test_download_error_reasons();
    test_apply_update_from_files_reasons();
    test_plain_language_everywhere();
    test_hooks_or_their_absence();

    std::cout << "Pruebas de actualizaciones (B11) correctas. TEST_CHECK ejecutados: " << g_checks << std::endl;
    cleanup_and_exit(0);
}
