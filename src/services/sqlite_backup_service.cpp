#include "app/services/sqlite_backup_service.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include <atomic>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <vector>
#include "app/crypto/password_hasher.hpp"
#include "app/domain/clock.hpp"
#include "app/services/identifiers.hpp"
#include "app/storage/migration_runner.hpp"

namespace app::services {

namespace fs = std::filesystem;

namespace {

std::tm to_local_tm(std::time_t t) {
    std::tm out{};
#ifdef _WIN32
    localtime_s(&out, &t);
#else
    localtime_r(&t, &out);
#endif
    return out;
}

// ISO 8601 con zona horaria local, p. ej. 2026-11-08T14:03:22-06:00
// El desplazamiento se calcula a mano: %z de strftime en MinGW devuelve el nombre de la zona.
std::string iso_local(std::time_t t) {
    std::tm lt = to_local_tm(t);
    std::tm gt{};
#ifdef _WIN32
    gmtime_s(&gt, &t);
#else
    gmtime_r(&t, &gt);
#endif
    gt.tm_isdst = lt.tm_isdst;
    long off_min = static_cast<long>((static_cast<long long>(t) - static_cast<long long>(std::mktime(&gt))) / 60);
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", &lt);
    char zone[8];
    long a = off_min < 0 ? -off_min : off_min;
    std::snprintf(zone, sizeof(zone), "%c%02ld:%02ld", off_min < 0 ? '-' : '+', a / 60, a % 60);
    return std::string(buf) + zone;
}

std::string stamp_for_filename() {
    std::tm tm = to_local_tm(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now()));
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", &tm);
    return buf;
}

std::string file_modified_iso(const fs::path& p) {
#ifdef _WIN32
    // std::filesystem::last_write_time usa rutas estrechas en MinGW y falla con acentos.
    WIN32_FILE_ATTRIBUTE_DATA data{};
    if (!GetFileAttributesExW(p.wstring().c_str(), GetFileExInfoStandard, &data)) return "";
    ULARGE_INTEGER ticks;
    ticks.LowPart = data.ftLastWriteTime.dwLowDateTime;
    ticks.HighPart = data.ftLastWriteTime.dwHighDateTime;
    // 100 ns desde 1601-01-01 → segundos desde 1970-01-01
    return iso_local(static_cast<std::time_t>(ticks.QuadPart / 10000000ULL - 11644473600ULL));
#else
    std::error_code ec;
    auto ft = fs::last_write_time(p, ec);
    if (ec) return "";
    auto sys = std::chrono::system_clock::now() +
               std::chrono::duration_cast<std::chrono::system_clock::duration>(ft - fs::file_time_type::clock::now());
    return iso_local(std::chrono::system_clock::to_time_t(sys));
#endif
}

std::string name_of(const fs::path& p) {
    return p.filename().u8string();
}

void remove_with_sidecars(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
    fs::remove(fs::path(p.u8string() + "-wal"), ec);
    fs::remove(fs::path(p.u8string() + "-shm"), ec);
}

struct OperationGuard {
    std::atomic<bool>& flag;
    bool acquired;
    explicit OperationGuard(std::atomic<bool>& f) : flag(f), acquired(!f.exchange(true)) {}
    ~OperationGuard() { if (acquired) flag.store(false); }
    OperationGuard(const OperationGuard&) = delete;
    OperationGuard& operator=(const OperationGuard&) = delete;
};


} // namespace

SqliteBackupService::SqliteBackupService(
    storage::SqliteDatabase& db,
    std::shared_ptr<contracts::IAccountService> account_service,
    std::string data_dir,
    std::string migrations_dir,
    std::function<void()> on_restored
) : db_(db),
    account_service_(std::move(account_service)),
    data_dir_(std::move(data_dir)),
    migrations_dir_(std::move(migrations_dir)),
    on_restored_(std::move(on_restored)) {
}

bool SqliteBackupService::has_permission(const char* key) const {
    return account_service_ && account_service_->has_permission(key);
}

