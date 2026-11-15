// Pruebas B8 / punto 1: arranque seguro de la base local (sin GUI).
// Todo ocurre en una carpeta temporal propia (con acentos y espacios) que se borra al terminar.
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <set>
#include <string>
#include <stdexcept>
#include <vector>

#include "app/crypto/password_hasher.hpp"
#include "app/services/database_startup.hpp"
#include "app/services/package_self_check.hpp"
#include "app/services/sqlite_account_service.hpp"
#include "app/services/sqlite_backup_service.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/storage/sqlite_database.hpp"

namespace fs = std::filesystem;
using namespace app::services;
using namespace app::storage;
using namespace app::contracts;

namespace {

fs::path g_root;
int g_checks = 0;

void cleanup_and_exit(int code) {
    std::error_code ec;
    if (!g_root.empty()) fs::remove_all(g_root, ec);
    std::exit(code);
}

} // namespace

#define TEST_CHECK(condition, message) \
    do { \
        ++g_checks; \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA DE ARRANQUE] " << (message) \
                      << "\n  Condición: " << #condition \
                      << "\n  Archivo:   " << __FILE__ \
                      << "\n  Línea:     " << __LINE__ << "\n" << std::endl; \
            cleanup_and_exit(1); \
        } \
    } while (0)

namespace {

const std::string kOwnerPwd = "Clave-Dueno-B8-1";

std::string scalar_text(SqliteDatabase& db, const std::string& sql) {
    auto prep = db.prepare(sql);
    TEST_CHECK(prep.is_success(), "Debe preparar: " + sql);
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    TEST_CHECK(step.is_success() && step.value(), "Debe devolver fila: " + sql);
    return stmt.get_text(0);
}

std::int64_t scalar(SqliteDatabase& db, const std::string& sql) {
    return std::stoll(scalar_text(db, sql));
}

// Todas las filas de una consulta, columnas unidas con '|' y filas con '\n' (para comparar antes/después).
std::string dump_rows(SqliteDatabase& db, const std::string& sql, int columns) {
    auto prep = db.prepare(sql);
    TEST_CHECK(prep.is_success(), "Debe preparar: " + sql);
    auto stmt = std::move(prep.value());
    std::string out;
    while (true) {
        auto step = stmt.step();
        TEST_CHECK(step.is_success(), "Debe recorrer: " + sql);
        if (!step.value()) break;
        for (int c = 0; c < columns; ++c) {
            out += stmt.get_text(c);
            out += '|';
        }
        out += '\n';
    }
    return out;
}

bool table_exists(SqliteDatabase& db, const std::string& name) {
    return scalar(db, "SELECT COUNT(*) FROM sqlite_master WHERE type='table' AND name='" + name + "';") == 1;
}

std::string file_sha(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return app::crypto::PasswordHasher::to_hex(app::crypto::PasswordHasher::sha256(bytes.data(), bytes.size()));
}

std::size_t count_files(const fs::path& dir) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return 0;
    std::size_t n = 0;
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file()) ++n;
    }
    return n;
}

void write_text(const fs::path& p, const std::string& text) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << text;
}

// Copia las migraciones reales con versión <= max_version.
fs::path copy_migrations(const fs::path& to, int max_version) {
    fs::create_directories(to);
    int copied = 0;
    for (const auto& e : fs::directory_iterator(fs::u8path(SOURCE_MIGRATIONS_DIR))) {
        if (e.path().extension() != ".sql") continue;
        int v = std::stoi(e.path().filename().string().substr(0, 3));
        if (v <= max_version) {
            fs::copy_file(e.path(), to / e.path().filename(), fs::copy_options::overwrite_existing);
            ++copied;
        }
    }
    TEST_CHECK(copied == max_version, "Se esperaban migraciones 001.." + std::to_string(max_version));
    return to;
}

int max_real_version() {
    int mx = 0;
    for (const auto& e : fs::directory_iterator(fs::u8path(SOURCE_MIGRATIONS_DIR))) {
        if (e.path().extension() == ".sql") mx = std::max(mx, std::stoi(e.path().filename().string().substr(0, 3)));
    }
    return mx;
}

bool plain_language(const std::string& msg) {
    for (const char* bad : {"PRAGMA", "pragma", "quick_check", "integrity_check", "schema_migrations", "backup_files",
                            "SQLITE", "sqlite3", "SQL ", "rollback", "ROLLBACK", "constraint"}) {
        if (msg.find(bad) != std::string::npos) return false;
    }
    return true;
}

StartupOptions options_for(const fs::path& dir, const fs::path& migrations) {
    StartupOptions o;
    o.db_path = dir / "cafeteria.db";
    o.migrations_dir = migrations;
    o.backups_dir = dir / "backups";
    return o;
}

// Base en la versión 13 con un Dueño (datos reales), cerrada y lista para actualizar.
void make_db_with_data(const fs::path& dir, const fs::path& migrations_upto_13) {
    fs::create_directories(dir);
    SqliteDatabase db;
    auto r = open_and_migrate_database(db, options_for(dir, migrations_upto_13));
    TEST_CHECK(r.ok, "Debe crear la base inicial: " + r.message_utf8);
    SqliteAccountService account(db);
    auto own = account.create_initial_owner("dueno_b8", "Dueño B8", kOwnerPwd, "dueno@cafeteria-itch.edu.mx");
    TEST_CHECK(own.is_success(), "Debe crear Dueño: " + own.error_message());
    db.close();
}

} // namespace

