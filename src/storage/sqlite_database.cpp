#include "app/storage/sqlite_database.hpp"
#include "sqlite3.h"
#include <iostream>
#include <atomic>

namespace app::storage {

// --- SqliteStatement ---

SqliteStatement::SqliteStatement(sqlite3_stmt* stmt) : stmt_(stmt) {}

SqliteStatement::~SqliteStatement() {
    if (stmt_) {
        sqlite3_finalize(stmt_);
        stmt_ = nullptr;
    }
}

SqliteStatement::SqliteStatement(SqliteStatement&& other) noexcept
    : stmt_(other.stmt_) {
    other.stmt_ = nullptr;
}

SqliteStatement& SqliteStatement::operator=(SqliteStatement&& other) noexcept {
    if (this != &other) {
        if (stmt_) {
            sqlite3_finalize(stmt_);
        }
        stmt_ = other.stmt_;
        other.stmt_ = nullptr;
    }
    return *this;
}

static std::atomic<bool> s_simulate_bind_failure{false};

void SqliteStatement::set_simulate_bind_failure(bool enable) {
    s_simulate_bind_failure.store(enable);
}

bool SqliteStatement::is_simulating_bind_failure() {
    return s_simulate_bind_failure.load();
}

contracts::OperationResult SqliteStatement::bind_text(int index, const std::string& val) {
    if (s_simulate_bind_failure.load()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::StorageFailure, "Fallo simulado de bind_text para pruebas de propagación");
    }
    if (!stmt_) return contracts::OperationResult::fail(contracts::ErrorCode::InvalidState, "Sentencia nula");
    int rc = sqlite3_bind_text(stmt_, index, val.c_str(), static_cast<int>(val.length()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        return contracts::OperationResult::fail(contracts::ErrorCode::StorageFailure, "Error al enlazar texto en SQL");
    }
    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteStatement::bind_int64(int index, std::int64_t val) {
    if (s_simulate_bind_failure.load()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::StorageFailure, "Fallo simulado de bind_int64 para pruebas de propagación");
    }
    if (!stmt_) return contracts::OperationResult::fail(contracts::ErrorCode::InvalidState, "Sentencia nula");
    int rc = sqlite3_bind_int64(stmt_, index, val);
    if (rc != SQLITE_OK) {
        return contracts::OperationResult::fail(contracts::ErrorCode::StorageFailure, "Error al enlazar entero en SQL");
    }
    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteStatement::bind_blob(int index, const void* data, std::size_t size) {
    if (s_simulate_bind_failure.load()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::StorageFailure, "Fallo simulado de bind_blob para pruebas de propagación");
    }
    if (!stmt_) return contracts::OperationResult::fail(contracts::ErrorCode::InvalidState, "Sentencia nula");
    int rc = sqlite3_bind_blob(stmt_, index, data ? data : "", static_cast<int>(size), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) {
        return contracts::OperationResult::fail(contracts::ErrorCode::StorageFailure, "Error al enlazar BLOB en SQL");
    }
    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteStatement::bind_blob(int index, const std::vector<std::uint8_t>& bytes) {
    return bind_blob(index, bytes.data(), bytes.size());
}

contracts::OperationResult SqliteStatement::bind_null(int index) {
    if (s_simulate_bind_failure.load()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::StorageFailure, "Fallo simulado de bind_null para pruebas de propagación");
    }
    if (!stmt_) return contracts::OperationResult::fail(contracts::ErrorCode::InvalidState, "Sentencia nula");
    int rc = sqlite3_bind_null(stmt_, index);
    if (rc != SQLITE_OK) {
        return contracts::OperationResult::fail(contracts::ErrorCode::StorageFailure, "Error al enlazar null en SQL");
    }
    return contracts::OperationResult::ok();
}

contracts::Result<bool> SqliteStatement::step() {
    if (!stmt_) return contracts::Result<bool>::fail(contracts::ErrorCode::InvalidState, "Sentencia nula");
    int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) {
        return contracts::Result<bool>::ok(true);
    }
    if (rc == SQLITE_DONE) {
        return contracts::Result<bool>::ok(false);
    }
    return contracts::Result<bool>::fail(contracts::ErrorCode::StorageFailure, "Error en ejecución de consulta SQL");
}

contracts::OperationResult SqliteStatement::reset() {
    if (!stmt_) return contracts::OperationResult::fail(contracts::ErrorCode::InvalidState, "Sentencia nula");
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
    return contracts::OperationResult::ok();
}

std::string SqliteStatement::get_text(int col) const {
    if (!stmt_) return "";
    const unsigned char* text = sqlite3_column_text(stmt_, col);
    return text ? reinterpret_cast<const char*>(text) : "";
}

std::int64_t SqliteStatement::get_int64(int col) const {
    if (!stmt_) return 0;
    return sqlite3_column_int64(stmt_, col);
}