contracts::Result<contracts::BackupStatusDto> SqliteBackupService::get_status() {
    auto prep = db_.prepare(
        "SELECT is_enabled, last_backup_at, last_status, last_error FROM backup_state WHERE id = 1;");
    if (!prep.is_success()) {
        return contracts::Result<contracts::BackupStatusDto>::fail(prep.error_code(), prep.error_message());
    }
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::Result<contracts::BackupStatusDto>::fail(step.error_code(), step.error_message());
    }
    contracts::BackupStatusDto dto;
    if (step.value()) {
        dto.is_enabled = stmt.get_int64(0) != 0;
        dto.last_backup_time = stmt.is_null(1) ? "Nunca" : stmt.get_text(1);
        dto.last_status = stmt.get_text(2);
        dto.last_error = stmt.get_text(3);
    } else {
        dto.last_backup_time = "Nunca";
        dto.last_status = "PENDIENTE";
    }
    return contracts::Result<contracts::BackupStatusDto>::ok(dto);
}

contracts::OperationResult SqliteBackupService::write_state(
    const char* status, const std::string& error, bool set_time) {
    std::string sql = set_time
        ? "UPDATE backup_state SET last_status = ?, last_error = ?, last_backup_at = ? WHERE id = 1;"
        : "UPDATE backup_state SET last_status = ?, last_error = ? WHERE id = 1;";
    auto prep = db_.prepare(sql);
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, status);
    stmt.bind_text(2, error);
    if (set_time) {
        stmt.bind_text(3, iso_local(std::chrono::system_clock::to_time_t(std::chrono::system_clock::now())));
    }
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }
    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteBackupService::create_verified_snapshot(const fs::path& dest) {
    std::error_code ec;
    if (fs::is_directory(dest, ec)) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput, "El destino del respaldo es una carpeta, no un archivo");
    }
    if (dest.has_parent_path()) {
        fs::create_directories(dest.parent_path(), ec);
    }
    // Se escribe en un archivo temporal para no destruir un respaldo previo si algo falla.
    fs::path tmp = fs::path(dest.u8string() + ".tmp");
    remove_with_sidecars(tmp);

    auto res = db_.backup_to(tmp);
    if (!res.is_success()) {
        remove_with_sidecars(tmp);
        return res;
    }
    // El temporal se deja como archivo único (checkpoint + journal_mode=DELETE) ANTES de verificarlo:
    // así la verificación se hace sobre el archivo final y no queda ningún -wal/-shm junto al destino.
    auto single = storage::SqliteDatabase::make_single_file(tmp);
    if (!single.is_success()) {
        remove_with_sidecars(tmp);
        return contracts::OperationResult::fail(contracts::ErrorCode::BackupFailure, single.error_message());
    }
    auto info = storage::SqliteDatabase::inspect_file(tmp);
    if (!info.is_success() || !info.value().integrity_ok) {
        remove_with_sidecars(tmp);
        return contracts::OperationResult::fail(
            contracts::ErrorCode::BackupFailure,
            "El respaldo creado no pasó la verificación de integridad y fue eliminado");
    }
    remove_with_sidecars(dest);
    fs::rename(tmp, dest, ec);
    if (ec) {
        remove_with_sidecars(tmp);
        return contracts::OperationResult::fail(
            contracts::ErrorCode::BackupFailure, "No se pudo colocar el respaldo en su destino");
    }
    // Defensivo: tras el rename no debe quedar nada con el nombre temporal.
    remove_with_sidecars(tmp);
    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteBackupService::trigger_local_backup(const std::string& destination_path) {
    if (!has_permission("backup.manage")) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden, "Operación denegada: falta de permisos (backup.manage)");
    }
    if (destination_path.empty()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "Ruta de destino inválida");
    }
    OperationGuard op_guard(operation_in_progress_);
    if (!op_guard.acquired) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Conflict,
            "Hay un respaldo, una restauración o una limpieza en curso. Intente de nuevo en unos momentos.");
    }
    fs::path dest = fs::u8path(destination_path);
    auto res = create_verified_snapshot(dest);
    if (!res.is_success()) {
        write_state("FALLIDA", res.error_message(), false);
        account_service_->record_audit_event("BACKUP_LOCAL_FAILED", "Respaldo local fallido: " + name_of(dest));
        return res;
    }
    auto st = write_state("EXITOSA", "", true);
    if (!st.is_success()) return st;
    // Registro de lo que la aplicación creó (si falla, el respaldo simplemente no será candidato a limpieza)
    register_backup_file("LOCAL", dest);
    account_service_->record_audit_event("BACKUP_LOCAL", "Respaldo local creado y verificado: " + name_of(dest));
    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteBackupService::set_backup_enabled(bool enabled) {
    if (!has_permission("backup.manage")) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden, "Operación denegada: falta de permisos (backup.manage)");
    }
    auto prep = db_.prepare("UPDATE backup_state SET is_enabled = ? WHERE id = 1;");
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }
    auto stmt = std::move(prep.value());
    stmt.bind_int64(1, enabled ? 1 : 0);
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }
    account_service_->record_audit_event(enabled ? "BACKUP_ENABLED" : "BACKUP_DISABLED",
                                         enabled ? "Respaldo activado" : "Respaldo desactivado");
    return contracts::OperationResult::ok();
}

