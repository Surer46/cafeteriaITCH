// Pruebas B8 / punto 2: límite de intentos de inicio de sesión (reloj inyectado: sin esperar minutos reales).
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>

#include "app/services/sqlite_account_service.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/storage/sqlite_database.hpp"

namespace fs = std::filesystem;
using namespace app::services;
using namespace app::storage;
using namespace app::contracts;
using std::chrono::minutes;
using std::chrono::seconds;
using Tp = std::chrono::system_clock::time_point;

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
            std::cerr << "\n[FALLO DE PRUEBA DE INTENTOS] " << (message) \
                      << "\n  Condición: " << #condition \
                      << "\n  Archivo:   " << __FILE__ \
                      << "\n  Línea:     " << __LINE__ << "\n" << std::endl; \
            cleanup_and_exit(1); \
        } \
    } while (0)

namespace {

const std::string kOwnerPwd = "Clave-Dueno-B8-1";
const std::string kCashierPwd = "Clave-Cajero-B8-1";
const std::string kWrong = "Clave-Equivocada-77";
const std::string kGeneric = "Usuario o contraseña incorrectos";

std::string text(SqliteDatabase& db, const std::string& sql) {
    auto prep = db.prepare(sql);
    TEST_CHECK(prep.is_success(), "Debe preparar: " + sql);
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    TEST_CHECK(step.is_success() && step.value(), "Debe devolver fila: " + sql);
    return stmt.get_text(0);
}
std::int64_t num(SqliteDatabase& db, const std::string& sql) { return std::stoll(text(db, sql)); }

struct Env {
    fs::path dir;
    SqliteDatabase db;
    Tp now = Tp(std::chrono::seconds(1'800'000'000));  // instante falso, avanzado a mano
    std::shared_ptr<SqliteAccountService> account;

    explicit Env(const std::string& name) {
        dir = g_root / name;
        fs::create_directories(dir);
        open_db();
        account = make_account();
        auto own = account->create_initial_owner("dueno_l", "Dueño L", kOwnerPwd, "dueno@cafeteria-itch.edu.mx");
        TEST_CHECK(own.is_success(), "Dueño: " + own.error_message());
        CreateUserDto c{"cajero_l", "Cajero L", kCashierPwd, "role-cashier", "cajero@cafeteria-itch.edu.mx", std::nullopt};
        TEST_CHECK(account->create_user(c).is_success(), "Cajero");
        account->logout();
    }

    void open_db() {
        TEST_CHECK(db.open(dir / "cafeteria.db").is_success(), "Abrir base");
        MigrationRunner runner(db);
        auto m = runner.run_pending_migrations_from_dir(fs::u8path(SOURCE_MIGRATIONS_DIR));
        TEST_CHECK(m.is_success(), "Migraciones: " + m.error_message());
    }

    std::shared_ptr<SqliteAccountService> make_account() {
        auto a = std::make_shared<SqliteAccountService>(db);
        a->configure_login_lockout(LoginLockoutPolicy{5, 5}, [this]() { return now; });
        return a;
    }

    // Cierra y reabre la base (como cerrar y abrir la aplicación).
    void reopen() {
        account.reset();
        db.close();
        open_db();
        account = make_account();
    }

    std::string fail_with(const std::string& user, const std::string& pwd) {
        auto r = account->login(user, pwd);
        TEST_CHECK(!r.is_success(), "El intento debía fallar para " + user);
        TEST_CHECK(!account->is_authenticated(), "No debe quedar sesión");
        return r.error_message();
    }
};

std::string lock_text(int minutes_left) {
    return "Demasiados intentos fallidos. Espere " + std::to_string(minutes_left) +
           (minutes_left == 1 ? " minuto" : " minutos") + " para volver a intentar.";
}

} // namespace

static void test_lock_after_five_and_release() {
    std::cout << "[TEST] B8: bloqueo tras 5 fallos, mensaje de espera y liberación tras 5 minutos..." << std::endl;
    Env env("bloqueo");
    for (int i = 1; i <= 4; ++i) {
        TEST_CHECK(env.fail_with("cajero_l", kWrong) == kGeneric, "Los primeros 4 fallos dan el mensaje genérico");
    }
    TEST_CHECK(env.fail_with("cajero_l", kWrong) == lock_text(5), "El 5.º fallo bloquea y avisa: 5 minutos");
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM audit_events WHERE event_type='LOGIN_LOCKED';") == 1,
               "Un evento de auditoría al bloquear");

