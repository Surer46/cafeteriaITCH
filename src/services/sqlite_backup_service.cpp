#include "app/services/sqlite_backup_service.hpp"

#include <chrono>
#include <cstdio>
#include <ctime>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
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
    dev_config_.server_url = "https://backup.cafeteria-itch.edu.mx/api/v1";
    dev_config_.retention_days = "30";
    dev_config_.is_tls_enabled = true;
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
    fs::path dest = fs::u8path(destination_path);
    auto res = create_verified_snapshot(dest);
    if (!res.is_success()) {
        write_state("FALLIDA", res.error_message(), false);
        account_service_->record_audit_event("BACKUP_LOCAL_FAILED", "Respaldo local fallido: " + name_of(dest));
        return res;
    }
    auto st = write_state("EXITOSA", "", true);
    if (!st.is_success()) return st;
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
    fs::path src = fs::u8path(path);
    auto res = restore_impl(src, name_of(src));
    if (!res.is_success()) {
        account_service_->record_audit_event("BACKUP_RESTORE_FAILED", "Restauración fallida: " + name_of(src));
    }
    return res;
}

contracts::Result<contracts::BackupServerConfigDto> SqliteBackupService::get_developer_server_config() {
    // Comprobación de seguridad en la capa de servicios:
    // "Ocultar un botón no sustituye una autorización."
    if (!account_service_ || !account_service_->has_permission("backup.dev_config")) {
        return contracts::Result<contracts::BackupServerConfigDto>::fail(
            contracts::ErrorCode::Forbidden,
            "Acceso denegado: la configuración del servidor de respaldos es exclusiva de la identidad Desarrollador"
        );
    }
    return contracts::Result<contracts::BackupServerConfigDto>::ok(dev_config_);
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
    dev_config_ = config;
    return contracts::OperationResult::ok();
}

} // namespace app::services