contracts::Result<int> SqliteBackupService::current_schema_version() {
    storage::MigrationRunner runner(db_);
    auto applied = runner.get_applied_versions();
    if (!applied.is_success()) {
        return contracts::Result<int>::fail(applied.error_code(), applied.error_message());
    }
    int max_v = 0;
    for (int v : applied.value()) max_v = std::max(max_v, v);
    return contracts::Result<int>::ok(max_v);
}

contracts::Result<contracts::BackupVerificationDto> SqliteBackupService::verify_backup(const std::string& path) {
    using R = contracts::Result<contracts::BackupVerificationDto>;
    if (!has_permission("backup.manage") && !has_permission("backup.restore")) {
        return R::fail(contracts::ErrorCode::Forbidden,
                       "Operación denegada: falta de permisos (backup.manage o backup.restore)");
    }
    if (path.empty()) {
        return R::fail(contracts::ErrorCode::InvalidInput, "Ruta de respaldo inválida");
    }
    fs::path p = fs::u8path(path);
    auto info = storage::SqliteDatabase::inspect_file(p);
    if (!info.is_success()) {
        return R::fail(info.error_code(), info.error_message());
    }
    contracts::BackupVerificationDto dto;
    dto.integrity_ok = info.value().integrity_ok;
    dto.schema_version = info.value().schema_version;
    dto.file_modified_at = file_modified_iso(p);
    if (!dto.integrity_ok) {
        dto.detail = "Integridad fallida: " + info.value().integrity_message;
    } else if (!info.value().has_expected_tables) {
        dto.detail = "Faltan tablas esperadas (" + info.value().missing_table + ")";
    } else if (!info.value().has_active_owner) {
        dto.detail = "El respaldo no contiene una cuenta Dueño activa";
    }
    return R::ok(dto);
}

bool SqliteBackupService::recover_from(const fs::path& pre_backup) {
    return db_.restore_from(pre_backup).is_success();
}

