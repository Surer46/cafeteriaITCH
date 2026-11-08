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

struct BackupVerificationDto {
    bool integrity_ok{false};
    int schema_version{0};
    std::string file_modified_at;  // ISO 8601, hora local
    std::string detail;            // motivo legible si no es restaurable o está dañado
};

struct RestoreResultDto {
    bool requires_relogin{true};
    std::string pre_restore_backup_path;
};

class IBackupService {
public:
    virtual ~IBackupService() = default;

    virtual Result<BackupStatusDto> get_status() = 0;
    virtual OperationResult trigger_local_backup(const std::string& destination_path) = 0;
    virtual OperationResult set_backup_enabled(bool enabled) = 0;

    // Solo lectura: no modifica el archivo ni la base. Requiere backup.manage o backup.restore.
    virtual Result<BackupVerificationDto> verify_backup(const std::string& path) = 0;
    // Requiere backup.restore. Cierra la sesión actual (requires_relogin = true).
    virtual Result<RestoreResultDto> restore_from_backup(const std::string& path) = 0;

    // Technical developer-only backup server methods
    virtual Result<BackupServerConfigDto> get_developer_server_config() = 0;
    virtual OperationResult update_developer_server_config(const BackupServerConfigDto& config) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_BACKUP_SERVICE_HPP
