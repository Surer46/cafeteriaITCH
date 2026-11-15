// Medición B8/B9: costo de quick_check, integrity_check (con y sin manejador de progreso) y del arranque
// con una base de N ventas.
//
// Uso:
//   test_startup_timing [ventas]
//       por omisión 1500, para que CTest lo corra como prueba de humo; con 50000 o 250000 es la medición.
//   test_startup_timing --make-update-fixture <carpeta> <ventas>
//       crea en <carpeta> una base cafeteria.db con <ventas> ventas en la versión 14 (queda UNA migración
//       pendiente, la 015) y una copia de partida cafeteria-inicial.db para repetir la prueba. Sirve para ver a mano
//       la ventana de progreso de app_desktop.exe --data-dir <carpeta>. No crea cuentas ni contraseñas.
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "app/services/database_startup.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/storage/sqlite_database.hpp"

namespace fs = std::filesystem;
using namespace app::services;
using namespace app::storage;
using Clock = std::chrono::steady_clock;

static fs::path g_root;
static bool g_keep_root = false;
static int g_checks = 0;
static void cleanup_and_exit(int code) {
    std::error_code ec;
    if (!g_root.empty() && !g_keep_root) fs::remove_all(g_root, ec);
    std::exit(code);
}
#define TEST_CHECK(condition, message) \
    do { \
        ++g_checks; \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE MEDICIÓN] " << (message) << "\n  Condición: " << #condition \
                      << "\n  Línea: " << __LINE__ << "\n" << std::endl; \
            cleanup_and_exit(1); \
        } \
    } while (0)

static long long ms_since(Clock::time_point t) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t).count();
}

static std::string first_text(SqliteDatabase& db, const std::string& sql) {
    auto prep = db.prepare(sql);
    TEST_CHECK(prep.is_success(), "Preparar: " + sql);
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    TEST_CHECK(step.is_success() && step.value(), "Fila: " + sql);
    return stmt.get_text(0);
}

// N ventas con 2 líneas, un pago y un evento de auditoría cada una. Se desactivan las claves foráneas solo
// para generar datos de volumen; no afectan a las comprobaciones de integridad.
static void generate_sales(SqliteDatabase& db, long long sales) {
    TEST_CHECK(db.execute("PRAGMA foreign_keys = OFF;").is_success(), "FK off");
    const auto t0 = Clock::now();
    TEST_CHECK(db.begin_transaction().is_success(), "tx");
    const long long batch = 10000;
    for (long long from = 1; from <= sales; from += batch) {
        const long long to = std::min(sales, from + batch - 1);
        const std::string range = "WITH RECURSIVE n(i) AS (SELECT " + std::to_string(from) +
                                  " UNION ALL SELECT i+1 FROM n WHERE i < " + std::to_string(to) + ") ";
        TEST_CHECK(db.execute(range +
            "INSERT INTO sales (id, folio, shift_id, user_id, total_cents, idempotency_key, created_at, cashier_name, created_at_utc) "
            "SELECT 'sale-' || lower(hex(randomblob(16))), 'F-' || i, 'shift-x', 'user-x', 12000 + (i % 5000), "
            "'key-' || lower(hex(randomblob(16))), '2026-01-01 10:00:00', 'Cajero', '2026-01-01T16:00:00.000Z' FROM n;").is_success(),
            "ventas");
        TEST_CHECK(db.execute(range +
            "INSERT INTO sale_items (id, sale_id, product_id, product_name, quantity, unit_price_cents, subtotal_cents, notes) "
            "SELECT 'item-' || lower(hex(randomblob(16))), (SELECT id FROM sales WHERE folio = 'F-' || i), 'prod-x', 'Café americano', 2, 3000, 6000, '' "
            "FROM n;").is_success(), "líneas 1");
        TEST_CHECK(db.execute(range +
            "INSERT INTO sale_items (id, sale_id, product_id, product_name, quantity, unit_price_cents, subtotal_cents, notes) "
            "SELECT 'item-' || lower(hex(randomblob(16))), (SELECT id FROM sales WHERE folio = 'F-' || i), 'prod-y', 'Pan dulce', 1, 2500, 2500, '' "
            "FROM n;").is_success(), "líneas 2");
        TEST_CHECK(db.execute(range +
            "INSERT INTO payment_records (id, sale_id, payment_method, amount_cents, cash_received_cents, change_cents, created_at) "
            "SELECT 'pay-' || lower(hex(randomblob(16))), (SELECT id FROM sales WHERE folio = 'F-' || i), 'EFECTIVO', 8500, 10000, 1500, '2026-01-01 10:00:00' "
            "FROM n;").is_success(), "pagos");
        TEST_CHECK(db.execute(range +
            "INSERT INTO audit_events (id, event_type, user_id, details, created_at, created_at_utc) "
            "SELECT 'audit-' || lower(hex(randomblob(16))), 'SALE_CONFIRMED', NULL, 'Venta F-' || i || ' confirmada por Cajero', "
            "'2026-01-01 10:00:00', '2026-01-01T16:00:00.000Z' FROM n;").is_success(), "auditoría");
    }
    TEST_CHECK(db.commit_transaction().is_success(), "commit");
    std::cout << "Generadas " << sales << " ventas en " << ms_since(t0) << " ms" << std::endl;
}