// --- Base nueva y segundo arranque ---
static void test_new_database_and_second_start() {
    std::cout << "[TEST] B8: base nueva sin respaldo; segundo arranque sin respaldo nuevo..." << std::endl;
    const fs::path dir = g_root / fs::u8path(u8"nueva ñ") / "Datos Cafetería";
    fs::create_directories(dir);
    const fs::path real = fs::u8path(SOURCE_MIGRATIONS_DIR);
    const int N = max_real_version();

    {
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, options_for(dir, real));
        TEST_CHECK(r.ok && r.error == StartupError::None, "Base nueva debe arrancar: " + r.message_utf8);
        TEST_CHECK(!r.backup_created && r.backup_path.empty(), "Una base nueva no se respalda");
        TEST_CHECK(r.schema_version_before == 0 && r.schema_version_after == N && r.migrations_applied == N,
                   "Debe aplicar todas las migraciones");
        TEST_CHECK(db.is_open(), "La base debe quedar abierta");
        TEST_CHECK(r.quick_check_ms == 0 && r.full_check_ms == 0, "Base nueva: sin comprobaciones de integridad");
        db.close();
    }
    TEST_CHECK(!fs::exists(dir / "backups"), "Base nueva: no debe crearse carpeta de respaldos");

    {
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, options_for(dir, real));
        TEST_CHECK(r.ok, "Segundo arranque debe funcionar: " + r.message_utf8);
        TEST_CHECK(!r.backup_created && r.migrations_applied == 0, "Sin migraciones pendientes no hay respaldo");
        TEST_CHECK(r.schema_version_before == N && r.schema_version_after == N, "Versión estable");
        TEST_CHECK(r.full_check_ms == 0, "Sin pendientes no se corre la comprobación completa");
        db.close();
    }
    TEST_CHECK(count_files(dir / "backups") == 0, "Segundo arranque: no debe haber respaldos");

    // Un archivo de 0 bytes cuenta como base nueva.
    const fs::path dir2 = g_root / "vacia";
    fs::create_directories(dir2);
    write_text(dir2 / "cafeteria.db", "");
    SqliteDatabase db2;
    auto r2 = open_and_migrate_database(db2, options_for(dir2, real));
    TEST_CHECK(r2.ok && !r2.backup_created, "Archivo vacío = base nueva, sin respaldo");
    db2.close();
}

