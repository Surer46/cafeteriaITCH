#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstdlib>
#include <random>
#include <algorithm>
#include <limits>
#include <chrono>
#include <thread>
#include <unordered_set>
#include "app/storage/sqlite_database.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/crypto/password_hasher.hpp"
#include "app/domain/clock.hpp"
#include "app/domain/stock_valuation.hpp"
#include "app/services/identifiers.hpp"
#include "app/services/mock_services.hpp"
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

// Versión más alta presente en la carpeta de migraciones (prefijo numérico NNN_), como texto.
std::string latest_migration_version() {
    fs::path dir = find_migrations_dir();
    int best = 0;
    for (const auto& e : fs::directory_iterator(dir)) {
        const std::string name = e.path().filename().u8string();
        if (name.size() > 4 && name.substr(name.size() - 4) == ".sql" && name[0] >= '0' && name[0] <= '9') {
            best = std::max(best, std::atoi(name.c_str()));
        }
    }
    return std::to_string(best);
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
        // Fuera del directorio de ejecución: el respaldo y sus archivos auxiliares viven en un directorio temporal.
        const fs::path snapshot_dir = fs::temp_directory_path() / ("cafeteria_snapshot_" + generate_random_alphanumeric(8));
        fs::create_directories(snapshot_dir);
        const std::string backup_file = (snapshot_dir / "test_snapshot.bak").u8string();
        if (fs::exists(backup_file)) {
            fs::remove(backup_file);
        }
        // La identidad Desarrollador no tiene backup.manage: el servicio debe negar el respaldo local
        auto dev_bkp = backup_service.trigger_local_backup(backup_file);
        TEST_CHECK(!dev_bkp.is_success() && dev_bkp.error_code() == ErrorCode::Forbidden,
                   "Desarrollador sin backup.manage no debe poder ejecutar respaldo local");
        TEST_CHECK(!fs::exists(backup_file), "No debe crearse archivo cuando se niega el permiso");

        // El Dueño sí tiene backup.manage
        account_service2->set_current_user_for_testing(user);
        auto bkp_res = backup_service.trigger_local_backup(backup_file);
        TEST_CHECK(bkp_res.is_success(), "Respaldo local debe ser exitoso: " + bkp_res.error_message());
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
        std::error_code snap_ec;
        fs::remove_all(snapshot_dir, snap_ec);
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

void test_b4a_permission_titles_migration() {
    std::cout << "[TEST] B4a: migración 009, títulos, secciones y orden de permisos..." << std::endl;

    fs::path mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Debe encontrar directorio de migraciones");

    const fs::path base_dir = fs::temp_directory_path() / "cafeteria_b4a_perm_titles";
    fs::remove_all(base_dir);
    fs::create_directories(base_dir / "mig_old");
    const std::string db_file = (base_dir / "b4a.db").u8string();

    // Copia controlada de las migraciones 001 a 008 para tomar la línea base previa a 009.
    int copied = 0;
    for (const auto& entry : fs::directory_iterator(mig_dir)) {
        const std::string name = entry.path().filename().u8string();
        if (name.size() > 4 && name.substr(name.size() - 4) == ".sql" && name < "009") {
            fs::copy_file(entry.path(), base_dir / "mig_old" / entry.path().filename());
            ++copied;
        }
    }
    TEST_CHECK(copied == 8, "Deben copiarse exactamente las migraciones 001 a 008");

    {
        SqliteDatabase db;
        TEST_CHECK(db.open(db_file).is_success(), "Abrir SQLite B4a");

        auto query_text = [&](const std::string& sql) -> std::string {
            auto prep = db.prepare(sql);
            TEST_CHECK(prep.is_success(), "Preparar consulta B4a");
            auto stmt = std::move(prep.value());
            auto step = stmt.step();
            TEST_CHECK(step.is_success() && step.value(), "La consulta B4a debe devolver una fila");
            return stmt.get_text(0);
        };
        const std::string rp_sql =
            "SELECT group_concat(role_id || ':' || permission_id, ',') FROM "
            "(SELECT role_id, permission_id FROM role_permissions ORDER BY role_id, permission_id);";
        const std::string perm_sql =
            "SELECT group_concat(id || '|' || description || '|' || title || '|' || section || '|' || sort_order, ';') FROM "
            "(SELECT * FROM permissions ORDER BY id);";

        MigrationRunner old_runner(db);
        TEST_CHECK(old_runner.run_pending_migrations_from_dir(base_dir / "mig_old").is_success(), "Migrar hasta 008");
        TEST_CHECK(query_text("SELECT MAX(version) FROM schema_migrations;") == "8", "Línea base en versión 8");
        const std::string rp_before = query_text(rp_sql);
        TEST_CHECK(!rp_before.empty(), "role_permissions previos no deben estar vacíos");
        TEST_CHECK(query_text("SELECT COUNT(*) FROM permissions;") == "21", "21 permisos antes de 009");

        MigrationRunner runner(db);
        TEST_CHECK(runner.run_pending_migrations_from_dir(mig_dir).is_success(), "Aplicar migración 009");
        TEST_CHECK(query_text("SELECT MAX(version) FROM schema_migrations;") == latest_migration_version(), "Última versión del esquema es la de la carpeta de migraciones");
        TEST_CHECK(query_text(rp_sql) == rp_before, "role_permissions (role-owner y role-cashier incluidos) no cambian con 009");
        TEST_CHECK(query_text("SELECT COUNT(*) FROM permissions;") == "21", "Siguen existiendo 21 permisos");
        TEST_CHECK(query_text("SELECT COUNT(*) FROM permissions WHERE title <> '' AND section <> '' AND sort_order > 0;") == "21",
                   "Los 21 permisos tienen título, sección y orden");
        TEST_CHECK(query_text("SELECT COUNT(DISTINCT title) FROM permissions;") == "21", "Los títulos son únicos");
        TEST_CHECK(query_text("SELECT COUNT(*) FROM permissions WHERE section IN ('Punto de Venta','Inventario','Caja y Reportes','BackOffice');") == "21",
                   "Toda sección es una de las cuatro válidas");
        TEST_CHECK(query_text("SELECT title FROM permissions WHERE id = 'pos.sell';") == "Vender y cobrar", "Título de pos.sell");
        TEST_CHECK(query_text("SELECT description FROM permissions WHERE id = 'backup.restore';") ==
                       "Recuperar la información desde una copia. Reemplaza los datos actuales.",
                   "Descripción nueva de backup.restore");

        const std::string perm_after_first = query_text(perm_sql);
        MigrationRunner runner2(db);
        TEST_CHECK(runner2.run_pending_migrations_from_dir(mig_dir).is_success(), "Segunda ejecución del runner");
        TEST_CHECK(query_text(perm_sql) == perm_after_first, "Ejecutar el runner dos veces no cambia permissions");
        TEST_CHECK(query_text(rp_sql) == rp_before, "Segunda ejecución no cambia role_permissions");
        TEST_CHECK(query_text("SELECT COUNT(*) FROM schema_migrations;") == latest_migration_version(), "Segunda ejecución no duplica versiones");

        auto account_svc = std::make_shared<SqliteAccountService>(db);
        const std::string owner_pwd = generate_random_secret(18);
        TEST_CHECK(account_svc->create_initial_owner("owner_b4a", "Dueno B4a", owner_pwd, "owner_b4a@cafeteria-itch.edu.mx").is_success(), "Alta del Dueño B4a");
        TEST_CHECK(account_svc->login("owner_b4a", owner_pwd).is_success(), "Login Dueño B4a");

        auto list = account_svc->list_permissions();
        TEST_CHECK(list.is_success(), "list_permissions B4a");
        TEST_CHECK(list.value().size() == 21, "list_permissions devuelve las 21 filas");
        bool ordered = true;
        bool filled = true;
        for (std::size_t i = 0; i < list.value().size(); ++i) {
            const auto& p = list.value()[i];
            if (i > 0 && list.value()[i - 1].sort_order >= p.sort_order) ordered = false;
            if (p.title.empty() || p.section.empty() || p.sort_order <= 0 || p.module.empty()) filled = false;
        }
        TEST_CHECK(ordered, "list_permissions sale en orden de sort_order");
        TEST_CHECK(filled, "El DTO trae title, section, sort_order y module");
        TEST_CHECK(list.value().front().id == "pos.sell" && list.value().back().id == "backup.dev_config",
                   "Primero pos.sell y último backup.dev_config");
        TEST_CHECK(list.value().back().module == "backup", "El campo module conserva su cálculo por prefijo");

        // El mock devuelve los mismos textos, secciones y orden que la migración.
        MockAccountService mock;
        auto mock_list = mock.list_permissions();
        TEST_CHECK(mock_list.is_success() && mock_list.value().size() == list.value().size(), "El mock devuelve 21 permisos");
        bool same = true;
        for (std::size_t i = 0; i < list.value().size(); ++i) {
            const auto& a = list.value()[i];
            const auto& b = mock_list.value()[i];
            if (a.id != b.id || a.title != b.title || a.description != b.description ||
                a.section != b.section || a.sort_order != b.sort_order || a.module != b.module) same = false;
        }
        TEST_CHECK(same, "El mock coincide con el servicio real en id, textos, sección, orden y módulo");

        db.close();
    }

    fs::remove_all(base_dir);
    std::cout << "  -> Migración 009, orden y coherencia con el mock verificados." << std::endl;
}

// Convierte "YYYY-MM-DDTHH:MM:SS.mmmZ" a milisegundos desde 1970 (UTC). Devuelve -1 si el formato es inválido.
std::int64_t parse_utc_ms(const std::string& s) {
    if (s.size() != 24 || s[4] != '-' || s[7] != '-' || s[10] != 'T' || s[13] != ':' || s[16] != ':' || s[19] != '.' || s[23] != 'Z') return -1;
    auto num = [&](std::size_t pos, std::size_t len) -> int {
        int v = 0;
        for (std::size_t i = 0; i < len; ++i) {
            if (s[pos + i] < '0' || s[pos + i] > '9') return -1;
            v = v * 10 + (s[pos + i] - '0');
        }
        return v;
    };
    int y = num(0, 4), m = num(5, 2), d = num(8, 2), hh = num(11, 2), mm = num(14, 2), ss = num(17, 2), ms = num(20, 3);
    if (y < 0 || m < 1 || m > 12 || d < 1 || d > 31 || hh < 0 || mm < 0 || ss < 0 || ms < 0) return -1;
    // Días desde 1970 (algoritmo de fecha civil)
    y -= m <= 2;
    const int era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = static_cast<unsigned>((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    const std::int64_t days = static_cast<std::int64_t>(era) * 146097 + static_cast<std::int64_t>(doe) - 719468;
    return ((days * 24 + hh) * 60 + mm) * 60000 + static_cast<std::int64_t>(ss) * 1000 + ms;
}

void test_b4_ids_clock_folios_and_migration() {
    std::cout << "[TEST] B4: identificadores, hora local+UTC, updated_at, folios por caja y migración 010..." << std::endl;

    // --- Identificadores: 100000 sin repetición y con formato prefijo-UUIDv4 ---
    {
        std::unordered_set<std::string> seen;
        seen.reserve(100000);
        bool format_ok = true;
        for (int i = 0; i < 100000; ++i) {
            std::string id = generate_id("sale");
            if (id.size() != 5 + 36 || id.compare(0, 5, "sale-") != 0) {
                format_ok = false;
                seen.insert(std::move(id));
                continue;
            }
            const std::string u = id.substr(5);
            if (u[8] != '-' || u[13] != '-' || u[18] != '-' || u[23] != '-' || u[14] != '4') format_ok = false;
            const char var = u[19];
            if (!(var == '8' || var == '9' || var == 'a' || var == 'b')) format_ok = false;
            for (std::size_t k = 0; k < u.size(); ++k) {
                if (k == 8 || k == 13 || k == 18 || k == 23) continue;
                const char ch = u[k];
                if (!((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'))) format_ok = false;
            }
            seen.insert(std::move(id));
        }
        TEST_CHECK(format_ok, "Todos los ids tienen formato prefijo-UUID v4 en hexadecimal minúscula");
        TEST_CHECK(seen.size() == 100000, "100000 identificadores generados sin repetición");
    }

    // --- Reloj: local y UTC del mismo instante ---
    {
        const auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(1700000000123LL));
        auto ts = app::domain::timestamp_from(tp);
        TEST_CHECK(ts.utc == "2023-11-14T22:13:20.123Z", "UTC de un instante conocido");
        TEST_CHECK(ts.local.size() == 19 && ts.local[10] == ' ', "Formato local histórico YYYY-MM-DD HH:MM:SS");
        TEST_CHECK(parse_utc_ms(ts.utc) == 1700000000123LL, "El parser de pruebas reconstruye el instante");
    }

    fs::path mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Debe encontrar directorio de migraciones");

    const fs::path base_dir = fs::temp_directory_path() / ("cafeteria_b4_" + generate_random_alphanumeric(8));
    fs::remove_all(base_dir);
    fs::create_directories(base_dir / "mig_v9");
    int copied = 0;
    for (const auto& entry : fs::directory_iterator(mig_dir)) {
        const std::string name = entry.path().filename().u8string();
        if (name.size() > 4 && name.substr(name.size() - 4) == ".sql" && name < "010") {
            fs::copy_file(entry.path(), base_dir / "mig_v9" / entry.path().filename());
            ++copied;
        }
    }
    TEST_CHECK(copied == 9, "Deben copiarse exactamente las migraciones 001 a 009");

    SqliteDatabase db;
    TEST_CHECK(db.open((base_dir / "b4.db").u8string()).is_success(), "Abrir SQLite B4");

    auto q = [&](const std::string& sql) -> std::string {
        auto prep = db.prepare(sql);
        TEST_CHECK(prep.is_success(), "Preparar consulta B4: " + sql);
        auto stmt = std::move(prep.value());
        auto step = stmt.step();
        TEST_CHECK(step.is_success() && step.value(), "La consulta B4 debe devolver fila: " + sql);
        return stmt.get_text(0);
    };

    // --- Base con migraciones 001 a 009 y datos ---
    MigrationRunner old_runner(db);
    TEST_CHECK(old_runner.run_pending_migrations_from_dir(base_dir / "mig_v9").is_success(), "Migrar hasta 009");
    TEST_CHECK(q("SELECT MAX(version) FROM schema_migrations;") == "9", "Línea base en versión 9");

    const std::string owner_pwd = generate_random_secret(18);
    const std::string owner_hash = app::crypto::PasswordHasher::hash_password(owner_pwd);
    const char* seed[] = {
        "INSERT INTO cash_registers (id, name) VALUES ('reg-02', 'Caja Dos');",
        "INSERT INTO cash_registers (id, name) VALUES ('reg-03', 'Caja Tres');",
        "INSERT INTO products (id, sku, name, category, price_cents, created_at) VALUES ('prod-old', 'SKU-OLD', 'Torta Vieja', 'Platillos', 3500, '2026-10-01 09:00:00');",
        "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock) VALUES ('ing-old', 'Pan Viejo', 'COUNT', 'pz', 5);",
        "INSERT INTO recipes (id, product_id, version, created_at) VALUES ('rcp-old', 'prod-old', 1, '2026-10-01 09:05:00');",
        "INSERT INTO shifts (id, register_id, cashier_user_id, opened_at, closed_at, initial_cash_cents, final_cash_cents, is_open) VALUES ('shift-old', 'reg-01', 'user-dueno_b4', '2026-10-01 08:00:00', '2026-10-01 15:00:00', 50000, 53500, 0);",
        "INSERT INTO sales (id, folio, shift_id, user_id, total_cents, created_at, cashier_name) VALUES ('sale-old', 'V-10001', 'shift-old', 'user-dueno_b4', 3500, '2026-10-01 10:00:00', 'Dueño B4');",
        "INSERT INTO sale_items (id, sale_id, product_id, product_name, quantity, unit_price_cents, subtotal_cents) VALUES ('sitem-old', 'sale-old', 'prod-old', 'Torta Vieja', 1, 3500, 3500);",
        "INSERT INTO payment_records (id, sale_id, payment_method, amount_cents, cash_received_cents, change_cents, created_at) VALUES ('pay-old', 'sale-old', 'EFECTIVO', 3500, 4000, 500, '2026-10-01 10:00:00');",
        "INSERT INTO cash_movements (id, shift_id, movement_type, amount_cents, description, created_at) VALUES ('cm-old', 'shift-old', 'SALE', 3500, 'Venta V-10001 [EFECTIVO]', '2026-10-01 10:00:00');",
        "INSERT INTO stock_movements (id, ingredient_id, movement_type, quantity_base_units, reason, created_at) VALUES ('sm-old', 'ing-old', 'SALE', -1, 'Venta V-10001', '2026-10-01 10:00:00');",
        "INSERT INTO purchase_entries (id, ingredient_id, quantity_purchased, purchase_unit, total_cost_cents, base_units_purchased, created_at) VALUES ('pe-old', 'ing-old', 6, 'pz', 9000, 6, '2026-09-30 12:00:00');",
        "INSERT INTO audit_events (id, event_type, user_id, details, created_at) VALUES ('audit-old', 'SALE_CONFIRMED', 'user-dueno_b4', 'Venta confirmada folio V-10001', '2026-10-01 10:00:00');"
    };
    TEST_CHECK(db.execute("INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at) "
                          "VALUES ('user-dueno_b4', 'dueno_b4', 'Dueño B4', 'b4@cafeteria-itch.edu.mx', '" + owner_hash +
                          "', 'role-owner', 1, '2026-10-01 07:00:00');").is_success(), "Alta de Dueño v9 por SQL");
    for (const char* s : seed) {
        auto r = db.execute(s);
        TEST_CHECK(r.is_success(), std::string("Dato previo: ") + s + " -> " + r.error_message());
    }

    // Instantánea de las columnas originales (todas las tablas con datos) antes de migrar
    const std::string orig_sql =
        "SELECT (SELECT group_concat(id||'|'||folio||'|'||shift_id||'|'||user_id||'|'||total_cents||'|'||created_at||'|'||cashier_name, ';') FROM sales) || '#' ||"
        "(SELECT group_concat(id||'|'||payment_method||'|'||amount_cents||'|'||created_at, ';') FROM payment_records) || '#' ||"
        "(SELECT group_concat(id||'|'||movement_type||'|'||amount_cents||'|'||created_at, ';') FROM cash_movements) || '#' ||"
        "(SELECT group_concat(id||'|'||movement_type||'|'||quantity_base_units||'|'||created_at, ';') FROM stock_movements) || '#' ||"
        "(SELECT group_concat(id||'|'||total_cost_cents||'|'||created_at, ';') FROM purchase_entries) || '#' ||"
        "(SELECT group_concat(id||'|'||opened_at||'|'||closed_at||'|'||register_id, ';') FROM shifts) || '#' ||"
        "(SELECT group_concat(id||'|'||sku||'|'||name||'|'||price_cents||'|'||created_at, ';') FROM products) || '#' ||"
        "(SELECT group_concat(id||'|'||name||'|'||current_stock, ';') FROM ingredients) || '#' ||"
        "(SELECT group_concat(id||'|'||username||'|'||created_at, ';') FROM users) || '#' ||"
        "(SELECT group_concat(id||'|'||created_at, ';') FROM audit_events) || '#' ||"
        "(SELECT group_concat(id||'|'||name, ';') FROM cash_registers);";
    const std::string orig_before = q(orig_sql);
    TEST_CHECK(!orig_before.empty(), "La instantánea previa no puede estar vacía");

    // --- Migrar a la versión nueva ---
    MigrationRunner runner(db);
    TEST_CHECK(runner.run_pending_migrations_from_dir(mig_dir).is_success(), "Aplicar migración 010");
    TEST_CHECK(q("SELECT MAX(version) FROM schema_migrations;") == latest_migration_version(), "Esquema en la última versión");
    TEST_CHECK(q(orig_sql) == orig_before, "Los datos originales quedan intactos tras migrar");

    // Columnas nuevas presentes
    auto has_col = [&](const std::string& table, const std::string& col) {
        return q("SELECT COUNT(*) FROM pragma_table_info('" + table + "') WHERE name = '" + col + "';") == "1";
    };
    for (const char* t : {"sales", "payment_records", "cash_movements", "stock_movements", "purchase_entries", "audit_events"}) {
        TEST_CHECK(has_col(t, "created_at_utc"), std::string("created_at_utc en ") + t);
    }
    TEST_CHECK(has_col("shifts", "opened_at_utc") && has_col("shifts", "closed_at_utc"), "Columnas UTC de shifts");
    for (const char* t : {"products", "ingredients", "recipes", "product_images", "roles", "users"}) {
        TEST_CHECK(has_col(t, "updated_at") && has_col(t, "updated_at_utc"), std::string("updated_at y updated_at_utc en ") + t);
    }
    TEST_CHECK(has_col("cash_registers", "code"), "Columna code en cash_registers");

    // Filas anteriores: UTC en NULL y updated_at = created_at
    TEST_CHECK(q("SELECT COUNT(*) FROM sales WHERE created_at_utc IS NULL;") == "1", "sales antigua: created_at_utc NULL");
    TEST_CHECK(q("SELECT COUNT(*) FROM audit_events WHERE id = 'audit-old' AND created_at_utc IS NULL;") == "1", "audit antigua: created_at_utc NULL");
    TEST_CHECK(q("SELECT COUNT(*) FROM shifts WHERE id = 'shift-old' AND opened_at_utc IS NULL AND closed_at_utc IS NULL;") == "1", "shift antiguo: UTC NULL");
    TEST_CHECK(q("SELECT updated_at FROM products WHERE id = 'prod-old';") == "2026-10-01 09:00:00", "products.updated_at = created_at");
    TEST_CHECK(q("SELECT updated_at FROM recipes WHERE id = 'rcp-old';") == "2026-10-01 09:05:00", "recipes.updated_at = created_at");
    TEST_CHECK(q("SELECT updated_at FROM users WHERE id = 'user-dueno_b4';") == "2026-10-01 07:00:00", "users.updated_at = created_at");
    TEST_CHECK(q("SELECT COUNT(*) FROM ingredients WHERE updated_at <> '';") == "1", "ingredients.updated_at poblada");
    TEST_CHECK(q("SELECT updated_at_utc IS NULL FROM products WHERE id = 'prod-old';") == "1", "products.updated_at_utc NULL en filas antiguas");

    // Códigos de caja: reg-01 = C1; las demás C + ordinal por rowid, únicos
    TEST_CHECK(q("SELECT code FROM cash_registers WHERE id = 'reg-01';") == "C1", "reg-01 = C1");
    TEST_CHECK(q("SELECT code FROM cash_registers WHERE id = 'reg-02';") == "C2", "reg-02 = C2");
    TEST_CHECK(q("SELECT code FROM cash_registers WHERE id = 'reg-03';") == "C3", "reg-03 = C3");
    TEST_CHECK(q("SELECT COUNT(DISTINCT code) FROM cash_registers;") == "3", "Códigos de caja únicos");

    // Segunda ejecución del runner: nada cambia
    const std::string schema_sql = "SELECT group_concat(name||type, ';') FROM (SELECT name, type FROM sqlite_master ORDER BY name);";
    const std::string reg_sql = "SELECT group_concat(id||code, ';') FROM cash_registers;";
    const std::string schema_before = q(schema_sql);
    const std::string full_before = q(orig_sql) + q(reg_sql);
    MigrationRunner runner2(db);
    TEST_CHECK(runner2.run_pending_migrations_from_dir(mig_dir).is_success(), "Segunda ejecución del runner");
    TEST_CHECK(q("SELECT COUNT(*) FROM schema_migrations;") == latest_migration_version(), "No se duplican versiones");
    TEST_CHECK(q(schema_sql) == schema_before, "El esquema no cambia");
    TEST_CHECK(q(orig_sql) + q(reg_sql) == full_before, "Los datos no cambian");

    // --- Servicios sobre la base migrada ---
    auto account_svc = std::make_shared<SqliteAccountService>(db);
    auto cat_svc = std::make_shared<SqliteCatalogService>(db, account_svc);
    auto cart_svc = std::make_shared<SqliteCartService>(db, account_svc, cat_svc);
    auto shift_svc = std::make_shared<SqliteShiftService>(db, account_svc);
    auto sale_svc = std::make_shared<SqliteSaleService>(db, account_svc, cart_svc, shift_svc);
    TEST_CHECK(account_svc->login("dueno_b4", owner_pwd).is_success(), "Login del Dueño sembrado");

    // Folio antiguo sigue recuperable
    auto old_ticket = sale_svc->get_ticket("V-10001");
    TEST_CHECK(old_ticket.is_success(), "get_ticket con folio antiguo V-10001: " + old_ticket.error_message());
    TEST_CHECK(old_ticket.value().folio == "V-10001" && old_ticket.value().total.cents() == 3500, "Ticket antiguo íntegro");
    TEST_CHECK(old_ticket.value().items.size() == 1 && old_ticket.value().cashier_name == "Dueño B4", "Líneas y cajero del ticket antiguo");

    // Producto nuevo y edición: updated_at cambia, created_at no
    CreateProductDto pd;
    pd.sku = "SKU-B4"; pd.name = "Cafe B4"; pd.category = "Bebidas"; pd.price = Money::from_cents(2500); pd.is_prepared = false;
    auto prod = cat_svc->create_product(pd);
    TEST_CHECK(prod.is_success(), "Crear producto B4");
    const std::string prod_id = prod.value().id;
    TEST_CHECK(prod_id.rfind("prod-", 0) == 0 && prod_id.size() == 5 + 36, "El id del producto usa el generador compartido");
    const std::string created_before = q("SELECT created_at FROM products WHERE id = '" + prod_id + "';");
    const std::string upd_before = q("SELECT updated_at FROM products WHERE id = '" + prod_id + "';");
    const std::string upd_utc_before = q("SELECT updated_at_utc FROM products WHERE id = '" + prod_id + "';");
    TEST_CHECK(upd_before == created_before && parse_utc_ms(upd_utc_before) > 0, "Alta: updated_at = created_at y updated_at_utc válido");
    std::this_thread::sleep_for(std::chrono::milliseconds(1200));
    // Iniciar sesión y consultar no cambian updated_at
    TEST_CHECK(account_svc->login("dueno_b4", owner_pwd).is_success(), "Login repetido");
    TEST_CHECK(cat_svc->get_product_by_id(prod_id).is_success(), "Consulta de producto");
    TEST_CHECK(q("SELECT updated_at FROM products WHERE id = '" + prod_id + "';") == upd_before, "Leer no cambia updated_at");
    TEST_CHECK(q("SELECT updated_at FROM users WHERE id = 'user-dueno_b4';") == "2026-10-01 07:00:00", "Iniciar sesión no cambia users.updated_at");
    UpdateProductDto up;
    up.product_id = prod_id;
    up.name = "Cafe B4 Grande";
    TEST_CHECK(cat_svc->update_product(up).is_success(), "Editar producto");
    TEST_CHECK(q("SELECT created_at FROM products WHERE id = '" + prod_id + "';") == created_before, "Editar no cambia created_at");
    TEST_CHECK(q("SELECT updated_at FROM products WHERE id = '" + prod_id + "';") != upd_before, "Editar cambia updated_at");
    TEST_CHECK(q("SELECT updated_at_utc FROM products WHERE id = '" + prod_id + "';") != upd_utc_before, "Editar cambia updated_at_utc");

    // --- Venta nueva: local y UTC del mismo instante; folio por caja ---
    auto sell_on = [&](const std::string& reg, const std::string& key) -> SaleConfirmationDto {
        TEST_CHECK(shift_svc->open_shift(reg, Money::from_cents(10000)).is_success(), "Abrir turno en " + reg);
        TEST_CHECK(cart_svc->add_item(prod_id, 1, "").is_success(), "Agregar al carrito en " + reg);
        auto sale = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(5000), key);
        TEST_CHECK(sale.is_success(), "Confirmar venta en " + reg + ": " + sale.error_message());
        return sale.value();
    };
    auto close_current = [&]() { TEST_CHECK(shift_svc->close_shift(Money::from_cents(10000)).is_success(), "Cerrar turno"); };

    const auto before_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    auto sale1 = sell_on("reg-01", "key-b4-1");
    const auto after_ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    TEST_CHECK(sale1.folio == "V-C1-00001", "Primer folio de la caja C1: " + sale1.folio);
    const std::string sid = "'" + sale1.sale_id + "'";
    const std::string sale_utc = q("SELECT created_at_utc FROM sales WHERE id = " + sid + ";");
    const std::string sale_local = q("SELECT created_at FROM sales WHERE id = " + sid + ";");
    const std::int64_t utc_ms = parse_utc_ms(sale_utc);
    TEST_CHECK(utc_ms >= before_ms - 1 && utc_ms <= after_ms + 1, "created_at_utc de la venta es el instante actual en UTC");
    auto from_utc = app::domain::timestamp_from(std::chrono::system_clock::time_point(std::chrono::milliseconds(utc_ms)));
    TEST_CHECK(from_utc.local == sale_local && from_utc.utc == sale_utc, "La hora local y la UTC de la venta son el mismo instante");
    TEST_CHECK(sale1.created_at == sale_local, "El DTO devuelve la hora local guardada");
    // El mismo instante en pago, caja, existencias y auditoría de esa venta
    TEST_CHECK(q("SELECT created_at_utc FROM payment_records WHERE sale_id = " + sid + ";") == sale_utc, "payment_records comparte el instante");
    TEST_CHECK(q("SELECT created_at_utc FROM cash_movements WHERE description LIKE 'Venta V-C1-00001%';") == sale_utc, "cash_movements comparte el instante");
    TEST_CHECK(q("SELECT created_at_utc FROM audit_events WHERE event_type = 'SALE_CONFIRMED' AND details LIKE '%V-C1-00001%';") == sale_utc, "audit_events comparte el instante");
    TEST_CHECK(q("SELECT COUNT(*) FROM shifts WHERE opened_at_utc IS NOT NULL;") == "1", "El turno nuevo guarda opened_at_utc");
    TEST_CHECK(q("SELECT COUNT(*) FROM audit_events WHERE created_at_utc IS NULL;") == "1", "Todo evento nuevo guarda created_at_utc (solo queda el antiguo en NULL)");

    // Reintento idempotente: mismo folio, sin venta duplicada
    TEST_CHECK(cart_svc->add_item(prod_id, 1, "").is_success(), "Carrito para reintento");
    auto retry = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(5000), "key-b4-1");
    TEST_CHECK(retry.is_success() && retry.value().folio == sale1.folio && retry.value().sale_id == sale1.sale_id, "El reintento devuelve el mismo folio");
    TEST_CHECK(q("SELECT COUNT(*) FROM sales WHERE idempotency_key = 'key-b4-1';") == "1", "No se duplica la venta en el reintento");

    // Segunda venta en la misma caja (el carrito del reintento sigue lleno): secuencia por caja
    auto s2 = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(5000), "key-b4-2");
    TEST_CHECK(s2.is_success(), "Venta 2: " + s2.error_message());
    const std::string folio2 = s2.value().folio;
    TEST_CHECK(folio2 == "V-C1-00002", "Segundo folio de C1: " + folio2);
    close_current();

    // Segunda caja: folio con su propio código, sin chocar con la primera
    auto sale_c2 = sell_on("reg-02", "key-b4-3");
    TEST_CHECK(sale_c2.folio == "V-C2-00001", "Primer folio de la caja C2: " + sale_c2.folio);
    TEST_CHECK(sale_c2.folio != sale1.folio, "Folios de cajas distintas no chocan");
    close_current();

    // Respaldo cuando una caja no tiene código: A-Z0-9, máximo 4 caracteres
    TEST_CHECK(db.execute("INSERT INTO cash_registers (id, name) VALUES ('caja-larga-9', 'Caja sin codigo');").is_success(), "Caja sin código");
    TEST_CHECK(db.execute("INSERT INTO cash_registers (id, name, code) VALUES ('reg-largo', 'Caja codigo largo', 'abc-defgh');").is_success(), "Caja con código largo");
    TEST_CHECK(db.execute("INSERT INTO cash_registers (id, name) VALUES ('---', 'Caja sin alfanumericos');").is_success(), "Caja sin caracteres válidos");
    auto sale_x = sell_on("caja-larga-9", "");
    TEST_CHECK(sale_x.folio == "V-CAJA-00001", "Caja sin código usa su id saneado (4 máx): " + sale_x.folio);
    close_current();
    auto sale_y = sell_on("reg-largo", "");
    TEST_CHECK(sale_y.folio == "V-ABCD-00001", "Código largo se limita a 4 caracteres A-Z0-9: " + sale_y.folio);
    close_current();
    auto sale_z = sell_on("---", "");
    TEST_CHECK(sale_z.folio == "V-X-00001", "Sin caracteres válidos se usa X: " + sale_z.folio);
    close_current();
    for (const std::string& f : {sale1.folio, folio2, sale_c2.folio, sale_x.folio, sale_y.folio, sale_z.folio}) {
        TEST_CHECK(f.size() <= 12, "El folio no excede 12 caracteres: " + f);
        auto t = sale_svc->get_ticket(f);
        TEST_CHECK(t.is_success() && t.value().folio == f, "Cada folio nuevo se recupera con get_ticket: " + f);
    }
    TEST_CHECK(q("SELECT COUNT(DISTINCT folio) FROM sales;") == "7", "Los 7 folios (1 antiguo + 6 nuevos) son distintos");

    db.close();
    std::error_code ec;
    fs::remove_all(base_dir, ec);
    std::cout << "  -> Identificadores, UTC, updated_at, folios por caja y migración 010 verificados." << std::endl;
}

void test_b5_adjustments_movements_and_migration() {
    std::cout << "[TEST] B5: migración 011, mermas, ajustes, movimientos, compras y folio por rango..." << std::endl;

    fs::path mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Debe encontrar directorio de migraciones");

    const fs::path base_dir = fs::temp_directory_path() / ("cafeteria_b5_" + generate_random_alphanumeric(8));
    fs::remove_all(base_dir);
    fs::create_directories(base_dir / "mig_v10");
    int copied = 0;
    for (const auto& entry : fs::directory_iterator(mig_dir)) {
        const std::string name = entry.path().filename().u8string();
        if (name.size() > 4 && name.substr(name.size() - 4) == ".sql" && name < "011") {
            fs::copy_file(entry.path(), base_dir / "mig_v10" / entry.path().filename());
            ++copied;
        }
    }
    TEST_CHECK(copied == 10, "Deben copiarse exactamente las migraciones 001 a 010");

    SqliteDatabase db;
    TEST_CHECK(db.open((base_dir / "b5.db").u8string()).is_success(), "Abrir SQLite B5");

    auto q = [&](const std::string& sql) -> std::string {
        auto prep = db.prepare(sql);
        TEST_CHECK(prep.is_success(), "Preparar consulta B5: " + sql);
        auto stmt = std::move(prep.value());
        auto step = stmt.step();
        TEST_CHECK(step.is_success() && step.value(), "La consulta B5 debe devolver fila: " + sql);
        return stmt.get_text(0);
    };

    // --- Base v10 con datos ---
    MigrationRunner old_runner(db);
    TEST_CHECK(old_runner.run_pending_migrations_from_dir(base_dir / "mig_v10").is_success(), "Migrar hasta 010");
    TEST_CHECK(q("SELECT MAX(version) FROM schema_migrations;") == "10", "Línea base en versión 10");

    const std::string owner_pwd = generate_random_secret(18);
    const std::string owner_hash = app::crypto::PasswordHasher::hash_password(owner_pwd);
    TEST_CHECK(db.execute("INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at, updated_at) "
                          "VALUES ('user-dueno_b5', 'dueno_b5', 'Dueño B5', 'b5@cafeteria-itch.edu.mx', '" + owner_hash +
                          "', 'role-owner', 1, '2026-10-01 07:00:00', '2026-10-01 07:00:00');").is_success(), "Alta de Dueño v10 por SQL");
    const char* seed[] = {
        "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, updated_at) VALUES ('ing-a', 'Limon', 'MASS', 'mg', 1000000, '2026-10-01 07:00:00');",
        "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, updated_at) VALUES ('ing-v', 'Leche', 'VOLUME', 'ml', 5000, '2026-10-01 07:00:00');",
        "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, updated_at) VALUES ('ing-big', 'Agua', 'VOLUME', 'ml', 9223372036854775800, '2026-10-01 07:00:00');",
        "INSERT INTO purchase_entries (id, ingredient_id, quantity_purchased, purchase_unit, total_cost_cents, base_units_purchased, created_at) VALUES ('pe-old', 'ing-a', 1, 'kg', 5000, 1000000, '2026-09-30 12:00:00');",
        "INSERT INTO stock_movements (id, ingredient_id, movement_type, quantity_base_units, reason, created_at) VALUES ('sm-old', 'ing-a', 'PURCHASE', 1000000, 'Entrada de compra', '2026-09-30 12:00:00');"
    };
    for (const char* s : seed) {
        auto r = db.execute(s);
        TEST_CHECK(r.is_success(), std::string("Dato previo: ") + s + " -> " + r.error_message());
    }
    const std::string orig_sql =
        "SELECT (SELECT group_concat(id||'|'||ingredient_id||'|'||movement_type||'|'||quantity_base_units||'|'||reason||'|'||created_at, ';') FROM stock_movements) || '#' ||"
        "(SELECT group_concat(id||'|'||ingredient_id||'|'||total_cost_cents||'|'||base_units_purchased||'|'||created_at, ';') FROM purchase_entries) || '#' ||"
        "(SELECT group_concat(id||'|'||name||'|'||current_stock, ';') FROM ingredients) || '#' ||"
        "(SELECT group_concat(id||'|'||username, ';') FROM users);";
    const std::string orig_before = q(orig_sql);

    // --- Migrar a 011 ---
    MigrationRunner runner(db);
    TEST_CHECK(runner.run_pending_migrations_from_dir(mig_dir).is_success(), "Aplicar migración 011");
    TEST_CHECK(std::stoll(q("SELECT MAX(version) FROM schema_migrations;")) >= 11, "Esquema al menos en versión 11");
    TEST_CHECK(q(orig_sql) == orig_before, "Los datos originales quedan intactos tras migrar a 011");
    TEST_CHECK(q("SELECT COUNT(*) FROM pragma_table_info('stock_movements') WHERE name = 'actor_user_id';") == "1", "actor_user_id en stock_movements");
    TEST_CHECK(q("SELECT COUNT(*) FROM pragma_table_info('purchase_entries') WHERE name = 'actor_user_id';") == "1", "actor_user_id en purchase_entries");
    TEST_CHECK(q("SELECT COUNT(*) FROM stock_movements WHERE id = 'sm-old' AND actor_user_id IS NULL;") == "1", "Movimiento anterior: actor NULL");
    TEST_CHECK(q("SELECT COUNT(*) FROM sqlite_master WHERE type = 'trigger' AND name LIKE 'trg_ingredients_no_negative_stock%';") == "2", "Triggers de existencias negativas");
    TEST_CHECK(q("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name IN ('idx_stock_movements_ingredient', 'idx_stock_movements_created', 'idx_purchase_entries_ingredient', 'idx_purchase_entries_created');") == "4", "Índices de movimientos y compras");
    const std::string schema_sql = "SELECT group_concat(name||type, ';') FROM (SELECT name, type FROM sqlite_master ORDER BY name);";
    const std::string schema_before = q(schema_sql);
    const std::string version_count_before = q("SELECT COUNT(*) FROM schema_migrations;");
    MigrationRunner runner2(db);
    TEST_CHECK(runner2.run_pending_migrations_from_dir(mig_dir).is_success(), "Segunda ejecución del runner");
    TEST_CHECK(q(schema_sql) == schema_before && q(orig_sql) == orig_before, "El segundo runner no cambia esquema ni datos");
    TEST_CHECK(q("SELECT COUNT(*) FROM schema_migrations;") == version_count_before, "El segundo runner no duplica versiones");
    // Respaldo en la base: ninguna escritura directa deja existencias negativas
    TEST_CHECK(!db.execute("UPDATE ingredients SET current_stock = -1 WHERE id = 'ing-v';").is_success(), "El trigger impide existencias negativas por UPDATE");
    TEST_CHECK(q("SELECT current_stock FROM ingredients WHERE id = 'ing-v';") == "5000", "La existencia no cambió tras el UPDATE rechazado");

    // --- Folio por rango: usa el índice único ---
    {
        auto prep = db.prepare("EXPLAIN QUERY PLAN SELECT COALESCE(MAX(CAST(substr(folio, 6) AS INTEGER)), 0) FROM sales WHERE folio >= 'V-C1-' AND folio < 'V-C1.';");
        TEST_CHECK(prep.is_success(), "Preparar plan de consulta del folio");
        auto stmt = std::move(prep.value());
        std::string plan;
        while (true) {
            auto step = stmt.step();
            TEST_CHECK(step.is_success(), "Leer plan");
            if (!step.value()) break;
            plan += stmt.get_text(3) + "|";
        }
        TEST_CHECK(plan.find("SEARCH") != std::string::npos && plan.find("INDEX") != std::string::npos && plan.find("folio>") != std::string::npos &&
                       plan.find("SCAN") == std::string::npos,
                   "La búsqueda de folio es un rango sobre un índice de folio, sin recorrer la tabla (plan: " + plan + ")");
    }

    // --- Servicios ---
    auto account_svc = std::make_shared<SqliteAccountService>(db);
    auto cat_svc = std::make_shared<SqliteCatalogService>(db, account_svc);
    auto inv_svc = std::make_shared<SqliteInventoryService>(db, account_svc);
    auto cart_svc = std::make_shared<SqliteCartService>(db, account_svc, cat_svc);
    auto shift_svc = std::make_shared<SqliteShiftService>(db, account_svc);
    auto sale_svc = std::make_shared<SqliteSaleService>(db, account_svc, cart_svc, shift_svc);
    TEST_CHECK(account_svc->login("dueno_b5", owner_pwd).is_success(), "Login del Dueño");
    const std::string owner_id = "user-dueno_b5";

    const std::string cashier_pwd = generate_random_secret(18);
    {
        CreateUserDto cu;
        cu.username = "cajero_b5"; cu.display_name = "Cajero B5"; cu.raw_password = cashier_pwd;
        cu.role_id = "role-cashier"; cu.email = "cajero_b5@cafeteria-itch.edu.mx";
        TEST_CHECK(account_svc->create_user(cu).is_success(), "Crear cajero");
    }
    auto stock_of = [&](const std::string& id) { return std::stoll(q("SELECT current_stock FROM ingredients WHERE id = '" + id + "';")); };
    auto count_of = [&](const std::string& table) { return std::stoll(q("SELECT COUNT(*) FROM " + table + ";")); };

    // Merma
    TEST_CHECK(inv_svc->register_waste("ing-a", 300, "g", "Se cayó el costal").is_success(), "Merma de 300 g");
    TEST_CHECK(stock_of("ing-a") == 700000, "Existencia tras la merma: 700000 mg");
    TEST_CHECK(q("SELECT quantity_base_units||'|'||movement_type||'|'||reason||'|'||actor_user_id||'|'||(created_at_utc IS NOT NULL) FROM stock_movements WHERE movement_type = 'WASTE';")
                   == "-300000|WASTE|Se cayó el costal|" + owner_id + "|1", "Movimiento de merma correcto con actor y UTC");
    TEST_CHECK(q("SELECT COUNT(*) FROM audit_events WHERE event_type = 'STOCK_WASTE' AND created_at_utc IS NOT NULL;") == "1", "Auditoría de la merma");
    // Ajustes positivo y negativo
    TEST_CHECK(inv_svc->register_adjustment("ing-a", 2, "kg", "Conteo físico").is_success(), "Ajuste +2 kg");
    TEST_CHECK(stock_of("ing-a") == 2700000, "Existencia tras ajuste positivo");
    TEST_CHECK(inv_svc->register_adjustment("ing-a", -1, "kg", "Corrección").is_success(), "Ajuste -1 kg");
    TEST_CHECK(stock_of("ing-a") == 1700000, "Existencia tras ajuste negativo");
    TEST_CHECK(q("SELECT COUNT(*) FROM stock_movements WHERE movement_type = 'ADJUSTMENT';") == "2", "Dos movimientos de ajuste");
    TEST_CHECK(q("SELECT COUNT(*) FROM audit_events WHERE event_type = 'STOCK_ADJUSTMENT';") == "2", "Dos auditorías de ajuste");
    // Motivo: 200 caracteres acentuados (400 bytes) se aceptan; 201 se rechazan
    std::string reason200;
    for (int i = 0; i < 200; ++i) reason200 += "ñ";
    TEST_CHECK(inv_svc->register_adjustment("ing-a", 1, "g", reason200).is_success(), "Motivo de 200 caracteres (400 bytes) aceptado");
    TEST_CHECK(!inv_svc->register_adjustment("ing-a", 1, "g", reason200 + "x").is_success(), "Motivo de 201 caracteres rechazado");
    TEST_CHECK(stock_of("ing-a") == 1701000, "Solo el ajuste de 200 caracteres cambió existencias");

    // Rechazos sin cambios
    const auto mov_before = count_of("stock_movements");
    const auto audit_before = count_of("audit_events");
    const auto stock_before = stock_of("ing-a");
    auto expect_rejected = [&](const OperationResult& r, ErrorCode code, const std::string& what) {
        TEST_CHECK(!r.is_success() && r.error_code() == code, what);
        TEST_CHECK(count_of("stock_movements") == mov_before && count_of("audit_events") == audit_before && stock_of("ing-a") == stock_before,
                   what + " (sin cambios)");
    };
    expect_rejected(inv_svc->register_waste("ing-a", 1, "g", ""), ErrorCode::InvalidInput, "Motivo vacío");
    expect_rejected(inv_svc->register_waste("ing-a", 1, "g", "   \t "), ErrorCode::InvalidInput, "Motivo solo espacios");
    expect_rejected(inv_svc->register_waste("ing-a", 0, "g", "x"), ErrorCode::InvalidInput, "Merma de cantidad cero");
    expect_rejected(inv_svc->register_waste("ing-a", -5, "g", "x"), ErrorCode::InvalidInput, "Merma de cantidad negativa");
    expect_rejected(inv_svc->register_adjustment("ing-a", 0, "g", "x"), ErrorCode::InvalidInput, "Ajuste de cantidad cero");
    expect_rejected(inv_svc->register_waste("ing-a", 2, "kg", "x"), ErrorCode::InsufficientStock, "Merma mayor a la existencia (1.701 kg)");
    expect_rejected(inv_svc->register_adjustment("ing-a", -2, "kg", "x"), ErrorCode::InsufficientStock, "Ajuste que dejaría existencia negativa");
    expect_rejected(inv_svc->register_waste("ing-a", 1, "l", "x"), ErrorCode::InvalidInput, "Unidad de otra dimensión");
    expect_rejected(inv_svc->register_waste("ing-a", std::numeric_limits<std::int64_t>::max(), "kg", "x"), ErrorCode::InvalidInput, "Conversión que desborda");
    expect_rejected(inv_svc->register_adjustment("ing-a", std::numeric_limits<std::int64_t>::min(), "g", "x"), ErrorCode::InvalidInput, "Cantidad mínima de int64");
    expect_rejected(inv_svc->register_adjustment("ing-inexistente", 1, "g", "x"), ErrorCode::NotFound, "Ingrediente inexistente");
    TEST_CHECK(!inv_svc->register_adjustment("ing-big", 100, "ml", "x").is_success() && stock_of("ing-big") == 9223372036854775800LL,
               "Un ajuste que desborda las existencias se rechaza sin cambios");

    // Falla a mitad de transacción: la auditoría falla y todo se revierte
    TEST_CHECK(db.execute("CREATE TRIGGER t_fail_waste_audit BEFORE INSERT ON audit_events WHEN NEW.event_type = 'STOCK_WASTE' "
                          "BEGIN SELECT RAISE(ABORT, 'forzado'); END;").is_success(), "Trigger de falla forzada");
    auto forced = inv_svc->register_waste("ing-a", 1, "g", "No debe quedar");
    TEST_CHECK(!forced.is_success(), "La falla de auditoría hace fallar la merma");
    TEST_CHECK(count_of("stock_movements") == mov_before && stock_of("ing-a") == stock_before && count_of("audit_events") == audit_before,
               "La falla a mitad de transacción no deja datos parciales");
    TEST_CHECK(db.execute("DROP TRIGGER t_fail_waste_audit;").is_success(), "Quitar trigger de falla forzada");
    TEST_CHECK(inv_svc->register_waste("ing-a", 1, "g", "Ahora sí").is_success(), "La merma funciona tras quitar la falla");
    TEST_CHECK(stock_of("ing-a") == stock_before - 1000, "Existencia tras la merma recuperada");

    // Compra y venta con actor
    TEST_CHECK(inv_svc->register_purchase_entry("ing-a", 2, "kg", Money::from_cents(10000)).is_success(), "Compra de 2 kg");
    CreateProductDto pd;
    pd.sku = "SKU-B5"; pd.name = "Limonada B5"; pd.category = "Bebidas"; pd.price = Money::from_cents(3500); pd.is_prepared = true;
    auto prod = cat_svc->create_product(pd);
    TEST_CHECK(prod.is_success(), "Crear producto");
    RecipeItemDto ri;
    ri.ingredient_id = "ing-a"; ri.quantity_base_units = 100000;
    TEST_CHECK(cat_svc->set_recipe(prod.value().id, {ri}).is_success(), "Receta de limonada");
    TEST_CHECK(shift_svc->open_shift("reg-01", Money::from_cents(10000)).is_success(), "Abrir turno");
    TEST_CHECK(cart_svc->add_item(prod.value().id, 1, "").is_success(), "Agregar al carrito");
    auto sale = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(5000), "");
    TEST_CHECK(sale.is_success(), "Confirmar venta: " + sale.error_message());
    TEST_CHECK(sale.value().folio == "V-C1-00001", "Folio por rango: primera venta de C1");
    TEST_CHECK(cart_svc->add_item(prod.value().id, 1, "").is_success(), "Carrito de segunda venta");
    auto sale_b = sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(5000), "");
    TEST_CHECK(sale_b.is_success() && sale_b.value().folio == "V-C1-00002", "Folio por rango: segunda venta de C1");

    // Movimientos: filtros, orden y límite
    StockMovementFilterDto f;
    auto all = inv_svc->list_stock_movements(f);
    TEST_CHECK(all.is_success(), "Listar movimientos: " + all.error_message());
    TEST_CHECK(static_cast<long long>(all.value().size()) == count_of("stock_movements"), "El listado trae todos los movimientos");
    TEST_CHECK(all.value().front().movement_type == "SALE" && all.value().back().id == "sm-old", "Más recientes primero; el antiguo al final");
    bool desc = true;
    for (std::size_t i = 1; i < all.value().size(); ++i) {
        if (all.value()[i - 1].created_at < all.value()[i].created_at) desc = false;
    }
    TEST_CHECK(desc, "Orden descendente por fecha");
    TEST_CHECK(all.value().back().actor_user_id.empty() && all.value().back().actor_name.empty(), "Movimiento anterior sin actor");
    int sales = 0, purchases = 0, wastes = 0;
    for (const auto& m : all.value()) {
        if (m.movement_type == "SALE") { ++sales; TEST_CHECK(m.actor_user_id == owner_id && m.actor_name == "Dueño B5" && m.quantity_base_units == -100000, "Venta con actor y cantidad negativa"); }
        if (m.movement_type == "PURCHASE" && m.id != "sm-old") { ++purchases; TEST_CHECK(m.actor_user_id == owner_id && m.quantity_base_units == 2000000, "Compra con actor y cantidad"); }
        if (m.movement_type == "WASTE") ++wastes;
    }
    TEST_CHECK(sales == 2 && purchases == 1 && wastes == 2, "Una venta y una compra aparecen con su tipo");
    f.movement_type = "WASTE";
    TEST_CHECK(inv_svc->list_stock_movements(f).value().size() == 2, "Filtro por tipo");
    f = StockMovementFilterDto{};
    f.ingredient_id = "ing-v";
    TEST_CHECK(inv_svc->list_stock_movements(f).value().empty(), "Filtro por ingrediente sin movimientos");
    f.ingredient_id = "ing-a";
    f.limit = 2;
    TEST_CHECK(inv_svc->list_stock_movements(f).value().size() == 2, "Límite de 2");
    f.limit = 100000;
    TEST_CHECK(inv_svc->list_stock_movements(f).is_success(), "Límite excesivo se acota");
    const std::string today = app::domain::now().local.substr(0, 10);
    f = StockMovementFilterDto{};
    f.date_from = today; f.date_to = today;
    TEST_CHECK(static_cast<long long>(inv_svc->list_stock_movements(f).value().size()) == count_of("stock_movements") - 1, "Rango de hoy: todos menos el antiguo");
    f.date_from = "2999-01-01"; f.date_to = "";
    TEST_CHECK(inv_svc->list_stock_movements(f).value().empty(), "Rango futuro vacío");
    f.date_from = ""; f.date_to = "2026-09-30";
    TEST_CHECK(inv_svc->list_stock_movements(f).value().size() == 1, "Rango hasta 2026-09-30 incluye el movimiento antiguo");
    f = StockMovementFilterDto{};
    f.movement_type = "INVENTADO";
    TEST_CHECK(!inv_svc->list_stock_movements(f).is_success(), "Tipo desconocido rechazado");
    f = StockMovementFilterDto{};
    f.date_from = "12/11/2026";
    TEST_CHECK(!inv_svc->list_stock_movements(f).is_success(), "Fecha mal formada rechazada");

    // Entradas de compra
    PurchaseEntryFilterDto pf;
    auto pur = inv_svc->list_purchase_entries(pf);
    TEST_CHECK(pur.is_success() && pur.value().size() == 2, "Dos entradas de compra");
    TEST_CHECK(pur.value()[0].total_cost.cents() == 10000 && pur.value()[0].base_units_purchased == 2000000 &&
               pur.value()[0].actor_name == "Dueño B5" && pur.value()[0].ingredient_name == "Limon", "Entrada más reciente con costo, unidades y actor");
    TEST_CHECK(pur.value()[1].id == "pe-old" && pur.value()[1].actor_user_id.empty(), "Entrada anterior sin actor, al final");
    pf.ingredient_id = "ing-v";
    TEST_CHECK(inv_svc->list_purchase_entries(pf).value().empty(), "Filtro de compras por ingrediente");
    pf = PurchaseEntryFilterDto{};
    pf.limit = 1;
    TEST_CHECK(inv_svc->list_purchase_entries(pf).value().size() == 1, "Límite en compras");

    // Cajero sin permisos
    TEST_CHECK(account_svc->login("cajero_b5", cashier_pwd).is_success(), "Login del cajero");
    TEST_CHECK(inv_svc->register_waste("ing-a", 1, "g", "x").error_code() == ErrorCode::Forbidden, "Cajero no puede registrar mermas");
    TEST_CHECK(inv_svc->register_adjustment("ing-a", 1, "g", "x").error_code() == ErrorCode::Forbidden, "Cajero no puede registrar ajustes");
    TEST_CHECK(inv_svc->list_stock_movements(StockMovementFilterDto{}).error_code() == ErrorCode::Forbidden, "Cajero no puede listar movimientos");
    TEST_CHECK(inv_svc->list_purchase_entries(PurchaseEntryFilterDto{}).error_code() == ErrorCode::Forbidden, "Cajero no puede listar compras");
    TEST_CHECK(stock_of("ing-a") == stock_before - 1000 + 2000000 - 200000, "Las existencias no cambiaron con el cajero");

    db.close();
    std::error_code ec;
    fs::remove_all(base_dir, ec);
    std::cout << "  -> Migración 011, mermas, ajustes, movimientos, compras y folio por rango verificados." << std::endl;
}

