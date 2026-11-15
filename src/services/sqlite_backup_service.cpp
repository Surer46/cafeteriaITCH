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
#include <cstring>
#include <mutex>
#include <thread>
#include <fstream>
#include <iterator>
#include <vector>
#include "app/crypto/password_hasher.hpp"
#include "app/domain/clock.hpp"
#include "app/services/file_fingerprint.hpp"
#include "app/services/identifiers.hpp"
#include "app/storage/migration_runner.hpp"
#include "sqlite3.h"

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

// Estado compartido con el hilo del respaldo automático. Lo mantiene vivo el propio hilo (shared_ptr),
// de modo que el servicio puede destruirse sin esperar a que termine una copia larga.
struct SqliteBackupService::AutoWorker {
    std::mutex mutex;                    // protege active/rerun/ids/db_file
    bool active{false};                  // hay un hilo trabajando
    bool rerun{false};                   // llegó otro cierre de turno mientras trabajaba
    std::string next_shift_id;
    std::string next_user_id;
    std::string db_file;                 // ruta (UTF-8) del archivo de la base
    fs::path data_dir;                   // carpeta de datos (los automáticos van en backups\automaticos)
    std::shared_ptr<std::atomic<bool>> op_flag;  // la misma guardia que el respaldo manual
    std::atomic<bool> cancel{false};
};

namespace {
void remove_stale_tmp(const fs::path& dir);
}

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
    auto_worker_ = std::make_shared<AutoWorker>();
    auto_worker_->op_flag = operation_in_progress_;
    if (!data_dir_.empty()) {
        auto_worker_->data_dir = fs::u8path(data_dir_);
        // Archivos temporales huérfanos de un respaldo automático que quedó a medias al cerrarse el programa.
        remove_stale_tmp(auto_worker_->data_dir / "backups" / "automaticos");
    }
}

bool SqliteBackupService::has_permission(const char* key) const {
    return account_service_ && account_service_->has_permission(key);
}

