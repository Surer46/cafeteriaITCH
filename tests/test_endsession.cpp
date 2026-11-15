// Prueba B12 / punto 0: cierre por Windows (WM_QUERYENDSESSION / WM_ENDSESSION) del ejecutable REAL.
//
// Lanza app_desktop.exe con un --data-dir temporal propio, espera la ventana de cada estado alcanzable sin
// GUI automatizada, envía los mensajes de apagado y comprueba: el proceso termina (sin caerse con 0xC0000374),
// la base queda íntegra (PRAGMA quick_check) y lo confirmado sigue ahí.
//
// Estados cubiertos: alta del Dueño, inicio de sesión, ventana principal sin turno, ventana principal con turno
// abierto y un carrito con líneas (se llega a ella con WM_SETTEXT y BM_CLICK sobre los controles del inicio de
// sesión: mensajes Win32, no automatización de interfaz), ventana de progreso de la actualización de la base y
// un arranque que se deja 35 s sin tocar (comprobación de que el programa no se cierra solo).
// NO cubiertos: diálogos modales abiertos desde la ventana principal (cobro y otros: requieren clics sobre
// controles propios) y el apagado real de Windows.
//
// Solo corre en Release con app_desktop; en otro caso termina con 77 (OMITIDA en CTest).
// No deja archivos fuera de su carpeta temporal y no depende del directorio de trabajo.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include "app/contracts/money.hpp"
#include "app/services/database_startup.hpp"
#include "app/services/mock_services.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/storage/sqlite_database.hpp"

namespace fs = std::filesystem;
using namespace app::services;
using namespace app::storage;
using namespace app::contracts;

static fs::path g_root;
static int g_checks = 0;
static std::vector<PROCESS_INFORMATION> g_live;

static void kill_live() {
    for (auto& pi : g_live) {
        if (pi.hProcess) {
            TerminateProcess(pi.hProcess, 98);
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
            pi.hProcess = nullptr;
        }
    }
    g_live.clear();
}
static void cleanup_and_exit(int code) {
    kill_live();
    std::error_code ec;
    if (!g_root.empty()) fs::remove_all(g_root, ec);
    std::exit(code);
}
#define TEST_CHECK(condition, message) \
    do { \
        ++g_checks; \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA DE CIERRE POR WINDOWS] " << (message) << "\n  Condición: " << #condition \
                      << "\n  Línea: " << __LINE__ << "\n" << std::endl; \
            cleanup_and_exit(1); \
        } \
    } while (0)

#ifndef ENDSESSION_TEST_ENABLED
#define ENDSESSION_TEST_ENABLED 0
#endif

namespace {

const std::string kOwnerPwd = "Clave-Dueno-B12-1";
constexpr DWORD kHeapCorruption = 0xC0000374;

std::string text_of(HWND h) {
    wchar_t buf[256] = {0};
    GetWindowTextW(h, buf, 255);
    return fs::path(buf).u8string();
}

struct Found {
    DWORD pid;
    std::string title_part;
    HWND hwnd{nullptr};
};

BOOL CALLBACK enum_find(HWND h, LPARAM lp) {
    auto* f = reinterpret_cast<Found*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    if (pid != f->pid || !IsWindowVisible(h)) return TRUE;
    if (text_of(h).find(f->title_part) != std::string::npos) {
        f->hwnd = h;
        return FALSE;
    }
    return TRUE;
}

HWND wait_window(DWORD pid, const std::string& title_part, int timeout_ms) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (std::chrono::steady_clock::now() < deadline) {
        Found f{pid, title_part};
        EnumWindows(enum_find, reinterpret_cast<LPARAM>(&f));
        if (f.hwnd) return f.hwnd;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    return nullptr;
}

BOOL CALLBACK enum_collect(HWND h, LPARAM lp) {
    auto* v = reinterpret_cast<std::vector<std::pair<DWORD, HWND>>*>(lp);
    DWORD pid = 0;
    GetWindowThreadProcessId(h, &pid);
    v->emplace_back(pid, h);
    return TRUE;
}

// TODAS las ventanas de nivel superior del proceso, visibles o no (como hace el Restart Manager).
std::vector<HWND> all_windows(DWORD pid) {
    std::vector<std::pair<DWORD, HWND>> v;
    EnumWindows(enum_collect, reinterpret_cast<LPARAM>(&v));
    std::vector<HWND> out;
    for (auto& e : v) if (e.first == pid) out.push_back(e.second);
    return out;
}

PROCESS_INFORMATION launch(const fs::path& data_dir) {
    const fs::path exe = fs::u8path(APP_EXE_PATH);
    std::wstring cmd = L"\"" + exe.wstring() + L"\" \"--data-dir=" + data_dir.wstring() + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, exe.parent_path().c_str(), &si, &pi);
    TEST_CHECK(ok, "No se pudo lanzar app_desktop.exe");
    g_live.push_back(pi);
    return pi;
}

