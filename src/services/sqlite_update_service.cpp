#include "app/services/sqlite_update_service.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <thread>
#include <variant>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

#include "app/crypto/sha256.hpp"
#include "app/domain/clock.hpp"
#include "app/services/file_fingerprint.hpp"
#include "app/services/identifiers.hpp"
#include "app/services/update_manifest.hpp"
#include "sqlite3.h"

namespace app::services {

namespace fs = std::filesystem;
using contracts::ErrorCode;
using contracts::OperationResult;

// ---------------------------------------------------------------------------------------------
// Mensajes en lenguaje llano (nada de jerga técnica en lo que ve la persona)
// ---------------------------------------------------------------------------------------------
namespace {

constexpr const char* kMsgNotConfigured =
    "Las actualizaciones por internet todavía no están activadas en este programa. Puede actualizar con un instalador desde una USB.";
constexpr const char* kMsgNoNetwork =
    "No hay conexión a internet en este momento. No pasa nada: el programa sigue funcionando y se volverá a intentar.";
constexpr const char* kMsgTimeout = "La conexión está muy lenta. El programa sigue funcionando y se volverá a intentar más tarde.";
constexpr const char* kMsgTlsUnavailable =
    "Este Windows necesita una actualización para conectarse de forma segura a internet. Puede actualizar con un instalador desde una USB.";
constexpr const char* kMsgTlsFailure = "No se pudo comprobar que la conexión sea segura, así que no se descargó nada.";
constexpr const char* kMsgInvalid =
    "La información de la actualización no se pudo comprobar como auténtica y se ignoró. No se instaló nada.";
constexpr const char* kMsgDownloadCorrupt = "La descarga llegó incompleta o dañada y se descartó. Puede volver a intentarlo.";
constexpr const char* kMsgPortable =
    "Esta copia del programa es portable y no se actualiza sola. Descargue la versión nueva y reemplace la carpeta del programa.";

struct ErrorInfo {
    std::string kind;
    std::string message;
};

// Motivo de una descarga fallida (download_error_kind): INVALID si la descarga no es válida; el resto, fallo de conexión.
std::string download_kind_of(const std::string& check_kind) {
    if (check_kind == "NO_NETWORK" || check_kind == "TIMEOUT" || check_kind == "SERVER" || check_kind == "TLS_UNAVAILABLE" ||
        check_kind == "TLS_FAILURE" || check_kind == "INVALID") return check_kind;
    return "SERVER";
}

ErrorInfo describe_http_error(HttpError e, int status) {
    switch (e) {
        case HttpError::NoNetwork: return {"NO_NETWORK", kMsgNoNetwork};
        case HttpError::Timeout: return {"TIMEOUT", kMsgTimeout};
        case HttpError::TlsUnavailable: return {"TLS_UNAVAILABLE", kMsgTlsUnavailable};
        case HttpError::TlsFailure: return {"TLS_FAILURE", kMsgTlsFailure};
        case HttpError::BadStatus:
            return {"SERVER", "El servidor de actualizaciones no respondió como se esperaba (código " + std::to_string(status) +
                                  "). Se volverá a intentar más tarde."};
        case HttpError::TooLarge: return {"INVALID", kMsgInvalid};
        case HttpError::NotHttps: return {"INVALID", kMsgInvalid};
        default: return {"SERVER", "No se pudo consultar el servidor de actualizaciones. Se volverá a intentar más tarde."};
    }
}

std::int64_t system_now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}

std::string local_text(std::int64_t ms) {
    return domain::timestamp_from(std::chrono::system_clock::time_point(std::chrono::milliseconds(ms))).local;
}

std::string hm_text(std::int64_t ms) {
    const std::string full = local_text(ms);  // "YYYY-MM-DD HH:MM:SS"
    return full.size() >= 16 ? full.substr(11, 5) : full;
}

std::vector<std::string> split_lines(const std::string& text) {
    std::vector<std::string> out;
    std::string line;
    std::istringstream in(text);
    while (std::getline(in, line)) if (!line.empty()) out.push_back(line);
    return out;
}

// ---------------------------------------------------------------------------------------------
// Fila de estado (update_state)
// ---------------------------------------------------------------------------------------------
struct Row {
    std::int64_t last_check_ms{0};
    std::int64_t last_manual_check_ms{0};
    std::string last_check_at;
    std::string last_check_result{"NONE"};
    bool last_check_manual{false};
    std::string last_error;
    std::string available_version;
    std::string notes;
    std::string installer_name;
    std::int64_t installer_size{0};
    std::string installer_sha256;
    std::vector<std::uint8_t> manifest_bytes;
    std::string manifest_sig;
    std::string download_state{"NONE"};
    std::string downloaded_path;
    std::string download_error;
    std::string download_error_kind;
    std::string last_install_reason;
    bool accepted{false};
    std::string install_target_version;
    std::string last_install_result{"NONE"};
    std::string last_install_message;
    std::string last_install_version;
    bool last_install_shown{true};
};

bool load_row(storage::SqliteDatabase& db, Row& r) {
    auto prep = db.prepare(
        "SELECT last_check_ms, last_manual_check_ms, last_check_at, last_check_result, last_check_manual, last_error, "
        "available_version, notes, installer_name, installer_size, installer_sha256, manifest_bytes, manifest_sig, "
        "download_state, downloaded_path, download_error, accepted, install_target_version, last_install_result, "
        "last_install_message, last_install_version, last_install_shown, download_error_kind, last_install_reason FROM update_state WHERE id = 1;");
    if (!prep.is_success()) return false;
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    if (!step.is_success() || !step.value()) return false;
    r.last_check_ms = stmt.get_int64(0);
    r.last_manual_check_ms = stmt.get_int64(1);
    r.last_check_at = stmt.get_text(2);
    r.last_check_result = stmt.get_text(3);
    r.last_check_manual = stmt.get_int64(4) != 0;
    r.last_error = stmt.get_text(5);
    r.available_version = stmt.get_text(6);
    r.notes = stmt.get_text(7);
    r.installer_name = stmt.get_text(8);
    r.installer_size = stmt.get_int64(9);
    r.installer_sha256 = stmt.get_text(10);
    r.manifest_bytes = stmt.is_null(11) ? std::vector<std::uint8_t>() : stmt.get_blob(11);
    r.manifest_sig = stmt.get_text(12);
    r.download_state = stmt.get_text(13);
    r.downloaded_path = stmt.get_text(14);
    r.download_error = stmt.get_text(15);
    r.accepted = stmt.get_int64(16) != 0;
    r.install_target_version = stmt.get_text(17);
    r.last_install_result = stmt.get_text(18);
    r.last_install_message = stmt.get_text(19);
    r.last_install_version = stmt.get_text(20);
    r.last_install_shown = stmt.get_int64(21) != 0;
    r.download_error_kind = stmt.get_text(22);
    r.last_install_reason = stmt.get_text(23);
    return true;
}

using Val = std::variant<std::string, std::int64_t, std::vector<std::uint8_t>>;
using Col = std::pair<const char*, Val>;

// Actualiza varias columnas en una sola sentencia (transacción corta, con busy_timeout de la conexión).
bool set_cols(storage::SqliteDatabase& db, const std::vector<Col>& cols) {
    if (cols.empty()) return true;
    std::string sql = "UPDATE update_state SET ";
    for (std::size_t i = 0; i < cols.size(); ++i) {
        sql += cols[i].first;
        sql += " = ?";
        if (i + 1 < cols.size()) sql += ", ";
    }
    sql += " WHERE id = 1;";
    auto prep = db.prepare(sql);
    if (!prep.is_success()) return false;
    auto stmt = std::move(prep.value());
    for (std::size_t i = 0; i < cols.size(); ++i) {
        const int idx = static_cast<int>(i) + 1;
        const Val& v = cols[i].second;
        if (std::holds_alternative<std::string>(v)) stmt.bind_text(idx, std::get<std::string>(v));
        else if (std::holds_alternative<std::int64_t>(v)) stmt.bind_int64(idx, std::get<std::int64_t>(v));
        else stmt.bind_blob(idx, std::get<std::vector<std::uint8_t>>(v));
    }
    return stmt.step().is_success();
}

void write_audit_direct(storage::SqliteDatabase& db, const std::string& user_id, const char* type, const std::string& details) {
    const auto ts = domain::now();
    auto prep = db.prepare(
        "INSERT INTO audit_events (id, event_type, user_id, details, created_at, created_at_utc) VALUES (?, ?, ?, ?, ?, ?);");
    if (!prep.is_success()) return;
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, generate_id("audit"));
    stmt.bind_text(2, type);
    if (user_id.empty()) stmt.bind_null(3); else stmt.bind_text(3, user_id);
    stmt.bind_text(4, details);
    stmt.bind_text(5, ts.local);
    stmt.bind_text(6, ts.utc);
    stmt.step();
}

