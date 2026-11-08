// Pruebas B2: servicio de reportes (resumen diario de ventas). Sin GUI.
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include "app/storage/sqlite_database.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/services/sqlite_account_service.hpp"
#include "app/services/sqlite_catalog_service.hpp"
#include "app/services/sqlite_shift_service.hpp"
#include "app/services/sqlite_cart_service.hpp"
#include "app/services/sqlite_sale_service.hpp"
#include "app/services/sqlite_report_service.hpp"

#define TEST_CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA DE REPORTES] " << (message) \
                      << "\n  CondiciÃ³n: " << #condition \
                      << "\n  Archivo:   " << __FILE__ \
                      << "\n  LÃ­nea:     " << __LINE__ << "\n" << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace app::storage;
using namespace app::services;
using namespace app::contracts;
namespace fs = std::filesystem;

namespace {

const char* kOwnerPwd = "Clave-Prueba-Dueno-1";
const char* kCashierPwd = "Clave-Cajero-1";

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

struct Env {
    fs::path root;
    SqliteDatabase db;
    std::shared_ptr<SqliteAccountService> account;
    std::shared_ptr<SqliteCatalogService> catalog;
    std::shared_ptr<SqliteShiftService> shift;
    std::shared_ptr<SqliteCartService> cart;
    std::shared_ptr<SqliteSaleService> sale;
    std::shared_ptr<SqliteReportService> report;
    std::string p_1250, p_3399, p_799;

    explicit Env(const fs::path& mig) {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        root = fs::temp_directory_path() / fs::u8path("itch_b2_" + std::to_string(gen()));
        fs::create_directories(root);
        TEST_CHECK(db.open(root / "cafeteria.db").is_success(), "Debe abrir la base");
        MigrationRunner runner(db);
        auto m = runner.run_pending_migrations_from_dir(mig);
        TEST_CHECK(m.is_success(), "Migraciones: " + m.error_message());
        account = std::make_shared<SqliteAccountService>(db);
        catalog = std::make_shared<SqliteCatalogService>(db, account);
        shift = std::make_shared<SqliteShiftService>(db, account);
        cart = std::make_shared<SqliteCartService>(db, account, catalog);
        sale = std::make_shared<SqliteSaleService>(db, account, cart, shift);
        report = std::make_shared<SqliteReportService>(db, account);

        TEST_CHECK(account->create_initial_owner("dueno_b2", "DueÃ±o B2", kOwnerPwd, "dueno@cafeteria-itch.edu.mx").is_success(),
                   "Alta del DueÃ±o");
        CreateUserDto c{"cajero_b2", "Cajero B2", kCashierPwd, "role-cashier", "cajero@cafeteria-itch.edu.mx"};
        TEST_CHECK(account->create_user(c).is_success(), "Alta del cajero");

        p_1250 = make_product("B2-1250", "750200000001", 1250);
        p_3399 = make_product("B2-3399", "750200000002", 3399);
        p_799 = make_product("B2-0799", "750200000003", 799);
        TEST_CHECK(shift->open_shift("reg-01", Money::from_cents(50000)).is_success(), "Abrir turno");
    }

    std::string make_product(const std::string& sku, const std::string& barcode, std::int64_t cents) {
        CreateProductDto p;
        p.sku = sku;
        p.barcode = barcode;
        p.name = "Producto " + sku;
        p.category = "Pruebas";
        p.price = Money::from_cents(cents);
        p.is_prepared = false;
        auto r = catalog->create_product(p);
        TEST_CHECK(r.is_success(), "Crear producto " + sku + ": " + r.error_message());
        return r.value().id;
    }

    void exec(const std::string& sql) {
        auto r = db.execute(sql);
        TEST_CHECK(r.is_success(), "SQL: " + sql + " -> " + r.error_message());
    }