// --- Respaldo antes de migrar, registro y migración 014 ---
static void test_backup_before_migration_and_registry() {
    std::cout << "[TEST] B8: respaldo previo verificado, registro PRE_MIGRATION y migración 014..." << std::endl;
    const fs::path dir = g_root / fs::u8path(u8"con datos ñ") / "Datos Cafetería";
    const fs::path m13 = copy_migrations(g_root / "migraciones13", 13);
    make_db_with_data(dir, m13);

    // Respaldos reales de B6 antes de la migración: LOCAL y PRE_RESTORE con su tamaño y hash.
    std::string rows_before;
    std::set<std::string> preview_before;
    fs::path r5;
    {
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, options_for(dir, m13));
        TEST_CHECK(r.ok && !r.backup_created && r.migrations_applied == 0, "Reabrir en 013 sin pendientes");
        auto account = std::make_shared<SqliteAccountService>(db);
        TEST_CHECK(account->login("dueno_b8", kOwnerPwd).is_success(), "Dueño debe entrar");
        SqliteBackupService backup(db, account, dir.u8string(), m13.u8string());
        const fs::path manual = dir / fs::u8path(u8"respaldos manuales");
        for (int i = 1; i <= 3; ++i) {
            TEST_CHECK(backup.trigger_local_backup((manual / ("r" + std::to_string(i) + ".db")).u8string()).is_success(),
                       "Respaldo local " + std::to_string(i));
        }
        auto restore = backup.restore_from_backup((manual / "r3.db").u8string());
        TEST_CHECK(restore.is_success(), "Restauración (crea el respaldo PRE_RESTORE): " + restore.error_message());
        TEST_CHECK(account->login("dueno_b8", kOwnerPwd).is_success(), "Dueño debe volver a entrar");
        for (int i = 4; i <= 5; ++i) {
            TEST_CHECK(backup.trigger_local_backup((manual / ("r" + std::to_string(i) + ".db")).u8string()).is_success(),
                       "Respaldo local " + std::to_string(i));
        }
        r5 = manual / "r5.db";
        TEST_CHECK(backup.set_retention_policy(BackupRetentionPolicyDto{1, 0}).is_success(), "Política 1 / 0");
        auto preview = backup.preview_backup_cleanup();
        TEST_CHECK(preview.is_success(), "Vista previa antes de migrar");
        for (const auto& it : preview.value().to_delete) preview_before.insert(it.path);
        TEST_CHECK(preview_before.size() == 3, "Antes de migrar se borrarían 3 respaldos LOCAL antiguos");
        rows_before = dump_rows(db,
            "SELECT rowid, id, kind, path, size_bytes, sha256, created_at, created_at_utc, status, COALESCE(status_at,'NULL'), last_error "
            "FROM backup_files ORDER BY rowid;", 11);
        TEST_CHECK(scalar(db, "SELECT COUNT(*) FROM backup_files WHERE kind='LOCAL';") == 4 &&
                       scalar(db, "SELECT COUNT(*) FROM backup_files WHERE kind='PRE_RESTORE';") == 1,
                   "Debe haber 4 LOCAL y 1 PRE_RESTORE registrados");
        db.close();
    }

    // Actualización: base con datos y migraciones pendientes (014 y 015 reales).
    const fs::path real = fs::u8path(SOURCE_MIGRATIONS_DIR);
    const int N = max_real_version();
    SqliteDatabase db;
    auto r = open_and_migrate_database(db, options_for(dir, real));
    TEST_CHECK(r.ok, "La actualización debe funcionar: " + r.message_utf8);
    TEST_CHECK(r.backup_created && r.backup_registered, "Debe crear y registrar el respaldo previo");
    TEST_CHECK(r.schema_version_before == 13 && r.schema_version_after == N && r.migrations_applied == N - 13,
               "Versiones antes y después");
    TEST_CHECK(r.full_check_ms >= 0 && r.quick_check_ms >= 0, "Se midieron las comprobaciones");
    TEST_CHECK(fs::is_regular_file(r.backup_path), "El respaldo previo debe existir");
    TEST_CHECK(r.backup_path.parent_path() == dir / "backups" / "pre-migracion", "Ruta del respaldo previo");
    const std::string name = r.backup_path.filename().string();
    TEST_CHECK(name.rfind("cafeteria-v13-a-v" + std::to_string(N) + "-", 0) == 0 && r.backup_path.extension() == ".db",
               "Nombre del respaldo previo: " + name);
    TEST_CHECK(!fs::exists(fs::path(r.backup_path.u8string() + ".tmp")) &&
                   !fs::exists(fs::path(r.backup_path.u8string() + "-wal")),
               "Sin temporales junto al respaldo");

    // El respaldo es el estado ANTERIOR: versión 13 y los datos del Dueño.
    auto info = SqliteDatabase::inspect_file(r.backup_path);
    TEST_CHECK(info.is_success() && info.value().integrity_ok && info.value().schema_version == 13 &&
                   info.value().has_active_owner,
               "El respaldo previo debe estar íntegro, en la versión 13 y con el Dueño");

    // Registro: tipo PRE_MIGRATION con ruta exacta, tamaño y hash del archivo.
    std::error_code ec;
    const std::string abs_path = fs::absolute(r.backup_path, ec).lexically_normal().u8string();
    TEST_CHECK(scalar(db, "SELECT COUNT(*) FROM backup_files WHERE kind='PRE_MIGRATION';") == 1, "Una fila PRE_MIGRATION");
    TEST_CHECK(scalar_text(db, "SELECT path FROM backup_files WHERE kind='PRE_MIGRATION';") == abs_path, "Ruta registrada");
    TEST_CHECK(scalar(db, "SELECT size_bytes FROM backup_files WHERE kind='PRE_MIGRATION';") ==
                   static_cast<std::int64_t>(fs::file_size(r.backup_path)), "Tamaño registrado");
    TEST_CHECK(scalar_text(db, "SELECT sha256 FROM backup_files WHERE kind='PRE_MIGRATION';") == file_sha(r.backup_path),
               "Hash registrado");
    TEST_CHECK(scalar_text(db, "SELECT status FROM backup_files WHERE kind='PRE_MIGRATION';") == "ACTIVE", "Estado ACTIVE");

    // Migración 014: las filas anteriores se conservan idénticas (incluido el rowid).
    const std::string rows_after = dump_rows(db,
        "SELECT rowid, id, kind, path, size_bytes, sha256, created_at, created_at_utc, status, COALESCE(status_at,'NULL'), last_error "
        "FROM backup_files WHERE kind IN ('LOCAL','PRE_RESTORE') ORDER BY rowid;", 11);
    TEST_CHECK(rows_after == rows_before, "Filas LOCAL y PRE_RESTORE idénticas tras la migración 014");
    // Índices y restricciones.
    TEST_CHECK(scalar(db, "SELECT COUNT(*) FROM sqlite_master WHERE type='index' AND name IN "
                          "('idx_backup_files_kind_created','idx_backup_files_path');") == 2,
               "Los dos índices deben existir");
    TEST_CHECK(scalar_text(db, "SELECT sql FROM sqlite_master WHERE name='backup_files';").find("PRE_MIGRATION") != std::string::npos,
               "La tabla debe aceptar PRE_MIGRATION");
    TEST_CHECK(!db.execute("INSERT INTO backup_files (id, kind, path, size_bytes, sha256, created_at, created_at_utc) "
                           "VALUES ('x','OTRO','p',1,'h','a','b');").is_success(), "Un tipo desconocido sigue rechazado");
    TEST_CHECK(!db.execute("INSERT INTO backup_files (id, kind, path, size_bytes, sha256, created_at, created_at_utc) "
                           "VALUES ('y','LOCAL','p',-1,'h','a','b');").is_success(), "Un tamaño negativo sigue rechazado");
    TEST_CHECK(!db.execute("INSERT INTO backup_files (id, kind, path, size_bytes, sha256, created_at, created_at_utc, status) "
                           "VALUES ('z','LOCAL','p',1,'h','a','b','RARO');").is_success(), "Un estado desconocido sigue rechazado");
    TEST_CHECK(table_exists(db, "login_attempts"), "La migración 015 debe crear login_attempts");
    // Una ejecución repetida del runner no cambia nada.
    {
        MigrationRunner runner(db);
        const std::string before = dump_rows(db, "SELECT version FROM schema_migrations ORDER BY version;", 1);
        TEST_CHECK(runner.run_pending_migrations_from_dir(real).is_success(), "Repetir el runner");
        TEST_CHECK(dump_rows(db, "SELECT version FROM schema_migrations ORDER BY version;", 1) == before,
                   "Repetir el runner no duplica migraciones");
    }

    // La limpieza de B6 sigue igual y NO toca el respaldo previo a migrar.
    {
        auto account = std::make_shared<SqliteAccountService>(db);
        TEST_CHECK(account->login("dueno_b8", kOwnerPwd).is_success(), "Dueño debe entrar tras migrar");
        SqliteBackupService backup(db, account, dir.u8string(), real.u8string());
        auto policy = backup.get_retention_policy();
        TEST_CHECK(policy.is_success() && policy.value().keep_last == 1 && policy.value().keep_days == 0,
                   "La política de retención se conserva");
        auto preview = backup.preview_backup_cleanup();
        TEST_CHECK(preview.is_success(), "Vista previa tras migrar");
        std::set<std::string> preview_after;
        for (const auto& it : preview.value().to_delete) preview_after.insert(it.path);
        TEST_CHECK(preview_after == preview_before, "La limpieza elegiría exactamente lo mismo que antes de migrar");
        TEST_CHECK(preview.value().to_delete.size() == 3 && preview.value().changed.empty(),
                   "Nada cambiado ni inesperado");
        auto clean = backup.cleanup_old_backups();
        TEST_CHECK(clean.is_success() && clean.value().to_delete.size() == 3 && clean.value().failed.empty(),
                   "La limpieza borra los 3 antiguos");
        TEST_CHECK(fs::is_regular_file(r.backup_path), "El respaldo previo a migrar sigue existiendo tras la limpieza");
        TEST_CHECK(scalar_text(db, "SELECT status FROM backup_files WHERE kind='PRE_MIGRATION';") == "ACTIVE",
                   "El respaldo previo sigue ACTIVO");
        TEST_CHECK(fs::is_regular_file(r5), "El respaldo LOCAL más reciente se conserva");
    }
    db.close();
}