std::string to_hex(const std::vector<std::uint8_t>& b) {
    static const char* d = "0123456789abcdef";
    std::string s;
    for (auto c : b) { s += d[c >> 4]; s += d[c & 15]; }
    return s;
}

bool read_small_file(const fs::path& p, std::size_t max_bytes, std::vector<std::uint8_t>& out) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return false;
    const auto size = fs::file_size(p, ec);
    if (ec || size > max_bytes) return false;
    std::ifstream in(p, std::ios::binary);
    out.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return static_cast<std::uint64_t>(out.size()) == size;
}

bool decode_hex(const std::string& hex, std::vector<std::uint8_t>& out) {
    if (hex.size() % 2) return false;
    out.clear();
    auto nib = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < hex.size(); i += 2) {
        const int hi = nib(hex[i]), lo = nib(hex[i + 1]);
        if (hi < 0 || lo < 0) return false;
        out.push_back(static_cast<std::uint8_t>((hi << 4) | lo));
    }
    return true;
}

void remove_dir_quiet(const fs::path& p) {
    std::error_code ec;
    fs::remove_all(p, ec);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// Estado compartido con los hilos
// ---------------------------------------------------------------------------------------------
struct SqliteUpdateService::Context {
    UpdateServiceOptions opt;
    std::string db_file;
    fs::path updates_dir;
    std::int64_t now() const { return opt.now_ms ? opt.now_ms() : system_now_ms(); }
};

struct SqliteUpdateService::Worker {
    std::mutex mutex;
    bool checking{false};
    bool downloading{false};
    bool postponed{false};
    bool manual_throttled{false};
    std::atomic<bool> cancel_download{false};
    std::atomic<bool> shutting_down{false};
    std::atomic<std::uint64_t> downloaded{0};
};

namespace {

// Descarta lo guardado de una versión (carpeta de descargas).
void discard_download_dir(const fs::path& updates_dir, const std::string& version) {
    if (version.empty()) return;
    UpdateVersion v;
    if (!parse_update_version(version, v)) return;  // nunca se arma una ruta con texto no validado
    remove_dir_quiet(updates_dir / version);
}

// Limpia carpetas de descargas de versiones que ya no importan (todas menos `keep_version`).
void cleanup_updates_dir(const fs::path& updates_dir, const std::string& keep_version) {
    std::error_code ec;
    if (!fs::is_directory(updates_dir, ec)) return;
    std::vector<fs::path> victims;
    for (const auto& e : fs::directory_iterator(updates_dir, ec)) {
        if (!e.is_directory()) continue;
        if (e.path().filename().u8string() == keep_version) continue;
        victims.push_back(e.path());
    }
    for (const auto& v : victims) remove_dir_quiet(v);
}

// Verifica el instalador por su tamaño y SHA-256 (por bloques).
bool installer_matches(const fs::path& file, std::uint64_t size, const std::string& sha256) {
    // Un antivirus puede tener el archivo recién escrito abierto unos instantes: se reintenta antes de rechazar.
    FileFingerprint fp;
    bool read_ok = false;
    for (int attempt = 0; attempt < 20 && !read_ok; ++attempt) {
        read_ok = fingerprint_file(file, fp);
        if (!read_ok) std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    if (!read_ok) return false;
    return static_cast<std::uint64_t>(fp.size) == size && fp.sha256 == sha256;
}

// Archivo de resultado del lanzamiento: <datos>\updates\ultimo-lanzamiento.txt. Se escribe al cerrar, cuando la base
// ya está cerrada, y el siguiente arranque lo lee y lo borra. Solo puede AFINAR el motivo de una instalación que no
// se completó; "instalada" nunca depende de él (eso lo decide la versión en ejecución).
fs::path launch_result_path(const fs::path& updates_dir) { return updates_dir / "ultimo-lanzamiento.txt"; }

std::string launch_result_body(const std::string& target, const std::string& from, const std::string& outcome) {
    return "CAFEUPD1\n" + target + "\n" + from + "\n" + outcome + "\n";
}

std::string sha_text(const std::string& text) {
    crypto::Sha256 h;
    h.update(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    return to_hex(h.finalize());
}

// Escribe con temporal y renombrado; si falla no pasa nada (el arranque siguiente cae en NOT_COMPLETED).
void write_launch_result(const fs::path& updates_dir, const std::string& target, const std::string& from, const std::string& outcome) {
    try {
        std::error_code ec;
        fs::create_directories(updates_dir, ec);
        const fs::path final_file = launch_result_path(updates_dir);
        const fs::path tmp = fs::path(final_file.u8string() + ".tmp");
        const std::string body = launch_result_body(target, from, outcome);
        {
            std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
            out << body << sha_text(body) << "\n";
            out.flush();
            if (!out) { out.close(); fs::remove(tmp, ec); return; }
        }
        fs::remove(final_file, ec);
        fs::rename(tmp, final_file, ec);
        if (ec) fs::remove(tmp, ec);
    } catch (...) {
    }
}

// Lee y BORRA siempre el archivo. Devuelve el resultado (UAC_CANCELLED, FAILED o LAUNCHED) solo si el archivo está
// íntegro y es de esta misma instalación (misma versión objetivo y misma versión de origen); si no, vacío.
std::string take_launch_result(const fs::path& updates_dir, const std::string& target, const std::string& from) {
    std::string outcome;
    try {
        std::error_code ec;
        const fs::path file = launch_result_path(updates_dir);
        std::vector<std::uint8_t> raw;
        if (read_small_file(file, 1024, raw)) {
            const std::string text(raw.begin(), raw.end());
            std::vector<std::string> lines;
            std::string cur;
            for (char c : text) { if (c == '\n') { lines.push_back(cur); cur.clear(); } else cur += c; }
            if (!cur.empty()) lines.push_back(cur);
            if (lines.size() == 5 && lines[0] == "CAFEUPD1" && !target.empty() && lines[1] == target && lines[2] == from &&
                (lines[3] == "UAC_CANCELLED" || lines[3] == "FAILED" || lines[3] == "LAUNCHED") &&
                lines[4] == sha_text(launch_result_body(lines[1], lines[2], lines[3]))) {
                outcome = lines[3];
            }
        }
        fs::remove(file, ec);
        fs::remove(fs::path(file.u8string() + ".tmp"), ec);
    } catch (...) {
    }
    return outcome;
}

std::string installer_url(const UpdateServiceOptions& opt, const UpdateManifest& m) {
    return opt.installer_base_url + "/v" + m.version + "/" + m.installer_name;
}

// ---------------------------------------------------------------------------------------------
// Hilo de búsqueda
// ---------------------------------------------------------------------------------------------
struct FlagGuard {
    std::mutex& m;
    bool& flag;
    ~FlagGuard() {
        std::lock_guard<std::mutex> lock(m);
        flag = false;
    }
};

} // namespace

// ---------------------------------------------------------------------------------------------
// Construcción
// ---------------------------------------------------------------------------------------------
SqliteUpdateService::SqliteUpdateService(storage::SqliteDatabase& db,
                                         std::shared_ptr<contracts::IAccountService> account_service,
                                         UpdateServiceOptions options)
    : db_(db), account_service_(std::move(account_service)) {
    ctx_ = std::make_shared<Context>();
    ctx_->opt = std::move(options);
    if (!is_valid_installer_extension(ctx_->opt.installer_extension)) ctx_->opt.installer_extension = ".exe";
    if (!ctx_->opt.http && !ctx_->opt.keys.empty()) ctx_->opt.http = make_default_update_http_client();
    const char* file = db_.handle() ? sqlite3_db_filename(db_.handle(), "main") : nullptr;
    ctx_->db_file = file ? file : "";
    ctx_->updates_dir = fs::u8path(ctx_->opt.data_dir.empty() ? std::string(".") : ctx_->opt.data_dir) / "updates";
    worker_ = std::make_shared<Worker>();
    reconcile_install_state();
}

SqliteUpdateService::~SqliteUpdateService() {
    worker_->shutting_down.store(true);
    worker_->cancel_download.store(true);
    wait_idle(1500);
}

bool SqliteUpdateService::wait_idle(int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        {
            std::lock_guard<std::mutex> lock(worker_->mutex);
            if (!worker_->checking && !worker_->downloading) return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

bool SqliteUpdateService::has_permission() const {
    return account_service_ && account_service_->is_authenticated() && account_service_->has_permission("updates.manage");
}

OperationResult SqliteUpdateService::require_permission() const {
    if (!account_service_ || !account_service_->is_authenticated()) {
        return OperationResult::fail(ErrorCode::Forbidden, "Inicie sesión para continuar.");
    }
    if (!account_service_->has_permission("updates.manage")) {
        return OperationResult::fail(ErrorCode::Forbidden, "No tiene permiso para actualizar el programa.");
    }
    return OperationResult::ok();
}

// Tras un arranque: ¿qué pasó con la instalación que se lanzó al cerrar la vez anterior?
void SqliteUpdateService::reconcile_install_state() {
    Row row;
    if (!load_row(db_, row)) return;
    UpdateVersion installed;
    if (!parse_update_version(ctx_->opt.installed_version, installed)) return;

    const std::string launch_outcome = take_launch_result(ctx_->updates_dir, row.install_target_version, ctx_->opt.installed_version);
    if (!row.install_target_version.empty()) {
        UpdateVersion target;
        const bool target_ok = parse_update_version(row.install_target_version, target);
        if (target_ok && compare_update_versions(installed, target) >= 0) {
            set_cols(db_, {{"last_install_result", std::string("SUCCESS")}, {"last_install_reason", std::string("INSTALLED")},
                           {"last_install_message", "Se actualizó a la versión " + row.install_target_version + "."},
                           {"last_install_version", row.install_target_version},
                           {"last_install_shown", std::int64_t(0)},
                           {"install_target_version", std::string()},
                           {"available_version", std::string()}, {"notes", std::string()}, {"installer_name", std::string()},
                           {"installer_size", std::int64_t(0)}, {"installer_sha256", std::string()},
                           {"manifest_bytes", std::vector<std::uint8_t>()}, {"manifest_sig", std::string()},
                           {"download_state", std::string("NONE")}, {"downloaded_path", std::string()},
                           {"accepted", std::int64_t(0)}});
            write_audit_direct(db_, "", "UPDATE_INSTALL_OK", "Actualización instalada: versión " + row.install_target_version);
        } else {
            std::string reason = "NOT_COMPLETED";
            std::string message = "La actualización no se completó; sigues con la versión " + ctx_->opt.installed_version + ". Se volverá a ofrecer.";
            if (launch_outcome == "UAC_CANCELLED") {
                reason = "PERMISSION_DENIED";
                message = "No se instaló porque no se dio permiso en Windows. Se volverá a ofrecer.";
            } else if (launch_outcome == "FAILED") {
                reason = "LAUNCH_FAILED";
                message = "No se pudo iniciar la instalación. El programa sigue en la versión " + ctx_->opt.installed_version + " y se volverá a ofrecer.";
            }
            // "Se volverá a ofrecer" es verdad: la versión sigue disponible, sin aceptar. La descarga se conserva solo si
            // el archivo sigue en su sitio con su tamaño (el hash se vuelve a comprobar antes de instalar).
            std::vector<Col> cols = {{"last_install_result", std::string("NOT_COMPLETED")}, {"last_install_reason", reason}, {"last_install_message", message},
                                     {"last_install_version", row.install_target_version}, {"last_install_shown", std::int64_t(0)},
                                     {"install_target_version", std::string()}, {"accepted", std::int64_t(0)}};
            std::error_code ec;
            const fs::path kept = fs::u8path(row.downloaded_path);
            // El tamaño esperado sale del manifiesto firmado vuelto a verificar, no de las columnas de apoyo.
            std::vector<std::uint8_t> kept_sig;
            bool file_ok = false;
            if (!row.downloaded_path.empty() && decode_hex(row.manifest_sig, kept_sig)) {
                auto kept_manifest = verify_and_parse_manifest(row.manifest_bytes, kept_sig, ctx_->opt.keys, "0.0.0", ctx_->opt.installer_extension);
                file_ok = kept_manifest.error == ManifestError::None && fs::is_regular_file(kept, ec) &&
                          fs::file_size(kept, ec) == kept_manifest.manifest.installer_size;
            }
            if (row.download_state == "DONE" && !file_ok) {
                cols.push_back({"download_state", std::string("NONE")});
                cols.push_back({"downloaded_path", std::string()});
            }
            set_cols(db_, cols);
            write_audit_direct(db_, "", "UPDATE_INSTALL_NOT_COMPLETED", "La instalación de la versión " + row.install_target_version + " no se completó (" + reason + ")");
        }
        load_row(db_, row);
    }
    // Una versión disponible que ya es la instalada (o menor) no tiene sentido: se limpia.
    if (!row.available_version.empty()) {
        UpdateVersion avail;
        if (!parse_update_version(row.available_version, avail) || compare_update_versions(avail, installed) <= 0) {
            discard_download_dir(ctx_->updates_dir, row.available_version);
            set_cols(db_, {{"available_version", std::string()}, {"notes", std::string()}, {"installer_name", std::string()},
                           {"installer_size", std::int64_t(0)}, {"installer_sha256", std::string()},
                           {"manifest_bytes", std::vector<std::uint8_t>()}, {"manifest_sig", std::string()},
                           {"download_state", std::string("NONE")}, {"downloaded_path", std::string()},
                           {"accepted", std::int64_t(0)}});
        }
    }
    cleanup_updates_dir(ctx_->updates_dir, row.available_version);
}

// ---------------------------------------------------------------------------------------------
// Estado
// ---------------------------------------------------------------------------------------------
contracts::Result<contracts::UpdateStatusDto> SqliteUpdateService::get_update_status() {
    using R = contracts::Result<contracts::UpdateStatusDto>;
    auto perm = require_permission();
    if (!perm.is_success()) return R::fail(perm.error_code(), perm.error_message());

    Row row;
    if (!load_row(db_, row)) return R::fail(ErrorCode::StorageFailure, "No se pudo leer el estado de las actualizaciones");
    const auto& opt = ctx_->opt;
    contracts::UpdateStatusDto dto;
    dto.installed_version = opt.installed_version;
    dto.is_portable = opt.is_portable;
    dto.last_check_at = row.last_check_at;
    const std::int64_t now = ctx_->now();
    if (row.last_manual_check_ms > 0 && now < row.last_manual_check_ms + opt.manual_check_min_interval_ms) {
        dto.next_manual_check_at = local_text(row.last_manual_check_ms + opt.manual_check_min_interval_ms);
    }
    bool checking, downloading;
    {
        std::lock_guard<std::mutex> lock(worker_->mutex);
        checking = worker_->checking;
        downloading = worker_->downloading;
        dto.postponed_this_session = worker_->postponed;
        dto.manual_check_throttled = worker_->manual_throttled && !dto.next_manual_check_at.empty();
    }
    if (dto.manual_check_throttled) {
        dto.info_message = "Ya se buscó hace poco. Podrá buscar de nuevo a las " + hm_text(row.last_manual_check_ms + opt.manual_check_min_interval_ms) + ".";
    }

    UpdateVersion installed, avail;
    const bool have_avail = !row.available_version.empty() && parse_update_version(row.available_version, avail) &&
                            parse_update_version(opt.installed_version, installed) && compare_update_versions(avail, installed) > 0;
    if (have_avail) {
        dto.available_version = row.available_version;
        dto.notes = split_lines(row.notes);
        dto.installer_size_bytes = row.installer_size;
        dto.accepted = row.accepted;
        dto.download_verified = row.download_state == "DONE";
        dto.downloaded_bytes = dto.download_verified ? row.installer_size : 0;
    }

    if (opt.keys.empty()) {
        dto.state = "NOT_CONFIGURED";
        dto.info_message = kMsgNotConfigured;
    } else if (checking) {
        dto.state = "CHECKING";
    } else if (downloading) {
        dto.state = "DOWNLOADING";
        dto.downloaded_bytes = static_cast<std::int64_t>(worker_->downloaded.load());
    } else if (have_avail) {
        dto.state = (dto.accepted && dto.download_verified) ? "READY" : "AVAILABLE";
        if (!row.download_error.empty()) {
            dto.error_kind = "DOWNLOAD";
            dto.error_message = row.download_error;
            dto.download_error_kind = row.download_error_kind;  // vacío = motivo desconocido (filas anteriores a la migración 018)
        }
    } else if (row.last_check_result != "NONE" && row.last_check_result != "OK") {
        dto.state = "ERROR";
        dto.error_kind = row.last_check_result;
        dto.error_message = row.last_error;
        dto.error_from_manual_check = row.last_check_manual;
    } else {
        dto.state = "UP_TO_DATE";
    }
    if (opt.is_portable) {
        dto.info_message = kMsgPortable;
    }
    dto.will_install_on_close = dto.state == "READY" && !dto.postponed_this_session && !opt.is_portable;
    return R::ok(std::move(dto));
}

// ---------------------------------------------------------------------------------------------
// Hilos
// ---------------------------------------------------------------------------------------------
// Los cuerpos de los hilos necesitan los tipos privados Context y Worker: se definen como lambdas dentro de los
// métodos start_*_thread, que sí tienen acceso.
void SqliteUpdateService::start_check_thread(bool manual) {
    {
        std::lock_guard<std::mutex> lock(worker_->mutex);
        if (worker_->checking) return;
        worker_->checking = true;
    }
    auto ctx = ctx_;
    auto worker = worker_;
    auto body = [ctx, worker, manual]() {
        FlagGuard guard{worker->mutex, worker->checking};
        try {
            storage::SqliteDatabase db;
            if (!db.open(fs::u8path(ctx->db_file)).is_success()) return;
            const auto& opt = ctx->opt;
            const std::int64_t started = ctx->now();

            auto finish_with = [&](const std::string& result, const std::string& message) {
                set_cols(db, {{"last_check_ms", started}, {"last_check_at", local_text(started)}, {"last_check_result", result},
                              {"last_check_manual", std::int64_t(manual ? 1 : 0)}, {"last_error", message}});
            };

            // 1. Manifiesto y firma (respuestas pequeñas, tiempos acotados).
            auto manifest = opt.http->get(opt.manifest_url, kMaxManifestBytes, opt.http_timeout_ms);
            if (manifest.error != HttpError::None) {
                const auto info = describe_http_error(manifest.error, manifest.status);
                finish_with(info.kind, info.message);
                write_audit_direct(db, "", "UPDATE_CHECK_FAILED", "No se pudo buscar actualizaciones: " + info.kind);
                return;
            }
            auto sig = opt.http->get(opt.signature_url, kMaxSignatureFileBytes, opt.http_timeout_ms);
            if (sig.error != HttpError::None) {
                const auto info = describe_http_error(sig.error, sig.status);
                finish_with(info.kind, info.message);
                write_audit_direct(db, "", "UPDATE_CHECK_FAILED", "No se pudo buscar actualizaciones: " + info.kind);
                return;
            }

            // 2. Verificación completa.
            auto verified = verify_and_parse_manifest(manifest.body, sig.body, opt.keys, opt.installed_version, opt.installer_extension);
            Row row;
            load_row(db, row);
            if (verified.error == ManifestError::NotNewer) {
                // Válida pero no es más nueva: se ignora (nunca se baja de versión).
                finish_with("OK", "");
                if (!row.available_version.empty()) {
                    UpdateVersion a, i;
                    if (parse_update_version(row.available_version, a) && parse_update_version(opt.installed_version, i) &&
                        compare_update_versions(a, i) <= 0) {
                        discard_download_dir(ctx->updates_dir, row.available_version);
                        set_cols(db, {{"available_version", std::string()}, {"notes", std::string()}, {"installer_name", std::string()},
                                      {"installer_size", std::int64_t(0)}, {"installer_sha256", std::string()},
                                      {"manifest_bytes", std::vector<std::uint8_t>()}, {"manifest_sig", std::string()},
                                      {"download_state", std::string("NONE")}, {"downloaded_path", std::string()},
                                      {"accepted", std::int64_t(0)}});
                    }
                }
                return;
            }
            if (verified.error != ManifestError::None) {
                finish_with("INVALID", kMsgInvalid);
                write_audit_direct(db, "", "UPDATE_REJECTED", "Una información de actualización no pasó la verificación");
                return;
            }

            // 3. Hay una versión nueva y válida.
            const auto& m = verified.manifest;
            std::string notes;
            for (const auto& n : m.notes) notes += n + "\n";
            std::vector<std::uint8_t> sig_decoded;
            decode_signature_file(sig.body, sig_decoded);  // la firma se guarda decodificada (hexadecimal), ya verificada
            std::vector<Col> cols = {{"available_version", m.version}, {"notes", notes}, {"installer_name", m.installer_name},
                                     {"installer_size", std::int64_t(m.installer_size)}, {"installer_sha256", m.installer_sha256},
                                     {"manifest_bytes", manifest.body}, {"manifest_sig", to_hex(sig_decoded)}};
            const bool is_new_version = row.available_version != m.version;
            if (is_new_version) {
                discard_download_dir(ctx->updates_dir, row.available_version);
                cols.push_back({"accepted", std::int64_t(0)});
                cols.push_back({"download_state", std::string("NONE")});
                cols.push_back({"downloaded_path", std::string()});
                cols.push_back({"download_error", std::string()});
                cols.push_back({"download_error_kind", std::string()});
            }
            set_cols(db, cols);
            finish_with("OK", "");
            if (is_new_version) write_audit_direct(db, "", "UPDATE_AVAILABLE", "Hay una versión nueva disponible: " + m.version);
            cleanup_updates_dir(ctx->updates_dir, m.version);
        } catch (...) {
            // El hilo nunca propaga excepciones.
        }
    };
    try {
        std::thread(body).detach();
    } catch (...) {
        std::lock_guard<std::mutex> lock(worker_->mutex);
        worker_->checking = false;
    }
}

void SqliteUpdateService::start_download_thread() {
    {
        std::lock_guard<std::mutex> lock(worker_->mutex);
        if (worker_->downloading) return;
        worker_->downloading = true;
    }
    worker_->cancel_download.store(false);
    worker_->downloaded.store(0);
    auto ctx = ctx_;
    auto worker = worker_;
    auto body = [ctx, worker]() {
        FlagGuard guard{worker->mutex, worker->downloading};
        try {
            storage::SqliteDatabase db;
            if (!db.open(fs::u8path(ctx->db_file)).is_success()) return;
            const auto& opt = ctx->opt;
            Row row;
            if (!load_row(db, row) || row.available_version.empty()) return;

            auto fail_download = [&](const std::string& kind, const std::string& message) {
                set_cols(db, {{"download_state", std::string("NONE")}, {"accepted", std::int64_t(0)}, {"download_error", message},
                              {"download_error_kind", kind}});
                write_audit_direct(db, "", "UPDATE_DOWNLOAD_FAILED", "No se pudo descargar la versión " + row.available_version);
            };

            // La descarga se decide con el manifiesto guardado, VUELTO A VERIFICAR (la base puede ser editada por otros).
            std::vector<std::uint8_t> sig;
            if (!decode_hex(row.manifest_sig, sig)) { fail_download("INVALID", kMsgInvalid); return; }
            auto verified = verify_and_parse_manifest(row.manifest_bytes, sig, opt.keys, opt.installed_version, opt.installer_extension);
            if (verified.error != ManifestError::None) {
                fail_download("INVALID", kMsgInvalid);
                write_audit_direct(db, "", "UPDATE_REJECTED", "El manifiesto guardado ya no pasa la verificación");
                return;
            }
            const auto& m = verified.manifest;
            const fs::path dir = ctx->updates_dir / m.version;
            std::error_code ec;
            fs::create_directories(dir, ec);
            const fs::path final_file = dir / m.installer_name;
            const fs::path part_file = dir / (m.installer_name + ".part");
            worker->downloaded.store(0);

            if (!(fs::is_regular_file(final_file, ec) && installer_matches(final_file, m.installer_size, m.installer_sha256))) {
                fs::remove(final_file, ec);
                write_audit_direct(db, "", "UPDATE_DOWNLOAD_STARTED", "Descarga de la versión " + m.version);
                auto res = opt.http->download(
                    installer_url(opt, m), part_file, m.installer_size,
                    [&](std::uint64_t bytes) {
                        worker->downloaded.store(bytes);
                        return !worker->cancel_download.load() && !worker->shutting_down.load();
                    },
                    10 * 60 * 1000);
                if (res.error == HttpError::Cancelled) {
                    if (!worker->shutting_down.load()) {
                        set_cols(db, {{"download_state", std::string("NONE")}, {"accepted", std::int64_t(0)}, {"download_error", std::string()}, {"download_error_kind", std::string()}});
                        write_audit_direct(db, "", "UPDATE_DOWNLOAD_CANCELLED", "Descarga cancelada: versión " + m.version);
                    }
                    return;
                }
                if (res.error != HttpError::None) {
                    const auto info = describe_http_error(res.error, res.status);
                    fail_download(download_kind_of(info.kind), info.message);
                    return;
                }
                if (!installer_matches(part_file, m.installer_size, m.installer_sha256)) {
                    fs::remove(part_file, ec);
                    fail_download("INVALID", kMsgDownloadCorrupt);
                    write_audit_direct(db, "", "UPDATE_REJECTED", "La descarga no coincide con el manifiesto firmado");
                    return;
                }
                fs::rename(part_file, final_file, ec);
                if (ec) { fail_download("STORAGE", "No se pudo guardar la descarga en el disco."); return; }
            }
            worker->downloaded.store(m.installer_size);
            set_cols(db, {{"download_state", std::string("DONE")}, {"downloaded_path", final_file.u8string()}, {"download_error", std::string()}, {"download_error_kind", std::string()}});
            write_audit_direct(db, "", "UPDATE_DOWNLOADED", "Versión " + m.version + " descargada y verificada");
        } catch (...) {
        }
    };
    try {
        std::thread(body).detach();
    } catch (...) {
        std::lock_guard<std::mutex> lock(worker_->mutex);
        worker_->downloading = false;
    }
}

// ---------------------------------------------------------------------------------------------
// Contrato
// ---------------------------------------------------------------------------------------------
OperationResult SqliteUpdateService::check_for_updates() {
    auto perm = require_permission();
    if (!perm.is_success()) return perm;
    if (ctx_->opt.keys.empty()) return OperationResult::ok();  // el estado dice NOT_CONFIGURED; no hay petición alguna
    Row row;
    if (!load_row(db_, row)) return OperationResult::fail(ErrorCode::StorageFailure, "No se pudo leer el estado de las actualizaciones");
    const std::int64_t now = ctx_->now();
    {
        std::lock_guard<std::mutex> lock(worker_->mutex);
        if (row.last_manual_check_ms > 0 && now < row.last_manual_check_ms + ctx_->opt.manual_check_min_interval_ms) {
            worker_->manual_throttled = true;  // no es un error: el estado lo dice
            return OperationResult::ok();
        }
        worker_->manual_throttled = false;
    }
    set_cols(db_, {{"last_manual_check_ms", now}});
    account_service_->record_audit_event("UPDATE_CHECK", "Búsqueda manual de actualizaciones");
    start_check_thread(true);
    return OperationResult::ok();
}

OperationResult SqliteUpdateService::accept_update() {
    auto perm = require_permission();
    if (!perm.is_success()) return perm;
    if (ctx_->opt.is_portable) return OperationResult::fail(ErrorCode::InvalidState, kMsgPortable);
    if (ctx_->opt.keys.empty()) return OperationResult::fail(ErrorCode::InvalidState, kMsgNotConfigured);
    Row row;
    if (!load_row(db_, row) || row.available_version.empty()) {
        return OperationResult::fail(ErrorCode::InvalidState, "No hay ninguna actualización disponible para aceptar.");
    }
    const bool downloaded = row.download_state == "DONE";
    set_cols(db_, {{"accepted", std::int64_t(1)}, {"accepted_at", local_text(ctx_->now())}, {"download_error", std::string()}, {"download_error_kind", std::string()},
                   {"download_state", std::string(downloaded ? "DONE" : "DOWNLOADING")}});
    account_service_->record_audit_event("UPDATE_ACCEPTED", "Actualización aceptada: versión " + row.available_version);
    if (!downloaded) start_download_thread();
    return OperationResult::ok();
}

OperationResult SqliteUpdateService::decline_update() {
    auto perm = require_permission();
    if (!perm.is_success()) return perm;
    set_cols(db_, {{"accepted", std::int64_t(0)}});
    account_service_->record_audit_event("UPDATE_DECLINED", "Se quitó la aceptación de la actualización");
    return OperationResult::ok();
}

OperationResult SqliteUpdateService::cancel_download() {
    auto perm = require_permission();
    if (!perm.is_success()) return perm;
    bool downloading;
    {
        std::lock_guard<std::mutex> lock(worker_->mutex);
        downloading = worker_->downloading;
    }
    if (downloading) {
        worker_->cancel_download.store(true);  // el hilo deja la versión disponible y NO aceptada
    } else {
        set_cols(db_, {{"accepted", std::int64_t(0)}});
    }
    return OperationResult::ok();
}

contracts::Result<contracts::UpdateFromFilesResultDto> SqliteUpdateService::apply_update_from_files(
    const std::string& installer_path, const std::string& data_path, const std::string& verification_path) {
    using R = contracts::Result<contracts::UpdateFromFilesResultDto>;
    auto perm = require_permission();
    if (!perm.is_success()) return R::fail(perm.error_code(), perm.error_message());
    auto done = [](bool accepted, const char* reason, const std::string& message) {
        contracts::UpdateFromFilesResultDto dto;
        dto.accepted = accepted;
        dto.reason = reason;
        dto.message = message;
        return R::ok(std::move(dto));
    };
    if (ctx_->opt.is_portable) return done(false, "PORTABLE", kMsgPortable);
    if (ctx_->opt.keys.empty()) return done(false, "NOT_CONFIGURED", kMsgNotConfigured);
    const std::string unreadable =
        "No se pudieron leer los archivos. Elija el instalador, el archivo de datos y el archivo de verificación.";
    const fs::path installer = fs::u8path(installer_path);
    std::vector<std::uint8_t> manifest, sig_file;
    std::error_code ec;
    if (!fs::is_regular_file(installer, ec) || !read_small_file(fs::u8path(data_path), kMaxManifestBytes, manifest) ||
        !read_small_file(fs::u8path(verification_path), kMaxSignatureFileBytes, sig_file)) {
        return done(false, "UNREADABLE", unreadable);
    }
    auto verified = verify_and_parse_manifest(manifest, sig_file, ctx_->opt.keys, ctx_->opt.installed_version, ctx_->opt.installer_extension);
    if (verified.error == ManifestError::NotNewer) {
        return done(false, "NOT_NEWER", "Esa actualización no es más nueva que la versión instalada.");
    }
    if (verified.error != ManifestError::None) {
        write_audit_direct(db_, "", "UPDATE_REJECTED", "Una actualización desde archivos no pasó la verificación");
        return done(false, "INVALID", "Esa actualización no es auténtica o está dañada. No se instaló nada.");
    }
    const auto& m = verified.manifest;
    if (!installer_matches(installer, m.installer_size, m.installer_sha256)) {
        write_audit_direct(db_, "", "UPDATE_REJECTED", "El instalador elegido no coincide con la información firmada");
        return done(false, "INVALID", "El instalador elegido no corresponde a esa actualización o está dañado. No se instaló nada.");
    }
    // Se copia a la carpeta propia (con archivo temporal) y se vuelve a verificar la copia.
    const fs::path dir = ctx_->updates_dir / m.version;
    fs::create_directories(dir, ec);
    const fs::path final_file = dir / m.installer_name;
    const fs::path tmp_file = dir / (m.installer_name + ".part");
    fs::remove(tmp_file, ec);
    fs::copy_file(installer, tmp_file, fs::copy_options::overwrite_existing, ec);
    if (ec || !installer_matches(tmp_file, m.installer_size, m.installer_sha256)) {
        fs::remove(tmp_file, ec);
        return done(false, "STORAGE", "No se pudo copiar la actualización al disco del equipo.");
    }
    fs::remove(final_file, ec);
    fs::rename(tmp_file, final_file, ec);
    if (ec) return done(false, "STORAGE", "No se pudo guardar la actualización en el disco del equipo.");

    Row row;
    load_row(db_, row);
    if (row.available_version != m.version) discard_download_dir(ctx_->updates_dir, row.available_version);
    std::string notes;
    for (const auto& n : m.notes) notes += n + "\n";
    std::vector<std::uint8_t> sig_decoded;
    decode_signature_file(sig_file, sig_decoded);
    set_cols(db_, {{"available_version", m.version}, {"notes", notes}, {"installer_name", m.installer_name},
                   {"installer_size", std::int64_t(m.installer_size)}, {"installer_sha256", m.installer_sha256},
                   {"manifest_bytes", manifest}, {"manifest_sig", to_hex(sig_decoded)}, {"download_state", std::string("DONE")},
                   {"downloaded_path", final_file.u8string()}, {"download_error", std::string()}, {"download_error_kind", std::string()},
                   {"accepted", std::int64_t(1)}, {"accepted_at", local_text(ctx_->now())}});
    cleanup_updates_dir(ctx_->updates_dir, m.version);
    account_service_->record_audit_event("UPDATE_FROM_FILES", "Actualización desde archivos aceptada: versión " + m.version);
    return done(true, "OK", "La actualización quedó lista para instalarse al cerrar el programa.");
}

OperationResult SqliteUpdateService::use_update_from_files(const std::string& installer_path, const std::string& manifest_path,
                                                           const std::string& signature_path) {
    auto r = apply_update_from_files(installer_path, manifest_path, signature_path);
    if (!r.is_success()) return OperationResult::fail(r.error_code(), r.error_message());
    const auto& v = r.value();
    if (v.accepted) return OperationResult::ok();
    ErrorCode code = ErrorCode::InvalidState;
    if (v.reason == "INVALID" || v.reason == "UNREADABLE") code = ErrorCode::InvalidInput;
    else if (v.reason == "STORAGE") code = ErrorCode::StorageFailure;
    return OperationResult::fail(code, v.message);
}

OperationResult SqliteUpdateService::postpone_install_this_session() {
    auto perm = require_permission();
    if (!perm.is_success()) return perm;
    std::lock_guard<std::mutex> lock(worker_->mutex);
    worker_->postponed = true;
    return OperationResult::ok();
}

contracts::Result<contracts::UpdateInstallResultDto> SqliteUpdateService::get_last_install_result() {
    using R = contracts::Result<contracts::UpdateInstallResultDto>;
    auto perm = require_permission();
    if (!perm.is_success()) return R::fail(perm.error_code(), perm.error_message());
    Row row;
    if (!load_row(db_, row)) return R::fail(ErrorCode::StorageFailure, "No se pudo leer el estado de las actualizaciones");
    contracts::UpdateInstallResultDto dto;
    if (row.last_install_result != "NONE" && !row.last_install_shown) {
        dto.has_result = true;
        dto.success = row.last_install_result == "SUCCESS";
        dto.reason = dto.success ? "INSTALLED"
                     : (row.last_install_reason == "PERMISSION_DENIED" || row.last_install_reason == "LAUNCH_FAILED") ? row.last_install_reason
                                                                                                                       : "NOT_COMPLETED";
        dto.version = row.last_install_version;
        dto.message = row.last_install_message;
    }
    return R::ok(dto);
}

OperationResult SqliteUpdateService::acknowledge_install_result() {
    auto perm = require_permission();
    if (!perm.is_success()) return perm;
    set_cols(db_, {{"last_install_shown", std::int64_t(1)}});
    return OperationResult::ok();
}

// ---------------------------------------------------------------------------------------------
// Búsqueda automática al abrir
// ---------------------------------------------------------------------------------------------
void SqliteUpdateService::start_automatic_check() {
    const char* off = std::getenv("CAFETERIA_UPDATE_CHECK");
    if (off && std::string(off) == "0") return;       // pruebas de interfaz: sin red
    if (ctx_->opt.keys.empty()) return;               // sin clave pública no hay nada que descargar: ni una petición
    Row row;
    if (!load_row(db_, row)) return;
    // Una actualización aceptada con la descarga a medias se reanuda (sin límite de 24 h).
    if (row.accepted && !row.available_version.empty() && row.download_state != "DONE" && !ctx_->opt.is_portable) {
        start_download_thread();
    }
    if (ctx_->now() - row.last_check_ms < ctx_->opt.auto_check_interval_ms && row.last_check_ms > 0) return;
    start_check_thread(false);
}

// ---------------------------------------------------------------------------------------------
// Instalación al cerrar
// ---------------------------------------------------------------------------------------------
namespace {
#ifdef _WIN32
struct HandleCloser {
    void operator()(void* h) const { if (h && h != INVALID_HANDLE_VALUE) CloseHandle(static_cast<HANDLE>(h)); }
};
#endif
} // namespace

SqliteUpdateService::InstallLaunchPlan SqliteUpdateService::prepare_install_on_close() {
    InstallLaunchPlan plan;
    const auto& opt = ctx_->opt;
    auto no = [&](const std::string& why) { plan.ok = false; plan.reason = why; return plan; };

    if (opt.keys.empty()) return no("sin claves públicas");
    if (opt.is_portable) return no("copia portable");
    if (!account_service_ || !account_service_->is_authenticated()) return no("sin sesión (p. ej. cierre tras restaurar un respaldo)");
    {
        std::lock_guard<std::mutex> lock(worker_->mutex);
        if (worker_->postponed) return no("el Dueño eligió cerrar sin instalar");
        if (worker_->downloading) return no("descarga en curso");
    }
    Row row;
    if (!load_row(db_, row)) return no("sin estado");
    if (!row.accepted || row.available_version.empty() || row.download_state != "DONE") return no("no hay actualización aceptada y descargada");

    // Verificación completa, desde cero: nada de lo guardado en la base se da por bueno.
    std::vector<std::uint8_t> sig;
    if (!decode_hex(row.manifest_sig, sig)) return no("firma guardada ilegible");
    auto verified = verify_and_parse_manifest(row.manifest_bytes, sig, opt.keys, opt.installed_version, opt.installer_extension);
    if (verified.error != ManifestError::None) return no("el manifiesto guardado no verifica o no es más nuevo");
    const auto& m = verified.manifest;

#ifdef _WIN32
    // La ruta se arma aquí, no se toma de la base. El archivo se abre SIN permiso de escritura, borrado ni renombrado
    // para otros, y se verifica por ese mismo manejador; el manejador queda abierto durante el lanzamiento.
    const fs::path installer = ctx_->updates_dir / m.version / m.installer_name;
    HANDLE h = CreateFileW(installer.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return no("no se pudo abrir el instalador descargado");
    std::shared_ptr<void> lock(h, HandleCloser());
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(h, &size) || static_cast<std::uint64_t>(size.QuadPart) != m.installer_size) return no("tamaño distinto al del manifiesto");
    app::crypto::Sha256 hasher;
    std::vector<std::uint8_t> buf(1024 * 1024);
    std::uint64_t total = 0;
    while (true) {
        DWORD got = 0;
        if (!ReadFile(h, buf.data(), static_cast<DWORD>(buf.size()), &got, nullptr)) return no("no se pudo leer el instalador");
        if (got == 0) break;
        hasher.update(buf.data(), got);
        total += got;
    }
    if (total != m.installer_size || to_hex(hasher.finalize()) != m.installer_sha256) return no("el instalador no coincide con el manifiesto firmado");

    plan.installer = installer;
    plan.version = m.version;
    plan.arguments = "/VERYSILENT /SUPPRESSMSGBOXES /NORESTART /LOG=\"" +
                     (ctx_->updates_dir / ("instalacion-" + m.version + ".log")).u8string() + "\"";
    plan.lock = lock;
    plan.ok = true;

    // Se anota ANTES de cerrar la base: si el UAC se cancela o la instalación falla, el próximo arranque lo detecta
    // porque la versión instalada no cambió.
    set_cols(db_, {{"install_target_version", m.version}, {"install_attempt_at", local_text(ctx_->now())}});
    account_service_->record_audit_event("UPDATE_INSTALL_LAUNCHED", "Se lanza la instalación de la versión " + m.version);
    return plan;
#else
    return no("solo Windows");
#endif
}

bool SqliteUpdateService::database_closed_cleanly(const fs::path& db_file) {
    std::error_code ec;
    const fs::path wal = fs::path(db_file.u8string() + "-wal");
    const fs::path shm = fs::path(db_file.u8string() + "-shm");
    if (fs::exists(wal, ec) && fs::file_size(wal, ec) > 0) return false;
    if (fs::exists(shm, ec)) return false;
    return true;
}

SqliteUpdateService::LaunchOutcome SqliteUpdateService::launch_install(const InstallLaunchPlan& plan, std::string* error) const {
    if (!plan.ok) {
        if (error) *error = plan.reason;
        return LaunchOutcome::Failed;
    }
    // El resultado se guarda DESPUÉS de que el lanzamiento vuelva (el aviso de Windows bloquea hasta que la persona
    // decide). La base ya está cerrada: se usa un archivo pequeño que el siguiente arranque lee y borra.
    auto record = [&](LaunchOutcome outcome) {
        const char* text = outcome == LaunchOutcome::Launched ? "LAUNCHED" : outcome == LaunchOutcome::UacCancelled ? "UAC_CANCELLED" : "FAILED";
        write_launch_result(ctx_->updates_dir, plan.version, ctx_->opt.installed_version, text);
        return outcome;
    };
#ifdef CAFETERIA_TEST_HOOKS
    {
        const char* target = std::getenv("CAFETERIA_UPDATE_TEST_LAUNCH");
        if (target && *target) {
            const char* forced = std::getenv("CAFETERIA_UPDATE_TEST_LAUNCH_RESULT");
            const std::string want = forced ? forced : "";
            if (want == "UAC_CANCELLED") { if (error) *error = "simulado: autorización cancelada"; return record(LaunchOutcome::UacCancelled); }
            if (want == "FAILED") { if (error) *error = "simulado: no se pudo lanzar"; return record(LaunchOutcome::Failed); }
            std::ofstream out(fs::u8path(target), std::ios::binary | std::ios::trunc);
            out << plan.installer.u8string() << "\n" << plan.version << "\n" << plan.arguments << "\n";
            return record(out ? LaunchOutcome::Launched : LaunchOutcome::Failed);
        }
    }
#endif
#ifdef _WIN32
    const std::wstring file = plan.installer.wstring();
    const std::wstring args = fs::u8path(plan.arguments).wstring();
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof(sei);
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = file.c_str();
    sei.lpParameters = args.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei)) {
        const DWORD e = GetLastError();
        if (error) *error = "ShellExecuteEx falló (" + std::to_string(e) + ")";
        return record(e == ERROR_CANCELLED ? LaunchOutcome::UacCancelled : LaunchOutcome::Failed);
    }
    if (sei.hProcess) CloseHandle(sei.hProcess);
    return record(LaunchOutcome::Launched);
#else
    if (error) *error = "solo Windows";
    return record(LaunchOutcome::Failed);
#endif
}

// Hubo plan pero no se pudo intentar el lanzamiento (la base no cerró limpia): se anota como fallo de inicio. Solo
// escribe si el plan es válido; sin plan (sin sesión, pospuesto, portable...) no escribe nada.
void SqliteUpdateService::note_launch_not_attempted(const InstallLaunchPlan& plan) const {
    if (!plan.ok) return;
    write_launch_result(ctx_->updates_dir, plan.version, ctx_->opt.installed_version, "FAILED");
}

} // namespace app::services