    // Con la contraseña correcta, bloqueado sigue bloqueado.
    TEST_CHECK(env.fail_with("cajero_l", kCashierPwd) == lock_text(5), "Bloqueado ni con la clave correcta entra");
    env.now += minutes(1);
    TEST_CHECK(env.fail_with("cajero_l", kWrong) == lock_text(4), "Pasado 1 minuto faltan 4");
    env.now += minutes(3);
    TEST_CHECK(env.fail_with("cajero_l", kWrong) == lock_text(1), "A 1 minuto del final: '1 minuto'");
    // Los intentos durante el bloqueo no lo alargan ni se cuentan.
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM audit_events WHERE event_type='LOGIN_LOCKED';") == 1,
               "Los intentos durante el bloqueo no auditan de nuevo");
    env.now += minutes(1) - std::chrono::milliseconds(1);
    TEST_CHECK(env.fail_with("cajero_l", kWrong) == lock_text(1), "Un milisegundo antes aún bloqueado");
    env.now += std::chrono::milliseconds(1);

    // Liberado: entra con la clave correcta y la cuenta de fallos se reinicia.
    auto ok = env.account->login("cajero_l", kCashierPwd);
    TEST_CHECK(ok.is_success(), "Tras 5 minutos debe poder entrar: " + ok.error_message());
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM login_attempts WHERE login_key='cajero_l';") == 0,
               "Un inicio correcto borra los fallos");
    env.account->logout();

    // Nunca se guardó ni auditó una contraseña.
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM audit_events WHERE details LIKE '%" + kWrong + "%' OR details LIKE '%" +
                               kCashierPwd + "%' OR details LIKE '%" + kOwnerPwd + "%';") == 0,
               "La auditoría no contiene contraseñas");
    TEST_CHECK(text(env.db, "SELECT details FROM audit_events WHERE event_type='LOGIN_LOCKED';").find("cajero_l") !=
                   std::string::npos, "El evento de bloqueo nombra al usuario");
}

static void test_count_restarts_after_release_and_success_resets() {
    std::cout << "[TEST] B8: tras liberar la cuenta empieza de cero; un éxito reinicia la cuenta..." << std::endl;
    Env env("reinicio");
    for (int i = 0; i < 5; ++i) env.fail_with("cajero_l", kWrong);
    env.now += minutes(5);
    // Tras el bloqueo vencido hay 5 intentos nuevos, no 1.
    for (int i = 1; i <= 4; ++i) {
        TEST_CHECK(env.fail_with("cajero_l", kWrong) == kGeneric, "Cuenta de cero tras liberar");
    }
    TEST_CHECK(env.fail_with("cajero_l", kWrong) == lock_text(5), "El 5.º vuelve a bloquear");
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM audit_events WHERE event_type='LOGIN_LOCKED';") == 2, "Dos bloqueos auditados");
    env.now += minutes(5);

    // Un inicio correcto entre fallos reinicia la cuenta.
    for (int i = 0; i < 4; ++i) env.fail_with("cajero_l", kWrong);
    TEST_CHECK(env.account->login("cajero_l", kCashierPwd).is_success(), "Entrada correcta");
    env.account->logout();
    for (int i = 0; i < 4; ++i) {
        TEST_CHECK(env.fail_with("cajero_l", kWrong) == kGeneric, "Tras el éxito, 4 fallos no bloquean");
    }
    TEST_CHECK(env.fail_with("cajero_l", kWrong) == lock_text(5), "Pero el 5.º sí");

    // Otro usuario no se ve afectado.
    TEST_CHECK(env.account->login("dueno_l", kOwnerPwd).is_success(), "El bloqueo es por usuario");
    env.account->logout();
}

static void test_persists_across_restart() {
    std::cout << "[TEST] B8: el bloqueo persiste al cerrar y abrir la aplicación..." << std::endl;
    Env env("persistencia");
    for (int i = 0; i < 4; ++i) env.fail_with("cajero_l", kWrong);
    env.reopen();  // 4 fallos persisten
    TEST_CHECK(env.fail_with("cajero_l", kWrong) == lock_text(5), "El 5.º fallo tras reabrir completa los 5");
    env.reopen();
    env.now += minutes(2);
    TEST_CHECK(env.fail_with("cajero_l", kCashierPwd) == lock_text(3), "Bloqueado tras reabrir; faltan 3 minutos");
    env.now += minutes(3);
    TEST_CHECK(env.account->login("cajero_l", kCashierPwd).is_success(), "Liberado tras cumplirse el tiempo");
}