std::vector<std::uint8_t> SqliteStatement::get_blob(int col) const {
    if (!stmt_) return {};
    const void* data = sqlite3_column_blob(stmt_, col);
    int bytes = sqlite3_column_bytes(stmt_, col);
    if (!data || bytes <= 0) return {};
    const auto* byte_ptr = reinterpret_cast<const std::uint8_t*>(data);
    return std::vector<std::uint8_t>(byte_ptr, byte_ptr + bytes);
}

bool SqliteStatement::is_null(int col) const {
    if (!stmt_) return true;
    return sqlite3_column_type(stmt_, col) == SQLITE_NULL;
}

// --- SqliteDatabase ---

SqliteDatabase::SqliteDatabase() : db_(nullptr) {}

SqliteDatabase::~SqliteDatabase() {
    close();
}

contracts::OperationResult SqliteDatabase::open(const std::filesystem::path& db_path) {
    close();
#ifdef _WIN32
    int rc = sqlite3_open16(db_path.wstring().c_str(), &db_);
#else
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    int rc = sqlite3_open_v2(db_path.c_str(), &db_, flags, nullptr);
#endif
    if (rc != SQLITE_OK) {
        std::string err = db_ ? sqlite3_errmsg(db_) : "No se pudo abrir el archivo";
        close();
        return contracts::OperationResult::fail(
            contracts::ErrorCode::StorageFailure,
            "Error al abrir la base de datos local: " + err
        );
    }

    // Configurar pragmas fundamentales
    execute("PRAGMA foreign_keys = ON;");
    execute("PRAGMA busy_timeout = 5000;");
    execute("PRAGMA journal_mode = WAL;");
    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteDatabase::open(const std::string& db_path) {
    return open(std::filesystem::u8path(db_path));
}

void SqliteDatabase::close() {
    if (db_) {
        sqlite3_close_v2(db_);
        db_ = nullptr;
    }
}

contracts::OperationResult SqliteDatabase::execute(const std::string& sql) {
    if (!db_) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidState, "Base de datos no inicializada");
    }
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        std::string err = err_msg ? err_msg : "Error desconocido en SQLite";
        sqlite3_free(err_msg);
        return contracts::OperationResult::fail(contracts::ErrorCode::StorageFailure, err);
    }
    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteDatabase::execute_script(const std::string& sql_script) {
    return execute(sql_script);
}

contracts::OperationResult SqliteDatabase::begin_transaction() {
    return execute("BEGIN IMMEDIATE;");
}

contracts::OperationResult SqliteDatabase::commit_transaction() {
    return execute("COMMIT;");
}

contracts::OperationResult SqliteDatabase::rollback_transaction() {
    return execute("ROLLBACK;");
}

contracts::OperationResult SqliteDatabase::backup_to(const std::filesystem::path& dest_path) {
    if (!db_) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidState, "Base de datos no inicializada");
    }
    sqlite3* dest_db = nullptr;
#ifdef _WIN32
    int rc = sqlite3_open16(dest_path.wstring().c_str(), &dest_db);
#else
    int rc = sqlite3_open_v2(dest_path.c_str(), &dest_db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
#endif
    if (rc != SQLITE_OK) {
        std::string err = dest_db ? sqlite3_errmsg(dest_db) : "No se pudo crear archivo de respaldo";
        if (dest_db) sqlite3_close_v2(dest_db);
        return contracts::OperationResult::fail(
            contracts::ErrorCode::StorageFailure,
            "Error al abrir archivo destino para respaldo: " + err
        );
    }

    sqlite3_backup* backup = sqlite3_backup_init(dest_db, "main", db_, "main");
    if (!backup) {
        std::string err = sqlite3_errmsg(dest_db);
        sqlite3_close_v2(dest_db);
        return contracts::OperationResult::fail(
            contracts::ErrorCode::StorageFailure,
            "Error al inicializar respaldo SQLite: " + err
        );
    }

    rc = sqlite3_backup_step(backup, -1);
    sqlite3_backup_finish(backup);
    if (rc != SQLITE_DONE) {
        std::string err = sqlite3_errmsg(dest_db);
        sqlite3_close_v2(dest_db);
        return contracts::OperationResult::fail(
            contracts::ErrorCode::StorageFailure,
            "Error al completar copia de instantánea SQLite: " + err
        );
    }

    sqlite3_close_v2(dest_db);
    return contracts::OperationResult::ok();
}

contracts::Result<SqliteStatement> SqliteDatabase::prepare(const std::string& sql) {
    if (!db_) {
        return contracts::Result<SqliteStatement>::fail(contracts::ErrorCode::InvalidState, "Base de datos no inicializada");
    }
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.length()), &stmt, nullptr);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        return contracts::Result<SqliteStatement>::fail(
            contracts::ErrorCode::StorageFailure,
            "Error al preparar consulta SQL: " + err
        );
    }
    return contracts::Result<SqliteStatement>::ok(SqliteStatement(stmt));
}

} // namespace app::storage
