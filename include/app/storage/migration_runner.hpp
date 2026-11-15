#ifndef APP_STORAGE_MIGRATION_RUNNER_HPP
#define APP_STORAGE_MIGRATION_RUNNER_HPP

#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include "app/storage/sqlite_database.hpp"
#include "app/contracts/result.hpp"

namespace app::storage {

struct MigrationRecord {
    int version;
    std::string description;
    std::string applied_at;
};

// Aviso antes de aplicar cada migración pendiente: (índice 1..total, total pendientes, versión).
using MigrationProgress = std::function<void(int index, int total, int version)>;

class MigrationRunner {
public:
    explicit MigrationRunner(SqliteDatabase& db);

    contracts::OperationResult init_migration_table();
    contracts::Result<std::vector<int>> get_applied_versions();

    contracts::OperationResult apply_migration(
        int version,
        const std::string& description,
        const std::string& sql_content
    );

    // Applies all pending files from the given directory (e.g. "migrations/")
    contracts::OperationResult run_pending_migrations_from_dir(const std::filesystem::path& migrations_dir);
    // Igual, avisando antes de cada migración pendiente. Con `on_start` vacío equivale a la anterior.
    contracts::OperationResult run_pending_migrations_from_dir(
        const std::filesystem::path& migrations_dir, const MigrationProgress& on_start);
    contracts::OperationResult run_pending_migrations_from_dir(const std::string& migrations_dir);

private:
    SqliteDatabase& db_;
};

} // namespace app::storage

#endif // APP_STORAGE_MIGRATION_RUNNER_HPP