// Código de salida, o -1 si no terminó en el plazo (se mata).
long wait_exit(PROCESS_INFORMATION& pi, int timeout_ms) {
    long result;
    if (WaitForSingleObject(pi.hProcess, timeout_ms) == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 99);
        result = -1;
    } else {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        result = static_cast<long>(code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    pi.hProcess = nullptr;
    return result;
}

enum class Mode { EndOnly, QueryThenEnd, PostedAll };

void send_end_session(DWORD pid, Mode mode) {
    auto windows = all_windows(pid);
    DWORD_PTR r = 0;
    switch (mode) {
        case Mode::EndOnly:
            for (HWND h : windows) SendMessageTimeoutW(h, WM_ENDSESSION, TRUE, ENDSESSION_CLOSEAPP, SMTO_ABORTIFHUNG, 3000, &r);
            break;
        case Mode::QueryThenEnd:
            for (HWND h : windows) SendMessageTimeoutW(h, WM_QUERYENDSESSION, 0, ENDSESSION_CLOSEAPP, SMTO_ABORTIFHUNG, 3000, &r);
            for (HWND h : windows) SendMessageTimeoutW(h, WM_ENDSESSION, TRUE, ENDSESSION_CLOSEAPP, SMTO_ABORTIFHUNG, 3000, &r);
            break;
        case Mode::PostedAll:
            for (HWND h : windows) {
                PostMessageW(h, WM_QUERYENDSESSION, 0, ENDSESSION_CLOSEAPP);
                PostMessageW(h, WM_ENDSESSION, TRUE, ENDSESSION_CLOSEAPP);
                PostMessageW(h, WM_CLOSE, 0, 0);
            }
            break;
    }
}

const char* mode_name(Mode m) {
    return m == Mode::EndOnly ? "solo WM_ENDSESSION"
         : m == Mode::QueryThenEnd ? "WM_QUERYENDSESSION y luego WM_ENDSESSION" : "QUERY+END+CLOSE publicados juntos";
}

std::string scalar_text(SqliteDatabase& db, const std::string& sql) {
    auto prep = db.prepare(sql);
    TEST_CHECK(prep.is_success(), "Preparar: " + sql);
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    TEST_CHECK(step.is_success() && step.value(), "Fila: " + sql);
    return stmt.get_text(0);
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

StartupOptions options_for(const fs::path& dir, const fs::path& migrations) {
    StartupOptions o;
    o.db_path = dir / "cafeteria.db";
    o.migrations_dir = migrations;
    o.backups_dir = dir / "backups";
    return o;
}

// Prepara la base del estado pedido (con los servicios, en este proceso) y la cierra antes de lanzar el programa.
// Devuelve cuántas líneas de carrito y turnos abiertos debe haber al final.
struct Expected {
    long open_shifts{0};
    long cart_lines{0};
    long users{0};
    bool cart_checked{false};
};

Expected prepare_db(const std::string& state, const fs::path& dir) {
    Expected ex;
    fs::create_directories(dir);
    if (state == "alta") return ex;  // carpeta vacía: el programa pide el alta del Dueño

    SqliteDatabase db;
    const fs::path real = fs::u8path(SOURCE_MIGRATIONS_DIR);
    if (state == "progreso") {
        // Base con datos en la versión anterior a la última: el arranque tiene una migración pendiente.
        const int max_v = [&] {
            int mx = 0;
            for (const auto& e : fs::directory_iterator(real)) if (e.path().extension() == ".sql") mx = std::max(mx, std::stoi(e.path().filename().string().substr(0, 3)));
            return mx;
        }();
        const fs::path old_set = copy_migrations_upto(dir / "migraciones-temporales", max_v - 1);
        auto r = open_and_migrate_database(db, options_for(dir, old_set));
        TEST_CHECK(r.ok, "Base previa: " + r.message_utf8);
        // Volumen para que la actualización dure varios cientos de milisegundos.
        TEST_CHECK(db.execute("PRAGMA foreign_keys = OFF;").is_success(), "fk");
        TEST_CHECK(db.execute("CREATE TABLE relleno_b12 (x TEXT NOT NULL);").is_success(), "tabla de relleno");
        TEST_CHECK(db.execute("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i+1 FROM n WHERE i < 400000) "
                              "INSERT INTO relleno_b12 SELECT hex(randomblob(60)) FROM n;").is_success(), "relleno");
        db.close();
        std::error_code ec;
        fs::remove_all(dir / "migraciones-temporales", ec);
        return ex;
    }

    auto r = open_and_migrate_database(db, options_for(dir, real));
    TEST_CHECK(r.ok, "Base: " + r.message_utf8);
    auto reg = ServiceRegistry::create_sqlite_registry(db, dir.u8string(), real.u8string());
    TEST_CHECK(reg.account_service->create_initial_owner("dueno_b12", "Dueño B12", kOwnerPwd, "dueno@cafeteria-itch.edu.mx").is_success(), "Dueño");
    if (state == "principal_con_turno") {
        TEST_CHECK(reg.shift_service->open_shift("reg-01", Money::from_cents(50000)).is_success(), "Abrir turno");
        CreateProductDto p;
        p.sku = "B12-1"; p.name = "Café B12"; p.category = "Bebidas"; p.price = Money::from_cents(3000);
        auto prod = reg.catalog_service->create_product(p);
        TEST_CHECK(prod.is_success(), "Producto: " + prod.error_message());
        TEST_CHECK(reg.cart_service->add_item(prod.value().id, 2, "").is_success(), "Línea 1");
        TEST_CHECK(reg.cart_service->add_item(prod.value().id, 1, "sin azúcar").is_success(), "Línea 2");
        ex.cart_checked = true;
        ex.open_shifts = 1;
        ex.cart_lines = std::stol(scalar_text(db, "SELECT COUNT(*) FROM cart_items;"));
        TEST_CHECK(ex.cart_lines >= 2, "El carrito debe tener líneas");
    }
    ex.users = std::stol(scalar_text(db, "SELECT COUNT(*) FROM users;"));
    reg.account_service->logout();
    db.close();
    return ex;
}

struct ChildFind {
    std::vector<HWND> edits;
    HWND button{nullptr};
};
BOOL CALLBACK enum_child(HWND h, LPARAM lp) {
    auto* c = reinterpret_cast<ChildFind*>(lp);
    wchar_t cls[64] = {0};
    GetClassNameW(h, cls, 63);
    if (std::wstring(cls) == L"Edit") c->edits.push_back(h);
    if (std::wstring(cls) == L"Button" && text_of(h).find("Iniciar") != std::string::npos) c->button = h;
    return TRUE;
}

// Inicia sesión con mensajes Win32 sobre el diálogo de inicio de sesión (usuario, contraseña y botón).
bool login_with_messages(HWND dialog) {
    ChildFind c;
    EnumChildWindows(dialog, enum_child, reinterpret_cast<LPARAM>(&c));
    if (c.edits.size() < 2 || !c.button) return false;
    const std::wstring user = L"dueno_b12";
    const std::wstring pwd = fs::u8path(kOwnerPwd).wstring();
    DWORD_PTR r = 0;
    SendMessageTimeoutW(c.edits[0], WM_SETTEXT, 0, reinterpret_cast<LPARAM>(user.c_str()), SMTO_ABORTIFHUNG, 3000, &r);
    SendMessageTimeoutW(c.edits[1], WM_SETTEXT, 0, reinterpret_cast<LPARAM>(pwd.c_str()), SMTO_ABORTIFHUNG, 3000, &r);
    PostMessageW(c.button, BM_CLICK, 0, 0);
    return true;
}

void run_state(const std::string& state, Mode mode) {
    const std::string label = state + " / " + mode_name(mode);
    const fs::path dir = g_root / (state + "-" + std::to_string(static_cast<int>(mode)) + " ñ");
    const Expected ex = prepare_db(state, dir);

    PROCESS_INFORMATION pi = launch(dir);
    std::string title;
    HWND hwnd = nullptr;
    if (state == "alta") {
        hwnd = wait_window(pi.dwProcessId, "Alta de Cuenta", 30000);
    } else if (state == "login") {
        hwnd = wait_window(pi.dwProcessId, "Iniciar Sesi", 30000);
    } else if (state == "progreso") {
        // La ventana de progreso de la actualización (B9b): sin el título del diálogo de inicio de sesión.
        hwnd = wait_window(pi.dwProcessId, "Cafetería ITCH", 30000);
    } else {
        HWND login = wait_window(pi.dwProcessId, "Iniciar Sesi", 30000);
        TEST_CHECK(login != nullptr, label + ": no apareció el inicio de sesión");
        TEST_CHECK(login_with_messages(login), label + ": no se encontraron los controles del inicio de sesión");
        hwnd = wait_window(pi.dwProcessId, "Cafetería ITCH II", 30000);
        // El diálogo de inicio de sesión también contiene ese texto en su título: se espera a que desaparezca.
        for (int i = 0; i < 100 && hwnd; ++i) {
            Found f{pi.dwProcessId, "Iniciar Sesi"};
            EnumWindows(enum_find, reinterpret_cast<LPARAM>(&f));
            if (!f.hwnd) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
    TEST_CHECK(hwnd != nullptr, label + ": no apareció la ventana esperada");
    std::this_thread::sleep_for(std::chrono::milliseconds(state == "progreso" ? 200 : 600));

    send_end_session(pi.dwProcessId, mode);
    const long code = wait_exit(pi, 15000);
    TEST_CHECK(code != -1, label + ": el programa no terminó en 15 s (¿bloquea el apagado?)");
    TEST_CHECK(static_cast<DWORD>(code) != kHeapCorruption, label + ": cayó con 0xC0000374 (corrupción del heap)");
    TEST_CHECK(code == 0, label + ": código de salida inesperado " + std::to_string(code));
    g_live.clear();

    // La base queda íntegra y lo confirmado sigue ahí.
    const fs::path db_file = dir / "cafeteria.db";
    if (state == "alta" && !fs::exists(db_file)) return;
    SqliteDatabase db;
    TEST_CHECK(db.open(db_file).is_success(), label + ": no se pudo reabrir la base");
    TEST_CHECK(scalar_text(db, "PRAGMA quick_check;") == "ok", label + ": quick_check no dio ok");
    if (state == "principal_sin_turno" || state == "principal_con_turno" || state == "login") {
        TEST_CHECK(std::stol(scalar_text(db, "SELECT COUNT(*) FROM users;")) == ex.users, label + ": cambió el número de usuarios");
    }
    if (ex.cart_checked) {
        TEST_CHECK(std::stol(scalar_text(db, "SELECT COUNT(*) FROM shifts WHERE is_open = 1;")) == ex.open_shifts,
                   label + ": el turno abierto debe seguir abierto");
        TEST_CHECK(std::stol(scalar_text(db, "SELECT COUNT(*) FROM cart_items;")) == ex.cart_lines,
                   label + ": las líneas del carrito deben seguir ahí");
    }
    if (state == "progreso") {
        // Migración interrumpida: o quedó completa o se revirtió; nunca a medias. Y el relleno sigue.
        TEST_CHECK(std::stol(scalar_text(db, "SELECT COUNT(*) FROM relleno_b12;")) == 400000, label + ": se perdieron datos del relleno");
        // Cualquier respaldo previo que haya quedado registrado debe ser un archivo íntegro.
        auto prep = db.prepare("SELECT path FROM backup_files WHERE kind = 'PRE_MIGRATION';");
        if (prep.is_success()) {
            auto stmt = std::move(prep.value());
            while (true) {
                auto step = stmt.step();
                if (!step.is_success() || !step.value()) break;
                auto info = SqliteDatabase::inspect_file(fs::u8path(stmt.get_text(0)));
                TEST_CHECK(info.is_success() && info.value().integrity_ok, label + ": respaldo previo registrado no íntegro");
            }
        }
    }
    db.close();
}

} // namespace

int main() {
    if (!ENDSESSION_TEST_ENABLED) {
        std::cout << "OMITIDA: la prueba de cierre por Windows solo corre en Release con app_desktop." << std::endl;
        return 77;
    }
    std::error_code ec;
    g_root = fs::temp_directory_path(ec) / (L"cafeteria b12 endsession ñ " + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);

    for (Mode m : {Mode::EndOnly, Mode::QueryThenEnd, Mode::PostedAll}) run_state("alta", m);
    for (Mode m : {Mode::EndOnly, Mode::PostedAll}) run_state("login", m);
    for (Mode m : {Mode::EndOnly, Mode::QueryThenEnd, Mode::PostedAll}) run_state("principal_sin_turno", m);
    for (Mode m : {Mode::EndOnly, Mode::PostedAll}) run_state("principal_con_turno", m);
    run_state("progreso", Mode::EndOnly);
    run_state("progreso", Mode::PostedAll);

    // El programa no se cierra solo: 35 s en el alta del Dueño sin enviar nada (B10 vio cierres sin causa).
    {
        const fs::path dir = g_root / "vida";
        fs::create_directories(dir);
        PROCESS_INFORMATION pi = launch(dir);
        HWND hwnd = wait_window(pi.dwProcessId, "Alta de Cuenta", 30000);
        TEST_CHECK(hwnd != nullptr, "vida: no apareció el alta del Dueño");
        const DWORD wait = WaitForSingleObject(pi.hProcess, 35000);
        TEST_CHECK(wait == WAIT_TIMEOUT, "El programa se cerró solo antes de 35 s en el alta del Dueño");
        TerminateProcess(pi.hProcess, 97);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        g_live.clear();
    }

    std::cout << "Pruebas de cierre por Windows (B12) correctas. TEST_CHECK ejecutados: " << g_checks << std::endl;
    cleanup_and_exit(0);
}
