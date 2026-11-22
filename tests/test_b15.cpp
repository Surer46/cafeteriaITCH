// Pruebas B15: conciliación del corte de turno, permiso para cerrar el turno de otro usuario, movimientos de caja,
// reportes por rango de fechas y exportación a CSV. Sin GUI, sin red y sin escribir ningún .exe.
// Uso: test_b15 shift | test_b15 reports
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <vector>
#include "app/domain/clock.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/storage/sqlite_database.hpp"
#include "app/services/sqlite_account_service.hpp"
#include "app/services/sqlite_catalog_service.hpp"
#include "app/services/sqlite_inventory_service.hpp"
#include "app/services/sqlite_shift_service.hpp"
#include "app/services/sqlite_cart_service.hpp"
#include "app/services/sqlite_sale_service.hpp"
#include "app/services/sqlite_report_service.hpp"

#define TEST_CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA B15] " << (message) \
                      << "\n  Condicion: " << #condition \
                      << "\n  Archivo:   " << __FILE__ \
                      << "\n  Linea:     " << __LINE__ << "\n" << std::endl; \
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
    std::shared_ptr<SqliteInventoryService> inventory;
    std::shared_ptr<SqliteShiftService> shift;
    std::shared_ptr<SqliteCartService> cart;
    std::shared_ptr<SqliteSaleService> sale;
    std::shared_ptr<SqliteReportService> report;
    std::string p_1250, p_3399, p_799;
    fs::path migrations;

    explicit Env(const fs::path& mig) : migrations(mig) {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        root = fs::temp_directory_path() / fs::u8path("itch_b15_" + std::to_string(gen()));
        fs::create_directories(root);
        TEST_CHECK(db.open(root / "cafeteria.db").is_success(), "Debe abrir la base");
        MigrationRunner runner(db);
        auto m = runner.run_pending_migrations_from_dir(mig);
        TEST_CHECK(m.is_success(), "Migraciones: " + m.error_message());
        account = std::make_shared<SqliteAccountService>(db);
        catalog = std::make_shared<SqliteCatalogService>(db, account);
        inventory = std::make_shared<SqliteInventoryService>(db, account);
        shift = std::make_shared<SqliteShiftService>(db, account);
        cart = std::make_shared<SqliteCartService>(db, account, catalog);
        sale = std::make_shared<SqliteSaleService>(db, account, cart, shift);
        report = std::make_shared<SqliteReportService>(db, account);

        TEST_CHECK(account->create_initial_owner("dueno_b15", "Dueño B15", kOwnerPwd, "dueno@cafeteria-itch.edu.mx").is_success(), "Alta del Dueño");
        CreateUserDto c{"cajero_b15", "Cajero B15", kCashierPwd, "role-cashier", "cajero@cafeteria-itch.edu.mx"};
        TEST_CHECK(account->create_user(c).is_success(), "Alta del cajero");
        p_1250 = make_product("B15-1250", "750300000001", 1250);
        p_3399 = make_product("B15-3399", "750300000002", 3399);
        p_799 = make_product("B15-0799", "750300000003", 799);
    }

    std::string make_product(const std::string& sku, const std::string& barcode, std::int64_t cents) {
        CreateProductDto p;
        p.track_stock = false;
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

    std::int64_t num(const std::string& sql) {
        auto prep = db.prepare(sql);
        TEST_CHECK(prep.is_success(), "Preparar: " + sql);
        auto st = std::move(prep.value());
        auto step = st.step();
        TEST_CHECK(step.is_success() && step.value(), "Sin fila: " + sql);
        return st.get_int64(0);
    }

    std::string text(const std::string& sql) {
        auto prep = db.prepare(sql);
        TEST_CHECK(prep.is_success(), "Preparar: " + sql);
        auto st = std::move(prep.value());
        auto step = st.step();
        TEST_CHECK(step.is_success() && step.value(), "Sin fila: " + sql);
        return st.get_text(0);
    }

    void login(const std::string& user, const std::string& pwd) {
        account->logout();
        TEST_CHECK(account->login(user, pwd).is_success(), "Login " + user);
    }

    std::string sell(const std::vector<std::string>& products, PaymentMethod method) {
        for (const auto& pid : products) {
            auto a = cart->add_item(pid, 1, "");
            TEST_CHECK(a.is_success(), "Agregar al carrito: " + a.error_message());
        }
        auto total = cart->get_cart().value().total;
        auto r = sale->confirm_sale(method, method == PaymentMethod::Efectivo ? Money::from_cents(total.cents() + 500) : Money::zero());
        TEST_CHECK(r.is_success(), "confirm_sale: " + r.error_message());
        return r.value().folio;
    }

    void cleanup() {
        report.reset(); sale.reset(); cart.reset(); shift.reset(); inventory.reset(); catalog.reset(); account.reset();
        db.close();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

std::string today() { return app::domain::now().local.substr(0, 10); }

// ============================================================================================== TURNO

void test_closeout_flow(Env& e) {
    std::cout << "[TEST] B15: vista previa, movimientos de caja, cierre sin diferencia e instantánea inmutable..." << std::endl;
    e.login("dueno_b15", kOwnerPwd);

    // Sin turno
    TEST_CHECK(e.shift->preview_closeout().error_code() == ErrorCode::NotFound, "Sin turno: no hay vista previa");
    auto no_shift = e.shift->record_cash_movement(CashMovementKind::Inflow, Money::from_cents(100), "Cambio inicial");
    TEST_CHECK(!no_shift.is_success() && no_shift.error_message() == "Abre un turno antes de registrar movimientos de caja",
               "Sin turno no hay movimientos: " + no_shift.error_message());
    TEST_CHECK(e.shift->close_shift_with_count("", Money::zero(), "").error_code() == ErrorCode::NotFound, "Sin turno no se cierra");

    TEST_CHECK(e.shift->open_shift("reg-01", Money::from_cents(50000)).is_success(), "Abrir turno");
    const std::string f1 = e.sell({e.p_1250}, PaymentMethod::Efectivo);
    e.sell({e.p_799, e.p_799}, PaymentMethod::Efectivo);
    e.sell({e.p_3399}, PaymentMethod::Tarjeta);
    e.sell({e.p_799}, PaymentMethod::Transferencia);
    // Descuento de B14 en la primera venta (como lo deja confirm_sale con una promoción): total y pago bajan 100.
    e.exec("UPDATE sales SET total_cents = total_cents - 100, discount_cents = 100 WHERE folio = '" + f1 + "';");
    e.exec("UPDATE payment_records SET amount_cents = amount_cents - 100 WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + f1 + "');");
    e.exec("UPDATE sale_items SET discount_cents = 100 WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + f1 + "');");

    // Movimientos de caja: validaciones
    TEST_CHECK(e.shift->record_cash_movement(CashMovementKind::Inflow, Money::zero(), "Cambio de tienda").error_code() == ErrorCode::InvalidInput, "Importe 0");
    TEST_CHECK(e.shift->record_cash_movement(CashMovementKind::Inflow, Money::from_cents(-5), "Cambio de tienda").error_code() == ErrorCode::InvalidInput, "Importe negativo");
    auto short_desc = e.shift->record_cash_movement(CashMovementKind::Inflow, Money::from_cents(100), "ab");
    TEST_CHECK(short_desc.error_code() == ErrorCode::InvalidInput && short_desc.error_message() == "Escribe una descripción de 3 a 200 caracteres", "Descripción corta");
    TEST_CHECK(e.shift->record_cash_movement(CashMovementKind::Inflow, Money::from_cents(100), std::string(201, 'x')).error_code() == ErrorCode::InvalidInput, "Descripción larga");
    TEST_CHECK(e.shift->record_cash_movement(CashMovementKind::Inflow, Money::from_cents(100), "   ").error_code() == ErrorCode::InvalidInput, "Descripción en blanco");
    auto too_much = e.shift->record_cash_movement(CashMovementKind::Outflow, Money::from_cents(99999999), "Pago grande");
    TEST_CHECK(too_much.error_code() == ErrorCode::InvalidInput && too_much.error_message() == "No hay tanto efectivo en caja para esa salida",
               "Salida mayor que el efectivo esperado: " + too_much.error_message());
    TEST_CHECK(e.num("SELECT COUNT(*) FROM cash_movements WHERE movement_type IN ('INFLOW','OUTFLOW');") == 0, "Los rechazos no escriben nada");
    auto in = e.shift->record_cash_movement(CashMovementKind::Inflow, Money::from_cents(2000), "Cambio de la tienda");
    TEST_CHECK(in.is_success() && in.value().amount.cents() == 2000 && in.value().kind == CashMovementKind::Inflow, "Entrada: " + in.error_message());
    auto out = e.shift->record_cash_movement(CashMovementKind::Outflow, Money::from_cents(1000), "Pago a proveedor de hielo");
    TEST_CHECK(out.is_success(), "Salida: " + out.error_message());
    TEST_CHECK(e.num("SELECT COUNT(*) FROM audit_events WHERE event_type IN ('CASH_INFLOW','CASH_OUTFLOW');") == 2, "Los movimientos se auditan");

    // Vista previa
    auto pv = e.shift->preview_closeout();
    TEST_CHECK(pv.is_success(), "Vista previa: " + pv.error_message());
    const auto& p = pv.value();
    TEST_CHECK(!p.is_closed && !p.snapshot_saved, "Vista previa: turno abierto");
    TEST_CHECK(p.initial_cash.cents() == 50000, "Fondo inicial");
    TEST_CHECK(p.sales_count == 4, "Ventas del turno");
    TEST_CHECK(p.gross_sales.cents() == 1250 + 1598 + 3399 + 799, "Bruto");
    TEST_CHECK(p.discount_total.cents() == 100, "Descuento");
    TEST_CHECK(p.net_sales.cents() == p.gross_sales.cents() - 100, "Neto = bruto - descuento");
    TEST_CHECK(p.cash_sales.cents() == 1150 + 1598 && p.card_sales.cents() == 3399 && p.transfer_sales.cents() == 799, "Ventas por método");
    TEST_CHECK(p.net_sales.cents() == p.cash_sales.cents() + p.card_sales.cents() + p.transfer_sales.cents() && p.payments_mismatch_cents == 0, "Neto = pagos");
    TEST_CHECK(p.cash_inflows.cents() == 2000 && p.cash_outflows.cents() == 1000, "Entradas y salidas");
    TEST_CHECK(p.expected_cash.cents() == 50000 + 2748 + 2000 - 1000, "Efectivo esperado");
    TEST_CHECK(p.movements.size() == 2 && p.movements[0].description == "Cambio de la tienda" && p.movements[1].kind == CashMovementKind::Outflow, "Lista de movimientos");
    const std::string shift_id = p.shift_id;

    // Una salida igual al esperado se acepta (queda en cero) pero la dejamos fuera: no cambiar el esperado.

    // Cierre sin diferencia: no pide motivo
    auto closed = e.shift->close_shift_with_count(shift_id, Money::from_cents(53748), "");
    TEST_CHECK(closed.is_success(), "Cierre sin diferencia: " + closed.error_message());
    const auto c = closed.value();
    TEST_CHECK(c.is_closed && c.snapshot_saved && c.difference_cents == 0 && c.difference_reason.empty(), "Instantánea sin diferencia");
    TEST_CHECK(c.expected_cash.cents() == 53748 && c.counted_cash.cents() == 53748, "Esperado y contado");
    TEST_CHECK(c.closed_by_name == "Dueño B15" && c.opened_by_name == "Dueño B15" && !c.closed_at.empty(), "Quién lo cerró");
    TEST_CHECK(c.movements.size() == 2, "El corte conserva los movimientos");

    // Instantánea inmutable: cambian ventas, precios y pagos después y el corte guardado no cambia.
    e.exec("UPDATE sales SET total_cents = 1, discount_cents = 0 WHERE shift_id = '" + shift_id + "';");
    e.exec("UPDATE payment_records SET amount_cents = 1 WHERE sale_id IN (SELECT id FROM sales WHERE shift_id = '" + shift_id + "');");
    e.exec("UPDATE sale_items SET unit_price_cents = 1, subtotal_cents = 1;");
    e.exec("DELETE FROM sales WHERE folio = '" + f1 + "';");
    auto again = e.shift->get_closeout(shift_id);
    TEST_CHECK(again.is_success(), "get_closeout: " + again.error_message());
    const auto& a = again.value();
    TEST_CHECK(a.sales_count == c.sales_count && a.gross_sales.cents() == c.gross_sales.cents() && a.discount_total.cents() == 100 &&
               a.net_sales.cents() == c.net_sales.cents() && a.cash_sales.cents() == c.cash_sales.cents() &&
               a.card_sales.cents() == c.card_sales.cents() && a.transfer_sales.cents() == c.transfer_sales.cents() &&
               a.expected_cash.cents() == 53748 && a.counted_cash.cents() == 53748 && a.difference_cents == 0,
               "La instantánea no cambia aunque cambien las ventas después");
    // Cerrar dos veces: Conflict en lenguaje llano y sin segunda instantánea
    const std::string closed_at = e.text("SELECT closed_at FROM shifts WHERE id = '" + shift_id + "';");
    auto twice = e.shift->close_shift_with_count(shift_id, Money::from_cents(1), "otro intento");
    TEST_CHECK(twice.error_code() == ErrorCode::Conflict && twice.error_message() == "Ese turno ya fue cerrado. No se puede cerrar dos veces.", "Doble cierre: " + twice.error_message());
    TEST_CHECK(e.text("SELECT closed_at FROM shifts WHERE id = '" + shift_id + "';") == closed_at &&
               e.num("SELECT final_cash_cents FROM shifts WHERE id = '" + shift_id + "';") == 53748, "El doble cierre no cambió nada");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM audit_events WHERE event_type = 'SHIFT_CLOSED';") == 1, "Una sola auditoría de cierre");
    TEST_CHECK(e.shift->close_shift(Money::from_cents(1)).error_code() == ErrorCode::NotFound, "close_shift sin turno abierto: NotFound");
    TEST_CHECK(e.shift->record_cash_movement(CashMovementKind::Inflow, Money::from_cents(100), "Cambio de tienda").error_code() == ErrorCode::NotFound,
               "Turno cerrado: no admite movimientos");
}

void test_differences_and_legacy(Env& e) {
    std::cout << "[TEST] B15: diferencias con motivo, close_shift heredado y turnos anteriores a la migración 020..." << std::endl;
    e.login("dueno_b15", kOwnerPwd);

    // Faltante sin motivo: se rechaza y el turno sigue abierto
    TEST_CHECK(e.shift->open_shift("reg-01", Money::from_cents(10000)).is_success(), "Abrir turno 2");
    e.sell({e.p_1250}, PaymentMethod::Efectivo);   // esperado 11250
    const std::string id2 = e.shift->get_current_shift().value().id;
    auto miss = e.shift->close_shift_with_count(id2, Money::from_cents(11000), "");
    TEST_CHECK(miss.error_code() == ErrorCode::InvalidInput && miss.error_message() == "Indica el motivo de la diferencia (de 3 a 200 caracteres)",
               "Faltante sin motivo: " + miss.error_message());
    TEST_CHECK(e.shift->close_shift_with_count(id2, Money::from_cents(11000), "ab").error_code() == ErrorCode::InvalidInput, "Motivo demasiado corto");
    TEST_CHECK(e.shift->close_shift_with_count(id2, Money::from_cents(11000), std::string(201, 'm')).error_code() == ErrorCode::InvalidInput, "Motivo demasiado largo");
    TEST_CHECK(e.shift->close_shift_with_count(id2, Money::from_cents(-1), "negativo").error_code() == ErrorCode::InvalidInput, "Contado negativo");
    TEST_CHECK(e.shift->get_current_shift().is_success(), "Los rechazos dejan el turno abierto");
    auto ok = e.shift->close_shift_with_count(id2, Money::from_cents(11000), "  Se dio mal el cambio  ");
    TEST_CHECK(ok.is_success(), "Faltante con motivo: " + ok.error_message());
    TEST_CHECK(ok.value().difference_cents == -250 && ok.value().difference_reason == "Se dio mal el cambio", "Faltante -250 y motivo recortado");

    // Sobrante
    TEST_CHECK(e.shift->open_shift("reg-01", Money::from_cents(5000)).is_success(), "Abrir turno 3");
    auto over = e.shift->close_shift_with_count("", Money::from_cents(5300), "Propina en la caja");
    TEST_CHECK(over.is_success() && over.value().difference_cents == 300, "Sobrante +300");

    // close_shift(Money) heredado: cierra con cualquier diferencia y motivo "Sin motivo registrado"
    TEST_CHECK(e.shift->open_shift("reg-01", Money::from_cents(7000)).is_success(), "Abrir turno 4");
    const std::string id4 = e.shift->get_current_shift().value().id;
    TEST_CHECK(e.shift->close_shift(Money::from_cents(6000)).is_success(), "close_shift heredado cierra con diferencia");
    auto legacy = e.shift->get_closeout(id4);
    TEST_CHECK(legacy.is_success() && legacy.value().difference_cents == -1000 && legacy.value().difference_reason == "Sin motivo registrado" &&
               legacy.value().snapshot_saved, "Instantánea del cierre heredado: " + legacy.error_message());
    TEST_CHECK(e.shift->open_shift("reg-01", Money::from_cents(7000)).is_success(), "Abrir turno 5");
    TEST_CHECK(e.shift->close_shift(Money::from_cents(7000)).is_success(), "close_shift heredado sin diferencia");

    // Turno cerrado antes de la migración 020: sin instantánea, se calcula al consultar
    e.exec("INSERT INTO shifts (id, register_id, cashier_user_id, opened_at, closed_at, initial_cash_cents, final_cash_cents, is_open) "
           "VALUES ('shift-viejo', 'reg-01', (SELECT id FROM users WHERE username = 'dueno_b15'), '2026-01-10 08:00:00', '2026-01-10 20:00:00', 3000, 3500, 0);");
    auto old = e.shift->get_closeout("shift-viejo");
    TEST_CHECK(old.is_success() && !old.value().snapshot_saved && old.value().is_closed, "Turno viejo: " + old.error_message());
    TEST_CHECK(old.value().expected_cash.cents() == 3000 && old.value().counted_cash.cents() == 3500 && old.value().difference_cents == 500, "Turno viejo: se calcula al consultar");
    TEST_CHECK(e.shift->get_closeout("no-existe").error_code() == ErrorCode::NotFound, "Turno inexistente");

    // Historial
    auto all = e.shift->list_closed_shifts(today(), today());
    TEST_CHECK(all.is_success() && all.value().items.size() >= 5 && !all.value().truncated, "Historial de hoy: " + all.error_message());
    TEST_CHECK(all.value().items.front().closed_at >= all.value().items.back().closed_at, "Del más reciente al más antiguo");
    auto one = e.shift->list_closed_shifts(today(), today(), 2);
    TEST_CHECK(one.is_success() && one.value().items.size() == 2 && one.value().truncated, "Límite con truncated");
    auto jan = e.shift->list_closed_shifts("2026-01-01", "2026-01-31");
    TEST_CHECK(jan.is_success() && jan.value().items.size() == 1 && jan.value().items[0].shift_id == "shift-viejo", "Filtro por fecha de cierre");
    TEST_CHECK(e.shift->list_closed_shifts("2026-02-30", "2026-03-01").error_code() == ErrorCode::InvalidInput, "Fecha inválida");
    TEST_CHECK(e.shift->list_closed_shifts("2026-03-02", "2026-03-01").error_code() == ErrorCode::InvalidInput, "Rango invertido");
}

void test_close_others(Env& e) {
    std::cout << "[TEST] B15: cerrar el turno de otro usuario (permiso shift.close_others) y bitácora..." << std::endl;
    e.login("dueno_b15", kOwnerPwd);
    // Rol con el permiso nuevo y usuario con ese rol
    TEST_CHECK(e.num("SELECT COUNT(*) FROM role_permissions WHERE role_id = 'role-owner' AND permission_id = 'shift.close_others';") == 1, "Dueño tiene el permiso nuevo");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM role_permissions WHERE role_id = 'role-cashier' AND permission_id = 'shift.close_others';") == 0, "Cajero no lo tiene");
    auto role = e.account->create_role({"Supervisor B15", "Cierra turnos de otros", {"shift.operate", "shift.close_others"}});
    TEST_CHECK(role.is_success(), "Crear rol: " + role.error_message());
    CreateUserDto sup{"super_b15", "Supervisor B15", kCashierPwd, role.value().id, "super@cafeteria-itch.edu.mx"};
    TEST_CHECK(e.account->create_user(sup).is_success(), "Alta del supervisor");
    CreateUserDto c2{"cajero2_b15", "Cajero Dos", kCashierPwd, "role-cashier", "cajero2@cafeteria-itch.edu.mx"};
    TEST_CHECK(e.account->create_user(c2).is_success(), "Alta del segundo cajero");

    // El cajero abre; otro cajero sin permiso no puede cerrarlo; sí puede ver la vista previa
    e.login("cajero_b15", kCashierPwd);
    TEST_CHECK(e.shift->open_shift("reg-01", Money::from_cents(2000)).is_success(), "El cajero abre su turno");
    const std::string sid = e.shift->get_current_shift().value().id;
    e.login("cajero2_b15", kCashierPwd);
    TEST_CHECK(e.shift->preview_closeout(sid).is_success(), "Otro cajero puede ver la vista previa");
    auto denied = e.shift->close_shift_with_count(sid, Money::from_cents(2000), "");
    TEST_CHECK(denied.error_code() == ErrorCode::Forbidden, "Cajero sin permiso no cierra el turno de otro");
    TEST_CHECK(e.shift->close_shift(Money::from_cents(2000)).error_code() == ErrorCode::Forbidden, "close_shift heredado tampoco");
    TEST_CHECK(e.shift->get_current_shift().is_success(), "El turno sigue abierto");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM audit_events WHERE event_type LIKE 'SHIFT_CLOSED%' AND created_at >= '" + today() + "' AND details LIKE '%" + sid + "%';") == 0, "Sin cierre no hay bitácora");
    TEST_CHECK(e.shift->get_closeout(sid).is_success(), "Turno abierto: lo ve quien tiene shift.operate");
    TEST_CHECK(e.shift->record_cash_movement(CashMovementKind::Inflow, Money::from_cents(500), "Cambio del turno ajeno").is_success(), "Cualquiera con shift.operate registra movimientos");

    // El supervisor (permiso nuevo) lo cierra y la bitácora deja ambos nombres
    e.login("super_b15", kCashierPwd);
    auto by_sup = e.shift->close_shift_with_count(sid, Money::from_cents(2500), "");
    TEST_CHECK(by_sup.is_success(), "Supervisor cierra el turno ajeno: " + by_sup.error_message());
    TEST_CHECK(by_sup.value().opened_by_name == "Cajero B15" && by_sup.value().closed_by_name == "Supervisor B15", "Quién abrió y quién cerró");
    auto audit = e.text("SELECT details FROM audit_events WHERE event_type = 'SHIFT_CLOSED_BY_OTHER' ORDER BY created_at DESC LIMIT 1;");
    TEST_CHECK(audit.find("Cajero B15") != std::string::npos && audit.find("Supervisor B15") != std::string::npos && audit.find(sid) != std::string::npos,
               "Bitácora con ambos nombres: " + audit);
    TEST_CHECK(e.num("SELECT COUNT(*) FROM audit_events WHERE event_type = 'SHIFT_CLOSED' AND details LIKE '%" + sid + "%';") == 0, "No se duplica como cierre propio");

    // El Dueño cierra el turno de otro sin permiso extra
    e.login("cajero_b15", kCashierPwd);
    TEST_CHECK(e.shift->open_shift("reg-01", Money::from_cents(100)).is_success(), "Cajero abre otro turno");
    const std::string sid2 = e.shift->get_current_shift().value().id;
    e.login("dueno_b15", kOwnerPwd);
    auto by_owner = e.shift->close_shift_with_count(sid2, Money::from_cents(100), "");
    TEST_CHECK(by_owner.is_success() && by_owner.value().closed_by_name == "Dueño B15", "El Dueño cierra el turno de otro: " + by_owner.error_message());

    // Cada quien cierra el propio con shift.operate
    e.login("cajero_b15", kCashierPwd);
    TEST_CHECK(e.shift->open_shift("reg-01", Money::from_cents(100)).is_success(), "Cajero abre");
    TEST_CHECK(e.shift->close_shift(Money::from_cents(100)).is_success(), "Cajero cierra el propio");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM audit_events WHERE event_type = 'SHIFT_CLOSED';") >= 1, "Cierre propio auditado");

    // Historial y consulta: el cajero solo ve los propios; sin reports.view no ve los de otros
    auto mine = e.shift->list_closed_shifts(today(), today());
    TEST_CHECK(mine.is_success(), "Historial del cajero");
    for (const auto& it : mine.value().items) TEST_CHECK(it.opened_by_name == "Cajero B15", "El cajero solo ve los turnos que abrió");
    TEST_CHECK(!mine.value().items.empty(), "El cajero ve los suyos");
    TEST_CHECK(e.shift->get_closeout(by_owner.value().shift_id).is_success(), "El turno propio cerrado por otro se ve");
    e.login("cajero2_b15", kCashierPwd);
    TEST_CHECK(e.shift->get_closeout(by_owner.value().shift_id).error_code() == ErrorCode::Forbidden, "Turno cerrado ajeno: sin reports.view no se ve");
    e.login("dueno_b15", kOwnerPwd);
    auto owner_all = e.shift->list_closed_shifts(today(), today());
    TEST_CHECK(owner_all.is_success() && owner_all.value().items.size() > mine.value().items.size(), "El Dueño ve todos");

    // Sin sesión
    e.account->logout();
    TEST_CHECK(e.shift->preview_closeout().error_code() == ErrorCode::Forbidden, "Sin sesión: vista previa");
    TEST_CHECK(e.shift->close_shift_with_count("", Money::zero(), "").error_code() == ErrorCode::Forbidden, "Sin sesión: cierre");
    TEST_CHECK(e.shift->record_cash_movement(CashMovementKind::Inflow, Money::from_cents(1), "Sin sesion").error_code() == ErrorCode::Forbidden, "Sin sesión: movimiento");
    TEST_CHECK(e.shift->list_closed_shifts(today(), today()).error_code() == ErrorCode::Forbidden, "Sin sesión: historial");
}

void test_migration_020(Env& e) {
    std::cout << "[TEST] B15: la migración 020 es repetible y el permiso queda una sola vez..." << std::endl;
    const auto before = e.num("SELECT COUNT(*) FROM schema_migrations;");
    MigrationRunner runner(e.db);
    TEST_CHECK(runner.run_pending_migrations_from_dir(e.migrations).is_success(), "Segunda pasada de migraciones");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM schema_migrations;") == before, "Sin migraciones nuevas");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM permissions WHERE id = 'shift.close_others';") == 1, "Permiso único");
    TEST_CHECK(e.text("SELECT title FROM permissions WHERE id = 'shift.close_others';") == "Cerrar turno de otro usuario", "Título del permiso");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM sqlite_master WHERE type = 'index' AND name IN ('idx_sales_shift','idx_sale_items_sale','idx_payment_records_sale','idx_sale_items_product');") == 4, "Índices creados");
}

// ============================================================================================== REPORTES

struct CsvFile {
    std::string bytes;
    std::vector<std::string> lines;   // sin CRLF
};

CsvFile read_csv(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    CsvFile f;
    f.bytes = ss.str();
    std::string cur;
    for (std::size_t i = 0; i < f.bytes.size(); ++i) {
        if (f.bytes[i] == '\r' && i + 1 < f.bytes.size() && f.bytes[i + 1] == '\n') { f.lines.push_back(cur); cur.clear(); ++i; }
        else cur += f.bytes[i];
    }
    if (!cur.empty()) f.lines.push_back(cur);
    return f;
}

void test_range_reports(Env& e) {
    std::cout << "[TEST] B15: reportes por rango: totales que cuadran en todas las vistas..." << std::endl;
    e.login("dueno_b15", kOwnerPwd);
    TEST_CHECK(e.shift->open_shift("reg-01", Money::from_cents(1000)).is_success(), "Abrir turno");
    // Tres días, dos cajeros
    const std::string f1 = e.sell({e.p_1250}, PaymentMethod::Efectivo);
    const std::string f2 = e.sell({e.p_799, e.p_3399}, PaymentMethod::Tarjeta);
    e.login("cajero_b15", kCashierPwd);
    const std::string f3 = e.sell({e.p_799, e.p_799}, PaymentMethod::Transferencia);
    const std::string f4 = e.sell({e.p_1250}, PaymentMethod::Efectivo);
    e.exec("UPDATE sales SET created_at = '2026-05-10 09:00:00' WHERE folio = '" + f1 + "';");
    e.exec("UPDATE sales SET created_at = '2026-05-11 23:59:59' WHERE folio = '" + f2 + "';");
    e.exec("UPDATE sales SET created_at = '2026-05-12 00:00:00' WHERE folio = '" + f3 + "';");
    e.exec("UPDATE sales SET created_at = '2026-06-30 12:00:00' WHERE folio = '" + f4 + "';");
    // Descuento de B14 en f2 (150) con su partida
    e.exec("UPDATE sales SET total_cents = total_cents - 150, discount_cents = 150 WHERE folio = '" + f2 + "';");
    e.exec("UPDATE payment_records SET amount_cents = amount_cents - 150 WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + f2 + "');");
    e.exec("UPDATE sale_items SET discount_cents = 150 WHERE id = (SELECT id FROM sale_items WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + f2 + "') ORDER BY subtotal_cents DESC LIMIT 1);");

    // El cajero no tiene reports.view
    TEST_CHECK(e.report->sales_by_day("2026-05-01", "2026-05-31").error_code() == ErrorCode::Forbidden, "Cajero sin reports.view");
    TEST_CHECK(e.report->export_report_csv(ReportKind::SalesByDay, "2026-05-01", "2026-05-31", (e.root / "x.csv").u8string()).error_code() == ErrorCode::Forbidden, "Exportar exige reports.view");
    e.login("dueno_b15", kOwnerPwd);

    auto d = e.report->sales_by_day("2026-05-01", "2026-05-31");
    TEST_CHECK(d.is_success(), "Por día: " + d.error_message());
    TEST_CHECK(d.value().rows.size() == 3, "Tres días con ventas en mayo");
    TEST_CHECK(d.value().rows[0].date == "2026-05-10" && d.value().rows[0].net.cents() == 1250 && d.value().rows[0].cash.cents() == 1250, "Día 10");
    TEST_CHECK(d.value().rows[1].date == "2026-05-11" && d.value().rows[1].gross.cents() == 4198 && d.value().rows[1].discount.cents() == 150 &&
               d.value().rows[1].net.cents() == 4048 && d.value().rows[1].card.cents() == 4048, "Día 11 con descuento");
    TEST_CHECK(d.value().rows[2].date == "2026-05-12" && d.value().rows[2].transfer.cents() == 1598, "Día 12 (00:00:00 entra en el 12)");
    const auto& t = d.value().totals;
    TEST_CHECK(t.sales_count == 3 && t.gross.cents() == 1250 + 4198 + 1598 && t.discount.cents() == 150 && t.net.cents() == t.gross.cents() - t.discount.cents(), "Totales: bruto - descuento = neto");
    TEST_CHECK(t.net.cents() == t.payments_total.cents() && t.payments_mismatch_cents == 0, "Neto = pagos");
    std::int64_t sum_net = 0, sum_gross = 0, sum_disc = 0;
    for (const auto& r : d.value().rows) { sum_net += r.net.cents(); sum_gross += r.gross.cents(); sum_disc += r.discount.cents(); }
    TEST_CHECK(sum_net == t.net.cents() && sum_gross == t.gross.cents() && sum_disc == t.discount.cents(), "Las filas suman los totales");

    auto p = e.report->sales_by_product("2026-05-01", "2026-05-31");
    TEST_CHECK(p.is_success(), "Por producto: " + p.error_message());
    std::int64_t pg = 0, pd = 0, pn = 0, pieces = 0;
    for (const auto& r : p.value().rows) {
        pg += r.gross.cents(); pd += r.discount.cents(); pn += r.net.cents(); pieces += r.pieces;
        TEST_CHECK(r.net.cents() == r.gross.cents() - r.discount.cents(), "Renglón: bruto - descuento = neto");
    }
    TEST_CHECK(pg == t.gross.cents() && pd == t.discount.cents() && pn == t.net.cents(), "Por producto cuadra con por día");
    TEST_CHECK(pieces == 1 + 2 + 2 && p.value().totals.pieces == 5, "Piezas");
    TEST_CHECK(p.value().rows.front().net.cents() >= p.value().rows.back().net.cents(), "Ordenado por neto descendente");

    auto m = e.report->sales_by_payment_method("2026-05-01", "2026-05-31");
    TEST_CHECK(m.is_success() && m.value().rows.size() == 3, "Por método: " + m.error_message());
    std::int64_t pm = 0;
    for (const auto& r : m.value().rows) pm += r.amount.cents();
    TEST_CHECK(pm == t.net.cents() && m.value().totals.payments_total.cents() == pm, "Por método cuadra");

    auto c = e.report->sales_by_cashier("2026-05-01", "2026-05-31");
    TEST_CHECK(c.is_success() && c.value().rows.size() == 2, "Por cajero: " + c.error_message());
    std::int64_t cn = 0, cd = 0;
    for (const auto& r : c.value().rows) { cn += r.net.cents(); cd += r.discount.cents(); TEST_CHECK(!r.cashier_name.empty(), "Nombre del cajero"); }
    TEST_CHECK(cn == t.net.cents() && cd == 150, "Por cajero cuadra");

    // Rango que cruza meses
    auto wide = e.report->sales_by_day("2026-05-01", "2026-06-30");
    TEST_CHECK(wide.is_success() && wide.value().rows.size() == 4 && wide.value().totals.sales_count == 4, "Rango de dos meses");
    // Rango vacío
    auto empty = e.report->sales_by_day("2020-01-01", "2020-01-31");
    TEST_CHECK(empty.is_success() && empty.value().rows.empty() && empty.value().totals.net.cents() == 0 && empty.value().totals.payments_mismatch_cents == 0, "Rango vacío");
    // Un solo día
    auto one = e.report->sales_by_day("2026-05-11", "2026-05-11");
    TEST_CHECK(one.is_success() && one.value().rows.size() == 1 && one.value().totals.net.cents() == 4048, "Un solo día");
    // Validaciones
    TEST_CHECK(e.report->sales_by_day("2026-13-01", "2026-13-02").error_code() == ErrorCode::InvalidInput, "Fecha inválida");
    TEST_CHECK(e.report->sales_by_day("2026-05-31", "2026-05-01").error_code() == ErrorCode::InvalidInput, "Rango invertido");
    auto big = e.report->sales_by_day("2025-01-01", "2026-12-31");
    TEST_CHECK(big.error_code() == ErrorCode::InvalidInput && big.error_message() == "El rango es demasiado largo: elija como máximo 366 días", "Rango largo: " + big.error_message());
    TEST_CHECK(e.report->sales_by_day("2026-01-01", "2026-12-31").is_success(), "365 días se acepta");
    TEST_CHECK(e.report->sales_by_day("2026-05-1", "2026-05-31").error_code() == ErrorCode::InvalidInput, "Formato");
    TEST_CHECK(e.report->sales_by_day("2026-05-01'; DROP TABLE sales;--", "2026-05-31").error_code() == ErrorCode::InvalidInput, "Texto raro");

    // Discrepancia de pagos visible, sin ocultarla
    e.exec("UPDATE payment_records SET amount_cents = amount_cents + 7 WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + f1 + "');");
    auto mm = e.report->sales_by_day("2026-05-10", "2026-05-10");
    TEST_CHECK(mm.is_success() && mm.value().totals.payments_mismatch_cents == -7, "Pagos de más: discrepancia -7");
    e.exec("UPDATE payment_records SET amount_cents = amount_cents - 7 WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + f1 + "');");
}

void test_inventory_reports(Env& e) {
    std::cout << "[TEST] B15: mermas, ajustes (positivos y negativos por separado), movimientos y compras..." << std::endl;
    e.login("dueno_b15", kOwnerPwd);
    auto ing = e.inventory->create_ingredient({"Leche B15", "VOLUME", "ml", 0});
    TEST_CHECK(ing.is_success(), "Ingrediente: " + ing.error_message());
    const std::string iid = ing.value().id;
    auto nocost = e.inventory->create_ingredient({"Sin costo B15", "MASS", "mg", 0});
    TEST_CHECK(nocost.is_success(), "Ingrediente sin costo");
    TEST_CHECK(e.inventory->register_purchase_entry(iid, 1000, "ml", Money::from_cents(5000)).is_success(), "Compra 1000 ml por $50");
    TEST_CHECK(e.inventory->register_waste(iid, 100, "ml", "Se derramó").is_success(), "Merma 100 ml");
    TEST_CHECK(e.inventory->register_adjustment(iid, 50, "ml", "Conteo físico mayor").is_success(), "Ajuste +50");
    TEST_CHECK(e.inventory->register_adjustment(iid, -30, "ml", "Conteo físico menor").is_success(), "Ajuste -30");
    TEST_CHECK(e.inventory->register_adjustment(nocost.value().id, 400, "mg", "Alta sin costo").is_success(), "Ajuste sin costo");
    const std::string d = today();

    auto l = e.report->inventory_losses(d, d);
    TEST_CHECK(l.is_success(), "Mermas: " + l.error_message());
    const InventoryLossRowDto* leche = nullptr;
    const InventoryLossRowDto* sc = nullptr;
    for (const auto& r : l.value().rows) { if (r.ingredient_id == iid) leche = &r; else sc = &r; }
    TEST_CHECK(leche && sc && l.value().rows.size() == 2, "Dos ingredientes con mermas o ajustes");
    TEST_CHECK(leche->waste_quantity_base == 100 && leche->adjustment_in_quantity_base == 50 && leche->adjustment_out_quantity_base == 30, "Cantidades separadas, no netas");
    TEST_CHECK(leche->has_known_cost && leche->waste_estimated_cost.cents() == 500 && leche->adjustment_in_estimated_cost.cents() == 250 &&
               leche->adjustment_out_estimated_cost.cents() == 150, "Costo estimado con la valuación vigente");
    TEST_CHECK(!sc->has_known_cost && sc->adjustment_in_quantity_base == 400 && sc->adjustment_in_estimated_cost.cents() == 0, "Sin costo conocido: 0 y bandera apagada");
    TEST_CHECK(l.value().total_waste_estimated_cost.cents() == 500 && l.value().total_adjustment_out_estimated_cost.cents() == 150, "Totales de costo");

    auto mv = e.report->inventory_movements(d, d);
    TEST_CHECK(mv.is_success() && mv.value().rows.size() >= 5 && !mv.value().truncated, "Movimientos: " + mv.error_message());
    auto lim = e.report->inventory_movements(d, d, 2);
    TEST_CHECK(lim.is_success() && lim.value().rows.size() == 2 && lim.value().truncated, "Límite con truncated");
    TEST_CHECK(e.report->inventory_movements(d, d, 999999).is_success(), "El límite máximo se acota");

    auto pu = e.report->purchases_summary(d, d);
    TEST_CHECK(pu.is_success() && pu.value().rows.size() == 1 && pu.value().rows[0].total_cost.cents() == 5000 &&
               pu.value().rows[0].base_units_purchased == 1000 && pu.value().total_cost.cents() == 5000, "Resumen de compras: " + pu.error_message());
    auto none = e.report->inventory_losses("2020-01-01", "2020-01-02");
    TEST_CHECK(none.is_success() && none.value().rows.empty(), "Sin mermas en otro rango");
}

void test_csv_export(Env& e) {
    std::cout << "[TEST] B15: exportación a CSV: formato, nombres con comas y fórmulas, sin sobrescribir, errores llanos..." << std::endl;
    e.login("dueno_b15", kOwnerPwd);
    e.exec("UPDATE sale_items SET product_name = '=SUMA(A1)' WHERE id = (SELECT id FROM sale_items WHERE sale_id = (SELECT id FROM sales WHERE created_at = '2026-05-10 09:00:00'));");
    e.exec("UPDATE sale_items SET product_name = 'Café \"especial\", grande' WHERE subtotal_cents = 3399 AND sale_id = (SELECT id FROM sales WHERE created_at = '2026-05-11 23:59:59');");
    const fs::path dir = e.root / "salida";
    fs::create_directories(dir);
    const fs::path f = dir / "ventas por producto.csv";

    auto r = e.report->export_report_csv(ReportKind::SalesByProduct, "2026-05-01", "2026-05-31", f.u8string());
    TEST_CHECK(r.is_success(), "Exportar: " + r.error_message());
    TEST_CHECK(r.value().file_name == "ventas por producto.csv", "Solo el nombre del archivo");
    auto csv = read_csv(f);
    TEST_CHECK(csv.bytes.size() > 3 && csv.bytes.substr(0, 3) == "\xEF\xBB\xBF", "UTF-8 con BOM");
    TEST_CHECK(csv.lines.size() >= 3, "Encabezado, filas y total");
    // B18: las columnas nuevas van SOLO al final; las anteriores conservan su orden
    const std::string old_header = "\xEF\xBB\xBFProducto,Piezas,Importe bruto,Descuento,Importe neto";
    TEST_CHECK(csv.lines[0].rfind(old_header + ",", 0) == 0, "Encabezados en español, columnas anteriores en su orden: " + csv.lines[0]);
    TEST_CHECK(csv.lines[0] == old_header + ",Piezas reembolsadas,Reembolsos,Neto después de reembolsos", "Columnas nuevas al final: " + csv.lines[0]);
    TEST_CHECK(csv.bytes.find("\n") != std::string::npos && csv.bytes.find("\r\n") != std::string::npos, "CRLF");
    bool has_formula = false, has_quotes = false;
    for (const auto& line : csv.lines) {
        if (line.rfind("'=SUMA(A1),", 0) == 0) has_formula = true;
        if (line.rfind("\"Café \"\"especial\"\", grande\",", 0) == 0) has_quotes = true;
    }
    TEST_CHECK(has_formula, "La fórmula se neutraliza con apóstrofo");
    TEST_CHECK(has_quotes, "Comillas y comas según RFC 4180");
    TEST_CHECK(csv.lines.back().rfind("Total,", 0) == 0, "Renglón de total");
    TEST_CHECK(csv.lines.back() == "Total,5,70.46,1.50,68.96,0,0.00,68.96", "Importes con dos decimales y punto: " + csv.lines.back());
    // Sin 'double': 4198 centavos -> 41.98
    bool has_money = false;
    for (const auto& line : csv.lines) if (line.find(",12.50,") != std::string::npos) has_money = true;
    TEST_CHECK(has_money, "1250 centavos = 12.50");

    // No sobrescribe sin indicarlo
    const std::string before = csv.bytes;
    auto again = e.report->export_report_csv(ReportKind::SalesByProduct, "2026-05-01", "2026-05-10", f.u8string());
    TEST_CHECK(again.error_code() == ErrorCode::Conflict &&
               again.error_message() == "Ya existe un archivo con ese nombre. Elige otro nombre o confirma que quieres reemplazarlo.", "Sin sobrescribir: " + again.error_message());
    TEST_CHECK(read_csv(f).bytes == before, "El archivo existente no cambió");
    auto over = e.report->export_report_csv(ReportKind::SalesByProduct, "2026-05-01", "2026-05-10", f.u8string(), true);
    TEST_CHECK(over.is_success() && read_csv(f).bytes != before, "Con overwrite sí reemplaza: " + over.error_message());
    for (const auto& ent : fs::directory_iterator(dir)) TEST_CHECK(ent.path().extension() == ".csv", "No quedan archivos temporales: " + ent.path().u8string());

    // Todos los tipos de reporte
    const ReportKind kinds[] = {ReportKind::SalesByDay, ReportKind::SalesByPaymentMethod, ReportKind::SalesByCashier, ReportKind::InventoryLosses,
                                ReportKind::InventoryMovements, ReportKind::PurchasesSummary};
    int n = 0;
    for (auto k : kinds) {
        const fs::path fk = dir / ("reporte_" + std::to_string(n++) + ".csv");
        const std::string d = today();
        const bool inv = (k == ReportKind::InventoryLosses || k == ReportKind::InventoryMovements || k == ReportKind::PurchasesSummary);
        auto x = e.report->export_report_csv(k, inv ? d : "2026-05-01", inv ? d : "2026-05-31", fk.u8string());
        TEST_CHECK(x.is_success(), "Exportar tipo " + std::to_string(n) + ": " + x.error_message());
        auto ck = read_csv(fk);
        TEST_CHECK(ck.bytes.substr(0, 3) == "\xEF\xBB\xBF" && ck.lines.size() >= 2, "CSV válido del tipo " + std::to_string(n));
    }

    // Errores en lenguaje llano
    auto nodir = e.report->export_report_csv(ReportKind::SalesByDay, "2026-05-01", "2026-05-31", (e.root / "no existe" / "a.csv").u8string());
    TEST_CHECK(nodir.error_code() == ErrorCode::InvalidInput &&
               nodir.error_message() == "No se encontró la carpeta o la USB. Revisa que esté conectada y elige otra ubicación.", "Carpeta inexistente: " + nodir.error_message());
    TEST_CHECK(e.report->export_report_csv(ReportKind::SalesByDay, "2026-05-01", "2026-05-31", "").error_code() == ErrorCode::InvalidInput, "Ruta vacía");
    TEST_CHECK(e.report->export_report_csv(ReportKind::SalesByDay, "2026-05-01", "2026-05-31", dir.u8string()).error_code() == ErrorCode::InvalidInput, "Una carpeta no es un archivo");
    TEST_CHECK(e.report->export_report_csv(ReportKind::SalesByDay, "2026-05-31", "2026-05-01", (dir / "z.csv").u8string()).error_code() == ErrorCode::InvalidInput, "Rango inválido al exportar");
    TEST_CHECK(!fs::exists(dir / "z.csv"), "Un rango inválido no crea archivo");

    // Bitácora: tipo, rango y solo el nombre del archivo
    auto aud = e.text("SELECT details FROM audit_events WHERE event_type = 'REPORT_EXPORTED' ORDER BY created_at DESC, rowid DESC LIMIT 1;");
    TEST_CHECK(aud.find(today()) != std::string::npos && aud.find("Resumen de compras") != std::string::npos && aud.find("reporte_5.csv") != std::string::npos && aud.find("salida") == std::string::npos &&
               aud.find("itch_b15_") == std::string::npos, "REPORT_EXPORTED sin ruta: " + aud);
}

void test_performance(Env& e) {
    std::cout << "[TEST] B15: rendimiento de un mes con 30 000 ventas..." << std::endl;
    e.login("dueno_b15", kOwnerPwd);
    const std::string shift_id = e.text("SELECT id FROM shifts LIMIT 1;");
    const std::string user_id = e.text("SELECT id FROM users WHERE username = 'cajero_b15';");
    const std::string product_id = e.p_1250;
    const int n = 30000;
    e.exec("BEGIN;");
    e.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < " + std::to_string(n) + ") "
           "INSERT INTO sales (id, folio, shift_id, user_id, total_cents, created_at, discount_cents, cashier_name) "
           "SELECT 'ps-' || i, 'PF-' || i, '" + shift_id + "', '" + user_id + "', 1000 + (i % 7) * 100, "
           "'2026-04-' || printf('%02d', 1 + (i % 30)) || ' ' || printf('%02d', i % 24) || ':' || printf('%02d', i % 60) || ':00', 0, 'Cajero B15' FROM n;");
    e.exec("INSERT INTO sale_items (id, sale_id, product_id, product_name, quantity, unit_price_cents, subtotal_cents) "
           "SELECT 'pi-' || id, id, '" + product_id + "', 'Producto de rendimiento', 1, total_cents, total_cents FROM sales WHERE id LIKE 'ps-%';");
    e.exec("INSERT INTO payment_records (id, sale_id, payment_method, amount_cents, created_at) "
           "SELECT 'pp-' || id, id, CASE (length(id) % 3) WHEN 0 THEN 'EFECTIVO' WHEN 1 THEN 'TARJETA' ELSE 'TRANSFERENCIA' END, total_cents, created_at FROM sales WHERE id LIKE 'ps-%';");
    e.exec("COMMIT;");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM sales WHERE id LIKE 'ps-%';") == n, "30 000 ventas insertadas");

    auto ms_since = [](std::chrono::steady_clock::time_point t0) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    };
    const auto t0 = std::chrono::steady_clock::now();
    auto d = e.report->sales_by_day("2026-04-01", "2026-04-30");
    const auto day_ms = ms_since(t0);
    auto p = e.report->sales_by_product("2026-04-01", "2026-04-30");
    auto m = e.report->sales_by_payment_method("2026-04-01", "2026-04-30");
    auto c = e.report->sales_by_cashier("2026-04-01", "2026-04-30");
    const auto report_ms = ms_since(t0);
    std::cout << "[MEDIDA] por día: " << day_ms << " ms" << std::endl;
    TEST_CHECK(d.is_success() && p.is_success() && m.is_success() && c.is_success(), "Reportes de un mes");
    TEST_CHECK(d.value().totals.sales_count == n && d.value().rows.size() == 30, "Cuentan las 30 000 ventas en 30 días");
    TEST_CHECK(d.value().totals.net.cents() == p.value().totals.net.cents() && d.value().totals.payments_mismatch_cents == 0 &&
               d.value().totals.gross.cents() == d.value().totals.net.cents() + d.value().totals.discount.cents(), "Los totales cuadran con 30 000 ventas");
    std::int64_t pn = 0, cn = 0;
    for (const auto& r : p.value().rows) pn += r.net.cents();
    for (const auto& r : c.value().rows) cn += r.net.cents();
    TEST_CHECK(pn == d.value().totals.net.cents() && cn == d.value().totals.net.cents(), "Por producto y por cajero cuadran");

    const fs::path f = e.root / "mes.csv";
    const auto t1 = std::chrono::steady_clock::now();
    auto x = e.report->export_report_csv(ReportKind::SalesByDay, "2026-04-01", "2026-04-30", f.u8string());
    const auto export_ms = ms_since(t1);
    TEST_CHECK(x.is_success(), "Exportar el mes: " + x.error_message());
    std::cout << "[MEDIDA] cuatro reportes de ventas de 30 000 ventas: " << report_ms << " ms; exportación a CSV: " << export_ms << " ms" << std::endl;
#ifdef NDEBUG
    TEST_CHECK(report_ms < 3000, "Los cuatro reportes de un mes tardan menos de 3 s en Release (tardaron " + std::to_string(report_ms) + " ms)");
    TEST_CHECK(export_ms < 3000, "La exportación de un mes tarda menos de 3 s en Release (tardó " + std::to_string(export_ms) + " ms)");
#endif
}

} // namespace

int main(int argc, char** argv) {
    const std::string suite = argc > 1 ? argv[1] : "";
    if (suite != "shift" && suite != "reports") {
        std::cerr << "Uso: test_b15 shift | reports" << std::endl;
        return 2;
    }
    std::cout << "=== PRUEBAS B15 (" << suite << ") ===" << std::endl;
    fs::path mig = find_migrations_dir();
    TEST_CHECK(!mig.empty(), "Debe localizar la carpeta de migraciones");
    Env e(mig);
    if (suite == "shift") {
        test_migration_020(e);
        test_closeout_flow(e);
        test_differences_and_legacy(e);
        test_close_others(e);
    } else {
        test_range_reports(e);
        test_inventory_reports(e);
        test_csv_export(e);
        test_performance(e);
    }
    e.cleanup();
    std::cout << "\n>>> TODAS LAS PRUEBAS B15 (" << suite << ") PASARON CON EXITO! <<<\n" << std::endl;
    return 0;
}
