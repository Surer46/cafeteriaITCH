// Medición B8: costo de quick_check, integrity_check y del arranque con una base de N ventas.
// Uso: test_startup_timing [ventas]   (por omisión 1500, para que CTest lo corra como prueba de humo).
// Con 50000 o 250000 ventas es la medición pedida para decidir qué comprobación va en cada arranque.
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

int main(int argc, char** argv) {
    const long long sales = argc > 1 ? std::atoll(argv[1]) : 1500;
    std::error_code ec;
    g_root = fs::temp_directory_path(ec) /
             fs::u8path(u8"cafeteria b8 medicion ñ " + std::to_string(Clock::now().time_since_epoch().count()));
    fs::create_directories(g_root, ec);
    const fs::path real = fs::u8path(SOURCE_MIGRATIONS_DIR);

    // Base real migrada.
    StartupOptions opt;
    opt.db_path = g_root / "cafeteria.db";
    opt.migrations_dir = real;
    opt.backups_dir = g_root / "backups";
    {
        SqliteDatabase db;
        auto r = open_and_migrate_database(db, opt);
        TEST_CHECK(r.ok, r.message_utf8);

        // N ventas con 2 líneas, un pago, un movimiento de auditoría y un movimiento de caja cada una.
        // Se desactivan las claves foráneas solo para generar datos de volumen; no afectan la comprobación.
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
        db.close();
    }
    std::cout << "Tamaño de la base: " << fs::file_size(opt.db_path) / (1024.0 * 1024.0) << " MB" << std::endl;

    // 1. quick_check e integrity_check sueltos.
    {
        SqliteDatabase db;
        TEST_CHECK(db.open(opt.db_path).is_success(), "abrir");
        auto t = Clock::now();
        TEST_CHECK(first_text(db, "PRAGMA quick_check;") == "ok", "quick_check ok");
        std::cout << "quick_check:     " << ms_since(t) << " ms" << std::endl;
        t = Clock::now();
        TEST_CHECK(first_text(db, "PRAGMA integrity_check;") == "ok", "integrity_check ok");
        std::cout << "integrity_check: " << ms_since(t) << " ms" << std::endl;
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

    // 3. Arranque con una migración pendiente: comprobación completa + respaldo verificado + migración + registro.
    {
        const fs::path mig2 = g_root / "migraciones con una mas";
        fs::create_directories(mig2);
        for (const auto& e : fs::directory_iterator(real)) fs::copy_file(e.path(), mig2 / e.path().filename());
        std::ofstream(mig2 / "099_medicion.sql") << "CREATE TABLE medicion_b8 (x INTEGER);\n";
        StartupOptions opt2 = opt;
        opt2.migrations_dir = mig2;
        SqliteDatabase db;
        const auto t = Clock::now();
        auto r = open_and_migrate_database(db, opt2);
        TEST_CHECK(r.ok && r.backup_created && r.backup_registered, "arranque con pendiente: " + r.message_utf8);
        std::cout << "Arranque CON una migración pendiente: total " << ms_since(t) << " ms (quick_check " << r.quick_check_ms
                  << ", integrity_check " << r.full_check_ms << ", respaldo+verificación " << r.backup_ms << ", migración "
                  << r.migrate_ms << " ms)" << std::endl;
        std::cout << "Tamaño del respaldo previo: " << fs::file_size(r.backup_path) / (1024.0 * 1024.0) << " MB" << std::endl;
        db.close();
    }

    std::cout << "Medición de arranque (B8) correcta. TEST_CHECK ejecutados: " << g_checks << std::endl;
    cleanup_and_exit(0);
}