contracts::Result<contracts::RestoreResultDto> SqliteBackupService::restore_impl(
    const fs::path& src, const std::string&) {
    using R = contracts::Result<contracts::RestoreResultDto>;
    using EC = contracts::ErrorCode;

    // --- Validaciones: nada de la base viva se toca todavía ---
    auto info_res = storage::SqliteDatabase::inspect_file(src);
    if (!info_res.is_success()) return R::fail(info_res.error_code(), info_res.error_message());
    const auto& info = info_res.value();
    if (!info.integrity_ok) {
        return R::fail(EC::BackupFailure, "El respaldo está dañado: " + info.integrity_message);
    }
    if (!info.has_expected_tables) {
        return R::fail(EC::BackupFailure, "El respaldo no es una base de Cafetería ITCH (falta " + info.missing_table + ")");
    }
    if (!info.has_active_owner) {
        return R::fail(EC::BackupFailure, "El respaldo no contiene una cuenta Dueño activa");
    }
    auto cur_ver = current_schema_version();
    if (!cur_ver.is_success()) return R::fail(cur_ver.error_code(), cur_ver.error_message());
    if (info.schema_version > cur_ver.value()) {
        return R::fail(EC::BackupFailure, "El respaldo es de una versión más nueva de la aplicación; actualice antes de restaurar");
    }
    auto live_page = db_.page_size();
    if (!live_page.is_success()) return R::fail(live_page.error_code(), live_page.error_message());
    if (info.page_size != live_page.value()) {
        return R::fail(EC::BackupFailure, "El tamaño de página del respaldo no coincide con la base actual");
    }
    const bool needs_migration = info.schema_version < cur_ver.value();
    if (needs_migration) {
        std::error_code ec;
        if (migrations_dir_.empty() || !fs::exists(fs::u8path(migrations_dir_), ec)) {
            return R::fail(EC::InvalidState, "El respaldo es de una versión anterior y no hay carpeta de migraciones disponible");
        }
    }
    if (data_dir_.empty()) {
        return R::fail(EC::InvalidState, "No hay directorio de datos configurado para el respaldo previo");
    }

    // Conservar el estado de activación del respaldo en la base viva.
    bool was_enabled = false;
    {
        auto st = get_status();
        if (st.is_success()) was_enabled = st.value().is_enabled;
    }

    // --- Respaldo previo automático, verificado ---
    fs::path backups_dir = fs::u8path(data_dir_) / "backups";
    std::error_code ec;
    fs::create_directories(backups_dir, ec);
    std::string stamp = stamp_for_filename();
    fs::path pre_path = backups_dir / fs::u8path("pre_restore_" + stamp + ".db");
    for (int n = 1; fs::exists(pre_path, ec); ++n) {
        pre_path = backups_dir / fs::u8path("pre_restore_" + stamp + "_" + std::to_string(n) + ".db");
    }
    auto pre = create_verified_snapshot(pre_path);
    if (!pre.is_success()) {
        return R::fail(EC::BackupFailure, "No se pudo crear el respaldo previo: " + pre.error_message());
    }

    // --- Copia sobre la conexión viva ---
    auto copy = db_.restore_from(src);
    if (!copy.is_success()) {
        bool rec = recover_from(pre_path);
        return R::fail(EC::BackupFailure, "La restauración falló (" + copy.error_message() + "); " +
            (rec ? "se recuperó el estado anterior" : "NO se pudo recuperar automáticamente; use el respaldo previo " + name_of(pre_path)));
    }

    auto fail_and_recover = [&](const std::string& why) {
        bool rec = recover_from(pre_path);
        return R::fail(EC::BackupFailure, why + "; " +
            (rec ? "se recuperó el estado anterior" : "NO se pudo recuperar automáticamente; use el respaldo previo " + name_of(pre_path)));
    };

    if (needs_migration) {
        storage::MigrationRunner runner(db_);
        auto mig = runner.run_pending_migrations_from_dir(fs::u8path(migrations_dir_));
        if (!mig.is_success()) {
            return fail_and_recover("Falló la migración posterior a la restauración (" + mig.error_message() + ")");
        }
    }

    {
        auto prep = db_.prepare("UPDATE backup_state SET is_enabled = ? WHERE id = 1;");
        if (!prep.is_success()) {
            return fail_and_recover("No se pudo conservar la configuración de respaldo");
        }
        auto stmt = std::move(prep.value());
        stmt.bind_int64(1, was_enabled ? 1 : 0);
        if (!stmt.step().is_success()) {
            return fail_and_recover("No se pudo conservar la configuración de respaldo");
        }
    }

    // La base restaurada trae su propio registro: se vuelve a registrar el respaldo previo en ella.
    register_backup_file("PRE_RESTORE", pre_path);

    // Auditoría en la base restaurada, antes de cerrar la sesión.
    auto audit = account_service_->record_audit_event(
        "BACKUP_RESTORED",
        "Restauración desde " + name_of(src) + "; respaldo previo: " + name_of(pre_path));
    if (!audit.is_success()) {
        return fail_and_recover("No se pudo registrar la auditoría de la restauración");
    }

    if (on_restored_) on_restored_();
    account_service_->logout();

    contracts::RestoreResultDto dto;
    dto.requires_relogin = true;
    dto.pre_restore_backup_path = pre_path.u8string();
    return R::ok(dto);
}

