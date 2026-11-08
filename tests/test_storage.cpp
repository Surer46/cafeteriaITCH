#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <random>
#include "app/storage/sqlite_database.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/services/sqlite_account_service.hpp"
#include "app/services/sqlite_backup_service.hpp"
#include "app/services/sqlite_catalog_service.hpp"
#include "app/services/sqlite_inventory_service.hpp"
#include "app/services/sqlite_shift_service.hpp"
#include "app/services/sqlite_cart_service.hpp"
#include "app/services/sqlite_sale_service.hpp"
#include "app/services/sqlite_print_config_service.hpp"
#include "app/domain/unit_converter.hpp"

namespace {

std::string generate_random_secret(std::size_t length = 16) {
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789!@#$%^&*()_+-=";
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::size_t> dist(0, sizeof(charset) - 2);
    std::string result;
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        result += charset[dist(gen)];
    }
    return result;
}

std::string generate_random_alphanumeric(std::size_t length = 8) {
    static const char charset[] =
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937_64 gen(rd());
    std::uniform_int_distribution<std::size_t> dist(0, sizeof(charset) - 2);
    std::string result;
    result.reserve(length);
    for (std::size_t i = 0; i < length; ++i) {
        result += charset[dist(gen)];
    }
    return result;
}

} // namespace

#define TEST_CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA DE ALMACENAMIENTO] " << (message) \
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

// Función para localizar la migración de forma robusta desde cualquier directorio
fs::path find_migration_file() {
    std::error_code ec;
#ifdef MIGRATIONS_FILE_PATH
    fs::path p1(MIGRATIONS_FILE_PATH);
    if (fs::exists(p1, ec)) {
        return p1;
    }
#endif
#ifdef SOURCE_MIGRATIONS_PATH
    fs::path p2(SOURCE_MIGRATIONS_PATH);
    if (fs::exists(p2, ec)) {
        return p2;
    }
#endif
    std::vector<fs::path> candidates = {
        fs::path("migrations/001_initial_schema.sql"),
        fs::path("../migrations/001_initial_schema.sql"),
        fs::path("../../migrations/001_initial_schema.sql"),
        fs::path("tests/migrations/001_initial_schema.sql")
    };
    for (const auto& path : candidates) {
        if (fs::exists(path, ec)) {
            return path;
        }
    }
    return fs::path();
}

fs::path find_migrations_dir() {
    std::error_code ec;
#ifdef MIGRATIONS_DIR_PATH
    fs::path p1(MIGRATIONS_DIR_PATH);
    if (fs::exists(p1 / "001_initial_schema.sql", ec)) {
        return p1;
    }
#endif
#ifdef SOURCE_MIGRATIONS_DIR
    fs::path p2(SOURCE_MIGRATIONS_DIR);
    if (fs::exists(p2 / "001_initial_schema.sql", ec)) {
        return p2;
    }
#endif
    std::vector<fs::path> candidates = {
        fs::path("migrations"),
        fs::path("../migrations"),
        fs::path("../../migrations"),
        fs::path("tests/migrations")
    };
    for (const auto& path : candidates) {
        if (fs::exists(path / "001_initial_schema.sql", ec)) {
            return path;
        }
    }
    return fs::path();
}

void test_sqlite_lifecycle_and_persistence() {
    std::cout << "[TEST] Storage: ciclo de vida, migraciones y persistencia..." << std::endl;

    const std::string db_file = "test_persistence.db";
    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }

    // 1. Cargar archivo SQL de migración 001
    fs::path migration_path = find_migration_file();
    TEST_CHECK(!migration_path.empty(), "Debe localizar el archivo 001_initial_schema.sql desde cualquier directorio");

    std::ifstream sql_file(migration_path);
    TEST_CHECK(sql_file.is_open(), "Debe poder abrir el archivo de migración");
    std::stringstream buffer;
    buffer << sql_file.rdbuf();
    std::string schema_sql = buffer.str();
    TEST_CHECK(!schema_sql.empty(), "El contenido de la migración no puede estar vacío");

    // 2. Primera sesión: abrir base, aplicar migración e insertar dato de prueba
    {
        SqliteDatabase db;
        auto open_res = db.open(db_file);
        TEST_CHECK(open_res.is_success(), "Apertura de SQLite debe ser exitosa");
        TEST_CHECK(db.is_open(), "db.is_open() debe ser true");

        MigrationRunner runner(db);
        auto mig_res = runner.apply_migration(1, "001_initial_schema.sql", schema_sql);
        TEST_CHECK(mig_res.is_success(), "Aplicación de migración debe ser exitosa");

        auto versions = runner.get_applied_versions();
        TEST_CHECK(versions.is_success(), "Consulta de versiones debe ser exitosa");
        TEST_CHECK(versions.value().size() == 1, "Debe haber 1 migración aplicada");
        TEST_CHECK(versions.value()[0] == 1, "La versión aplicada debe ser 1");

        // Verificar que NO hay usuarios por defecto (requisito estricto de seguridad)
        auto prep_users = db.prepare("SELECT COUNT(*) FROM users;");
        TEST_CHECK(prep_users.is_success(), "Consulta de usuarios debe ser exitosa");
        auto stmt_users = std::move(prep_users.value());
        TEST_CHECK(stmt_users.step().value(), "Debe retornar fila");
        TEST_CHECK(stmt_users.get_int64(0) == 0, "NO debe haber usuarios o contraseñas por defecto");

        // Insertar un dato de prueba en catálogo
        auto prep_insert = db.prepare(
            "INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);"
        );
        TEST_CHECK(prep_insert.is_success(), "Prepare de inserción debe ser exitoso");
        auto stmt_ins = std::move(prep_insert.value());
        stmt_ins.bind_text(1, "prod-test-01");
        stmt_ins.bind_text(2, "SKU-PERSIST");
        stmt_ins.bind_text(3, "750000001");
        stmt_ins.bind_text(4, "Torta Especial de Prueba");
        stmt_ins.bind_text(5, "Platillos");
        stmt_ins.bind_int64(6, 4500); // $45.00
        stmt_ins.bind_int64(7, 1);
        stmt_ins.bind_int64(8, 1);
        stmt_ins.bind_text(9, "2026-09-25 00:00:00");
        TEST_CHECK(!stmt_ins.step().value(), "Inserción debe finalizar con SQLITE_DONE");

        // db se cierra automáticamente al salir del bloque
    }

    // 3. Segunda sesión: reabrir la base y verificar que el dato sobrevive el cierre
    {
        SqliteDatabase db2;
        auto open_res = db2.open(db_file);
        TEST_CHECK(open_res.is_success(), "Reapertura de base de datos debe ser exitosa");
        TEST_CHECK(db2.is_open(), "db2 debe estar abierta");

        auto prep_query = db2.prepare("SELECT name, category, price_cents FROM products WHERE sku = ?;");
        TEST_CHECK(prep_query.is_success(), "Prepare de consulta debe ser exitoso");
        auto stmt_q = std::move(prep_query.value());
        stmt_q.bind_text(1, "SKU-PERSIST");

        auto has_row = stmt_q.step();
        TEST_CHECK(has_row.is_success(), "Step de consulta debe ser exitoso");
        TEST_CHECK(has_row.value() == true, "El registro persistido debe encontrarse tras reapertura");

        std::string name = stmt_q.get_text(0);
        std::string cat = stmt_q.get_text(1);
        std::int64_t price_cents = stmt_q.get_int64(2);

        TEST_CHECK(name == "Torta Especial de Prueba", "Nombre debe coincidir");
        TEST_CHECK(cat == "Platillos", "Categoría debe coincidir");
        TEST_CHECK(price_cents == 4500, "Precio debe coincidir en 4500 centavos ($45.00)");

        // 4. Probar violación de clave foránea (debe ser rechazada)
        auto prep_fk = db2.prepare(
            "INSERT INTO users (id, username, display_name, password_hash, role_id, is_active, created_at) "
            "VALUES ('u-invalid', 'hacker', 'Hacker', 'hash', 'rol-inexistente', 1, '2026-09-25');"
        );
        TEST_CHECK(prep_fk.is_success(), "Prepare de inserción con rol inválido");
        auto stmt_fk = std::move(prep_fk.value());
        auto step_fk = stmt_fk.step();
        TEST_CHECK(!step_fk.is_success(), "La clave foránea debe impedir insertar un rol huérfano");

        // 5. Probar restricción CHECK (precio negativo debe ser rechazado)
        auto prep_check = db2.prepare(
            "INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at) "
            "VALUES ('p-neg', 'SKU-NEG', '0000', 'Invalido', 'Test', -100, 0, 1, '2026-09-25');"
        );
        TEST_CHECK(prep_check.is_success(), "Prepare de producto inválido");
        auto stmt_check = std::move(prep_check.value());
        auto step_check = stmt_check.step();
        TEST_CHECK(!step_check.is_success(), "La restricción CHECK debe impedir precios negativos");
    }

    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }
}

void test_owner_persistence_and_auth() {
    std::cout << "[TEST] AccountService: alta de Dueño, persistencia, login y bloqueo de último Dueño..." << std::endl;

    const std::string db_file = "test_owner_auth.db";
    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }

    fs::path mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Debe encontrar directorio de migraciones");

    // Generar credenciales de prueba dinámicas en memoria (sin registrar ni hardcodear)
    const std::string owner_password = generate_random_secret(18);
    const std::string dupe_password = generate_random_secret(16);

    // Sesión 1: Crear base y dar de alta al Dueño
    {
        SqliteDatabase db;
        TEST_CHECK(db.open(db_file).is_success(), "Apertura de BD");
        MigrationRunner runner(db);
        TEST_CHECK(runner.run_pending_migrations_from_dir(mig_dir).is_success(), "Ejecución de migraciones");

        SqliteAccountService account_service(db);
        auto has_owner = account_service.has_owner();
        TEST_CHECK(has_owner.is_success() && !has_owner.value(), "Inicialmente no debe haber Dueño");

        // Alta obligatoria de cuenta Dueño con contraseña personalizada generada
        auto create_res = account_service.create_initial_owner("dueno_itch", "Ing. Director", owner_password, "director@cafeteria-itch.edu.mx");
        TEST_CHECK(create_res.is_success(), "Alta de Dueño debe ser exitosa");

        // Comprobar que ya existe dueño
        auto has_owner_now = account_service.has_owner();
        TEST_CHECK(has_owner_now.is_success() && has_owner_now.value(), "Ahora debe existir Dueño");

        // Intentar crear un segundo dueño inicial debe fallar por conflicto
        auto create_dupe = account_service.create_initial_owner("otro_dueno", "Otro", dupe_password, "otro@cafeteria-itch.edu.mx");
        TEST_CHECK(!create_dupe.is_success(), "No se debe permitir un segundo alta inicial");
        TEST_CHECK(create_dupe.error_code() == ErrorCode::Conflict, "Error debe ser Conflict");

        // db se cierra aquí
    }

    // Sesión 2: Reabrir base, verificar que la cuenta persiste y exige autenticación
    {
        SqliteDatabase db2;
        TEST_CHECK(db2.open(db_file).is_success(), "Reapertura de BD");

        auto account_service2 = std::make_shared<SqliteAccountService>(db2);
        TEST_CHECK(!account_service2->is_authenticated(), "Al reiniciar la aplicación, no debe haber sesión activa");

        auto has_owner = account_service2->has_owner();
        TEST_CHECK(has_owner.is_success() && has_owner.value(), "La cuenta Dueño debe persistir en SQLite");

        // Generar y verificar contraseña incorrecta dinámica
        std::string wrong_login_password = generate_random_secret(22);
        while (wrong_login_password == owner_password) {
            wrong_login_password = generate_random_secret(22);
        }
        auto bad_login = account_service2->login("dueno_itch", wrong_login_password);
        TEST_CHECK(!bad_login.is_success(), "Login con contraseña incorrecta debe fallar");
        TEST_CHECK(bad_login.error_code() == ErrorCode::Forbidden, "Error debe ser Forbidden");
        TEST_CHECK(!account_service2->is_authenticated(), "No debe autenticar con clave errónea");

        // Login con contraseña correcta generada dinámicamente
        auto good_login = account_service2->login("dueno_itch", owner_password);
        TEST_CHECK(good_login.is_success(), "Login con contraseña correcta debe ser exitoso");
        TEST_CHECK(account_service2->is_authenticated(), "Sesión debe estar autenticada");

        auto user = good_login.value();
        TEST_CHECK(user.username == "dueno_itch", "Nombre de usuario debe coincidir");
        TEST_CHECK(user.is_owner, "La cuenta debe tener flag is_owner = true");
        TEST_CHECK(user.role_name == "Dueño", "El rol debe ser Dueño");

        // Invariante de seguridad: El último Dueño activo NUNCA puede ser desactivado
        auto can_deact = account_service2->can_deactivate_user(user.id);
        TEST_CHECK(can_deact.is_success(), "Consulta de desactivación exitosa");
        TEST_CHECK(can_deact.value() == false, "El último Dueño activo NO puede ser desactivado");

        // Permisos: El Dueño NO tiene el permiso técnico exclusivo "backup.dev_config"
        TEST_CHECK(!account_service2->has_permission("backup.dev_config"),
                   "El Dueño NUNCA debe tener el permiso técnico backup.dev_config");

        // Prueba del servicio de respaldos con la sesión Dueño
        SqliteBackupService backup_service(db2, account_service2);
        auto dev_cfg = backup_service.get_developer_server_config();
        TEST_CHECK(!dev_cfg.is_success(), "El servicio de respaldos debe rechazar al Dueño para dev_config");
        TEST_CHECK(dev_cfg.error_code() == ErrorCode::Forbidden, "Debe retornar Forbidden");

        // Ahora probamos con identidad Desarrollador
        UserDto dev_user;
        dev_user.id = "user-dev";
        dev_user.username = "desarrollador";
        dev_user.display_name = "Desarrollador de Sistema";
        dev_user.role_id = "role-dev";
        dev_user.role_name = "Desarrollador";
        dev_user.is_active = true;
        dev_user.is_owner = false;

        account_service2->set_current_user_for_testing(dev_user);
        TEST_CHECK(account_service2->has_permission("backup.dev_config"),
                   "La identidad Desarrollador sí debe tener backup.dev_config");

        auto dev_cfg_allowed = backup_service.get_developer_server_config();
        TEST_CHECK(dev_cfg_allowed.is_success(), "Desarrollador debe poder consultar dev_config");

        // Probar instantánea real de respaldo mediante la API oficial de backup
        const std::string backup_file = "test_snapshot.bak";
        if (fs::exists(backup_file)) {
            fs::remove(backup_file);
        }
        auto bkp_res = backup_service.trigger_local_backup(backup_file);
        TEST_CHECK(bkp_res.is_success(), "Respaldo local debe ser exitoso");
        TEST_CHECK(fs::exists(backup_file), "El archivo de respaldo snapshot debe existir en disco");
        TEST_CHECK(fs::file_size(backup_file) > 0, "El archivo de respaldo snapshot no debe estar vacío");

        // Verificar que el archivo de respaldo es una base SQLite consistente y consultable
        {
            SqliteDatabase db_bkp;
            TEST_CHECK(db_bkp.open(backup_file).is_success(), "El respaldo debe ser una BD SQLite válida");
            auto prep = db_bkp.prepare("SELECT COUNT(*) FROM users;");
            TEST_CHECK(prep.is_success(), "Debe poder consultar la BD de respaldo");
            auto stmt = std::move(prep.value());
            TEST_CHECK(stmt.step().value(), "Debe retornar fila");
            TEST_CHECK(stmt.get_int64(0) == 1, "El snapshot debe contener exactamente el usuario Dueño persistido");
        }
        if (fs::exists(backup_file)) {
            fs::remove(backup_file);
        }
    }

    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }
}

