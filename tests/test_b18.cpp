// Pruebas B18: reembolsos de ventas y cargo extra por pago con tarjeta. Sin GUI, sin red y sin escribir ningún .exe.
// Uso: test_b18 refund | test_b18 surcharge
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include "app/domain/card_surcharge.hpp"
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
#include "app/services/sqlite_promotion_service.hpp"
#include "app/services/sqlite_refund_service.hpp"
#include "app/services/sqlite_payment_settings_service.hpp"
#include "app/services/mock_services.hpp"

#define TEST_CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA B18] " << (message) \
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
    std::shared_ptr<SqlitePromotionService> promo;
    std::shared_ptr<SqliteRefundService> refund;
    std::shared_ptr<SqlitePaymentSettingsService> settings;
    int seq{0};

    Env() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        root = fs::temp_directory_path() / fs::u8path("itch_b18_" + std::to_string(gen()));
        fs::create_directories(root);
        TEST_CHECK(db.open(root / "cafeteria.db").is_success(), "Abrir base");
        MigrationRunner runner(db);
        auto m = runner.run_pending_migrations_from_dir(fs::u8path(SOURCE_MIGRATIONS_DIR));
        TEST_CHECK(m.is_success(), "Migraciones: " + m.error_message());
        account = std::make_shared<SqliteAccountService>(db);
        catalog = std::make_shared<SqliteCatalogService>(db, account);
        inventory = std::make_shared<SqliteInventoryService>(db, account);
        shift = std::make_shared<SqliteShiftService>(db, account);
        cart = std::make_shared<SqliteCartService>(db, account, catalog);
        sale = std::make_shared<SqliteSaleService>(db, account, cart, shift);
        report = std::make_shared<SqliteReportService>(db, account);
        promo = std::make_shared<SqlitePromotionService>(db, account, catalog);
        refund = std::make_shared<SqliteRefundService>(db, account);
        settings = std::make_shared<SqlitePaymentSettingsService>(db, account);
        TEST_CHECK(account->create_initial_owner("dueno_b18", "Dueño B18", kOwnerPwd, "dueno@cafeteria-itch.edu.mx").is_success(), "Alta del Dueño");
        CreateUserDto c{"cajero_b18", "Cajero B18", kCashierPwd, "role-cashier", "cajero@cafeteria-itch.edu.mx"};
        TEST_CHECK(account->create_user(c).is_success(), "Alta del cajero");
        login("dueno_b18", kOwnerPwd);
    }

    void login(const std::string& user, const std::string& pwd) {
        account->logout();
        TEST_CHECK(account->login(user, pwd).is_success(), "Login " + user);
    }
    void owner() { login("dueno_b18", kOwnerPwd); }

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
        TEST_CHECK(step.is_success(), "Paso: " + sql);
        return step.value() && !st.is_null(0) ? st.get_text(0) : "";
    }
    std::string next(const std::string& p) { return p + "-" + std::to_string(++seq); }

    std::string ingredient(const std::string& name, const std::string& dim, const std::string& unit) {
        auto r = inventory->create_ingredient({name, dim, unit, 0});
        TEST_CHECK(r.is_success(), "Ingrediente: " + r.error_message());
        return r.value().id;
    }
    void buy(const std::string& ing, std::int64_t qty, const std::string& unit, std::int64_t cost_cents) {
        TEST_CHECK(inventory->register_purchase_entry(ing, qty, unit, Money::from_cents(cost_cents)).is_success(), "Compra");
    }
    // Directo sellado con existencia controlada (ingrediente por piezas enlazado).
    std::pair<std::string, std::string> direct(const std::string& name, std::int64_t price, std::int64_t stock) {
        CreateProductDto p;
        p.sku = next("D"); p.name = name; p.category = "Pruebas"; p.price = Money::from_cents(price);
        auto r = catalog->create_product(p);
        TEST_CHECK(r.is_success() && r.value().stock_tracked, "Directo: " + r.error_message());
        const std::string ing = catalog->get_recipe(r.value().id).value().items.at(0).ingredient_id;
        if (stock > 0) buy(ing, stock, "pz", stock * 100);
        return {r.value().id, ing};
    }
    std::string untracked(const std::string& name, std::int64_t price) {
        CreateProductDto p;
        p.sku = next("U"); p.name = name; p.category = "Pruebas"; p.price = Money::from_cents(price); p.track_stock = false;
        auto r = catalog->create_product(p);
        TEST_CHECK(r.is_success(), "Sin control: " + r.error_message());
        return r.value().id;
    }
    std::string prepared(const std::string& name, std::int64_t price, const std::string& ing, std::int64_t per_portion_mg) {
        CreateProductDto p;
        p.sku = next("P"); p.name = name; p.category = "Pruebas"; p.price = Money::from_cents(price); p.is_prepared = true;
        auto r = catalog->create_product(p);
        TEST_CHECK(r.is_success(), "Preparado: " + r.error_message());
        TEST_CHECK(catalog->set_recipe(r.value().id, {{ing, "", per_portion_mg, "mg"}}).is_success(), "Receta");
        return r.value().id;
    }
    std::string combo(const std::string& name, std::int64_t price, const std::vector<std::pair<std::string, int>>& parts) {
        CreateComboDto c;
        c.sku = next("C"); c.name = name; c.category = "Pruebas"; c.price = Money::from_cents(price);
        for (const auto& p : parts) c.components.push_back({p.first, "", p.second});
        auto r = promo->create_combo(c);
        TEST_CHECK(r.is_success(), "Combo: " + r.error_message());
        return r.value().product_id;
    }
    void open_shift(std::int64_t fund) {
        TEST_CHECK(shift->open_shift("reg-01", Money::from_cents(fund)).is_success(), "Abrir turno");
    }
    // Vende los productos indicados; efectivo con 5.00 de más para el cambio.
    std::string sell(const std::vector<std::pair<std::string, int>>& items, PaymentMethod method) {
        for (const auto& it : items) {
            auto a = cart->add_item(it.first, it.second, "");
            TEST_CHECK(a.is_success(), "Agregar al carrito: " + a.error_message());
        }
        auto total = cart->get_cart().value().total;
        auto r = sale->confirm_sale(method, method == PaymentMethod::Efectivo ? Money::from_cents(total.cents() + 500) : Money::zero());
        TEST_CHECK(r.is_success(), "confirm_sale: " + r.error_message());
        return r.value().folio;
    }
    SaleDetailDto detail(const std::string& folio) {
        auto d = refund->get_sale_for_refund(folio);
        TEST_CHECK(d.is_success(), "get_sale_for_refund: " + d.error_message());
        return d.value();
    }
    std::int64_t stock(const std::string& ing) { return num("SELECT current_stock FROM ingredients WHERE id = '" + ing + "';"); }
    RefundRequestDto req(const std::string& folio, const std::vector<RefundLineRequestDto>& lines, const std::string& reason = "Pedido equivocado", const std::string& key = "") {
        RefundRequestDto r;
        r.folio = folio; r.lines = lines; r.reason = reason; r.idempotency_key = key;
        return r;
    }
    void cleanup() {
        settings.reset(); refund.reset(); promo.reset(); report.reset(); sale.reset(); cart.reset(); shift.reset();
        inventory.reset(); catalog.reset(); account.reset();
        db.close();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

// ============================================================================================= REEMBOLSOS

void test_permission_and_messages(Env& e) {
    std::cout << "[TEST] B18: permiso pos.refund, motivo inválido y mensajes llanos..." << std::endl;
    e.owner();
    e.open_shift(50000);
    auto d = e.direct("Galleta B18", 1000, 20);
    const std::string folio = e.sell({{d.first, 2}}, PaymentMethod::Efectivo);
    const auto line = e.detail(folio).lines.at(0).sale_item_id;

    TEST_CHECK(e.num("SELECT COUNT(*) FROM role_permissions WHERE role_id = 'role-owner' AND permission_id = 'pos.refund';") == 1, "El Dueño tiene pos.refund");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM role_permissions WHERE role_id = 'role-cashier' AND permission_id = 'pos.refund';") == 0, "El Cajero no lo tiene");
    // Cajero sin permiso
    e.login("cajero_b18", kCashierPwd);
    auto no = e.refund->refund_sale(e.req(folio, {{line, 1, false}}));
    TEST_CHECK(no.error_code() == ErrorCode::Forbidden && no.error_message() == "No tienes permiso para hacer reembolsos.", "Sin permiso: " + no.error_message());
    TEST_CHECK(e.refund->get_sale_for_refund(folio).error_code() == ErrorCode::Forbidden, "Consulta sin permiso");
    e.account->logout();
    TEST_CHECK(e.refund->refund_sale(e.req(folio, {{line, 1, false}})).error_code() == ErrorCode::Forbidden, "Sin sesión");
    // Rol propio con el permiso, asignable desde Roles y permisos
    e.owner();
    auto role = e.account->create_role({"Encargado B18", "Reembolsos", {"pos.refund", "pos.sell", "shift.operate"}});
    TEST_CHECK(role.is_success(), "Crear rol: " + role.error_message());
    CreateUserDto enc{"encargado_b18", "Encargado B18", kCashierPwd, role.value().id, "enc@cafeteria-itch.edu.mx"};
    TEST_CHECK(e.account->create_user(enc).is_success(), "Alta del encargado");
    e.login("encargado_b18", kCashierPwd);
    TEST_CHECK(e.refund->get_sale_for_refund(folio).is_success(), "El rol propio con pos.refund consulta");
    e.owner();

    // Motivo inválido
    const std::string msg = "Escribe el motivo del reembolso (de 3 a 200 caracteres).";
    for (const std::string& bad : {std::string(""), std::string("   "), std::string("ab"), std::string(201, 'x'), std::string("con\nsalto")}) {
        auto r = e.refund->refund_sale(e.req(folio, {{line, 1, false}}, bad));
        TEST_CHECK(r.error_code() == ErrorCode::InvalidInput && r.error_message() == msg, "Motivo inválido '" + bad.substr(0, 10) + "': " + r.error_message());
    }
    TEST_CHECK(e.num("SELECT COUNT(*) FROM refunds;") == 0, "Los rechazos no escriben nada");
    TEST_CHECK(e.refund->refund_sale(e.req(folio, {})).error_message() == "Elige al menos una pieza para reembolsar.", "Sin líneas");
    TEST_CHECK(e.refund->refund_sale(e.req(folio, {{line, 0, false}})).error_message() == "La cantidad a reembolsar debe ser de al menos 1 pieza.", "Cantidad 0");
    TEST_CHECK(e.refund->refund_sale(e.req("NO-EXISTE", {{line, 1, false}})).error_code() == ErrorCode::NotFound, "Folio inexistente");
    auto other = e.refund->refund_sale(e.req(folio, {{"linea-ajena", 1, false}}));
    TEST_CHECK(other.error_code() == ErrorCode::InvalidInput && other.error_message().find("no pertenece a la venta") != std::string::npos, "Línea ajena: " + other.error_message());
}

void test_partial_total_excess_and_stock(Env& e) {
    std::cout << "[TEST] B18: reembolso parcial y total, exceso rechazado, sellado regresa existencia, auditoría..." << std::endl;
    e.owner();
    e.open_shift(50000);
    auto d = e.direct("Pan B18", 1500, 10);
    const std::string folio = e.sell({{d.first, 3}}, PaymentMethod::Efectivo);   // 4500, stock 7
    TEST_CHECK(e.stock(d.second) == 7, "Vendido: 7 en existencia");
    auto det = e.detail(folio);
    const std::string line = det.lines.at(0).sale_item_id;
    TEST_CHECK(det.can_refund && det.lines[0].quantity_refundable == 3 && det.lines[0].net_paid.cents() == 4500, "Detalle antes de reembolsar");

    auto r1 = e.refund->refund_sale(e.req(folio, {{line, 1, false}}));
    TEST_CHECK(r1.is_success(), "Parcial: " + r1.error_message());
    TEST_CHECK(r1.value().amount.cents() == 1500 && r1.value().refund_folio == folio + "-R1" && r1.value().lines.size() == 1, "Importe y folio del reembolso");
    TEST_CHECK(r1.value().user_name == "Dueño B18" && r1.value().reason == "Pedido equivocado" && r1.value().payment_method == PaymentMethod::Efectivo, "Datos del comprobante");
    TEST_CHECK(e.stock(d.second) == 8, "El sellado regresa a existencias");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM stock_movements WHERE refund_id = '" + r1.value().refund_id + "' AND movement_type = 'ADJUSTMENT' AND quantity_base_units = 1;") == 1, "Movimiento marcado con el reembolso");
    det = e.detail(folio);
    TEST_CHECK(det.lines[0].quantity_refunded == 1 && det.lines[0].quantity_refundable == 2 && det.refunded_total.cents() == 1500 && det.refunds.size() == 1, "Detalle tras el parcial");

    // Exceso: 3 piezas cuando solo quedan 2
    auto too_many = e.refund->refund_sale(e.req(folio, {{line, 3, false}}));
    TEST_CHECK(too_many.error_code() == ErrorCode::InvalidInput &&
               too_many.error_message() == "No se pueden reembolsar 3 pieza(s) de Pan B18: solo quedan 2 por reembolsar.", "Exceso: " + too_many.error_message());
    TEST_CHECK(e.stock(d.second) == 8 && e.num("SELECT COUNT(*) FROM refunds;") == 1, "El exceso no cambia nada");
    // El resto (total)
    auto r2 = e.refund->refund_sale(e.req(folio, {{line, 2, false}}));
    TEST_CHECK(r2.is_success() && r2.value().amount.cents() == 3000 && r2.value().refund_folio == folio + "-R2", "Resto: " + r2.error_message());
    TEST_CHECK(e.stock(d.second) == 10, "Todo regresó a existencias");
    // Ya no queda nada
    auto none = e.refund->refund_sale(e.req(folio, {{line, 1, false}}));
    TEST_CHECK(none.error_code() == ErrorCode::InvalidInput &&
               none.error_message() == "No se pueden reembolsar 1 pieza(s) de Pan B18: ya se reembolsó todo lo vendido.", "Nada por reembolsar: " + none.error_message());
    det = e.detail(folio);
    TEST_CHECK(!det.can_refund && det.cannot_refund_reason == "Esta venta ya se reembolsó por completo." && det.refunded_total.cents() == 4500, "Reembolsada por completo");
    // Concurrencia (a): dos reembolsos seguidos que juntos exceden lo vendido; el segundo se rechaza
    const std::string folio2 = e.sell({{d.first, 2}}, PaymentMethod::Efectivo);
    const auto line2 = e.detail(folio2).lines.at(0).sale_item_id;
    TEST_CHECK(e.refund->refund_sale(e.req(folio2, {{line2, 2, false}})).is_success(), "Primer reembolso de 2");
    auto second = e.refund->refund_sale(e.req(folio2, {{line2, 1, false}}));
    TEST_CHECK(!second.is_success(), "El segundo reembolso se rechaza");
    TEST_CHECK(e.num("SELECT COALESCE(SUM(quantity), 0) FROM refund_items WHERE sale_item_id = '" + line2 + "';") == 2, "Nunca se reembolsa más de lo vendido");
    // Auditoría
    TEST_CHECK(e.num("SELECT COUNT(*) FROM audit_events WHERE event_type = 'SALE_REFUNDED';") == 3, "Cada reembolso queda en la bitácora");
    const std::string audit = e.text("SELECT details FROM audit_events WHERE event_type = 'SALE_REFUNDED' ORDER BY created_at, rowid LIMIT 1;");
    TEST_CHECK(audit.find(folio) != std::string::npos && audit.find("1x Pan B18") != std::string::npos && audit.find("$15.00") != std::string::npos &&
               audit.find("EFECTIVO") != std::string::npos && audit.find("Pedido equivocado") != std::string::npos, "Auditoría con folio, líneas, importe, método y motivo: " + audit);
    TEST_CHECK(e.text("SELECT u.username FROM audit_events a JOIN users u ON u.id = a.user_id WHERE a.event_type = 'SALE_REFUNDED' LIMIT 1;") == "dueno_b18", "Auditoría con el actor");
    // list_refunds / get_refund
    auto list = e.refund->list_refunds(folio);
    TEST_CHECK(list.is_success() && list.value().size() == 2 && e.refund->get_refund(r1.value().refund_id).value().amount.cents() == 1500, "list_refunds y get_refund");
    // El ticket muestra los reembolsos
    auto ticket = e.sale->get_ticket(folio);
    TEST_CHECK(ticket.is_success() && ticket.value().refunds.size() == 2 && ticket.value().refunded_total.cents() == 4500, "El ticket trae los reembolsos");
    TEST_CHECK(ticket.value().to_formatted_text(40).find("Reembolso " + folio + "-R1:") != std::string::npos, "El texto del ticket muestra el reembolso");
    auto recent = e.sale->get_recent_tickets(10);
    bool found = false;
    for (const auto& t : recent.value()) if (t.folio == folio && t.refunded_total.cents() == 4500) found = true;
    TEST_CHECK(found, "La lista de tickets trae el reembolsado");
}

void test_idempotency_and_snapshot(Env& e) {
    std::cout << "[TEST] B18: doble envío idempotente; producto dado de baja o con otro precio..." << std::endl;
    e.owner();
    e.open_shift(50000);
    auto d = e.direct("Jugo B18", 2000, 10);
    const std::string folio = e.sell({{d.first, 2}}, PaymentMethod::Efectivo);   // 4000
    const auto line = e.detail(folio).lines.at(0).sale_item_id;
    auto a = e.refund->refund_sale(e.req(folio, {{line, 1, false}}, "Cliente se arrepintió", "clave-1"));
    auto b = e.refund->refund_sale(e.req(folio, {{line, 1, false}}, "Cliente se arrepintió", "clave-1"));
    TEST_CHECK(a.is_success() && b.is_success() && a.value().refund_id == b.value().refund_id, "La misma clave devuelve el mismo reembolso");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM refunds WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + folio + "');") == 1, "Un solo reembolso");
    TEST_CHECK(e.stock(d.second) == 9, "La existencia regresó una sola vez (10 - 2 + 1)");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM audit_events WHERE event_type = 'SALE_REFUNDED' AND details LIKE '%" + folio + "%';") == 1, "Una sola auditoría");
    const std::string other_folio = e.sell({{d.first, 1}}, PaymentMethod::Efectivo);
    const auto other_line = e.detail(other_folio).lines.at(0).sale_item_id;
    auto clash = e.refund->refund_sale(e.req(other_folio, {{other_line, 1, false}}, "Cliente se arrepintió", "clave-1"));
    TEST_CHECK(clash.error_code() == ErrorCode::Conflict, "La clave de otro reembolso no se reutiliza");

    // Producto dado de baja y con otro precio después de la venta: el reembolso usa lo guardado en la venta
    e.exec("UPDATE products SET is_active = 0, price_cents = 1 WHERE id = '" + d.first + "';");
    auto late = e.refund->refund_sale(e.req(folio, {{line, 1, false}}, "Cambio de pedido"));
    TEST_CHECK(late.is_success() && late.value().amount.cents() == 2000, "Producto de baja y precio cambiado: importe de la venta: " + late.error_message());
    TEST_CHECK(e.stock(d.second) == 9, "Y su existencia regresa (10 - 2 + 1 - 1 + 1)");
}

