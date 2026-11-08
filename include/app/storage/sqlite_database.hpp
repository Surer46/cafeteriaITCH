#ifndef APP_STORAGE_SQLITE_DATABASE_HPP
#define APP_STORAGE_SQLITE_DATABASE_HPP

#include <filesystem>
#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include "app/contracts/result.hpp"

struct sqlite3;
struct sqlite3_stmt;

namespace app::storage {

class SqliteStatement {
public:
    explicit SqliteStatement(sqlite3_stmt* stmt);
    ~SqliteStatement();

    SqliteStatement(const SqliteStatement&) = delete;
    SqliteStatement& operator=(const SqliteStatement&) = delete;

    SqliteStatement(SqliteStatement&& other) noexcept;
    SqliteStatement& operator=(SqliteStatement&& other) noexcept;

    contracts::OperationResult bind_text(int index, const std::string& val);
    contracts::OperationResult bind_int64(int index, std::int64_t val);
    contracts::OperationResult bind_blob(int index, const void* data, std::size_t size);
    contracts::OperationResult bind_blob(int index, const std::vector<std::uint8_t>& bytes);
    contracts::OperationResult bind_null(int index);

    // Gancho de prueba para simulación controlada de fallos en bind
    static void set_simulate_bind_failure(bool enable);
    static bool is_simulating_bind_failure();

    // Returns true if a row is available, false if done
    contracts::Result<bool> step();
    contracts::OperationResult reset();

    std::string get_text(int col) const;
    std::int64_t get_int64(int col) const;
    std::vector<std::uint8_t> get_blob(int col) const;
    bool is_null(int col) const;

private:
    sqlite3_stmt* stmt_{nullptr};
};

struct BackupFileInfo {
    bool integrity_ok{false};
    std::string integrity_message;
    int schema_version{0};
    std::int64_t page_size{0};
    bool has_expected_tables{false};
    std::string missing_table;
    bool has_active_owner{false};
};

class SqliteDatabase {
public:
    SqliteDatabase();
    ~SqliteDatabase();

    SqliteDatabase(const SqliteDatabase&) = delete;
    SqliteDatabase& operator=(const SqliteDatabase&) = delete;

    contracts::OperationResult open(const std::filesystem::path& db_path);
    contracts::OperationResult open(const std::string& db_path);
    void close();
    bool is_open() const { return db_ != nullptr; }

    contracts::OperationResult execute(const std::string& sql);
    contracts::OperationResult execute_script(const std::string& sql_script);

    contracts::OperationResult begin_transaction();
    contracts::OperationResult commit_transaction();
    contracts::OperationResult rollback_transaction();

    contracts::OperationResult backup_to(const std::filesystem::path& dest_path);

    // Inspección de solo lectura de un archivo de respaldo; no modifica nada.
    static contracts::Result<BackupFileInfo> inspect_file(const std::filesystem::path& file_path);

    // Copia el archivo sobre esta conexión viva con sqlite3_backup.
    // Falla sin tocar nada si hay una transacción abierta.
    contracts::OperationResult restore_from(const std::filesystem::path& src_path);

    contracts::Result<std::int64_t> page_size();

    contracts::Result<SqliteStatement> prepare(const std::string& sql);

    sqlite3* handle() const { return db_; }

private:
    sqlite3* db_{nullptr};
};

} // namespace app::storage

#endif // APP_STORAGE_SQLITE_DATABASE_HPP