void test_be02_identity_roles_permissions_and_audit() {
    std::cout << "[TEST] BE-02: Gestion de cuentas, roles configurables, permisos directos, ultimo Dueno y auditoria..." << std::endl;

    const std::string db_file = "test_be02.db";
    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }

    fs::path mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Debe encontrar el directorio de migraciones");

    // Dynamic random secrets (no hardcoded passwords)
    const std::string owner_pwd = generate_random_secret(20);
    const std::string cashier_pwd = generate_random_secret(18);
    const std::string cashier_new_pwd = generate_random_secret(18);
    const std::string supervisor_pwd = generate_random_secret(19);
    const std::string owner2_pwd = generate_random_secret(22);

    // 1. SESION 1: Aplicar todas las migraciones, verificar catalogo de permisos
    {
        SqliteDatabase db;
        TEST_CHECK(db.open(db_file).is_success(), "Apertura de SQLite");
        MigrationRunner runner(db);
        auto mig_res = runner.run_pending_migrations_from_dir(mig_dir);
        TEST_CHECK(mig_res.is_success(), "Ejecucion de todas las migraciones pendientes debe ser exitosa");

        auto applied = runner.get_applied_versions();
        TEST_CHECK(applied.is_success(), "Consulta de versiones");
        TEST_CHECK(applied.value().size() >= 2, "Debe haber al menos 2 versiones aplicadas (001 y 002)");
        TEST_CHECK(applied.value()[0] == 1, "Version 1 aplicada");
        TEST_CHECK(applied.value()[1] == 2, "Version 2 aplicada");

        SqliteAccountService svc(db);

        // 2. Permisos por llamada directa SIN sesion autenticada: deben retornar Forbidden
        TEST_CHECK(!svc.is_authenticated(), "No debe haber sesion activa");
        
        CreateUserDto dummy_u{"cajero01", "Cajero 01", cashier_pwd, "role-cashier", "cajero01@cafeteria-itch.edu.mx"};
        auto unauth_u = svc.create_user(dummy_u);
        TEST_CHECK(!unauth_u.is_success() && unauth_u.error_code() == ErrorCode::Forbidden,
                   "create_user sin autenticacion debe fallar con Forbidden");

        UpdateUserDto dummy_upd{"user-any", std::nullopt, std::nullopt, std::nullopt, false};
        auto unauth_upd = svc.update_user(dummy_upd);
        TEST_CHECK(!unauth_upd.is_success() && unauth_upd.error_code() == ErrorCode::Forbidden,
                   "update_user sin autenticacion debe fallar con Forbidden");

        auto unauth_deact = svc.deactivate_user("user-any");
        TEST_CHECK(!unauth_deact.is_success() && unauth_deact.error_code() == ErrorCode::Forbidden,
                   "deactivate_user sin autenticacion debe fallar con Forbidden");

        CreateRoleDto dummy_r{"RolTest", "Test", {"pos.sell"}};
        auto unauth_r = svc.create_role(dummy_r);
        TEST_CHECK(!unauth_r.is_success() && unauth_r.error_code() == ErrorCode::Forbidden,
                   "create_role sin autenticacion debe fallar con Forbidden");

        UpdateRoleDto dummy_rupd{"role-test", "Nuevo", "Desc", {"pos.sell"}};
        auto unauth_rupd = svc.update_role(dummy_rupd);
        TEST_CHECK(!unauth_rupd.is_success() && unauth_rupd.error_code() == ErrorCode::Forbidden,
                   "update_role sin autenticacion debe fallar con Forbidden");

        auto unauth_rdel = svc.delete_role("role-test");
        TEST_CHECK(!unauth_rdel.is_success() && unauth_rdel.error_code() == ErrorCode::Forbidden,
                   "delete_role sin autenticacion debe fallar con Forbidden");

        AuditFilterDto dummy_flt;
        auto unauth_aud = svc.list_audit_events(dummy_flt);
        TEST_CHECK(!unauth_aud.is_success() && unauth_aud.error_code() == ErrorCode::Forbidden,
                   "list_audit_events sin autenticacion debe fallar con Forbidden");

        auto unauth_perms = svc.list_permissions();
        TEST_CHECK(!unauth_perms.is_success() && unauth_perms.error_code() == ErrorCode::Forbidden,
                   "list_permissions sin autenticacion debe fallar con Forbidden");

        // 3. Alta obligatoria inicial de Dueno
        auto owner_res = svc.create_initial_owner("dueno_master", "Dueno Principal ITCH", owner_pwd, "master@cafeteria-itch.edu.mx");
        TEST_CHECK(owner_res.is_success(), "Alta inicial de Dueno exitosa");
        TEST_CHECK(svc.is_authenticated(), "Dueno debe quedar autenticado");

        auto cur_u = svc.current_user().value();
        TEST_CHECK(cur_u.is_owner, "Flag is_owner debe ser true");
        TEST_CHECK(svc.has_permission("users.manage"), "Dueno debe tener users.manage");
        TEST_CHECK(svc.has_permission("roles.manage"), "Dueno debe tener roles.manage");
        TEST_CHECK(svc.has_permission("audit.view"), "Dueno debe tener audit.view");
        TEST_CHECK(svc.has_permission("pos.sell"), "Dueno debe tener pos.sell");
        // Regla estricta de aislamiento de Desarrollador:
        TEST_CHECK(!svc.has_permission("backup.dev_config"), "Dueno NUNCA debe tener backup.dev_config");

        // 4. Dueno crea usuario Cajero
        CreateUserDto new_cashier{"cajero_juan", "Juan Cajero", cashier_pwd, "role-cashier", "juan@cafeteria-itch.edu.mx"};
        auto create_cashier_res = svc.create_user(new_cashier);
        TEST_CHECK(create_cashier_res.is_success(), "Dueno debe poder crear cuenta de Cajero");
        auto cashier_dto = create_cashier_res.value();
        TEST_CHECK(cashier_dto.username == "cajero_juan", "Username de cajero debe coincidir");
        TEST_CHECK(cashier_dto.role_id == "role-cashier", "Rol debe ser role-cashier");

        // Dueno intenta crear un usuario con rol Desarrollador (debe ser bloqueado)
        CreateUserDto dev_attempt{"hacker_dev", "Fake Dev", "passwd", "role-dev", "hacker@dev.com"};
        auto dev_attempt_res = svc.create_user(dev_attempt);
        TEST_CHECK(!dev_attempt_res.is_success() && dev_attempt_res.error_code() == ErrorCode::Forbidden,
                   "No se debe permitir crear cuentas con rol Desarrollador mediante create_user");

        // 5. Probar permisos por llamada directa como Cajero (no autorizado)
        svc.logout();
        auto cashier_login = svc.login("cajero_juan", cashier_pwd);
        TEST_CHECK(cashier_login.is_success(), "Cajero debe poder iniciar sesion");
        TEST_CHECK(svc.is_authenticated(), "Cajero esta autenticado");
        TEST_CHECK(svc.has_permission("pos.sell"), "Cajero tiene pos.sell");
        TEST_CHECK(svc.has_permission("shift.operate"), "Cajero tiene shift.operate");
        TEST_CHECK(!svc.has_permission("users.manage"), "Cajero NO tiene users.manage");
        TEST_CHECK(!svc.has_permission("roles.manage"), "Cajero NO tiene roles.manage");
        TEST_CHECK(!svc.has_permission("audit.view"), "Cajero NO tiene audit.view");
        TEST_CHECK(!svc.has_permission("backup.dev_config"), "Cajero NO tiene backup.dev_config");

        // Llamadas directas de cajero a metodos protegidos
        auto cashier_create_u = svc.create_user(dummy_u);
        TEST_CHECK(!cashier_create_u.is_success() && cashier_create_u.error_code() == ErrorCode::Forbidden,
                   "Cajero no puede crear usuarios");

        auto cashier_deact = svc.deactivate_user(cur_u.id);
        TEST_CHECK(!cashier_deact.is_success() && cashier_deact.error_code() == ErrorCode::Forbidden,
                   "Cajero no puede desactivar al Dueno");

        auto cashier_role_create = svc.create_role(dummy_r);
        TEST_CHECK(!cashier_role_create.is_success() && cashier_role_create.error_code() == ErrorCode::Forbidden,
                   "Cajero no puede crear roles");

        auto cashier_aud = svc.list_audit_events(dummy_flt);
        TEST_CHECK(!cashier_aud.is_success() && cashier_aud.error_code() == ErrorCode::Forbidden,
                   "Cajero no puede consultar auditoria");

        // Cajero intentando cambiar contrasena del Dueno -> Forbidden
        auto cashier_hack_pwd = svc.change_password(cur_u.id, "hack12345");
        TEST_CHECK(!cashier_hack_pwd.is_success() && cashier_hack_pwd.error_code() == ErrorCode::Forbidden,
                   "Cajero no puede cambiar la contrasena del Dueno");

        // Cajero cambiando su PROPIA contrasena -> Exitoso
        auto cashier_self_pwd = svc.change_password(cashier_dto.id, cashier_new_pwd);
        TEST_CHECK(cashier_self_pwd.is_success(), "Cajero debe poder cambiar su propia contrasena");

        // Verificar cambio de contrasena en login
        svc.logout();
        auto old_pwd_login = svc.login("cajero_juan", cashier_pwd);
        TEST_CHECK(!old_pwd_login.is_success() && old_pwd_login.error_code() == ErrorCode::Forbidden,
                   "Login con contrasena anterior del cajero debe fallar");
        auto new_pwd_login = svc.login("cajero_juan", cashier_new_pwd);
        TEST_CHECK(new_pwd_login.is_success(), "Login con nueva contrasena del cajero debe ser exitoso");
        svc.logout();

        // 6. Iniciar sesion como Dueno y probar gestion de roles configurables
        auto owner_login = svc.login("dueno_master", owner_pwd);
        TEST_CHECK(owner_login.is_success(), "Dueno debe poder autenticarse");

        // Consultar catalogo maestro de permisos
        auto perm_list = svc.list_permissions();
        TEST_CHECK(perm_list.is_success(), "Dueno puede consultar lista de permisos");
        TEST_CHECK(perm_list.value().size() >= 20, "Debe haber al menos 20 permisos en catalogo maestro");

        // Dueno intenta crear un rol que incluya backup.dev_config -> RECHAZO OBLIGATORIO
        CreateRoleDto malicious_role{"RolPeligroso", "Intento de acceso tecnico", {"pos.sell", "backup.dev_config"}};
        auto mal_role_res = svc.create_role(malicious_role);
        TEST_CHECK(!mal_role_res.is_success() && mal_role_res.error_code() == ErrorCode::Forbidden,
                   "Ningun rol configurable puede tener el permiso backup.dev_config");

        // Crear un rol configurable valido: "Supervisor"
        CreateRoleDto supervisor_role{
            "Supervisor",
            "Supervisor de almacen y reportes",
            {"inventory.view", "inventory.manage_products", "reports.view"}
        };
        auto sup_res = svc.create_role(supervisor_role);
        TEST_CHECK(sup_res.is_success(), "Creacion de rol Supervisor debe ser exitosa");
        auto sup_dto = sup_res.value();
        TEST_CHECK(sup_dto.name == "Supervisor", "Nombre de rol");
        TEST_CHECK(sup_dto.permissions.size() == 3, "Debe tener 3 permisos asignados");

        // Crear usuario con rol Supervisor
        CreateUserDto sup_user{"super_ana", "Ana Supervisora", supervisor_pwd, sup_dto.id, "ana@cafeteria-itch.edu.mx"};
        auto create_sup_user = svc.create_user(sup_user);
        TEST_CHECK(create_sup_user.is_success(), "Creacion de usuario Supervisor");
        auto ana_dto = create_sup_user.value();

        // Intentar eliminar un rol que TIENE USUARIOS asignados -> debe fallar con Conflict
        auto del_active_role = svc.delete_role(sup_dto.id);
        TEST_CHECK(!del_active_role.is_success() && del_active_role.error_code() == ErrorCode::Conflict,
                   "No se debe permitir eliminar un rol con usuarios asignados");

        // Intentar eliminar roles de sistema -> debe fallar con Forbidden
        TEST_CHECK(!svc.delete_role("role-cashier").is_success(), "No se puede eliminar role-cashier");
        TEST_CHECK(!svc.delete_role("role-owner").is_success(), "No se puede eliminar role-owner");
        TEST_CHECK(!svc.delete_role("role-dev").is_success(), "No se puede eliminar role-dev");

        // Intentar modificar role-dev -> debe fallar con Forbidden
        UpdateRoleDto upd_dev{"role-dev", "Nuevo Dev", "Desc", {"backup.dev_config"}};
        TEST_CHECK(!svc.update_role(upd_dev).is_success(), "No se puede modificar role-dev");

        // Intentar modificar role-owner -> debe fallar con Forbidden
        UpdateRoleDto upd_own{"role-owner", "Dueno Alterado", "Desc", {"pos.sell"}};
        TEST_CHECK(!svc.update_role(upd_own).is_success(), "No se puede alterar el rol Dueno");

        // Actualizar rol Supervisor agregando "audit.view"
        UpdateRoleDto sup_upd{
            sup_dto.id,
            "Supervisor General",
            "Supervision con acceso a bitacora",
            {"inventory.view", "inventory.manage_products", "reports.view", "audit.view"}
        };
        auto upd_sup_res = svc.update_role(sup_upd);
        TEST_CHECK(upd_sup_res.is_success(), "Actualizacion de rol Supervisor debe ser exitosa");

        // Iniciar sesion con Ana (Supervisor) y validar permisos efectivos dinamicos
        svc.logout();
        auto ana_login = svc.login("super_ana", supervisor_pwd);
        TEST_CHECK(ana_login.is_success(), "Login de Supervisora exitoso");
        TEST_CHECK(svc.has_permission("inventory.view"), "Ana tiene inventory.view");
        TEST_CHECK(svc.has_permission("inventory.manage_products"), "Ana tiene inventory.manage_products");
        TEST_CHECK(svc.has_permission("reports.view"), "Ana tiene reports.view");
        TEST_CHECK(svc.has_permission("audit.view"), "Ana tiene audit.view (otorgado dinamicamente)");
        TEST_CHECK(!svc.has_permission("users.manage"), "Ana NO tiene users.manage");
        TEST_CHECK(!svc.has_permission("backup.dev_config"), "Ana NO tiene backup.dev_config");
        svc.logout();

        // 7. PROTECCION TRANSACCIONAL DE LA ULTIMA CUENTA DUENO ACTIVA
        svc.login("dueno_master", owner_pwd);

        // a) Con 1 solo dueno activo:
        auto can_deact_single = svc.can_deactivate_user(cur_u.id);
        TEST_CHECK(can_deact_single.is_success() && can_deact_single.value() == false,
                   "can_deactivate_user debe retornar false para el unico Dueno activo");

        auto deact_attempt = svc.deactivate_user(cur_u.id);
        TEST_CHECK(!deact_attempt.is_success() && deact_attempt.error_code() == ErrorCode::Conflict,
                   "deactivate_user debe fallar con Conflict al intentar desactivar al unico Dueno");

        UpdateUserDto demote_attempt;
        demote_attempt.user_id = cur_u.id;
        demote_attempt.role_id = "role-cashier";
        auto demote_res = svc.update_user(demote_attempt);
        TEST_CHECK(!demote_res.is_success() && demote_res.error_code() == ErrorCode::Conflict,
                   "update_user debe fallar con Conflict al intentar quitar el rol al unico Dueno");

        // Verificar que el Dueno sigue intacto
        auto check_cur_owner = svc.get_user_by_id(cur_u.id).value();
        TEST_CHECK(check_cur_owner.is_active, "El dueno debe seguir activo");
        TEST_CHECK(check_cur_owner.role_id == "role-owner", "El dueno debe seguir teniendo role-owner");

        // b) Dar de alta a un segundo Dueno
        CreateUserDto second_owner{"dueno_secundario", "Segundo Propietario", owner2_pwd, "role-owner", "dueno2@cafeteria-itch.edu.mx"};
        auto create_own2 = svc.create_user(second_owner);
        TEST_CHECK(create_own2.is_success(), "Creacion de segundo Dueno debe ser exitosa");
        auto own2_dto = create_own2.value();

        // Ahora hay 2 duenos activos: can_deactivate_user debe permitir desactivar a dueno_master
        auto can_deact_now = svc.can_deactivate_user(cur_u.id);
        TEST_CHECK(can_deact_now.is_success() && can_deact_now.value() == true,
                   "can_deactivate_user debe retornar true al haber 2 duenos activos");

        auto deact_first_owner = svc.deactivate_user(cur_u.id);
        TEST_CHECK(deact_first_owner.is_success(), "Desactivar al primer dueno debe ser exitoso al quedar otro activo");

        // Ahora solo queda dueno_secundario activo: intentar desactivarlo o demotarlo DEBE FALLAR
        auto can_deact_last = svc.can_deactivate_user(own2_dto.id);
        TEST_CHECK(can_deact_last.is_success() && can_deact_last.value() == false,
                   "El segundo dueno es ahora el ultimo activo, can_deactivate debe ser false");

        auto deact_own2 = svc.deactivate_user(own2_dto.id);
        TEST_CHECK(!deact_own2.is_success() && deact_own2.error_code() == ErrorCode::Conflict,
                   "No se puede desactivar al ultimo dueno restante");

        UpdateUserDto demote_own2;
        demote_own2.user_id = own2_dto.id;
        demote_own2.role_id = "role-cashier";
        auto demote_own2_res = svc.update_user(demote_own2);
        TEST_CHECK(!demote_own2_res.is_success() && demote_own2_res.error_code() == ErrorCode::Conflict,
                   "No se puede cambiar el rol al ultimo dueno restante");

        // Reactivar al primer dueno para que ambas cuentas queden activas
        UpdateUserDto reactivate_own1;
        reactivate_own1.user_id = cur_u.id;
        reactivate_own1.is_active = true;
        TEST_CHECK(svc.update_user(reactivate_own1).is_success(), "Reactivacion de primer dueno exitosa");

        // 8. AUDITORIA: Verificacion de eventos registrados e inviolabilidad de secretos
        AuditFilterDto all_filter;
        all_filter.limit = 1000;
        auto audit_list = svc.list_audit_events(all_filter);
        TEST_CHECK(audit_list.is_success(), "Consulta de eventos de auditoria debe ser exitosa");
        const auto& events = audit_list.value();
        TEST_CHECK(events.size() >= 8, "Debe haber multiples eventos de auditoria registrados");

        bool found_owner_create = false;
        bool found_user_create = false;
        bool found_user_update = false;
        bool found_role_create = false;
        bool found_role_update = false;
        bool found_login_success = false;
        bool found_pwd_change = false;

        for (const auto& evt : events) {
            if (evt.event_type == "OWNER_CREATED") found_owner_create = true;
            if (evt.event_type == "USER_CREATED") found_user_create = true;
            if (evt.event_type == "USER_UPDATED") found_user_update = true;
            if (evt.event_type == "ROLE_CREATED") found_role_create = true;
            if (evt.event_type == "ROLE_UPDATED") found_role_update = true;
            if (evt.event_type == "LOGIN_SUCCESS") found_login_success = true;
            if (evt.event_type == "PASSWORD_CHANGED") found_pwd_change = true;

            // COMPROBACION ESTRICTA: Ningun detalle de auditoria debe contener contrasenas
            TEST_CHECK(evt.details.find(owner_pwd) == std::string::npos, "Auditoria no debe contener owner_pwd");
            TEST_CHECK(evt.details.find(cashier_pwd) == std::string::npos, "Auditoria no debe contener cashier_pwd");
            TEST_CHECK(evt.details.find(cashier_new_pwd) == std::string::npos, "Auditoria no debe contener cashier_new_pwd");
            TEST_CHECK(evt.details.find(supervisor_pwd) == std::string::npos, "Auditoria no debe contener supervisor_pwd");
            TEST_CHECK(evt.details.find(owner2_pwd) == std::string::npos, "Auditoria no debe contener owner2_pwd");
            TEST_CHECK(evt.details.find("pbkdf2_sha256$") == std::string::npos, "Auditoria no debe contener password hashes");
        }

        TEST_CHECK(found_owner_create, "Evento OWNER_CREATED registrado");
        TEST_CHECK(found_user_create, "Evento USER_CREATED registrado");
        TEST_CHECK(found_user_update, "Evento USER_UPDATED registrado");
        TEST_CHECK(found_role_create, "Evento ROLE_CREATED registrado");
        TEST_CHECK(found_role_update, "Evento ROLE_UPDATED registrado");
        TEST_CHECK(found_login_success, "Evento LOGIN_SUCCESS registrado");
        TEST_CHECK(found_pwd_change, "Evento PASSWORD_CHANGED registrado");

        // Probar filtrado por tipo de evento
        AuditFilterDto filter_pwd;
        filter_pwd.event_type = "PASSWORD_CHANGED";
        auto pwd_events = svc.list_audit_events(filter_pwd);
        TEST_CHECK(pwd_events.is_success(), "Filtro de auditoria por tipo de evento exitoso");
        for (const auto& e : pwd_events.value()) {
            TEST_CHECK(e.event_type == "PASSWORD_CHANGED", "Todos los resultados deben ser PASSWORD_CHANGED");
        }
    }

    // 9. PERSISTENCIA TRAS REAPERTURA DE BASE DE DATOS
    {
        SqliteDatabase db2;
        TEST_CHECK(db2.open(db_file).is_success(), "Reapertura de base de datos BE-02");
        SqliteAccountService svc2(db2);

        TEST_CHECK(!svc2.is_authenticated(), "No debe haber sesion activa tras reinicio");

        // El segundo dueno puede iniciar sesion con sus credenciales tras el reinicio
        auto own2_reopen_login = svc2.login("dueno_secundario", owner2_pwd);
        TEST_CHECK(own2_reopen_login.is_success(), "Segundo dueno debe poder autenticarse tras reapertura");
        TEST_CHECK(svc2.is_authenticated(), "Sesion autenticada");

        // El rol configurable "Supervisor" debe persistir con sus permisos
        auto roles_list = svc2.list_roles();
        TEST_CHECK(roles_list.is_success(), "Consulta de roles tras reapertura");
        bool supervisor_found = false;
        for (const auto& r : roles_list.value()) {
            if (r.name == "Supervisor General") {
                supervisor_found = true;
                TEST_CHECK(r.permissions.size() == 4, "Supervisor General debe conservar sus 4 permisos");
            }
        }
        TEST_CHECK(supervisor_found, "El rol Supervisor General debe persistir en SQLite tras reapertura");

        // El cajero Juan debe poder autenticarse con su contrasena actualizada
        svc2.logout();
        auto cashier_reopen_login = svc2.login("cajero_juan", cashier_new_pwd);
        TEST_CHECK(cashier_reopen_login.is_success(), "Cajero debe poder autenticarse con su contrasena cambiada");

        // Las cuentas de usuarios deben persistir
        svc2.logout();
        svc2.login("dueno_master", owner_pwd);
        auto users = svc2.list_users().value();
        TEST_CHECK(users.size() >= 4, "Deben persistir los 4 usuarios (2 duenos, cajero, supervisora)");
    }

    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }
}