void test_b5_valuation_domain_rules() {
    std::cout << "[TEST] B5: reglas puras de valuación (promedio móvil, redondeo, desbordamiento)..." << std::endl;
    using app::domain::StockState;
    namespace dom = app::domain;

    StockState empty;
    TEST_CHECK(dom::cost_micro_for_quantity(empty, 10).error_code() == ErrorCode::NotFound, "Sin compras el costo es desconocido, no 0");
    auto p1 = dom::valuation_after_purchase(empty, 1000, 5000);
    TEST_CHECK(p1.is_success() && p1.value().value_micro == 5000000000LL && p1.value().stock == 1000, "Compra: la valuación sube por el costo total");
    TEST_CHECK(dom::valuation_after_purchase(empty, 0, 5000).error_code() == ErrorCode::InvalidInput, "Compra de cantidad 0 rechazada");
    TEST_CHECK(dom::valuation_after_purchase(empty, 10, -1).error_code() == ErrorCode::InvalidInput, "Compra de costo negativo rechazada");
    TEST_CHECK(dom::valuation_after_outflow(p1.value(), 1001).error_code() == ErrorCode::InsufficientStock, "Salida mayor a la existencia rechazada");
    auto out_all = dom::valuation_after_outflow(p1.value(), 1000);
    TEST_CHECK(out_all.is_success() && out_all.value().value_micro == 0 && out_all.value().stock == 0, "Salir todo deja la valuación exactamente en 0");
    TEST_CHECK(out_all.value().last_avg_units == 1000 && out_all.value().last_avg_value_micro == 5000000000LL, "Se conserva el último promedio conocido");
    auto uc = dom::cost_micro_for_quantity(out_all.value(), 100);
    TEST_CHECK(uc.is_success() && uc.value().from_last_known && uc.value().micro == 500000000LL, "Con existencia 0 se usa el último promedio conocido");
    TEST_CHECK(dom::micro_to_cents(499999) == 0 && dom::micro_to_cents(500000) == 1 && dom::micro_to_cents(0) == 0, "Redondeo de micro-centavos a centavos");
    TEST_CHECK(dom::margin_basis_points(3280, 3500).value() == 9371, "Margen 3280 sobre 3500 = 9371 puntos base");
    TEST_CHECK(dom::margin_basis_points(-500, 1000).value() == -5000, "Margen negativo");
    TEST_CHECK(!dom::margin_basis_points(100, 0).is_success(), "Sin porcentaje cuando el precio es 0");
    // Desbordamientos
    StockState big;
    big.stock = std::numeric_limits<std::int64_t>::max() - 5;
    TEST_CHECK(dom::valuation_after_purchase(big, 10, 1).error_code() == ErrorCode::InvalidInput, "Existencia que desborda rechazada");
    TEST_CHECK(dom::valuation_after_inflow_adjustment(big, 10).error_code() == ErrorCode::InvalidInput, "Ajuste que desborda rechazado");
    TEST_CHECK(dom::valuation_after_purchase(empty, 1, 9223372036855LL).error_code() == ErrorCode::InvalidInput, "Costo que desborda la escala de micro-centavos rechazado");
    TEST_CHECK(dom::valuation_after_purchase(empty, 1, 9223372036854LL).is_success(), "El mayor costo que cabe se acepta");
}

