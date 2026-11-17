// Pruebas B12: respaldo automático al cerrar turno, segunda carpeta, lista de respaldos, limpieza por tipo y
// destino, aviso de versión más nueva y migración 016. Todo ocurre en carpetas temporales propias.
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "sqlite3.h"
#include "app/services/database_startup.hpp"
#include "app/services/file_fingerprint.hpp"
#include "app/services/mock_services.hpp"
#include "app/services/sqlite_backup_service.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/storage/sqlite_database.hpp"

namespace fs = std::filesystem;
using namespace app::services;
using namespace app::storage;
using namespace app::contracts;
using Clock = std::chrono::steady_clock;

static fs::path g_root;
static int g_checks = 0;
static void cleanup_and_exit(int code) {
    std::error_code ec;
    if (!g_root.empty()) fs::remove_all(g_root, ec);
    std::exit(code);
}
#define TEST_CHECK(condition, message) \
    do { \
        ++g_checks; \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA DE RESPALDO AUTOMÁTICO] " << (message) << "\n  Condición: " << #condition \
                      << "\n  Línea: " << __LINE__ << "\n" << std::endl; \
            cleanup_and_exit(1); \
        } \
    } while (0)

namespace {

const std::string kOwnerPwd = "Clave-Dueno-B12-1";
const std::string kCashierPwd = "Clave-Cajero-B12-1";
const int kWaitMs = 180000;

long long ms_since(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

std::string text(SqliteDatabase& db, const std::string& sql) {
    auto prep = db.prepare(sql);
    TEST_CHECK(prep.is_success(), "Preparar: " + sql);
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    TEST_CHECK(step.is_success() && step.value(), "Fila: " + sql);
    return stmt.get_text(0);
}
long long num(SqliteDatabase& db, const std::string& sql) { return std::stoll(text(db, sql)); }

std::vector<fs::path> files_in(const fs::path& dir, const std::string& suffix = "") {
    std::vector<fs::path> out;
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return out;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        const std::string n = e.path().filename().u8string();
        if (suffix.empty() || (n.size() >= suffix.size() && n.compare(n.size() - suffix.size(), suffix.size(), suffix) == 0)) {
            out.push_back(e.path());
        }
    }
    std::sort(out.begin(), out.end());
    return out;
}

void write_text(const fs::path& p, const std::string& t) {
    fs::create_directories(p.parent_path());
    std::ofstream out(p, std::ios::binary);
    out << t;
}

fs::path copy_migrations_upto(const fs::path& to, int max_version) {
    fs::create_directories(to);
    for (const auto& e : fs::directory_iterator(fs::u8path(SOURCE_MIGRATIONS_DIR))) {
        if (e.path().extension() != ".sql") continue;
        if (std::stoi(e.path().filename().string().substr(0, 3)) <= max_version) {
            fs::copy_file(e.path(), to / e.path().filename(), fs::copy_options::overwrite_existing);
        }
    }
    return to;
}

// Entorno completo: base real migrada, servicios reales, Dueño con sesión.
struct Env {
    fs::path dir;
    SqliteDatabase db;
    ServiceRegistry reg;
    std::shared_ptr<SqliteBackupService> backup;
    std::string owner_id;

    explicit Env(const std::string& name, long long filler_rows = 0) {
        dir = g_root / fs::u8path(name + u8" ñ");
        fs::create_directories(dir);
        StartupOptions o;
        o.db_path = dir / "cafeteria.db";
        o.migrations_dir = fs::u8path(SOURCE_MIGRATIONS_DIR);
        o.backups_dir = dir / "backups";
        auto r = open_and_migrate_database(db, o);
        TEST_CHECK(r.ok, "Base: " + r.message_utf8);
        reg = ServiceRegistry::create_sqlite_registry(db, dir.u8string(), SOURCE_MIGRATIONS_DIR);
        backup = std::dynamic_pointer_cast<SqliteBackupService>(reg.backup_service);
        TEST_CHECK(backup != nullptr, "El servicio de respaldos debe ser el real");
        auto own = reg.account_service->create_initial_owner("dueno_b12", "Dueño B12", kOwnerPwd, "dueno@cafeteria-itch.edu.mx");
        TEST_CHECK(own.is_success(), "Dueño: " + own.error_message());
        owner_id = reg.account_service->current_user().value().id;
        if (filler_rows > 0) {
            TEST_CHECK(db.execute("CREATE TABLE relleno_b12 (x TEXT NOT NULL);").is_success(), "Tabla de relleno");
            TEST_CHECK(db.execute("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM n WHERE i < " + std::to_string(filler_rows) +
                                  ") INSERT INTO relleno_b12 SELECT hex(randomblob(100)) FROM n;").is_success(), "Relleno");
        }
    }
    ~Env() {
        reg = ServiceRegistry();
        backup.reset();
        db.close();
    }
    fs::path auto_dir() const { return dir / "backups" / "automaticos"; }

    void login(const std::string& user, const std::string& pwd) {
        reg.account_service->logout();
        auto r = reg.account_service->login(user, pwd);
        TEST_CHECK(r.is_success(), "Iniciar sesión " + user + ": " + r.error_message());
    }
    void add_cashier(const std::string& name = "cajero_b12") {
        CreateUserDto c{name, "Cajero B12", kCashierPwd, "role-cashier", name + "@cafeteria-itch.edu.mx", std::nullopt};
        auto r = reg.account_service->create_user(c);
        TEST_CHECK(r.is_success(), "Cajero: " + r.error_message());
    }
    void open_shift() {
        auto r = reg.shift_service->open_shift("reg-01", Money::from_cents(10000));
        TEST_CHECK(r.is_success(), "Abrir turno: " + r.error_message());
    }
    long long close_shift_timed() {
        const auto t = Clock::now();
        auto r = reg.shift_service->close_shift(Money::from_cents(10000));
        const auto elapsed = ms_since(t);
        TEST_CHECK(r.is_success(), "Cerrar turno: " + r.error_message());
        return elapsed;
    }
    // Abre y cierra un turno (cada cierre dispara el automático) y espera a que termine.
    void open_close_and_wait() {
        open_shift();
        close_shift_timed();
        TEST_CHECK(backup->wait_for_auto_backup(kWaitMs), "El respaldo automático debe terminar");
    }
    AutoBackupStatusDto status() {
        auto s = reg.backup_service->get_auto_backup_status();
        TEST_CHECK(s.is_success(), "get_auto_backup_status: " + s.error_message());
        return s.value();
    }
    long long count_files(const std::string& where) { return num(db, "SELECT COUNT(*) FROM backup_files WHERE " + where + ";"); }
};

fs::path make_usb(const std::string& name) {
    fs::path p = g_root / fs::u8path(u8"USB " + name + u8" ñ");
    fs::create_directories(p);
    return p;
}

} // namespace

