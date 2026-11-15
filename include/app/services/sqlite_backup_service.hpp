#ifndef APP_SERVICES_SQLITE_BACKUP_SERVICE_HPP
#define APP_SERVICES_SQLITE_BACKUP_SERVICE_HPP

#include <filesystem>
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <thread>
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
    // No espera a un respaldo automático en curso más de un instante: pide su cancelación y, si no
    // termina enseguida, lo deja terminar solo (el archivo temporal huérfano se limpia al arrancar).
    ~SqliteBackupService() override;

    contracts::Result<contracts::BackupStatusDto> get_status() override;
    contracts::OperationResult trigger_local_backup(const std::string& destination_path) override;
    contracts::OperationResult set_backup_enabled(bool enabled) override;
    contracts::Result<contracts::BackupVerificationDto> verify_backup(const std::string& path) override;
    contracts::Result<contracts::RestoreResultDto> restore_from_backup(const std::string& path) override;
    contracts::Result<contracts::BackupServerConfigDto> get_developer_server_config() override;
    contracts::OperationResult update_developer_server_config(
        const contracts::BackupServerConfigDto& config
    ) override;
    contracts::Result<contracts::BackupRetentionPolicyDto> get_retention_policy() override;
    contracts::OperationResult set_retention_policy(const contracts::BackupRetentionPolicyDto& policy) override;
    contracts::Result<contracts::BackupCleanupResultDto> preview_backup_cleanup() override;
    contracts::Result<contracts::BackupCleanupResultDto> cleanup_old_backups() override;

    // --- B12: respaldo automático al cerrar turno, segunda carpeta y lista ---
    contracts::Result<contracts::AutoBackupStatusDto> get_auto_backup_status() override;
    contracts::Result<std::string> get_secondary_backup_dir() override;
    contracts::OperationResult set_secondary_backup_dir(const std::string& path) override;
    contracts::OperationResult clear_secondary_backup_dir() override;
    contracts::Result<std::vector<contracts::BackupListItemDto>> list_backups(
        const contracts::BackupListFilterDto& filter) override;

    // Lo llama el cierre de turno (SqliteShiftService::set_after_close_hook) DESPUÉS de confirmarlo.
    // Es una acción del sistema: no exige backup.manage. Si el respaldo automático está activado,
    // arranca un hilo con su propia conexión y vuelve de inmediato. Nunca lanza ni devuelve error.
    void on_shift_closed(const std::string& shift_id, const std::string& user_id);

    // Espera a que no haya un respaldo automático activo (para pruebas y para el cierre ordenado).
    // true si terminó dentro del plazo.
    bool wait_for_auto_backup(int timeout_ms);
    // true mientras hay un respaldo, una restauración o una limpieza en curso.
    bool operation_in_progress() const { return operation_in_progress_->load(); }

private:
    struct AutoWorker;  // estado compartido con el hilo del respaldo automático
    // Cuerpo del hilo del respaldo automático: usa SOLO su propia conexión, nunca la de la interfaz.
    static void run_auto_worker(std::shared_ptr<AutoWorker> worker, std::string shift_id, std::string user_id);

    // Registra un respaldo recién creado y verificado (ruta exacta, tamaño y SHA-256).
    contracts::OperationResult register_backup_file(const char* kind, const std::filesystem::path& path);
    contracts::Result<contracts::BackupCleanupResultDto> run_cleanup(bool dry_run);
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
    // Compartida con el hilo automático: una sola operación de respaldo a la vez.
    std::shared_ptr<std::atomic<bool>> operation_in_progress_{std::make_shared<std::atomic<bool>>(false)};
    std::shared_ptr<AutoWorker> auto_worker_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_BACKUP_SERVICE_HPP