contracts::Result<contracts::BackupStatusDto> SqliteBackupService::get_status() {
    if (!account_service_ || !account_service_->is_authenticated()) {
        return contracts::Result<contracts::BackupStatusDto>::fail(contracts::ErrorCode::Forbidden, "Se requiere inicio de sesión");
    }
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
    OperationGuard op_guard(*operation_in_progress_);
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
    } else {
        auto cur_ver = current_schema_version();
        if (cur_ver.is_success() && info.value().schema_version > cur_ver.value()) {
            dto.detail = "El respaldo es de una versión más nueva del programa; actualice el programa antes de restaurarlo";
        } else if (!info.value().has_active_owner) {
            dto.detail = "El respaldo no contiene una cuenta Dueño activa";
        }
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
    OperationGuard op_guard(*operation_in_progress_);
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

struct RegisteredBackup {
    std::string id;
    std::string kind;
    std::string path;
    std::int64_t size_bytes{0};
    std::string sha256;
    std::string created_at;
    std::string created_at_utc;
    std::string destination{"DATA"};
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
    item.destination = r.destination;
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

namespace {

// Núcleo de la limpieza, sobre la conexión recibida (la de la interfaz o la del hilo del respaldo
// automático). Sin permisos, sin guardia de operación y sin bitácora: lo hacen quienes lo llaman.
// Se aplica por (tipo, destino): de cada uno nunca se borra el respaldo verificado más reciente.
contracts::OperationResult cleanup_core(storage::SqliteDatabase& db, const std::vector<std::string>& kinds,
                                        bool dry_run, contracts::BackupCleanupResultDto& result) {
    using EC = contracts::ErrorCode;
    int keep_last = 30;
    int keep_days = 90;
    {
        auto prep = db.prepare("SELECT keep_last, keep_days FROM backup_state WHERE id = 1;");
        if (!prep.is_success()) return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        auto step = stmt.step();
        if (!step.is_success()) return contracts::OperationResult::fail(step.error_code(), step.error_message());
        if (step.value()) {
            keep_last = static_cast<int>(stmt.get_int64(0));
            keep_days = static_cast<int>(stmt.get_int64(1));
        }
    }
    const std::string cutoff = keep_days > 0 ? utc_cutoff(keep_days) : std::string();
    result.dry_run = dry_run;
    const auto stamp = domain::now();

    auto mark = [&](const std::string& id, const char* status, const std::string& error) {
        auto prep = db.prepare("UPDATE backup_files SET status = ?, status_at = ?, last_error = ? WHERE id = ?;");
        if (!prep.is_success()) return false;
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, status);
        stmt.bind_text(2, stamp.local);
        stmt.bind_text(3, error);
        stmt.bind_text(4, id);
        return stmt.step().is_success();
    };
    auto note_error = [&](const std::string& id, const std::string& error) {
        auto prep = db.prepare("UPDATE backup_files SET last_error = ? WHERE id = ?;");
        if (!prep.is_success()) return;
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, error);
        stmt.bind_text(2, id);
        stmt.step();
    };

    // Bases anteriores a la migración 016 no tienen la columna destination: todo es de la carpeta de datos.
    const bool has_destination = db.prepare("SELECT destination FROM backup_files LIMIT 0;").is_success();
    for (const std::string& kind : kinds) {
        for (const char* destination : {"DATA", "SECONDARY"}) {
            if (!has_destination && std::string(destination) == "SECONDARY") continue;
            std::vector<RegisteredBackup> rows;
            {
                auto prep = db.prepare(has_destination
                    ? "SELECT id, kind, path, size_bytes, sha256, created_at, created_at_utc, destination FROM backup_files "
                      "WHERE kind = ? AND destination = ? AND status = 'ACTIVE' ORDER BY created_at_utc DESC, rowid DESC;"
                    : "SELECT id, kind, path, size_bytes, sha256, created_at, created_at_utc, 'DATA' FROM backup_files "
                      "WHERE kind = ? AND ? = 'DATA' AND status = 'ACTIVE' ORDER BY created_at_utc DESC, rowid DESC;");
                if (!prep.is_success()) return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
                auto stmt = std::move(prep.value());
                stmt.bind_text(1, kind);
                stmt.bind_text(2, destination);
                while (true) {
                    auto step = stmt.step();
                    if (!step.is_success()) return contracts::OperationResult::fail(step.error_code(), step.error_message());
                    if (!step.value()) break;
                    RegisteredBackup r;
                    r.id = stmt.get_text(0);
                    r.kind = stmt.get_text(1);
                    r.path = stmt.get_text(2);
                    r.size_bytes = stmt.get_int64(3);
                    r.sha256 = stmt.get_text(4);
                    r.created_at = stmt.get_text(5);
                    r.created_at_utc = stmt.get_text(6);
                    r.destination = stmt.get_text(7);
                    rows.push_back(std::move(r));
                }
            }
            if (rows.empty()) continue;
            // Nada que borrar si todos caben en "conservar los últimos N": no se verifica nada (verificar el más
            // reciente cuesta leer el archivo entero, y la limpieza automática corre tras cada respaldo).
            if (rows.size() <= static_cast<std::size_t>(keep_last)) {
                result.kept_count += static_cast<int>(rows.size());
                continue;
            }

            // El respaldo más reciente que sigue siendo el registrado y pasa la verificación de integridad
            // nunca se borra. Si ninguno es válido, no se borra nada de este tipo y destino.
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
    }
    (void)EC::Success;
    return contracts::OperationResult::ok();
}

} // namespace

contracts::Result<contracts::BackupCleanupResultDto> SqliteBackupService::run_cleanup(bool dry_run) {
    using R = contracts::Result<contracts::BackupCleanupResultDto>;
    if (!has_permission("backup.manage")) {
        return R::fail(contracts::ErrorCode::Forbidden, "Operación denegada: falta de permisos (backup.manage)");
    }
    OperationGuard guard(*operation_in_progress_);
    if (!guard.acquired) {
        return R::fail(contracts::ErrorCode::Conflict,
                       "Hay un respaldo, una restauración o una limpieza en curso. Intente de nuevo en unos momentos.");
    }

    contracts::BackupCleanupResultDto result;
    auto core = cleanup_core(db_, {"LOCAL", "PRE_RESTORE", "AUTO"}, dry_run, result);
    if (!core.is_success()) return R::fail(core.error_code(), core.error_message());

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


// ---------------------------------------------------------------------------------------------
// B12: respaldo automático al cerrar turno, segunda carpeta y lista de respaldos
// ---------------------------------------------------------------------------------------------

namespace {

constexpr int kAutoWaitLimitMs = 10 * 60 * 1000;  // espera máxima a otra operación de respaldo: 10 minutos

struct AutoRow {
    std::string state{"IDLE"};
    std::string started_at;
    std::string finished_at;
    std::string shift_id;
    bool local_ok{false};
    std::string local_file;
    std::string secondary_status{"NONE"};
    std::string error;
};

bool write_auto_row(storage::SqliteDatabase& db, const AutoRow& r) {
    auto prep = db.prepare(
        "UPDATE backup_state SET auto_state = ?, auto_started_at = ?, auto_finished_at = ?, auto_shift_id = ?, "
        "auto_local_ok = ?, auto_local_file = ?, auto_secondary_status = ?, auto_error = ? WHERE id = 1;");
    if (!prep.is_success()) return false;
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, r.state);
    if (r.started_at.empty()) stmt.bind_null(2); else stmt.bind_text(2, r.started_at);
    if (r.finished_at.empty()) stmt.bind_null(3); else stmt.bind_text(3, r.finished_at);
    stmt.bind_text(4, r.shift_id);
    stmt.bind_int64(5, r.local_ok ? 1 : 0);
    stmt.bind_text(6, r.local_file);
    stmt.bind_text(7, r.secondary_status);
    stmt.bind_text(8, r.error);
    return stmt.step().is_success();
}

// Bitácora escrita por el hilo con su propia conexión (el usuario se captura al cerrar el turno).
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

bool register_file_direct(storage::SqliteDatabase& db, const char* kind, const char* destination,
                          const fs::path& path, const FileFingerprint& fp) {
    std::error_code ec;
    fs::path abs = fs::absolute(path, ec);
    if (ec) abs = path;
    abs = abs.lexically_normal();
    const auto ts = domain::now();
    auto prep = db.prepare(
        "INSERT INTO backup_files (id, kind, path, size_bytes, sha256, created_at, created_at_utc, destination) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
    if (!prep.is_success()) return false;
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, generate_id("bkf"));
    stmt.bind_text(2, kind);
    stmt.bind_text(3, abs.u8string());
    stmt.bind_int64(4, fp.size);
    stmt.bind_text(5, fp.sha256);
    stmt.bind_text(6, ts.local);
    stmt.bind_text(7, ts.utc);
    stmt.bind_text(8, destination);
    return stmt.step().is_success();
}

void remove_stale_tmp(const fs::path& dir) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return;
    std::vector<fs::path> victims;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        const std::string n = e.path().filename().u8string();
        auto ends = [&](const char* suf) {
            const std::size_t len = std::strlen(suf);
            return n.size() >= len && n.compare(n.size() - len, len, suf) == 0;
        };
        if (ends(".tmp") || ends(".tmp-wal") || ends(".tmp-shm")) victims.push_back(e.path());
    }
    for (const auto& v : victims) fs::remove(v, ec);
}