// --- 1. Automático activado: respaldo íntegro y registrado; bases nuevas ya activadas ---
static void test_auto_backup_created_on_close_shift() {
    std::cout << "[TEST] B12: cierre de turno con automático activado crea un respaldo íntegro y registrado..." << std::endl;
    Env env("auto basico");
    TEST_CHECK(env.reg.backup_service->get_status().value().is_enabled, "Una base nueva arranca con el respaldo automático activado");
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM audit_events WHERE event_type = 'BACKUP_AUTO_ACTIVADO';") == 1,
               "La activación por la migración queda en la bitácora");
    TEST_CHECK(env.status().state == "IDLE", "Antes de cerrar turno el estado es IDLE");

    env.open_shift();
    env.close_shift_timed();
    TEST_CHECK(env.backup->wait_for_auto_backup(kWaitMs), "El automático debe terminar");
    const auto st = env.status();
    TEST_CHECK(st.state == "DONE" && st.local_ok && st.error.empty() && st.secondary_status == "NONE" && !st.secondary_configured,
               "Estado: " + st.state + " / " + st.error);
    TEST_CHECK(!st.started_at.empty() && !st.finished_at.empty() && !st.shift_id.empty(), "Horas y turno en el estado");
    TEST_CHECK(st.local_file_name.rfind("auto-", 0) == 0 && st.local_file_name.find('\\') == std::string::npos &&
                   st.local_file_name.find('/') == std::string::npos, "Solo el nombre del archivo: " + st.local_file_name);

    const auto files = files_in(env.auto_dir(), ".db");
    TEST_CHECK(files.size() == 1 && files[0].filename().u8string() == st.local_file_name, "Un archivo .db en backups\\automaticos");
    TEST_CHECK(files_in(env.auto_dir(), ".tmp").empty(), "Sin archivos temporales");
    auto info = SqliteDatabase::inspect_file(files[0]);
    TEST_CHECK(info.is_success() && info.value().integrity_ok && info.value().has_active_owner &&
                   info.value().schema_version == num(env.db, "SELECT MAX(version) FROM schema_migrations;"),
               "El respaldo debe estar íntegro, completo y en la versión actual");

    TEST_CHECK(env.count_files("kind = 'AUTO' AND destination = 'DATA' AND status = 'ACTIVE'") == 1, "Registrado como AUTO / DATA");
    FileFingerprint fp;
    TEST_CHECK(fingerprint_file(files[0], fp), "Hash del respaldo");
    TEST_CHECK(text(env.db, "SELECT sha256 FROM backup_files WHERE kind = 'AUTO';") == fp.sha256 &&
                   num(env.db, "SELECT size_bytes FROM backup_files WHERE kind = 'AUTO';") == fp.size, "Tamaño y hash registrados");
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM audit_events WHERE event_type = 'BACKUP_AUTO' AND user_id = '" + env.owner_id + "';") == 1,
               "Bitácora con el usuario que cerró el turno");
    TEST_CHECK(text(env.db, "SELECT details FROM audit_events WHERE event_type = 'BACKUP_AUTO';").find(env.dir.u8string()) == std::string::npos,
               "La bitácora no lleva rutas absolutas");
    const auto manual = env.reg.backup_service->get_status().value();
    TEST_CHECK(manual.last_status == "EXITOSA" && manual.last_backup_time != "Nunca", "get_status refleja el último respaldo");
}

// --- 2. Desactivado: no crea nada ---
static void test_disabled_creates_nothing() {
    std::cout << "[TEST] B12: automático desactivado no crea nada..." << std::endl;
    Env env("auto desactivado");
    TEST_CHECK(env.reg.backup_service->set_backup_enabled(false).is_success(), "Desactivar");
    env.open_shift();
    env.close_shift_timed();
    TEST_CHECK(env.backup->wait_for_auto_backup(kWaitMs), "Nada que esperar");
    TEST_CHECK(files_in(env.auto_dir()).empty() && !fs::exists(env.auto_dir()), "No se crea la carpeta ni archivos");
    TEST_CHECK(env.count_files("kind = 'AUTO'") == 0, "Sin filas AUTO");
    TEST_CHECK(env.status().state == "IDLE" && !env.status().is_enabled, "Estado IDLE y desactivado");
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM shifts WHERE is_open = 0;") == 1, "El turno sí se cerró");
}

// --- 3. Segunda carpeta disponible: copia verificada ---
static void test_secondary_folder_receives_verified_copy() {
    std::cout << "[TEST] B12: segunda carpeta disponible recibe una copia verificada..." << std::endl;
    Env env("auto segunda");
    const fs::path usb = make_usb("buena");
    TEST_CHECK(env.reg.backup_service->set_secondary_backup_dir(usb.u8string()).is_success(), "Configurar la segunda carpeta");
    TEST_CHECK(env.reg.backup_service->get_secondary_backup_dir().value() == usb.u8string(), "Se lee lo guardado");
    TEST_CHECK(!fs::exists(usb / ".escritura-prueba-cafeteria.tmp"), "El archivo de prueba se borra");
    env.open_close_and_wait();
    const auto st = env.status();
    TEST_CHECK(st.state == "DONE" && st.local_ok && st.secondary_status == "OK" && st.secondary_configured && st.error.empty(),
               "Estado: " + st.state + " / " + st.secondary_status + " / " + st.error);
    const auto sec_files = files_in(usb / "CafeteriaITCH", ".db");
    TEST_CHECK(sec_files.size() == 1 && sec_files[0].filename().u8string() == st.local_file_name, "La copia está en <segunda>\\CafeteriaITCH");
    FileFingerprint a, b;
    TEST_CHECK(fingerprint_file(env.auto_dir() / st.local_file_name, a) && fingerprint_file(sec_files[0], b) &&
                   a.sha256 == b.sha256 && a.size == b.size, "La copia de la USB es idéntica a la local");
    auto info = SqliteDatabase::inspect_file(sec_files[0]);
    TEST_CHECK(info.is_success() && info.value().integrity_ok, "Y es una base íntegra");
    TEST_CHECK(env.count_files("kind = 'AUTO' AND destination = 'SECONDARY' AND status = 'ACTIVE' AND sha256 = '" + a.sha256 + "'") == 1,
               "Registrada como AUTO / SECONDARY");
    TEST_CHECK(files_in(usb / "CafeteriaITCH", ".tmp").empty(), "Sin temporales en la USB");
}