    // Vende con los servicios reales y fija la hora de venta y de pago (el servicio usa el reloj del sistema).
    std::string sell(const std::vector<std::string>& products, PaymentMethod method,
                     const std::string& sale_ts, const std::string& pay_ts) {
        for (const auto& pid : products) {
            auto a = cart->add_item(pid, 1, "");
            TEST_CHECK(a.is_success(), "Agregar al carrito: " + a.error_message());
        }
        auto total = cart->get_cart().value().total;
        auto r = sale->confirm_sale(method, method == PaymentMethod::Efectivo ? Money::from_cents(total.cents() + 500) : Money::zero());
        TEST_CHECK(r.is_success(), "confirm_sale: " + r.error_message());
        const std::string folio = r.value().folio;
        exec("UPDATE sales SET created_at = '" + sale_ts + "' WHERE folio = '" + folio + "';");
        exec("UPDATE payment_records SET created_at = '" + pay_ts + "' WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + folio + "');");
        return folio;
    }

    // Inserta una venta con su pago directamente (importes fuera del alcance de un carrito real).
    void insert_raw(const std::string& id, std::int64_t total_cents, const std::string& method,
                    std::int64_t pay_cents, const std::string& ts) {
        auto prep = db.prepare("SELECT shift_id, user_id FROM sales LIMIT 1;");
        TEST_CHECK(prep.is_success(), "Preparar lectura de turno");
        auto st = std::move(prep.value());
        TEST_CHECK(st.step().value(), "Debe existir al menos una venta real");
        const std::string shift_id = st.get_text(0), user_id = st.get_text(1);
        exec("INSERT INTO sales (id, folio, shift_id, user_id, total_cents, created_at) VALUES ('" + id + "', 'F-" + id +
             "', '" + shift_id + "', '" + user_id + "', " + std::to_string(total_cents) + ", '" + ts + "');");
        if (!method.empty()) {
            exec("INSERT INTO payment_records (id, sale_id, payment_method, amount_cents, created_at) VALUES ('pay-" + id +
                 "', '" + id + "', '" + method + "', " + std::to_string(pay_cents) + ", '" + ts + "');");
        }
    }

    Result<DailySalesReportDto> day(const std::string& d) { return report->get_daily_sales_summary(d); }

    void login(const std::string& user, const std::string& pwd) {
        account->logout();
        TEST_CHECK(account->login(user, pwd).is_success(), "Login " + user);
    }

    void cleanup() {
        report.reset(); sale.reset(); cart.reset(); shift.reset(); catalog.reset(); account.reset();
        db.close();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

void check_day(const DailySalesReportDto& r, std::int64_t count, std::int64_t total, std::int64_t cash,
               std::int64_t card, std::int64_t transfer, std::int64_t mismatch, const std::string& what) {
    TEST_CHECK(r.total_sales_count == count, what + ": conteo");
    TEST_CHECK(r.total_amount.cents() == total, what + ": total");
    TEST_CHECK(r.cash_amount.cents() == cash, what + ": efectivo");
    TEST_CHECK(r.card_amount.cents() == card, what + ": tarjeta");
    TEST_CHECK(r.transfer_amount.cents() == transfer, what + ": transferencia");
    TEST_CHECK(r.payments_total.cents() == cash + card + transfer, what + ": payments_total");
    TEST_CHECK(r.payments_mismatch_cents == mismatch, what + ": discrepancia");
}

void test_days_boundaries_and_cross_midnight(Env& e) {
    std::cout << "[TEST] B2: tres mÃ©todos, bordes 23:59:59 y 00:00:00, pago a medianoche..." << std::endl;
    e.login("dueno_b2", kOwnerPwd);
    // DÃ­a 2026-03-14 (excluido del 15)
    e.sell({e.p_1250}, PaymentMethod::Tarjeta, "2026-03-14 23:59:59", "2026-03-14 23:59:59");
    // DÃ­a 2026-03-15: cuatro ventas, tres mÃ©todos; D en el borde inferior 00:00:00
    e.sell({e.p_799, e.p_3399}, PaymentMethod::Transferencia, "2026-03-15 00:00:00", "2026-03-15 00:00:00"); // 4198
    e.sell({e.p_1250}, PaymentMethod::Efectivo, "2026-03-15 08:00:00", "2026-03-15 08:00:00");              // 1250
    e.sell({e.p_799, e.p_799}, PaymentMethod::Efectivo, "2026-03-15 12:30:00", "2026-03-15 12:30:00");      // 1598
    e.sell({e.p_3399}, PaymentMethod::Tarjeta, "2026-03-15 23:59:59", "2026-03-15 23:59:59");              // 3399
    // DÃ­a 2026-03-16: una venta a 00:00:00 (no cuenta el 15) y una a 23:59:59 con pago registrado el 17 a 00:00:00
    e.sell({e.p_799}, PaymentMethod::Efectivo, "2026-03-16 00:00:00", "2026-03-16 00:00:00");              // 799
    e.sell({e.p_1250}, PaymentMethod::Transferencia, "2026-03-16 23:59:59", "2026-03-17 00:00:00");        // 1250

    auto d15 = e.day("2026-03-15");
    TEST_CHECK(d15.is_success(), "Reporte del 15: " + d15.error_message());
    check_day(d15.value(), 4, 4198 + 1250 + 1598 + 3399, 1250 + 1598, 3399, 4198, 0, "dÃ­a 15");
    TEST_CHECK(d15.value().date == "2026-03-15", "El reporte repite la fecha");

    auto d14 = e.day("2026-03-14");
    check_day(d14.value(), 1, 1250, 0, 1250, 0, 0, "dÃ­a 14");

    // DÃ­a 16: la venta de 23:59:59 cuenta completa con su pago aunque este se registre el 17
    auto d16 = e.day("2026-03-16");
    check_day(d16.value(), 2, 799 + 1250, 799, 0, 1250, 0, "dÃ­a 16 (pago a medianoche atribuido a su venta)");
    // DÃ­a 17: sin ventas, y el pago de la medianoche no genera discrepancia
    auto d17 = e.day("2026-03-17");
    check_day(d17.value(), 0, 0, 0, 0, 0, 0, "dÃ­a 17 (sin ventas, sin discrepancia)");

    // Cambio de mes y de aÃ±o
    e.sell({e.p_1250}, PaymentMethod::Efectivo, "2026-12-31 23:59:59", "2026-12-31 23:59:59");
    e.sell({e.p_799}, PaymentMethod::Efectivo, "2027-01-01 00:00:00", "2027-01-01 00:00:00");
    check_day(e.day("2026-12-31").value(), 1, 1250, 1250, 0, 0, 0, "31 de diciembre");
    check_day(e.day("2027-01-01").value(), 1, 799, 799, 0, 0, 0, "1 de enero");
    e.sell({e.p_1250}, PaymentMethod::Efectivo, "2028-02-29 23:59:59", "2028-02-29 23:59:59");
    e.sell({e.p_799}, PaymentMethod::Efectivo, "2028-03-01 00:00:00", "2028-03-01 00:00:00");
    check_day(e.day("2028-02-29").value(), 1, 1250, 1250, 0, 0, 0, "29 de febrero bisiesto");
    check_day(e.day("2028-03-01").value(), 1, 799, 799, 0, 0, 0, "1 de marzo tras bisiesto");
}

void test_empty_day(Env& e) {
    std::cout << "[TEST] B2: dÃ­a sin ventas en cero..." << std::endl;
    e.login("dueno_b2", kOwnerPwd);
    auto r = e.day("2026-04-01");
    TEST_CHECK(r.is_success(), "DÃ­a sin ventas debe ser Ã©xito");
    check_day(r.value(), 0, 0, 0, 0, 0, 0, "dÃ­a vacÃ­o");
}

void test_invalid_dates(Env& e) {
    std::cout << "[TEST] B2: fechas invÃ¡lidas..." << std::endl;
    e.login("dueno_b2", kOwnerPwd);
    const char* bad[] = {"2026-02-30", "2026-13-01", "2026-00-10", "2026-04-31", "2026-02-29", "2100-02-29",
                         "hola", "", "2026-1-5", "2026-01-01 ", "2026/01/01", "26-01-01", "2026-01-0a",
                         "1969-12-31", "2026-01-00", "-026-01-01"};
    for (const char* b : bad) {
        auto r = e.day(b);
        TEST_CHECK(!r.is_success() && r.error_code() == ErrorCode::InvalidInput, std::string("Debe ser InvalidInput: '") + b + "'");
    }
    TEST_CHECK(e.day("2028-02-29").is_success(), "29 de febrero de aÃ±o bisiesto es vÃ¡lido");
    TEST_CHECK(e.day("2000-02-29").is_success(), "29 de febrero de 2000 (bisiesto por 400) es vÃ¡lido");
    TEST_CHECK(e.day("1970-01-01").is_success(), "1970-01-01 es vÃ¡lido");
}

void test_permissions(Env& e) {
    std::cout << "[TEST] B2: permisos reports.view dentro del servicio..." << std::endl;
    e.account->logout();
    auto none = e.day("2026-03-15");
    TEST_CHECK(!none.is_success() && none.error_code() == ErrorCode::Forbidden, "Sin sesiÃ³n = Forbidden");

    e.login("cajero_b2", kCashierPwd);
    TEST_CHECK(!e.account->has_permission("reports.view"), "El cajero no tiene reports.view");
    auto c = e.day("2026-03-15");
    TEST_CHECK(!c.is_success() && c.error_code() == ErrorCode::Forbidden, "Cajero = Forbidden");
    auto c2 = e.day("hola");
    TEST_CHECK(!c2.is_success() && c2.error_code() == ErrorCode::Forbidden, "Cajero con fecha invÃ¡lida = Forbidden, no InvalidInput");

    e.login("dueno_b2", kOwnerPwd);
    TEST_CHECK(e.day("2026-03-15").is_success(), "DueÃ±o sÃ­ puede");
}

void test_overflow(Env& e) {
    std::cout << "[TEST] B2: desbordamiento y lÃ­mites de int64..." << std::endl;
    e.login("dueno_b2", kOwnerPwd);
    const std::int64_t half_plus = INT64_MAX / 2 + 1;  // 2 x half_plus desborda

    // Suma de ventas desborda
    e.insert_raw("ovf-a1", half_plus, "EFECTIVO", 1, "2026-05-10 10:00:00");
    e.insert_raw("ovf-a2", half_plus, "EFECTIVO", 1, "2026-05-10 11:00:00");
    auto a = e.day("2026-05-10");
    TEST_CHECK(!a.is_success() && a.error_code() == ErrorCode::InvalidInput, "Ventas que desbordan = error");
    TEST_CHECK(a.error_message().find("Desbordamiento") != std::string::npos, "El mensaje indica desbordamiento");

    // Pagos de un mismo mÃ©todo desbordan
    e.insert_raw("ovf-b1", 1, "TARJETA", half_plus, "2026-05-11 10:00:00");
    e.insert_raw("ovf-b2", 1, "TARJETA", half_plus, "2026-05-11 11:00:00");
    auto b = e.day("2026-05-11");
    TEST_CHECK(!b.is_success() && b.error_code() == ErrorCode::InvalidInput, "Pagos del mismo mÃ©todo que desbordan = error");

    // Cada mÃ©todo cabe, pero la suma de pagos desborda
    e.insert_raw("ovf-c1", 1, "EFECTIVO", half_plus, "2026-05-12 10:00:00");
    e.insert_raw("ovf-c2", 1, "TARJETA", half_plus, "2026-05-12 11:00:00");
    auto c = e.day("2026-05-12");
    TEST_CHECK(!c.is_success() && c.error_code() == ErrorCode::InvalidInput, "Suma total de pagos que desborda = error");

    // Exactamente INT64_MAX es vÃ¡lido y exacto
    e.insert_raw("ovf-d1", INT64_MAX, "TRANSFERENCIA", INT64_MAX, "2026-05-13 10:00:00");
    auto d = e.day("2026-05-13");
    TEST_CHECK(d.is_success(), "INT64_MAX exacto no desborda: " + d.error_message());
    check_day(d.value(), 1, INT64_MAX, 0, 0, INT64_MAX, 0, "INT64_MAX exacto");
}

void test_mismatch_and_orphan(Env& e) {
    std::cout << "[TEST] B2: discrepancia pagos/total y pago huÃ©rfano..." << std::endl;
    e.login("dueno_b2", kOwnerPwd);

    // Pagos de menos
    std::string f1 = e.sell({e.p_1250}, PaymentMethod::Efectivo, "2026-06-01 09:00:00", "2026-06-01 09:00:00");
    e.exec("UPDATE payment_records SET amount_cents = 1000 WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + f1 + "');");
    auto r1 = e.day("2026-06-01");
    TEST_CHECK(r1.is_success(), "Con discrepancia el reporte se devuelve igual");
    check_day(r1.value(), 1, 1250, 1000, 0, 0, 250, "pagos de menos");

    // Pagos de mÃ¡s
    std::string f2 = e.sell({e.p_1250}, PaymentMethod::Tarjeta, "2026-06-02 09:00:00", "2026-06-02 09:00:00");
    e.exec("UPDATE payment_records SET amount_cents = 1500 WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + f2 + "');");
    check_day(e.day("2026-06-02").value(), 1, 1250, 0, 1500, 0, -250, "pagos de mÃ¡s");

    // Venta sin pago registrado
    std::string f3 = e.sell({e.p_799}, PaymentMethod::Efectivo, "2026-06-04 09:00:00", "2026-06-04 09:00:00");
    e.exec("DELETE FROM payment_records WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + f3 + "');");
    check_day(e.day("2026-06-04").value(), 1, 799, 0, 0, 0, 799, "venta sin pago");

    // Pago huÃ©rfano (sin venta): no pertenece a ningÃºn dÃ­a y no se suma
    e.exec("PRAGMA foreign_keys = OFF;");
    e.exec("INSERT INTO payment_records (id, sale_id, payment_method, amount_cents, created_at) "
           "VALUES ('pay-huerfano', 'venta-inexistente', 'EFECTIVO', 999, '2026-06-03 10:00:00');");
    e.exec("PRAGMA foreign_keys = ON;");
    auto o = e.day("2026-06-03");
    TEST_CHECK(o.is_success(), "DÃ­a con solo un pago huÃ©rfano");
    check_day(o.value(), 0, 0, 0, 0, 0, 0, "pago huÃ©rfano ignorado");
}

} // namespace

int main() {
    std::cout << "=== PRUEBAS B2: SERVICIO DE REPORTES (Debug y Release) ===" << std::endl;
    fs::path mig = find_migrations_dir();
    TEST_CHECK(!mig.empty(), "Debe localizar la carpeta de migraciones");
    Env e(mig);

    test_days_boundaries_and_cross_midnight(e);
    test_empty_day(e);
    test_invalid_dates(e);
    test_permissions(e);
    test_overflow(e);
    test_mismatch_and_orphan(e);

    e.cleanup();
    std::cout << "\n>>> TODAS LAS PRUEBAS DE REPORTES PASARON CON EXITO! <<<\n" << std::endl;
    return 0;
}