// --- Espacio insuficiente y respaldo imposible: no se migra ---
static void test_backup_failures_do_not_migrate() {
    std::cout << "[TEST] B8: sin espacio o sin poder respaldar -> no migra..." << std::endl;
    const fs::path m13 = copy_migrations(g_root / "migraciones13b", 13);
    const fs::path real = fs::u8path(SOURCE_MIGRATIONS_DIR);

    // a) espacio insuficiente (callback inyectado, sin llenar el disco)
    {
        const fs::path dir = g_root / "sin espacio";
        make_db_with_data(dir, m13);
        auto opt = options_for(dir, real);
        fs::path asked;
        opt.free_space_bytes = [&](const fs::path& p) { asked = p; return std::uint64_t(1024); };
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, opt);
        TEST_CHECK(!r.ok && r.error == StartupError::NotEnoughSpace, "Sin espacio no debe migrar");
        TEST_CHECK(!db.is_open(), "La base debe quedar cerrada");
        TEST_CHECK(asked.parent_path() == dir / "backups", "Se consultó el espacio de la unidad del respaldo");
        TEST_CHECK(r.message_utf8.find("espacio") != std::string::npos && r.message_utf8.find("MB") != std::string::npos &&
                       r.message_utf8.find((dir / "backups" / "pre-migracion").u8string()) != std::string::npos &&
                       plain_language(r.message_utf8),
                   "Mensaje llano con megabytes y ruta: " + r.message_utf8);
        TEST_CHECK(count_files(dir / "backups") == 0, "No debe haber respaldo");
        SqliteDatabase check;
        TEST_CHECK(check.open(dir / "cafeteria.db").is_success(), "Reabrir");
        TEST_CHECK(scalar(check, "SELECT MAX(version) FROM schema_migrations;") == 13, "La base sigue en la versión 13");
        TEST_CHECK(scalar(check, "SELECT COUNT(*) FROM users;") == 1, "Los datos siguen ahí");
        check.close();

        // con espacio suficiente (callback generoso) sí migra
        auto opt2 = options_for(dir, real);
        opt2.free_space_bytes = [](const fs::path&) { return std::uint64_t(1) << 40; };
        SqliteDatabase db2;
        auto r2 = open_and_migrate_database(db2, opt2);
        TEST_CHECK(r2.ok && r2.backup_created, "Con espacio suficiente debe migrar con respaldo: " + r2.message_utf8);
        db2.close();
    }

    // b) no se puede crear la carpeta del respaldo (ya existe un archivo con ese nombre)
    {
        const fs::path dir = g_root / "carpeta imposible";
        make_db_with_data(dir, m13);
        write_text(dir / "backups", "soy un archivo, no una carpeta");
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, options_for(dir, real));
        TEST_CHECK(!r.ok && r.error == StartupError::BackupFailed, "Sin poder respaldar no debe migrar");
        TEST_CHECK(!db.is_open(), "La base debe quedar cerrada");
        TEST_CHECK(plain_language(r.message_utf8) && r.message_utf8.find("no se actualizó nada") != std::string::npos,
                   "Mensaje llano: " + r.message_utf8);
        SqliteDatabase check;
        TEST_CHECK(check.open(dir / "cafeteria.db").is_success(), "Reabrir");
        TEST_CHECK(scalar(check, "SELECT MAX(version) FROM schema_migrations;") == 13, "La base sigue en la versión 13");
        check.close();
    }
}

// --- Migración que falla a medias, con dos pendientes ---
static void test_failed_migration_with_two_pending() {
    std::cout << "[TEST] B8: dos migraciones pendientes, falla la segunda..." << std::endl;
    const fs::path dir = g_root / "falla segunda";
    const fs::path m13 = copy_migrations(g_root / "migraciones13c", 13);
    make_db_with_data(dir, m13);

    const fs::path m2 = copy_migrations(g_root / "migraciones_rota", 13);
    write_text(m2 / "014_prueba_buena.sql",
               "CREATE TABLE prueba_b8_buena (x INTEGER NOT NULL);\nINSERT INTO prueba_b8_buena (x) VALUES (1);\n");
    write_text(m2 / "015_prueba_rota.sql",
               "CREATE TABLE prueba_b8_rota (x INTEGER NOT NULL);\nINSERT INTO prueba_b8_rota (x) VALUES (1);\n"
               "INSERT INTO tabla_que_no_existe (x) VALUES (1);\n");

    SqliteDatabase db;
    auto r = open_and_migrate_database(db, options_for(dir, m2));
    TEST_CHECK(!r.ok && r.error == StartupError::MigrationFailed, "La migración rota debe detener el arranque");
    TEST_CHECK(!db.is_open(), "La base debe quedar cerrada");
    TEST_CHECK(r.backup_created && fs::is_regular_file(r.backup_path), "El respaldo previo existe");
    TEST_CHECK(!r.backup_registered, "No se registra un respaldo si la migración falló");
    TEST_CHECK(r.schema_version_before == 13 && r.schema_version_after == 14, "Quedó en la versión 14 (la primera sí se aplicó)");
    TEST_CHECK(r.message_utf8.find("versión 15") != std::string::npos &&
                   r.message_utf8.find("versión 14") != std::string::npos &&
                   r.message_utf8.find(r.backup_path.u8string()) != std::string::npos &&
                   r.message_utf8.find("no se abrirá") != std::string::npos && plain_language(r.message_utf8),
               "El mensaje debe decir el paso que falló, la versión en que quedó, que no abre y la ruta del respaldo: " +
                   r.message_utf8);

    // La base: primera migración aplicada, segunda revertida por completo.
    SqliteDatabase check;
    TEST_CHECK(check.open(dir / "cafeteria.db").is_success(), "Reabrir");
    TEST_CHECK(scalar(check, "SELECT MAX(version) FROM schema_migrations;") == 14, "schema_migrations en 14");
    TEST_CHECK(table_exists(check, "prueba_b8_buena"), "La migración 014 de prueba quedó aplicada");
    TEST_CHECK(!table_exists(check, "prueba_b8_rota"), "La migración rota no dejó nada a medias");
    TEST_CHECK(scalar(check, "SELECT COUNT(*) FROM users;") == 1, "Los datos siguen ahí");
    TEST_CHECK(scalar(check, "SELECT COUNT(*) FROM backup_files WHERE kind='PRE_MIGRATION';") == 0 ||
                   !table_exists(check, "backup_files"), "No hay fila PRE_MIGRATION");
    check.close();

    // El respaldo es el estado anterior COMPLETO (versión 13, sin la tabla de la migración 014 de prueba).
    SqliteDatabase backup;
    TEST_CHECK(backup.open(r.backup_path).is_success(), "Abrir el respaldo");
    TEST_CHECK(scalar(backup, "SELECT MAX(version) FROM schema_migrations;") == 13, "El respaldo está en la versión 13");
    TEST_CHECK(!table_exists(backup, "prueba_b8_buena"), "El respaldo no tiene la tabla de la migración aplicada");
    TEST_CHECK(scalar(backup, "SELECT COUNT(*) FROM users;") == 1, "El respaldo conserva los datos");
    backup.close();
}