// Copia consistente de la base por tandas, con la conexión de solo lectura del hilo. Entre tandas se mira
// la cancelación. Si la base cambia por otra conexión y SQLite reinicia la copia 3 veces, se hace en una
// sola pasada (instantánea consistente en WAL). Devuelve "" si salió bien; si no, el motivo.
std::string copy_database(sqlite3* src, const fs::path& dest, const std::atomic<bool>& cancel, bool& cancelled) {
    cancelled = false;
    for (int attempt = 0; attempt < 2; ++attempt) {
        const bool single_pass = attempt == 1;
        sqlite3* dest_db = nullptr;
#ifdef _WIN32
        int rc = sqlite3_open16(dest.wstring().c_str(), &dest_db);
#else
        int rc = sqlite3_open_v2(dest.c_str(), &dest_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
#endif
        if (rc != SQLITE_OK) {
            if (dest_db) sqlite3_close_v2(dest_db);
            return "No se pudo crear el archivo del respaldo";
        }
        sqlite3_busy_timeout(dest_db, 5000);
        sqlite3_backup* b = sqlite3_backup_init(dest_db, "main", src, "main");
        if (!b) {
            sqlite3_close_v2(dest_db);
            return "No se pudo iniciar la copia de la base";
        }
        bool restart_needed = false;
        int restarts = 0;
        int last_remaining = -1;
        int busy_retries = 0;
        if (single_pass) {
            rc = sqlite3_backup_step(b, -1);
        } else {
            while (true) {
                if (cancel.load()) {
                    cancelled = true;
                    break;
                }
                rc = sqlite3_backup_step(b, 256);
                if (rc == SQLITE_DONE) break;
                if (rc == SQLITE_OK) {
                    busy_retries = 0;
                    const int remaining = sqlite3_backup_remaining(b);
                    if (last_remaining >= 0 && remaining > last_remaining) ++restarts;  // SQLite reinició la copia
                    last_remaining = remaining;
                    if (restarts >= 3) {
                        restart_needed = true;
                        break;
                    }
                } else if ((rc == SQLITE_BUSY || rc == SQLITE_LOCKED) && busy_retries < 50) {
                    ++busy_retries;
                    sqlite3_sleep(100);
                } else {
                    break;
                }
            }
        }
        sqlite3_backup_finish(b);
        sqlite3_close_v2(dest_db);
        if (cancelled) return "";
        if (restart_needed) {
            std::error_code ec;
            fs::remove(dest, ec);
            fs::remove(fs::path(dest.u8string() + "-wal"), ec);
            fs::remove(fs::path(dest.u8string() + "-shm"), ec);
            continue;
        }
        if (rc != SQLITE_DONE) return "La copia de la base no se completó";
        return "";
    }
    return "La copia de la base no se completó";
}

} // namespace


