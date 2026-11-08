#include "app/services/sqlite_backup_service.hpp"

namespace app::services {

SqliteBackupService::SqliteBackupService(
    storage::SqliteDatabase& db,
    std::shared_ptr<contracts::IAccountService> account_service
) : db_(db), account_service_(std::move(account_service)) {
    status_.is_enabled = false;
    status_.last_backup_time = "Nunca";
    status_.last_status = "PENDIENTE";
    status_.last_error = "";

    dev_config_.server_url = "https://backup.cafeteria-itch.edu.mx/api/v1";
    dev_config_.retention_days = "30";
    dev_config_.is_tls_enabled = true;
}

contracts::Result<contracts::BackupStatusDto> SqliteBackupService::get_status() {
    return contracts::Result<contracts::BackupStatusDto>::ok(status_);
}

contracts::OperationResult SqliteBackupService::trigger_local_backup(const std::string& destination_path) {
    if (destination_path.empty()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "Ruta de destino inválida");
    }
    auto dest = std::filesystem::u8path(destination_path);
    auto res = db_.backup_to(dest);
    if (!res.is_success()) {
        status_.last_status = "FALLIDA";
        status_.last_error = res.error_message();
        return res;
    }
    status_.last_status = "EXITOSA";
    status_.last_error = "";
    status_.last_backup_time = "Reciente";
    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteBackupService::set_backup_enabled(bool enabled) {
    status_.is_enabled = enabled;
    return contracts::OperationResult::ok();
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