// --- Base dañada ---
static void test_damaged_database_not_opened() {
    std::cout << "[TEST] B8: base dañada -> se detecta y no se abre..." << std::endl;
    const fs::path real = fs::u8path(SOURCE_MIGRATIONS_DIR);
    const fs::path dir = g_root / "danada";
    fs::create_directories(dir);
    {
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, options_for(dir, real));
        TEST_CHECK(r.ok, "Base inicial");
        SqliteAccountService account(db);
        TEST_CHECK(account.create_initial_owner("dueno_b8", "Dueño B8", kOwnerPwd, "dueno@cafeteria-itch.edu.mx").is_success(),
                   "Dueño");
        db.close();
    }
    const fs::path db_file = dir / "cafeteria.db";
    // Estropear páginas internas de una copia: bytes alterados en el cuerpo del archivo.
    {
        std::fstream f(db_file, std::ios::in | std::ios::out | std::ios::binary);
        f.seekg(0, std::ios::end);
        const std::streamoff size = f.tellg();
        TEST_CHECK(size > 4096 * 20, "La base de prueba debe tener varias páginas");
        for (std::streamoff off = 4096 * 3; off < 4096 * 18; off += 4096) {
            f.seekp(off + 8);
            const std::string junk(300, '\xA5');
            f.write(junk.data(), static_cast<std::streamsize>(junk.size()));
        }
    }
    const std::string sha_before = file_sha(db_file);
    SqliteDatabase db;
    auto r = open_and_migrate_database(db, options_for(dir, real));
    TEST_CHECK(!r.ok && r.error == StartupError::DatabaseDamaged, "La base dañada debe detectarse");
    TEST_CHECK(!db.is_open(), "La base dañada no se deja abierta para vender");
    TEST_CHECK(r.message_utf8.find(db_file.u8string()) != std::string::npos &&
                   r.message_utf8.find("restaurar") != std::string::npos && plain_language(r.message_utf8),
               "Mensaje llano con la ruta y la indicación de restaurar: " + r.message_utf8);
    TEST_CHECK(file_sha(db_file) == sha_before, "La base dañada no se modificó");
    TEST_CHECK(count_files(dir / "backups") == 0, "No se respalda una base dañada");

    // Un archivo que no es una base de datos.
    const fs::path dir2 = g_root / "basura";
    fs::create_directories(dir2);
    write_text(dir2 / "cafeteria.db", std::string(5000, 'x') + " esto no es una base de datos");
    const std::string sha2 = file_sha(dir2 / "cafeteria.db");
    SqliteDatabase db2;
    auto r2 = open_and_migrate_database(db2, options_for(dir2, real));
    TEST_CHECK(!r2.ok && r2.error == StartupError::DatabaseDamaged && !db2.is_open(), "Un archivo ajeno se rechaza");
    TEST_CHECK(file_sha(dir2 / "cafeteria.db") == sha2, "El archivo ajeno no se modificó");
}

// --- Falta la carpeta de migraciones ---
static void test_missing_migrations_folder() {
    std::cout << "[TEST] B8: sin carpeta de migraciones no se toca nada..." << std::endl;
    const fs::path dir = g_root / "sin migraciones";
    fs::create_directories(dir);
    SqliteDatabase db;
    auto r = open_and_migrate_database(db, options_for(dir, g_root / "no existe"));
    TEST_CHECK(!r.ok && r.error == StartupError::MigrationsFolderMissing && !db.is_open(), "Debe fallar");
    TEST_CHECK(!fs::exists(dir / "cafeteria.db"), "No debe crear la base");
}

// --- --self-check no crea respaldos ---
static void test_self_check_creates_no_backups() {
    std::cout << "[TEST] B8: la autocomprobación no crea respaldos..." << std::endl;
    const fs::path pkg = g_root / "paquete falso";
    const fs::path mig = copy_migrations(pkg / "migrations", max_real_version());
    (void)mig;
    std::string manifest = "version=0.0.0\n";
    for (const auto& e : fs::directory_iterator(pkg / "migrations")) manifest += "migrations/" + e.path().filename().string() + "\n";
    write_text(pkg / kPackageManifestName, manifest);
    const fs::path tmp = g_root / "tmp autocomprobacion";
    fs::create_directories(tmp);
    SelfCheckInput in;
    in.package_dir = pkg;
    in.temp_parent = tmp;
    in.version = "0.0.0";
    auto res = run_package_self_check(in);
    TEST_CHECK(res.ok, "La autocomprobación debe pasar: " + res.report_utf8);
    TEST_CHECK(count_files(tmp) == 0 && !fs::exists(tmp / "backups"), "No debe quedar nada ni respaldos en el temporal");
    TEST_CHECK(!fs::exists(pkg / "backups") && !fs::exists(pkg / "data") && !fs::exists(pkg / "cafeteria.db"),
               "No debe crear respaldos ni datos en el paquete");
}

// ===================== B9 =====================

static std::set<std::string> list_names(const fs::path& dir) {
    std::set<std::string> names;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        names.insert(fs::relative(it->path(), dir, ec).generic_u8string());
    }
    return names;
}