void test_be03_catalog_ingredients_and_recipes() {
    std::cout << "\n--- TEST BE-03: Catalogo, Ingredientes, Recetas, Unidades y Costos Proporcionales ---" << std::endl;

    std::string unique_suffix = generate_random_alphanumeric(8);
    fs::path db_file = fs::current_path() / ("test_be03_cat_" + unique_suffix + ".db");
    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }

    auto mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Directorio de migraciones debe existir para BE-03");

    std::string owner_pwd = generate_random_secret(18);
    std::string cashier_pwd = generate_random_secret(16);
    std::string created_prod_id;
    std::string prepared_prod_id;
    std::string ing_limon_id;
    std::string ing_leche_id;
    std::string ing_pan_id;

    // 1. Configuración inicial y autenticación como Dueño
    {
        SqliteDatabase db;
        TEST_CHECK(db.open(db_file).is_success(), "Apertura de BD para BE-03");
        MigrationRunner runner(db);
        auto mig_res = runner.run_pending_migrations_from_dir(mig_dir);
        TEST_CHECK(mig_res.is_success(), "Ejecucion de migraciones para BE-03");

        auto account_svc = std::make_shared<SqliteAccountService>(db);
        auto create_own = account_svc->create_initial_owner("owner_cat", "Dueno Catalogo", owner_pwd, "owner_cat@cafeteria-itch.edu.mx");
        TEST_CHECK(create_own.is_success(), "Creacion del Dueno para BE-03");

        SqliteCatalogService catalog_svc(db, account_svc);
        SqliteInventoryService inventory_svc(db, account_svc);

        // 2. CRUD de Productos y Eliminación Lógica
        CreateProductDto prod_in;
        prod_in.sku = "SKU-BEV-101";
        prod_in.barcode = "750123456001";
        prod_in.name = "Chocolaleche Fria";
        prod_in.category = "Bebidas";
        prod_in.price = Money::from_major_minor(35, 0);
        prod_in.is_prepared = false;

        auto prod_res = catalog_svc.create_product(prod_in);
        TEST_CHECK(prod_res.is_success(), "Creacion de producto regular");
        created_prod_id = prod_res.value().id;
        TEST_CHECK(!created_prod_id.empty(), "ID generado de producto no vacio");
        TEST_CHECK(prod_res.value().price.cents() == 3500, "Precio en centavos debe ser 3500");
        TEST_CHECK(prod_res.value().is_active, "Producto debe iniciar activo");
        TEST_CHECK(!prod_res.value().has_image, "Producto no debe tener imagen aun");

        // Conflicto de SKU duplicado
        CreateProductDto dup_sku = prod_in;
        dup_sku.barcode = "750999999999";
        auto dup_sku_res = catalog_svc.create_product(dup_sku);
        TEST_CHECK(!dup_sku_res.is_success(), "No se debe permitir SKU duplicado");
        TEST_CHECK(dup_sku_res.error_code() == ErrorCode::Conflict, "Error debe ser Conflict por SKU duplicado");

        // Conflicto de Código de Barras duplicado
        CreateProductDto dup_bc = prod_in;
        dup_bc.sku = "SKU-DIFF-001";
        auto dup_bc_res = catalog_svc.create_product(dup_bc);
        TEST_CHECK(!dup_bc_res.is_success(), "No se debe permitir codigo de barras duplicado");
        TEST_CHECK(dup_bc_res.error_code() == ErrorCode::Conflict, "Error debe ser Conflict por codigo de barras");

        // Actualización de producto
        UpdateProductDto upd_in;
        upd_in.product_id = created_prod_id;
        upd_in.name = "Chocolaleche Especial Fria";
        upd_in.price = Money::from_major_minor(38, 50);
        auto upd_res = catalog_svc.update_product(upd_in);
        TEST_CHECK(upd_res.is_success(), "Actualizacion de producto");

        auto get_prod = catalog_svc.get_product_by_id(created_prod_id);
        TEST_CHECK(get_prod.is_success(), "Obtener producto por ID");
        TEST_CHECK(get_prod.value().name == "Chocolaleche Especial Fria", "Nombre actualizado");
        TEST_CHECK(get_prod.value().price.cents() == 3850, "Precio actualizado a 3850 centavos");

        // Rechazo de precio negativo
        UpdateProductDto upd_neg = upd_in;
        upd_neg.price = Money(-100);
        TEST_CHECK(!catalog_svc.update_product(upd_neg).is_success(), "Rechazar precio negativo");

        // Búsqueda y filtros
        auto search_res = catalog_svc.search_products("Chocolaleche", "Bebidas", 0, 50);
        TEST_CHECK(search_res.is_success() && search_res.value().size() == 1, "Busqueda parcial y por categoria");

        auto search_diff_cat = catalog_svc.search_products("Chocolaleche", "Snacks", 0, 50);
        TEST_CHECK(search_diff_cat.is_success() && search_diff_cat.value().empty(), "Filtro de categoria excluyente");

        // Búsqueda por SKU y Código de barras
        auto get_by_sku = catalog_svc.get_product_by_sku_or_barcode("SKU-BEV-101");
        TEST_CHECK(get_by_sku.is_success() && get_by_sku.value().id == created_prod_id, "Buscar por SKU");
        auto get_by_bc = catalog_svc.get_product_by_sku_or_barcode("750123456001");
        TEST_CHECK(get_by_bc.is_success() && get_by_bc.value().id == created_prod_id, "Buscar por Codigo de barras");

        // Baja lógica y reactivación
        auto deact_res = catalog_svc.deactivate_product(created_prod_id);
        TEST_CHECK(deact_res.is_success(), "Desactivacion (baja logica) de producto");

        auto search_active_only = catalog_svc.search_products("", std::nullopt, 0, 50, false);
        TEST_CHECK(search_active_only.is_success() && search_active_only.value().empty(), "Producto desactivado no aparece en busqueda normal");

        auto search_inc_inactive = catalog_svc.search_products("", std::nullopt, 0, 50, true);
        TEST_CHECK(search_inc_inactive.is_success() && search_inc_inactive.value().size() == 1, "Producto desactivado aparece con include_inactive=true");
        TEST_CHECK(!search_inc_inactive.value()[0].is_active, "is_active debe ser falso");

        auto react_res = catalog_svc.reactivate_product(created_prod_id);
        TEST_CHECK(react_res.is_success(), "Reactivacion de producto");
        auto search_reactivated = catalog_svc.search_products("", std::nullopt, 0, 50, false);
        TEST_CHECK(search_reactivated.is_success() && search_reactivated.value().size() == 1, "Producto reactivado vuelve a busqueda normal");

        // 3. Imágenes de Producto (Validación mágica y límite de 100 KB)
        // Imagen que supera el límite de 100 KB (102,400 bytes)
        std::vector<std::uint8_t> huge_img(105 * 1024, 0x00);
        auto img_huge_res = catalog_svc.set_product_image(created_prod_id, "image/jpeg", huge_img);
        TEST_CHECK(!img_huge_res.is_success(), "Rechazar imagen mayor a 100 KB");
        TEST_CHECK(img_huge_res.error_code() == ErrorCode::InvalidInput, "Error debe ser InvalidInput");

        // Imagen con bytes mágicos inválidos
        std::vector<std::uint8_t> invalid_img(512, 0xAA);
        auto img_inv_res = catalog_svc.set_product_image(created_prod_id, "image/jpeg", invalid_img);
        TEST_CHECK(!img_inv_res.is_success(), "Rechazar imagen con cabecera no valida");

        // Imagen JPEG válida (magic bytes: 0xFF, 0xD8, 0xFF)
        std::vector<std::uint8_t> valid_jpeg = {0xFF, 0xD8, 0xFF, 0xE0, 0x00, 0x10, 'J', 'F', 'I', 'F', 0x00, 0x01};
        valid_jpeg.resize(2048, 0x12);
        auto img_set_res = catalog_svc.set_product_image(created_prod_id, "image/jpeg", valid_jpeg);
        TEST_CHECK(img_set_res.is_success(), "Guardar imagen JPEG valida");

        auto img_get_res = catalog_svc.get_product_image(created_prod_id);
        TEST_CHECK(img_get_res.is_success(), "Recuperar imagen guardada");
        TEST_CHECK(img_get_res.value().mime_type == "image/jpeg", "MIME type detectado correctamente como image/jpeg");
        TEST_CHECK(img_get_res.value().image_blob == valid_jpeg, "BLOB de imagen debe coincidir byte por byte");

        auto prod_with_img = catalog_svc.get_product_by_id(created_prod_id);
        TEST_CHECK(prod_with_img.value().has_image, "has_image debe ser verdadero tras asignar imagen");

        // Imagen PNG válida (magic bytes: 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A)
        std::vector<std::uint8_t> valid_png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A};
        valid_png.resize(1024, 0x34);
        auto img_png_res = catalog_svc.set_product_image(created_prod_id, "image/png", valid_png);
        TEST_CHECK(img_png_res.is_success(), "Reemplazar por imagen PNG valida");
        auto img_png_get = catalog_svc.get_product_image(created_prod_id);
        TEST_CHECK(img_png_get.value().mime_type == "image/png", "MIME type detectado como image/png");

        // Eliminar imagen
        auto img_del_res = catalog_svc.remove_product_image(created_prod_id);
        TEST_CHECK(img_del_res.is_success(), "Eliminar imagen de producto");
        TEST_CHECK(!catalog_svc.get_product_image(created_prod_id).is_success(), "Imagen eliminada no se puede consultar");
        TEST_CHECK(!catalog_svc.get_product_by_id(created_prod_id).value().has_image, "has_image es falso tras eliminar");

        // 4. Ingredientes, Unidades Base e Integridad Dimensional
        CreateIngredientDto ing_limon_in;
        ing_limon_in.name = "Limon con cascara";
        ing_limon_in.unit_dimension = "MASS";
        ing_limon_in.base_unit = "mg";
        ing_limon_in.low_stock_threshold_base = 1000000; // 1 kg = 1,000,000 mg

        auto ing_limon_res = inventory_svc.create_ingredient(ing_limon_in);
        TEST_CHECK(ing_limon_res.is_success(), "Creacion de ingrediente con dimension MASS");
        ing_limon_id = ing_limon_res.value().id;
        TEST_CHECK(ing_limon_res.value().current_stock_base == 0, "Stock inicial debe ser 0");

        // Rechazo de dimensión inválida
        CreateIngredientDto ing_inv = ing_limon_in;
        ing_inv.name = "Ingrediente Invalido";
        ing_inv.unit_dimension = "ENERGY";
        TEST_CHECK(!inventory_svc.create_ingredient(ing_inv).is_success(), "Rechazo de dimension inexistente");

        // Rechazo de unidad base incompatible con dimensión (MASS con ml)
        CreateIngredientDto ing_mismatch = ing_limon_in;
        ing_mismatch.name = "Ingrediente Incompatible";
        ing_mismatch.base_unit = "ml";
        TEST_CHECK(!inventory_svc.create_ingredient(ing_mismatch).is_success(), "Rechazo de unidad base no canónica");

        // Creación de ingrediente VOLUME
        CreateIngredientDto ing_leche_in;
        ing_leche_in.name = "Leche entera pasteurizada";
        ing_leche_in.unit_dimension = "VOLUME";
        ing_leche_in.base_unit = "ml";
        ing_leche_in.low_stock_threshold_base = 2000; // 2 L = 2,000 ml
        auto ing_leche_res = inventory_svc.create_ingredient(ing_leche_in);
        TEST_CHECK(ing_leche_res.is_success(), "Creacion de ingrediente VOLUME");
        ing_leche_id = ing_leche_res.value().id;

        // Creación de ingrediente COUNT
        CreateIngredientDto ing_pan_in;
        ing_pan_in.name = "Pan bolillo para torta";
        ing_pan_in.unit_dimension = "COUNT";
        ing_pan_in.base_unit = "pz";
        ing_pan_in.low_stock_threshold_base = 20;
        auto ing_pan_res = inventory_svc.create_ingredient(ing_pan_in);
        TEST_CHECK(ing_pan_res.is_success(), "Creacion de ingrediente COUNT");
        ing_pan_id = ing_pan_res.value().id;

        // Rechazo de nombre duplicado de ingrediente
        TEST_CHECK(!inventory_svc.create_ingredient(ing_limon_in).is_success(), "Rechazo de nombre duplicado de ingrediente");

        // 5. Umbrales de Bajo Stock y Alertas
        // Con stock 0, todos deben generar alerta porque 0 <= umbral
        auto alerts_initial = inventory_svc.get_stock_alerts();
        TEST_CHECK(alerts_initial.is_success() && alerts_initial.value().size() == 3, "Los 3 ingredientes deben generar alerta de stock bajo con stock 0");

        // Modificar umbral de pan a 10 pz
        auto thresh_upd = inventory_svc.update_low_stock_threshold(ing_pan_id, 10);
        TEST_CHECK(thresh_upd.is_success(), "Actualizar umbral de bajo stock");
        auto ing_pan_chk = inventory_svc.get_ingredient(ing_pan_id);
        TEST_CHECK(ing_pan_chk.value().low_stock_threshold_base == 10, "Umbral actualizado verificado");

        // Rechazo de umbral negativo
        TEST_CHECK(!inventory_svc.update_low_stock_threshold(ing_pan_id, -5).is_success(), "Rechazar umbral negativo");

        // 6. Entradas de Compra y Conversión de Unidades
        // Intentar registrar compra con unidad incompatible (limón en litros)
        auto pe_inc = inventory_svc.register_purchase_entry(ing_limon_id, 5, "litros", Money::from_major_minor(250, 0));
        TEST_CHECK(!pe_inc.is_success(), "Rechazar compra con unidad dimensionalmente incompatible");

        // Compra válida de limón: 5 kg por $250.00 (25,000 centavos)
        // Base units: 5 kg = 5,000,000 mg
        auto pe_limon = inventory_svc.register_purchase_entry(ing_limon_id, 5, "kg", Money::from_major_minor(250, 0));
        TEST_CHECK(pe_limon.is_success(), "Registro de compra de limon en kg");

        auto ing_limon_after = inventory_svc.get_ingredient(ing_limon_id).value();
        TEST_CHECK(ing_limon_after.current_stock_base == 5000000, "Stock de limon debe ser 5,000,000 mg");

        // Compra de leche: 10 L por $260.00 (26,000 centavos)
        // Base units: 10 L = 10,000 ml
        auto pe_leche = inventory_svc.register_purchase_entry(ing_leche_id, 10, "L", Money::from_major_minor(260, 0));
        TEST_CHECK(pe_leche.is_success(), "Registro de compra de leche en L");
        auto ing_leche_after = inventory_svc.get_ingredient(ing_leche_id).value();
        TEST_CHECK(ing_leche_after.current_stock_base == 10000, "Stock de leche debe ser 10,000 ml");

        // Compra de pan: 50 piezas por $150.00
        auto pe_pan = inventory_svc.register_purchase_entry(ing_pan_id, 50, "pz", Money::from_major_minor(150, 0));
        TEST_CHECK(pe_pan.is_success(), "Registro de compra de pan en pz");
        auto ing_pan_after = inventory_svc.get_ingredient(ing_pan_id).value();
        TEST_CHECK(ing_pan_after.current_stock_base == 50, "Stock de pan debe ser 50 pz");

        // Ahora el stock de pan es 50 > umbral 10, así que no debe figurar en alertas
        auto alerts_after = inventory_svc.get_stock_alerts().value();
        bool pan_in_alerts = false;
        for (const auto& a : alerts_after) {
            if (a.ingredient_id == ing_pan_id) pan_in_alerts = true;
        }
        TEST_CHECK(!pan_in_alerts, "Pan no debe figurar en alertas tras reabastecimiento");

        // 7. Platillos Preparados, Recetas y Cálculo de Costo Proporcional (Regla AGENTS.md)
        CreateProductDto prep_prod_in;
        prep_prod_in.sku = "SKU-PREP-201";
        prep_prod_in.name = "Limonada Suiza Preparada";
        prep_prod_in.category = "Bebidas";
        prep_prod_in.price = Money::from_major_minor(45, 0);
        prep_prod_in.is_prepared = true;
        auto prep_prod_res = catalog_svc.create_product(prep_prod_in);
        TEST_CHECK(prep_prod_res.is_success(), "Creacion de producto preparado");
        prepared_prod_id = prep_prod_res.value().id;

        // Validación de receta: receta vacía rechazada
        TEST_CHECK(!catalog_svc.set_recipe(prepared_prod_id, {}).is_success(), "Rechazo de receta sin ingredientes");

        // Validación de receta: cantidad negativa o cero rechazada
        std::vector<RecipeItemDto> invalid_recipe = {
            {ing_limon_id, "Limon", 0, "mg"}
        };
        TEST_CHECK(!catalog_svc.set_recipe(prepared_prod_id, invalid_recipe).is_success(), "Rechazo de ingrediente con cantidad <= 0");

        // Receta válida:
        // - 200 g de limón = 200,000 mg
        // - 250 ml de leche = 250 ml
        std::vector<RecipeItemDto> valid_recipe = {
            {ing_limon_id, "Limon con cascara", 200000, "mg"},
            {ing_leche_id, "Leche entera pasteurizada", 250, "ml"}
        };
        auto set_rcp_res = catalog_svc.set_recipe(prepared_prod_id, valid_recipe);
        TEST_CHECK(set_rcp_res.is_success(), "Configuracion de receta valida");

        // Consulta de receta y cálculo de costo estimado por porción
        // Limón: 5,000,000 mg por 25,000 centavos -> para 200,000 mg = 25000 * 200000 / 5000000 = 1000 centavos ($10.00)
        // Leche: 10,000 ml por 26,000 centavos -> para 250 ml = 26000 * 250 / 10000 = 650 centavos ($6.50)
        // Costo por porción estimado total = 1000 + 650 = 1650 centavos ($16.50)
        auto rcp_res = catalog_svc.get_recipe(prepared_prod_id);
        TEST_CHECK(rcp_res.is_success(), "Consulta de receta");
        TEST_CHECK(rcp_res.value().items.size() == 2, "Receta debe tener 2 ingredientes");
        TEST_CHECK(rcp_res.value().estimated_cost_per_portion.cents() == 1650, "Costo estimado por porcion debe ser exactamente $16.50 (1650 centavos)");

        // Comprobación de existencias calculadas para producto preparado:
        // Stock Limón = 5,000,000 mg / 200,000 mg = 25 porciones
        // Stock Leche = 10,000 ml / 250 ml = 40 porciones
        // Porciones disponibles calculadas = min(25, 40) = 25
        auto search_prep = catalog_svc.search_products("Limonada Suiza", "Bebidas", 0, 10);
        TEST_CHECK(search_prep.is_success() && search_prep.value().size() == 1, "Busqueda de producto preparado");
        TEST_CHECK(search_prep.value()[0].stock_units == 25, "Existencias calculadas por receta deben ser 25 porciones");

        // Caso obligatorio de AGENTS.md:
        // "Ejemplo obligatorio: 1 kg de limón por $50 conserva un costo no nulo por mg y produce $0.05 por g."
        CreateIngredientDto limon_test_in;
        limon_test_in.name = "Limon Test Regla AGENTS";
        limon_test_in.unit_dimension = "MASS";
        limon_test_in.base_unit = "mg";
        auto ing_limon_test = inventory_svc.create_ingredient(limon_test_in).value();

        // Compra: 1 kg (1,000,000 mg) por $50.00 (5,000 centavos)
        auto pe_rule = inventory_svc.register_purchase_entry(ing_limon_test.id, 1, "kg", Money::from_major_minor(50, 0));
        TEST_CHECK(pe_rule.is_success(), "Compra de 1 kg de limon por $50");

        CreateProductDto prod_rule_in;
        prod_rule_in.sku = "SKU-RULE-TEST";
        prod_rule_in.name = "Platillo Test Regla AGENTS";
        prod_rule_in.category = "Platillos";
        prod_rule_in.price = Money::from_major_minor(20, 0);
        prod_rule_in.is_prepared = true;
        auto prod_rule = catalog_svc.create_product(prod_rule_in).value();

        // Receta con 1 g (1,000 mg)
        std::vector<RecipeItemDto> rcp_rule = {
            {ing_limon_test.id, ing_limon_test.name, 1000, "mg"}
        };
        catalog_svc.set_recipe(prod_rule.id, rcp_rule);
        auto rcp_rule_get = catalog_svc.get_recipe(prod_rule.id).value();
        TEST_CHECK(rcp_rule_get.estimated_cost_per_portion.cents() == 5, "1 g de limon a $50/kg DEBE producir exactamente 5 centavos ($0.05) y NO 0");

        // 8. Verificación de Permisos Granulares
        // Crear usuario Cajero
        CreateUserDto cashier_in;
        cashier_in.username = "cajero_pos";
        cashier_in.display_name = "Cajero POS";
        cashier_in.email = "cajero_pos@cafeteria-itch.edu.mx";
        cashier_in.raw_password = cashier_pwd;
        cashier_in.role_id = "role-cashier";
        auto create_cashier = account_svc->create_user(cashier_in);
        TEST_CHECK(create_cashier.is_success(), "Creacion de usuario cajero");

        // Cambiar sesión a Cajero
        account_svc->logout();
        auto cashier_login = account_svc->login("cajero_pos", cashier_pwd);
        TEST_CHECK(cashier_login.is_success(), "Login de cajero");

        // Cajero no debe poder crear productos
        CreateProductDto cajero_prod_in = prod_in;
        cajero_prod_in.sku = "SKU-UNAUTH";
        auto cajero_create = catalog_svc.create_product(cajero_prod_in);
        TEST_CHECK(!cajero_create.is_success(), "Cajero no debe poder crear productos");
        TEST_CHECK(cajero_create.error_code() == ErrorCode::Forbidden, "Error debe ser Forbidden para cajero");

        // Cajero no debe poder modificar umbrales de bajo stock
        auto cajero_thresh = inventory_svc.update_low_stock_threshold(ing_pan_id, 5);
        TEST_CHECK(!cajero_thresh.is_success(), "Cajero no debe poder modificar umbrales de stock");
        TEST_CHECK(cajero_thresh.error_code() == ErrorCode::Forbidden, "Error debe ser Forbidden en umbrales");

        // Cajero no debe poder registrar compras
        auto cajero_restock = inventory_svc.register_purchase_entry(ing_pan_id, 10, "pz", Money::from_major_minor(30, 0));
        TEST_CHECK(!cajero_restock.is_success(), "Cajero no debe poder registrar compras");
        TEST_CHECK(cajero_restock.error_code() == ErrorCode::Forbidden, "Error debe ser Forbidden en compras");

        // Cajero SI puede consultar productos (pos.sell / pos.visual_catalog)
        auto cajero_search = catalog_svc.search_products("Chocolaleche", std::nullopt, 0, 10);
        TEST_CHECK(cajero_search.is_success() && !cajero_search.value().empty(), "Cajero puede buscar productos para venta");

        account_svc->logout();
    }

    // 9. Persistencia tras reapertura de la base de datos
    {
        SqliteDatabase db2;
        TEST_CHECK(db2.open(db_file).is_success(), "Reapertura de base de datos BE-03");

        auto account_svc2 = std::make_shared<SqliteAccountService>(db2);
        auto login_reopen = account_svc2->login("owner_cat", owner_pwd);
        TEST_CHECK(login_reopen.is_success(), "Login de dueno tras reapertura");

        SqliteCatalogService cat2(db2, account_svc2);
        SqliteInventoryService inv2(db2, account_svc2);

        // Producto regular persiste
        auto p_reopen = cat2.get_product_by_id(created_prod_id);
        TEST_CHECK(p_reopen.is_success(), "Producto regular persiste tras reapertura");
        TEST_CHECK(p_reopen.value().name == "Chocolaleche Especial Fria", "Nombre coincide");
        TEST_CHECK(p_reopen.value().price.cents() == 3850, "Precio coincide");

        // Ingredientes y stock persisten
        auto ing_limon_reopen = inv2.get_ingredient(ing_limon_id);
        TEST_CHECK(ing_limon_reopen.is_success(), "Ingrediente persiste tras reapertura");
        TEST_CHECK(ing_limon_reopen.value().current_stock_base == 5000000, "Stock de 5,000,000 mg persiste");

        auto ing_leche_reopen = inv2.get_ingredient(ing_leche_id);
        TEST_CHECK(ing_leche_reopen.is_success() && ing_leche_reopen.value().current_stock_base == 10000, "Stock de leche persiste");

        // Receta y costo estimado persisten
        auto rcp_reopen = cat2.get_recipe(prepared_prod_id);
        TEST_CHECK(rcp_reopen.is_success(), "Receta de producto preparado persiste");
        TEST_CHECK(rcp_reopen.value().items.size() == 2, "2 ingredientes en receta persistida");
        TEST_CHECK(rcp_reopen.value().estimated_cost_per_portion.cents() == 1650, "Costo estimado proporcional persiste intacto ($16.50)");
    }

    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }
}