// --- 4. Segunda carpeta no disponible o sin permiso: el cierre y la copia local se hacen igual ---
static void test_secondary_unavailable_does_not_block() {
    std::cout << "[TEST] B12: segunda carpeta inexistente o sin permiso: cierre y copia local intactos..." << std::endl;
    {
        Env env("auto sin usb");
        const fs::path usb = make_usb("quitada");
        TEST_CHECK(env.reg.backup_service->set_secondary_backup_dir(usb.u8string()).is_success(), "Configurar");
        std::error_code ec;
        fs::remove_all(usb, ec);  // la USB se desconecta
        env.open_shift();
        env.close_shift_timed();
        TEST_CHECK(env.backup->wait_for_auto_backup(kWaitMs), "Terminar");
        const auto st = env.status();
        TEST_CHECK(st.state == "DONE" && st.local_ok && st.secondary_status == "UNAVAILABLE", "Estado: " + st.state + " / " + st.secondary_status);
        TEST_CHECK(st.error.find("segunda carpeta no está disponible") != std::string::npos, "Motivo claro: " + st.error);
        TEST_CHECK(files_in(env.auto_dir(), ".db").size() == 1, "La copia local existe");
        TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM shifts WHERE is_open = 0;") == 1, "El turno quedó cerrado");
        TEST_CHECK(env.count_files("kind = 'AUTO' AND destination = 'SECONDARY'") == 0, "Nada registrado en la segunda carpeta");
        // Vuelve la USB: la siguiente copia lo intenta de nuevo (sin método nuevo).
        fs::create_directories(usb);
        env.open_close_and_wait();
        TEST_CHECK(env.status().secondary_status == "OK" && env.count_files("kind = 'AUTO' AND destination = 'SECONDARY'") == 1,
                   "Con la USB de vuelta, la siguiente copia sí llega");
    }
    {
        Env env("auto usb protegida");
        const fs::path usb = make_usb("protegida");
        TEST_CHECK(env.reg.backup_service->set_secondary_backup_dir(usb.u8string()).is_success(), "Configurar");
        // Se ocupa el nombre del archivo de prueba con una carpeta: no se puede crear el archivo (sin permiso de escritura).
        fs::create_directories(usb / ".escritura-prueba-cafeteria.tmp");
        env.open_shift();
        env.close_shift_timed();
        TEST_CHECK(env.backup->wait_for_auto_backup(kWaitMs), "Terminar");
        const auto st = env.status();
        TEST_CHECK(st.state == "DONE" && st.local_ok && st.secondary_status == "UNAVAILABLE" &&
                       st.error.find("No se puede escribir") != std::string::npos, "Sin permiso: " + st.secondary_status + " / " + st.error);
        TEST_CHECK(files_in(env.auto_dir(), ".db").size() == 1 && num(env.db, "SELECT COUNT(*) FROM shifts WHERE is_open = 0;") == 1,
                   "Copia local y cierre intactos");
    }
}

// --- 5. Validación y persistencia de la segunda carpeta ---
static void test_secondary_dir_validation_and_persistence() {
    std::cout << "[TEST] B12: validar, guardar, borrar y persistir la segunda carpeta..." << std::endl;
    Env env("segunda validacion");
    auto& svc = *env.reg.backup_service;
    TEST_CHECK(svc.get_secondary_backup_dir().is_success() && svc.get_secondary_backup_dir().value().empty(), "Arranca vacía");
    TEST_CHECK(svc.set_secondary_backup_dir("").error_code() == ErrorCode::InvalidInput, "Vacía rechazada");
    TEST_CHECK(svc.set_secondary_backup_dir("carpeta\\relativa").error_code() == ErrorCode::InvalidInput, "Relativa rechazada");
    const auto not_exists = svc.set_secondary_backup_dir((g_root / "no existe esta carpeta").u8string());
    TEST_CHECK(not_exists.error_code() == ErrorCode::InvalidInput && not_exists.error_message().find("no existe") != std::string::npos,
               "Inexistente: " + not_exists.error_message());
    const fs::path own = env.dir / "backups";
    fs::create_directories(own);
    TEST_CHECK(svc.set_secondary_backup_dir(own.u8string()).error_code() == ErrorCode::InvalidInput, "La carpeta de respaldos del programa se rechaza");
    TEST_CHECK(svc.set_secondary_backup_dir((own / "automaticos").u8string()).error_code() == ErrorCode::InvalidInput ||
                   !fs::exists(own / "automaticos"), "Una subcarpeta propia también");
    const fs::path blocked = make_usb("bloqueada");
    fs::create_directories(blocked / ".escritura-prueba-cafeteria.tmp");
    const auto no_write = svc.set_secondary_backup_dir(blocked.u8string());
    TEST_CHECK(no_write.error_code() == ErrorCode::InvalidInput && no_write.error_message().find("No se puede escribir") != std::string::npos,
               "Sin permiso de escritura: " + no_write.error_message());
    TEST_CHECK(svc.get_secondary_backup_dir().value().empty(), "Una ruta inválida no se guarda");

    const fs::path usb = make_usb("valida");
    TEST_CHECK(svc.set_secondary_backup_dir(("  " + usb.u8string() + "  ")).is_success(), "Los espacios sobrantes se ignoran");
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM audit_events WHERE event_type = 'BACKUP_SECONDARY_DIR';") == 1, "Auditado");
    // Persistencia: otro servicio sobre la misma base.
    {
        auto again = std::make_shared<SqliteBackupService>(env.db, env.reg.account_service, env.dir.u8string(), SOURCE_MIGRATIONS_DIR);
        TEST_CHECK(again->get_secondary_backup_dir().value() == usb.u8string(), "La ruta persiste en la base");
    }
    TEST_CHECK(svc.clear_secondary_backup_dir().is_success() && svc.get_secondary_backup_dir().value().empty(), "Se puede borrar");

    // Permisos: el Cajero no puede ver ni cambiar la segunda carpeta.
    env.add_cashier();
    env.login("cajero_b12", kCashierPwd);
    TEST_CHECK(svc.get_secondary_backup_dir().error_code() == ErrorCode::Forbidden, "Cajero: leer rechazado");
    TEST_CHECK(svc.set_secondary_backup_dir(usb.u8string()).error_code() == ErrorCode::Forbidden, "Cajero: guardar rechazado");
    TEST_CHECK(svc.clear_secondary_backup_dir().error_code() == ErrorCode::Forbidden, "Cajero: borrar rechazado");
    TEST_CHECK(svc.list_backups(BackupListFilterDto{}).error_code() == ErrorCode::Forbidden, "Cajero: lista rechazada");
    env.reg.account_service->logout();
    TEST_CHECK(svc.get_auto_backup_status().error_code() == ErrorCode::Forbidden, "Sin sesión: estado rechazado");
    TEST_CHECK(svc.get_secondary_backup_dir().error_code() == ErrorCode::Forbidden && svc.list_backups(BackupListFilterDto{}).error_code() == ErrorCode::Forbidden,
               "Sin sesión: rechazado");
}