// --- Base más nueva que el programa ---
static void test_newer_database_than_program() {
    std::cout << "[TEST] B9: base más nueva que el programa -> no se abre y no se toca nada..." << std::endl;
    const fs::path real = fs::u8path(SOURCE_MIGRATIONS_DIR);
    const fs::path dir = g_root / "mas nueva";
    fs::create_directories(dir);
    {
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, options_for(dir, real));
        TEST_CHECK(r.ok, "Base inicial en la versión más alta: " + r.message_utf8);
        SqliteAccountService account(db);
        TEST_CHECK(account.create_initial_owner("dueno_b9", "Dueño B9", kOwnerPwd, "dueno@cafeteria-itch.edu.mx").is_success(), "Dueño");
        db.close();
    }
    // El programa instalado solo trae hasta la migración 13: es más antiguo que los datos.
    const fs::path old_package = copy_migrations(g_root / "paquete antiguo", 13);
    const fs::path db_file = dir / "cafeteria.db";
    const std::string sha_before = file_sha(db_file);
    const auto names_before = list_names(dir);

    std::vector<StartupProgress> events;
    auto opt = options_for(dir, old_package);
    opt.progress_min_interval_ms = 0;
    opt.on_progress = [&](const StartupProgress& p) { events.push_back(p); };
    SqliteDatabase db;
    auto r = open_and_migrate_database(db, opt);
    TEST_CHECK(!r.ok && r.error == StartupError::DatabaseNewerThanProgram, "Debe rechazar la base más nueva");
    TEST_CHECK(!db.is_open() && !db.has_progress_callback(), "La base queda cerrada y sin manejador de progreso");
    TEST_CHECK(!r.backup_created && r.migrations_applied == 0, "Sin respaldo ni migraciones");
    TEST_CHECK(r.message_utf8.find("más antiguo que los datos") != std::string::npos &&
                   r.message_utf8.find("Instale la versión correcta") != std::string::npos &&
                   r.message_utf8.find(db_file.u8string()) != std::string::npos && plain_language(r.message_utf8),
               "Mensaje llano con la ruta: " + r.message_utf8);
    TEST_CHECK(file_sha(db_file) == sha_before, "La base no se modificó");
    TEST_CHECK(!fs::exists(dir / "backups"), "No debe crearse la carpeta backups");
    TEST_CHECK(list_names(dir) == names_before, "No debe aparecer ni desaparecer ningún archivo");
    for (const auto& e : events) {
        TEST_CHECK(e.phase != StartupPhase::UpdateStarting && e.phase != StartupPhase::BackupCopy, "Sin ventana de actualización");
    }

    // Una base en la MISMA versión que el paquete no es "más nueva".
    SqliteDatabase db2;
    auto same = open_and_migrate_database(db2, options_for(dir, real));
    TEST_CHECK(same.ok, "Con el paquete correcto abre normalmente: " + same.message_utf8);
    db2.close();
}