SqliteBackupService::~SqliteBackupService() {
    if (auto_worker_) {
        auto_worker_->cancel.store(true);
        // No se espera a una copia larga: el hilo es dueño de su estado y, si no termina enseguida, termina
        // solo; lo que deje a medias es un .tmp sin registrar que se limpia al arrancar.
        wait_for_auto_backup(1500);
    }
}

bool SqliteBackupService::wait_for_auto_backup(int timeout_ms) {
    if (!auto_worker_) return true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (true) {
        {
            std::lock_guard<std::mutex> lock(auto_worker_->mutex);
            if (!auto_worker_->active) return true;
        }
        if (std::chrono::steady_clock::now() >= deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
}

void SqliteBackupService::on_shift_closed(const std::string& shift_id, const std::string& user_id) {
    try {
        if (!auto_worker_) return;
        // ¿Está activado? (lectura breve con la conexión de la interfaz)
        {
            auto prep = db_.prepare("SELECT is_enabled FROM backup_state WHERE id = 1;");
            if (!prep.is_success()) return;
            auto stmt = std::move(prep.value());
            auto step = stmt.step();
            if (!step.is_success() || !step.value() || stmt.get_int64(0) == 0) return;
        }
        const char* file = db_.handle() ? sqlite3_db_filename(db_.handle(), "main") : nullptr;
        AutoRow waiting;
        waiting.state = "WAITING";
        waiting.shift_id = shift_id;
        if (!file || !*file) {
            waiting.state = "FAILED";
            waiting.finished_at = domain::now().local;
            waiting.error = "No se pudo determinar dónde está la base de datos para hacer el respaldo automático";
            write_auto_row(db_, waiting);
            return;
        }
        write_auto_row(db_, waiting);  // el estado ya es consultable cuando close_shift vuelve

        bool start_thread = false;
        {
            std::lock_guard<std::mutex> lock(auto_worker_->mutex);
            auto_worker_->db_file = file;
            if (auto_worker_->data_dir.empty()) {
                auto_worker_->data_dir = fs::u8path(file).parent_path();
            }
            if (auto_worker_->active) {
                // Ya hay un respaldo automático en marcha: se repite al terminar con el cierre más reciente.
                auto_worker_->rerun = true;
                auto_worker_->next_shift_id = shift_id;
                auto_worker_->next_user_id = user_id;
            } else {
                auto_worker_->active = true;
                start_thread = true;
            }
        }
        if (start_thread) {
            auto_worker_->cancel.store(false);
            try {
                std::thread(&SqliteBackupService::run_auto_worker, auto_worker_, shift_id, user_id).detach();
            } catch (...) {
                {
                    std::lock_guard<std::mutex> lock(auto_worker_->mutex);
                    auto_worker_->active = false;
                }
                AutoRow failed;
                failed.state = "FAILED";
                failed.shift_id = shift_id;
                failed.finished_at = domain::now().local;
                failed.error = "No se pudo iniciar el respaldo automático";
                write_auto_row(db_, failed);
            }
        }
    } catch (...) {
        // Nunca afecta al cierre de turno.
    }
}

void SqliteBackupService::run_auto_worker(std::shared_ptr<AutoWorker> w, std::string shift_id, std::string user_id) {
    while (true) {
        try {
            storage::SqliteDatabase wdb;      // escrituras del hilo (estado, registro, bitácora): transacciones cortas
            storage::SqliteDatabase rdb;      // lectura de la copia consistente
            std::string db_file;
            fs::path data_dir;
            {
                std::lock_guard<std::mutex> lock(w->mutex);
                db_file = w->db_file;
                data_dir = w->data_dir;
            }
            AutoRow row;
            row.shift_id = shift_id;
            if (wdb.open(fs::u8path(db_file)).is_success() && rdb.open(fs::u8path(db_file)).is_success()) {
                // 1. Una sola operación de respaldo a la vez: espera (en este hilo, no en la ventana) hasta 10 min.
                bool acquired = false;
                const auto wait_start = std::chrono::steady_clock::now();
                while (!w->cancel.load()) {
                    if (!w->op_flag->exchange(true)) {
                        acquired = true;
                        break;
                    }
                    if (std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - wait_start).count() >
                        kAutoWaitLimitMs) {
                        break;
                    }
                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                }
                if (!acquired) {
                    row.state = "NOT_DONE";
                    row.finished_at = domain::now().local;
                    row.error = w->cancel.load()
                        ? "El programa se cerró mientras esperaba a que terminara otro respaldo: no se hizo el respaldo automático"
                        : "Otro respaldo o restauración no terminó en 10 minutos: no se hizo el respaldo automático";
                    write_auto_row(wdb, row);
                    write_audit_direct(wdb, user_id, "BACKUP_AUTO_FAILED", row.error);
                } else {
                    struct Release {
                        std::atomic<bool>& f;
                        ~Release() { f.store(false); }
                    } release{*w->op_flag};

                    row.state = "RUNNING";
                    row.started_at = domain::now().local;
                    write_auto_row(wdb, row);

                    // 2. Copia local verificada en backups\automaticos (temporal -> verificación -> nombre final).
                    const fs::path auto_dir = data_dir / "backups" / "automaticos";
                    std::error_code ec;
                    fs::create_directories(auto_dir, ec);
                    remove_stale_tmp(auto_dir);
                    std::string stamp = stamp_for_filename();
                    fs::path dest = auto_dir / ("auto-" + stamp + ".db");
                    for (int n = 2; fs::exists(dest, ec); ++n) dest = auto_dir / ("auto-" + stamp + "-" + std::to_string(n) + ".db");
                    const fs::path tmp = fs::path(dest.u8string() + ".tmp");

                    std::string local_error;
                    bool cancelled = false;
                    FileFingerprint local_fp;
                    if (!fs::is_directory(auto_dir, ec)) {
                        local_error = "No se pudo crear la carpeta de respaldos automáticos";
                    } else {
                        remove_with_sidecars(tmp);
                        local_error = copy_database(rdb.handle(), tmp, w->cancel, cancelled);
                        if (local_error.empty() && !cancelled) {
                            auto single = storage::SqliteDatabase::make_single_file(tmp);
                            if (!single.is_success()) {
                                local_error = "No se pudo dejar el respaldo como archivo único";
                            } else {
                                auto info = storage::SqliteDatabase::inspect_file(tmp);
                                if (!info.is_success() || !info.value().integrity_ok) {
                                    local_error = "El respaldo creado no pasó la verificación y se eliminó";
                                } else {
                                    fs::rename(tmp, dest, ec);
                                    if (ec) local_error = "No se pudo colocar el respaldo en su carpeta";
                                    else if (!fingerprint_file(dest, local_fp)) local_error = "No se pudo leer el respaldo para registrarlo";
                                }
                            }
                        }
                        if (cancelled || !local_error.empty()) remove_with_sidecars(tmp);
                    }

                    if (cancelled) {
                        row.state = "NOT_DONE";
                        row.finished_at = domain::now().local;
                        row.error = "El programa se cerró mientras se hacía el respaldo automático: no se completó";
                        write_auto_row(wdb, row);
                    } else if (!local_error.empty()) {
                        remove_with_sidecars(dest);
                        row.state = "FAILED";
                        row.finished_at = domain::now().local;
                        row.error = local_error;
                        write_auto_row(wdb, row);
                        auto st = wdb.prepare("UPDATE backup_state SET last_status = 'FALLIDA', last_error = ? WHERE id = 1;");
                        if (st.is_success()) { auto s = std::move(st.value()); s.bind_text(1, local_error); s.step(); }
                        write_audit_direct(wdb, user_id, "BACKUP_AUTO_FAILED", "Respaldo automático fallido: " + local_error);
                    } else {
                        // Registro (tras la verificación): nunca queda un archivo a medias registrado como válido.
                        register_file_direct(wdb, "AUTO", "DATA", dest, local_fp);
                        row.local_ok = true;
                        row.local_file = dest.filename().u8string();

                        // 3. Segunda carpeta (si el Dueño la configuró). La copia local ya existe pase lo que pase.
                        std::string secondary_dir;
                        {
                            auto prep = wdb.prepare("SELECT secondary_dir FROM backup_state WHERE id = 1;");
                            if (prep.is_success()) {
                                auto stmt = std::move(prep.value());
                                auto step = stmt.step();
                                if (step.is_success() && step.value()) secondary_dir = stmt.get_text(0);
                            }
                        }
                        row.secondary_status = "NONE";
                        std::string secondary_note;
                        if (!secondary_dir.empty()) {
                            const fs::path root = fs::u8path(secondary_dir);
                            if (!fs::is_directory(root, ec)) {
                                row.secondary_status = "UNAVAILABLE";
                                secondary_note = "La segunda carpeta no está disponible (¿está conectada la USB?)";
                            } else {
                                const fs::path probe = root / ".escritura-prueba-cafeteria.tmp";
                                bool writable = false;
                                {
                                    std::ofstream out(probe, std::ios::binary | std::ios::trunc);
                                    writable = out.is_open() && static_cast<bool>(out << "x");
                                }
                                fs::remove(probe, ec);
                                if (!writable) {
                                    row.secondary_status = "UNAVAILABLE";
                                    secondary_note = "No se puede escribir en la segunda carpeta (¿está protegida contra escritura?)";
                                } else {
                                    const fs::path sub = root / "CafeteriaITCH";
                                    fs::create_directories(sub, ec);
                                    remove_stale_tmp(sub);
                                    const fs::path sec_dest = sub / dest.filename();
                                    const fs::path sec_tmp = fs::path(sec_dest.u8string() + ".tmp");
                                    fs::copy_file(dest, sec_tmp, fs::copy_options::overwrite_existing, ec);
                                    FileFingerprint sec_fp;
                                    if (ec) {
                                        row.secondary_status = "FAILED";
                                        secondary_note = "No se pudo copiar a la segunda carpeta: " + ec.message();
                                    } else if (!fingerprint_file(sec_tmp, sec_fp) || sec_fp.size != local_fp.size ||
                                               sec_fp.sha256 != local_fp.sha256) {
                                        row.secondary_status = "FAILED";
                                        secondary_note = "La copia de la segunda carpeta no coincide con la original y se eliminó";
                                    } else {
                                        fs::rename(sec_tmp, sec_dest, ec);
                                        if (ec) {
                                            row.secondary_status = "FAILED";
                                            secondary_note = "No se pudo colocar la copia en la segunda carpeta";
                                        } else {
                                            register_file_direct(wdb, "AUTO", "SECONDARY", sec_dest, sec_fp);
                                            row.secondary_status = "OK";
                                        }
                                    }
                                    if (row.secondary_status != "OK") fs::remove(sec_tmp, ec);
                                }
                            }
                        }

                        row.state = "DONE";
                        row.finished_at = domain::now().local;
                        row.error = secondary_note;
                        write_auto_row(wdb, row);
                        {
                            auto st = wdb.prepare("UPDATE backup_state SET last_status = 'EXITOSA', last_error = '', last_backup_at = ? WHERE id = 1;");
                            if (st.is_success()) { auto s = std::move(st.value()); s.bind_text(1, row.finished_at); s.step(); }
                        }
                        std::string audit = "Respaldo automático al cerrar turno: " + row.local_file + "; segunda carpeta: ";
                        audit += row.secondary_status == "OK" ? "copia correcta"
                               : row.secondary_status == "NONE" ? "no configurada"
                               : (row.secondary_status == "UNAVAILABLE" ? "no disponible" : "falló");
                        write_audit_direct(wdb, user_id, "BACKUP_AUTO", audit);

                        // 4. Limpieza del tipo AUTO (por destino), con las mismas garantías que la manual.
                        contracts::BackupCleanupResultDto cleaned;
                        if (cleanup_core(wdb, {"AUTO"}, false, cleaned).is_success() && !cleaned.to_delete.empty()) {
                            write_audit_direct(wdb, user_id, "BACKUP_CLEANUP",
                                "Limpieza automática de respaldos: borrados " + std::to_string(cleaned.to_delete.size()) +
                                    ", liberados " + std::to_string(cleaned.bytes_freed) + " bytes");
                        }
                    }
                }
            }
        } catch (...) {
            // El hilo nunca propaga excepciones: lo peor es un estado sin actualizar.
        }

        // ¿Llegó otro cierre de turno mientras trabajaba?
        std::lock_guard<std::mutex> lock(w->mutex);
        if (w->rerun && !w->cancel.load()) {
            w->rerun = false;
            shift_id = w->next_shift_id;
            user_id = w->next_user_id;
            continue;
        }
        w->rerun = false;
        w->active = false;
        return;
    }
}

contracts::Result<contracts::AutoBackupStatusDto> SqliteBackupService::get_auto_backup_status() {
    using R = contracts::Result<contracts::AutoBackupStatusDto>;
    if (!account_service_ || !account_service_->is_authenticated()) {
        return R::fail(contracts::ErrorCode::Forbidden, "Se requiere inicio de sesión");
    }
    auto prep = db_.prepare(
        "SELECT is_enabled, auto_state, auto_started_at, auto_finished_at, auto_shift_id, auto_local_ok, "
        "auto_local_file, auto_secondary_status, secondary_dir, auto_error FROM backup_state WHERE id = 1;");
    if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
    contracts::AutoBackupStatusDto dto;
    if (step.value()) {
        dto.is_enabled = stmt.get_int64(0) != 0;
        dto.state = stmt.get_text(1);
        dto.started_at = stmt.is_null(2) ? "" : stmt.get_text(2);
        dto.finished_at = stmt.is_null(3) ? "" : stmt.get_text(3);
        dto.shift_id = stmt.get_text(4);
        dto.local_ok = stmt.get_int64(5) != 0;
        dto.local_file_name = stmt.get_text(6);
        dto.secondary_status = stmt.get_text(7);
        dto.secondary_configured = !stmt.get_text(8).empty();
        dto.error = stmt.get_text(9);
    }
    return R::ok(dto);
}

contracts::Result<std::string> SqliteBackupService::get_secondary_backup_dir() {
    using R = contracts::Result<std::string>;
    if (!has_permission("backup.manage")) {
        return R::fail(contracts::ErrorCode::Forbidden, "Operación denegada: falta de permisos (backup.manage)");
    }
    auto prep = db_.prepare("SELECT secondary_dir FROM backup_state WHERE id = 1;");
    if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
    return R::ok(step.value() ? stmt.get_text(0) : std::string());
}

namespace {
bool set_secondary_dir_column(storage::SqliteDatabase& db, const std::string& value, contracts::OperationResult& error) {
    auto prep = db.prepare("UPDATE backup_state SET secondary_dir = ? WHERE id = 1;");
    if (!prep.is_success()) {
        error = contracts::OperationResult::fail(prep.error_code(), prep.error_message());
        return false;
    }
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, value);
    auto step = stmt.step();
    if (!step.is_success()) {
        error = contracts::OperationResult::fail(step.error_code(), step.error_message());
        return false;
    }
    return true;
}
} // namespace

