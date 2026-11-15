#ifndef APP_SERVICES_DATABASE_STARTUP_HPP
#define APP_SERVICES_DATABASE_STARTUP_HPP

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

#include "app/storage/sqlite_database.hpp"

// Arranque seguro de la base local (sin wxWidgets):
//   1. base nueva (no existe, está vacía o no tiene migraciones aplicadas): se migra sin respaldo;
//   2. base con datos: comprobación rápida de integridad; si está dañada no se abre para vender;
//   3. base con datos y migraciones pendientes: comprobación completa, espacio libre, respaldo
//      consistente y verificado ANTES de aplicar la primera migración; si algo falla, no se migra;
//   4. migraciones; si una falla, la base queda en la versión alcanzada y el mensaje dice dónde está
//      el respaldo previo (el estado anterior completo);
//   5. el respaldo previo se registra (tipo PRE_MIGRATION) DESPUÉS de migrar.
// Todos los mensajes van en lenguaje llano, con la ruta y lo que debe hacer la persona.
namespace app::services {

enum class StartupError {
    None,
    MigrationsFolderMissing,
    OpenFailed,
    DatabaseDamaged,
    NotEnoughSpace,
    BackupFailed,
    MigrationFailed,
    DatabaseNewerThanProgram   // la base trae una versión mayor que la última migración del paquete
};

// Fases de una actualización larga (base con datos y migraciones pendientes).
enum class StartupPhase {
    UpdateStarting,  // se sabe que habrá actualización: es el momento de mostrar la ventana de progreso
    QuickCheck,
    FullCheck,
    BackupCopy,
    BackupVerify,
    Migrate,
    BackupRegister,
    Finished
};

struct StartupProgress {
    StartupPhase phase{StartupPhase::QuickCheck};
    int percent{-1};          // 0..100 si se conoce; -1 = sin porcentaje real (la interfaz muestra "en movimiento")
    int step{0};              // Migrate: número de la migración que se aplica (1..total)
    int total_steps{0};       // Migrate: total de migraciones pendientes
    std::string message_utf8; // texto de la fase en lenguaje llano
};

struct StartupOptions {
    std::filesystem::path db_path;
    std::filesystem::path migrations_dir;
    // Carpeta donde se crea backups\pre-migracion\ (normalmente <datos>\backups).
    std::filesystem::path backups_dir;
    // Bytes libres en la unidad de la ruta dada. Si está vacío se usa el sistema de archivos real.
    std::function<std::uint64_t(const std::filesystem::path&)> free_space_bytes;
    // Instante actual (para el nombre del respaldo y su registro). Si está vacío, la hora del sistema.
    std::function<std::chrono::system_clock::time_point()> now;
    // Aviso de avance opcional (mismo hilo; sin wxWidgets). Los cambios de fase siempre se avisan; el
    // avance dentro de una fase se limita a uno cada `progress_min_interval_ms`. Sin aviso, todo funciona igual.
    std::function<void(const StartupProgress&)> on_progress;
    int progress_min_interval_ms{200};
};

struct StartupResult {
    bool ok{false};
    StartupError error{StartupError::None};
    std::string message_utf8;                 // texto para el usuario si !ok
    bool backup_created{false};
    std::filesystem::path backup_path;        // respaldo previo a migrar (si se creó)
    bool backup_registered{false};
    int schema_version_before{0};             // 0 si la base era nueva
    int schema_version_after{0};
    int migrations_applied{0};
    // Tiempos medidos, en milisegundos (para diagnóstico y pruebas de rendimiento).
    std::int64_t quick_check_ms{0};
    std::int64_t full_check_ms{0};
    std::int64_t backup_ms{0};
    std::int64_t migrate_ms{0};
    std::int64_t total_ms{0};
};

// Abre `db` sobre options.db_path y la deja migrada. Si devuelve !ok la base queda cerrada.
StartupResult open_and_migrate_database(storage::SqliteDatabase& db, const StartupOptions& options);

} // namespace app::services

#endif // APP_SERVICES_DATABASE_STARTUP_HPP