// --- Avisos de progreso ---
static void test_progress_order_and_handler_removed() {
    std::cout << "[TEST] B9: avisos de progreso en orden y manejador de SQLite retirado..." << std::endl;
    const fs::path real = fs::u8path(SOURCE_MIGRATIONS_DIR);
    const int N = max_real_version();
    const fs::path m13 = copy_migrations(g_root / "migraciones13e", 13);

    // a) actualización con datos: todas las fases, en orden
    {
        const fs::path dir = g_root / "progreso";
        make_db_with_data(dir, m13);
        std::vector<StartupProgress> events;
        int ticks_after = 0;
        bool collecting = true;
        auto opt = options_for(dir, real);
        opt.progress_min_interval_ms = 0;
        opt.on_progress = [&](const StartupProgress& p) {
            if (collecting) events.push_back(p); else ++ticks_after;
        };
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, opt);
        collecting = false;
        TEST_CHECK(r.ok && r.backup_created, "Actualización: " + r.message_utf8);
        TEST_CHECK(!events.empty() && events.front().phase == StartupPhase::UpdateStarting, "El primer aviso es UpdateStarting");
        TEST_CHECK(events.back().phase == StartupPhase::Finished, "El último aviso es Finished");
        std::set<StartupPhase> seen;
        int last_phase = -1;
        bool in_order = true;
        for (const auto& e : events) {
            const int ph = static_cast<int>(e.phase);
            if (ph < last_phase) in_order = false;
            last_phase = ph;
            seen.insert(e.phase);
            TEST_CHECK(!e.message_utf8.empty() && plain_language(e.message_utf8), "Mensaje de fase en lenguaje llano");
            TEST_CHECK(e.percent >= -1 && e.percent <= 100, "Porcentaje en rango");
        }
        TEST_CHECK(in_order, "Las fases llegan en orden");
        for (auto ph : {StartupPhase::UpdateStarting, StartupPhase::QuickCheck, StartupPhase::FullCheck, StartupPhase::BackupCopy,
                        StartupPhase::BackupVerify, StartupPhase::Migrate, StartupPhase::BackupRegister, StartupPhase::Finished}) {
            TEST_CHECK(seen.count(ph) == 1, "Debe avisar la fase " + std::to_string(static_cast<int>(ph)));
        }
        // Migración N de M
        std::vector<int> steps;
        for (const auto& e : events) {
            if (e.phase == StartupPhase::Migrate && e.step > 0 && (steps.empty() || steps.back() != e.step)) {
                steps.push_back(e.step);
                TEST_CHECK(e.total_steps == N - 13, "total de migraciones pendientes");
                TEST_CHECK(e.message_utf8.find("paso " + std::to_string(e.step) + " de " + std::to_string(e.total_steps)) != std::string::npos,
                           "Texto 'paso N de M': " + e.message_utf8);
            }
        }
        TEST_CHECK(steps.size() == static_cast<std::size_t>(N - 13), "Un aviso por migración pendiente");
        for (std::size_t i = 0; i < steps.size(); ++i) TEST_CHECK(steps[i] == static_cast<int>(i) + 1, "Pasos 1..M en orden");
        // Copia del respaldo: porcentaje real, no decreciente, llega a 100
        int last_pct = -1, max_pct = -1;
        for (const auto& e : events) {
            if (e.phase != StartupPhase::BackupCopy || e.percent < 0) continue;
            TEST_CHECK(e.percent >= last_pct, "El porcentaje de la copia no retrocede");
            last_pct = e.percent;
            max_pct = std::max(max_pct, e.percent);
        }
        TEST_CHECK(max_pct == 100, "La copia llega al 100 %");
        // Las fases de integridad no tienen porcentaje real
        for (const auto& e : events) {
            if (e.phase == StartupPhase::QuickCheck || e.phase == StartupPhase::FullCheck || e.phase == StartupPhase::BackupVerify) {
                TEST_CHECK(e.percent == -1, "Fase sin porcentaje real");
            }
        }

        // El manejador de SQLite ya no existe: trabajo pesado después no genera avisos.
        TEST_CHECK(!db.has_progress_callback(), "La conexión no conserva manejador de progreso");
        const std::size_t seen_events = events.size();
        TEST_CHECK(db.execute("CREATE TABLE prueba_b9 (x INTEGER);").is_success(), "Tabla de prueba");
        TEST_CHECK(db.execute("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM n WHERE i < 300000) "
                              "INSERT INTO prueba_b9 SELECT i FROM n;").is_success(), "Trabajo pesado tras el arranque");
        TEST_CHECK(scalar(db, "SELECT COUNT(*) FROM prueba_b9;") == 300000, "El trabajo pesado se completó");
        TEST_CHECK(ticks_after == 0 && events.size() == seen_events, "Ningún aviso después del arranque");
        db.close();
    }

    // b) base nueva con aviso: sin ventana de actualización ni respaldo
    {
        const fs::path dir = g_root / "progreso nueva";
        fs::create_directories(dir);
        std::vector<StartupProgress> events;
        auto opt = options_for(dir, real);
        opt.progress_min_interval_ms = 0;
        opt.on_progress = [&](const StartupProgress& p) { events.push_back(p); };
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, opt);
        TEST_CHECK(r.ok && !r.backup_created, "Base nueva con aviso");
        for (const auto& e : events) {
            TEST_CHECK(e.phase != StartupPhase::UpdateStarting && e.phase != StartupPhase::BackupCopy &&
                           e.phase != StartupPhase::FullCheck, "Una base nueva no muestra fases de actualización");
        }
        TEST_CHECK(!db.has_progress_callback(), "Sin manejador tras el arranque");
        db.close();

        // Segundo arranque, sin pendientes: tampoco hay ventana.
        events.clear();
        SqliteDatabase db2;
        auto r2 = open_and_migrate_database(db2, opt);
        TEST_CHECK(r2.ok && !r2.backup_created, "Segundo arranque");
        for (const auto& e : events) TEST_CHECK(e.phase != StartupPhase::UpdateStarting, "Sin pendientes no hay ventana");
        TEST_CHECK(!db2.has_progress_callback(), "Sin manejador tras el segundo arranque");
        db2.close();
    }

    // c) sin aviso todo funciona igual (ya cubierto por todas las demás pruebas); además, un aviso que
    //    lanza una excepción no rompe el arranque.
    {
        const fs::path dir = g_root / "progreso excepcion";
        make_db_with_data(dir, m13);
        auto opt = options_for(dir, real);
        opt.progress_min_interval_ms = 0;
        int calls = 0;
        opt.on_progress = [&](const StartupProgress&) { ++calls; throw std::runtime_error("aviso defectuoso"); };
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, opt);
        TEST_CHECK(r.ok && r.backup_created && calls > 0, "Un aviso que lanza excepción no interrumpe la actualización");
        TEST_CHECK(scalar(db, "SELECT MAX(version) FROM schema_migrations;") == N, "Se aplicaron todas las migraciones");
        db.close();
    }

    // d) una migración larga no se aborta por el manejador, y los avisos de latido llegan durante ella
    {
        const fs::path dir = g_root / "progreso larga";
        make_db_with_data(dir, m13);
        const fs::path mlarga = copy_migrations(g_root / "migraciones larga", 13);
        write_text(mlarga / "014_larga.sql",
                   "CREATE TABLE larga_b9 (x INTEGER NOT NULL);\n"
                   "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM n WHERE i < 400000) "
                   "INSERT INTO larga_b9 SELECT i FROM n;\n");
        auto opt = options_for(dir, mlarga);
        opt.progress_min_interval_ms = 0;
        int migrate_events = 0;
        opt.on_progress = [&](const StartupProgress& p) { if (p.phase == StartupPhase::Migrate) ++migrate_events; };
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, opt);
        TEST_CHECK(r.ok, "La migración larga debe terminar: " + r.message_utf8);
        TEST_CHECK(scalar(db, "SELECT COUNT(*) FROM larga_b9;") == 400000, "La migración larga no se abortó (400000 filas)");
        TEST_CHECK(migrate_events > 2, "Llegaron avisos de latido durante la migración: " + std::to_string(migrate_events));
        TEST_CHECK(scalar(db, "SELECT MAX(version) FROM schema_migrations;") == 14, "Versión 14 aplicada");
        db.close();
    }
}