contracts::Result<contracts::RestoreResultDto> SqliteBackupService::restore_from_backup(const std::string& path) {
    using R = contracts::Result<contracts::RestoreResultDto>;
    if (!has_permission("backup.restore")) {
        return R::fail(contracts::ErrorCode::Forbidden,
                       "Operación denegada: falta de permisos (backup.restore)");
    }
    if (path.empty()) {
        return R::fail(contracts::ErrorCode::InvalidInput, "Ruta de respaldo inválida");
    }
    OperationGuard op_guard(operation_in_progress_);
    if (!op_guard.acquired) {
        return R::fail(contracts::ErrorCode::Conflict,
                       "Hay un respaldo, una restauración o una limpieza en curso. Intente de nuevo en unos momentos.");
    }
    fs::path src = fs::u8path(path);
    auto res = restore_impl(src, name_of(src));
    if (!res.is_success()) {
        account_service_->record_audit_event("BACKUP_RESTORE_FAILED", "Restauración fallida: " + name_of(src));
    }
    return res;
}

// ---------------------------------------------------------------------------------------------
// Registro y limpieza de respaldos locales
// ---------------------------------------------------------------------------------------------

namespace {

struct FileFingerprint {
    std::int64_t size{0};
    std::string sha256;
};

// Tamaño y SHA-256 de un archivo regular. false si no existe, no es regular o no se puede leer.
bool fingerprint_file(const fs::path& p, FileFingerprint& out) {
    std::error_code ec;
    if (!fs::is_regular_file(p, ec)) return false;
    std::ifstream in(p, std::ios::binary);
    if (!in) return false;
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    out.size = static_cast<std::int64_t>(bytes.size());
    out.sha256 = crypto::PasswordHasher::to_hex(crypto::PasswordHasher::sha256(bytes.data(), bytes.size()));
    return true;
}

struct RegisteredBackup {
    std::string id;
    std::string kind;
    std::string path;
    std::int64_t size_bytes{0};
    std::string sha256;
    std::string created_at;
    std::string created_at_utc;
};

// El archivo en la ruta registrada sigue siendo el mismo que se registró.
bool matches_registration(const RegisteredBackup& r) {
    FileFingerprint fp;
    if (!fingerprint_file(fs::u8path(r.path), fp)) return false;
    return fp.size == r.size_bytes && fp.sha256 == r.sha256;
}

contracts::BackupCleanupItemDto to_item(const RegisteredBackup& r) {
    contracts::BackupCleanupItemDto item;
    item.path = r.path;
    item.kind = r.kind;
    item.created_at = r.created_at;
    item.size_bytes = r.size_bytes;
    return item;
}

// Instante UTC (mismo formato que created_at_utc) de hace `days` días.
std::string utc_cutoff(int days) {
    auto tp = std::chrono::system_clock::now() - std::chrono::hours(24LL * days);
    return domain::timestamp_from(tp).utc;
}

std::string trim_text(const std::string& s) {
    const char* ws = " \t\r\n";
    const auto first = s.find_first_not_of(ws);
    if (first == std::string::npos) return "";
    return s.substr(first, s.find_last_not_of(ws) - first + 1);
}

// Devuelve un mensaje de error en lenguaje llano o cadena vacía si la configuración es válida.
std::string validate_server_config(const contracts::BackupServerConfigDto& c) {
    const std::string url = trim_text(c.server_url);
    const std::string days = trim_text(c.retention_days);
    if (url.empty()) {
        if (!days.empty() && days.find_first_not_of("0123456789") != std::string::npos) {
            return "Los días de retención deben ser un número entero";
        }
        return "";
    }
    if (url.size() > 2048) return "La dirección del servidor es demasiado larga";
    for (unsigned char ch : url) {
        if (ch <= 0x20 || ch == 0x7F || ch == '\\') return "La dirección del servidor contiene caracteres no permitidos";
    }
    std::string rest;
    if (url.compare(0, 8, "https://") == 0) {
        rest = url.substr(8);
    } else if (url.compare(0, 7, "http://") == 0) {
        if (c.is_tls_enabled) return "Con TLS activado la dirección debe empezar con https://";
        rest = url.substr(7);
    } else {
        return "La dirección debe empezar con https:// (o http:// si se desactiva TLS)";
    }
    if (url.find('?') != std::string::npos || url.find('#') != std::string::npos) {
        return "La dirección no puede incluir parámetros (?) ni fragmentos (#)";
    }
    const auto slash = rest.find('/');
    const std::string authority = rest.substr(0, slash);
    if (authority.empty()) return "Falta el nombre del servidor en la dirección";
    if (authority.find('@') != std::string::npos) {
        return "La dirección no puede incluir usuario ni contraseña; las credenciales no se guardan aquí";
    }
    if (days.empty() || days.size() > 4 || days.find_first_not_of("0123456789") != std::string::npos) {
        return "Los días de retención deben ser un número entero entre 1 y 3650";
    }
    const int d = std::atoi(days.c_str());
    if (d < 1 || d > 3650) return "Los días de retención deben estar entre 1 y 3650";
    return "";
}

} // namespace