contracts::OperationResult SqliteBackupService::set_secondary_backup_dir(const std::string& path) {
    using EC = contracts::ErrorCode;
    if (!has_permission("backup.manage")) {
        return contracts::OperationResult::fail(EC::Forbidden, "Operación denegada: falta de permisos (backup.manage)");
    }
    const std::string trimmed = trim_text(path);
    if (trimmed.empty()) {
        return contracts::OperationResult::fail(EC::InvalidInput, "Indique la carpeta donde guardar la segunda copia");
    }
    const fs::path dir = fs::u8path(trimmed);
    if (!dir.is_absolute()) {
        return contracts::OperationResult::fail(EC::InvalidInput, "Indique la ruta completa de la carpeta (por ejemplo E:\\Respaldos)");
    }
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) {
        return contracts::OperationResult::fail(EC::InvalidInput,
            "La carpeta no existe o no está disponible. Conecte la USB y elija una carpeta que exista");
    }
    // Debe ser una carpeta distinta de la de respaldos del propio programa.
    if (!data_dir_.empty()) {
        const fs::path own = (fs::absolute(fs::u8path(data_dir_), ec) / "backups").lexically_normal();
        const fs::path chosen = fs::absolute(dir, ec).lexically_normal();
        auto lower = [](std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; };
        std::string o = lower(own.generic_u8string()), c = lower(chosen.generic_u8string());
        while (!o.empty() && o.back() == '/') o.pop_back();
        while (!c.empty() && c.back() == '/') c.pop_back();
        if (c == o || c.rfind(o + "/", 0) == 0) {
            return contracts::OperationResult::fail(EC::InvalidInput,
                "La segunda carpeta debe ser distinta de la carpeta de respaldos del programa");
        }
    }
    // Se comprueba que se puede escribir (crea y borra un archivo de prueba).
    const fs::path probe = dir / ".escritura-prueba-cafeteria.tmp";
    bool writable = false;
    {
        std::ofstream out(probe, std::ios::binary | std::ios::trunc);
        writable = out.is_open() && static_cast<bool>(out << "x");
    }
    std::error_code rm_ec;
    const bool removed = fs::remove(probe, rm_ec);
    if (!writable || !removed) {
        fs::remove(probe, rm_ec);
        return contracts::OperationResult::fail(EC::InvalidInput,
            "No se puede escribir en esa carpeta. Revise que la USB no esté protegida contra escritura");
    }
    contracts::OperationResult error = contracts::OperationResult::ok();
    if (!set_secondary_dir_column(db_, dir.u8string(), error)) return error;
    account_service_->record_audit_event("BACKUP_SECONDARY_DIR", "Segunda carpeta de respaldos configurada: " + dir.filename().u8string());
    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteBackupService::clear_secondary_backup_dir() {
    if (!has_permission("backup.manage")) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden, "Operación denegada: falta de permisos (backup.manage)");
    }
    contracts::OperationResult error = contracts::OperationResult::ok();
    if (!set_secondary_dir_column(db_, "", error)) return error;
    account_service_->record_audit_event("BACKUP_SECONDARY_DIR", "Segunda carpeta de respaldos quitada");
    return contracts::OperationResult::ok();
}