void test_email_and_avatar_management() {
    std::cout << "\n--- TEST: Correo Obligatorio y Avatar Opcional (Migración 004) ---" << std::endl;
    const std::string db_file = "test_email_avatar.db";
    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }

    std::string pwd_owner = generate_random_secret(16);
    std::string pwd_user1 = generate_random_secret(16);
    std::string pwd_user2 = generate_random_secret(16);

    // Archivo de avatar de prueba PNG válido (8 bytes mínimos PNG)
    std::vector<std::uint8_t> valid_png = {0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x01, 0x02};
    // Avatar inválido (formato erróneo)
    std::vector<std::uint8_t> invalid_img = {0x12, 0x34, 0x56, 0x78};
    // Avatar excesivo (> 100 KB)
    std::vector<std::uint8_t> oversized_png(101 * 1024, 0x00);
    oversized_png[0] = 0x89; oversized_png[1] = 'P'; oversized_png[2] = 'N'; oversized_png[3] = 'G';
    oversized_png[4] = 0x0D; oversized_png[5] = 0x0A; oversized_png[6] = 0x1A; oversized_png[7] = 0x0A;

    std::string user1_id;

    // Sesión 1:
    {
        SqliteDatabase db;
        TEST_CHECK(db.open(db_file).is_success(), "Apertura de BD aislada");

        MigrationRunner runner(db);
        std::filesystem::path mig_dir = find_migrations_dir();
        auto mig_res = runner.run_pending_migrations_from_dir(mig_dir);
        TEST_CHECK(mig_res.is_success(), "Ejecucion de todas las migraciones incluyendo 004");

        SqliteAccountService svc(db);

        // 1. Alta inicial exige correo obligatorio con formato válido
        auto bad_email_owner = svc.create_initial_owner("dueno_test", "Dueno", pwd_owner, "correo_invalido");
        TEST_CHECK(!bad_email_owner.is_success() && bad_email_owner.error_code() == ErrorCode::InvalidInput,
                   "Alta inicial con correo invalido debe fallar con InvalidInput");

        auto empty_email_owner = svc.create_initial_owner("dueno_test", "Dueno", pwd_owner, "");
        TEST_CHECK(!empty_email_owner.is_success() && empty_email_owner.error_code() == ErrorCode::InvalidInput,
                   "Alta inicial con correo vacio debe fallar con InvalidInput");

        // Alta inicial con correo valido y sin avatar
        auto ok_owner = svc.create_initial_owner("dueno_test", "Dueno Principal", pwd_owner, "dueno@cafeteria-itch.edu.mx");
        TEST_CHECK(ok_owner.is_success(), "Alta inicial con correo valido debe ser exitosa");

        auto cur_owner = svc.current_user().value();
        TEST_CHECK(cur_owner.email == "dueno@cafeteria-itch.edu.mx", "Email del dueno coincide");
        TEST_CHECK(cur_owner.has_avatar == false, "Dueno creado sin avatar inicial");

        // 2. Crear usuario con correo y avatar valido
        CreateUserDto u1;
        u1.username = "empleado_carlos";
        u1.display_name = "Carlos Empleado";
        u1.email = "carlos@cafeteria-itch.edu.mx";
        u1.raw_password = pwd_user1;
        u1.role_id = "role-cashier";
        u1.avatar_bytes = valid_png;

        auto create_u1 = svc.create_user(u1);
        TEST_CHECK(create_u1.is_success(), "Creacion de usuario con avatar valido debe ser exitosa");
        auto u1_dto = create_u1.value();
        user1_id = u1_dto.id;
        TEST_CHECK(u1_dto.email == "carlos@cafeteria-itch.edu.mx", "Email de u1 coincide");
        TEST_CHECK(u1_dto.has_avatar == true, "u1 tiene avatar");

        // Verificar descarga del avatar
        auto av_res = svc.get_user_avatar(user1_id);
        TEST_CHECK(av_res.is_success(), "get_user_avatar exitoso");
        TEST_CHECK(av_res.value() == valid_png, "Bytes del avatar coinciden exactamente");

        // 3. Rechazar usuario con correo duplicado
        CreateUserDto u_dupe_email;
        u_dupe_email.username = "otro_carlos";
        u_dupe_email.display_name = "Otro Carlos";
        u_dupe_email.email = "carlos@cafeteria-itch.edu.mx"; // duplicado
        u_dupe_email.raw_password = pwd_user2;
        u_dupe_email.role_id = "role-cashier";

        auto create_dupe = svc.create_user(u_dupe_email);
        TEST_CHECK(!create_dupe.is_success() && create_dupe.error_code() == ErrorCode::Conflict,
                   "Creacion de usuario con correo duplicado debe fallar con Conflict");

        // 4. Rechazar avatar de formato invalido
        CreateUserDto u_bad_avatar;
        u_bad_avatar.username = "bad_av_user";
        u_bad_avatar.display_name = "Bad Avatar";
        u_bad_avatar.email = "bad_av@cafeteria-itch.edu.mx";
        u_bad_avatar.raw_password = pwd_user2;
        u_bad_avatar.role_id = "role-cashier";
        u_bad_avatar.avatar_bytes = invalid_img;

        auto create_bad_av = svc.create_user(u_bad_avatar);
        TEST_CHECK(!create_bad_av.is_success() && create_bad_av.error_code() == ErrorCode::InvalidInput,
                   "Avatar con formato desconocido debe fallar con InvalidInput");

        // 5. Rechazar avatar que exceda 100 KB
        CreateUserDto u_oversized;
        u_oversized.username = "oversized_user";
        u_oversized.display_name = "Oversized User";
        u_oversized.email = "oversized@cafeteria-itch.edu.mx";
        u_oversized.raw_password = pwd_user2;
        u_oversized.role_id = "role-cashier";
        u_oversized.avatar_bytes = oversized_png;

        auto create_over = svc.create_user(u_oversized);
        TEST_CHECK(!create_over.is_success() && create_over.error_code() == ErrorCode::InvalidInput,
                   "Avatar de mas de 100 KB debe fallar con InvalidInput");

        // 6. Actualizar correo y remover avatar
        UpdateUserDto upd_u1;
        upd_u1.user_id = user1_id;
        upd_u1.email = "carlos_nuevo@cafeteria-itch.edu.mx";
        upd_u1.remove_avatar = true;

        auto upd_res = svc.update_user(upd_u1);
        TEST_CHECK(upd_res.is_success(), "Actualizacion de email y eliminacion de avatar");

        auto u1_updated = svc.get_user_by_id(user1_id).value();
        TEST_CHECK(u1_updated.email == "carlos_nuevo@cafeteria-itch.edu.mx", "Nuevo correo reflejado");
        TEST_CHECK(u1_updated.has_avatar == false, "Avatar eliminado reflejado en DTO");

        auto av_after_remove = svc.get_user_avatar(user1_id);
        TEST_CHECK(!av_after_remove.is_success() && av_after_remove.error_code() == ErrorCode::NotFound,
                   "get_user_avatar debe retornar NotFound tras remover el avatar");

        // Reasignar avatar para probar persistencia tras reapertura
        UpdateUserDto upd_readd;
        upd_readd.user_id = user1_id;
        upd_readd.avatar_bytes = valid_png;
        TEST_CHECK(svc.update_user(upd_readd).is_success(), "Reasignar avatar");
    }

    // Sesión 2: Reapertura y verificación de persistencia
    {
        SqliteDatabase db2;
        TEST_CHECK(db2.open(db_file).is_success(), "Reapertura de BD");

        SqliteAccountService svc2(db2);
        auto login_res = svc2.login("dueno_test", pwd_owner);
        TEST_CHECK(login_res.is_success(), "Login de dueno tras reapertura");
        TEST_CHECK(login_res.value().email == "dueno@cafeteria-itch.edu.mx", "Email persiste en login");

        auto u1_reopen = svc2.get_user_by_id(user1_id);
        TEST_CHECK(u1_reopen.is_success(), "u1 existe tras reapertura");
        TEST_CHECK(u1_reopen.value().email == "carlos_nuevo@cafeteria-itch.edu.mx", "Email de u1 persiste");
        TEST_CHECK(u1_reopen.value().has_avatar == true, "has_avatar persiste");

        auto av_reopen = svc2.get_user_avatar(user1_id);
        TEST_CHECK(av_reopen.is_success(), "get_user_avatar exitoso tras reapertura");
        TEST_CHECK(av_reopen.value() == valid_png, "Bytes del avatar persisten intactos");

        auto user_list = svc2.list_users().value();
        TEST_CHECK(user_list.size() == 2, "2 usuarios registrados");
        for (const auto& u : user_list) {
            TEST_CHECK(!u.email.empty(), "Todos los usuarios tienen correo obligatorio no vacio");
        }
    }

    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }
}

void test_i2_shifts_cart_and_sales() {
    std::cout << "\n--- TEST I2: Turnos de Caja, Carrito Persistente, Notas y Venta Atomica con Deduccion de Receta ---" << std::endl;

    std::string unique_suffix = generate_random_alphanumeric(8);
    fs::path db_file = fs::current_path() / ("test_i2_pos_" + unique_suffix + ".db");
    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }

    auto mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Directorio de migraciones debe existir para I2");

    std::string owner_pwd = generate_random_secret(18);
    std::string cashier_pwd = generate_random_secret(16);
    std::string cashier_id;
    std::string prod_torta_id;
    std::string ing_pan_id;
    std::string ing_jamon_id;
    std::string sale_folio;

    {
        SqliteDatabase db;
        TEST_CHECK(db.open(db_file).is_success(), "Apertura de BD para I2");
        MigrationRunner runner(db);
        auto mig_res = runner.run_pending_migrations_from_dir(mig_dir);
        TEST_CHECK(mig_res.is_success(), "Ejecucion de migraciones para I2");

        auto account_svc = std::make_shared<SqliteAccountService>(db);
        auto cat_svc = std::make_shared<SqliteCatalogService>(db, account_svc);
        auto inv_svc = std::make_shared<SqliteInventoryService>(db, account_svc);
        auto shift_svc = std::make_shared<SqliteShiftService>(db, account_svc);
        auto cart_svc = std::make_shared<SqliteCartService>(db, account_svc, cat_svc);
        auto sale_svc = std::make_shared<SqliteSaleService>(db, account_svc, cart_svc, shift_svc);

        // 1. Alta de Dueno y Cajero
        auto create_own = account_svc->create_initial_owner("owner_i2", "Dueno I2", owner_pwd, "owner_i2@cafeteria-itch.edu.mx");
        TEST_CHECK(create_own.is_success(), "Creacion de dueno para I2");

        CreateUserDto cashier_dto;
        cashier_dto.username = "cajero_i2";
        cashier_dto.display_name = "Cajero Turno I2";
        cashier_dto.email = "cajero_i2@cafeteria-itch.edu.mx";
        cashier_dto.raw_password = cashier_pwd;
        cashier_dto.role_id = "role-cashier";

        auto create_cashier = account_svc->create_user(cashier_dto);
        TEST_CHECK(create_cashier.is_success(), "Creacion de usuario cajero");
        cashier_id = create_cashier.value().id;

        // 2. Catalogo: Ingredientes, Producto y Receta
        CreateIngredientDto ing_pan;
        ing_pan.name = "Pan Bolillo I2";
        ing_pan.unit_dimension = "COUNT";
        ing_pan.base_unit = "pz";
        ing_pan.low_stock_threshold_base = 5;
        auto pan_res = inv_svc->create_ingredient(ing_pan);
        TEST_CHECK(pan_res.is_success(), "Crear ingrediente Pan");
        ing_pan_id = pan_res.value().id;

        CreateIngredientDto ing_jamon;
        ing_jamon.name = "Jamon Pierna I2";
        ing_jamon.unit_dimension = "MASS";
        ing_jamon.base_unit = "mg";
        ing_jamon.low_stock_threshold_base = 100000;
        auto jamon_res = inv_svc->create_ingredient(ing_jamon);
        TEST_CHECK(jamon_res.is_success(), "Crear ingrediente Jamon");
        ing_jamon_id = jamon_res.value().id;

        // Compras iniciales: 20 pzas de pan ($50.00), 2 kg de jamón (2,000,000 mg) ($240.00)
        TEST_CHECK(inv_svc->register_purchase_entry(ing_pan_id, 20, "pz", Money::from_major_minor(50, 0)).is_success(), "Compra pan");
        TEST_CHECK(inv_svc->register_purchase_entry(ing_jamon_id, 2, "kg", Money::from_major_minor(240, 0)).is_success(), "Compra jamon");

        // Crear producto Torta
        CreateProductDto prod_torta;
        prod_torta.sku = "TORTA-01";
        prod_torta.barcode = "750100000001";
        prod_torta.name = "Torta Clasica I2";
        prod_torta.category = "Alimentos";
        prod_torta.price = Money::from_cents(4500); // $45.00
        prod_torta.is_prepared = true;
        auto prod_res = cat_svc->create_product(prod_torta);
        TEST_CHECK(prod_res.is_success(), "Crear producto torta");
        prod_torta_id = prod_res.value().id;

        // Receta: 1 pz pan + 50 g jamon (50,000 mg)
        std::vector<RecipeItemDto> recipe_items;
        recipe_items.push_back({ing_pan_id, "Pan Bolillo I2", 1, "pz"});
        recipe_items.push_back({ing_jamon_id, "Jamon Pierna I2", 50000, "mg"});
        TEST_CHECK(cat_svc->set_recipe(prod_torta_id, recipe_items).is_success(), "Asignar receta a torta");

        // Stock antes de la venta (consultado por Dueno con permisos de inventario)
        auto pan_before = inv_svc->get_ingredient(ing_pan_id).value().current_stock_base;
        auto jamon_before = inv_svc->get_ingredient(ing_jamon_id).value().current_stock_base;
        TEST_CHECK(pan_before == 20, "Stock inicial Pan 20");
        TEST_CHECK(jamon_before == 2000000, "Stock inicial Jamon 2000000 mg");

        auto get_stock_db = [&](const std::string& ing_id) -> std::int64_t {
            auto prep = db.prepare("SELECT current_stock FROM ingredients WHERE id = ?;");
            if (!prep.is_success()) return -1;
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, ing_id);
            if (!stmt.step().is_success()) return -1;
            return stmt.get_int64(0);
        };

        // 3. Login como Cajero
        account_svc->logout();
        auto login_cashier = account_svc->login("cajero_i2", cashier_pwd);
        TEST_CHECK(login_cashier.is_success(), "Login como cajero");

        // 4. Intentar venta sin turno abierto -> Debe fallar con ShiftClosed
        auto sale_no_shift = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000));
        TEST_CHECK(!sale_no_shift.is_success(), "Venta sin turno abierto debe fallar");
        TEST_CHECK(sale_no_shift.error_code() == ErrorCode::ShiftClosed, "Error debe ser ShiftClosed");

        // 5. Apertura de Turno
        auto open_shift_res = shift_svc->open_shift("reg-01", Money::from_cents(50000)); // $500.00 inicial
        TEST_CHECK(open_shift_res.is_success(), "Apertura de turno exitosa");
        TEST_CHECK(open_shift_res.value().initial_cash.cents() == 50000, "Fondo inicial $500.00");
        TEST_CHECK(open_shift_res.value().is_open == true, "Turno en estado open");

        // Conflicto de turno doble apertura
        auto open_double = shift_svc->open_shift("reg-01", Money::from_cents(20000));
        TEST_CHECK(!open_double.is_success() && open_double.error_code() == ErrorCode::Conflict, "No se permite abrir dos turnos concurrentes");

        auto cur_shift = shift_svc->get_current_shift();
        TEST_CHECK(cur_shift.is_success(), "Obtener turno activo");

        // 6. Pruebas de Carrito y Notas de Línea
        auto cart_init = cart_svc->get_cart();
        TEST_CHECK(cart_init.is_success() && cart_init.value().items.empty(), "Carrito inicialmente vacio");

        // Agregar 1 torta sin notas
        auto add1 = cart_svc->add_item(prod_torta_id, 1, "");
        TEST_CHECK(add1.is_success(), "Agregar torta sin notas");
        TEST_CHECK(add1.value().items.size() == 1, "1 linea en carrito");
        TEST_CHECK(add1.value().items[0].quantity == 1, "Cantidad 1");
        std::string line1_id = add1.value().items[0].line_id;

        // Agregar 2 tortas mas sin notas -> Se debe acumular en la misma linea
        auto add2 = cart_svc->add_item(prod_torta_id, 2, "");
        TEST_CHECK(add2.is_success(), "Agregar 2 tortas mas sin notas");
        TEST_CHECK(add2.value().items.size() == 1, "Sigue siendo 1 linea");
        TEST_CHECK(add2.value().items[0].quantity == 3, "Cantidad acumulada a 3");

        // Agregar torta con nota "sin cebolla" -> Debe crear linea separada obligatoria
        auto add_note1 = cart_svc->add_item(prod_torta_id, 1, "sin cebolla");
        TEST_CHECK(add_note1.is_success(), "Agregar torta con nota 'sin cebolla'");
        TEST_CHECK(add_note1.value().items.size() == 2, "Debe haber 2 lineas distintas para notas distintas");
        std::string line2_id;
        for (const auto& it : add_note1.value().items) {
            if (it.notes == "sin cebolla") {
                line2_id = it.line_id;
                TEST_CHECK(it.quantity == 1, "Cantidad 1 en linea con nota");
            }
        }
        TEST_CHECK(!line2_id.empty(), "Linea con nota encontrada");

        // Agregar otra torta con la misma nota "sin cebolla" -> Se acumula en line2
        auto add_note2 = cart_svc->add_item(prod_torta_id, 1, "sin cebolla");
        TEST_CHECK(add_note2.is_success(), "Agregar segunda torta con nota identica");
        TEST_CHECK(add_note2.value().items.size() == 2, "Se mantiene en 2 lineas");
        for (const auto& it : add_note2.value().items) {
            if (it.line_id == line2_id) {
                TEST_CHECK(it.quantity == 2, "Cantidad acumulada en linea con nota identica");
            }
        }

        // Rechazo de nota con mas de 250 caracteres
        std::string long_note(251, 'A');
        auto add_long = cart_svc->add_item(prod_torta_id, 1, long_note);
        TEST_CHECK(!add_long.is_success() && add_long.error_code() == ErrorCode::InvalidInput, "Nota mayor a 250 caracteres debe fallar");

        // Rechazo de caracteres de control en notas
        std::string ctrl_note = "sin cebolla\ncon tomate";
        auto add_ctrl = cart_svc->add_item(prod_torta_id, 1, ctrl_note);
        TEST_CHECK(!add_ctrl.is_success() && add_ctrl.error_code() == ErrorCode::InvalidInput, "Caracter de control en nota debe fallar");

        // Actualizar notas de la linea 2
        auto upd_note = cart_svc->update_item_notes(line2_id, "sin cebolla y bien dorada");
        TEST_CHECK(upd_note.is_success(), "Actualizar nota de linea");
        for (const auto& it : upd_note.value().items) {
            if (it.line_id == line2_id) {
                TEST_CHECK(it.notes == "sin cebolla y bien dorada", "Nota actualizada correctamente");
            }
        }

        // Eliminar linea 1 (sin notas), dejando solo la linea 2 con 2 unidades ($90.00)
        auto rem1 = cart_svc->remove_item(line1_id);
        TEST_CHECK(rem1.is_success(), "Remover linea 1");
        TEST_CHECK(rem1.value().items.size() == 1, "Solo queda 1 linea");
        TEST_CHECK(rem1.value().items[0].line_id == line2_id, "Queda la linea con notas");
        TEST_CHECK(rem1.value().total.cents() == 9000, "Total $90.00 (2 x $45.00)");

        // 7. Venta con Efectivo: Total 9000 ($90.00), Pagado 10000 ($100.00), Cambio 1000 ($10.00)
        // Intentar pago con efectivo insuficiente ($80.00 < $90.00) -> Debe fallar
        auto sale_underpaid = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(8000));
        TEST_CHECK(!sale_underpaid.is_success() && sale_underpaid.error_code() == ErrorCode::InvalidInput, "Efectivo recibido menor al total debe fallar");

        std::string idemp_key = "idemp-test-sale-001";
        auto sale_res = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), idemp_key);
        TEST_CHECK(sale_res.is_success(), "Confirmacion de venta atomica");
        auto sale_dto = sale_res.value();
        sale_folio = sale_dto.folio;
        TEST_CHECK(!sale_folio.empty(), "Folio asignado");
        TEST_CHECK(sale_dto.total.cents() == 9000, "Total $90.00");
        TEST_CHECK(sale_dto.cash_received.cents() == 10000, "Recibido $100.00");
        TEST_CHECK(sale_dto.change.cents() == 1000, "Cambio $10.00");

        // Verificar que el carrito quedo vacio tras la venta
        auto cart_after = cart_svc->get_cart();
        TEST_CHECK(cart_after.is_success() && cart_after.value().items.empty(), "Carrito debe limpiarse tras venta confirmada");

        // Verificar deduccion automatica de inventario en BD:
        // 2 tortas x (1 pan + 50,000 mg jamon) = 2 pan + 100,000 mg jamon
        auto pan_after = get_stock_db(ing_pan_id);
        auto jamon_after = get_stock_db(ing_jamon_id);
        TEST_CHECK(pan_after == 18, "Stock Pan deducido correctamente a 18 pz");
        TEST_CHECK(jamon_after == 1900000, "Stock Jamon deducido correctamente a 1,900,000 mg");

        // 8. Idempotencia: Confirmar de nuevo con la misma llave debe devolver la misma venta sin duplicar deduccion
        auto sale_idemp = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), idemp_key);
        TEST_CHECK(sale_idemp.is_success(), "Llamada idempotente exitosa");
        TEST_CHECK(sale_idemp.value().folio == sale_folio, "Folio coincide en reintento idempotente");
        auto pan_idemp = get_stock_db(ing_pan_id);
        auto jamon_idemp = get_stock_db(ing_jamon_id);
        TEST_CHECK(pan_idemp == 18, "Idempotencia: stock Pan no se duplica");
        TEST_CHECK(jamon_idemp == 1900000, "Idempotencia: stock Jamon no se duplica");

        // 9. Comprobar Ticket Inmutable
        auto ticket_res = sale_svc->get_ticket(sale_folio);
        TEST_CHECK(ticket_res.is_success(), "Obtencion de ticket por folio");
        auto ticket = ticket_res.value();
        TEST_CHECK(ticket.folio == sale_folio, "Folio en ticket");
        TEST_CHECK(ticket.business_name == "Cafetería ITCH II", "Nombre de negocio en ticket");
        TEST_CHECK(ticket.items.size() == 1, "1 item en ticket");
        TEST_CHECK(ticket.items[0].product_name == "Torta Clasica I2", "Nombre del producto en ticket");
        TEST_CHECK(ticket.items[0].quantity == 2, "Cantidad 2 en ticket");
        TEST_CHECK(ticket.items[0].notes == "sin cebolla y bien dorada", "Nota de linea preservada en ticket");
        TEST_CHECK(ticket.total.cents() == 9000, "Total ticket $90.00");
        TEST_CHECK(ticket.cash_received.cents() == 10000, "Recibido ticket $100.00");
        TEST_CHECK(ticket.change.cents() == 1000, "Cambio ticket $10.00");

        std::string ticket_text = ticket.to_formatted_text();
        TEST_CHECK(ticket_text.find("Cafetería ITCH II") != std::string::npos, "Ticket texto contiene negocio");
        TEST_CHECK(ticket_text.find(sale_folio) != std::string::npos, "Ticket texto contiene folio");
        TEST_CHECK(ticket_text.find("Torta Clasica I2") != std::string::npos, "Ticket texto contiene producto");
        TEST_CHECK(ticket_text.find("sin cebolla y bien dorada") != std::string::npos, "Ticket texto contiene notas");
        TEST_CHECK(ticket_text.find("TOTAL:") != std::string::npos, "Ticket texto contiene TOTAL:");
        TEST_CHECK(ticket_text.find("$90.00") != std::string::npos, "Ticket texto contiene $90.00");
        TEST_CHECK(ticket_text.find("Recibido:") != std::string::npos, "Ticket texto contiene Recibido:");
        TEST_CHECK(ticket_text.find("$100.00") != std::string::npos, "Ticket texto contiene $100.00");
        TEST_CHECK(ticket_text.find("Cambio:") != std::string::npos, "Ticket texto contiene Cambio:");
        TEST_CHECK(ticket_text.find("$10.00") != std::string::npos, "Ticket texto contiene $10.00");

        // 10. Cierre de Turno
        auto close_res = shift_svc->close_shift(Money::from_cents(59000)); // $500 inicial + $90 venta = $590.00
        TEST_CHECK(close_res.is_success(), "Cierre de turno exitoso");

        // Una vez cerrado, get_current_shift debe retornar NotFound
        auto cur_closed = shift_svc->get_current_shift();
        TEST_CHECK(!cur_closed.is_success() && cur_closed.error_code() == ErrorCode::NotFound, "No hay turno activo tras cierre");
    }

    // 11. Persistencia tras reapertura de base de datos
    {
        SqliteDatabase db2;
        TEST_CHECK(db2.open(db_file).is_success(), "Reapertura de BD I2");

        auto account_svc2 = std::make_shared<SqliteAccountService>(db2);
        auto cat_svc2 = std::make_shared<SqliteCatalogService>(db2, account_svc2);
        auto inv_svc2 = std::make_shared<SqliteInventoryService>(db2, account_svc2);
        auto shift_svc2 = std::make_shared<SqliteShiftService>(db2, account_svc2);
        auto cart_svc2 = std::make_shared<SqliteCartService>(db2, account_svc2, cat_svc2);
        auto sale_svc2 = std::make_shared<SqliteSaleService>(db2, account_svc2, cart_svc2, shift_svc2);

        // Login con cajero
        auto login_reopen = account_svc2->login("cajero_i2", cashier_pwd);
        TEST_CHECK(login_reopen.is_success(), "Login cajero tras reapertura");

        // Ticket persiste inmutable
        auto ticket_reopen = sale_svc2->get_ticket(sale_folio);
        TEST_CHECK(ticket_reopen.is_success(), "Ticket recuperado tras reapertura");
        TEST_CHECK(ticket_reopen.value().folio == sale_folio, "Folio coincide tras reapertura");
        TEST_CHECK(ticket_reopen.value().items[0].notes == "sin cebolla y bien dorada", "Notas de linea persisten tras reapertura");
        TEST_CHECK(ticket_reopen.value().total.cents() == 9000, "Total persiste tras reapertura");

        // Stocks persisten (consultados con Dueno)
        account_svc2->logout();
        auto login_owner = account_svc2->login("owner_i2", owner_pwd);
        TEST_CHECK(login_owner.is_success(), "Login dueno tras reapertura");
        auto pan_reopen = inv_svc2->get_ingredient(ing_pan_id).value().current_stock_base;
        auto jamon_reopen = inv_svc2->get_ingredient(ing_jamon_id).value().current_stock_base;
        TEST_CHECK(pan_reopen == 18, "Stock Pan persiste a 18");
        TEST_CHECK(jamon_reopen == 1900000, "Stock Jamon persiste a 1,900,000 mg");
    }

    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }
}