// --- 6. Un fallo del respaldo local nunca revierte el cierre de turno ---
static void test_local_failure_does_not_revert_close() {
    std::cout << "[TEST] B12: fallo forzado del respaldo local no revierte el cierre de turno..." << std::endl;
    Env env("auto falla local");
    // backups\automaticos ocupado por un archivo: la carpeta no se puede crear.
    write_text(env.dir / "backups" / "automaticos", "soy un archivo");
    env.open_shift();
    const auto shift_id = text(env.db, "SELECT id FROM shifts WHERE is_open = 1;");
    env.close_shift_timed();
    TEST_CHECK(env.backup->wait_for_auto_backup(kWaitMs), "Terminar");
    const auto st = env.status();
    TEST_CHECK(st.state == "FAILED" && !st.local_ok && !st.error.empty(), "Estado FAILED con motivo: " + st.error);
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM shifts WHERE id = '" + shift_id + "' AND is_open = 0;") == 1, "El turno quedó cerrado");
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM audit_events WHERE event_type = 'SHIFT_CLOSED';") == 1, "Y su auditoría existe");
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM audit_events WHERE event_type = 'BACKUP_AUTO_FAILED';") == 1, "El fallo queda en la bitácora");
    TEST_CHECK(env.count_files("kind = 'AUTO'") == 0, "Nada registrado");
    TEST_CHECK(env.reg.backup_service->get_status().value().last_status == "FALLIDA", "get_status dice FALLIDA");
    TEST_CHECK(!env.reg.shift_service->get_current_shift().is_success(), "No hay turno abierto");
}