void test_b5_costing_and_valuation() {
    std::cout << "[TEST] B5: costo promedio móvil, receta, margen, permisos e invariantes..." << std::endl;
    fs::path mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Debe encontrar directorio de migraciones");
    const fs::path base_dir = fs::temp_directory_path() / ("cafeteria_b5c_" + generate_random_alphanumeric(8));
    fs::remove_all(base_dir);
    fs::create_directories(base_dir);

    SqliteDatabase db;
    TEST_CHECK(db.open((base_dir / "b5c.db").u8string()).is_success(), "Abrir SQLite B5 costos");
    MigrationRunner runner(db);
    TEST_CHECK(runner.run_pending_migrations_from_dir(mig_dir).is_success(), "Migraciones completas");

    auto account_svc = std::make_shared<SqliteAccountService>(db);
    auto cat_svc = std::make_shared<SqliteCatalogService>(db, account_svc);
    auto inv_svc = std::make_shared<SqliteInventoryService>(db, account_svc);
    auto cart_svc = std::make_shared<SqliteCartService>(db, account_svc, cat_svc);
    auto shift_svc = std::make_shared<SqliteShiftService>(db, account_svc);
    auto sale_svc = std::make_shared<SqliteSaleService>(db, account_svc, cart_svc, shift_svc);
    const std::string owner_pwd = generate_random_secret(18);
    TEST_CHECK(account_svc->create_initial_owner("dueno_c", "Dueño C", owner_pwd, "dueno_c@cafeteria-itch.edu.mx").is_success(), "Alta del Dueño");

    auto q = [&](const std::string& sql) -> std::string {
        auto prep = db.prepare(sql);
        TEST_CHECK(prep.is_success(), "Preparar consulta B5c: " + sql);
        auto stmt = std::move(prep.value());
        auto step = stmt.step();
        TEST_CHECK(step.is_success() && step.value(), "La consulta B5c debe devolver fila: " + sql);
        return stmt.get_text(0);
    };
    auto num = [&](const std::string& sql) { return std::stoll(q(sql)); };
    auto stock_of = [&](const std::string& id) { return num("SELECT current_stock FROM ingredients WHERE id = '" + id + "';"); };
    auto value_of = [&](const std::string& id) { return num("SELECT stock_value_micro FROM ingredients WHERE id = '" + id + "';"); };
    auto make_ing = [&](const std::string& name, const std::string& dim, const std::string& unit) {
        CreateIngredientDto d;
        d.name = name; d.unit_dimension = dim; d.base_unit = unit;
        auto r = inv_svc->create_ingredient(d);
        TEST_CHECK(r.is_success(), "Crear ingrediente " + name + ": " + r.error_message());
        return r.value().id;
    };
    auto buy = [&](const std::string& id, std::int64_t qty, const std::string& unit, std::int64_t cents) {
        auto r = inv_svc->register_purchase_entry(id, qty, unit, Money::from_cents(cents));
        TEST_CHECK(r.is_success(), "Compra: " + r.error_message());
    };
    auto cost = [&](const std::string& id, std::int64_t qty) {
        auto r = inv_svc->get_ingredient_cost(id, qty);
        TEST_CHECK(r.is_success(), "Consultar costo: " + r.error_message());
        return r.value();
    };
    auto make_product = [&](const std::string& sku, std::int64_t price_cents, const std::vector<std::pair<std::string, std::int64_t>>& items) {
        CreateProductDto pd;
        pd.sku = sku; pd.name = "Producto " + sku; pd.category = "Pruebas"; pd.price = Money::from_cents(price_cents); pd.is_prepared = true;
        auto p = cat_svc->create_product(pd);
        TEST_CHECK(p.is_success(), "Crear producto " + sku);
        std::vector<RecipeItemDto> ris;
        for (const auto& it : items) {
            RecipeItemDto ri;
            ri.ingredient_id = it.first; ri.quantity_base_units = it.second;
            ris.push_back(ri);
        }
        TEST_CHECK(cat_svc->set_recipe(p.value().id, ris).is_success(), "Receta de " + sku);
        return p.value().id;
    };

    // --- Ejemplo del limón: 1 kg por $50 => 0.05 pesos por g y costo no nulo por mg ---
    const std::string lim = make_ing("Limon", "MASS", "mg");
    buy(lim, 1, "kg", 5000);
    TEST_CHECK(stock_of(lim) == 1000000 && value_of(lim) == 5000000000LL, "Limón: 1 kg => valuación 5 000 000 000 micro-centavos");
    TEST_CHECK(cost(lim, 1).known && cost(lim, 1).cost_for_quantity_micro == 5000, "Limón: costo no nulo por mg (5000 micro-centavos)");
    TEST_CHECK(cost(lim, 1000).cost_for_quantity.cents() == 5 && cost(lim, 1000).basis == CostBasis::CurrentAverage, "Limón: 1 g cuesta exactamente 5 centavos ($0.05)");

    // --- Ejemplo del método B: 1000 g a $50, consumo de 800 g, 1000 g a $100 ---
    const std::string azu = make_ing("Azucar", "MASS", "mg");
    buy(azu, 1000, "g", 5000);
    TEST_CHECK(inv_svc->register_adjustment(azu, -800, "g", "Consumo de prueba").is_success(), "Salida de 800 g");
    TEST_CHECK(stock_of(azu) == 200000 && value_of(azu) == 1000000000LL, "Tras consumir 800 g quedan 200 g valuados en 1 000 000 000 micro");
    buy(azu, 1000, "g", 10000);
    TEST_CHECK(stock_of(azu) == 1200000 && value_of(azu) == 11000000000LL, "Valuación 11000 centavos entre 1200 g");
    TEST_CHECK(cost(azu, 100000).cost_for_quantity.cents() == 917, "100 g cuestan 917 centavos (9.17 por g, redondeando solo al final)");
    TEST_CHECK(cost(azu, 1200000).cost_for_quantity.cents() == 11000 && cost(azu, 1200000).stock_value.cents() == 11000, "1200 g cuestan exactamente 11000 centavos");

    // --- Una merma no cambia el costo promedio ---
    const std::string har = make_ing("Harina", "MASS", "mg");
    buy(har, 2, "kg", 8000);
    const auto micro_before = cost(har, 1000000).cost_for_quantity_micro;
    TEST_CHECK(inv_svc->register_waste(har, 500, "g", "Humedad").is_success(), "Merma de 500 g");
    TEST_CHECK(stock_of(har) == 1500000 && value_of(har) == 6000000000LL, "La merma baja la valuación proporcionalmente");
    TEST_CHECK(cost(har, 1000000).cost_for_quantity_micro == micro_before, "La merma no cambia el costo promedio");

    // --- Muchas salidas pequeñas hasta existencia 0 dejan valuación exactamente 0 ---
    const std::string sal = make_ing("Sal", "MASS", "mg");
    buy(sal, 3, "g", 100);
    std::int64_t remaining = 3000;
    while (remaining > 7) {
        TEST_CHECK(inv_svc->register_waste(sal, 7, "mg", "Gota").is_success(), "Salida pequeña");
        remaining -= 7;
        TEST_CHECK(value_of(sal) >= 0 && stock_of(sal) == remaining, "Valuación no negativa en cada salida");
    }
    TEST_CHECK(inv_svc->register_waste(sal, remaining, "mg", "Último").is_success(), "Salida final");
    TEST_CHECK(stock_of(sal) == 0 && value_of(sal) == 0, "Existencia 0 => valuación exactamente 0");
    auto sal_cost = cost(sal, 1000);
    TEST_CHECK(sal_cost.known && sal_cost.basis == CostBasis::LastKnown, "Con existencia 0 se usa el último costo conocido");
    TEST_CHECK(sal_cost.cost_for_quantity.cents() >= 33 && sal_cost.cost_for_quantity.cents() <= 34, "El último costo conocido sigue siendo ~33.3 centavos por g");

    // --- Una venta que falla y se revierte deja la valuación intacta ---
    const std::string pan = make_ing("Pan", "MASS", "mg");
    buy(pan, 1, "kg", 1000);
    const std::string torta = make_product("TORTA-B5", 3500, {{pan, 200000}});
    TEST_CHECK(shift_svc->open_shift("reg-01", Money::from_cents(10000)).is_success(), "Abrir turno");
    TEST_CHECK(cart_svc->add_item(torta, 1, "").is_success(), "Agregar al carrito");
    const auto sales_before = num("SELECT COUNT(*) FROM sales;");
    const auto mov_before = num("SELECT COUNT(*) FROM stock_movements;");
    TEST_CHECK(db.execute("CREATE TRIGGER t_fail_pay BEFORE INSERT ON payment_records BEGIN SELECT RAISE(ABORT, 'forzado'); END;").is_success(), "Falla forzada del pago");
    TEST_CHECK(!sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(5000), "").is_success(), "La venta falla a mitad de transacción");
    TEST_CHECK(stock_of(pan) == 1000000 && value_of(pan) == 1000000000LL, "Venta revertida: existencias y valuación intactas");
    TEST_CHECK(num("SELECT COUNT(*) FROM sales;") == sales_before && num("SELECT COUNT(*) FROM stock_movements;") == mov_before, "Venta revertida: sin ventas ni movimientos parciales");
    TEST_CHECK(db.execute("DROP TRIGGER t_fail_pay;").is_success(), "Quitar falla forzada");
    TEST_CHECK(sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(5000), "").is_success(), "La venta se confirma tras quitar la falla");
    TEST_CHECK(stock_of(pan) == 800000 && value_of(pan) == 800000000LL, "La venta baja la valuación proporcionalmente (800 000 000 micro)");
    TEST_CHECK(cost(pan, 1000).cost_for_quantity_micro == 1000000, "La venta no cambia el costo promedio (1 centavo por g)");

    // --- Costo desconocido nunca es 0 y bloquea el margen ---
    const std::string fan = make_ing("Fantasma", "MASS", "mg");
    TEST_CHECK(!cost(fan, 100).known && cost(fan, 100).basis == CostBasis::Unknown, "Ingrediente sin compras: costo desconocido");
    const std::string misterio = make_product("MISTERIO", 1000, {{fan, 100}, {pan, 100000}});
    auto mc = cat_svc->get_recipe_costing(misterio);
    TEST_CHECK(mc.is_success(), "Costeo de receta con un ingrediente desconocido");
    TEST_CHECK(!mc.value().all_costs_known && !mc.value().margin_known && !mc.value().margin_percent_known, "Con un costo desconocido no hay margen");
    TEST_CHECK(mc.value().unknown_ingredient_names.size() == 1 && mc.value().unknown_ingredient_names[0] == "Fantasma", "Se lista el ingrediente desconocido");
    TEST_CHECK(mc.value().known_cost_subtotal.cents() == 100, "El subtotal conocido (Pan 100 g) es 100 centavos");
    TEST_CHECK(mc.value().cost_per_portion.cents() == 0 && mc.value().margin.cents() == 0, "No se presenta un costo ni margen falsos");

    // --- Receta con varios ingredientes y margen ---
    const std::string ia = make_ing("IngA", "MASS", "mg");
    const std::string ib = make_ing("IngB", "VOLUME", "ml");
    buy(ia, 1, "kg", 5000);
    buy(ib, 500, "ml", 2000);
    const std::string combo = make_product("COMBO-B5", 3500, {{ia, 20000}, {ib, 30}});
    auto cc = cat_svc->get_recipe_costing(combo);
    TEST_CHECK(cc.is_success() && cc.value().all_costs_known, "Costeo completo");
    TEST_CHECK(cc.value().lines.size() == 2 && cc.value().cost_per_portion.cents() == 220, "Costo por porción: 100 + 120 = 220 centavos");
    TEST_CHECK(cc.value().price.cents() == 3500 && cc.value().margin.cents() == 3280 && cc.value().margin_known, "Margen = 3500 - 220 = 3280");
    TEST_CHECK(cc.value().margin_percent_known && cc.value().margin_basis_points == 9371, "Margen 93.71 %");
    // El total se redondea una sola vez: dos líneas de 0.4 centavos suman 1 centavo (no 0)
    const std::string ic = make_ing("IngC", "MASS", "mg");
    const std::string id_ = make_ing("IngD", "MASS", "mg");
    buy(ic, 1000, "mg", 1);
    buy(id_, 1000, "mg", 1);
    const std::string redondeo = make_product("REDONDEO", 100, {{ic, 400}, {id_, 400}});
    auto rc = cat_svc->get_recipe_costing(redondeo);
    TEST_CHECK(rc.is_success() && rc.value().lines.size() == 2, "Costeo de redondeo");
    TEST_CHECK(rc.value().lines[0].cost.cents() == 0 && rc.value().lines[1].cost.cents() == 0, "Cada línea sola se muestra en 0 centavos");
    TEST_CHECK(rc.value().cost_per_portion.cents() == 1 && rc.value().margin.cents() == 99, "El total redondea una sola vez: 1 centavo");
    // Existencia 0 con compras: último costo conocido, marcado
    const std::string salado = make_product("SALADO", 500, {{sal, 1000}});
    auto sc = cat_svc->get_recipe_costing(salado);
    TEST_CHECK(sc.is_success() && sc.value().all_costs_known && sc.value().uses_last_known_cost &&
               sc.value().lines[0].basis == CostBasis::LastKnown && sc.value().margin_known, "Receta con ingrediente en 0: usa el último costo conocido y lo marca");

    // --- Costos: ajuste positivo (a) ---
    const std::string cafe = make_ing("Cafe", "MASS", "mg");
    buy(cafe, 1000, "g", 5000);
    const auto cafe_micro = cost(cafe, 1000000).cost_for_quantity_micro;
    TEST_CHECK(inv_svc->register_adjustment(cafe, 1000, "g", "Apareció mercancía").is_success(), "Ajuste positivo");
    TEST_CHECK(stock_of(cafe) == 2000000 && value_of(cafe) == 10000000000LL, "El ajuste positivo entra al costo promedio vigente");
    TEST_CHECK(cost(cafe, 1000000).cost_for_quantity_micro == cafe_micro, "El ajuste positivo no cambia el promedio");
    TEST_CHECK(inv_svc->register_waste(cafe, 2000, "g", "Se acabó").is_success() && value_of(cafe) == 0, "Vaciado: valuación 0");
    TEST_CHECK(inv_svc->register_adjustment(cafe, 500, "g", "Reaparece").is_success() && value_of(cafe) == 2500000000LL, "Con existencia 0 entra al último promedio conocido");
    TEST_CHECK(cost(cafe, 1000).basis == CostBasis::CurrentAverage, "Tras entrar mercancía el promedio vuelve a ser vigente");

    // --- Regla (d): existencia previa sin costo ---
    const std::string mis = make_ing("Misterioso", "MASS", "mg");
    TEST_CHECK(inv_svc->register_adjustment(mis, 500, "g", "Existencia sin factura").is_success(), "Ajuste positivo sin compras previas");
    TEST_CHECK(stock_of(mis) == 500000 && value_of(mis) == 0 && !cost(mis, 1000).known, "Sin costo conocido: entra sin valuación y el costo sigue desconocido");
    buy(mis, 500, "g", 2500);
    TEST_CHECK(stock_of(mis) == 1000000 && value_of(mis) == 5000000000LL, "La primera compra valora también la existencia previa sin costo");
    TEST_CHECK(cost(mis, 1000).cost_for_quantity.cents() == 5, "Primer costo conocido: 5 centavos por g");
    const std::string don = make_ing("Donado", "MASS", "mg");
    TEST_CHECK(inv_svc->register_adjustment(don, 100, "g", "Sin costo").is_success(), "Existencia sin costo");
    buy(don, 100, "g", 0);
    auto don_cost = cost(don, 1000);
    TEST_CHECK(stock_of(don) == 200000 && value_of(don) == 0, "Compra de costo 0: existencias sumadas y valuación 0");
    TEST_CHECK(don_cost.known && don_cost.cost_for_quantity_micro == 0 && don_cost.basis == CostBasis::CurrentAverage, "Compra de costo 0: el costo es conocido y vale 0");

    // --- Desbordamientos: error y nada cambia ---
    const std::string caro = make_ing("Caro", "MASS", "mg");
    const auto pe_count = num("SELECT COUNT(*) FROM purchase_entries;");
    const auto mv_count = num("SELECT COUNT(*) FROM stock_movements;");
    auto too_big = inv_svc->register_purchase_entry(caro, 1, "mg", Money::from_cents(9223372036855LL));
    TEST_CHECK(!too_big.is_success() && too_big.error_code() == ErrorCode::InvalidInput, "Compra cuya valuación no cabe: rechazada");
    TEST_CHECK(stock_of(caro) == 0 && value_of(caro) == 0 && num("SELECT COUNT(*) FROM purchase_entries;") == pe_count &&
               num("SELECT COUNT(*) FROM stock_movements;") == mv_count, "Compra rechazada: no cambia nada");
    buy(caro, 1, "mg", 9000000000000LL);
    TEST_CHECK(value_of(caro) == 9000000000000000000LL, "Una valuación grande que sí cabe se acepta");
    const auto pe_count2 = num("SELECT COUNT(*) FROM purchase_entries;");
    auto second = inv_svc->register_purchase_entry(caro, 1, "mg", Money::from_cents(9000000000000LL));
    TEST_CHECK(!second.is_success() && second.error_code() == ErrorCode::InvalidInput, "Una segunda compra que desborda la valuación: rechazada");
    TEST_CHECK(stock_of(caro) == 1 && value_of(caro) == 9000000000000000000LL && num("SELECT COUNT(*) FROM purchase_entries;") == pe_count2,
               "Desbordamiento de valuación: no cambia nada");
    TEST_CHECK(db.execute("INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, updated_at) VALUES ('ing-lleno', 'Lleno', 'MASS', 'mg', 9223372036854775802, '2026-11-12 00:00:00');").is_success(), "Ingrediente casi lleno");
    auto too_many = inv_svc->register_purchase_entry("ing-lleno", 10, "mg", Money::from_cents(1));
    TEST_CHECK(!too_many.is_success() && too_many.error_code() == ErrorCode::InvalidInput && stock_of("ing-lleno") == 9223372036854775802LL,
               "Compra que desborda las existencias: rechazada sin cambios");

    // --- Permisos: solo inventory.manage_products ve costos ---
    auto owner_recipe = cat_svc->get_recipe(combo);
    TEST_CHECK(owner_recipe.is_success() && owner_recipe.value().estimated_cost_per_portion.cents() == 220, "El Dueño recibe el costo (campo obsoleto) como siempre");
    {
        CreateUserDto cu;
        cu.username = "cajero_c"; cu.display_name = "Cajero C"; cu.raw_password = generate_random_secret(18);
        cu.role_id = "role-cashier"; cu.email = "cajero_c@cafeteria-itch.edu.mx";
        TEST_CHECK(account_svc->create_user(cu).is_success(), "Crear cajero");
        TEST_CHECK(account_svc->login("cajero_c", cu.raw_password).is_success(), "Login del cajero");
    }
    TEST_CHECK(inv_svc->get_ingredient_cost(lim, 1000).error_code() == ErrorCode::Forbidden, "Cajero: get_ingredient_cost denegado");
    TEST_CHECK(cat_svc->get_recipe_costing(combo).error_code() == ErrorCode::Forbidden, "Cajero: get_recipe_costing denegado");
    auto cashier_recipe = cat_svc->get_recipe(combo);
    TEST_CHECK(cashier_recipe.is_success() && cashier_recipe.value().estimated_cost_per_portion.cents() == 0, "Cajero: get_recipe devuelve el costo estimado en 0");
    TEST_CHECK(!cashier_recipe.value().items.empty(), "Cajero: get_recipe sigue devolviendo la receta");
    TEST_CHECK(account_svc->login("dueno_c", owner_pwd).is_success(), "Volver al Dueño");

    // --- Invariantes tras una secuencia larga y mezclada ---
    struct Track {
        std::string id;
        bool has_min = false;
        __int128 min_num = 0, min_den = 1, max_num = 0, max_den = 1; // micro-centavos por unidad base (fracción)
    };
    std::vector<Track> tracks(3);
    const char* names[3] = {"SeqX", "SeqY", "SeqZ"};
    for (int i = 0; i < 3; ++i) tracks[i].id = make_ing(names[i], "MASS", "mg");
    const std::string seq_prod = make_product("SEQ-VENTA", 900, {{tracks[0].id, 100}});
    std::uint64_t rng = 0x9E3779B97F4A7C15ULL;
    auto next = [&]() { rng = rng * 6364136223846793005ULL + 1442695040888963407ULL; return rng >> 33; };
    auto do_purchase = [&](Track& t, std::int64_t units, std::int64_t cents) {
        buy(t.id, units, "mg", cents);
        const __int128 n = static_cast<__int128>(cents) * 1000000, d = units;
        if (!t.has_min) { t.min_num = t.max_num = n; t.min_den = t.max_den = d; t.has_min = true; }
        if (n * t.min_den < t.min_num * d) { t.min_num = n; t.min_den = d; }
        if (n * t.max_den > t.max_num * d) { t.max_num = n; t.max_den = d; }
    };
    for (auto& t : tracks) do_purchase(t, 1000 + static_cast<std::int64_t>(next() % 4000), 1 + static_cast<std::int64_t>(next() % 500));
    int sales_done = 0;
    long long ops = 3;
    for (int step = 0; step < 300; ++step) {
        Track& t = tracks[next() % 3];
        const std::int64_t s = stock_of(t.id);
        const auto kind = next() % 10;
        if (kind <= 3 || s == 0) {
            do_purchase(t, 1 + static_cast<std::int64_t>(next() % 5000), static_cast<std::int64_t>(next() % 20000));
        } else if (kind <= 5) {
            TEST_CHECK(inv_svc->register_waste(t.id, 1 + static_cast<std::int64_t>(next() % std::min<std::int64_t>(s, 3000)), "mg", "Merma").is_success(), "Merma aleatoria");
        } else if (kind == 6) {
            TEST_CHECK(inv_svc->register_adjustment(t.id, -(1 + static_cast<std::int64_t>(next() % std::min<std::int64_t>(s, 2000))), "mg", "Ajuste -").is_success(), "Ajuste negativo aleatorio");
        } else if (kind == 7) {
            TEST_CHECK(inv_svc->register_adjustment(t.id, 1 + static_cast<std::int64_t>(next() % 2000), "mg", "Ajuste +").is_success(), "Ajuste positivo aleatorio");
        } else if (stock_of(tracks[0].id) >= 100 && sales_done < 15) {
            TEST_CHECK(cart_svc->add_item(seq_prod, 1, "").is_success(), "Carrito de la secuencia");
            TEST_CHECK(sale_svc->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(1000), "").is_success(), "Venta de la secuencia");
            ++sales_done;
        }
        ++ops;
        for (const auto& tr : tracks) {
            const std::int64_t stock = stock_of(tr.id), value = value_of(tr.id);
            TEST_CHECK(value >= 0 && stock >= 0, "La valuación nunca es negativa");
            if (stock == 0) {
                TEST_CHECK(value == 0, "Con existencia 0 la valuación es exactamente 0");
            } else {
                // El promedio vigente queda entre el costo unitario mínimo y máximo de las compras (±1 micro por operación)
                const __int128 tol = ops;
                TEST_CHECK((static_cast<__int128>(value) + tol) * tr.min_den >= static_cast<__int128>(stock) * tr.min_num, "El promedio vigente no baja del mínimo de las compras");
                TEST_CHECK(static_cast<__int128>(value) * tr.max_den <= static_cast<__int128>(stock) * tr.max_num + tol * tr.max_den, "El promedio vigente no supera el máximo de las compras");
            }
        }
    }
    TEST_CHECK(sales_done > 0, "La secuencia incluyó ventas reales");

    db.close();
    std::error_code ec;
    fs::remove_all(base_dir, ec);
    std::cout << "  -> Costos, receta, margen, permisos e invariantes verificados." << std::endl;
}

