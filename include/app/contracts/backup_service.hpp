#ifndef APP_CONTRACTS_BACKUP_SERVICE_HPP
#define APP_CONTRACTS_BACKUP_SERVICE_HPP

#include <cstdint>
#include <string>
#include <vector>
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

// Política de limpieza de respaldos locales: se conservan los últimos `keep_last` de cada tipo
// y todo lo creado en los últimos `keep_days` días. Valores por omisión: 30 y 90.
// Rangos válidos: keep_last 1..1000, keep_days 0..3650 (0 = sin protección por antigüedad).
struct BackupRetentionPolicyDto {
    int keep_last{30};
    int keep_days{90};
};

struct BackupCleanupItemDto {
    std::string path;        // ruta exacta registrada
    std::string kind;        // "LOCAL", "PRE_RESTORE" o "AUTO"
    std::string created_at;  // hora local de creación
    std::int64_t size_bytes{0};
    std::string destination{"DATA"};  // "DATA" o "SECONDARY" (segunda carpeta o USB)
};

// Resultado de la vista previa o de la limpieza. En la vista previa, `to_delete` es lo que SE BORRARÍA;
// en la limpieza, lo que se borró.
struct BackupCleanupResultDto {
    bool dry_run{false};
    std::vector<BackupCleanupItemDto> to_delete;
    std::vector<BackupCleanupItemDto> changed;  // el archivo en esa ruta ya no es el registrado: no se borra
    std::vector<BackupCleanupItemDto> failed;   // el borrado falló: el archivo sigue ahí
    int kept_count{0};
    int missing_count{0};                       // registrados cuyo archivo ya no existe
    std::int64_t bytes_freed{0};                // vista previa: bytes que se liberarían
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

    // Limpieza de respaldos locales creados por la aplicación. Todas requieren backup.manage.
    virtual Result<BackupRetentionPolicyDto> get_retention_policy() = 0;
    virtual OperationResult set_retention_policy(const BackupRetentionPolicyDto& policy) = 0;
    // Lista lo que se borraría; no borra ni modifica nada.
    virtual Result<BackupCleanupResultDto> preview_backup_cleanup() = 0;
    // Borra los respaldos antiguos según la política. Nunca borra el último respaldo verificado de
    // cada tipo, ni archivos no registrados, ni archivos que cambiaron desde que se registraron.
    virtual Result<BackupCleanupResultDto> cleanup_old_backups() = 0;

    // --- B12: respaldo automático al cerrar turno, segunda carpeta y lista ---
    // Los métodos siguientes tienen cuerpo por omisión (error) para no romper a quien ya implementa la interfaz.

    // Resultado del último respaldo automático. Requiere sesión.
    virtual Result<AutoBackupStatusDto> get_auto_backup_status() {
        return Result<AutoBackupStatusDto>::fail(ErrorCode::InvalidState, "No disponible en este servicio");
    }
    // Segunda carpeta (normalmente una USB). Las tres requieren backup.manage. La ruta se guarda en la base.
    // set valida que existe y que se puede escribir (crea y borra un archivo de prueba).
    virtual Result<std::string> get_secondary_backup_dir() {
        return Result<std::string>::fail(ErrorCode::InvalidState, "No disponible en este servicio");
    }
    virtual OperationResult set_secondary_backup_dir(const std::string&) {
        return OperationResult::fail(ErrorCode::InvalidState, "No disponible en este servicio");
    }
    virtual OperationResult clear_secondary_backup_dir() {
        return OperationResult::fail(ErrorCode::InvalidState, "No disponible en este servicio");
    }
    // Respaldos registrados, más recientes primero, con límite y desplazamiento.
    // Requiere backup.manage o backup.restore.
    virtual Result<std::vector<BackupListItemDto>> list_backups(const BackupListFilterDto&) {
        return Result<std::vector<BackupListItemDto>>::fail(ErrorCode::InvalidState, "No disponible en este servicio");
    }
};

} // namespace app::contracts

#endif // APP_CONTRACTS_BACKUP_SERVICE_HPP