void test_i2_permissions_and_line_isolation() {
    std::cout << "[TEST] I2: Permisos en servicios protegidos y aislamiento estricto de lineas de carrito..." << std::endl;

    const std::string db_file = "test_i2_permissions.db";
    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }

    auto mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Directorio de migraciones debe existir");

    std::string owner_pwd = generate_random_secret(18);
    std::string cashier1_pwd = generate_random_secret(16);
    std::string cashier2_pwd = generate_random_secret(16);

    {
        SqliteDatabase db;
        TEST_CHECK(db.open(db_file).is_success(), "Apertura de BD");
        MigrationRunner runner(db);
        auto mig_res = runner.run_pending_migrations_from_dir(mig_dir);
        TEST_CHECK(mig_res.is_success(), "Ejecutar migraciones");

        auto account_svc = std::make_shared<SqliteAccountService>(db);
        auto cat_svc = std::make_shared<SqliteCatalogService>(db, account_svc);
        auto inv_svc = std::make_shared<SqliteInventoryService>(db, account_svc);
        auto shift_svc = std::make_shared<SqliteShiftService>(db, account_svc);
        auto cart_svc = std::make_shared<SqliteCartService>(db, account_svc, cat_svc);
        auto sale_svc = std::make_shared<SqliteSaleService>(db, account_svc, cart_svc, shift_svc);

        // 1. Verificar llamadas no autenticadas (sin sesion) en servicios protegidos
        auto unauth_cart = cart_svc->get_cart();
        TEST_CHECK(!unauth_cart.is_success() && unauth_cart.error_code() == ErrorCode::Forbidden,
                   "get_cart() sin sesion debe retornar Forbidden");

        auto unauth_add = cart_svc->add_item("prod-dummy", 1);
        TEST_CHECK(!unauth_add.is_success() && unauth_add.error_code() == ErrorCode::Forbidden,
                   "add_item() sin sesion debe retornar Forbidden");

        auto unauth_qty = cart_svc->update_quantity("line-dummy", 2);
        TEST_CHECK(!unauth_qty.is_success() && unauth_qty.error_code() == ErrorCode::Forbidden,
                   "update_quantity() sin sesion debe retornar Forbidden");

        auto unauth_note = cart_svc->update_item_notes("line-dummy", "nota");
        TEST_CHECK(!unauth_note.is_success() && unauth_note.error_code() == ErrorCode::Forbidden,
                   "update_item_notes() sin sesion debe retornar Forbidden");

        auto unauth_rem = cart_svc->remove_item("line-dummy");
        TEST_CHECK(!unauth_rem.is_success() && unauth_rem.error_code() == ErrorCode::Forbidden,
                   "remove_item() sin sesion debe retornar Forbidden");

        auto unauth_clear = cart_svc->clear_cart();
        TEST_CHECK(!unauth_clear.is_success() && unauth_clear.error_code() == ErrorCode::Forbidden,
                   "clear_cart() sin sesion debe retornar Forbidden");

        auto unauth_sale = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(1000));
        TEST_CHECK(!unauth_sale.is_success() && unauth_sale.error_code() == ErrorCode::Forbidden,
                   "confirm_sale() sin sesion debe retornar Forbidden");

        // 2. Dar de alta Dueno y 2 Cajeros
        TEST_CHECK(account_svc->create_initial_owner("owner_iso", "Dueno Iso", owner_pwd, "owner_iso@itch.edu.mx").is_success(), "Crear Dueno");

        CreateUserDto c1{"cajero_1", "Cajero Uno", cashier1_pwd, "role-cashier", "c1@itch.edu.mx"};
        TEST_CHECK(account_svc->create_user(c1).is_success(), "Crear Cajero 1");

        CreateUserDto c2{"cajero_2", "Cajero Dos", cashier2_pwd, "role-cashier", "c2@itch.edu.mx"};
        TEST_CHECK(account_svc->create_user(c2).is_success(), "Crear Cajero 2");

        // Dueno crea un producto en catalogo
        CreateProductDto prod;
        prod.sku = "CAFE-ISO";
        prod.name = "Cafe Americano Iso";
        prod.category = "Bebidas";
        prod.price = Money::from_cents(2500);
        auto prod_res = cat_svc->create_product(prod);
        TEST_CHECK(prod_res.is_success(), "Crear producto");
        std::string prod_id = prod_res.value().id;

        account_svc->logout();

        // 3. Login Cajero 1 y agregar item al carrito
        TEST_CHECK(account_svc->login("cajero_1", cashier1_pwd).is_success(), "Login Cajero 1");
        auto add_res = cart_svc->add_item(prod_id, 2, "sin azucar");
        TEST_CHECK(add_res.is_success(), "Cajero 1 agrega item");
        TEST_CHECK(add_res.value().items.size() == 1, "Cajero 1 tiene 1 item en carrito");
        std::string line1_id = add_res.value().items[0].line_id;

        account_svc->logout();

        // 4. Login Cajero 2 -> Validar aislamiento de linea
        TEST_CHECK(account_svc->login("cajero_2", cashier2_pwd).is_success(), "Login Cajero 2");

        // Carrito de Cajero 2 debe estar vacio
        auto cart_c2 = cart_svc->get_cart();
        TEST_CHECK(cart_c2.is_success() && cart_c2.value().items.empty(), "Carrito de Cajero 2 debe estar vacio");

        // Cajero 2 intenta modificar cantidad de linea de Cajero 1 -> NotFound
        auto c2_upd_qty = cart_svc->update_quantity(line1_id, 10);
        TEST_CHECK(!c2_upd_qty.is_success() && c2_upd_qty.error_code() == ErrorCode::NotFound,
                   "Cajero 2 no puede actualizar cantidad de linea ajena");

        // Cajero 2 intenta modificar nota de linea de Cajero 1 -> NotFound
        auto c2_upd_note = cart_svc->update_item_notes(line1_id, "hackeado");
        TEST_CHECK(!c2_upd_note.is_success() && c2_upd_note.error_code() == ErrorCode::NotFound,
                   "Cajero 2 no puede modificar notas de linea ajena");

        // Cajero 2 intenta eliminar linea de Cajero 1 -> NotFound
        auto c2_rem = cart_svc->remove_item(line1_id);
        TEST_CHECK(!c2_rem.is_success() && c2_rem.error_code() == ErrorCode::NotFound,
                   "Cajero 2 no puede eliminar linea ajena");

        account_svc->logout();

        // 5. Login nuevamente con Cajero 1 -> su carrito sigue intacto
        TEST_CHECK(account_svc->login("cajero_1", cashier1_pwd).is_success(), "Re-login Cajero 1");
        auto c1_cart_back = cart_svc->get_cart();
        TEST_CHECK(c1_cart_back.is_success() && c1_cart_back.value().items.size() == 1,
                   "Carrito de Cajero 1 preservado intacto");
        TEST_CHECK(c1_cart_back.value().items[0].line_id == line1_id, "ID de linea coincide");
        TEST_CHECK(c1_cart_back.value().items[0].quantity == 2, "Cantidad intacta");
        TEST_CHECK(c1_cart_back.value().items[0].notes == "sin azucar", "Notas intactas");

        // Cajero 1 si puede limpiar su propio carrito
        TEST_CHECK(cart_svc->clear_cart().is_success(), "Cajero 1 limpia su propio carrito");
        TEST_CHECK(cart_svc->get_cart().value().items.empty(), "Carrito de Cajero 1 limpio");
    }

    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }
}

void test_i2_sales_transaction_rollback_and_idempotency() {
    std::cout << "[TEST] I2: Transaccion atomica de venta, rollback ante fallos, cash movements e idempotencia..." << std::endl;

    const std::string db_file = "test_i2_tx_rollback.db";
    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }

    auto mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Directorio de migraciones debe existir");

    std::string owner_pwd = generate_random_secret(18);
    std::string cashier_pwd = generate_random_secret(16);

    {
        SqliteDatabase db;
        TEST_CHECK(db.open(db_file).is_success(), "Apertura de BD");
        MigrationRunner runner(db);
        auto mig_res = runner.run_pending_migrations_from_dir(mig_dir);
        TEST_CHECK(mig_res.is_success(), "Ejecutar migraciones");

        auto account_svc = std::make_shared<SqliteAccountService>(db);
        auto cat_svc = std::make_shared<SqliteCatalogService>(db, account_svc);
        auto inv_svc = std::make_shared<SqliteInventoryService>(db, account_svc);
        auto shift_svc = std::make_shared<SqliteShiftService>(db, account_svc);
        auto cart_svc = std::make_shared<SqliteCartService>(db, account_svc, cat_svc);
        auto sale_svc = std::make_shared<SqliteSaleService>(db, account_svc, cart_svc, shift_svc);

        // 1. Alta Dueno y Cajero
        TEST_CHECK(account_svc->create_initial_owner("owner_tx", "Dueno Tx", owner_pwd, "owner_tx@itch.edu.mx").is_success(), "Crear Dueno");
        CreateUserDto c_dto{"cajero_tx", "Cajero Tx", cashier_pwd, "role-cashier", "cajero_tx@itch.edu.mx"};
        TEST_CHECK(account_svc->create_user(c_dto).is_success(), "Crear Cajero");

        // 2. Catalogo: Ingrediente Carne (stock 2 pz) y Producto Hamburguesa
        CreateIngredientDto ing_carne;
        ing_carne.name = "Carne Hamburguesa";
        ing_carne.unit_dimension = "COUNT";
        ing_carne.base_unit = "pz";
        ing_carne.low_stock_threshold_base = 5;
        auto carne_res = inv_svc->create_ingredient(ing_carne);
        TEST_CHECK(carne_res.is_success(), "Crear ingrediente Carne");
        std::string ing_carne_id = carne_res.value().id;

        // Comprar exactamente 2 piezas de carne
        TEST_CHECK(inv_svc->register_purchase_entry(ing_carne_id, 2, "pz", Money::from_major_minor(60, 0)).is_success(), "Compra 2 pzas carne");

        // Crear Hamburguesa ($65.00) con receta de 1 carne por hamburguesa
        CreateProductDto prod_burg;
        prod_burg.sku = "BURGER-01";
        prod_burg.name = "Hamburguesa Sencilla";
        prod_burg.category = "Alimentos";
        prod_burg.price = Money::from_cents(6500);
        prod_burg.is_prepared = true;
        auto burg_res = cat_svc->create_product(prod_burg);
        TEST_CHECK(burg_res.is_success(), "Crear producto Hamburguesa");
        std::string prod_burg_id = burg_res.value().id;

        std::vector<RecipeItemDto> burger_recipe;
        burger_recipe.push_back({ing_carne_id, "Carne Hamburguesa", 1, "pz"});
        TEST_CHECK(cat_svc->set_recipe(prod_burg_id, burger_recipe).is_success(), "Asignar receta Hamburguesa");

        // Crear Papas ($35.00) sin receta
        CreateProductDto prod_papas;
        prod_papas.sku = "PAPAS-01";
        prod_papas.name = "Papas a la Francesa";
        prod_papas.category = "Alimentos";
        prod_papas.price = Money::from_cents(3500);
        prod_papas.is_prepared = false;
        auto papas_res = cat_svc->create_product(prod_papas);
        TEST_CHECK(papas_res.is_success(), "Crear producto Papas");
        std::string prod_papas_id = papas_res.value().id;

        account_svc->logout();

        // 3. Login como Cajero y apertura explicita de turno con $250.00
        TEST_CHECK(account_svc->login("cajero_tx", cashier_pwd).is_success(), "Login cajero");
        auto open_shift = shift_svc->open_shift("reg-01", Money::from_cents(25000));
        TEST_CHECK(open_shift.is_success(), "Apertura de turno con $250.00");
        std::string shift_id = open_shift.value().id;

        // Validar registro en cash_movements para INITIAL_CASH
        auto count_cash_movements = [&](const std::string& type) -> int {
            auto prep = db.prepare("SELECT COUNT(*) FROM cash_movements WHERE shift_id = ? AND movement_type = ?;");
            if (!prep.is_success()) return -1;
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, shift_id);
            stmt.bind_text(2, type);
            if (!stmt.step().is_success()) return -1;
            return static_cast<int>(stmt.get_int64(0));
        };
        TEST_CHECK(count_cash_movements("INITIAL_CASH") == 1, "Debe existir 1 movimiento INITIAL_CASH");

        // 4. TEST DE ROLLBACK POR STOCK INSUFICIENTE
        TEST_CHECK(cart_svc->add_item(prod_burg_id, 3, "bien cocidas").is_success(), "Agregar 3 hamburguesas");
        auto cart_items = cart_svc->get_cart().value().items;
        TEST_CHECK(cart_items.size() == 1 && cart_items[0].quantity == 3, "Carrito tiene 3 hamburguesas");

        // Intentar venta -> debe fallar con InsufficientStock
        auto sale_insufficient = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(20000));
        TEST_CHECK(!sale_insufficient.is_success() && sale_insufficient.error_code() == ErrorCode::InsufficientStock,
                   "Confirmar venta con stock insuficiente debe fallar");

        // VERIFICAR ROLLBACK:
        // a) El carrito de compra NO se borra (borrador se preserva)
        auto cart_preserved = cart_svc->get_cart();
        TEST_CHECK(cart_preserved.is_success() && cart_preserved.value().items.size() == 1,
                   "El carrito se conserva intacto tras fallo de venta");
        TEST_CHECK(cart_preserved.value().items[0].quantity == 3, "Cantidad en carrito sigue intacta");

        // b) El stock de carne sigue en 2 pz
        {
            auto prep = db.prepare("SELECT current_stock FROM ingredients WHERE id = ?;");
            TEST_CHECK(prep.is_success(), "Prepare SELECT current_stock debe ser exitoso");
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, ing_carne_id);
            auto st_res = stmt.step();
            TEST_CHECK(st_res.is_success() && st_res.value(), "Step SELECT current_stock debe devolver fila");
            TEST_CHECK(stmt.get_int64(0) == 2, "Stock no fue modificado tras rollback");
        }

        // c) No se crearon registros de ventas ni cash_movements de venta
        {
            auto prep = db.prepare("SELECT COUNT(*) FROM sales;");
            TEST_CHECK(prep.is_success(), "Prepare SELECT COUNT(*) FROM sales debe ser exitoso");
            auto stmt = std::move(prep.value());
            auto st_res = stmt.step();
            TEST_CHECK(st_res.is_success() && st_res.value(), "Step COUNT(*) sales debe ser exitoso");
            TEST_CHECK(stmt.get_int64(0) == 0, "No debe haber ventas registradas tras rollback");
        }
        TEST_CHECK(count_cash_movements("SALE") == 0, "No debe haber movimientos SALE tras rollback");

        // Limpiar carrito para siguiente prueba
        TEST_CHECK(cart_svc->clear_cart().is_success(), "Limpiar carrito");

        // 5. TEST DE ROLLBACK POR CAMBIO DE PRECIO EN CATALOGO
        // Agregar 1 porcion de Papas ($35.00)
        TEST_CHECK(cart_svc->add_item(prod_papas_id, 1).is_success(), "Agregar papas al carrito");
        TEST_CHECK(cart_svc->get_cart().value().total.cents() == 3500, "Total $35.00");

        // El Dueno cambia el precio de las papas en el catalogo a $40.00
        account_svc->logout();
        TEST_CHECK(account_svc->login("owner_tx", owner_pwd).is_success(), "Login Dueno");
        UpdateProductDto upd_papas;
        upd_papas.product_id = prod_papas_id;
        upd_papas.price = Money::from_cents(4000); // Nuevo precio $40.00
        TEST_CHECK(cat_svc->update_product(upd_papas).is_success(), "Dueno actualiza precio de papas a $40.00");

        // Cajero intenta confirmar la venta a $35.00 -> Debe fallar con Conflict
        account_svc->logout();
        TEST_CHECK(account_svc->login("cajero_tx", cashier_pwd).is_success(), "Login Cajero");
        auto sale_conflict = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(5000));
        TEST_CHECK(!sale_conflict.is_success() && sale_conflict.error_code() == ErrorCode::Conflict,
                   "Venta con precio desactualizado debe fallar con Conflict");

        // Verificar carrito conservado
        TEST_CHECK(cart_svc->get_cart().value().items.size() == 1, "Borrador de carrito conservado tras conflicto");
        TEST_CHECK(count_cash_movements("SALE") == 0, "Sin movimientos de efectivo tras conflicto");

        // Limpiar carrito
        TEST_CHECK(cart_svc->clear_cart().is_success(), "Limpiar carrito");

        // 6. VENTA EXITOSA Y MOVIMIENTO DE CAJA
        // Agregar 1 Hamburguesa ($65.00) (stock = 2, requiere 1 -> OK)
        TEST_CHECK(cart_svc->add_item(prod_burg_id, 1, "con extra pepinillos").is_success(), "Agregar 1 hamburguesa");
        TEST_CHECK(cart_svc->get_cart().value().total.cents() == 6500, "Total $65.00");

        std::string stable_idemp_key = "idemp-atomic-sale-42";
        auto sale_ok = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), stable_idemp_key);
        TEST_CHECK(sale_ok.is_success(), "Confirmacion de venta atomica exitosa");
        auto conf_dto = sale_ok.value();
        TEST_CHECK(conf_dto.total.cents() == 6500, "Total venta $65.00");
        TEST_CHECK(conf_dto.cash_received.cents() == 10000, "Recibido $100.00");
        TEST_CHECK(conf_dto.change.cents() == 3500, "Cambio calculado exacto $35.00");

        // Verificar que el carrito quedo vacio tras la venta exitosa
        TEST_CHECK(cart_svc->get_cart().value().items.empty(), "Carrito limpio tras venta confirmada");

        // Stock de carne se redujo de 2 a 1
        {
            auto prep = db.prepare("SELECT current_stock FROM ingredients WHERE id = ?;");
            TEST_CHECK(prep.is_success(), "Prepare stock reducido");
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, ing_carne_id);
            auto st_res = stmt.step();
            TEST_CHECK(st_res.is_success() && st_res.value(), "Step stock reducido");
            TEST_CHECK(stmt.get_int64(0) == 1, "Stock carne reducido a 1 pz");
        }

        // Verificar cash_movements de venta
        TEST_CHECK(count_cash_movements("SALE") == 1, "Existe exactamente 1 cash_movement de tipo SALE");
        {
            auto prep = db.prepare("SELECT amount_cents FROM cash_movements WHERE shift_id = ? AND movement_type = 'SALE';");
            TEST_CHECK(prep.is_success(), "Prepare amount_cents SALE");
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, shift_id);
            auto st_res = stmt.step();
            TEST_CHECK(st_res.is_success() && st_res.value(), "Step amount_cents SALE");
            TEST_CHECK(stmt.get_int64(0) == 6500, "Monto de cash_movement es $65.00");
        }

        // Verificar bitacora de auditoria
        {
            auto prep = db.prepare("SELECT COUNT(*) FROM audit_events WHERE event_type = 'SALE_CONFIRMED';");
            TEST_CHECK(prep.is_success(), "Prepare count audit_events");
            auto stmt = std::move(prep.value());
            auto st_res = stmt.step();
            TEST_CHECK(st_res.is_success() && st_res.value(), "Step count audit_events");
            TEST_CHECK(stmt.get_int64(0) == 1, "Auditoria SALE_CONFIRMED registrada");
        }

        // 7. CIERRE DE TURNO
        // Total esperado en caja: $250 inicial + $65 venta = $315.00 (31500 centavos)
        auto close_res = shift_svc->close_shift(Money::from_cents(31500));
        TEST_CHECK(close_res.is_success(), "Cierre de turno exitoso");

        // Verificar que ya no hay turno activo
        TEST_CHECK(!shift_svc->get_current_shift().is_success(), "Ya no hay turno activo");

        // 8. IDEMPOTENCIA CON TURNO CERRADO: Un reintento autorizado con la misma llave debe recuperar la venta original
        auto retry_after_close = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), stable_idemp_key);
        TEST_CHECK(retry_after_close.is_success(), "Reintento idempotente DEBE tener exito incluso con turno cerrado");
        TEST_CHECK(retry_after_close.value().folio == conf_dto.folio, "Folio coincide con la venta original");
        TEST_CHECK(retry_after_close.value().total.cents() == 6500, "Total coincide");
        TEST_CHECK(retry_after_close.value().change.cents() == 3500, "Cambio coincide");

        // Stock NO debe haber sido descontado nuevamente (sigue en 1)
        {
            auto prep = db.prepare("SELECT current_stock FROM ingredients WHERE id = ?;");
            TEST_CHECK(prep.is_success(), "Prepare stock idempotencia");
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, ing_carne_id);
            auto st_res = stmt.step();
            TEST_CHECK(st_res.is_success() && st_res.value(), "Step stock idempotencia");
            TEST_CHECK(stmt.get_int64(0) == 1, "Stock no fue duplicado en reintento idempotente");
        }
        // Movimientos de efectivo no se duplican
        TEST_CHECK(count_cash_movements("SALE") == 1, "No se duplico el movimiento de efectivo");
    }

    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }
}

