#ifndef APP_SERVICES_SQLITE_BACKUP_SERVICE_HPP
#define APP_SERVICES_SQLITE_BACKUP_SERVICE_HPP

#include <memory>
#include "app/contracts/backup_service.hpp"
#include "app/contracts/account_service.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

class SqliteBackupService : public contracts::IBackupService {
public:
    SqliteBackupService(
        storage::SqliteDatabase& db,
        std::shared_ptr<contracts::IAccountService> account_service
    );

    contracts::Result<contracts::BackupStatusDto> get_status() override;
    contracts::OperationResult trigger_local_backup(const std::string& destination_path) override;
    contracts::OperationResult set_backup_enabled(bool enabled) override;

    contracts::Result<contracts::BackupServerConfigDto> get_developer_server_config() override;
    contracts::OperationResult update_developer_server_config(
        const contracts::BackupServerConfigDto& config
    ) override;

private:
    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
    contracts::BackupStatusDto status_;
    contracts::BackupServerConfigDto dev_config_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_BACKUP_SERVICE_HPP
