#include "app/storage/migration_runner.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <iostream>

namespace app::storage {

MigrationRunner::MigrationRunner(SqliteDatabase& db) : db_(db) {}

contracts::OperationResult MigrationRunner::init_migration_table() {
    const std::string sql =
        "CREATE TABLE IF NOT EXISTS schema_migrations ("
        "  version INTEGER PRIMARY KEY,"
        "  description TEXT NOT NULL,"
        "  applied_at TEXT NOT NULL DEFAULT (datetime('now'))"
        ");";
    return db_.execute(sql);
}

contracts::Result<std::vector<int>> MigrationRunner::get_applied_versions() {
    auto init_res = init_migration_table();
    if (!init_res.is_success()) {
        return contracts::Result<std::vector<int>>::fail(
            init_res.error_code(),
            init_res.error_message()
        );
    }

    auto prep = db_.prepare("SELECT version FROM schema_migrations ORDER BY version ASC;");
    if (!prep.is_success()) {
        return contracts::Result<std::vector<int>>::fail(
            prep.error_code(),
            prep.error_message()
        );
    }

    auto stmt = std::move(prep.value());
    std::vector<int> versions;
    while (true) {
        auto step_res = stmt.step();
        if (!step_res.is_success()) {
            return contracts::Result<std::vector<int>>::fail(
                step_res.error_code(),
                step_res.error_message()
            );
        }
        if (!step_res.value()) {
            break;
        }
        versions.push_back(static_cast<int>(stmt.get_int64(0)));
    }

    return contracts::Result<std::vector<int>>::ok(versions);
}

contracts::OperationResult MigrationRunner::apply_migration(
    int version,
    const std::string& description,
    const std::string& sql_content
) {
    auto applied_res = get_applied_versions();
    if (!applied_res.is_success()) {
        return contracts::OperationResult::fail(
            applied_res.error_code(),
            applied_res.error_message()
        );
    }

    const auto& applied = applied_res.value();
    if (std::find(applied.begin(), applied.end(), version) != applied.end()) {
        // Ya está aplicada
        return contracts::OperationResult::ok();
    }

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return tx;

    auto exec = db_.execute_script(sql_content);
    if (!exec.is_success()) {
        db_.rollback_transaction();
        return exec;
    }

    auto prep = db_.prepare("INSERT INTO schema_migrations (version, description) VALUES (?, ?);");
    if (!prep.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_int64(1, version);
    stmt.bind_text(2, description);
    auto step_res = stmt.step();
    if (!step_res.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(step_res.error_code(), step_res.error_message());
    }

    return db_.commit_transaction();
}

contracts::OperationResult MigrationRunner::run_pending_migrations_from_dir(const std::filesystem::path& migrations_dir) {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists(migrations_dir, ec)) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::NotFound,
            "Directorio de migraciones no encontrado: " + migrations_dir.string()
        );
    }

    std::vector<fs::path> files;
    for (const auto& entry : fs::directory_iterator(migrations_dir, ec)) {
        if (entry.is_regular_file() && entry.path().extension() == ".sql") {
            files.push_back(entry.path());
        }
    }
    std::sort(files.begin(), files.end());

    for (const auto& filepath : files) {
        std::string filename = filepath.filename().string();
        // Parse version e.g. "001_initial_schema.sql"
        int version = 0;
        try {
            version = std::stoi(filename.substr(0, 3));
        } catch (...) {
            continue;
        }

        std::ifstream f(filepath);
        if (!f.is_open()) {
            return contracts::OperationResult::fail(
                contracts::ErrorCode::StorageFailure,
                "No se pudo leer la migración: " + filename
            );
        }
        std::stringstream ss;
        ss << f.rdbuf();

        auto res = apply_migration(version, filename, ss.str());
        if (!res.is_success()) {
            return res;
        }
    }

    return contracts::OperationResult::ok();
}

contracts::OperationResult MigrationRunner::run_pending_migrations_from_dir(const std::string& migrations_dir) {
    return run_pending_migrations_from_dir(std::filesystem::u8path(migrations_dir));
}

} // namespace app::storage