// --- 7. Un respaldo manual en curso no se pisa con el automático ---
static void test_manual_backup_in_progress_is_not_trampled() {
    std::cout << "[TEST] B12: un respaldo manual en curso no se pisa con el automático..." << std::endl;
    Env env("auto y manual", 300000);
    env.open_shift();
    const fs::path manual = env.dir / "manual.db";
    std::atomic<bool> manual_ok{false};
    std::thread t([&] { manual_ok = env.reg.backup_service->trigger_local_backup(manual.u8string()).is_success(); });
    const auto deadline = Clock::now() + std::chrono::seconds(30);
    while (!env.backup->operation_in_progress() && Clock::now() < deadline) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    TEST_CHECK(env.backup->operation_in_progress(), "El manual debe estar en curso");
    // Mientras el manual verifica su copia, llega el cierre de turno: el automático espera, no pisa.
    env.backup->on_shift_closed(text(env.db, "SELECT id FROM shifts WHERE is_open = 1;"), env.owner_id);
    const auto st0 = env.status();
    TEST_CHECK(st0.state == "WAITING" || st0.state == "RUNNING" || st0.state == "DONE", "Estado inmediato: " + st0.state);
    bool saw_waiting = st0.state == "WAITING";
    bool running_during_manual = false;
    while (env.backup->operation_in_progress() && Clock::now() < deadline) {
        const auto s = env.status();
        if (s.state == "WAITING") saw_waiting = true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    t.join();
    (void)running_during_manual;
    TEST_CHECK(manual_ok.load(), "El respaldo manual debe terminar bien");
    TEST_CHECK(env.backup->wait_for_auto_backup(kWaitMs), "El automático termina después");
    const auto st = env.status();
    TEST_CHECK(st.state == "DONE" && st.local_ok, "El automático se hizo después del manual: " + st.state + " " + st.error);
    TEST_CHECK(saw_waiting, "El automático esperó (WAITING) mientras el manual estaba en curso");
    auto info_manual = SqliteDatabase::inspect_file(manual);
    auto info_auto = SqliteDatabase::inspect_file(env.auto_dir() / st.local_file_name);
    TEST_CHECK(info_manual.is_success() && info_manual.value().integrity_ok && info_auto.is_success() && info_auto.value().integrity_ok,
               "Los dos archivos están íntegros");

    // Y al revés: con el automático en marcha, un manual recibe el Conflict de siempre.
    env.close_shift_timed();
    bool conflict_seen = false;
    for (int i = 0; i < 200 && !conflict_seen; ++i) {
        if (env.backup->operation_in_progress()) {
            auto r = env.reg.backup_service->trigger_local_backup((env.dir / "manual2.db").u8string());
            conflict_seen = r.error_code() == ErrorCode::Conflict;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    TEST_CHECK(env.backup->wait_for_auto_backup(kWaitMs), "Terminar");
    TEST_CHECK(conflict_seen, "Un manual durante el automático recibe Conflict");
    TEST_CHECK(!fs::exists(env.dir / "manual2.db"), "Y no crea archivo");
}

// --- 8. Cuenta Cajero: cierra turno, el automático se hace; no cambia la segunda carpeta ni lista ---
static void test_cashier_closes_shift_and_auto_runs() {
    std::cout << "[TEST] B12: el Cajero cierra turno y el automático se hace..." << std::endl;
    Env env("auto cajero");
    env.add_cashier();
    env.login("cajero_b12", kCashierPwd);
    TEST_CHECK(!env.reg.account_service->has_permission("backup.manage"), "El Cajero no tiene backup.manage");
    env.open_close_and_wait();
    const auto st = env.status();
    TEST_CHECK(st.state == "DONE" && st.local_ok, "El automático se hizo: " + st.state + " " + st.error);
    const auto cashier_id = text(env.db, "SELECT id FROM users WHERE username = 'cajero_b12';");
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM audit_events WHERE event_type = 'BACKUP_AUTO' AND user_id = '" + cashier_id + "';") == 1,
               "La bitácora nombra al Cajero");
    TEST_CHECK(env.reg.backup_service->trigger_local_backup((env.dir / "x.db").u8string()).error_code() == ErrorCode::Forbidden,
               "El Cajero sigue sin poder hacer respaldos manuales");
}

// --- 9. Lista de respaldos ---
static void test_list_backups() {
    std::cout << "[TEST] B12: lista de respaldos ordenada, paginada y con el estado de cada archivo..." << std::endl;
    Env env("lista");
    const fs::path usb = make_usb("lista");
    TEST_CHECK(env.reg.backup_service->set_secondary_backup_dir(usb.u8string()).is_success(), "Segunda carpeta");
    const fs::path manual = env.dir / "manuales" / "uno.db";
    TEST_CHECK(env.reg.backup_service->trigger_local_backup(manual.u8string()).is_success(), "Manual");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    env.open_close_and_wait();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    env.open_close_and_wait();

    auto all = env.reg.backup_service->list_backups(BackupListFilterDto{});
    TEST_CHECK(all.is_success(), "Lista: " + all.error_message());
    const auto& items = all.value();
    // 1 manual (LOCAL) + 2 automáticos x 2 destinos (DATA y SECONDARY); la limpieza automática no borra nada (30 por tipo).
    TEST_CHECK(items.size() == 5, "5 respaldos registrados, hay " + std::to_string(items.size()));
    int auto_data = 0, auto_sec = 0, local = 0;
    for (const auto& it : items) {
        if (it.kind == "AUTO" && it.destination == "DATA") ++auto_data;
        if (it.kind == "AUTO" && it.destination == "SECONDARY") ++auto_sec;
        if (it.kind == "LOCAL" && it.destination == "DATA") ++local;
        TEST_CHECK(it.file_exists && it.unchanged && it.size_bytes > 0 && !it.created_at.empty() && !it.path.empty() && !it.id.empty(),
                   "Cada archivo existe y no cambió: " + it.path);
    }
    TEST_CHECK(auto_data == 2 && auto_sec == 2 && local == 1, "Tipos y destinos correctos");
    TEST_CHECK(std::is_sorted(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.created_at > b.created_at; }) ||
                   std::is_sorted(items.begin(), items.end(), [](const auto& a, const auto& b) { return a.created_at >= b.created_at; }),
               "Más recientes primero");
    TEST_CHECK(items.back().kind == "LOCAL", "El manual, el más antiguo, va al final");

    // Paginación
    auto p1 = env.reg.backup_service->list_backups(BackupListFilterDto{2, 0});
    auto p2 = env.reg.backup_service->list_backups(BackupListFilterDto{2, 2});
    auto p3 = env.reg.backup_service->list_backups(BackupListFilterDto{2, 4});
    TEST_CHECK(p1.value().size() == 2 && p2.value().size() == 2 && p3.value().size() == 1, "Páginas de 2");
    TEST_CHECK(p1.value()[0].id == items[0].id && p2.value()[0].id == items[2].id && p3.value()[0].id == items[4].id, "Mismo orden por páginas");
    TEST_CHECK(env.reg.backup_service->list_backups(BackupListFilterDto{0, 0}).error_code() == ErrorCode::InvalidInput &&
                   env.reg.backup_service->list_backups(BackupListFilterDto{501, 0}).error_code() == ErrorCode::InvalidInput &&
                   env.reg.backup_service->list_backups(BackupListFilterDto{10, -1}).error_code() == ErrorCode::InvalidInput,
               "Límites inválidos rechazados");

    // Estado de cada archivo: borrado y cambiado
    const auto deleted = fs::u8path(items[0].path);
    const auto changed = fs::u8path(items[1].path);
    std::error_code ec;
    fs::remove(deleted, ec);
    { std::ofstream out(changed, std::ios::binary | std::ios::app); out << "basura"; }
    auto after = env.reg.backup_service->list_backups(BackupListFilterDto{});
    TEST_CHECK(after.is_success(), "Lista tras cambios");
    TEST_CHECK(!after.value()[0].file_exists && !after.value()[0].unchanged, "Borrado: no existe");
    TEST_CHECK(after.value()[1].file_exists && !after.value()[1].unchanged, "Cambiado: existe pero no está sin cambios");
    TEST_CHECK(after.value()[2].file_exists && after.value()[2].unchanged, "El resto sigue igual");

    // Permisos: backup.restore sin backup.manage también puede listar.
    auto role = env.reg.account_service->create_role(CreateRoleDto{"Solo restaurar", "r", {"backup.restore"}});
    TEST_CHECK(role.is_success(), "Rol");
    CreateUserDto u{"restaura_b12", "Restaura", kCashierPwd, role.value().id, "r@cafeteria-itch.edu.mx", std::nullopt};
    TEST_CHECK(env.reg.account_service->create_user(u).is_success(), "Usuario con backup.restore");
    env.login("restaura_b12", kCashierPwd);
    TEST_CHECK(env.reg.backup_service->list_backups(BackupListFilterDto{}).is_success(), "backup.restore puede listar");
    TEST_CHECK(env.reg.backup_service->get_secondary_backup_dir().error_code() == ErrorCode::Forbidden, "Pero no ve la segunda carpeta");
}

// --- 10. Limpieza: tipo AUTO por destino, nunca el último ---
static void test_cleanup_covers_auto_by_destination() {
    std::cout << "[TEST] B12: limpieza cubre AUTO por destino y respeta el último de cada tipo y destino..." << std::endl;
    Env env("limpieza auto");
    const fs::path usb = make_usb("limpieza");
    TEST_CHECK(env.reg.backup_service->set_secondary_backup_dir(usb.u8string()).is_success(), "Segunda carpeta");
    TEST_CHECK(env.reg.backup_service->set_retention_policy(BackupRetentionPolicyDto{1, 0}).is_success(), "Conservar 1, sin días");
    // Dos respaldos manuales (LOCAL): la limpieza automática NO debe tocarlos.
    const fs::path m1 = env.dir / "manuales" / "a.db", m2 = env.dir / "manuales" / "b.db";
    TEST_CHECK(env.reg.backup_service->trigger_local_backup(m1.u8string()).is_success(), "Manual 1");
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    TEST_CHECK(env.reg.backup_service->trigger_local_backup(m2.u8string()).is_success(), "Manual 2");

    for (int i = 0; i < 3; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(30));
        env.open_close_and_wait();
    }
    // Tras 3 automáticos con conservar 1: solo queda el último en cada destino, y sus archivos.
    TEST_CHECK(env.count_files("kind = 'AUTO' AND destination = 'DATA' AND status = 'ACTIVE'") == 1, "Un AUTO / DATA activo");
    TEST_CHECK(env.count_files("kind = 'AUTO' AND destination = 'SECONDARY' AND status = 'ACTIVE'") == 1, "Un AUTO / SECONDARY activo");
    TEST_CHECK(env.count_files("kind = 'AUTO' AND status = 'DELETED'") == 4, "Los 4 antiguos quedaron DELETED");
    TEST_CHECK(files_in(env.auto_dir(), ".db").size() == 1 && files_in(usb / "CafeteriaITCH", ".db").size() == 1, "Solo un archivo en cada destino");
    const std::string newest = env.status().local_file_name;
    TEST_CHECK(files_in(env.auto_dir(), ".db")[0].filename().u8string() == newest && files_in(usb / "CafeteriaITCH", ".db")[0].filename().u8string() == newest,
               "Y es el más reciente");
    TEST_CHECK(fs::exists(m1) && fs::exists(m2) && env.count_files("kind = 'LOCAL' AND status = 'ACTIVE'") == 2,
               "La limpieza automática no toca los respaldos manuales");
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM audit_events WHERE event_type = 'BACKUP_CLEANUP';") == 2, "La limpieza automática queda en la bitácora");

    // Limpieza manual: borra el LOCAL antiguo, conserva el último de cada tipo y destino.
    auto preview = env.reg.backup_service->preview_backup_cleanup();
    TEST_CHECK(preview.is_success() && preview.value().to_delete.size() == 1 && preview.value().to_delete[0].kind == "LOCAL" &&
                   preview.value().to_delete[0].destination == "DATA", "Vista previa: solo el LOCAL antiguo");
    auto clean = env.reg.backup_service->cleanup_old_backups();
    TEST_CHECK(clean.is_success() && clean.value().to_delete.size() == 1 && !fs::exists(m1) && fs::exists(m2), "Limpieza manual");
    TEST_CHECK(env.count_files("kind = 'AUTO' AND status = 'ACTIVE'") == 2, "Los últimos AUTO de cada destino se conservan");

    // Otro cierre de turno más: sigue habiendo exactamente uno por destino.
    env.open_close_and_wait();
    TEST_CHECK(env.count_files("kind = 'AUTO' AND destination = 'DATA' AND status = 'ACTIVE'") == 1, "Sigue habiendo uno por destino");
    TEST_CHECK(files_in(env.auto_dir(), ".db").size() == 1, "Un archivo local");
}

