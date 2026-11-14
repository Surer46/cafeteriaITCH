// Pruebas B1 / I5: respaldo local seguro y restauración (solo backend, sin GUI).
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include <iterator>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "sqlite3.h"
#include "app/storage/sqlite_database.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/crypto/password_hasher.hpp"
#include "app/services/sqlite_account_service.hpp"
#include "app/services/sqlite_backup_service.hpp"

#define TEST_CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA DE RESPALDO] " << (message) \
                      << "\n  Condición: " << #condition \
                      << "\n  Archivo:   " << __FILE__ \
                      << "\n  Línea:     " << __LINE__ << "\n" << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace app::storage;
using namespace app::services;
using namespace app::contracts;
namespace fs = std::filesystem;

namespace {

const char* kOwnerPwd = "Clave-Prueba-Dueno-1";

fs::path find_migrations_dir() {
    std::error_code ec;
#ifdef MIGRATIONS_DIR_PATH
    fs::path p1(MIGRATIONS_DIR_PATH);
    if (fs::exists(p1 / "001_initial_schema.sql", ec)) return p1;
#endif
#ifdef SOURCE_MIGRATIONS_DIR
    fs::path p2(SOURCE_MIGRATIONS_DIR);
    if (fs::exists(p2 / "001_initial_schema.sql", ec)) return p2;
#endif
    return fs::path();
}

fs::path make_temp_root() {
    std::random_device rd;
    std::mt19937_64 gen(rd());
    fs::path root = fs::temp_directory_path() / fs::u8path("itch_b1_" + std::to_string(gen()));
    fs::create_directories(root);
    return root;
}

std::int64_t scalar(SqliteDatabase& db, const std::string& sql) {
    auto prep = db.prepare(sql);
    TEST_CHECK(prep.is_success(), "Debe preparar consulta: " + sql);
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    TEST_CHECK(step.is_success() && step.value(), "Consulta debe devolver fila: " + sql);
    return stmt.get_int64(0);
}

std::string text_scalar(SqliteDatabase& db, const std::string& sql) {
    auto prep = db.prepare(sql);
    TEST_CHECK(prep.is_success(), "Debe preparar consulta: " + sql);
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    TEST_CHECK(step.is_success() && step.value(), "Consulta debe devolver fila: " + sql);
    return stmt.get_text(0);
}

std::int64_t count_users(SqliteDatabase& db) { return scalar(db, "SELECT COUNT(*) FROM users;"); }
std::int64_t count_audit(SqliteDatabase& db, const std::string& type) {
    return scalar(db, "SELECT COUNT(*) FROM audit_events WHERE event_type = '" + type + "';");
}

// Entorno completo: base real migrada, Dueño con sesión y servicio de respaldos.
struct Env {
    fs::path root;
    fs::path data_dir;
    fs::path migrations;
    SqliteDatabase db;
    std::shared_ptr<SqliteAccountService> account;
    std::unique_ptr<SqliteBackupService> backup;
    int restored_callbacks{0};

    explicit Env(const fs::path& base_root, const fs::path& mig_dir, bool pass_migrations = true) {
        root = base_root;
        migrations = mig_dir;
        // Ruta con acentos y espacios
        data_dir = root / fs::u8path(u8"Datos Cafetería ñandú");
        fs::create_directories(data_dir);
        TEST_CHECK(db.open(data_dir / "cafeteria.db").is_success(), "Debe abrir la base");
        MigrationRunner runner(db);
        auto mig = runner.run_pending_migrations_from_dir(migrations);
        TEST_CHECK(mig.is_success(), "Migraciones deben aplicarse: " + mig.error_message());
        account = std::make_shared<SqliteAccountService>(db);
        auto own = account->create_initial_owner("dueno_b1", "Dueño B1", kOwnerPwd, "dueno@cafeteria-itch.edu.mx");
        TEST_CHECK(own.is_success(), "Debe crear Dueño: " + own.error_message());
        make_backup_service(pass_migrations);
    }

    void make_backup_service(bool pass_migrations = true) {
        backup = std::make_unique<SqliteBackupService>(
            db, account, data_dir.u8string(),
            pass_migrations ? migrations.u8string() : std::string(),
            [this]() { ++restored_callbacks; });
    }

    void login_owner(const std::string& user = "dueno_b1") {
        auto r = account->login(user, kOwnerPwd);
        TEST_CHECK(r.is_success(), "Dueño debe iniciar sesión: " + r.error_message());
    }

    void add_cashier(const std::string& name) {
        CreateUserDto c{name, "Cajero " + name, "Clave-Cajero-1", "role-cashier", name + "@cafeteria-itch.edu.mx"};
        auto r = account->create_user(c);
        TEST_CHECK(r.is_success(), "Debe crear cajero: " + r.error_message());
    }