static void test_nonexistent_user_indistinguishable() {
    std::cout << "[TEST] B8: usuario inexistente indistinguible (mensajes y tiempo)..." << std::endl;
    Env env("inexistente");
    for (int i = 1; i <= 4; ++i) {
        TEST_CHECK(env.fail_with("fantasma", kWrong) == kGeneric, "Inexistente: mismo mensaje genérico");
    }
    const std::string ghost_lock = env.fail_with("fantasma", kWrong);
    for (int i = 0; i < 4; ++i) env.fail_with("cajero_l", kWrong);
    const std::string real_lock = env.fail_with("cajero_l", kWrong);
    TEST_CHECK(ghost_lock == lock_text(5) && ghost_lock == real_lock, "Mismo mensaje de bloqueo: " + ghost_lock);
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM login_attempts WHERE login_key='fantasma';") == 1,
               "El nombre inexistente también se cuenta");

    // El tiempo de respuesta es comparable (el hash de relleno cuesta lo mismo que uno real).
    Env timing("tiempos");
    using Clock = std::chrono::steady_clock;
    auto measure = [&](const std::string& user_prefix, bool real) {
        const auto t0 = Clock::now();
        for (int i = 0; i < 3; ++i) {
            // Nombres distintos en cada intento para no llegar al bloqueo (3 < 5 de todos modos).
            timing.fail_with(real ? "cajero_l" : user_prefix + std::to_string(i), kWrong);
        }
        return std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - t0).count();
    };
    const auto real_us = measure("x", true);
    timing.account->login("cajero_l", kCashierPwd);  // reinicia los fallos del real
    timing.account->logout();
    const auto ghost_us = measure("sin_cuenta_", false);
    TEST_CHECK(ghost_us * 2 > real_us && real_us * 2 > ghost_us,
               "Tiempos comparables (real " + std::to_string(real_us) + " us, inexistente " + std::to_string(ghost_us) + " us)");
}

static void test_inactive_account_message_only_with_correct_password() {
    std::cout << "[TEST] B8: cuenta desactivada: aviso solo con la contraseña correcta..." << std::endl;
    Env env("inactiva");
    TEST_CHECK(env.account->login("dueno_l", kOwnerPwd).is_success(), "Dueño entra");
    auto cashier = env.account->list_users();
    TEST_CHECK(cashier.is_success(), "Listar usuarios");
    std::string cashier_id;
    for (const auto& u : cashier.value()) if (u.username == "cajero_l") cashier_id = u.id;
    TEST_CHECK(!cashier_id.empty() && env.account->deactivate_user(cashier_id).is_success(), "Desactivar al cajero");
    env.account->logout();

    TEST_CHECK(env.fail_with("cajero_l", kWrong) == kGeneric, "Contraseña incorrecta: mensaje genérico (no revela que está inactiva)");
    TEST_CHECK(env.fail_with("cajero_l", kCashierPwd) == "La cuenta de usuario está desactivada",
               "Contraseña correcta: aviso de cuenta desactivada");
    TEST_CHECK(env.fail_with("fantasma", kWrong) == kGeneric, "Inexistente: el mismo genérico");
}

static void test_last_owner_same_rule_no_backdoor() {
    std::cout << "[TEST] B8: la última cuenta Dueño sigue la misma regla y el bloqueo expira solo..." << std::endl;
    Env env("dueno");
    for (int i = 0; i < 4; ++i) TEST_CHECK(env.fail_with("dueno_l", kWrong) == kGeneric, "Dueño: fallos genéricos");
    TEST_CHECK(env.fail_with("dueno_l", kWrong) == lock_text(5), "El Dueño también se bloquea (sin excepción)");
    TEST_CHECK(env.fail_with("dueno_l", kOwnerPwd) == lock_text(5), "Ni con su clave correcta mientras dura el bloqueo");
    // Sin puerta trasera: ninguna contraseña alternativa ni usuario especial abre el bloqueo.
    TEST_CHECK(env.fail_with("dueno_l", "") == lock_text(5) && env.fail_with("dueno_l", "admin") == lock_text(5),
               "Ninguna clave genérica abre el bloqueo");
    // No bloquea a otros usuarios.
    TEST_CHECK(env.account->login("cajero_l", kCashierPwd).is_success(), "El cajero sigue pudiendo entrar");
    env.account->logout();
    // Reloj atrasado mucho: el bloqueo nunca dura más que la política (5 minutos).
    env.now -= std::chrono::hours(2);
    TEST_CHECK(env.fail_with("dueno_l", kOwnerPwd) == lock_text(5), "Con el reloj atrasado el bloqueo no supera 5 minutos");
    env.now += std::chrono::hours(2);
    // Siempre expira: nadie queda fuera de forma permanente.
    env.now += minutes(5);
    TEST_CHECK(env.account->login("dueno_l", kOwnerPwd).is_success(), "El Dueño entra al vencer el bloqueo");
    TEST_CHECK(env.account->has_permission("users.manage"), "Y conserva sus permisos");
}