// --- 11. Migración 016: activación única respetando la decisión del Dueño ---
static void test_migration_016() {
    std::cout << "[TEST] B12: migración 016 repetible; activa solo si nadie apagó el respaldo..." << std::endl;
    const fs::path real = fs::u8path(SOURCE_MIGRATIONS_DIR);
    auto make_old_db = [&](const std::string& name) {
        const fs::path dir = g_root / fs::u8path(name + u8" ñ");
        fs::create_directories(dir);
        SqliteDatabase db;
        StartupOptions o;
        o.db_path = dir / "cafeteria.db";
        o.migrations_dir = copy_migrations_upto(dir / "m15", 15);
        o.backups_dir = dir / "backups";
        auto r = open_and_migrate_database(db, o);
        TEST_CHECK(r.ok && r.schema_version_after == 15, "Base en la versión 15: " + r.message_utf8);
        return std::make_pair(dir, std::move(r));
    };
    auto upgrade = [&](const fs::path& dir, SqliteDatabase& db) {
        StartupOptions o;
        o.db_path = dir / "cafeteria.db";
        o.migrations_dir = real;
        o.backups_dir = dir / "backups";
        auto r = open_and_migrate_database(db, o);
        TEST_CHECK(r.ok, "Actualizar: " + r.message_utf8);
        return r;
    };
    auto set_sql = [&](const fs::path& dir, const std::string& sql) {
        SqliteDatabase db;
        TEST_CHECK(db.open(dir / "cafeteria.db").is_success(), "Abrir");
        TEST_CHECK(db.execute(sql).is_success(), "SQL: " + sql);
        db.close();
    };

    {   // a) nadie lo apagó nunca (is_enabled = 0, sin BACKUP_DISABLED): se activa y se registra
        auto [dir, r0] = make_old_db("mig a");
        set_sql(dir, "INSERT INTO backup_files (id,kind,path,size_bytes,sha256,created_at,created_at_utc,status) VALUES "
                     "('f1','LOCAL','C:/x/uno.db',10,'aa','2026-01-01 10:00:00','2026-01-01T16:00:00.000Z','ACTIVE'),"
                     "('f2','PRE_RESTORE','C:/x/dos.db',20,'bb','2026-01-02 10:00:00','2026-01-02T16:00:00.000Z','ACTIVE');");
        SqliteDatabase db;
        auto r = upgrade(dir, db);
        TEST_CHECK(num(db, "SELECT is_enabled FROM backup_state WHERE id = 1;") == 1, "Activado");
        TEST_CHECK(num(db, "SELECT COUNT(*) FROM audit_events WHERE event_type = 'BACKUP_AUTO_ACTIVADO';") == 1, "Con su evento");
        TEST_CHECK(num(db, "SELECT COUNT(*) FROM backup_files WHERE kind <> 'PRE_MIGRATION';") == 2 && text(db, "SELECT destination FROM backup_files WHERE id = 'f1';") == "DATA" &&
                       text(db, "SELECT sha256 FROM backup_files WHERE id = 'f2';") == "bb" && num(db, "SELECT rowid FROM backup_files WHERE id = 'f2';") == 2,
                   "Filas, rowid y destino conservados");
        TEST_CHECK(num(db, "SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name IN "
                           "('idx_backup_files_kind_created','idx_backup_files_path','idx_backup_files_kind_dest_created');") == 3, "Índices");
        TEST_CHECK(db.execute("INSERT INTO backup_files (id,kind,path,size_bytes,sha256,created_at,created_at_utc) VALUES ('f3','AUTO','p',1,'h','a','b');").is_success(),
                   "Acepta el tipo AUTO");
        TEST_CHECK(!db.execute("INSERT INTO backup_files (id,kind,path,size_bytes,sha256,created_at,created_at_utc) VALUES ('f4','OTRO','p',1,'h','a','b');").is_success(),
                   "Rechaza un tipo desconocido");
        TEST_CHECK(!db.execute("INSERT INTO backup_files (id,kind,path,size_bytes,sha256,created_at,created_at_utc,destination) VALUES ('f5','AUTO','p',1,'h','a','b','OTRA');").is_success(),
                   "Rechaza un destino desconocido");
        // Repetible: volver a correr el runner no cambia nada.
        MigrationRunner runner(db);
        TEST_CHECK(runner.run_pending_migrations_from_dir(real).is_success(), "Repetir el runner");
        TEST_CHECK(num(db, "SELECT COUNT(*) FROM audit_events WHERE event_type = 'BACKUP_AUTO_ACTIVADO';") == 1 &&
                       num(db, "SELECT COUNT(*) FROM schema_migrations WHERE version = 16;") == 1, "Sin duplicar nada");
        db.close();
    }
    {   // b) el Dueño lo apagó alguna vez (hay BACKUP_DISABLED): se respeta
        auto [dir, r0] = make_old_db("mig b");
        set_sql(dir, "INSERT INTO audit_events (id,event_type,user_id,details,created_at,created_at_utc) VALUES "
                     "('a1','BACKUP_DISABLED',NULL,'Respaldo desactivado','2026-01-01 10:00:00','2026-01-01T16:00:00.000Z');");
        SqliteDatabase db;
        auto r = upgrade(dir, db);
        TEST_CHECK(num(db, "SELECT is_enabled FROM backup_state WHERE id = 1;") == 0, "Sigue desactivado");
        TEST_CHECK(num(db, "SELECT COUNT(*) FROM audit_events WHERE event_type = 'BACKUP_AUTO_ACTIVADO';") == 0, "Sin evento");
        db.close();
    }
    {   // c) ya estaba activado: no se toca ni se registra
        auto [dir, r0] = make_old_db("mig c");
        set_sql(dir, "UPDATE backup_state SET is_enabled = 1 WHERE id = 1;");
        SqliteDatabase db;
        auto r = upgrade(dir, db);
        TEST_CHECK(num(db, "SELECT is_enabled FROM backup_state WHERE id = 1;") == 1, "Sigue activado");
        TEST_CHECK(num(db, "SELECT COUNT(*) FROM audit_events WHERE event_type = 'BACKUP_AUTO_ACTIVADO';") == 0, "Sin evento (no lo activó la migración)");
        db.close();
    }
}