void test_b5_valuation_backfill_migration() {
    std::cout << "[TEST] B5: migración 012, relleno aproximado de la valuación..." << std::endl;
    fs::path mig_dir = find_migrations_dir();
    TEST_CHECK(!mig_dir.empty(), "Debe encontrar directorio de migraciones");
    const fs::path base_dir = fs::temp_directory_path() / ("cafeteria_b5m_" + generate_random_alphanumeric(8));
    fs::remove_all(base_dir);
    fs::create_directories(base_dir / "mig_v11");
    int copied = 0;
    for (const auto& entry : fs::directory_iterator(mig_dir)) {
        const std::string name = entry.path().filename().u8string();
        if (name.size() > 4 && name.substr(name.size() - 4) == ".sql" && name < "012") {
            fs::copy_file(entry.path(), base_dir / "mig_v11" / entry.path().filename());
            ++copied;
        }
    }
    TEST_CHECK(copied == 11, "Deben copiarse exactamente las migraciones 001 a 011");

    SqliteDatabase db;
    TEST_CHECK(db.open((base_dir / "b5m.db").u8string()).is_success(), "Abrir SQLite B5 relleno");
    auto q = [&](const std::string& sql) -> std::string {
        auto prep = db.prepare(sql);
        TEST_CHECK(prep.is_success(), "Preparar consulta: " + sql);
        auto stmt = std::move(prep.value());
        auto step = stmt.step();
        TEST_CHECK(step.is_success() && step.value(), "La consulta debe devolver fila: " + sql);
        return stmt.get_text(0);
    };
    MigrationRunner old_runner(db);
    TEST_CHECK(old_runner.run_pending_migrations_from_dir(base_dir / "mig_v11").is_success(), "Migrar hasta 011");

    const std::string owner_pwd = generate_random_secret(18);
    const std::string owner_hash = app::crypto::PasswordHasher::hash_password(owner_pwd);
    TEST_CHECK(db.execute("INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at, updated_at) "
                          "VALUES ('user-dueno_m', 'dueno_m', 'Dueño M', 'm@cafeteria-itch.edu.mx', '" + owner_hash +
                          "', 'role-owner', 1, '2026-10-01 07:00:00', '2026-10-01 07:00:00');").is_success(), "Alta de Dueño v11 por SQL");
    const char* seed[] = {
        // ing-p: dos compras a precios distintos y existencia parcial (1 500 000 de 2 000 000 comprados)
        "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, updated_at) VALUES ('ing-p', 'Parcial', 'MASS', 'mg', 1500000, '2026-10-01 00:00:00');",
        "INSERT INTO purchase_entries (id, ingredient_id, quantity_purchased, purchase_unit, total_cost_cents, base_units_purchased, created_at) VALUES ('pe-1', 'ing-p', 1, 'kg', 5000, 1000000, '2026-09-01 10:00:00');",
        "INSERT INTO purchase_entries (id, ingredient_id, quantity_purchased, purchase_unit, total_cost_cents, base_units_purchased, created_at) VALUES ('pe-2', 'ing-p', 1, 'kg', 10000, 1000000, '2026-09-15 10:00:00');",
        // ing-n: sin compras
        "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, updated_at) VALUES ('ing-n', 'SinCompras', 'MASS', 'mg', 700, '2026-10-01 00:00:00');",
        // ing-z: con compras y existencia 0
        "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, updated_at) VALUES ('ing-z', 'Agotado', 'MASS', 'mg', 0, '2026-10-01 00:00:00');",
        "INSERT INTO purchase_entries (id, ingredient_id, quantity_purchased, purchase_unit, total_cost_cents, base_units_purchased, created_at) VALUES ('pe-3', 'ing-z', 3, 'g', 100, 3000, '2026-09-20 10:00:00');",
        // ing-h: cifras que obligan a dividir antes de multiplicar
        "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, updated_at) VALUES ('ing-h', 'Enorme', 'MASS', 'mg', 1000000, '2026-10-01 00:00:00');",
        "INSERT INTO purchase_entries (id, ingredient_id, quantity_purchased, purchase_unit, total_cost_cents, base_units_purchased, created_at) VALUES ('pe-4', 'ing-h', 3, 'kg', 9000000000000, 3000000, '2026-09-21 10:00:00');"
    };
    for (const char* s : seed) {
        auto r = db.execute(s);
        TEST_CHECK(r.is_success(), std::string("Dato previo: ") + s + " -> " + r.error_message());
    }
    const std::string orig_sql =
        "SELECT (SELECT group_concat(id||'|'||name||'|'||current_stock||'|'||low_stock_threshold, ';') FROM ingredients) || '#' ||"
        "(SELECT group_concat(id||'|'||ingredient_id||'|'||total_cost_cents||'|'||base_units_purchased, ';') FROM purchase_entries) || '#' ||"
        "(SELECT group_concat(id||'|'||username, ';') FROM users);";
    const std::string orig_before = q(orig_sql);

    MigrationRunner runner(db);
    TEST_CHECK(runner.run_pending_migrations_from_dir(mig_dir).is_success(), "Aplicar migración 012");
    TEST_CHECK(q("SELECT MAX(version) FROM schema_migrations;") == latest_migration_version(), "Esquema en la última versión");
    TEST_CHECK(q(orig_sql) == orig_before, "Los datos originales quedan intactos");

    auto row = [&](const std::string& id) {
        return q("SELECT stock_value_micro||'|'||last_avg_value_micro||'|'||last_avg_units FROM ingredients WHERE id = '" + id + "';");
    };
    TEST_CHECK(row("ing-p") == "11250000000|15000000000|2000000", "Parcial: promedio 7.5 c/g aplicado a 1.5 kg = 11250 centavos");
    TEST_CHECK(row("ing-n") == "0|0|0", "Sin compras: valuación 0 y costo desconocido");
    TEST_CHECK(row("ing-z") == "0|100000000|3000", "Agotado: valuación 0 y último promedio conocido");
    TEST_CHECK(row("ing-h") == "3000000000000000000|9000000000000000000|3000000", "Cifras enormes: se divide antes de multiplicar sin desbordar");

    const std::string schema_sql = "SELECT group_concat(name||type, ';') FROM (SELECT name, type FROM sqlite_master ORDER BY name);";
    const std::string schema_before = q(schema_sql);
    const std::string values_before = q("SELECT group_concat(id||'|'||stock_value_micro||'|'||last_avg_value_micro||'|'||last_avg_units, ';') FROM ingredients;");
    MigrationRunner runner2(db);
    TEST_CHECK(runner2.run_pending_migrations_from_dir(mig_dir).is_success(), "Segunda ejecución del runner");
    TEST_CHECK(q(schema_sql) == schema_before && q(orig_sql) == orig_before, "El segundo runner no cambia esquema ni datos");
    TEST_CHECK(q("SELECT group_concat(id||'|'||stock_value_micro||'|'||last_avg_value_micro||'|'||last_avg_units, ';') FROM ingredients;") == values_before,
               "El segundo runner no recalcula el relleno");
    TEST_CHECK(!db.execute("UPDATE ingredients SET stock_value_micro = -1 WHERE id = 'ing-n';").is_success(), "El trigger impide valuación negativa");

    // Los datos migrados son utilizables por los servicios
    auto account_svc = std::make_shared<SqliteAccountService>(db);
    auto inv_svc = std::make_shared<SqliteInventoryService>(db, account_svc);
    TEST_CHECK(account_svc->login("dueno_m", owner_pwd).is_success(), "Login del Dueño");
    auto p_cost = inv_svc->get_ingredient_cost("ing-p", 1000);
    TEST_CHECK(p_cost.is_success() && p_cost.value().known && p_cost.value().cost_for_quantity_micro == 7500000 && p_cost.value().cost_for_quantity.cents() == 8,
               "Parcial: 1 g cuesta 7.5 centavos (8 al redondear)");
    TEST_CHECK(!inv_svc->get_ingredient_cost("ing-n", 1000).value().known, "Sin compras: desconocido tras migrar");
    auto z_cost = inv_svc->get_ingredient_cost("ing-z", 1000);
    TEST_CHECK(z_cost.value().known && z_cost.value().basis == CostBasis::LastKnown && z_cost.value().cost_for_quantity.cents() == 33, "Agotado: último costo conocido");
    TEST_CHECK(inv_svc->register_purchase_entry("ing-p", 500, "g", Money::from_cents(5000)).is_success(), "Compra sobre datos migrados");
    TEST_CHECK(q("SELECT current_stock||'|'||stock_value_micro FROM ingredients WHERE id = 'ing-p';") == "2000000|16250000000", "La compra suma a la valuación rellenada (11250 + 5000 = 16250 centavos)");

    db.close();
    std::error_code ec;
    fs::remove_all(base_dir, ec);
    std::cout << "  -> Migración 012 y relleno aproximado verificados." << std::endl;
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
    test_b4a_permission_titles_migration();
    test_b4_ids_clock_folios_and_migration();
    test_b5_adjustments_movements_and_migration();
    test_b5_valuation_domain_rules();
    test_b5_costing_and_valuation();
    test_b5_valuation_backfill_migration();
    std::cout << "\n>>> TODAS LAS PRUEBAS DE ALMACENAMIENTO PASARON CON EXITO! <<<\n" << std::endl;
    return 0;
}