static fs::path copy_migrations_upto(const fs::path& real, const fs::path& to, int max_version) {
    fs::create_directories(to);
    for (const auto& e : fs::directory_iterator(real)) {
        if (e.path().extension() != ".sql") continue;
        if (std::stoi(e.path().filename().string().substr(0, 3)) <= max_version) {
            fs::copy_file(e.path(), to / e.path().filename(), fs::copy_options::overwrite_existing);
        }
    }
    return to;
}

int main(int argc, char** argv) {
    const fs::path real = fs::u8path(SOURCE_MIGRATIONS_DIR);

    // --- Modo 2: base de ejemplo para ver la ventana de progreso a mano ---
    if (argc >= 2 && std::string(argv[1]) == "--make-update-fixture") {
        if (argc < 4) {
            std::cerr << "Uso: test_startup_timing --make-update-fixture <carpeta> <ventas>\n";
            return 2;
        }
        const fs::path dir = fs::u8path(argv[2]);
        const long long sales = std::atoll(argv[3]);
        g_keep_root = true;
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (fs::exists(dir / "cafeteria.db", ec)) {
            std::cerr << "La carpeta ya tiene una cafeteria.db; use una carpeta nueva o vacía.\n";
            return 2;
        }
        const fs::path mig14 = copy_migrations_upto(real, dir / "migraciones-14-temporal", 14);
        StartupOptions opt;
        opt.db_path = dir / "cafeteria.db";
        opt.migrations_dir = mig14;
        opt.backups_dir = dir / "backups";
        {
            SqliteDatabase db;
            auto r = open_and_migrate_database(db, opt);
            TEST_CHECK(r.ok, r.message_utf8);
            generate_sales(db, sales);
            db.close();
        }
        fs::remove_all(mig14, ec);
        fs::copy_file(dir / "cafeteria.db", dir / "cafeteria-inicial.db", fs::copy_options::overwrite_existing, ec);
        TEST_CHECK(!ec, "Copia de partida");
        std::cout << "Base de ejemplo lista en " << dir.u8string() << "\n"
                  << "  versión 14 con " << sales << " ventas; falta aplicar la migración 015 (una pendiente).\n"
                  << "  tamaño: " << fs::file_size(dir / "cafeteria.db") / (1024.0 * 1024.0) << " MB\n"
                  << "  copia de partida: cafeteria-inicial.db (para repetir: borre cafeteria.db, cafeteria.db-wal,\n"
                  << "  cafeteria.db-shm y la carpeta backups, y copie cafeteria-inicial.db como cafeteria.db)\n"
                  << std::endl;
        return 0;
    }

    // --- Modo 1: medición ---
    const long long sales = argc > 1 ? std::atoll(argv[1]) : 1500;
    std::error_code ec;
    g_root = fs::temp_directory_path(ec) /
             fs::u8path(u8"cafeteria b8 medicion ñ " + std::to_string(Clock::now().time_since_epoch().count()));
    fs::create_directories(g_root, ec);

    StartupOptions opt;
    opt.db_path = g_root / "cafeteria.db";
    opt.migrations_dir = real;
    opt.backups_dir = g_root / "backups";
    {
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, opt);
        TEST_CHECK(r.ok, r.message_utf8);
        generate_sales(db, sales);
        db.close();
    }
    std::cout << "Tamaño de la base: " << fs::file_size(opt.db_path) / (1024.0 * 1024.0) << " MB" << std::endl;

    // 1. quick_check e integrity_check sueltos, sin y con manejador de progreso (el aviso solo cuenta).
    {
        SqliteDatabase db;
        TEST_CHECK(db.open(opt.db_path).is_success(), "abrir");
        auto t = Clock::now();
        TEST_CHECK(first_text(db, "PRAGMA quick_check;") == "ok", "quick_check ok");
        std::cout << "quick_check:                        " << ms_since(t) << " ms" << std::endl;
        long long plain = 0, with_handler = 0;
        for (int round = 0; round < 2; ++round) {
            t = Clock::now();
            TEST_CHECK(first_text(db, "PRAGMA integrity_check;") == "ok", "integrity_check ok");
            plain = ms_since(t);
            long long ticks = 0;
            db.set_progress_callback([&ticks] { ++ticks; });
            t = Clock::now();
            TEST_CHECK(first_text(db, "PRAGMA integrity_check;") == "ok", "integrity_check con manejador ok");
            with_handler = ms_since(t);
            db.clear_progress_callback();
            std::cout << "integrity_check (ronda " << round + 1 << "):           sin manejador " << plain << " ms, con manejador "
                      << with_handler << " ms (" << ticks << " avisos cada 20000 instrucciones)" << std::endl;
        }
        db.close();
    }

    // 2. Arranque sin migraciones pendientes.
    {
        SqliteDatabase db;
        const auto t = Clock::now();
        auto r = open_and_migrate_database(db, opt);
        TEST_CHECK(r.ok && !r.backup_created, "arranque sin pendientes: " + r.message_utf8);
        std::cout << "Arranque SIN migraciones pendientes: total " << ms_since(t) << " ms (quick_check " << r.quick_check_ms
                  << " ms)" << std::endl;
        db.close();
    }

    // 3. Arranque con una migración pendiente, sin y con aviso de progreso.
    for (int with_progress = 0; with_progress < 2; ++with_progress) {
        const fs::path mig2 = g_root / ("migraciones con una mas " + std::to_string(with_progress));
        fs::create_directories(mig2);
        for (const auto& e : fs::directory_iterator(real)) fs::copy_file(e.path(), mig2 / e.path().filename());
        std::ofstream(mig2 / "099_medicion.sql") << "CREATE TABLE medicion_b8 (x INTEGER);\n";
        StartupOptions opt2 = opt;
        opt2.migrations_dir = mig2;
        long long events = 0;
        if (with_progress) opt2.on_progress = [&events](const StartupProgress&) { ++events; };
        SqliteDatabase db;
        const auto t = Clock::now();
        auto r = open_and_migrate_database(db, opt2);
        TEST_CHECK(r.ok && r.backup_created && r.backup_registered, "arranque con pendiente: " + r.message_utf8);
        std::cout << "Arranque CON una migración pendiente" << (with_progress ? " (con avisos, " : " (sin avisos, ")
                  << (with_progress ? std::to_string(events) : std::string("0")) << (with_progress ? " avisos)" : ")")
                  << ": total " << ms_since(t) << " ms (quick_check " << r.quick_check_ms << ", integrity_check "
                  << r.full_check_ms << ", respaldo+verificación " << r.backup_ms << ", migración " << r.migrate_ms << " ms)"
                  << std::endl;
        db.close();
        // La siguiente vuelta necesita volver a tener una pendiente: se quita la migración 099 aplicada.
        if (!with_progress) {
            SqliteDatabase undo;
            TEST_CHECK(undo.open(opt.db_path).is_success(), "reabrir");
            TEST_CHECK(undo.execute("DROP TABLE medicion_b8; DELETE FROM schema_migrations WHERE version = 99;").is_success(), "deshacer 099");
            undo.close();
            std::error_code e2;
            fs::remove_all(opt.backups_dir, e2);
        }
    }

    std::cout << "Medición de arranque (B9) correcta. TEST_CHECK ejecutados: " << g_checks << std::endl;
    cleanup_and_exit(0);
}