contracts::OperationResult SqliteBackupService::register_backup_file(const char* kind, const fs::path& path) {
    std::error_code ec;
    fs::path abs = fs::absolute(path, ec);
    if (ec) abs = path;
    abs = abs.lexically_normal();
    FileFingerprint fp;
    if (!fingerprint_file(abs, fp)) {
        return contracts::OperationResult::fail(contracts::ErrorCode::StorageFailure, "No se pudo leer el respaldo para registrarlo");
    }
    const std::string path_text = abs.u8string();
    const auto ts = domain::now();

    {
        auto prep = db_.prepare("UPDATE backup_files SET status = 'SUPERSEDED', status_at = ? WHERE path = ? AND status = 'ACTIVE';");
        if (!prep.is_success()) return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, ts.local);
        stmt.bind_text(2, path_text);
        auto step = stmt.step();
        if (!step.is_success()) return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }
    auto prep = db_.prepare(
        "INSERT INTO backup_files (id, kind, path, size_bytes, sha256, created_at, created_at_utc) VALUES (?, ?, ?, ?, ?, ?, ?);");
    if (!prep.is_success()) return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, generate_id("bkf"));
    stmt.bind_text(2, kind);
    stmt.bind_text(3, path_text);
    stmt.bind_int64(4, fp.size);
    stmt.bind_text(5, fp.sha256);
    stmt.bind_text(6, ts.local);
    stmt.bind_text(7, ts.utc);
    auto step = stmt.step();
    if (!step.is_success()) return contracts::OperationResult::fail(step.error_code(), step.error_message());
    return contracts::OperationResult::ok();
}

contracts::Result<contracts::BackupRetentionPolicyDto> SqliteBackupService::get_retention_policy() {
    using R = contracts::Result<contracts::BackupRetentionPolicyDto>;
    if (!has_permission("backup.manage")) {
        return R::fail(contracts::ErrorCode::Forbidden, "Operación denegada: falta de permisos (backup.manage)");
    }
    auto prep = db_.prepare("SELECT keep_last, keep_days FROM backup_state WHERE id = 1;");
    if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
    contracts::BackupRetentionPolicyDto dto;
    if (step.value()) {
        dto.keep_last = static_cast<int>(stmt.get_int64(0));
        dto.keep_days = static_cast<int>(stmt.get_int64(1));
    }
    return R::ok(dto);
}

contracts::OperationResult SqliteBackupService::set_retention_policy(const contracts::BackupRetentionPolicyDto& policy) {
    if (!has_permission("backup.manage")) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden, "Operación denegada: falta de permisos (backup.manage)");
    }
    if (policy.keep_last < 1 || policy.keep_last > 1000) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "La cantidad de respaldos a conservar debe estar entre 1 y 1000");
    }
    if (policy.keep_days < 0 || policy.keep_days > 3650) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "Los días a conservar deben estar entre 0 y 3650");
    }
    auto prep = db_.prepare("UPDATE backup_state SET keep_last = ?, keep_days = ? WHERE id = 1;");
    if (!prep.is_success()) return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    auto stmt = std::move(prep.value());
    stmt.bind_int64(1, policy.keep_last);
    stmt.bind_int64(2, policy.keep_days);
    auto step = stmt.step();
    if (!step.is_success()) return contracts::OperationResult::fail(step.error_code(), step.error_message());
    account_service_->record_audit_event(
        "BACKUP_RETENTION_POLICY",
        "Política de limpieza de respaldos: conservar los últimos " + std::to_string(policy.keep_last) +
            " de cada tipo y todo lo de los últimos " + std::to_string(policy.keep_days) + " días");
    return contracts::OperationResult::ok();
}