void test_i2_recipes_idempotency_cart_editing_and_ticket_history() {
    std::cout << "\n[TEST] I2: Versión de recetas activa, idempotencia contextual, edición de carrito y tickets..." << std::endl;

    fs::path temp_dir = fs::temp_directory_path() / ("cafeteria_i2_closure_" + generate_random_alphanumeric(8));
    fs::create_directories(temp_dir);
    std::string db_file = (temp_dir / "closure.db").string();

    std::string owner_pwd = generate_random_secret(18);
    std::string cashier1_pwd = generate_random_secret(18);
    std::string cashier2_pwd = generate_random_secret(18);

    std::string prod_sandwich_id;
    std::string ing_pan_id;
    std::string ing_jamon_id;
    std::string sale1_folio;
    std::string sale2_folio;
    std::string sale3_folio;

    auto mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Directorio de migraciones debe existir");

    {
        SqliteDatabase db;
        TEST_CHECK(db.open(db_file).is_success(), "Abrir base temporal para cierre I2");

        MigrationRunner runner(db);
        auto mig_res = runner.run_pending_migrations_from_dir(mig_dir);
        TEST_CHECK(mig_res.is_success(), "Aplicar migraciones");

        auto account_svc = std::make_shared<SqliteAccountService>(db);
        auto inv_svc = std::make_shared<SqliteInventoryService>(db, account_svc);
        auto cat_svc = std::make_shared<SqliteCatalogService>(db, account_svc);
        auto shift_svc = std::make_shared<SqliteShiftService>(db, account_svc);
        auto cart_svc = std::make_shared<SqliteCartService>(db, account_svc, cat_svc);
        auto sale_svc = std::make_shared<SqliteSaleService>(db, account_svc, cart_svc, shift_svc);

        // 1. Alta de Dueño, Cajero 1 y Cajero 2
        TEST_CHECK(account_svc->create_initial_owner("dueno_close", "Dueño Cierre", owner_pwd, "dueno_close@itch.edu.mx").is_success(), "Alta dueño");
        TEST_CHECK(account_svc->login("dueno_close", owner_pwd).is_success(), "Login dueño");

        CreateUserDto cashier1_dto;
        cashier1_dto.username = "cajero_01";
        cashier1_dto.display_name = "Cajero Uno";
        cashier1_dto.email = "cajero1@itch.edu.mx";
        cashier1_dto.raw_password = cashier1_pwd;
        cashier1_dto.role_id = "role-cashier";
        TEST_CHECK(account_svc->create_user(cashier1_dto).is_success(), "Crear cajero 1");

        CreateUserDto cashier2_dto;
        cashier2_dto.username = "cajero_02";
        cashier2_dto.display_name = "Cajero Dos";
        cashier2_dto.email = "cajero2@itch.edu.mx";
        cashier2_dto.raw_password = cashier2_pwd;
        cashier2_dto.role_id = "role-cashier";
        TEST_CHECK(account_svc->create_user(cashier2_dto).is_success(), "Crear cajero 2");

        // 2. Dueño crea insumos: Pan Artesanal (10 pz) y Jamón (5 pz)
        CreateIngredientDto ing_pan;
        ing_pan.name = "Pan Artesanal";
        ing_pan.unit_dimension = "COUNT";
        ing_pan.base_unit = "pz";
        ing_pan.low_stock_threshold_base = 2;
        auto pan_res = inv_svc->create_ingredient(ing_pan);
        TEST_CHECK(pan_res.is_success(), "Crear ingrediente Pan");
        ing_pan_id = pan_res.value().id;
        TEST_CHECK(inv_svc->register_purchase_entry(ing_pan_id, 10, "pz", Money::from_major_minor(50, 0)).is_success(), "Entrada 10 Pan");

        CreateIngredientDto ing_jamon;
        ing_jamon.name = "Jamón Supremo";
        ing_jamon.unit_dimension = "COUNT";
        ing_jamon.base_unit = "pz";
        ing_jamon.low_stock_threshold_base = 1;
        auto jamon_res = inv_svc->create_ingredient(ing_jamon);
        TEST_CHECK(jamon_res.is_success(), "Crear ingrediente Jamón");
        ing_jamon_id = jamon_res.value().id;
        TEST_CHECK(inv_svc->register_purchase_entry(ing_jamon_id, 5, "pz", Money::from_major_minor(100, 0)).is_success(), "Entrada 5 Jamón");

        // 3. Dueño crea producto Sandwich ($60.00) y asigna Receta Versión 1 (2 Pan)
        CreateProductDto prod_sand;
        prod_sand.sku = "SAND-01";
        prod_sand.name = "Sándwich Especial";
        prod_sand.category = "Alimentos";
        prod_sand.price = Money::from_cents(6000);
        prod_sand.is_prepared = true;
        auto sand_res = cat_svc->create_product(prod_sand);
        TEST_CHECK(sand_res.is_success(), "Crear producto Sandwich");
        prod_sandwich_id = sand_res.value().id;

        std::vector<RecipeItemDto> recipe_v1;
        recipe_v1.push_back({ing_pan_id, "Pan Artesanal", 2, "pz"});
        TEST_CHECK(cat_svc->set_recipe(prod_sandwich_id, recipe_v1).is_success(), "Asignar Receta v1 (2x Pan)");

        // Verificar versión 1 en tabla recipes
        {
            auto prep = db.prepare("SELECT version FROM recipes WHERE product_id = ? ORDER BY version DESC LIMIT 1;");
            TEST_CHECK(prep.is_success(), "Prepare select recipe version");
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, prod_sandwich_id);
            TEST_CHECK(stmt.step().is_success() && stmt.get_int64(0) == 1, "Receta inicial debe ser versión 1");
        }

        account_svc->logout();

        // 4. Cajero 1 abre turno y realiza Venta 1 con Receta v1
        TEST_CHECK(account_svc->login("cajero_01", cashier1_pwd).is_success(), "Login Cajero 1");
        auto open_shift = shift_svc->open_shift("reg-01", Money::from_cents(30000));
        TEST_CHECK(open_shift.is_success(), "Abrir turno Cajero 1 con $300.00");

        TEST_CHECK(cart_svc->add_item(prod_sandwich_id, 1, "sin mayonesa").is_success(), "Agregar Sandwich a carrito");
        auto sale1_res = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000));
        TEST_CHECK(sale1_res.is_success(), "Confirmar Venta 1");
        sale1_folio = sale1_res.value().folio;

        auto get_stock_db = [&](const std::string& ing_id) -> std::int64_t {
            auto prep = db.prepare("SELECT current_stock FROM ingredients WHERE id = ?;");
            if (!prep.is_success()) return -1;
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, ing_id);
            if (!stmt.step().is_success()) return -1;
            return stmt.get_int64(0);
        };

        // Cajero no tiene permiso para consultar inventario directamente
        auto pan_forbidden = inv_svc->get_ingredient(ing_pan_id);
        TEST_CHECK(!pan_forbidden.is_success() && pan_forbidden.error_code() == ErrorCode::Forbidden,
                   "Cajero no debe tener permiso inventory.view para consultar ingredientes");

        // Comprobar existencias físicas tras Venta 1: Pan bajó de 10 a 8 (-2), Jamón intacto en 5
        TEST_CHECK(get_stock_db(ing_pan_id) == 8, "Stock Pan tras V1 debe ser 8 pz");
        TEST_CHECK(get_stock_db(ing_jamon_id) == 5, "Stock Jamón tras V1 debe ser 5 pz");

        account_svc->logout();

        // 5. Dueño actualiza receta a Versión 2 (1 Pan, 1 Jamón)
        TEST_CHECK(account_svc->login("dueno_close", owner_pwd).is_success(), "Login Dueño para v2");
        std::vector<RecipeItemDto> recipe_v2;
        recipe_v2.push_back({ing_pan_id, "Pan Artesanal", 1, "pz"});
        recipe_v2.push_back({ing_jamon_id, "Jamón Supremo", 1, "pz"});
        TEST_CHECK(cat_svc->set_recipe(prod_sandwich_id, recipe_v2).is_success(), "Asignar Receta v2 (1x Pan, 1x Jamón)");

        // Verificar que AMBAS recetas (v1 y v2) se conservan en la base de datos para preservar el historial
        {
            auto prep = db.prepare("SELECT COUNT(*) FROM recipes WHERE product_id = ?;");
            TEST_CHECK(prep.is_success(), "Prepare select recipe count");
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, prod_sandwich_id);
            TEST_CHECK(stmt.step().is_success() && stmt.get_int64(0) == 2, "Deben existir exactamente 2 versiones de receta en historial");
        }

        account_svc->logout();

        // 6. Cajero 1 realiza Venta 2: DEBE descontar según Receta v2 (1 Pan, 1 Jamón), NO según v1
        TEST_CHECK(account_svc->login("cajero_01", cashier1_pwd).is_success(), "Login Cajero 1 para V2");
        TEST_CHECK(cart_svc->add_item(prod_sandwich_id, 1, "con mostaza").is_success(), "Agregar Sandwich v2");
        auto sale2_res = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(6000));
        TEST_CHECK(sale2_res.is_success(), "Confirmar Venta 2");
        sale2_folio = sale2_res.value().folio;

        // Comprobar existencias tras Venta 2: Pan de 8 a 7 (-1), Jamón de 5 a 4 (-1)
        TEST_CHECK(get_stock_db(ing_pan_id) == 7, "Stock Pan tras V2 debe ser 7 pz");
        TEST_CHECK(get_stock_db(ing_jamon_id) == 4, "Stock Jamón tras V2 debe ser 4 pz");

        // 7. PRUEBA DE IDEMPOTENCIA CONTEXTUAL Y SEGURIDAD:
        // Cajero 1 confirma Venta 3 con clave idempotente específica
        std::string test_idemp_key = "idemp-secure-user01-xyz";
        TEST_CHECK(cart_svc->add_item(prod_sandwich_id, 1, "para llevar").is_success(), "Agregar Sandwich V3");
        auto sale3_res = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), test_idemp_key);
        TEST_CHECK(sale3_res.is_success(), "Confirmar Venta 3 con clave idempotente");
        sale3_folio = sale3_res.value().folio;

        account_svc->logout();

        // Cajero 2 (usuario distinto) intenta confirmar o recuperar con la clave de Cajero 1 -> DEBE SER RECHAZADO CON FORBIDDEN
        TEST_CHECK(account_svc->login("cajero_02", cashier2_pwd).is_success(), "Login Cajero 2");
        TEST_CHECK(cart_svc->add_item(prod_sandwich_id, 1, "ajeno").is_success(), "Carrito de Cajero 2");
        auto sale_foreign_res = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(6000), test_idemp_key);
        TEST_CHECK(!sale_foreign_res.is_success() && sale_foreign_res.error_code() == ErrorCode::Forbidden,
                   "Confirmar venta con clave de idempotencia de otro usuario DEBE retornar Forbidden");
        account_svc->logout();

        // Cajero 1 cierra su turno de caja
        TEST_CHECK(account_svc->login("cajero_01", cashier1_pwd).is_success(), "Login Cajero 1");
        TEST_CHECK(shift_svc->close_shift(Money::from_cents(50000)).is_success(), "Cerrar turno Cajero 1");
        TEST_CHECK(!shift_svc->get_current_shift().is_success(), "Turno cerrado");

        // Cajero 1 reintenta confirmación con la misma clave idempotente TRAS cerrar el turno:
        // DEBE recuperar exitosamente la Venta 3 original sin error de ShiftClosed ni duplicación
        auto sale_retry_closed = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), test_idemp_key);
        TEST_CHECK(sale_retry_closed.is_success(), "Reintento de idempotencia tras cierre de turno debe recuperar la venta");
        TEST_CHECK(sale_retry_closed.value().folio == sale3_folio, "Folio recuperado debe coincidir con Venta 3");

        // 8. PRUEBA DE EDICIÓN DE LÍNEAS DE CARRITO Y REGLAS DE AGRUPACIÓN
        // Abrir nuevo turno para Cajero 1
        TEST_CHECK(shift_svc->open_shift("reg-01", Money::from_cents(20000)).is_success(), "Reabrir turno para edición de carrito");

        // Agregar dos líneas con notas distintas
        TEST_CHECK(cart_svc->add_item(prod_sandwich_id, 1, "sin azucar").is_success(), "Agregar línea 1");
        TEST_CHECK(cart_svc->add_item(prod_sandwich_id, 1, "con azucar").is_success(), "Agregar línea 2");

        auto cart_2lines = cart_svc->get_cart();
        TEST_CHECK(cart_2lines.is_success() && cart_2lines.value().items.size() == 2, "Carrito debe tener 2 líneas separadas");
        std::string line1_id = cart_2lines.value().items[0].line_id;
        std::string line2_id = cart_2lines.value().items[1].line_id;

        // Modificar cantidad de Línea 1 a 3
        auto cart_upd_qty = cart_svc->update_quantity(line1_id, 3);
        TEST_CHECK(cart_upd_qty.is_success() && cart_upd_qty.value().total_items == 4, "Total artículos debe ser 4 tras modificar cantidad");

        // Modificar nota de Línea 2 a "sin azucar" (igual que Línea 1): DEBEN FUSIONARSE EN UNA SOLA LÍNEA
        auto cart_upd_notes = cart_svc->update_item_notes(line2_id, "sin azucar");
        TEST_CHECK(cart_upd_notes.is_success(), "Actualizar nota de línea 2");
        TEST_CHECK(cart_upd_notes.value().items.size() == 1, "Al igualar la nota, las dos líneas deben agruparse en 1 sola");
        TEST_CHECK(cart_upd_notes.value().items[0].quantity == 4, "La línea fusionada debe tener cantidad 4");
        TEST_CHECK(cart_upd_notes.value().items[0].notes == "sin azucar", "La nota debe ser 'sin azucar'");

        // Modificar cantidad a 0 -> elimina la línea
        auto cart_empty = cart_svc->update_quantity(cart_upd_notes.value().items[0].line_id, 0);
        TEST_CHECK(cart_empty.is_success() && cart_empty.value().items.empty(), "Cantidad 0 debe vaciar el carrito");

        // 9. PRUEBA DE HISTORIAL DE TICKETS
        auto history_res = sale_svc->get_recent_tickets(10);
        TEST_CHECK(history_res.is_success(), "get_recent_tickets debe ser exitoso");
        TEST_CHECK(history_res.value().size() >= 3, "Historial debe contener al menos las 3 ventas realizadas");

        bool found_v1 = false, found_v2 = false, found_v3 = false;
        for (const auto& t : history_res.value()) {
            if (t.folio == sale1_folio) found_v1 = true;
            if (t.folio == sale2_folio) found_v2 = true;
            if (t.folio == sale3_folio) found_v3 = true;
        }
        TEST_CHECK(found_v1 && found_v2 && found_v3, "El historial debe contener los folios V1, V2 y V3");
    }

    // 10. REAPERTURA DE BASE DE DATOS: VERIFICAR PERSISTENCIA EXACTA DE HISTORIAL Y TICKETS
    {
        SqliteDatabase db_reopen;
        TEST_CHECK(db_reopen.open(db_file).is_success(), "Reabrir SQLite para verificar persistencia de tickets");

        auto account_svc2 = std::make_shared<SqliteAccountService>(db_reopen);
        auto cat_svc2 = std::make_shared<SqliteCatalogService>(db_reopen, account_svc2);
        auto shift_svc2 = std::make_shared<SqliteShiftService>(db_reopen, account_svc2);
        auto cart_svc2 = std::make_shared<SqliteCartService>(db_reopen, account_svc2, cat_svc2);
        auto sale_svc2 = std::make_shared<SqliteSaleService>(db_reopen, account_svc2, cart_svc2, shift_svc2);

        TEST_CHECK(account_svc2->login("cajero_01", cashier1_pwd).is_success(), "Login Cajero 1 en base reabierta");

        // Historial persistido
        auto history2 = sale_svc2->get_recent_tickets(10);
        TEST_CHECK(history2.is_success() && history2.value().size() >= 3, "Historial persiste íntegramente tras reapertura");

        // Recuperar Ticket 1 inmutable
        auto ticket1 = sale_svc2->get_ticket(sale1_folio);
        TEST_CHECK(ticket1.is_success(), "Recuperar Ticket 1 tras reapertura");
        TEST_CHECK(ticket1.value().items.size() == 1, "Ticket 1 tiene 1 item");
        TEST_CHECK(ticket1.value().items[0].notes == "sin mayonesa", "Nota de Ticket 1 inmutable preservada");
        TEST_CHECK(ticket1.value().cashier_name == "Cajero Uno", "Nombre del cajero preservado en Ticket 1");

        // Recuperar Ticket 3 inmutable
        auto ticket3 = sale_svc2->get_ticket(sale3_folio);
        TEST_CHECK(ticket3.is_success(), "Recuperar Ticket 3 tras reapertura");
        TEST_CHECK(ticket3.value().items[0].notes == "para llevar", "Nota de Ticket 3 inmutable preservada");
    }

    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }
}