static void test_prune_never_deletes_active_lock() {
    std::cout << "[TEST] B8: la poda y el tope nunca borran un bloqueo vigente..." << std::endl;
    Env env("poda");
    for (int i = 0; i < 5; ++i) env.fail_with("cajero_l", kWrong);  // bloqueado ahora
    const std::int64_t now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(env.now.time_since_epoch()).count();

    // Una fila bloqueada pero con último fallo de hace más de 24 h (caso extremo) y 1100 filas viejas sin bloqueo.
    TEST_CHECK(env.db.execute("INSERT INTO login_attempts (login_key, failed_count, last_failed_ms, locked_until_ms) VALUES "
                              "('viejo_bloqueado', 5, " + std::to_string(now_ms - 25LL * 3600 * 1000) + ", " +
                              std::to_string(now_ms + 600000) + ");").is_success(), "Fila bloqueada antigua");
    TEST_CHECK(env.db.execute("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM n WHERE i < 1100) "
                              "INSERT INTO login_attempts (login_key, failed_count, last_failed_ms, locked_until_ms) "
                              "SELECT 'relleno_' || i, 1, " + std::to_string(now_ms - 1000) + " - i, 0 FROM n;").is_success(),
               "Relleno de 1100 filas");
    env.now += std::chrono::seconds(1);
    env.fail_with("otro_nombre", kWrong);  // dispara la poda y el tope

    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM login_attempts WHERE login_key='viejo_bloqueado';") == 1,
               "La fila bloqueada no se poda aunque su último fallo sea antiguo");
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM login_attempts WHERE login_key='cajero_l' AND locked_until_ms > 0;") == 1,
               "El bloqueo de cajero_l sigue");
    TEST_CHECK(env.fail_with("cajero_l", kCashierPwd).find("Demasiados intentos") == 0, "Y sigue bloqueado");
    const auto total = num(env.db, "SELECT COUNT(*) FROM login_attempts;");
    TEST_CHECK(total <= 1000 + 2, "El tope acota la tabla (filas: " + std::to_string(total) + ")");
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM login_attempts WHERE login_key='otro_nombre';") == 1,
               "El intento nuevo se conserva");
}

static void test_username_case_sensitive_like_login() {
    std::cout << "[TEST] B8: la clave distingue mayúsculas igual que el inicio de sesión..." << std::endl;
    Env env("mayusculas");
    // 'DUENO_L' no es 'dueno_l' para el inicio de sesión: es otro nombre (inexistente).
    for (int i = 0; i < 5; ++i) env.fail_with("DUENO_L", kWrong);
    TEST_CHECK(env.fail_with("DUENO_L", kOwnerPwd) == lock_text(5), "El nombre en mayúsculas queda bloqueado");
    TEST_CHECK(env.account->login("dueno_l", kOwnerPwd).is_success(), "El nombre real no se ve afectado");
}

int main() {
    std::error_code ec;
    g_root = fs::temp_directory_path(ec) /
             fs::u8path(u8"cafeteria b8 intentos ñ " +
                        std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);

    test_lock_after_five_and_release();
    test_count_restarts_after_release_and_success_resets();
    test_persists_across_restart();
    test_nonexistent_user_indistinguishable();
    test_inactive_account_message_only_with_correct_password();
    test_last_owner_same_rule_no_backdoor();
    test_prune_never_deletes_active_lock();
    test_username_case_sensitive_like_login();

    std::cout << "Pruebas de límite de intentos (B8) correctas. TEST_CHECK ejecutados: " << g_checks << std::endl;
    cleanup_and_exit(0);
}