contracts::Result<contracts::BackupCleanupResultDto> SqliteBackupService::run_cleanup(bool dry_run) {
    using R = contracts::Result<contracts::BackupCleanupResultDto>;
    if (!has_permission("backup.manage")) {
        return R::fail(contracts::ErrorCode::Forbidden, "Operación denegada: falta de permisos (backup.manage)");
    }
    OperationGuard guard(operation_in_progress_);
    if (!guard.acquired) {
        return R::fail(contracts::ErrorCode::Conflict,
                       "Hay un respaldo, una restauración o una limpieza en curso. Intente de nuevo en unos momentos.");
    }

    auto policy = get_retention_policy();
    if (!policy.is_success()) return R::fail(policy.error_code(), policy.error_message());
    const int keep_last = policy.value().keep_last;
    const int keep_days = policy.value().keep_days;
    const std::string cutoff = keep_days > 0 ? utc_cutoff(keep_days) : std::string();

    contracts::BackupCleanupResultDto result;
    result.dry_run = dry_run;
    const auto stamp = domain::now();

    auto mark = [&](const std::string& id, const char* status, const std::string& error) {
        auto prep = db_.prepare("UPDATE backup_files SET status = ?, status_at = ?, last_error = ? WHERE id = ?;");
        if (!prep.is_success()) return false;
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, status);
        stmt.bind_text(2, stamp.local);
        stmt.bind_text(3, error);
        stmt.bind_text(4, id);
        return stmt.step().is_success();
    };
    auto note_error = [&](const std::string& id, const std::string& error) {
        auto prep = db_.prepare("UPDATE backup_files SET last_error = ? WHERE id = ?;");
        if (!prep.is_success()) return;
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, error);
        stmt.bind_text(2, id);
        stmt.step();
    };

    for (const char* kind : {"LOCAL", "PRE_RESTORE"}) {
        std::vector<RegisteredBackup> rows;
        {
            auto prep = db_.prepare(
                "SELECT id, kind, path, size_bytes, sha256, created_at, created_at_utc FROM backup_files "
                "WHERE kind = ? AND status = 'ACTIVE' ORDER BY created_at_utc DESC, rowid DESC;");
            if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, kind);
            while (true) {
                auto step = stmt.step();
                if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
                if (!step.value()) break;
                RegisteredBackup r;
                r.id = stmt.get_text(0);
                r.kind = stmt.get_text(1);
                r.path = stmt.get_text(2);
                r.size_bytes = stmt.get_int64(3);
                r.sha256 = stmt.get_text(4);
                r.created_at = stmt.get_text(5);
                r.created_at_utc = stmt.get_text(6);
                rows.push_back(std::move(r));
            }
        }
        if (rows.empty()) continue;

        // El respaldo más reciente que sigue siendo el registrado y pasa la verificación de integridad
        // nunca se borra. Si ninguno es válido, no se borra nada de este tipo.
        std::size_t protected_idx = rows.size();
        for (std::size_t i = 0; i < rows.size(); ++i) {
            if (!matches_registration(rows[i])) continue;
            auto info = storage::SqliteDatabase::inspect_file(fs::u8path(rows[i].path));
            if (info.is_success() && info.value().integrity_ok) {
                protected_idx = i;
                break;
            }
        }
        if (protected_idx == rows.size()) {
            result.kept_count += static_cast<int>(rows.size());
            continue;
        }

        for (std::size_t i = 0; i < rows.size(); ++i) {
            const RegisteredBackup& r = rows[i];
            const bool kept_by_count = i < static_cast<std::size_t>(keep_last);
            const bool kept_by_age = keep_days > 0 && r.created_at_utc >= cutoff;
            if (i == protected_idx || kept_by_count || kept_by_age) {
                ++result.kept_count;
                continue;
            }

            std::error_code ec;
            const fs::path file = fs::u8path(r.path);
            if (!fs::exists(file, ec)) {
                ++result.missing_count;
                if (!dry_run) mark(r.id, "DELETED", "El archivo ya no existía");
                continue;
            }
            if (!matches_registration(r)) {
                result.changed.push_back(to_item(r));
                if (!dry_run) mark(r.id, "CHANGED", "El archivo cambió desde que se registró: no se borró");
                continue;
            }
            if (dry_run) {
                result.to_delete.push_back(to_item(r));
                result.bytes_freed += r.size_bytes;
                continue;
            }
            std::error_code rm_ec;
            const bool removed = fs::remove(file, rm_ec);
            if (rm_ec || !removed) {
                result.failed.push_back(to_item(r));
                note_error(r.id, "No se pudo borrar: " + (rm_ec ? rm_ec.message() : std::string("el archivo no se eliminó")));
                continue;
            }
            // Si no se pudiera marcar la fila, el archivo ya no existe y la próxima limpieza la cerrará como faltante.
            mark(r.id, "DELETED", "");
            result.to_delete.push_back(to_item(r));
            result.bytes_freed += r.size_bytes;
        }
    }

    if (!dry_run) {
        account_service_->record_audit_event(
            "BACKUP_CLEANUP",
            "Limpieza de respaldos: borrados " + std::to_string(result.to_delete.size()) +
                ", cambiados (no borrados) " + std::to_string(result.changed.size()) +
                ", con error " + std::to_string(result.failed.size()) +
                ", conservados " + std::to_string(result.kept_count) +
                ", liberados " + std::to_string(result.bytes_freed) + " bytes");
    }
    return R::ok(std::move(result));
}

