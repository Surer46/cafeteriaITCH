#ifndef APP_STORAGE_MIGRATION_RUNNER_HPP
#define APP_STORAGE_MIGRATION_RUNNER_HPP

#include <filesystem>
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
    contracts::OperationResult run_pending_migrations_from_dir(const std::string& migrations_dir);

private:
    SqliteDatabase& db_;
};

} // namespace app::storage

#endif // APP_STORAGE_MIGRATION_RUNNER_HPP