// --- 12. verify_backup avisa si el respaldo es de una versión más nueva ---
static void test_verify_newer_backup() {
    std::cout << "[TEST] B12: verify_backup explica que el respaldo es de una versión más nueva..." << std::endl;
    Env env("verifica nueva");
    const fs::path ok = env.dir / "ok.db";
    TEST_CHECK(env.reg.backup_service->trigger_local_backup(ok.u8string()).is_success(), "Respaldo");
    auto good = env.reg.backup_service->verify_backup(ok.u8string());
    TEST_CHECK(good.is_success() && good.value().integrity_ok && good.value().detail.empty(), "Un respaldo normal no trae aviso");

    const fs::path newer = env.dir / "nuevo.db";
    fs::copy_file(ok, newer, fs::copy_options::overwrite_existing);
    {
        SqliteDatabase raw;
        TEST_CHECK(raw.open(newer).is_success(), "Abrir copia");
        TEST_CHECK(raw.execute("INSERT INTO schema_migrations (version, description) VALUES (99, '099_futura.sql');").is_success(), "Versión 99");
        raw.close();
    }
    auto v = env.reg.backup_service->verify_backup(newer.u8string());
    TEST_CHECK(v.is_success() && v.value().integrity_ok && v.value().schema_version == 99, "Íntegro y en la versión 99");
    TEST_CHECK(v.value().detail == "El respaldo es de una versión más nueva del programa; actualice el programa antes de restaurarlo",
               "Aviso en lenguaje llano: " + v.value().detail);
    auto r = env.reg.backup_service->restore_from_backup(newer.u8string());
    TEST_CHECK(!r.is_success(), "Y restaurar sigue rechazándolo");
}

// --- 13. Una venta no falla mientras el automático copia (SQLITE_BUSY) ---
static void test_sale_while_auto_backup_copies() {
    std::cout << "[TEST] B12: confirmar una venta mientras el automático copia (base grande)..." << std::endl;
    Env env("venta durante auto", 400000);
    CreateProductDto p;
    p.track_stock = false;  // B14: se vende sin existencia controlada, como antes
    p.sku = "B12-V"; p.name = "Café"; p.category = "Bebidas"; p.price = Money::from_cents(3000);
    auto prod = env.reg.catalog_service->create_product(p);
    TEST_CHECK(prod.is_success(), "Producto: " + prod.error_message());

    env.open_shift();
    const auto close_ms = env.close_shift_timed();
    // El automático copia en su hilo. Mientras tanto se abre otro turno y se vende.
    // Se espera a que el hilo esté COPIANDO (RUNNING) antes de vender: así la venta ocurre durante la copia.
    {
        const auto limit = Clock::now() + std::chrono::seconds(10);
        while (env.status().state != "RUNNING" && Clock::now() < limit) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        TEST_CHECK(env.status().state == "RUNNING", "El automático debe estar copiando: " + env.status().state);
    }
    env.open_shift();
    TEST_CHECK(env.reg.cart_service->add_item(prod.value().id, 2, "").is_success(), "Agregar al carrito");
    const auto st_mid = env.status();
    auto sale = env.reg.sale_service->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), "clave-venta-b12");
    TEST_CHECK(sale.is_success(), "La venta debe confirmarse sin SQLITE_BUSY: " + sale.error_message());
    const bool overlapped = st_mid.state == "RUNNING";
    TEST_CHECK(env.backup->wait_for_auto_backup(kWaitMs), "El automático termina");
    TEST_CHECK(env.status().state == "DONE", "Y se completó: " + env.status().state + " " + env.status().error);
    TEST_CHECK(num(env.db, "SELECT COUNT(*) FROM sales;") == 1 && num(env.db, "SELECT total_cents FROM sales;") == 6000, "La venta quedó guardada ($60.00)");
    TEST_CHECK(overlapped, "La venta se hizo con el automático copiando (estado " + st_mid.state + ")");
    std::cout << "    close_shift volvió en " << close_ms << " ms; la venta se hizo con el automático en estado " << st_mid.state << std::endl;
    // El respaldo es una instantánea íntegra (no incluye necesariamente la venta posterior al cierre).
    auto info = SqliteDatabase::inspect_file(env.auto_dir() / env.status().local_file_name);
    TEST_CHECK(info.is_success() && info.value().integrity_ok, "El respaldo está íntegro");
}

