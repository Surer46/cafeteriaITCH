#ifndef APP_CONTRACTS_BACKUP_SERVICE_HPP
#define APP_CONTRACTS_BACKUP_SERVICE_HPP

#include <string>
#include "app/contracts/result.hpp"
#include "app/contracts/dto.hpp"

namespace app::contracts {

struct BackupServerConfigDto {
    std::string server_url;
    std::string retention_days;
    bool is_tls_enabled{true};
};

class IBackupService {
public:
    virtual ~IBackupService() = default;

    virtual Result<BackupStatusDto> get_status() = 0;
    virtual OperationResult trigger_local_backup(const std::string& destination_path) = 0;
    virtual OperationResult set_backup_enabled(bool enabled) = 0;

    // Technical developer-only backup server methods
    virtual Result<BackupServerConfigDto> get_developer_server_config() = 0;
    virtual OperationResult update_developer_server_config(const BackupServerConfigDto& config) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_BACKUP_SERVICE_HPP