// --- El manejador se quita también en los caminos de error ---
static void test_handler_removed_on_error_paths() {
    std::cout << "[TEST] B9: el manejador de progreso se retira en cada camino de error..." << std::endl;
    const fs::path real = fs::u8path(SOURCE_MIGRATIONS_DIR);
    const fs::path m13 = copy_migrations(g_root / "migraciones13f", 13);
    int events = 0;
    auto with_listener = [&](StartupOptions o) {
        o.progress_min_interval_ms = 0;
        o.on_progress = [&](const StartupProgress&) { ++events; };
        return o;
    };

    {   // sin espacio
        const fs::path dir = g_root / "err espacio";
        make_db_with_data(dir, m13);
        auto o = with_listener(options_for(dir, real));
        o.free_space_bytes = [](const fs::path&) { return std::uint64_t(0); };
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, o);
        TEST_CHECK(!r.ok && r.error == StartupError::NotEnoughSpace && !db.has_progress_callback() && !db.is_open(),
                   "Sin espacio: manejador retirado");
    }
    {   // migración que falla
        const fs::path dir = g_root / "err migracion";
        make_db_with_data(dir, m13);
        const fs::path mrota = copy_migrations(g_root / "migraciones rota b9", 13);
        write_text(mrota / "014_rota.sql", "INSERT INTO tabla_que_no_existe (x) VALUES (1);\n");
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, with_listener(options_for(dir, mrota)));
        TEST_CHECK(!r.ok && r.error == StartupError::MigrationFailed && !db.has_progress_callback() && !db.is_open(),
                   "Migración fallida: manejador retirado");
    }
    {   // base dañada
        const fs::path dir = g_root / "err danada";
        fs::create_directories(dir);
        write_text(dir / "cafeteria.db", std::string(5000, 'x') + " no es una base");
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, with_listener(options_for(dir, real)));
        TEST_CHECK(!r.ok && r.error == StartupError::DatabaseDamaged && !db.has_progress_callback() && !db.is_open(),
                   "Base dañada: manejador retirado");
    }
    {   // no se puede crear la carpeta del respaldo
        const fs::path dir = g_root / "err carpeta";
        make_db_with_data(dir, m13);
        write_text(dir / "backups", "soy un archivo");
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, with_listener(options_for(dir, real)));
        TEST_CHECK(!r.ok && r.error == StartupError::BackupFailed && !db.has_progress_callback() && !db.is_open(),
                   "Respaldo imposible: manejador retirado");
    }
    TEST_CHECK(events > 0, "Los avisos sí llegaron antes de cada error");
}

// --- Copia por tandas: mismo resultado que la copia de una vez ---
static void test_backup_in_batches_same_result() {
    std::cout << "[TEST] B9: copia por tandas igual que la copia de una vez..." << std::endl;
    const fs::path real = fs::u8path(SOURCE_MIGRATIONS_DIR);
    const fs::path dir = g_root / "tandas";
    fs::create_directories(dir);
    SqliteDatabase db;
    auto r = open_and_migrate_database(db, options_for(dir, real));
    TEST_CHECK(r.ok, "Base");
    SqliteAccountService account(db);
    TEST_CHECK(account.create_initial_owner("dueno_t", "Dueño T", kOwnerPwd, "dueno@cafeteria-itch.edu.mx").is_success(), "Dueño");
    TEST_CHECK(db.execute("CREATE TABLE relleno_b9 (x TEXT NOT NULL);").is_success(), "Tabla");
    TEST_CHECK(db.execute("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM n WHERE i < 60000) "
                          "INSERT INTO relleno_b9 SELECT hex(randomblob(100)) FROM n;").is_success(), "Relleno de 60000 filas");

    const fs::path one = dir / "una vez.db";
    const fs::path batches = dir / "tandas.db";
    const fs::path empty_cb = dir / "callback vacio.db";
    TEST_CHECK(db.backup_to(one).is_success(), "Copia de una vez");
    std::vector<std::pair<int, int>> calls;
    TEST_CHECK(db.backup_to(batches, [&](int remaining, int total) { calls.emplace_back(remaining, total); }).is_success(),
               "Copia por tandas");
    TEST_CHECK(db.backup_to(empty_cb, BackupProgress{}).is_success(), "Copia con callback vacío");

    TEST_CHECK(calls.size() > 5, "Varias tandas: " + std::to_string(calls.size()));
    bool monotonic = true;
    for (std::size_t i = 1; i < calls.size(); ++i) {
        if (calls[i].first > calls[i - 1].first) monotonic = false;
        if (calls[i].second <= 0) monotonic = false;
    }
    TEST_CHECK(monotonic && calls.back().first == 0, "Las páginas que faltan no aumentan y terminan en 0");

    for (const auto& f : {one, batches, empty_cb}) {
        TEST_CHECK(SqliteDatabase::make_single_file(f).is_success(), "Dejar como archivo único");
        auto info = SqliteDatabase::inspect_file(f);
        TEST_CHECK(info.is_success() && info.value().integrity_ok && info.value().has_expected_tables &&
                       info.value().schema_version == max_real_version() && info.value().has_active_owner,
                   "Copia íntegra y completa: " + f.filename().u8string());
        SqliteDatabase copy;
        TEST_CHECK(copy.open(f).is_success(), "Abrir copia");
        TEST_CHECK(scalar(copy, "SELECT COUNT(*) FROM relleno_b9;") == 60000, "60000 filas en la copia");
        TEST_CHECK(scalar(copy, "SELECT COUNT(*) FROM users;") == 1, "El Dueño está en la copia");
        copy.close();
    }
    TEST_CHECK(fs::file_size(one) == fs::file_size(batches) && fs::file_size(one) == fs::file_size(empty_cb),
               "Las tres copias pesan lo mismo");
    // inspect_file con latido: el resultado no cambia y el aviso nunca interrumpe.
    int ticks = 0;
    auto info = SqliteDatabase::inspect_file(batches, [&] { ++ticks; });
    TEST_CHECK(info.is_success() && info.value().integrity_ok && ticks > 0, "inspect_file con latido (" + std::to_string(ticks) + " avisos)");
    db.close();
}

int main() {
    std::error_code ec;
    g_root = fs::temp_directory_path(ec) /
             fs::u8path(u8"cafeteria b8 arranque ñ " +
                        std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);

    test_new_database_and_second_start();
    test_backup_before_migration_and_registry();
    test_backup_failures_do_not_migrate();
    test_failed_migration_with_two_pending();
    test_damaged_database_not_opened();
    test_missing_migrations_folder();
    test_self_check_creates_no_backups();
    test_newer_database_than_program();
    test_progress_order_and_handler_removed();
    test_handler_removed_on_error_paths();
    test_backup_in_batches_same_result();

    std::cout << "Pruebas de arranque seguro (B8) correctas. TEST_CHECK ejecutados: " << g_checks << std::endl;
    cleanup_and_exit(0);
}
