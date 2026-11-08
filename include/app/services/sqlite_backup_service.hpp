#ifndef APP_SERVICES_SQLITE_BACKUP_SERVICE_HPP
#define APP_SERVICES_SQLITE_BACKUP_SERVICE_HPP

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include "app/contracts/backup_service.hpp"
#include "app/contracts/account_service.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

class SqliteBackupService : public contracts::IBackupService {
public:
    // data_dir: directorio de datos (los respaldos previos van en data_dir/backups).
    // migrations_dir: necesario solo para restaurar archivos de esquema anterior.
    // on_restored: invalida estado en memoria de otros servicios tras restaurar.
    SqliteBackupService(
        storage::SqliteDatabase& db,
        std::shared_ptr<contracts::IAccountService> account_service,
        std::string data_dir = "",
        std::string migrations_dir = "",
        std::function<void()> on_restored = {}
    );

    contracts::Result<contracts::BackupStatusDto> get_status() override;
    contracts::OperationResult trigger_local_backup(const std::string& destination_path) override;
    contracts::OperationResult set_backup_enabled(bool enabled) override;
    contracts::Result<contracts::BackupVerificationDto> verify_backup(const std::string& path) override;
    contracts::Result<contracts::RestoreResultDto> restore_from_backup(const std::string& path) override;

    contracts::Result<contracts::BackupServerConfigDto> get_developer_server_config() override;
    contracts::OperationResult update_developer_server_config(
        const contracts::BackupServerConfigDto& config
    ) override;

private:
    bool has_permission(const char* key) const;
    contracts::OperationResult write_state(const char* status, const std::string& error, bool set_time);
    // Crea una instantánea en `dest` y la verifica; si falla borra el archivo.
    contracts::OperationResult create_verified_snapshot(const std::filesystem::path& dest);
    contracts::Result<int> current_schema_version();
    contracts::Result<contracts::RestoreResultDto> restore_impl(
        const std::filesystem::path& src, const std::string& file_name);
    bool recover_from(const std::filesystem::path& pre_backup);

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
    std::string data_dir_;
    std::string migrations_dir_;
    std::function<void()> on_restored_;
    contracts::BackupServerConfigDto dev_config_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_BACKUP_SERVICE_HPP
