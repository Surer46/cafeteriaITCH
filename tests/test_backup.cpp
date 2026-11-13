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
    test_failure_with_open_transaction(base, mig);

    std::error_code ec;
    fs::remove_all(base, ec);
    std::cout << "\n>>> TODAS LAS PRUEBAS DE RESPALDO PASARON CON EXITO! <<<\n" << std::endl;
    return 0;
}