contracts::Result<contracts::BackupCleanupResultDto> SqliteBackupService::preview_backup_cleanup() {
    return run_cleanup(true);
}

contracts::Result<contracts::BackupCleanupResultDto> SqliteBackupService::cleanup_old_backups() {
    return run_cleanup(false);
}

contracts::Result<contracts::BackupServerConfigDto> SqliteBackupService::get_developer_server_config() {
    // Comprobación de seguridad en la capa de servicios:
    // "Ocultar un botón no sustituye una autorización."
    using R = contracts::Result<contracts::BackupServerConfigDto>;
    if (!account_service_ || !account_service_->has_permission("backup.dev_config")) {
        return R::fail(
            contracts::ErrorCode::Forbidden,
            "Acceso denegado: la configuración del servidor de respaldos es exclusiva de la identidad Desarrollador"
        );
    }
    auto prep = db_.prepare("SELECT server_url, retention_days, is_tls_enabled FROM backup_server_config WHERE id = 1;");
    if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
    contracts::BackupServerConfigDto dto;  // vacío hasta que el Desarrollador la configure
    if (step.value()) {
        dto.server_url = stmt.get_text(0);
        dto.retention_days = stmt.get_text(1);
        dto.is_tls_enabled = stmt.get_int64(2) != 0;
    }
    return R::ok(dto);
}

contracts::OperationResult SqliteBackupService::update_developer_server_config(
    const contracts::BackupServerConfigDto& config
) {
    if (!account_service_ || !account_service_->has_permission("backup.dev_config")) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Acceso denegado: la configuración del servidor de respaldos es exclusiva de la identidad Desarrollador"
        );
    }
    const std::string problem = validate_server_config(config);
    if (!problem.empty()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, problem);
    }
    const std::string url = trim_text(config.server_url);
    const std::string days = trim_text(config.retention_days);

    std::string actor;
    auto cur = account_service_->current_user();
    if (cur.is_success()) actor = cur.value().id;

    auto prep = db_.prepare(
        "UPDATE backup_server_config SET server_url = ?, retention_days = ?, is_tls_enabled = ?, updated_at = ?, updated_by = ? WHERE id = 1;");
    if (!prep.is_success()) return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, url);
    stmt.bind_text(2, days);
    stmt.bind_int64(3, config.is_tls_enabled ? 1 : 0);
    stmt.bind_text(4, domain::now().local);
    stmt.bind_text(5, actor);
    auto step = stmt.step();
    if (!step.is_success()) return contracts::OperationResult::fail(step.error_code(), step.error_message());

    account_service_->record_audit_event(
        "BACKUP_SERVER_CONFIG_CHANGED",
        "Configuración del servidor de respaldos actualizada: " + (url.empty() ? std::string("sin configurar") : url) +
            ", retención " + (days.empty() ? std::string("-") : days) + " días, TLS " + (config.is_tls_enabled ? "sí" : "no"));
    return contracts::OperationResult::ok();
}

} // namespace app::services