void test_i2_integrity_historical_tickets_and_reprint_auth() {
    std::cout << "[TEST] I2: Integridad del carrito (fusión por precio, rollback), tickets históricos inmutables y autorización pos.reprint..." << std::endl;

    const std::string db_file = "test_i2_integrity_historical.db";
    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }

    fs::path mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Debe encontrar directorio de migraciones");

    const std::string owner_pwd = generate_random_secret(16);
    const std::string trainee_pwd = generate_random_secret(16);

    {
        SqliteDatabase db;
        TEST_CHECK(db.open(db_file).is_success(), "Apertura BD I2 Integridad");
        MigrationRunner runner(db);
        TEST_CHECK(runner.run_pending_migrations_from_dir(mig_dir).is_success(), "Ejecutar migraciones hasta 007");

        auto account_svc = std::make_shared<SqliteAccountService>(db);
        auto cat_svc = std::make_shared<SqliteCatalogService>(db, account_svc);
        auto inv_svc = std::make_shared<SqliteInventoryService>(db, account_svc);
        auto shift_svc = std::make_shared<SqliteShiftService>(db, account_svc);
        auto cart_svc = std::make_shared<SqliteCartService>(db, account_svc, cat_svc);
        auto sale_svc = std::make_shared<SqliteSaleService>(db, account_svc, cart_svc, shift_svc);

        // 1. Alta de Dueño
        TEST_CHECK(account_svc->create_initial_owner("owner_test", "Dueno Admin", owner_pwd, "admin@cafeteria-itch.edu.mx").is_success(), "Alta dueno");

        // 2. Crear rol 'role-trainee' que tiene pos.sell, pos.notes, shift.operate PERO NO pos.reprint
        TEST_CHECK(db.execute("INSERT INTO roles (id, name, description, is_system) VALUES ('role-trainee', 'Cajero Trainee', 'Sin permiso reprint', 0);").is_success(), "Insert rol trainee");
        TEST_CHECK(db.execute("INSERT INTO role_permissions (role_id, permission_id) VALUES ('role-trainee', 'pos.sell');").is_success(), "Permiso pos.sell");
        TEST_CHECK(db.execute("INSERT INTO role_permissions (role_id, permission_id) VALUES ('role-trainee', 'pos.notes');").is_success(), "Permiso pos.notes");
        TEST_CHECK(db.execute("INSERT INTO role_permissions (role_id, permission_id) VALUES ('role-trainee', 'shift.operate');").is_success(), "Permiso shift.operate");

        // 3. Crear usuario trainee con display_name original
        CreateUserDto trainee_dto;
        trainee_dto.username = "cajero_trainee";
        trainee_dto.display_name = "Cajero Original Snapshot";
        trainee_dto.email = "trainee@cafeteria-itch.edu.mx";
        trainee_dto.raw_password = trainee_pwd;
        trainee_dto.role_id = "role-trainee";
        auto create_trainee = account_svc->create_user(trainee_dto);
        TEST_CHECK(create_trainee.is_success(), "Crear usuario cajero trainee");
        std::string trainee_id = create_trainee.value().id;

        // 4. Crear producto para pruebas
        CreateProductDto prod;
        prod.sku = "INTEG-PROD";
        prod.name = "Sandwich Gourmet Especial";
        prod.category = "Alimentos";
        prod.price = Money::from_cents(5000); // $50.00
        prod.is_prepared = false;
        auto prod_res = cat_svc->create_product(prod);
        TEST_CHECK(prod_res.is_success(), "Crear producto");
        std::string prod_id = prod_res.value().id;

        // 5. Iniciar sesión como trainee y abrir turno
        TEST_CHECK(account_svc->login("cajero_trainee", trainee_pwd).is_success(), "Login trainee");
        TEST_CHECK(shift_svc->open_shift("reg-01", Money::from_cents(50000)).is_success(), "Abrir turno trainee");

        // 6. PRUEBAS DE FUSIÓN EN CARRITO POR PRECIO Y NOTAS
        // 6.1 Agregar línea 1: 1 pz, "sin picante", $50.00
        auto add1 = cart_svc->add_item(prod_id, 1, "sin picante");
        TEST_CHECK(add1.is_success() && add1.value().items.size() == 1, "Agregar línea 1");
        std::string line1_id = add1.value().items[0].line_id;

        // 6.2 Agregar línea 2: 2 pz, "con salsa", $50.00 -> Líneas separadas por notas distintas
        auto add2 = cart_svc->add_item(prod_id, 2, "con salsa");
        TEST_CHECK(add2.is_success() && add2.value().items.size() == 2, "Agregar línea 2 con nota distinta");
        std::string line2_id;
        for (const auto& it : add2.value().items) {
            if (it.line_id != line1_id) line2_id = it.line_id;
        }

        // 6.3 Modificar precio de la línea 2 directamente en BD a $40.00 para simular precios dispares
        auto upd_price = db.prepare("UPDATE cart_items SET unit_price_cents = 4000 WHERE id = ?;");
        TEST_CHECK(upd_price.is_success(), "Prepare update price");
        auto stmt_p = std::move(upd_price.value());
        stmt_p.bind_text(1, line2_id);
        TEST_CHECK(stmt_p.step().is_success(), "Precio de línea 2 alterado a 4000");

        // Intentar fusionar cambiando nota de línea 2 a "sin picante":
        // Como los precios difieren ($50.00 vs $40.00), NO deben fusionarse; deben permanecer como 2 líneas separadas
        auto fuse_diff_price = cart_svc->update_item_notes(line2_id, "sin picante");
        TEST_CHECK(fuse_diff_price.is_success(), "Actualizar nota de línea 2 a 'sin picante'");
        TEST_CHECK(fuse_diff_price.value().items.size() == 2, "Líneas con precios unitarios diferentes NO deben fusionarse");

        // 6.4 Ahora restaurar precio de línea 2 a 5000 (mismo precio unitario)
        auto upd_price2 = db.prepare("UPDATE cart_items SET unit_price_cents = 5000 WHERE id = ?;");
        auto stmt_p2 = std::move(upd_price2.value());
        stmt_p2.bind_text(1, line2_id);
        stmt_p2.step();

        // Ahora cambiar la nota de línea 2: primero a "temporal" y luego a "sin picante"
        cart_svc->update_item_notes(line2_id, "temporal");
        auto fuse_same_price = cart_svc->update_item_notes(line2_id, "sin picante");
        TEST_CHECK(fuse_same_price.is_success(), "Fusionar líneas con mismo producto, precio y nota");
        TEST_CHECK(fuse_same_price.value().items.size() == 1, "Debe fusionarse en 1 sola línea");
        TEST_CHECK(fuse_same_price.value().items[0].quantity == 3, "Cantidad fusionada debe ser 1 + 2 = 3");

        // 6.5 Validación de desbordamientos: cantidad excesiva
        auto ovf_qty = cart_svc->update_quantity(fuse_same_price.value().items[0].line_id, 2000000);
        TEST_CHECK(!ovf_qty.is_success() && ovf_qty.error_code() == ErrorCode::InvalidInput, "Cantidad > 1,000,000 debe fallar");

        // 7. CONFIRMAR VENTA V1 Y VALIDAR SNAPSHOT DE CAJERO
        auto sale1_res = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(20000)); // Total 3x$50 = $150.00
        TEST_CHECK(sale1_res.is_success(), "Confirmar venta V1");
        std::string folio1 = sale1_res.value().folio;

        // Vendedor trainee que acaba de confirmar la venta puede obtener su propio ticket recién confirmado
        auto tkt_trainee_own = sale_svc->get_ticket(folio1);
        TEST_CHECK(tkt_trainee_own.is_success(), "Vendedor puede consultar el ticket de su venta recién confirmada sin pos.reprint");
        TEST_CHECK(tkt_trainee_own.value().cashier_name == "Cajero Original Snapshot", "Cajero en ticket coincide con display_name al confirmar");

        // 8. CAMBIAR NOMBRE DE LA CUENTA EN USERS (Simular cambio de nombre posterior)
        auto upd_user_name = db.prepare("UPDATE users SET display_name = 'Cajero Renombrado Posterior' WHERE id = ?;");
        TEST_CHECK(upd_user_name.is_success(), "Prepare update user display_name");
        auto stmt_u = std::move(upd_user_name.value());
        stmt_u.bind_text(1, trainee_id);
        TEST_CHECK(stmt_u.step().is_success(), "display_name actualizado en users");

        // Agregar otro producto y confirmar una segunda venta V2 para desplazar last_confirmed_folio_
        cart_svc->add_item(prod_id, 1, "");
        auto sale2_res = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(5000));
        TEST_CHECK(sale2_res.is_success(), "Confirmar venta V2");
        std::string folio2 = sale2_res.value().folio;

        // Ahora last_confirmed_folio_ es folio2.
        // El cajero trainee (sin pos.reprint) intenta consultar folio1 (venta anterior): DEBE FALLAR
        auto tkt_trainee_old = sale_svc->get_ticket(folio1);
        TEST_CHECK(!tkt_trainee_old.is_success(), "Cajero sin pos.reprint NO puede consultar tickets de ventas pasadas");
        TEST_CHECK(tkt_trainee_old.error_code() == ErrorCode::Forbidden, "Error debe ser Forbidden por falta de pos.reprint");

        // El cajero trainee intenta consultar historial recent tickets: DEBE FALLAR
        auto hist_trainee = sale_svc->get_recent_tickets(10);
        TEST_CHECK(!hist_trainee.is_success(), "Cajero sin pos.reprint NO puede consultar historial de tickets");
        TEST_CHECK(hist_trainee.error_code() == ErrorCode::Forbidden, "Error debe ser Forbidden en historial");

        // 9. AUTORIZACIÓN: Dueño (con pos.reprint) consulta tickets históricos
        TEST_CHECK(account_svc->login("owner_test", owner_pwd).is_success(), "Login como Dueño");

        auto tkt_owner_v1 = sale_svc->get_ticket(folio1);
        TEST_CHECK(tkt_owner_v1.is_success(), "Dueño con pos.reprint puede consultar ticket V1");
        // ¡INMUTABILIDAD COMPROBADA!: El ticket V1 DEBE conservar "Cajero Original Snapshot" y NO "Cajero Renombrado Posterior"
        TEST_CHECK(tkt_owner_v1.value().cashier_name == "Cajero Original Snapshot", "Ticket V1 inmutable conserva el snapshot original del cajero");

        auto hist_owner = sale_svc->get_recent_tickets(10);
        TEST_CHECK(hist_owner.is_success(), "Dueño puede consultar historial");
        bool snapshot_preserved_in_history = false;
        for (const auto& h : hist_owner.value()) {
            if (h.folio == folio1 && h.cashier_name == "Cajero Original Snapshot") {
                snapshot_preserved_in_history = true;
            }
        }
        TEST_CHECK(snapshot_preserved_in_history, "Historial conserva snapshot del cajero original para folio V1");

        // 10. COMPROBAR VENTAS ANTIGUAS CON CASHIER_NAME NULL (MIGRACIONES PREVIAS)
        // Insertar una venta previa con cashier_name NULL y verificar fallback COALESCE sin inventar datos
        std::string valid_shift_id;
        {
            auto cur_s = shift_svc->get_current_shift();
            TEST_CHECK(cur_s.is_success(), "Obtener shift_id activo para venta legada");
            valid_shift_id = cur_s.value().id;
        }

        std::string old_sale_id = "sale-legacy-001";
        std::string old_folio = "V-00001";
        auto prep_leg = db.prepare("INSERT INTO sales (id, folio, shift_id, user_id, total_cents, created_at, cashier_name) VALUES (?, ?, ?, ?, 1000, '2026-09-01 10:00:00', NULL);");
        TEST_CHECK(prep_leg.is_success(), "Prepare legacy sale");
        auto stmt_leg = std::move(prep_leg.value());
        stmt_leg.bind_text(1, old_sale_id);
        stmt_leg.bind_text(2, old_folio);
        stmt_leg.bind_text(3, valid_shift_id);
        stmt_leg.bind_text(4, trainee_id);
        TEST_CHECK(stmt_leg.step().is_success(), "Insertar venta legada con shift_id válido");

        auto tkt_leg = sale_svc->get_ticket(old_folio);
        TEST_CHECK(tkt_leg.is_success(), "Recuperar venta previa con cashier_name NULL");
        std::string expected_legacy_cashier = "Nombre histórico no disponible (" + trainee_id + ")";
        TEST_CHECK(tkt_leg.value().cashier_name == expected_legacy_cashier, "Venta previa muestra nombre histórico no disponible con ID estable");

        // Comprobar también en get_recent_tickets
        auto hist_leg = sale_svc->get_recent_tickets(10);
        TEST_CHECK(hist_leg.is_success(), "Consultar historial reciente para venta legada");
        bool legacy_found_in_hist = false;
        for (const auto& h : hist_leg.value()) {
            if (h.folio == old_folio) {
                TEST_CHECK(h.cashier_name == expected_legacy_cashier, "Historial muestra nombre histórico no disponible con ID estable");
                legacy_found_in_hist = true;
            }
        }
        TEST_CHECK(legacy_found_in_hist, "Venta legada encontrada en historial reciente");

        // Renombrar la cuenta nuevamente en users y verificar que NO altera tickets pasados ni ventas legadas
        auto upd_user_again = db.prepare("UPDATE users SET display_name = 'Cajero Tercer Renombramiento' WHERE id = ?;");
        TEST_CHECK(upd_user_again.is_success(), "Prepare update user display_name again");
        auto stmt_u_again = std::move(upd_user_again.value());
        stmt_u_again.bind_text(1, trainee_id);
        TEST_CHECK(stmt_u_again.step().is_success(), "Segundo cambio de display_name en users");

        // Venta legada sigue mostrando Nombre histórico no disponible (ID)
        auto tkt_leg_after = sale_svc->get_ticket(old_folio);
        TEST_CHECK(tkt_leg_after.is_success(), "Consultar venta legada tras renombrar cuenta");
        TEST_CHECK(tkt_leg_after.value().cashier_name == expected_legacy_cashier, "Renombrar cuenta no altera nombre de venta legada");

        // Venta V1 (con snapshot) sigue mostrando su snapshot original
        auto tkt_v1_after = sale_svc->get_ticket(folio1);
        TEST_CHECK(tkt_v1_after.is_success(), "Consultar venta V1 tras renombrar cuenta");
        TEST_CHECK(tkt_v1_after.value().cashier_name == "Cajero Original Snapshot", "Renombrar cuenta no altera snapshot inmutable de venta V1");

        // 11. FORMATEO DE TICKET Y AJUSTE AL ANCHO IMPRIMIBLE (40 COLUMNAS)
        TicketDto long_ticket;
        long_ticket.folio = "V-99999";
        long_ticket.business_name = "Cafetería Universitaria del Instituto Tecnológico de Chihuahua II"; // > 40 chars
        long_ticket.created_at = "2026-09-30 20:00:00";
        long_ticket.cashier_name = "Nombre histórico no disponible (usr-super-long-identifier-12345)"; // > 40 chars
        TicketLineItemDto long_item;
        long_item.product_name = "Torta Super Especial con Doble Carne, Queso y Aguacate"; // > 40 chars
        long_item.quantity = 2;
        long_item.unit_price = Money::from_cents(850000000); // Gran importe ($8,500,000.00)
        long_item.subtotal = Money::from_cents(1700000000); // $17,000,000.00 (>10 chars)
        long_item.notes = "con extra mayonesa bien fria, salsa roja muy picante en recipiente aparte y pan bien dorado sin ajonjoli"; // > 100 chars
        long_ticket.items.push_back(long_item);
        long_ticket.total = Money::from_cents(1700000000);
        long_ticket.subtotal = long_ticket.total;
        long_ticket.cash_received = Money::from_cents(2000000000); // $20,000,000.00
        long_ticket.change = Money::from_cents(300000000); // $3,000,000.00

        std::string formatted_ticket = long_ticket.to_formatted_text();
        TEST_CHECK(!formatted_ticket.empty(), "Ticket formateado no vacío");
        TEST_CHECK(formatted_ticket.find("...") == std::string::npos, "El nombre de producto largo no debe estar truncado con '...'");

        // Verificar que ninguna línea exceda los 40 caracteres de ancho del ticket térmico
        std::istringstream iss(formatted_ticket);
        std::string line;
        bool all_lines_fit_width = true;
        while (std::getline(iss, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            size_t w = TicketDto::utf8_column_width(line);
            if (w > 40) {
                all_lines_fit_width = false;
                std::cerr << "LINE OVERFLOW (" << w << " cols): [" << line << "]" << std::endl;
            }
        }
        TEST_CHECK(all_lines_fit_width, "Todas las líneas del ticket respetan el ancho imprimible de 40 columnas");
    }

    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }
}

void test_i2_cart_atomic_rollback_and_overflow_integrity() {
    std::cout << "[TEST] Storage I2: atomicidad en carrito, rollback de fusión/updated_at y desbordamientos..." << std::endl;

    const std::string db_file = "test_cart_robustness.db";
    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }

    fs::path migration_dir = find_migrations_dir();
    TEST_CHECK(!migration_dir.empty(), "Debe localizar el directorio de migraciones");

    {
        SqliteDatabase db;
        TEST_CHECK(db.open(db_file).is_success(), "Abrir SQLite para pruebas de carrito");

        MigrationRunner runner(db);
        TEST_CHECK(runner.run_pending_migrations_from_dir(migration_dir).is_success(), "Aplicar todas las migraciones");

        auto account_svc = std::make_shared<SqliteAccountService>(db);
        auto cat_svc = std::make_shared<SqliteCatalogService>(db, account_svc);
        auto cart_svc = std::make_shared<SqliteCartService>(db, account_svc, cat_svc);
        auto shift_svc = std::make_shared<SqliteShiftService>(db, account_svc);

        // Crear Dueño
        std::string owner_pwd = generate_random_secret(18);
        TEST_CHECK(account_svc->create_initial_owner("owner_cart", "Dueno Pruebas Carrito", owner_pwd, "owner_cart@cafeteria-itch.edu.mx").is_success(), "Bootstrap dueno");

        TEST_CHECK(account_svc->login("owner_cart", owner_pwd).is_success(), "Login dueño");
        TEST_CHECK(shift_svc->open_shift("reg-01", Money::from_cents(50000)).is_success(), "Abrir turno");

        // Crear productos
        CreateProductDto prod1;
        prod1.sku = "PROD-ROLL-1";
        prod1.name = "Cafe Americano";
        prod1.category = "Bebidas";
        prod1.price = Money::from_cents(3500); // $35.00
        prod1.is_prepared = false;
        auto p1_res = cat_svc->create_product(prod1);
        TEST_CHECK(p1_res.is_success(), "Crear Cafe Americano");
        std::string p1_id = p1_res.value().id;

        CreateProductDto prod2;
        prod2.sku = "PROD-ROLL-2";
        prod2.name = "Pan Dulce Concha";
        prod2.category = "Panaderia";
        prod2.price = Money::from_cents(2000); // $20.00
        prod2.is_prepared = false;
        auto p2_res = cat_svc->create_product(prod2);
        TEST_CHECK(p2_res.is_success(), "Crear Pan Dulce Concha");
        std::string p2_id = p2_res.value().id;

        // -------------------------------------------------------------
        // CASO 1: Inducir fallo en DELETE durante fusión (después de UPDATE)
        // -------------------------------------------------------------
        // Agregar dos líneas compatibles para fusionar:
        // Línea A: 2 pz, nota "sin azucar"
        auto add_a = cart_svc->add_item(p1_id, 2, "sin azucar");
        TEST_CHECK(add_a.is_success() && add_a.value().items.size() == 1, "Agregar línea A");
        std::string line_a_id = add_a.value().items[0].line_id;

        // Línea B: 3 pz, nota "con canela" -> nota distinta, queda en línea separada
        auto add_b = cart_svc->add_item(p1_id, 3, "con canela");
        TEST_CHECK(add_b.is_success() && add_b.value().items.size() == 2, "Agregar línea B");
        std::string line_b_id;
        for (const auto& it : add_b.value().items) {
            if (it.line_id != line_a_id) line_b_id = it.line_id;
        }
        TEST_CHECK(!line_b_id.empty(), "Obtener ID de línea B");

        // Crear trigger en SQLite que lance error específicamente al intentar DELETE en cart_items
        auto trig_del = db.execute("CREATE TRIGGER trigger_fail_delete BEFORE DELETE ON cart_items BEGIN SELECT RAISE(ABORT, 'Fallo inducido en DELETE durante fusion'); END;");
        TEST_CHECK(trig_del.is_success(), "Crear trigger que induce fallo en DELETE");

        // Intentar fusionar Línea B con Línea A cambiando la nota de B a "sin azucar"
        // La fusión ejecuta UPDATE en línea A (cantidad -> 5) y luego DELETE en línea B.
        // El trigger aborta el DELETE. La transacción completa DEBE hacer rollback!
        auto fuse_fail = cart_svc->update_item_notes(line_b_id, "sin azucar");
        TEST_CHECK(!fuse_fail.is_success(), "La fusion con DELETE abortado DEBE fallar");

        // Eliminar el trigger
        TEST_CHECK(db.execute("DROP TRIGGER trigger_fail_delete;").is_success(), "Eliminar trigger de fallo en DELETE");

        // Verificar atomicidad estricta y conservación del borrador:
        // Ambas líneas DEBEN permanecer con sus cantidades y notas originales exactas!
        auto cart_after_fail = cart_svc->get_cart();
        TEST_CHECK(cart_after_fail.is_success(), "Obtener carrito tras fallo inducido");
        TEST_CHECK(cart_after_fail.value().items.size() == 2, "El borrador DEBE conservar exactamente 2 lineas tras rollback");
        for (const auto& it : cart_after_fail.value().items) {
            if (it.line_id == line_a_id) {
                TEST_CHECK(it.quantity == 2, "Linea A debe conservar cantidad original 2 (no 5)");
                TEST_CHECK(it.notes == "sin azucar", "Linea A conserva nota original");
            } else if (it.line_id == line_b_id) {
                TEST_CHECK(it.quantity == 3, "Linea B debe conservar cantidad original 3 (no eliminada)");
                TEST_CHECK(it.notes == "con canela", "Linea B conserva nota original 'con canela'");
            }
        }
        TEST_CHECK(cart_after_fail.value().total_items == 5, "Total de items en carrito permanece en 5");
        TEST_CHECK(cart_after_fail.value().subtotal == Money::from_cents(17500), "Subtotal en carrito permanece en $175.00");

        // Vaciar carrito para siguiente prueba
        TEST_CHECK(cart_svc->clear_cart().is_success(), "Vaciar carrito");

        // -------------------------------------------------------------
        // CASO 2: Inducir fallo en carts.updated_at y comprobar que no quedan cambios parciales
        // -------------------------------------------------------------
        // Agregar 1 item inicial
        auto add_init = cart_svc->add_item(p2_id, 1, "");
        TEST_CHECK(add_init.is_success() && add_init.value().items.size() == 1, "Agregar item inicial");
        std::string line_init_id = add_init.value().items[0].line_id;

        // Crear trigger que falle al actualizar carts.updated_at
        auto trig_upd = db.execute("CREATE TRIGGER trigger_fail_carts_upd BEFORE UPDATE OF updated_at ON carts BEGIN SELECT RAISE(ABORT, 'Fallo inducido en carts.updated_at'); END;");
        TEST_CHECK(trig_upd.is_success(), "Crear trigger que induce fallo en carts.updated_at");

        // 2.1 Intentar agregar un nuevo producto: debe fallar por updated_at y no dejar el producto insertado
        auto add_blocked = cart_svc->add_item(p1_id, 4, "");
        TEST_CHECK(!add_blocked.is_success(), "add_item debe fallar si carts.updated_at falla");

        // 2.2 Intentar modificar cantidad de la línea existente: debe fallar y no alterar la cantidad
        auto upd_blocked = cart_svc->update_quantity(line_init_id, 10);
        TEST_CHECK(!upd_blocked.is_success(), "update_quantity debe fallar si carts.updated_at falla");

        // 2.3 Intentar eliminar la línea: debe fallar y no eliminar la línea
        auto rem_blocked = cart_svc->remove_item(line_init_id);
        TEST_CHECK(!rem_blocked.is_success(), "remove_item debe fallar si carts.updated_at falla");

        // Eliminar el trigger
        TEST_CHECK(db.execute("DROP TRIGGER trigger_fail_carts_upd;").is_success(), "Eliminar trigger de fallo en carts.updated_at");

        // Verificar que el borrador NO sufrió ningún cambio parcial:
        auto cart_after_upd_fail = cart_svc->get_cart();
        TEST_CHECK(cart_after_upd_fail.is_success(), "Obtener carrito tras fallos de updated_at");
        TEST_CHECK(cart_after_upd_fail.value().items.size() == 1, "Debe haber exactamente 1 linea en carrito");
        TEST_CHECK(cart_after_upd_fail.value().items[0].line_id == line_init_id, "Es la linea inicial");
        TEST_CHECK(cart_after_upd_fail.value().items[0].quantity == 1, "La cantidad permanece en 1 (no cambio a 10)");
        TEST_CHECK(cart_after_upd_fail.value().total_items == 1, "Total de items permanece en 1");

        // Vaciar carrito
        TEST_CHECK(cart_svc->clear_cart().is_success(), "Vaciar carrito");

        // -------------------------------------------------------------
        // CASO 3: Desbordamiento de subtotal y total acumulado con cantidades/precios válidos individualmente
        // -------------------------------------------------------------
        // 3.1 Desbordamiento de subtotal de línea:
        // Cantidad válida: 1,000,000 (<= 1,000,000)
        // Precio individual válido (cabe en int64_t): 20,000,000,000,000 centavos ($200,000,000,000.00)
        // Producto 1,000,000 * 20,000,000,000,000 = 20,000,000,000,000,000,000 > INT64_MAX (~9.22e18)
        std::string huge_prod_id = "prod-huge-subtotal";
        auto prep_huge = db.prepare("INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at) VALUES (?, 'SKU-HUGE', NULL, 'Producto Gigante', 'Especial', 20000000000000, 0, 1, '2026-09-30 00:00:00');");
        TEST_CHECK(prep_huge.is_success(), "Prepare huge product");
        auto stmt_h = std::move(prep_huge.value());
        stmt_h.bind_text(1, huge_prod_id);
        TEST_CHECK(stmt_h.step().is_success(), "Insertar producto con precio grande");

        auto add_ovf_subtotal = cart_svc->add_item(huge_prod_id, 1000000, "");
        TEST_CHECK(!add_ovf_subtotal.is_success(), "add_item con subtotal desbordado DEBE fallar");
        TEST_CHECK(add_ovf_subtotal.error_code() == ErrorCode::InvalidInput, "Error debe ser InvalidInput");

        // Verificar que el borrador permaneció vacío
        auto cart_chk1 = cart_svc->get_cart();
        TEST_CHECK(cart_chk1.is_success() && cart_chk1.value().items.empty(), "El borrador permanece vacio tras rechazar desbordamiento de subtotal");

        // 3.2 Desbordamiento de total acumulado del carrito con subtotales individualmente válidos:
        // Producto con precio 5,000,000,000,000 centavos
        // Cantidad 1,000,000 -> subtotal por línea = 5,000,000,000,000,000,000 (5e18 < INT64_MAX, cabe individualmente!)
        std::string accum_prod_id = "prod-accum-ovf";
        auto prep_acc = db.prepare("INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at) VALUES (?, 'SKU-ACC', NULL, 'Producto Acumulado', 'Especial', 5000000000000, 0, 1, '2026-09-30 00:00:00');");
        TEST_CHECK(prep_acc.is_success(), "Prepare accum overflow product");
        auto stmt_acc = std::move(prep_acc.value());
        stmt_acc.bind_text(1, accum_prod_id);
        TEST_CHECK(stmt_acc.step().is_success(), "Insertar producto para acumulado grande");

        // Agregar línea 1: 1,000,000 pz, nota "linea 1" -> subtotal 5e18. Debe tener éxito.
        auto add_acc1 = cart_svc->add_item(accum_prod_id, 1000000, "linea 1");
        TEST_CHECK(add_acc1.is_success() && add_acc1.value().items.size() == 1, "Agregar linea 1 con subtotal 5e18");

        // Intentar agregar línea 2: 1,000,000 pz, nota "linea 2" -> subtotal individual 5e18 (válido).
        // Pero total acumulado = 5e18 + 5e18 = 10e18 > INT64_MAX! DEBE FALLAR y hacer rollback!
        auto add_acc2 = cart_svc->add_item(accum_prod_id, 1000000, "linea 2");
        TEST_CHECK(!add_acc2.is_success(), "add_item con total acumulado desbordado DEBE fallar");
        TEST_CHECK(add_acc2.error_code() == ErrorCode::InvalidInput, "Error debe ser InvalidInput");

        // Verificar que el borrador conserva ÚNICAMENTE la línea 1 y no quedó modificado:
        auto cart_chk2 = cart_svc->get_cart();
        TEST_CHECK(cart_chk2.is_success(), "Consultar carrito tras rechazar desbordamiento acumulado");
        TEST_CHECK(cart_chk2.value().items.size() == 1, "El carrito solo debe contener la linea 1");
        TEST_CHECK(cart_chk2.value().items[0].notes == "linea 1", "La linea 1 permanece intacta");
        TEST_CHECK(cart_chk2.value().items[0].quantity == 1000000, "Cantidad de linea 1 permanece en 1,000,000");

        // Vaciar carrito
        TEST_CHECK(cart_svc->clear_cart().is_success(), "Vaciar carrito");

        // -------------------------------------------------------------
        // CASO 4: Fusión compatible vs precios distintos y conservación del borrador
        // -------------------------------------------------------------
        // 4.1 Agregar línea A: 1 pz, $35.00, nota "tibio"
        auto l_a = cart_svc->add_item(p1_id, 1, "tibio");
        TEST_CHECK(l_a.is_success() && l_a.value().items.size() == 1, "Agregar tibio");
        std::string la_id = l_a.value().items[0].line_id;

        // 4.2 Agregar línea B: 2 pz, $35.00, nota "muy caliente"
        auto l_b = cart_svc->add_item(p1_id, 2, "muy caliente");
        TEST_CHECK(l_b.is_success() && l_b.value().items.size() == 2, "Agregar muy caliente");
        std::string lb_id;
        for (const auto& it : l_b.value().items) {
            if (it.line_id != la_id) lb_id = it.line_id;
        }

        // 4.3 Modificar precio de la línea B en BD a $30.00 para simular precios distintos
        auto upd_pr = db.prepare("UPDATE cart_items SET unit_price_cents = 3000 WHERE id = ?;");
        TEST_CHECK(upd_pr.is_success(), "Prepare update price línea B");
        auto stmt_pr = std::move(upd_pr.value());
        stmt_pr.bind_text(1, lb_id);
        TEST_CHECK(stmt_pr.step().is_success(), "Precio línea B alterado");

        // Intentar fusionar cambiando nota de B a "tibio": Precios distintos -> NO se fusionan
        auto fuse_diff = cart_svc->update_item_notes(lb_id, "tibio");
        TEST_CHECK(fuse_diff.is_success(), "Cambio de nota exitoso");
        TEST_CHECK(fuse_diff.value().items.size() == 2, "Líneas con precios distintos NO deben fusionarse");

        // Restaurar precio de línea B a 3500 (mismo precio unitario)
        auto upd_pr2 = db.prepare("UPDATE cart_items SET unit_price_cents = 3500 WHERE id = ?;");
        auto stmt_pr2 = std::move(upd_pr2.value());
        stmt_pr2.bind_text(1, lb_id);
        stmt_pr2.step();

        // Cambiar nota de línea B a "temporal" y luego a "tibio":
        // Coincide producto, nota y precio unitario ($35.00) -> DEBEN fusionarse en 1 línea con 3 pz
        cart_svc->update_item_notes(lb_id, "temporal");
        auto fuse_ok = cart_svc->update_item_notes(lb_id, "tibio");
        TEST_CHECK(fuse_ok.is_success(), "Fusión compatible exitosa");
        TEST_CHECK(fuse_ok.value().items.size() == 1, "Debe quedar exactamente 1 linea fusionada");
        TEST_CHECK(fuse_ok.value().items[0].quantity == 3, "Cantidad fusionada debe ser 1 + 2 = 3");
        TEST_CHECK(fuse_ok.value().items[0].notes == "tibio", "Nota de linea fusionada es 'tibio'");

        // -------------------------------------------------------------
        // CASO 5: Propagación de fallo en bind_text/bind_int64 y rollback estricto
        // -------------------------------------------------------------
        auto cart_before_bind = cart_svc->get_cart();
        TEST_CHECK(cart_before_bind.is_success() && cart_before_bind.value().items.size() == 1, "Estado previo para prueba de bind");
        std::string line_pre_id = cart_before_bind.value().items[0].line_id;

        // Activar fallo simulado en bind en SqliteStatement
        SqliteStatement::set_simulate_bind_failure(true);

        // 5.1 Intentar add_item con bind fallido: DEBE fallar inmediatamente con StorageFailure
        auto add_bind_fail = cart_svc->add_item(p2_id, 2, "para llevar");
        TEST_CHECK(!add_bind_fail.is_success(), "add_item con fallo de bind DEBE fallar");
        TEST_CHECK(add_bind_fail.error_code() == ErrorCode::StorageFailure, "Error debe ser StorageFailure");

        // 5.2 Intentar update_quantity con bind fallido: DEBE fallar con StorageFailure
        auto upd_q_fail = cart_svc->update_quantity(line_pre_id, 10);
        TEST_CHECK(!upd_q_fail.is_success(), "update_quantity con fallo de bind DEBE fallar");
        TEST_CHECK(upd_q_fail.error_code() == ErrorCode::StorageFailure, "Error debe ser StorageFailure");

        // 5.3 Intentar update_item_notes con bind fallido: DEBE fallar con StorageFailure
        auto upd_n_fail = cart_svc->update_item_notes(line_pre_id, "bien frio");
        TEST_CHECK(!upd_n_fail.is_success(), "update_item_notes con fallo de bind DEBE fallar");
        TEST_CHECK(upd_n_fail.error_code() == ErrorCode::StorageFailure, "Error debe ser StorageFailure");

        // Desactivar simulación de fallo de bind
        SqliteStatement::set_simulate_bind_failure(false);

        // 5.4 Comprobar que el borrador del carrito quedó 100% IDÉNTICO tras los fallos abortados
        auto cart_after_bind = cart_svc->get_cart();
        TEST_CHECK(cart_after_bind.is_success(), "Consultar carrito tras desactivar fallo de bind");
        TEST_CHECK(cart_after_bind.value().items.size() == 1, "El carrito debe conservar exactamente 1 línea");
        TEST_CHECK(cart_after_bind.value().items[0].line_id == line_pre_id, "ID de línea debe ser el original");
        TEST_CHECK(cart_after_bind.value().items[0].quantity == 3, "Cantidad debe permanecer en 3 (no 10)");
        TEST_CHECK(cart_after_bind.value().items[0].notes == "tibio", "Nota debe permanecer en 'tibio' (no 'bien frio')");
    }

    if (fs::exists(db_file)) {
        fs::remove(db_file);
    }
}