    void cleanup() {
        backup.reset();
        db.close();
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path snapshot(const std::string& file_name) {
        fs::path dest = root / fs::u8path(u8"Respaldos Importantes áéí") / fs::u8path(file_name);
        auto r = backup->trigger_local_backup(dest.u8string());
        TEST_CHECK(r.is_success(), "Debe crear respaldo: " + r.error_message());
        return dest;
    }
};

// Modifica una copia del respaldo con SQL crudo (modo clásico para evitar archivos -wal).
void tamper(const fs::path& file, const std::string& sql) {
    sqlite3* h = nullptr;
    std::string u = file.u8string();
    TEST_CHECK(sqlite3_open_v2(u.c_str(), &h, SQLITE_OPEN_READWRITE, nullptr) == SQLITE_OK, "Debe abrir copia");
    char* err = nullptr;
    int rc = sqlite3_exec(h, sql.c_str(), nullptr, nullptr, &err);
    std::string msg = err ? err : "";
    sqlite3_free(err);
    sqlite3_close_v2(h);
    TEST_CHECK(rc == SQLITE_OK, "SQL de manipulación debe ejecutarse: " + msg);
}

void duplicate_file(const fs::path& from, const fs::path& to) {
    fs::copy_file(from, to, fs::copy_options::overwrite_existing);
}

// --- Casos ---

void test_roundtrip_and_pre_restore(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B1: respaldo, cambio, restauración, previo y auditoría..." << std::endl;
    Env env(base / "t1", mig);
    env.login_owner();
    env.add_cashier("cajero_a");
    const auto original_users = count_users(env.db);

    fs::path bk = env.snapshot("copia inicial.db");
    TEST_CHECK(fs::exists(bk), "El archivo de respaldo con acentos y espacios debe existir");

    auto ver = env.backup->verify_backup(bk.u8string());
    TEST_CHECK(ver.is_success() && ver.value().integrity_ok, "verify_backup debe reportar integridad ok");
    TEST_CHECK(ver.value().schema_version >= 8, "verify_backup debe reportar la versión de esquema");
    TEST_CHECK(!ver.value().file_modified_at.empty(), "verify_backup debe reportar fecha del archivo");

    env.add_cashier("cajero_b");
    TEST_CHECK(count_users(env.db) == original_users + 1, "El cambio de datos debe aplicarse");

    auto res = env.backup->restore_from_backup(bk.u8string());
    TEST_CHECK(res.is_success(), "Restauración debe tener éxito: " + res.error_message());
    TEST_CHECK(res.value().requires_relogin, "Debe exigir nuevo inicio de sesión");
    TEST_CHECK(count_users(env.db) == original_users, "Deben volver los datos originales");
    TEST_CHECK(!env.account->is_authenticated(), "La sesión debe cerrarse tras restaurar");
    TEST_CHECK(env.restored_callbacks == 1, "Debe invalidarse el estado en memoria de otros servicios");

    // Respaldo previo: existe, está en data_dir/backups y pasa integrity_check
    fs::path pre = fs::u8path(res.value().pre_restore_backup_path);
    TEST_CHECK(fs::exists(pre), "El respaldo previo debe existir");
    TEST_CHECK(pre.parent_path().filename() == "backups", "El respaldo previo debe estar en la carpeta backups");
    auto pre_info = SqliteDatabase::inspect_file(pre);
    TEST_CHECK(pre_info.is_success() && pre_info.value().integrity_ok, "El respaldo previo debe pasar integrity_check");
    {
        // Contiene el estado previo a la restauración (con cajero_b)
        SqliteDatabase pdb;
        TEST_CHECK(pdb.open(pre).is_success(), "Debe abrir el respaldo previo");
        TEST_CHECK(count_users(pdb) == original_users + 1, "El respaldo previo debe contener el estado anterior");
    }

    // Auditoría en la base restaurada, sin rutas absolutas
    TEST_CHECK(count_audit(env.db, "BACKUP_RESTORED") == 1, "Debe auditarse la restauración");
    std::string details = text_scalar(env.db, "SELECT details FROM audit_events WHERE event_type='BACKUP_RESTORED';");
    TEST_CHECK(details.find("copia inicial.db") != std::string::npos, "La auditoría nombra el archivo");
    TEST_CHECK(details.find(base.u8string()) == std::string::npos, "La auditoría no debe contener rutas absolutas");

    // La base restaurada sigue operativa: el Dueño puede volver a entrar
    env.login_owner();
    TEST_CHECK(env.account->has_permission("backup.restore"), "El Dueño conserva backup.restore");
    env.cleanup();
}

void test_corrupt_file_rejected(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B1: archivo corrupto rechazado y base intacta..." << std::endl;
    Env env(base / "t2", mig);
    env.login_owner();
    env.add_cashier("cajero_a");
    const auto users_before = count_users(env.db);

    fs::path bad = env.root / fs::u8path(u8"corrupto ñ.db");
    {
        std::ofstream f(bad, std::ios::binary);
        for (int i = 0; i < 4096; ++i) f << "esto no es sqlite ";
    }
    auto ver = env.backup->verify_backup(bad.u8string());
    TEST_CHECK(!ver.is_success() || !ver.value().integrity_ok, "verify_backup debe marcar el archivo corrupto");

    auto res = env.backup->restore_from_backup(bad.u8string());
    TEST_CHECK(!res.is_success(), "Restaurar archivo corrupto debe fallar");
    TEST_CHECK(count_users(env.db) == users_before, "La base viva debe quedar intacta");
    TEST_CHECK(env.account->is_authenticated(), "La sesión no se cierra si la restauración falla");
    TEST_CHECK(env.restored_callbacks == 0, "No se invalida estado si no hubo restauración");
    TEST_CHECK(!fs::exists(env.data_dir / "backups"), "No se crea respaldo previo si la validación falla");

    // Falla registrada solo con el nombre del archivo
    TEST_CHECK(count_audit(env.db, "BACKUP_RESTORE_FAILED") == 1, "Debe auditarse BACKUP_RESTORE_FAILED");
    std::string details = text_scalar(env.db, "SELECT details FROM audit_events WHERE event_type='BACKUP_RESTORE_FAILED';");
    TEST_CHECK(details.find("corrupto") != std::string::npos, "La auditoría de falla nombra el archivo");
    TEST_CHECK(details.find(base.u8string()) == std::string::npos, "La auditoría de falla no debe contener rutas absolutas");
    env.cleanup();
}

void test_newer_version_rejected(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B1: archivo de versión más nueva rechazado..." << std::endl;
    Env env(base / "t3", mig);
    env.login_owner();
    fs::path bk = env.snapshot("nueva.db");
    fs::path newer = env.root / "version futura.db";
    duplicate_file(bk, newer);
    tamper(newer, "INSERT INTO schema_migrations (version, description) VALUES (999, 'futura');");

    env.add_cashier("cajero_a");
    const auto users_before = count_users(env.db);
    auto res = env.backup->restore_from_backup(newer.u8string());
    TEST_CHECK(!res.is_success(), "Un respaldo de versión más nueva debe rechazarse");
    TEST_CHECK(count_users(env.db) == users_before, "La base viva debe quedar intacta");
    TEST_CHECK(count_audit(env.db, "BACKUP_RESTORE_FAILED") == 1, "Debe auditarse el rechazo");
    env.cleanup();
}

void test_permissions(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B1: permisos backup.manage y backup.restore en el servicio..." << std::endl;
    Env env(base / "t4", mig);
    env.login_owner();
    fs::path bk = env.snapshot("permisos.db");
    env.add_cashier("cajero_p");
    TEST_CHECK(env.account->has_permission("backup.restore"), "El Dueño tiene backup.restore");
    TEST_CHECK(env.account->has_permission("backup.manage"), "El Dueño tiene backup.manage");

    // Cajero
    env.account->logout();
    auto lg = env.account->login("cajero_p", "Clave-Cajero-1");
    TEST_CHECK(lg.is_success(), "Cajero debe iniciar sesión");
    const auto users_before = count_users(env.db);
    const fs::path out = env.root / "no_debe_existir.db";
    auto t = env.backup->trigger_local_backup(out.u8string());
    TEST_CHECK(!t.is_success() && t.error_code() == ErrorCode::Forbidden, "trigger_local_backup sin backup.manage = Forbidden");
    TEST_CHECK(!fs::exists(out), "No debe crearse archivo si no hay permiso");
    auto e = env.backup->set_backup_enabled(true);
    TEST_CHECK(!e.is_success() && e.error_code() == ErrorCode::Forbidden, "set_backup_enabled sin backup.manage = Forbidden");
    auto v = env.backup->verify_backup(bk.u8string());
    TEST_CHECK(!v.is_success() && v.error_code() == ErrorCode::Forbidden, "verify_backup sin permisos = Forbidden");
    auto r = env.backup->restore_from_backup(bk.u8string());
    TEST_CHECK(!r.is_success() && r.error_code() == ErrorCode::Forbidden, "restore_from_backup sin backup.restore = Forbidden");
    TEST_CHECK(count_users(env.db) == users_before, "La base no cambia si se deniega");

    // Sin sesión
    env.account->logout();
    auto r2 = env.backup->restore_from_backup(bk.u8string());
    TEST_CHECK(!r2.is_success() && r2.error_code() == ErrorCode::Forbidden, "Sin sesión = Forbidden");

    // Desarrollador: no recibe backup.restore por defecto
    UserDto dev;
    dev.id = "user-dev";
    dev.username = "desarrollador";
    dev.display_name = "Desarrollador";
    dev.role_id = "role-dev";
    dev.role_name = "Desarrollador";
    dev.is_active = true;
    env.account->set_current_user_for_testing(dev);
    TEST_CHECK(!env.account->has_permission("backup.restore"), "role-dev no tiene backup.restore por defecto");
    auto r3 = env.backup->restore_from_backup(bk.u8string());
    TEST_CHECK(!r3.is_success() && r3.error_code() == ErrorCode::Forbidden, "Desarrollador no puede restaurar");
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM role_permissions WHERE permission_id='backup.restore' AND role_id <> 'role-owner';") == 0,
               "backup.restore solo está asignado a role-owner");
    env.cleanup();
}

void test_page_size_and_no_owner(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B1: page_size distinto y origen sin Dueño activo rechazados..." << std::endl;
    Env env(base / "t5", mig);
    env.login_owner();
    fs::path bk = env.snapshot("base.db");
    auto live_info = SqliteDatabase::inspect_file(bk);
    TEST_CHECK(live_info.is_success() && live_info.value().page_size > 0, "inspect_file devuelve page_size");

    fs::path big = env.root / "pagina 8192.db";
    duplicate_file(bk, big);
    tamper(big, "PRAGMA journal_mode=DELETE; PRAGMA page_size=8192; VACUUM;");
    auto big_info = SqliteDatabase::inspect_file(big);
    TEST_CHECK(big_info.is_success() && big_info.value().integrity_ok, "La copia modificada es íntegra");
    TEST_CHECK(big_info.value().page_size == 8192, "La copia modificada tiene page_size 8192");

    env.add_cashier("cajero_a");
    const auto users_before = count_users(env.db);
    auto r1 = env.backup->restore_from_backup(big.u8string());
    TEST_CHECK(!r1.is_success(), "Page_size distinto debe rechazarse antes de tocar la base");
    TEST_CHECK(count_users(env.db) == users_before, "Base viva intacta tras rechazo por page_size");
    TEST_CHECK(!fs::exists(env.data_dir / "backups"), "No se crea respaldo previo si se rechaza por page_size");

    fs::path no_owner = env.root / "sin dueno.db";
    duplicate_file(bk, no_owner);
    tamper(no_owner, "PRAGMA journal_mode=DELETE; UPDATE users SET role_id='role-cashier';");
    auto no_info = SqliteDatabase::inspect_file(no_owner);
    TEST_CHECK(no_info.is_success() && !no_info.value().has_active_owner, "inspect_file detecta ausencia de Dueño activo");
    auto r2 = env.backup->restore_from_backup(no_owner.u8string());
    TEST_CHECK(!r2.is_success(), "Origen sin Dueño activo debe rechazarse");
    TEST_CHECK(count_users(env.db) == users_before, "Base viva intacta tras rechazo por falta de Dueño");
    TEST_CHECK(env.account->is_authenticated(), "La sesión sigue activa tras rechazo");
    TEST_CHECK(count_audit(env.db, "BACKUP_RESTORE_FAILED") == 2, "Ambos rechazos quedan auditados");
    env.cleanup();
}

void test_status_persists_across_reopen(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B1: el estado del respaldo sobrevive a reabrir la base..." << std::endl;
    fs::path db_file;
    fs::path data_dir;
    {
        Env env(base / "t6", mig);
        db_file = env.data_dir / "cafeteria.db";
        data_dir = env.data_dir;
        env.login_owner();
        auto st0 = env.backup->get_status();
        TEST_CHECK(st0.is_success() && st0.value().last_backup_time == "Nunca" && st0.value().last_status == "PENDIENTE",
                   "Estado inicial: Nunca / PENDIENTE");
        TEST_CHECK(env.backup->set_backup_enabled(true).is_success(), "Activar respaldo");
        env.snapshot("estado.db");
        TEST_CHECK(count_audit(env.db, "BACKUP_LOCAL") == 1, "Respaldo exitoso se audita");
        std::string d = text_scalar(env.db, "SELECT details FROM audit_events WHERE event_type='BACKUP_LOCAL';");
        TEST_CHECK(d.find(base.u8string()) == std::string::npos, "La auditoría de respaldo no contiene rutas absolutas");

        // Fallo: destino imposible (un directorio existente como archivo)
        fs::path as_dir = env.root / "es_directorio.db";
        fs::create_directories(as_dir);
        auto bad = env.backup->trigger_local_backup(as_dir.u8string());
        TEST_CHECK(!bad.is_success(), "Respaldo hacia un destino inválido debe fallar");
        auto st1 = env.backup->get_status();
        TEST_CHECK(st1.value().last_status == "FALLIDA" && !st1.value().last_error.empty(), "Fallo queda registrado con su error");
        // Una vez más con éxito para dejar fecha real
        env.snapshot("estado2.db");
    }
    SqliteDatabase db2;
    TEST_CHECK(db2.open(db_file).is_success(), "Debe reabrir la base");
    auto account2 = std::make_shared<SqliteAccountService>(db2);
    SqliteBackupService backup2(db2, account2, data_dir.u8string(), mig.u8string());
    auto st = backup2.get_status();
    TEST_CHECK(st.is_success(), "get_status tras reabrir");
    const auto& s = st.value();
    TEST_CHECK(s.is_enabled, "is_enabled debe persistir");
    TEST_CHECK(s.last_status == "EXITOSA", "Resultado debe persistir");
    TEST_CHECK(s.last_backup_time != "Reciente" && s.last_backup_time != "Nunca", "La fecha ya no es texto fijo");
    TEST_CHECK(s.last_backup_time.size() == 25 && s.last_backup_time[10] == 'T' && s.last_backup_time[22] == ':',
               "La fecha debe ser ISO 8601 local con zona: " + s.last_backup_time);
    db2.close();
    std::error_code ec6;
    fs::remove_all(base / "t6", ec6);
}

void test_older_version_migrates(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B1: restaurar un respaldo de esquema anterior ejecuta migraciones..." << std::endl;
    Env env(base / "t7", mig);
    env.login_owner();

    // Construir un respaldo de la versión 7 con su propio Dueño
    fs::path old_file = env.root / fs::u8path(u8"antiguo v7 ñ.db");
    {
        SqliteDatabase odb;
        TEST_CHECK(odb.open(env.root / "origen_v7.db").is_success(), "Debe abrir base v7");
        MigrationRunner runner(odb);
        for (int v = 1; v <= 7; ++v) {
            char name[64];
            std::snprintf(name, sizeof(name), "%03d_", v);
            fs::path found;
            for (auto& e : fs::directory_iterator(mig)) {
                if (e.path().filename().string().rfind(name, 0) == 0) found = e.path();
            }
            TEST_CHECK(!found.empty(), "Debe existir la migración " + std::to_string(v));
            std::ifstream in(found, std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            auto r = runner.apply_migration(v, found.filename().string(), ss.str());
            TEST_CHECK(r.is_success(), "Aplicar migración " + std::to_string(v) + ": " + r.error_message());
        }
        // Los servicios actuales ya escriben columnas de migraciones posteriores (updated_at, *_utc):
        // el Dueño del esquema v7 se inserta por SQL con las columnas que ese esquema tenía.
        const std::string old_hash = app::crypto::PasswordHasher::hash_password(kOwnerPwd);
        TEST_CHECK(odb.execute("INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at) "
                               "VALUES ('user-dueno_viejo', 'dueno_viejo', 'Dueño Viejo', 'viejo@cafeteria-itch.edu.mx', '" + old_hash +
                               "', 'role-owner', 1, '2026-10-01 08:00:00');").is_success(),
                   "Debe crear Dueño en base v7");
        TEST_CHECK(odb.backup_to(old_file).is_success(), "Debe exportar respaldo v7");
    }
    auto info = SqliteDatabase::inspect_file(old_file);
    TEST_CHECK(info.is_success() && info.value().schema_version == 7, "El archivo antiguo es de versión 7");

    // Sin carpeta de migraciones: rechazo antes de tocar nada
    env.make_backup_service(false);
    auto no_mig = env.backup->restore_from_backup(old_file.u8string());
    TEST_CHECK(!no_mig.is_success(), "Sin carpeta de migraciones, un respaldo antiguo se rechaza");
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM users WHERE username='dueno_viejo';") == 0, "Base viva intacta");

    env.make_backup_service(true);
    auto res = env.backup->restore_from_backup(old_file.u8string());
    TEST_CHECK(res.is_success(), "Restaurar v7 con migraciones: " + res.error_message());
    TEST_CHECK(scalar(env.db, "SELECT MAX(version) FROM schema_migrations;") >= 8, "Las migraciones pendientes se ejecutan");
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM users WHERE username='dueno_viejo';") == 1, "Los datos del respaldo v7 están presentes");
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM users WHERE username='dueno_b1';") == 0, "Los datos anteriores ya no están");
    env.login_owner("dueno_viejo");
    TEST_CHECK(env.account->has_permission("backup.restore"), "La migración 008 da backup.restore al Dueño restaurado");
    TEST_CHECK(env.backup->get_status().is_success(), "backup_state existe tras migrar");
    env.cleanup();
}

void test_v9_backup_restores_and_migrates(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B4: un respaldo del esquema 9 se restaura y queda migrado a la versión nueva..." << std::endl;
    Env env(base / "t9", mig);
    env.login_owner();
    const auto latest = scalar(env.db, "SELECT MAX(version) FROM schema_migrations;");
    TEST_CHECK(latest >= 10, "El esquema actual debe ser al menos la versión 10");

    // Base v9 con datos (insertados por SQL: los servicios actuales ya esperan el esquema nuevo)
    fs::path old_file = env.root / fs::u8path(u8"antiguo v9 ñ.db");
    {
        SqliteDatabase odb;
        TEST_CHECK(odb.open(env.root / "origen_v9.db").is_success(), "Debe abrir base v9");
        MigrationRunner runner(odb);
        for (int v = 1; v <= 9; ++v) {
            char name[64];
            std::snprintf(name, sizeof(name), "%03d_", v);
            fs::path found;
            for (auto& e : fs::directory_iterator(mig)) {
                if (e.path().filename().string().rfind(name, 0) == 0) found = e.path();
            }
            TEST_CHECK(!found.empty(), "Debe existir la migración " + std::to_string(v));
            std::ifstream in(found, std::ios::binary);
            std::stringstream ss;
            ss << in.rdbuf();
            auto r = runner.apply_migration(v, found.filename().string(), ss.str());
            TEST_CHECK(r.is_success(), "Aplicar migración " + std::to_string(v) + ": " + r.error_message());
        }
        const std::string hash = app::crypto::PasswordHasher::hash_password(kOwnerPwd);
        TEST_CHECK(odb.execute("INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at) "
                               "VALUES ('user-dueno_v9', 'dueno_v9', 'Dueño V9', 'v9@cafeteria-itch.edu.mx', '" + hash +
                               "', 'role-owner', 1, '2026-10-01 08:00:00');").is_success(), "Alta de Dueño v9 por SQL");
        TEST_CHECK(odb.execute("INSERT INTO audit_events (id, event_type, user_id, details, created_at) "
                               "VALUES ('audit-v9', 'OWNER_CREATED', 'user-dueno_v9', 'Alta v9', '2026-10-01 08:00:00');").is_success(),
                   "Evento de auditoría v9 por SQL");
        TEST_CHECK(odb.backup_to(old_file).is_success(), "Debe exportar respaldo v9");
    }
    auto info = SqliteDatabase::inspect_file(old_file);
    TEST_CHECK(info.is_success() && info.value().schema_version == 9, "El archivo antiguo es de versión 9");

    auto res = env.backup->restore_from_backup(old_file.u8string());
    TEST_CHECK(res.is_success(), "Restaurar v9: " + res.error_message());
    TEST_CHECK(scalar(env.db, "SELECT MAX(version) FROM schema_migrations;") == latest, "La base restaurada queda en la versión actual");
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM users WHERE username='dueno_v9';") == 1, "Los datos del respaldo v9 están presentes");
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM users WHERE username='dueno_b1';") == 0, "Los datos anteriores ya no están");
    TEST_CHECK(text_scalar(env.db, "SELECT updated_at FROM users WHERE id='user-dueno_v9';") == "2026-10-01 08:00:00",
               "updated_at de filas antiguas iguala created_at");
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM audit_events WHERE id='audit-v9' AND created_at_utc IS NULL;") == 1,
               "Las filas antiguas conservan created_at_utc en NULL");
    TEST_CHECK(text_scalar(env.db, "SELECT code FROM cash_registers WHERE id='reg-01';") == "C1", "reg-01 recibe el código C1");
    env.login_owner("dueno_v9");
    TEST_CHECK(env.account->has_permission("backup.restore"), "El Dueño restaurado conserva sus permisos");
    env.cleanup();
}

void test_successful_backup_leaves_no_tmp_files(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B5: un respaldo exitoso no deja archivos .tmp y se restaura..." << std::endl;
    Env env(base / "t10", mig);
    env.login_owner();
    env.add_cashier("cajero_tmp");
    fs::path bk = env.snapshot("sin-residuos.db");
    TEST_CHECK(fs::exists(bk), "El respaldo existe");
    int tmp_like = 0;
    int total = 0;
    for (auto& e : fs::directory_iterator(bk.parent_path())) {
        ++total;
        const std::string name = e.path().filename().u8string();
        if (name.find(".tmp") != std::string::npos || name.find("-wal") != std::string::npos || name.find("-shm") != std::string::npos) ++tmp_like;
    }
    TEST_CHECK(total == 1, "Solo existe el archivo de respaldo junto al destino");
    TEST_CHECK(tmp_like == 0, "No quedan archivos .tmp, -wal ni -shm junto al destino");
    // Dos respaldos seguidos al mismo destino tampoco dejan residuos
    fs::path bk2 = env.snapshot("sin-residuos.db");
    int total2 = 0;
    for (auto& e : fs::directory_iterator(bk2.parent_path())) { (void)e; ++total2; }
    TEST_CHECK(total2 == 1, "Un segundo respaldo al mismo destino no deja residuos");
    // El respaldo pasa verificación y se restaura con sus datos
    auto info = SqliteDatabase::inspect_file(bk);
    TEST_CHECK(info.is_success() && info.value().integrity_ok, "El respaldo pasa la verificación de integridad");
    env.add_cashier("cajero_posterior");
    auto res = env.backup->restore_from_backup(bk.u8string());
    TEST_CHECK(res.is_success(), "El respaldo se restaura: " + res.error_message());
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM users WHERE username='cajero_tmp';") == 1, "Los datos del respaldo están presentes tras restaurar");
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM users WHERE username='cajero_posterior';") == 0, "Lo posterior al respaldo ya no está");
    env.cleanup();
}

void write_text_file(const fs::path& p, const std::string& text) {
    std::ofstream out(p, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string file_name_of(const std::string& path_text) {
    return fs::u8path(path_text).filename().u8string();
}

bool contains_name(const std::vector<BackupCleanupItemDto>& items, const std::string& name) {
    for (const auto& it : items) {
        if (file_name_of(it.path) == name) return true;
    }
    return false;
}

void test_cleanup_policy_preview_and_safety(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B6: limpieza de respaldos (política, vista previa, archivos ajenos, permisos, auditoría)..." << std::endl;
    Env env(base / "t11", mig);
    env.login_owner();

    auto pol = env.backup->get_retention_policy();
    TEST_CHECK(pol.is_success() && pol.value().keep_last == 30 && pol.value().keep_days == 90, "Política por omisión: 30 respaldos y 90 días");
    TEST_CHECK(!env.backup->set_retention_policy({0, 10}).is_success(), "keep_last 0 rechazado");
    TEST_CHECK(!env.backup->set_retention_policy({1001, 10}).is_success(), "keep_last 1001 rechazado");
    TEST_CHECK(!env.backup->set_retention_policy({5, -1}).is_success(), "keep_days negativo rechazado");
    TEST_CHECK(!env.backup->set_retention_policy({5, 3651}).is_success(), "keep_days 3651 rechazado");
    TEST_CHECK(env.backup->get_retention_policy().value().keep_last == 30, "Una política rechazada no cambia nada");
    TEST_CHECK(env.backup->set_retention_policy({2, 0}).is_success(), "Política de 2 respaldos sin protección por días");
    TEST_CHECK(count_audit(env.db, "BACKUP_RETENTION_POLICY") == 1, "Auditoría del cambio de política");

    std::vector<fs::path> p;
    for (int i = 1; i <= 5; ++i) p.push_back(env.snapshot("b" + std::to_string(i) + ".db"));
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM backup_files WHERE kind='LOCAL' AND status='ACTIVE';") == 5, "Los 5 respaldos quedan registrados");

    // Archivos que NO son respaldos registrados: nunca se borran
    const fs::path foreign = p[0].parent_path() / "ajeno.db";
    write_text_file(foreign, "no soy un respaldo de la aplicación");
    const fs::path backups_dir = env.data_dir / "backups";
    fs::create_directories(backups_dir);
    const fs::path legacy = backups_dir / "pre_restore_20250101_000000.db";
    fs::copy_file(p[4], legacy);

    auto preview = env.backup->preview_backup_cleanup();
    TEST_CHECK(preview.is_success(), "Vista previa: " + preview.error_message());
    TEST_CHECK(preview.value().dry_run && preview.value().to_delete.size() == 3 && preview.value().kept_count == 2, "La vista previa lista 3 y conserva 2");
    TEST_CHECK(contains_name(preview.value().to_delete, "b1.db") && contains_name(preview.value().to_delete, "b2.db") &&
               contains_name(preview.value().to_delete, "b3.db"), "La vista previa señala los 3 más antiguos");
    std::int64_t sum = 0;
    for (const auto& it : preview.value().to_delete) {
        TEST_CHECK(!it.path.empty() && !it.created_at.empty() && it.size_bytes > 0 && it.kind == "LOCAL", "Cada elemento trae ruta, fecha, tamaño y tipo");
        sum += it.size_bytes;
    }
    TEST_CHECK(preview.value().bytes_freed == sum, "La vista previa suma los bytes");
    for (const auto& f : p) TEST_CHECK(fs::exists(f), "La vista previa no borra nada");
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM backup_files WHERE status='ACTIVE';") == 5 && count_audit(env.db, "BACKUP_CLEANUP") == 0,
               "La vista previa no modifica el registro ni audita");

    auto clean = env.backup->cleanup_old_backups();
    TEST_CHECK(clean.is_success() && !clean.value().dry_run, "Limpieza: " + clean.error_message());
    TEST_CHECK(clean.value().to_delete.size() == 3 && clean.value().bytes_freed == sum && clean.value().failed.empty() && clean.value().changed.empty(), "Se borraron los 3 antiguos");
    TEST_CHECK(!fs::exists(p[0]) && !fs::exists(p[1]) && !fs::exists(p[2]), "Los 3 antiguos ya no existen");
    TEST_CHECK(fs::exists(p[3]) && fs::exists(p[4]), "Se conservan los 2 más recientes");
    TEST_CHECK(fs::exists(foreign) && fs::exists(legacy), "El archivo ajeno y el respaldo anterior no registrado siguen ahí");
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM backup_files WHERE status='DELETED';") == 3, "Registro: 3 borrados");
    TEST_CHECK(count_audit(env.db, "BACKUP_CLEANUP") == 1 &&
               text_scalar(env.db, "SELECT details FROM audit_events WHERE event_type='BACKUP_CLEANUP';").find("borrados 3") != std::string::npos,
               "Auditoría con la cantidad de borrados");
    auto again = env.backup->cleanup_old_backups();
    TEST_CHECK(again.is_success() && again.value().to_delete.empty() && fs::exists(p[3]) && fs::exists(p[4]), "Una segunda limpieza no borra nada más");

    // Sin permiso se rechaza
    env.add_cashier("cajero_pol");
    TEST_CHECK(env.account->login("cajero_pol", "Clave-Cajero-1").is_success(), "Login del cajero");
    TEST_CHECK(env.backup->preview_backup_cleanup().error_code() == ErrorCode::Forbidden, "Cajero: vista previa rechazada");
    TEST_CHECK(env.backup->cleanup_old_backups().error_code() == ErrorCode::Forbidden, "Cajero: limpieza rechazada");
    TEST_CHECK(env.backup->get_retention_policy().error_code() == ErrorCode::Forbidden, "Cajero: leer la política rechazado");
    TEST_CHECK(env.backup->set_retention_policy({5, 5}).error_code() == ErrorCode::Forbidden, "Cajero: cambiar la política rechazado");
    TEST_CHECK(fs::exists(p[3]) && fs::exists(p[4]), "Sin permiso no se tocó ningún archivo");
    env.cleanup();
}

void test_cleanup_never_deletes_last_valid_or_changed_files(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B6: la limpieza no borra el último válido ni archivos que cambiaron..." << std::endl;
    {
        Env env(base / "t12", mig);
        env.login_owner();
        TEST_CHECK(env.backup->set_retention_policy({1, 0}).is_success(), "Política de 1 respaldo");
        fs::path a = env.snapshot("a.db");
        fs::path b = env.snapshot("b.db");
        fs::path c = env.snapshot("c.db");
        // El más reciente deja de ser el registrado: el último VÁLIDO es b
        { std::ofstream out(c, std::ios::binary | std::ios::app); out << "basura"; }
        auto res = env.backup->cleanup_old_backups();
        TEST_CHECK(res.is_success(), "Limpieza con el más reciente alterado");
        TEST_CHECK(!fs::exists(a) && fs::exists(b) && fs::exists(c), "Se borra a; b (último válido) y c (alterado) se conservan");
        TEST_CHECK(contains_name(res.value().to_delete, "a.db") && res.value().to_delete.size() == 1, "Solo a.db borrado");
        // Si ninguno es válido, no se borra nada
        { std::ofstream out(b, std::ios::binary | std::ios::app); out << "basura"; }
        auto none = env.backup->cleanup_old_backups();
        TEST_CHECK(none.is_success() && none.value().to_delete.empty() && fs::exists(b) && fs::exists(c), "Sin respaldo válido no se borra nada");
        env.cleanup();
    }
    {
        Env env(base / "t13", mig);
        env.login_owner();
        TEST_CHECK(env.backup->set_retention_policy({1, 0}).is_success(), "Política de 1 respaldo");
        fs::path d = env.snapshot("d.db");
        fs::path e = env.snapshot("e.db");
        // Un archivo ajeno ocupa la ruta de un respaldo registrado
        write_text_file(d, "archivo ajeno puesto en una ruta registrada");
        auto preview = env.backup->preview_backup_cleanup();
        TEST_CHECK(preview.is_success() && preview.value().to_delete.empty() && preview.value().changed.size() == 1 &&
                   contains_name(preview.value().changed, "d.db"), "La vista previa lo marca como cambiado y no como borrable");
        auto res = env.backup->cleanup_old_backups();
        TEST_CHECK(res.is_success() && res.value().to_delete.empty() && res.value().changed.size() == 1, "Se cuenta aparte como cambiado");
        TEST_CHECK(fs::exists(d) && fs::exists(e), "El archivo ajeno no se borró");
        std::ifstream in(d, std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        TEST_CHECK(content == "archivo ajeno puesto en una ruta registrada", "El contenido ajeno quedó intacto");
        TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM backup_files WHERE status='CHANGED';") == 1, "La fila queda marcada como cambiada");
        auto again = env.backup->cleanup_old_backups();
        TEST_CHECK(again.is_success() && again.value().changed.empty() && fs::exists(d), "No se vuelve a considerar ni se borra");
        env.cleanup();
    }
#ifdef _WIN32
    {
        Env env(base / "t14", mig);
        env.login_owner();
        TEST_CHECK(env.backup->set_retention_policy({1, 0}).is_success(), "Política de 1 respaldo");
        fs::path f1 = env.snapshot("f1.db");
        fs::path f2 = env.snapshot("f2.db");
        fs::path f3 = env.snapshot("f3.db");
        // f1 queda abierto sin permiso de borrado: el borrado falla a medias
        HANDLE hold = CreateFileW(f1.wstring().c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        TEST_CHECK(hold != INVALID_HANDLE_VALUE, "Bloquear f1");
        auto res = env.backup->cleanup_old_backups();
        TEST_CHECK(res.is_success(), "La limpieza no falla en bloque");
        TEST_CHECK(res.value().failed.size() == 1 && contains_name(res.value().failed, "f1.db"), "El borrado fallido se reporta");
        TEST_CHECK(contains_name(res.value().to_delete, "f2.db") && res.value().to_delete.size() == 1 && !fs::exists(f2), "Lo demás sí se borró");
        TEST_CHECK(fs::exists(f1) && fs::exists(f3), "f1 (bloqueado) y f3 (el último) siguen ahí: no se pierde nada válido");
        TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM backup_files WHERE status='ACTIVE' AND last_error <> '';") == 1, "El error queda anotado en el registro");
        TEST_CHECK(text_scalar(env.db, "SELECT details FROM audit_events WHERE event_type='BACKUP_CLEANUP' ORDER BY rowid DESC LIMIT 1;").find("con error 1") != std::string::npos, "La auditoría cuenta el error");
        CloseHandle(hold);
        auto retry = env.backup->cleanup_old_backups();
        TEST_CHECK(retry.is_success() && contains_name(retry.value().to_delete, "f1.db") && !fs::exists(f1) && fs::exists(f3), "Liberado el archivo, la siguiente limpieza lo borra");
        env.cleanup();
    }
#endif
}

void test_restore_registers_pre_restore_backup(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B6: el respaldo previo a una restauración queda registrado..." << std::endl;
    Env env(base / "t15", mig);
    env.login_owner();
    fs::path bk = env.snapshot("para-restaurar.db");
    env.add_cashier("cajero_r");
    auto res = env.backup->restore_from_backup(bk.u8string());
    TEST_CHECK(res.is_success(), "Restaurar: " + res.error_message());
    env.login_owner();
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM backup_files WHERE kind='PRE_RESTORE' AND status='ACTIVE';") == 1, "El respaldo previo queda registrado en la base restaurada");
    TEST_CHECK(fs::exists(fs::u8path(res.value().pre_restore_backup_path)), "El respaldo previo existe");
    env.cleanup();
}

void test_server_config_persistence(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B6: configuración del servidor de respaldos persistente y validada..." << std::endl;
    Env env(base / "t16", mig);
    env.login_owner();
    TEST_CHECK(env.backup->get_developer_server_config().error_code() == ErrorCode::Forbidden, "El Dueño no puede leer la configuración técnica");
    BackupServerConfigDto owner_try;
    owner_try.server_url = "https://x.example/api"; owner_try.retention_days = "30";
    TEST_CHECK(env.backup->update_developer_server_config(owner_try).error_code() == ErrorCode::Forbidden, "El Dueño no puede cambiarla");

    UserDto dev_user;
    dev_user.id = "user-dev"; dev_user.username = "desarrollador"; dev_user.display_name = "Desarrollador";
    dev_user.role_id = "role-dev"; dev_user.role_name = "Desarrollador"; dev_user.is_active = true; dev_user.is_owner = false;
    env.account->set_current_user_for_testing(dev_user);

    auto initial = env.backup->get_developer_server_config();
    TEST_CHECK(initial.is_success() && initial.value().server_url.empty() && initial.value().retention_days.empty() && initial.value().is_tls_enabled,
               "Arranca vacía: sin URL ni días inventados");

    BackupServerConfigDto good;
    good.server_url = "https://respaldos.cafeteria.example/api/v1"; good.retention_days = "45"; good.is_tls_enabled = true;
    TEST_CHECK(env.backup->update_developer_server_config(good).is_success(), "Configuración válida guardada");
    auto stored = env.backup->get_developer_server_config();
    TEST_CHECK(stored.is_success() && stored.value().server_url == good.server_url && stored.value().retention_days == "45" && stored.value().is_tls_enabled, "Se lee lo guardado");
    TEST_CHECK(count_audit(env.db, "BACKUP_SERVER_CONFIG_CHANGED") == 1, "Auditoría al cambiarla");

    struct Bad { const char* url; const char* days; bool tls; const char* why; };
    const Bad bad[] = {
        {"https://usuario:secreto@host.example/x", "30", true, "credenciales en la URL"},
        {"https://host.example/x?token=abc", "30", true, "parámetros en la URL"},
        {"https://host.example/x#frag", "30", true, "fragmento en la URL"},
        {"http://host.example/x", "30", true, "http con TLS activado"},
        {"ftp://host.example/x", "30", true, "esquema no permitido"},
        {"https://host.example/x", "abc", true, "días no numéricos"},
        {"https://host.example/x", "0", true, "días en 0"},
        {"https://host.example/x", "99999", true, "días fuera de rango"},
        {"https://host.example/x", "", true, "días vacíos con URL"},
        {"https://", "30", true, "sin nombre de servidor"},
        {"https://host.example/ x", "30", true, "espacios en la URL"},
    };
    for (const auto& b : bad) {
        BackupServerConfigDto cfg;
        cfg.server_url = b.url; cfg.retention_days = b.days; cfg.is_tls_enabled = b.tls;
        auto r = env.backup->update_developer_server_config(cfg);
        TEST_CHECK(!r.is_success() && r.error_code() == ErrorCode::InvalidInput, std::string("Rechazada: ") + b.why);
    }
    TEST_CHECK(env.backup->get_developer_server_config().value().server_url == good.server_url, "Las configuraciones rechazadas no cambian lo guardado");
    TEST_CHECK(count_audit(env.db, "BACKUP_SERVER_CONFIG_CHANGED") == 1, "Lo rechazado no se audita como cambio");
    TEST_CHECK(scalar(env.db, "SELECT COUNT(*) FROM backup_server_config WHERE server_url LIKE '%secreto%' OR server_url LIKE '%token%';") == 0, "Ningún secreto llegó a la base");

    BackupServerConfigDto internal;
    internal.server_url = "http://interno.local/api"; internal.retention_days = "30"; internal.is_tls_enabled = false;
    TEST_CHECK(env.backup->update_developer_server_config(internal).is_success(), "http se acepta si se desactiva TLS");
    TEST_CHECK(env.backup->update_developer_server_config(good).is_success(), "Volver a la configuración con TLS");

    // Sobrevive a cerrar y reabrir la base
    env.backup.reset();
    env.db.close();
    TEST_CHECK(env.db.open(env.data_dir / "cafeteria.db").is_success(), "Reabrir la base");
    env.make_backup_service();
    auto reopened = env.backup->get_developer_server_config();
    TEST_CHECK(reopened.is_success() && reopened.value().server_url == good.server_url && reopened.value().retention_days == "45", "La configuración sobrevive a cerrar y reabrir la base");
    env.cleanup();
}

void test_failure_with_open_transaction(const fs::path& base, const fs::path& mig) {
    std::cout << "[TEST] B1: fallo de copia deja base utilizable y audita..." << std::endl;
    Env env(base / "t8", mig);
    env.login_owner();
    fs::path bk = env.snapshot("tx.db");
    env.add_cashier("cajero_a");
    const auto users_before = count_users(env.db);

    // Con una transacción abierta la copia sobre la conexión viva debe fallar sin cambiar datos.
    TEST_CHECK(env.db.begin_transaction().is_success(), "Abrir transacción");
    auto res = env.backup->restore_from_backup(bk.u8string());
    TEST_CHECK(!res.is_success(), "La restauración con transacción abierta debe fallar");
    TEST_CHECK(env.account->is_authenticated(), "La sesión sigue activa tras el fallo");
    TEST_CHECK(env.restored_callbacks == 0, "No se invalida estado tras un fallo");
    TEST_CHECK(env.db.commit_transaction().is_success(), "Cerrar transacción");
    TEST_CHECK(count_users(env.db) == users_before, "Los datos vivos no cambian tras el fallo");
    TEST_CHECK(count_audit(env.db, "BACKUP_RESTORE_FAILED") == 1, "El fallo de copia queda auditado");
    env.cleanup();
}

} // namespace

int main() {
    std::cout << "=== PRUEBAS B1: RESPALDO LOCAL SEGURO Y RESTAURACION (Debug y Release) ===" << std::endl;
    fs::path mig = find_migrations_dir();
    TEST_CHECK(!mig.empty(), "Debe localizar la carpeta de migraciones");
    fs::path base = make_temp_root();

    test_roundtrip_and_pre_restore(base, mig);
    test_corrupt_file_rejected(base, mig);
    test_newer_version_rejected(base, mig);
    test_permissions(base, mig);
    test_page_size_and_no_owner(base, mig);
    test_status_persists_across_reopen(base, mig);
    test_older_version_migrates(base, mig);
    test_v9_backup_restores_and_migrates(base, mig);
    test_successful_backup_leaves_no_tmp_files(base, mig);
    test_cleanup_policy_preview_and_safety(base, mig);
    test_cleanup_never_deletes_last_valid_or_changed_files(base, mig);
    test_restore_registers_pre_restore_backup(base, mig);
    test_server_config_persistence(base, mig);
    test_failure_with_open_transaction(base, mig);

    std::error_code ec;
    fs::remove_all(base, ec);
    std::cout << "\n>>> TODAS LAS PRUEBAS DE RESPALDO PASARON CON EXITO! <<<\n" << std::endl;
    return 0;
}