void test_proportional_discount(Env& e) {
    std::cout << "[TEST] B18: descuento de promoción en proporción, residuo en la última pieza, sin exceder el neto..." << std::endl;
    e.owner();
    e.open_shift(50000);
    auto d = e.direct("Dona B18", 1000, 10);
    const std::string folio = e.sell({{d.first, 3}}, PaymentMethod::Efectivo);   // subtotal 3000
    // Instantánea de B14: promoción de 100 en la línea (total y pago bajan 100)
    e.exec("UPDATE sale_items SET discount_cents = 100, promotion_name = 'Promo de prueba' WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + folio + "');");
    e.exec("UPDATE sales SET total_cents = total_cents - 100, discount_cents = 100 WHERE folio = '" + folio + "';");
    e.exec("UPDATE payment_records SET amount_cents = amount_cents - 100 WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + folio + "');");
    const auto line = e.detail(folio).lines.at(0).sale_item_id;   // neto 2900
    std::int64_t sum = 0;
    const std::int64_t expected[3] = {966, 967, 967};
    for (int i = 0; i < 3; ++i) {
        auto r = e.refund->refund_sale(e.req(folio, {{line, 1, false}}, "Pieza " + std::to_string(i + 1)));
        TEST_CHECK(r.is_success() && r.value().amount.cents() == expected[i], "Pieza " + std::to_string(i + 1) + ": " + std::to_string(r.value().amount.cents()));
        sum += r.value().amount.cents();
        TEST_CHECK(sum <= 2900, "Nunca se supera el neto de la línea");
    }
    TEST_CHECK(sum == 2900, "La suma de los reembolsos es exactamente el neto pagado");
    // Todo en un solo reembolso da lo mismo
    const std::string folio2 = e.sell({{d.first, 3}}, PaymentMethod::Efectivo);
    e.exec("UPDATE sale_items SET discount_cents = 100 WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + folio2 + "');");
    e.exec("UPDATE sales SET total_cents = total_cents - 100, discount_cents = 100 WHERE folio = '" + folio2 + "';");
    e.exec("UPDATE payment_records SET amount_cents = amount_cents - 100 WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + folio2 + "');");
    const auto line2 = e.detail(folio2).lines.at(0).sale_item_id;
    auto all = e.refund->refund_sale(e.req(folio2, {{line2, 3, false}}));
    TEST_CHECK(all.is_success() && all.value().amount.cents() == 2900, "Las tres piezas juntas devuelven el neto");
}

void test_prepared_combo_and_untracked(Env& e) {
    std::cout << "[TEST] B18: preparado sin preparar regresa ingredientes; ya preparado se registra como merma; combos; sin control..." << std::endl;
    e.owner();
    e.open_shift(50000);
    const std::string ing = e.ingredient("Harina B18", "MASS", "mg");
    e.buy(ing, 10000, "mg", 5000);
    const std::string prep = e.prepared("Pastel B18", 3000, ing, 100);
    const std::string folio = e.sell({{prep, 2}}, PaymentMethod::Efectivo);   // consume 200
    TEST_CHECK(e.stock(ing) == 9800, "Vendido: 9800");
    const auto line = e.detail(folio).lines.at(0).sale_item_id;
    TEST_CHECK(e.detail(folio).lines.at(0).is_prepared, "La línea sabe que es un preparado");

    // No preparado: regresan los ingredientes
    auto r1 = e.refund->refund_sale(e.req(folio, {{line, 1, false}}));
    TEST_CHECK(r1.is_success(), "Reembolso sin preparar: " + r1.error_message());
    TEST_CHECK(e.stock(ing) == 9900, "Regresan los ingredientes de la pieza");

    // Ya preparado: existencia y costo promedio exactamente iguales; queda una merma con el motivo
    auto snapshot = [&]() {
        return e.text("SELECT current_stock || '|' || stock_value_micro || '|' || last_avg_value_micro || '|' || last_avg_units FROM ingredients WHERE id = '" + ing + "';");
    };
    const std::string before = snapshot();
    auto r2 = e.refund->refund_sale(e.req(folio, {{line, 1, true}}, "Se cayó el pastel"));
    TEST_CHECK(r2.is_success(), "Reembolso ya preparado: " + r2.error_message());
    TEST_CHECK(snapshot() == before, "Existencia, valuación y costo promedio exactamente iguales: " + before + " vs " + snapshot());
    TEST_CHECK(e.num("SELECT COUNT(*) FROM stock_movements WHERE refund_id = '" + r2.value().refund_id + "' AND movement_type = 'WASTE' AND quantity_base_units = -100;") == 1, "Merma de 100");
    TEST_CHECK(e.text("SELECT reason FROM stock_movements WHERE refund_id = '" + r2.value().refund_id + "' AND movement_type = 'WASTE';") ==
               "Reembolso " + folio + ": Se cayó el pastel", "Motivo de la merma");
    TEST_CHECK(e.num("SELECT COALESCE(SUM(quantity_base_units), 0) FROM stock_movements WHERE refund_id = '" + r2.value().refund_id + "';") == 0, "Los dos movimientos se compensan");
    // Reportes: aparece como merma y no como ajuste manual
    const std::string today = e.text("SELECT substr(created_at, 1, 10) FROM sales WHERE folio = '" + folio + "';");
    auto losses = e.report->inventory_losses(today, today);
    TEST_CHECK(losses.is_success() && losses.value().rows.size() == 1, "Reporte de mermas: " + losses.error_message());
    const auto& row = losses.value().rows[0];
    TEST_CHECK(row.waste_quantity_base == 100 && row.adjustment_in_quantity_base == 0 && row.adjustment_out_quantity_base == 0, "Merma de 100; sin ajustes manuales");

    // Combo: directo + preparado
    auto dd = e.direct("Parte directa B18", 500, 10);
    const std::string cmb = e.combo("Combo B18", 3300, {{dd.first, 1}, {prep, 1}});
    const std::string cfolio = e.sell({{cmb, 2}}, PaymentMethod::Efectivo);   // 2 directas y 2 porciones (200 mg)
    const std::int64_t d_after_sale = e.stock(dd.second), i_after_sale = e.stock(ing);
    TEST_CHECK(d_after_sale == 8 && i_after_sale == 9700, "Combos vendidos");
    const auto cline = e.detail(cfolio).lines.at(0).sale_item_id;
    TEST_CHECK(e.refund->refund_sale(e.req(cfolio, {{cline, 1, false}})).is_success(), "Combo sin preparar");
    TEST_CHECK(e.stock(dd.second) == 9 && e.stock(ing) == 9800, "La parte directa y los ingredientes de la preparada regresan");
    TEST_CHECK(e.refund->refund_sale(e.req(cfolio, {{cline, 1, true}}, "Ya se preparó")).is_success(), "Combo ya preparado");
    TEST_CHECK(e.stock(dd.second) == 10, "La parte directa sellada regresa aunque el combo ya esté preparado");
    TEST_CHECK(e.stock(ing) == 9800, "La parte preparada ya consumida no regresa");

    // Sin control de existencia: no mueve inventario
    const std::string free = e.untracked("Servicio B18", 700);
    const std::string ffolio = e.sell({{free, 1}}, PaymentMethod::Efectivo);
    const auto moves_before = e.num("SELECT COUNT(*) FROM stock_movements;");
    const auto fline = e.detail(ffolio).lines.at(0).sale_item_id;
    TEST_CHECK(e.refund->refund_sale(e.req(ffolio, {{fline, 1, false}})).is_success(), "Reembolso sin control");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM stock_movements;") == moves_before, "Un producto sin control no mueve inventario");
}

void test_cash_rules_and_closeout(Env& e) {
    std::cout << "[TEST] B18: efectivo sin turno, mayor al esperado, tarjeta sin turno, corte con reembolsos..." << std::endl;
    e.owner();
    auto d = e.direct("Torta B18", 5000, 30);
    // Turno de venta que luego se cierra
    e.open_shift(0);
    const std::string cash_folio = e.sell({{d.first, 1}}, PaymentMethod::Efectivo);   // 5000
    const std::string card_folio = e.sell({{d.first, 1}}, PaymentMethod::Tarjeta);
    const auto cash_line = e.detail(cash_folio).lines.at(0).sale_item_id;
    const auto card_line = e.detail(card_folio).lines.at(0).sale_item_id;
    TEST_CHECK(e.shift->close_shift_with_count("", Money::from_cents(5000), "").is_success(), "Cerrar el turno");

    auto no_shift = e.refund->refund_sale(e.req(cash_folio, {{cash_line, 1, false}}));
    TEST_CHECK(no_shift.error_code() == ErrorCode::Conflict && no_shift.error_message() == "Para reembolsar en efectivo primero abre un turno.", "Efectivo sin turno: " + no_shift.error_message());
    // Tarjeta sin turno: se registra sin shift_id
    auto card_r = e.refund->refund_sale(e.req(card_folio, {{card_line, 1, false}}));
    TEST_CHECK(card_r.is_success(), "Tarjeta sin turno abierto se registra: " + card_r.error_message());
    TEST_CHECK(e.num("SELECT COUNT(*) FROM refunds WHERE id = '" + card_r.value().refund_id + "' AND shift_id IS NULL;") == 1, "Sin shift_id");

    // Efectivo mayor que el esperado: fondo 0 y una salida de caja dejan menos efectivo que lo reembolsado
    e.open_shift(1000);
    TEST_CHECK(e.shift->record_cash_movement(CashMovementKind::Outflow, Money::from_cents(600), "Compra de hielo").is_success(), "Salida de caja");
    auto big = e.refund->refund_sale(e.req(cash_folio, {{cash_line, 1, false}}));
    TEST_CHECK(big.error_code() == ErrorCode::InvalidInput &&
               big.error_message().rfind("No hay tanto efectivo en caja para este reembolso. En caja hay ", 0) == 0, "Efectivo insuficiente: " + big.error_message());
    TEST_CHECK(big.error_message().find("$4.00") != std::string::npos, "Dice cuánto hay en caja: " + big.error_message());
    TEST_CHECK(e.num("SELECT COUNT(*) FROM refunds WHERE payment_method = 'EFECTIVO';") == 0, "No se escribió nada");
    TEST_CHECK(e.shift->record_cash_movement(CashMovementKind::Inflow, Money::from_cents(5000), "Cambio para la caja").is_success(), "Entrada de caja");

    // Ahora sí alcanza: sale de la caja de este turno y entra en el corte
    auto ok = e.refund->refund_sale(e.req(cash_folio, {{cash_line, 1, false}}, "Cambio de pedido", "k-corte"));
    TEST_CHECK(ok.is_success(), "Efectivo alcanza: " + ok.error_message());
    auto pv = e.shift->preview_closeout();
    TEST_CHECK(pv.is_success(), "Vista previa");
    const auto& p = pv.value();
    TEST_CHECK(p.cash_refunds.cents() == 5000 && p.card_refunds.cents() == 0 && p.transfer_refunds.cents() == 0, "Reembolsos en efectivo del turno");
    TEST_CHECK(p.expected_cash.cents() == 1000 + 0 + 5000 - 600 - 5000, "El efectivo esperado resta los reembolsos en efectivo: " + std::to_string(p.expected_cash.cents()));
    const std::string shift_id = p.shift_id;
    auto closed = e.shift->close_shift_with_count(shift_id, Money::from_cents(400), "");
    TEST_CHECK(closed.is_success() && closed.value().difference_cents == 0, "Cierre sin diferencia: " + closed.error_message());
    TEST_CHECK(closed.value().cash_refunds.cents() == 5000 && closed.value().expected_cash.cents() == 400, "La instantánea muestra los reembolsos aparte");
    // La instantánea no cambia aunque cambien los datos después
    e.exec("DELETE FROM refunds WHERE id = '" + ok.value().refund_id + "';");
    auto again = e.shift->get_closeout(shift_id);
    TEST_CHECK(again.is_success() && again.value().cash_refunds.cents() == 5000 && again.value().expected_cash.cents() == 400, "La instantánea guardada no cambia");
    // Los cortes anteriores a la 022 tienen 0 (no cambian)
    TEST_CHECK(e.num("SELECT COUNT(*) FROM shifts WHERE closeout_saved = 1 AND snap_cash_refunds_cents = 0;") >= 1, "Las instantáneas anteriores quedan en 0");
}

void test_other_day_and_reports(Env& e) {
    std::cout << "[TEST] B18: venta de otro día rechazada; reportes netos con reembolsos y CSV con columnas al final..." << std::endl;
    e.owner();
    e.open_shift(100000);
    auto d = e.direct("Cafe B18", 2500, 40);
    const std::string old_folio = e.sell({{d.first, 1}}, PaymentMethod::Efectivo);
    const auto old_line = e.detail(old_folio).lines.at(0).sale_item_id;
    e.exec("UPDATE sales SET created_at = '2026-01-05 10:00:00' WHERE folio = '" + old_folio + "';");
    auto old = e.refund->refund_sale(e.req(old_folio, {{old_line, 1, false}}));
    TEST_CHECK(old.error_code() == ErrorCode::InvalidInput &&
               old.error_message() == "Solo se pueden reembolsar ventas del mismo día. Esta venta es del 2026-01-05.", "Otro día: " + old.error_message());
    auto det = e.detail(old_folio);
    TEST_CHECK(!det.can_refund && det.cannot_refund_reason == "Solo se pueden reembolsar ventas del mismo día. Esta venta es del 2026-01-05.", "can_refund falso con la razón");

    // Reportes del día de hoy: tres ventas (efectivo, tarjeta, transferencia) y reembolsos
    const std::string f1 = e.sell({{d.first, 4}}, PaymentMethod::Efectivo);       // 10000
    const std::string f2 = e.sell({{d.first, 2}}, PaymentMethod::Tarjeta);        // 5000
    const std::string f3 = e.sell({{d.first, 1}}, PaymentMethod::Transferencia);  // 2500
    const std::string today = e.text("SELECT substr(created_at, 1, 10) FROM sales WHERE folio = '" + f1 + "';");
    auto before = e.report->sales_by_day(today, today);
    TEST_CHECK(before.is_success() && before.value().totals.refunds.cents() == 0 && before.value().totals.net.cents() == 17500, "Antes de reembolsar");
    TEST_CHECK(e.refund->refund_sale(e.req(f1, {{e.detail(f1).lines.at(0).sale_item_id, 1, false}})).is_success(), "Reembolso efectivo");
    TEST_CHECK(e.refund->refund_sale(e.req(f2, {{e.detail(f2).lines.at(0).sale_item_id, 2, false}})).is_success(), "Reembolso tarjeta");
    TEST_CHECK(e.refund->refund_sale(e.req(f3, {{e.detail(f3).lines.at(0).sale_item_id, 1, false}})).is_success(), "Reembolso transferencia");

    auto day = e.report->sales_by_day(today, today);
    TEST_CHECK(day.is_success(), "Por día: " + day.error_message());
    const auto& t = day.value().totals;
    TEST_CHECK(t.net.cents() == 17500 && t.refunds.cents() == 2500 + 5000 + 2500 && t.net_after_refunds.cents() == 17500 - 10000, "Netas = bruto - descuentos - reembolsos");
    TEST_CHECK(t.payments_mismatch_cents == 0, "Los pagos siguen cuadrando");
    TEST_CHECK(day.value().rows.size() == 1 && day.value().rows[0].refunds.cents() == 10000 && day.value().rows[0].net_after_refunds.cents() == 7500, "La fila del día");
    auto prod = e.report->sales_by_product(today, today);
    TEST_CHECK(prod.is_success() && prod.value().rows.size() == 1, "Por producto");
    TEST_CHECK(prod.value().rows[0].pieces == 7 - 4 && prod.value().rows[0].pieces_refunded == 4 && prod.value().rows[0].refunds.cents() == 10000 &&
               prod.value().rows[0].net_after_refunds.cents() == 7500, "Unidades vendidas netas (vendidas - reembolsadas)");
    auto method = e.report->sales_by_payment_method(today, today);
    std::int64_t refunds_by_method = 0;
    for (const auto& r : method.value().rows) refunds_by_method += r.refunds.cents();
    TEST_CHECK(method.is_success() && refunds_by_method == 10000, "Reembolsos por método");
    auto daily = e.report->get_daily_sales_summary(today);
    TEST_CHECK(daily.is_success() && daily.value().refunds_amount.cents() == 10000 && daily.value().payments_mismatch_cents == 0, "Resumen diario");
    // CSV: columnas nuevas solo al final
    const fs::path f = e.root / "dia.csv";
    auto x = e.report->export_report_csv(ReportKind::SalesByDay, today, today, f.u8string());
    TEST_CHECK(x.is_success(), "Exportar por día: " + x.error_message());
    std::string content;
    { std::ifstream in(f, std::ios::binary); std::ostringstream ss; ss << in.rdbuf(); content = ss.str(); }
    const std::string header = "Fecha,Ventas,Importe bruto,Descuento,Importe neto,Efectivo,Tarjeta,Transferencia";
    TEST_CHECK(content.find(header + ",Cargo por tarjeta,Reembolsos,Neto después de reembolsos\r\n") != std::string::npos, "Las columnas anteriores conservan su orden y las nuevas van al final");
}

// ============================================================================================= CARGO POR TARJETA

void test_card_surcharge(Env& e) {
    std::cout << "[TEST] B18: cargo por tarjeta: 0 por omisión, porcentaje y fijo, redondeo, solo tarjeta, instantánea estable..." << std::endl;
    e.owner();
    e.open_shift(50000);
    auto d = e.direct("Torta cargo B18", 1000, 100);

    // Por omisión: desactivado
    auto cfg = e.settings->get_card_surcharge_config();
    TEST_CHECK(cfg.is_success() && cfg.value().percent_hundredths == 0 && cfg.value().fixed.cents() == 0, "0 % y $0 por omisión");
    TEST_CHECK(e.cart->add_item(d.first, 1, "").is_success(), "Carrito");
    auto pv0 = e.sale->preview_checkout(PaymentMethod::Tarjeta);
    TEST_CHECK(pv0.is_success() && pv0.value().card_surcharge.cents() == 0 && pv0.value().charged_total.cents() == 1000, "Vista previa sin cargo");
    auto sale0 = e.sale->confirm_sale(PaymentMethod::Tarjeta, Money::zero());
    TEST_CHECK(sale0.is_success() && sale0.value().total.cents() == 1000 && sale0.value().card_surcharge.cents() == 0, "Sin cargo: nada cambia");

    // Configuración: permisos y rangos
    e.login("cajero_b18", kCashierPwd);
    TEST_CHECK(e.settings->get_card_surcharge_config().is_success(), "El cajero puede leer la configuración");
    auto denied = e.settings->set_card_surcharge_config(150, Money::from_cents(300));
    TEST_CHECK(denied.error_code() == ErrorCode::Forbidden && denied.error_message() == "No tienes permiso para cambiar el cargo por tarjeta.", "El cajero no la cambia");
    e.owner();
    TEST_CHECK(e.settings->set_card_surcharge_config(2001, Money::zero()).error_code() == ErrorCode::InvalidInput, "Porcentaje mayor a 20.00 %");
    TEST_CHECK(e.settings->set_card_surcharge_config(-1, Money::zero()).error_code() == ErrorCode::InvalidInput, "Porcentaje negativo");
    TEST_CHECK(e.settings->set_card_surcharge_config(0, Money::from_cents(10001)).error_code() == ErrorCode::InvalidInput, "Fijo mayor a $100");
    TEST_CHECK(e.settings->set_card_surcharge_config(0, Money::from_cents(-1)).error_code() == ErrorCode::InvalidInput, "Fijo negativo");
    TEST_CHECK(e.settings->set_card_surcharge_config(2000, Money::from_cents(10000)).is_success(), "Los máximos se aceptan");
    TEST_CHECK(e.settings->set_card_surcharge_config(150, Money::from_cents(300)).is_success(), "1.50 % + $3.00");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM audit_events WHERE event_type = 'CARD_SURCHARGE_CONFIGURED';") == 2, "Cada cambio válido queda en la bitácora (los rechazados no)");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM payment_settings;") == 1, "Una sola fila de configuración");

    // Redondeo (función pura): mitad hacia arriba, solo enteros
    std::int64_t out = 0;
    TEST_CHECK(app::domain::card_surcharge_cents(1000, 5, 0, out) && out == 1, "0.5 centavos sube a 1");
    TEST_CHECK(app::domain::card_surcharge_cents(1033, 150, 0, out) && out == 15, "15.495 baja a 15");
    TEST_CHECK(app::domain::card_surcharge_cents(1050, 150, 0, out) && out == 16, "15.75 sube a 16");
    TEST_CHECK(app::domain::card_surcharge_cents(1000, 150, 300, out) && out == 315, "porcentaje + fijo");
    TEST_CHECK(app::domain::card_surcharge_cents(0, 150, 300, out) && out == 300, "venta de $0: solo el fijo");
    TEST_CHECK(app::domain::card_surcharge_cents(INT64_MAX, 20000, 0, out) == false, "Desbordamiento detectado");
    TEST_CHECK(app::domain::card_surcharge_cents(-1, 150, 0, out) == false, "Entradas negativas rechazadas");

    // Vista previa por método: solo Tarjeta lleva el cargo
    TEST_CHECK(e.cart->add_item(d.first, 1, "").is_success(), "Carrito");
    auto pvc = e.sale->preview_checkout(PaymentMethod::Tarjeta);
    TEST_CHECK(pvc.is_success() && pvc.value().products_total.cents() == 1000 && pvc.value().card_surcharge.cents() == 315 &&
               pvc.value().charged_total.cents() == 1315 && pvc.value().percent_hundredths == 150 && pvc.value().fixed.cents() == 300, "Vista previa con tarjeta");
    TEST_CHECK(e.sale->preview_checkout(PaymentMethod::Transferencia).value().card_surcharge.cents() == 0, "Transferencia sin cargo");
    TEST_CHECK(e.sale->preview_checkout(PaymentMethod::Efectivo).value().charged_total.cents() == 1000, "Efectivo sin cargo");

    // Venta con tarjeta
    auto cs = e.sale->confirm_sale(PaymentMethod::Tarjeta, Money::zero());
    TEST_CHECK(cs.is_success(), "Venta con tarjeta: " + cs.error_message());
    TEST_CHECK(cs.value().total.cents() == 1315 && cs.value().products_total.cents() == 1000 && cs.value().card_surcharge.cents() == 315 && cs.value().cash_received.cents() == 1315, "Total cobrado con cargo");
    const std::string folio = cs.value().folio;
    TEST_CHECK(e.num("SELECT total_cents FROM sales WHERE folio = '" + folio + "';") == 1000, "total_cents sigue siendo el neto de productos");
    TEST_CHECK(e.num("SELECT amount_cents FROM payment_records WHERE sale_id = (SELECT id FROM sales WHERE folio = '" + folio + "');") == 1315, "El pago registrado incluye el cargo");
    TEST_CHECK(e.num("SELECT card_surcharge_cents FROM sales WHERE folio = '" + folio + "';") == 315 &&
               e.num("SELECT card_surcharge_bp FROM sales WHERE folio = '" + folio + "';") == 150 &&
               e.num("SELECT card_surcharge_fixed_cents FROM sales WHERE folio = '" + folio + "';") == 300, "Instantánea: cargo, porcentaje y fijo usados");
    // Transferencia y efectivo: sin cargo
    TEST_CHECK(e.cart->add_item(d.first, 1, "").is_success(), "Carrito");
    auto ts = e.sale->confirm_sale(PaymentMethod::Transferencia, Money::zero());
    TEST_CHECK(ts.is_success() && ts.value().total.cents() == 1000 && ts.value().card_surcharge.cents() == 0, "Transferencia: sin cargo");
    TEST_CHECK(e.cart->add_item(d.first, 1, "").is_success(), "Carrito");
    auto es = e.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(1000));
    TEST_CHECK(es.is_success() && es.value().total.cents() == 1000 && es.value().change.cents() == 0, "Efectivo: sin cargo");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM sales WHERE card_surcharge_cents > 0;") == 1, "Solo la venta con tarjeta lleva cargo");

    // Ticket: línea aparte
    auto ticket = e.sale->get_ticket(folio);
    TEST_CHECK(ticket.is_success() && ticket.value().total.cents() == 1315 && ticket.value().products_total.cents() == 1000 &&
               ticket.value().card_surcharge.cents() == 315 && ticket.value().subtotal.cents() == 1000, "Ticket con cargo");
    const std::string text = ticket.value().to_formatted_text(40);
    TEST_CHECK(text.find("Cargo por pago con tarjeta:") != std::string::npos && text.find("$3.15") != std::string::npos && text.find("TOTAL:") != std::string::npos, "Línea «Cargo por pago con tarjeta»");
    const std::string plain = e.sale->get_ticket(ts.value().folio).value().to_formatted_text(40);
    TEST_CHECK(plain.find("Cargo por pago con tarjeta") == std::string::npos, "Sin cargo no hay línea");
    auto by_folio = e.sale->get_sale_by_folio(folio);
    TEST_CHECK(by_folio.is_success() && by_folio.value().total.cents() == 1315 && by_folio.value().card_surcharge.cents() == 315, "get_sale_by_folio");

    // La instantánea no cambia al editar la configuración
    TEST_CHECK(e.settings->set_card_surcharge_config(500, Money::from_cents(999)).is_success(), "Nueva configuración");
    auto ticket2 = e.sale->get_ticket(folio);
    TEST_CHECK(ticket2.value().card_surcharge.cents() == 315 && ticket2.value().total.cents() == 1315, "La venta pasada no cambia");
    TEST_CHECK(e.num("SELECT card_surcharge_bp FROM sales WHERE folio = '" + folio + "';") == 150, "Instantánea intacta");

    // El cargo no se devuelve en el reembolso
    const auto line = e.detail(folio).lines.at(0).sale_item_id;
    auto rf = e.refund->refund_sale(e.req(folio, {{line, 1, false}}));
    TEST_CHECK(rf.is_success() && rf.value().amount.cents() == 1000 && rf.value().card_surcharge_not_refunded.cents() == 315, "Se devuelve 1000; el cargo de 315 no");
    auto detail = e.detail(folio);
    TEST_CHECK(detail.charged_total.cents() == 1315 && detail.refunded_total.cents() == 1000 && detail.card_surcharge.cents() == 315, "Detalle de la venta");
}

void test_card_surcharge_reports_and_closeout(Env& e) {
    std::cout << "[TEST] B18: cargos por tarjeta en reportes y corte (aparte, sin inflar las ventas)..." << std::endl;
    e.owner();
    e.open_shift(50000);
    TEST_CHECK(e.settings->set_card_surcharge_config(250, Money::from_cents(100)).is_success(), "2.50 % + $1.00");
    auto d = e.direct("Producto cargo B18", 2000, 100);
    const std::string today = app::domain::now().local.substr(0, 10);
    const auto cards_before = e.report->sales_by_day(today, today).value().totals.card_surcharges.cents();
    const auto net_before = e.report->sales_by_day(today, today).value().totals.net.cents();
    const auto shift_before = e.shift->preview_closeout().value();
    const auto expected_before = shift_before.expected_cash.cents();

    TEST_CHECK(e.cart->add_item(d.first, 2, "").is_success(), "Carrito");
    auto s = e.sale->confirm_sale(PaymentMethod::Tarjeta, Money::zero());   // 4000 + 100 + 100 = 4200
    TEST_CHECK(s.is_success() && s.value().card_surcharge.cents() == 200 && s.value().total.cents() == 4200, "Venta con 2.50 % + $1.00: " + std::to_string(s.value().card_surcharge.cents()));

    auto day = e.report->sales_by_day(today, today);
    TEST_CHECK(day.is_success(), "Por día");
    TEST_CHECK(day.value().totals.card_surcharges.cents() == cards_before + 200, "Cargos aparte");
    TEST_CHECK(day.value().totals.net.cents() == net_before + 4000, "Las ventas de productos no se inflan con el cargo");
    TEST_CHECK(day.value().totals.payments_mismatch_cents == 0, "Neto + cargos = pagos");
    auto daily = e.report->get_daily_sales_summary(today);
    TEST_CHECK(daily.is_success() && daily.value().payments_mismatch_cents == 0 && daily.value().card_surcharge_amount.cents() == cards_before + 200, "Resumen diario con cargos");
    auto cashier = e.report->sales_by_cashier(today, today);
    std::int64_t cash_sur = 0;
    for (const auto& r : cashier.value().rows) cash_sur += r.card_surcharges.cents();
    TEST_CHECK(cashier.is_success() && cash_sur == cards_before + 200, "Por cajero con cargos");

    auto pv = e.shift->preview_closeout();
    TEST_CHECK(pv.is_success() && pv.value().card_surcharges.cents() == shift_before.card_surcharges.cents() + 200, "El corte muestra los cargos aparte");
    TEST_CHECK(pv.value().payments_mismatch_cents == 0 && pv.value().expected_cash.cents() == expected_before, "El efectivo esperado no cambia con una venta con tarjeta");
    auto closed = e.shift->close_shift_with_count("", pv.value().expected_cash, "");
    TEST_CHECK(closed.is_success() && closed.value().card_surcharges.cents() == pv.value().card_surcharges.cents(), "La instantánea guarda los cargos");
}

void test_mock_and_migration(Env& e) {
    std::cout << "[TEST] B18: mock coherente y migración 022 repetible..." << std::endl;
    MockPaymentSettingsService ms;
    TEST_CHECK(ms.get_card_surcharge_config().value().percent_hundredths == 0, "Mock: 0 % por omisión");
    TEST_CHECK(ms.set_card_surcharge_config(2001, Money::zero()).error_code() == ErrorCode::InvalidInput, "Mock: rango del porcentaje");
    TEST_CHECK(ms.set_card_surcharge_config(150, Money::from_cents(10001)).error_code() == ErrorCode::InvalidInput, "Mock: rango del fijo");
    TEST_CHECK(ms.set_card_surcharge_config(150, Money::from_cents(300)).is_success(), "Mock: configurar");
    MockSaleService msale;
    msale.set_card_surcharge(150, 300);
    auto pv = msale.preview_checkout(PaymentMethod::Tarjeta);
    TEST_CHECK(pv.is_success() && pv.value().card_surcharge.cents() == 210 + 300 && pv.value().charged_total.cents() == 14000 + 510, "Mock: vista previa 1.50 % de $140.00 + $3.00");
    TEST_CHECK(msale.preview_checkout(PaymentMethod::Efectivo).value().card_surcharge.cents() == 0, "Mock: efectivo sin cargo");
    MockRefundService mr;
    auto det = mr.get_sale_for_refund("V-20260924-1001");
    TEST_CHECK(det.is_success() && det.value().lines.size() == 2, "Mock: detalle");
    RefundRequestDto rq;
    rq.folio = "V-20260924-1001"; rq.reason = "Prueba"; rq.lines = {{"line-mock-1", 1, false}};
    auto r1 = mr.refund_sale(rq);
    TEST_CHECK(r1.is_success() && r1.value().amount.cents() == 5500, "Mock: reembolso de una pieza");
    rq.lines = {{"line-mock-1", 2, false}};
    auto bad = mr.refund_sale(rq);
    TEST_CHECK(bad.error_code() == ErrorCode::InvalidInput && bad.error_message().find("solo quedan 1 por reembolsar") != std::string::npos, "Mock: exceso");
    rq.reason = "ab";
    TEST_CHECK(mr.refund_sale(rq).error_message() == "Escribe el motivo del reembolso (de 3 a 200 caracteres).", "Mock: motivo");
    // Migración 022 repetible
    const auto versions = e.num("SELECT COUNT(*) FROM schema_migrations;");
    MigrationRunner runner(e.db);
    TEST_CHECK(runner.run_pending_migrations_from_dir(fs::u8path(SOURCE_MIGRATIONS_DIR)).is_success() &&
               e.num("SELECT COUNT(*) FROM schema_migrations;") == versions, "Segunda pasada de migraciones: nada que aplicar");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM permissions WHERE id = 'pos.refund';") == 1, "Permiso único");
    TEST_CHECK(e.text("SELECT title FROM permissions WHERE id = 'pos.refund';") == "Hacer reembolsos", "Título del permiso");
    TEST_CHECK(e.num("SELECT COUNT(*) FROM pragma_table_info('sales') WHERE name IN ('card_surcharge_cents','card_surcharge_bp','card_surcharge_fixed_cents');") == 3, "Columnas de sales");
}

} // namespace

int main(int argc, char** argv) {
    const std::string suite = argc > 1 ? argv[1] : "";
    if (suite != "refund" && suite != "surcharge") {
        std::cerr << "Uso: test_b18 refund | surcharge" << std::endl;
        return 2;
    }
    std::cout << "=== PRUEBAS B18 (" << suite << ") ===" << std::endl;
    auto run = [](void (*test)(Env&)) {
        Env env;   // base nueva para cada prueba: los totales de un día no se mezclan
        test(env);
        env.cleanup();
    };
    if (suite == "refund") {
        run(test_permission_and_messages);
        run(test_partial_total_excess_and_stock);
        run(test_idempotency_and_snapshot);
        run(test_proportional_discount);
        run(test_prepared_combo_and_untracked);
        run(test_cash_rules_and_closeout);
        run(test_other_day_and_reports);
    } else {
        run(test_card_surcharge);
        run(test_card_surcharge_reports_and_closeout);
        run(test_mock_and_migration);
    }
    std::cout << "\n>>> TODAS LAS PRUEBAS B18 (" << suite << ") PASARON CON EXITO! <<<\n" << std::endl;
    return 0;
}