// --- 14. close_shift vuelve igual de rápido; el servicio puede destruirse con el hilo en marcha ---
static void test_close_shift_latency_and_destruction() {
    std::cout << "[TEST] B12: el cierre de turno no espera al respaldo; destruir el servicio no se cuelga..." << std::endl;
    long long with_ms = 0, without_ms = 0;
    {
        Env env("latencia sin", 400000);
        TEST_CHECK(env.reg.backup_service->set_backup_enabled(false).is_success(), "Desactivar");
        env.open_shift();
        without_ms = env.close_shift_timed();
    }
    {
        Env env("latencia con", 400000);
        env.open_shift();
        with_ms = env.close_shift_timed();
        std::cout << "    close_shift: " << without_ms << " ms sin automático, " << with_ms << " ms con automático" << std::endl;
        TEST_CHECK(with_ms < without_ms + 1000, "El cierre con automático no espera a la copia");

        // Destruir los servicios con el hilo todavía copiando: vuelve enseguida y no se cae.
        const auto auto_dir = env.auto_dir();
        const auto t = Clock::now();
        env.reg = ServiceRegistry();
        env.backup.reset();
        const auto destroy_ms = ms_since(t);
        TEST_CHECK(destroy_ms < 5000, "Destruir el servicio tardó " + std::to_string(destroy_ms) + " ms");
        std::this_thread::sleep_for(std::chrono::seconds(4));  // el hilo desprendido termina solo
        // Nada a medias registrado como válido: toda fila AUTO ACTIVE apunta a un archivo íntegro.
        auto prep = env.db.prepare("SELECT path FROM backup_files WHERE kind = 'AUTO' AND status = 'ACTIVE';");
        TEST_CHECK(prep.is_success(), "Consulta");
        auto stmt = std::move(prep.value());
        while (true) {
            auto step = stmt.step();
            if (!step.is_success() || !step.value()) break;
            auto info = SqliteDatabase::inspect_file(fs::u8path(stmt.get_text(0)));
            TEST_CHECK(info.is_success() && info.value().integrity_ok, "Un respaldo registrado debe ser íntegro");
        }
        // Un .tmp huérfano se limpia al volver a crear el servicio.
        write_text(auto_dir / "auto-huerfano.db.tmp", "a medias");
        auto again = std::make_shared<SqliteBackupService>(env.db, env.reg.account_service, env.dir.u8string(), SOURCE_MIGRATIONS_DIR);
        TEST_CHECK(files_in(auto_dir, ".tmp").empty(), "El temporal huérfano se limpia al arrancar el servicio");
    }
}


// Medición (no forma parte de CTest): test_auto_backup --measure <ventas>
// Base con N ventas, una segunda carpeta, y tiempos de close_shift y del respaldo automático completo.
static void measure(long long sales) {
    Env env("medicion", 0);
    const fs::path usb = make_usb("medicion");
    TEST_CHECK(env.reg.backup_service->set_secondary_backup_dir(usb.u8string()).is_success(), "Segunda carpeta");
    TEST_CHECK(env.db.execute("PRAGMA foreign_keys = OFF;").is_success(), "fk");
    TEST_CHECK(env.db.begin_transaction().is_success(), "tx");
    for (long long from = 1; from <= sales; from += 10000) {
        const long long to = std::min(sales, from + 9999);
        const std::string range = "WITH RECURSIVE n(i) AS (SELECT " + std::to_string(from) +
                                  " UNION ALL SELECT i+1 FROM n WHERE i < " + std::to_string(to) + ") ";
        TEST_CHECK(env.db.execute(range +
            "INSERT INTO sales (id, folio, shift_id, user_id, total_cents, idempotency_key, created_at, cashier_name, created_at_utc) "
            "SELECT 'sale-' || lower(hex(randomblob(16))), 'F-' || i, 'shift-x', 'user-x', 12000 + (i % 5000), "
            "'key-' || lower(hex(randomblob(16))), '2026-01-01 10:00:00', 'Cajero', '2026-01-01T16:00:00.000Z' FROM n;").is_success(), "ventas");
        TEST_CHECK(env.db.execute(range +
            "INSERT INTO sale_items (id, sale_id, product_id, product_name, quantity, unit_price_cents, subtotal_cents, notes) "
            "SELECT 'item-' || lower(hex(randomblob(16))), (SELECT id FROM sales WHERE folio = 'F-' || i), 'prod-x', 'Café americano', 2, 3000, 6000, '' FROM n;").is_success(), "líneas");
        TEST_CHECK(env.db.execute(range +
            "INSERT INTO audit_events (id, event_type, user_id, details, created_at, created_at_utc) "
            "SELECT 'audit-' || lower(hex(randomblob(16))), 'SALE_CONFIRMED', NULL, 'Venta F-' || i || ' confirmada', "
            "'2026-01-01 10:00:00', '2026-01-01T16:00:00.000Z' FROM n;").is_success(), "auditoría");
    }
    TEST_CHECK(env.db.commit_transaction().is_success(), "commit");
    TEST_CHECK(env.db.execute("PRAGMA foreign_keys = ON;").is_success(), "fk on");
    std::cout << "Base con " << sales << " ventas: " << fs::file_size(env.dir / "cafeteria.db") / (1024.0 * 1024.0) << " MB" << std::endl;

    env.reg.backup_service->set_backup_enabled(false);
    env.open_shift();
    const auto without_ms = env.close_shift_timed();
    env.reg.backup_service->set_backup_enabled(true);
    env.open_shift();
    const auto t0 = Clock::now();
    const auto with_ms = env.close_shift_timed();
    const auto st_after_close = env.status();
    TEST_CHECK(env.backup->wait_for_auto_backup(kWaitMs), "Terminar");
    const auto total_ms = ms_since(t0);
    const auto st = env.status();
    std::cout << "close_shift sin automatico: " << without_ms << " ms; con automatico: " << with_ms << " ms (estado al volver: "
              << st_after_close.state << ")" << std::endl;
    std::cout << "respaldo automatico completo (local + segunda carpeta + limpieza): " << total_ms << " ms; estado " << st.state
              << ", segunda carpeta " << st.secondary_status << std::endl;
    std::cout << "inicio " << st.started_at << ", fin " << st.finished_at << std::endl;
}

int main(int argc, char** argv) {
    std::error_code ec;
    if (argc >= 3 && std::string(argv[1]) == "--measure") {
        g_root = fs::temp_directory_path(ec) / fs::u8path(u8"cafeteria b12 medicion " + std::to_string(Clock::now().time_since_epoch().count()));
        fs::create_directories(g_root, ec);
        measure(std::atoll(argv[2]));
        cleanup_and_exit(0);
    }
    g_root = fs::temp_directory_path(ec) / fs::u8path(u8"cafeteria b12 auto " + std::to_string(Clock::now().time_since_epoch().count()));
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);

    test_auto_backup_created_on_close_shift();
    test_disabled_creates_nothing();
    test_secondary_folder_receives_verified_copy();
    test_secondary_unavailable_does_not_block();
    test_secondary_dir_validation_and_persistence();
    test_local_failure_does_not_revert_close();
    test_manual_backup_in_progress_is_not_trampled();
    test_cashier_closes_shift_and_auto_runs();
    test_list_backups();
    test_cleanup_covers_auto_by_destination();
    test_migration_016();
    test_verify_newer_backup();
    test_sale_while_auto_backup_copies();
    test_close_shift_latency_and_destruction();

    std::cout << "Pruebas de respaldo automático (B12) correctas. TEST_CHECK ejecutados: " << g_checks << std::endl;
    cleanup_and_exit(0);
}