contracts::Result<std::vector<contracts::BackupListItemDto>> SqliteBackupService::list_backups(
    const contracts::BackupListFilterDto& filter) {
    using R = contracts::Result<std::vector<contracts::BackupListItemDto>>;
    if (!has_permission("backup.manage") && !has_permission("backup.restore")) {
        return R::fail(contracts::ErrorCode::Forbidden,
                       "Operación denegada: falta de permisos (backup.manage o backup.restore)");
    }
    if (filter.limit < 1 || filter.limit > 500 || filter.offset < 0) {
        return R::fail(contracts::ErrorCode::InvalidInput, "El límite debe estar entre 1 y 500 y el desplazamiento no puede ser negativo");
    }
    auto prep = db_.prepare(
        "SELECT id, kind, destination, path, created_at, size_bytes, status FROM backup_files "
        "WHERE status IN ('ACTIVE', 'CHANGED') ORDER BY created_at_utc DESC, rowid DESC LIMIT ? OFFSET ?;");
    if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
    auto stmt = std::move(prep.value());
    stmt.bind_int64(1, filter.limit);
    stmt.bind_int64(2, filter.offset);
    std::vector<contracts::BackupListItemDto> items;
    while (true) {
        auto step = stmt.step();
        if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
        if (!step.value()) break;
        contracts::BackupListItemDto it;
        it.id = stmt.get_text(0);
        it.kind = stmt.get_text(1);
        it.destination = stmt.get_text(2);
        it.path = stmt.get_text(3);
        it.created_at = stmt.get_text(4);
        it.size_bytes = stmt.get_int64(5);
        const std::string status = stmt.get_text(6);
        std::error_code ec;
        const fs::path file = fs::u8path(it.path);
        it.file_exists = fs::is_regular_file(file, ec);
        it.unchanged = it.file_exists && status == "ACTIVE" &&
                       static_cast<std::int64_t>(fs::file_size(file, ec)) == it.size_bytes && !ec;
        items.push_back(std::move(it));
    }
    return R::ok(std::move(items));
}

} // namespace app::services