void test_i2_print_config_service_permissions() {
    using namespace app;
    using namespace app::storage;
    using namespace app::services;
    using namespace app::contracts;
    namespace fs = std::filesystem;

    std::cout << "[TEST] Configuración de Impresión Térmica 55/80 mm, Persistencia y Permisos config.manage..." << std::endl;

    fs::path temp_dir = fs::temp_directory_path() / ("test_print_cfg_" + generate_random_alphanumeric(8));
    fs::create_directories(temp_dir);
    fs::path db_file = temp_dir / "test_print_cfg.db";

    storage::SqliteDatabase db;
    auto open_res = db.open(db_file.u8string());
    TEST_CHECK(open_res.is_success(), "Abrir DB para prueba de print config");

    storage::MigrationRunner runner(db);
    fs::path mig_dir = fs::u8path(MIGRATIONS_DIR_PATH);
    auto mig_res = runner.run_pending_migrations_from_dir(mig_dir);
    TEST_CHECK(mig_res.is_success(), "Migraciones para print config");

    auto account_svc = std::make_shared<services::SqliteAccountService>(db);

    // 1. Alta inicial Dueño
    auto owner_res = account_svc->create_initial_owner("dueño_cfg", "Dueño Config", "ClaveSegura123!", "owner@itch.edu.mx");
    TEST_CHECK(owner_res.is_success(), "Crear dueño");

    // 2. Crear usuario Cajero (sin permiso config.manage)
    contracts::CreateUserDto new_cashier;
    new_cashier.username = "cajero_cfg";
    new_cashier.display_name = "Cajero Sin Config";
    new_cashier.raw_password = "ClaveCajero123!";
    new_cashier.role_id = "role-cashier";
    new_cashier.email = "cajero@itch.edu.mx";
    auto cashier_create = account_svc->create_user(new_cashier);
    TEST_CHECK(cashier_create.is_success(), "Crear cajero");

    // 3. Instanciar SqlitePrintConfigService
    auto print_svc = std::make_shared<services::SqlitePrintConfigService>(db, account_svc, temp_dir.u8string());

    // 4. Intento sin estar autenticado:
    account_svc->logout();
    contracts::PrinterStationConfig cfg55;
    cfg55.profile = contracts::ThermalPaperProfile::Profile55mm;
    cfg55.nominal_width_mm = 55;
    cfg55.characters_per_line = 28;
    cfg55.printer_name = "Termica 55mm";

    auto anon_res = print_svc->set_station_config(cfg55);
    TEST_CHECK(!anon_res.is_success(), "set_station_config sin login DEBE fallar");
    TEST_CHECK(anon_res.error_code() == contracts::ErrorCode::Forbidden, "Error debe ser Forbidden");

    // 5. Login como cajero (no tiene config.manage)
    auto cashier_login = account_svc->login("cajero_cfg", "ClaveCajero123!");
    TEST_CHECK(cashier_login.is_success(), "Login cajero");

    auto cashier_set_res = print_svc->set_station_config(cfg55);
    TEST_CHECK(!cashier_set_res.is_success(), "Cajero sin config.manage DEBE ser rechazado");
    TEST_CHECK(cashier_set_res.error_code() == contracts::ErrorCode::Forbidden, "Error debe ser Forbidden");

    // 6. Login como Dueño (tiene config.manage)
    auto owner_login = account_svc->login("dueño_cfg", "ClaveSegura123!");
    TEST_CHECK(owner_login.is_success(), "Login dueño");

    // 6.1 Probar validación: ancho nominal inválido (probar explícitamente que 58 mm es rechazado y se exige 55 mm)
    contracts::PrinterStationConfig cfg_invalid_58 = cfg55;
    cfg_invalid_58.nominal_width_mm = 58; // No sustituir 55 mm por 58 mm
    auto inv_58_res = print_svc->set_station_config(cfg_invalid_58);
    TEST_CHECK(!inv_58_res.is_success(), "58 mm nominal debe ser rechazado (solo 55 mm y 80 mm soportados)");
    TEST_CHECK(inv_58_res.error_code() == contracts::ErrorCode::InvalidInput, "Error debe ser InvalidInput");

    // 6.2 Probar validación: caracteres por línea fuera de rango
    contracts::PrinterStationConfig cfg_invalid_cols = cfg55;
    cfg_invalid_cols.characters_per_line = 10;
    auto inv_cols_res = print_svc->set_station_config(cfg_invalid_cols);
    TEST_CHECK(!inv_cols_res.is_success(), "Columnas < 20 debe ser rechazado");
    TEST_CHECK(inv_cols_res.error_code() == contracts::ErrorCode::InvalidInput, "Error debe ser InvalidInput");

    // 6.3 Dueño guarda perfil 55 mm explícito
    auto save55_res = print_svc->set_station_config(cfg55);
    TEST_CHECK(save55_res.is_success(), "Dueño con config.manage debe poder guardar perfil 55 mm");

    // 6.4 Comprobar persistencia per-station abriendo una nueva instancia del servicio
    {
        services::SqlitePrintConfigService print_svc2(db, account_svc, temp_dir.u8string());
        auto read_res = print_svc2.get_station_config();
        TEST_CHECK(read_res.is_success(), "Lectura de config en nueva instancia");
        TEST_CHECK(read_res.value().profile == contracts::ThermalPaperProfile::Profile55mm, "Perfil persistido debe ser 55mm");
        TEST_CHECK(read_res.value().nominal_width_mm == 55, "Ancho nominal debe ser 55");
        TEST_CHECK(read_res.value().characters_per_line == 28, "Columnas deben ser 28");
        TEST_CHECK(read_res.value().printer_name == "Termica 55mm", "Nombre persistido");
    }

    // 6.5 Dueño actualiza a perfil 80 mm
    contracts::PrinterStationConfig cfg80;
    cfg80.profile = contracts::ThermalPaperProfile::Profile80mm;
    cfg80.nominal_width_mm = 80;
    cfg80.characters_per_line = 42;
    cfg80.printer_name = "Termica Mostrador 80mm";

    auto save80_res = print_svc->set_station_config(cfg80);
    TEST_CHECK(save80_res.is_success(), "Guardar perfil 80 mm");

    {
        services::SqlitePrintConfigService print_svc3(db, account_svc, temp_dir.u8string());
        auto read_res = print_svc3.get_station_config();
        TEST_CHECK(read_res.is_success(), "Lectura de config 80 mm");
        TEST_CHECK(read_res.value().profile == contracts::ThermalPaperProfile::Profile80mm, "Perfil debe ser 80mm");
        TEST_CHECK(read_res.value().nominal_width_mm == 80, "Ancho nominal debe ser 80");
        TEST_CHECK(read_res.value().characters_per_line == 42, "Columnas deben ser 42");
    }

    db.close();
    fs::remove_all(temp_dir);
    std::cout << "  -> Configuración de impresión 55/80 mm, persistencia y permisos verificados exitosamente." << std::endl;
}

void test_i2_print_config_atomic_save_and_isolation() {
    std::cout << "--- PRUEBA I2: Guardado consistente de configuración, fallo atómico y aislamiento por puesto ---" << std::endl;
    namespace fs = std::filesystem;
    using namespace app;

    // 1. Directorios temporales con espacios y acentos usando fs::u8path(u8"...") para validar soporte Unicode estricto
    auto base_dir = fs::temp_directory_path() / fs::u8path(u8"cafeteria_cfg_test_áéíóú");
    auto dir_puesto1 = base_dir / fs::u8path(u8"puesto cafetería principal");
    auto dir_puesto2 = base_dir / fs::u8path(u8"puesto barra secundaria");
    fs::create_directories(dir_puesto1);
    fs::create_directories(dir_puesto2);

    auto db_file = base_dir / fs::u8path(u8"test_cfg_iso.db");
    storage::SqliteDatabase db;
    TEST_CHECK(db.open(db_file.u8string()).is_success(), "Apertura DB");
    storage::MigrationRunner runner(db);
    fs::path mig_dir = fs::u8path(MIGRATIONS_DIR_PATH);
    TEST_CHECK(runner.run_pending_migrations_from_dir(mig_dir).is_success(), "Migraciones");

    auto account_svc = std::make_shared<services::SqliteAccountService>(db);
    account_svc->create_initial_owner("dueño_iso", "Dueño Iso", "ClaveSegura123!", "owner@itch.edu.mx");
    account_svc->login("dueño_iso", "ClaveSegura123!");

    // 2. Guardar dos veces en el mismo directorio (80 mm y luego 55 mm) y verificar con nuevo servicio que se leyó el segundo
    contracts::PrinterStationConfig cfg1_80;
    cfg1_80.profile = contracts::ThermalPaperProfile::Profile80mm;
    cfg1_80.nominal_width_mm = 80;
    cfg1_80.characters_per_line = 42;
    cfg1_80.printer_name = "Impresora Mostrador 80mm";

    services::SqlitePrintConfigService svc1(db, account_svc, dir_puesto1.u8string());
    auto save1_1 = svc1.set_station_config(cfg1_80);
    TEST_CHECK(save1_1.is_success(), "Puesto 1 debe guardar primer perfil de 80 mm");

    contracts::PrinterStationConfig cfg1_55;
    cfg1_55.profile = contracts::ThermalPaperProfile::Profile55mm;
    cfg1_55.nominal_width_mm = 55;
    cfg1_55.characters_per_line = 28;
    cfg1_55.printer_name = "Impresora Mostrador 55mm";

    auto save1_2 = svc1.set_station_config(cfg1_55);
    TEST_CHECK(save1_2.is_success(), "Puesto 1 debe guardar segundo perfil (55 mm) en el mismo directorio");

    // Comprobar con un servicio nuevo que leyó efectivamente el segundo guardado (55 mm)
    {
        services::SqlitePrintConfigService check_second(db, account_svc, dir_puesto1.u8string());
        auto r_second = check_second.get_station_config();
        TEST_CHECK(r_second.is_success(), "Lectura de configuracion tras segundo guardado");
        TEST_CHECK(r_second.value().profile == contracts::ThermalPaperProfile::Profile55mm,
                   "La segunda configuración (55 mm) debe ser la leída por el nuevo servicio");
        TEST_CHECK(r_second.value().nominal_width_mm == 55, "55 mm nominales tras segundo guardado");
        TEST_CHECK(r_second.value().characters_per_line == 28, "28 columnas nominales tras segundo guardado");
        TEST_CHECK(r_second.value().printer_name == "Impresora Mostrador 55mm",
                   "Nombre de impresora correspondiente al segundo guardado");
    }

    // 3. Aislamiento por puesto: configurar puesto 2 y comprobar que no afecta a puesto 1
    contracts::PrinterStationConfig cfg2;
    cfg2.profile = contracts::ThermalPaperProfile::Profile80mm;
    cfg2.nominal_width_mm = 80;
    cfg2.characters_per_line = 42;
    cfg2.printer_name = "Impresora Barra 80mm";

    services::SqlitePrintConfigService svc2(db, account_svc, dir_puesto2.u8string());
    auto save2 = svc2.set_station_config(cfg2);
    TEST_CHECK(save2.is_success(), "Puesto 2 debe guardar perfil 80 mm");

    {
        services::SqlitePrintConfigService check1(db, account_svc, dir_puesto1.u8string());
        auto r1 = check1.get_station_config().value();
        TEST_CHECK(r1.profile == contracts::ThermalPaperProfile::Profile55mm, "Puesto 1 conserva 55mm tras configurar puesto 2");
        TEST_CHECK(r1.printer_name == "Impresora Mostrador 55mm", "Puesto 1 conserva impresora");

        services::SqlitePrintConfigService check2(db, account_svc, dir_puesto2.u8string());
        auto r2 = check2.get_station_config().value();
        TEST_CHECK(r2.profile == contracts::ThermalPaperProfile::Profile80mm, "Puesto 2 conserva 80mm");
        TEST_CHECK(r2.printer_name == "Impresora Barra 80mm", "Puesto 2 conserva impresora");
    }

    // 4. Con station_printer.cfg ya existente, provocar fallo de escritura y verificar:
    //    - resultado de error
    //    - contenido del archivo previo byte a byte idéntico
    //    - configuración en memoria sin cambios
    {
        fs::path cfg_file_p1 = dir_puesto1 / "station_printer.cfg";
        TEST_CHECK(fs::exists(cfg_file_p1), "station_printer.cfg debe existir previamente en puesto 1");

        std::string content_before;
        {
            std::ifstream in(cfg_file_p1, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            content_before = ss.str();
        }
        TEST_CHECK(!content_before.empty(), "station_printer.cfg previo no debe estar vacio");

        auto mem_before = svc1.get_station_config().value();
        TEST_CHECK(mem_before.profile == contracts::ThermalPaperProfile::Profile55mm, "Configuracion en memoria previa es 55 mm");

        // Crear un DIRECTORIO llamado station_printer.cfg.tmp para que el ofstream falle al abrirlo
        fs::path tmp_blocker = dir_puesto1 / "station_printer.cfg.tmp";
        std::error_code ec;
        fs::create_directories(tmp_blocker, ec);
        TEST_CHECK(fs::is_directory(tmp_blocker), "El bloqueador temporal debe existir como directorio");

        // Intentar guardar una configuración distinta
        contracts::PrinterStationConfig fail_attempt;
        fail_attempt.profile = contracts::ThermalPaperProfile::Profile80mm;
        fail_attempt.nominal_width_mm = 80;
        fail_attempt.characters_per_line = 42;
        fail_attempt.printer_name = "Impresora Falla Temporal";

        auto fail_res = svc1.set_station_config(fail_attempt);
        TEST_CHECK(!fail_res.is_success(), "set_station_config debe fallar ante directorio .tmp bloqueador");

        // Verificar que en memoria NO cambió la configuración
        auto mem_after = svc1.get_station_config().value();
        TEST_CHECK(mem_after.profile == mem_before.profile, "Perfil en memoria no debe mutar ante fallo");
        TEST_CHECK(mem_after.nominal_width_mm == mem_before.nominal_width_mm, "nominal_width_mm en memoria sin cambios tras fallo");
        TEST_CHECK(mem_after.characters_per_line == mem_before.characters_per_line, "characters_per_line en memoria sin cambios tras fallo");
        TEST_CHECK(mem_after.printer_name == mem_before.printer_name, "printer_name en memoria sin cambios tras fallo");

        // Verificar que el archivo previo en disco es byte a byte idéntico
        std::string content_after;
        {
            std::ifstream in(cfg_file_p1, std::ios::binary);
            std::ostringstream ss;
            ss << in.rdbuf();
            content_after = ss.str();
        }
        TEST_CHECK(content_before == content_after, "Contenido de station_printer.cfg en disco debe ser byte a byte identico tras fallo");

        // Limpiar el directorio bloqueador
        fs::remove(tmp_blocker, ec);
    }

    // 5. Probar que un archivo con printer_name=Impresora Térmica Predeterminada se carga como vacío
    {
        auto dir_legacy = base_dir / fs::u8path(u8"puesto archivo legado");
        fs::create_directories(dir_legacy);
        fs::path legacy_file = dir_legacy / "station_printer.cfg";
        {
            std::ofstream leg_out(legacy_file);
            leg_out << "profile=80mm\n";
            leg_out << "nominal_width_mm=80\n";
            leg_out << "characters_per_line=42\n";
            leg_out << "printer_name=Impresora Térmica Predeterminada\n";
        }

        services::SqlitePrintConfigService svc_legacy(db, account_svc, dir_legacy.u8string());
        auto leg_cfg = svc_legacy.get_station_config();
        TEST_CHECK(leg_cfg.is_success(), "Carga de configuracion desde archivo legado exitosa");
        TEST_CHECK(leg_cfg.value().printer_name.empty(),
                   "printer_name con centinela 'Impresora Térmica Predeterminada' debe cargarse como cadena vacia");
        TEST_CHECK(leg_cfg.value().profile == contracts::ThermalPaperProfile::Profile80mm, "Perfil 80mm de archivo legado");
    }

    db.close();
    fs::remove_all(base_dir);
    std::cout << "  -> Aislamiento por puesto (Unicode con u8path), persistencia atómica y compatibilidad de centinela verificados exitosamente." << std::endl;
}

int main() {
    std::cout << "=== PRUEBAS DE ALMACENAMIENTO SQLITE, PERSISTENCIA Y SEGURIDAD (Debug y Release) ===" << std::endl;
    test_sqlite_lifecycle_and_persistence();
    test_owner_persistence_and_auth();
    test_be02_identity_roles_permissions_and_audit();
    test_be03_catalog_ingredients_and_recipes();
    test_email_and_avatar_management();
    test_i2_shifts_cart_and_sales();
    test_i2_permissions_and_line_isolation();
    test_i2_sales_transaction_rollback_and_idempotency();
    test_i2_recipes_idempotency_cart_editing_and_ticket_history();
    test_i2_integrity_historical_tickets_and_reprint_auth();
    test_i2_cart_atomic_rollback_and_overflow_integrity();
    test_i2_print_config_service_permissions();
    test_i2_print_config_atomic_save_and_isolation();
    std::cout << "\n>>> TODAS LAS PRUEBAS DE ALMACENAMIENTO PASARON CON EXITO! <<<\n" << std::endl;
    return 0;
}
