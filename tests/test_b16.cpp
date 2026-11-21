// Pruebas B16: estado de existencia (stock_status) y último movimiento (last_movement_at) de los productos para la
// pantalla Existencias. Sin GUI, sin red y sin escribir ningún .exe.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>
#include "app/storage/migration_runner.hpp"
#include "app/storage/sqlite_database.hpp"
#include "app/services/sqlite_account_service.hpp"
#include "app/services/sqlite_catalog_service.hpp"
#include "app/services/sqlite_inventory_service.hpp"
#include "app/services/sqlite_shift_service.hpp"
#include "app/services/sqlite_cart_service.hpp"
#include "app/services/sqlite_sale_service.hpp"
#include "app/services/sqlite_promotion_service.hpp"
#include "app/services/mock_services.hpp"

#define TEST_CHECK(condition, message) \
    do { \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA B16] " << (message) \
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

struct Env {
    fs::path root;
    SqliteDatabase db;
    std::shared_ptr<SqliteAccountService> account;
    std::shared_ptr<SqliteCatalogService> catalog;
    std::shared_ptr<SqliteInventoryService> inventory;
    std::shared_ptr<SqliteShiftService> shift;
    std::shared_ptr<SqliteCartService> cart;
    std::shared_ptr<SqliteSaleService> sale;
    std::shared_ptr<SqlitePromotionService> promo;
    int seq{0};

    Env() {
        std::random_device rd;
        std::mt19937_64 gen(rd());
        root = fs::temp_directory_path() / fs::u8path("itch_b16_" + std::to_string(gen()));
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
        promo = std::make_shared<SqlitePromotionService>(db, account, catalog);
        TEST_CHECK(account->create_initial_owner("dueno_b16", "Dueño B16", kOwnerPwd, "dueno@cafeteria-itch.edu.mx").is_success(), "Alta del Dueño");
        TEST_CHECK(account->login("dueno_b16", kOwnerPwd).is_success(), "Login");
        TEST_CHECK(shift->open_shift("reg-01", Money::from_cents(50000)).is_success(), "Abrir turno");
    }

    void exec(const std::string& sql) {
        auto r = db.execute(sql);
        TEST_CHECK(r.is_success(), "SQL: " + sql + " -> " + r.error_message());
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

    std::string ingredient(const std::string& name, const std::string& dim, const std::string& unit, std::int64_t threshold) {
        auto r = inventory->create_ingredient({name, dim, unit, threshold});
        TEST_CHECK(r.is_success(), "Ingrediente " + name + ": " + r.error_message());
        return r.value().id;
    }
    void buy(const std::string& ing, std::int64_t qty, const std::string& unit) {
        TEST_CHECK(inventory->register_purchase_entry(ing, qty, unit, Money::from_cents(qty * 10)).is_success(), "Compra");
    }
    void waste(const std::string& ing, std::int64_t qty, const std::string& unit) {
        auto r = inventory->register_waste(ing, qty, unit, "Prueba de merma");
        TEST_CHECK(r.is_success(), "Merma: " + r.error_message());
    }
    // Directo con ingrediente por piezas enlazado (por omisión). Devuelve {producto, ingrediente}.
    std::pair<std::string, std::string> direct(const std::string& name, std::int64_t threshold) {
        CreateProductDto p;
        p.sku = next("D"); p.name = name; p.category = "Pruebas"; p.price = Money::from_cents(1000);
        auto r = catalog->create_product(p);
        TEST_CHECK(r.is_success() && r.value().stock_tracked, "Directo: " + r.error_message());
        const std::string ing = catalog->get_recipe(r.value().id).value().items.at(0).ingredient_id;
        TEST_CHECK(inventory->update_low_stock_threshold(ing, threshold).is_success(), "Umbral");
        return {r.value().id, ing};
    }
    std::string untracked(const std::string& name) {
        CreateProductDto p;
        p.sku = next("U"); p.name = name; p.category = "Pruebas"; p.price = Money::from_cents(500); p.track_stock = false;
        auto r = catalog->create_product(p);
        TEST_CHECK(r.is_success(), "Sin control: " + r.error_message());
        return r.value().id;
    }
    std::string prepared(const std::string& name, const std::vector<RecipeItemDto>& items) {
        CreateProductDto p;
        p.sku = next("P"); p.name = name; p.category = "Pruebas"; p.price = Money::from_cents(3000); p.is_prepared = true;
        auto r = catalog->create_product(p);
        TEST_CHECK(r.is_success(), "Preparado: " + r.error_message());
        TEST_CHECK(catalog->set_recipe(r.value().id, items).is_success(), "Receta");
        return r.value().id;
    }
    std::string combo(const std::string& name, const std::vector<std::pair<std::string, int>>& parts) {
        CreateComboDto c;
        c.sku = next("C"); c.name = name; c.category = "Pruebas"; c.price = Money::from_cents(4000);
        for (const auto& p : parts) c.components.push_back({p.first, "", p.second});
        auto r = promo->create_combo(c);
        TEST_CHECK(r.is_success(), "Combo: " + r.error_message());
        return r.value().product_id;
    }
    ProductDto get(const std::string& id) {
        auto r = catalog->get_product_by_id(id);
        TEST_CHECK(r.is_success(), "get_product_by_id");
        return r.value();
    }
    // Pone todos los movimientos de un ingrediente en una hora vieja, para ver el cambio de una operación nueva.
    void age_movements(const std::string& ing, const std::string& when) {
        exec("UPDATE stock_movements SET created_at = '" + when + "' WHERE ingredient_id = '" + ing + "';");
    }
    std::string last_of(const std::string& ing) {
        return text("SELECT MAX(created_at) FROM stock_movements WHERE ingredient_id = '" + ing + "';");
    }
    void cleanup() {
        promo.reset(); sale.reset(); cart.reset(); shift.reset(); inventory.reset(); catalog.reset(); account.reset();
        db.close();
        std::error_code ec;
        fs::remove_all(root, ec);
    }
};

void check_same_three(Env& e, const std::string& id) {
    const ProductDto a = e.get(id);
    auto sku = e.catalog->get_product_by_sku_or_barcode(a.sku);
    TEST_CHECK(sku.is_success(), "get_product_by_sku_or_barcode");
    auto list = e.catalog->search_products(a.sku, std::nullopt, 0, 50, true);
    TEST_CHECK(list.is_success(), "search_products por SKU");
    const ProductDto* found = nullptr;
    for (const auto& x : list.value()) if (x.sku == a.sku) found = &x;
    TEST_CHECK(found != nullptr, "search_products encuentra el producto");
    const ProductDto* both[] = {&sku.value(), found};
    for (const ProductDto* b : both) {
        TEST_CHECK(b->id == a.id && b->stock_status == a.stock_status && b->last_movement_at == a.last_movement_at &&
                   b->stock_units == a.stock_units && b->stock_tracked == a.stock_tracked,
                   "Las tres lecturas devuelven lo mismo para " + a.name);
    }
}

void test_direct(Env& e) {
    std::cout << "[TEST] B16: producto directo con ingrediente enlazado: OUT, OK, LOW y umbral en cero..." << std::endl;
    auto d = e.direct("Directo B16", 5);
    ProductDto p = e.get(d.first);
    TEST_CHECK(p.stock_tracked && p.stock_status == "OUT" && p.last_movement_at.empty() && p.stock_units == 0, "Sin existencia ni movimientos: OUT y vacío");
    e.buy(d.second, 10, "pz");
    p = e.get(d.first);
    TEST_CHECK(p.stock_status == "OK" && p.stock_units == 10 && !p.last_movement_at.empty(), "10 con umbral 5: OK");
    e.waste(d.second, 5, "pz");
    TEST_CHECK(e.get(d.first).stock_status == "LOW", "5 con umbral 5: LOW (igual al umbral)");
    e.waste(d.second, 4, "pz");
    TEST_CHECK(e.get(d.first).stock_status == "LOW", "1 con umbral 5: LOW");
    e.waste(d.second, 1, "pz");
    p = e.get(d.first);
    TEST_CHECK(p.stock_status == "OUT" && p.stock_units == 0, "0: OUT");
    // El estado es el de su ingrediente (mismo umbral que el Dueño fija en Ingredientes)
    auto ing = e.inventory->get_ingredient(d.second).value();
    TEST_CHECK(ing.stock_status == p.stock_status && ing.last_movement_at == p.last_movement_at, "Igual que el ingrediente: estado y último movimiento");
    // Umbral en cero: nunca LOW
    TEST_CHECK(e.inventory->update_low_stock_threshold(d.second, 0).is_success(), "Umbral 0");
    e.buy(d.second, 1, "pz");
    TEST_CHECK(e.get(d.first).stock_status == "OK", "1 pieza sin umbral: OK, nunca LOW");
    check_same_three(e, d.first);
}

void test_prepared(Env& e) {
    std::cout << "[TEST] B16: preparado: OUT si no alcanza una porción, LOW por cualquier ingrediente, último movimiento..." << std::endl;
    const std::string a = e.ingredient("Harina B16", "MASS", "mg", 500);
    const std::string b = e.ingredient("Leche B16", "VOLUME", "ml", 0);
    const std::string c = e.ingredient("Sal B16", "MASS", "mg", 3000);   // ingrediente con umbral alto, no entra a la receta aún
    const std::string prep = e.prepared("Preparado B16", {{a, "", 100, "mg"}, {b, "", 50, "ml"}});
    ProductDto p = e.get(prep);
    TEST_CHECK(p.stock_tracked && p.stock_status == "OUT" && p.stock_units == 0 && p.last_movement_at.empty(), "Sin existencias: OUT y sin movimientos");
    e.buy(a, 1000, "mg");
    TEST_CHECK(e.get(prep).stock_status == "OUT", "Falta un ingrediente: OUT");
    e.buy(b, 1000, "ml");
    p = e.get(prep);
    TEST_CHECK(p.stock_status == "OK" && p.stock_units == 10, "Alcanza para 10 y ninguno bajo su umbral: OK");
    e.waste(a, 600, "mg");   // 400 <= 500: poco
    p = e.get(prep);
    TEST_CHECK(p.stock_status == "LOW" && p.stock_units == 4, "Harina en poco: LOW");
    e.buy(a, 1000, "mg");    // 1400 > 500 otra vez
    TEST_CHECK(e.get(prep).stock_status == "OK", "Vuelve a OK");
    TEST_CHECK(e.inventory->update_low_stock_threshold(b, 5000).is_success(), "Umbral de la leche");
    TEST_CHECK(e.get(prep).stock_status == "LOW", "LOW por otro ingrediente (la leche)");
    TEST_CHECK(e.inventory->update_low_stock_threshold(b, 0).is_success(), "Umbral 0");
    e.waste(a, 1350, "mg");  // 50 de harina: no alcanza para una porción (100)
    p = e.get(prep);
    TEST_CHECK(p.stock_status == "OUT" && p.stock_units == 0, "No alcanza una porción: OUT aunque haya existencia");
    // Último movimiento: el más reciente entre sus ingredientes
    e.age_movements(a, "2026-01-01 10:00:00");
    e.age_movements(b, "2026-02-02 08:00:00");
    TEST_CHECK(e.get(prep).last_movement_at == "2026-02-02 08:00:00", "El más reciente entre los ingredientes (leche)");
    e.age_movements(a, "2026-03-03 09:00:00");
    TEST_CHECK(e.get(prep).last_movement_at == "2026-03-03 09:00:00", "Ahora el de la harina");
    (void)c;
    check_same_three(e, prep);
}

void test_combo_and_untracked(Env& e) {
    std::cout << "[TEST] B16: combo (OUT, LOW, OK, una parte sin control) y producto sin control..." << std::endl;
    auto d = e.direct("Parte directa B16", 5);
    const std::string ia = e.ingredient("Harina combo B16", "MASS", "mg", 0);
    const std::string prep = e.prepared("Parte preparada B16", {{ia, "", 100, "mg"}});
    const std::string cmb = e.combo("Combo B16", {{d.first, 1}, {prep, 2}});
    ProductDto p = e.get(cmb);
    TEST_CHECK(p.is_combo && p.stock_tracked && p.stock_status == "OUT" && p.stock_units == 0, "Sin existencias: OUT");
    e.buy(d.second, 20, "pz");
    e.buy(ia, 1000, "mg");   // 10 porciones -> 5 combos; directo 20
    p = e.get(cmb);
    TEST_CHECK(p.stock_status == "OK" && p.stock_units == 5, "Todas las partes bien: OK, 5 combos");
    e.waste(d.second, 15, "pz");   // 5 piezas, umbral 5: LOW
    p = e.get(cmb);
    TEST_CHECK(p.stock_status == "LOW" && p.stock_units == 5, "Una parte en poco: LOW");
    e.waste(d.second, 5, "pz");
    TEST_CHECK(e.get(cmb).stock_status == "OUT" && e.get(cmb).stock_units == 0, "Una parte agotada: OUT");
    e.buy(d.second, 20, "pz");
    e.age_movements(d.second, "2026-04-01 12:00:00");
    e.age_movements(ia, "2026-05-05 07:30:00");
    TEST_CHECK(e.get(cmb).last_movement_at == "2026-05-05 07:30:00", "El más reciente entre las partes");
    check_same_three(e, cmb);

    // Decisión del orquestador (2026-11-18): un combo con una parte sin control NO cambia el significado de
    // stock_tracked: sigue falso y, por la regla 4, sin estado ni último movimiento. La regla del combo (OUT, LOW, OK)
    // aplica solo a combos con todas sus partes controladas. La parte sin control no entra en la cuenta de stock_units.
    const std::string free_part = e.untracked("Parte sin control B16");
    const std::string mixed = e.combo("Combo mixto B16", {{d.first, 1}, {free_part, 1}});
    p = e.get(mixed);
    TEST_CHECK(!p.stock_tracked && p.stock_status.empty() && p.last_movement_at.empty(), "Combo con una parte sin control: sin estado ni movimiento");
    TEST_CHECK(p.stock_units == 20, "stock_units cuenta solo las partes con control");
    check_same_three(e, mixed);

    // Producto sin control
    const std::string u = e.untracked("Sin control B16");
    p = e.get(u);
    TEST_CHECK(!p.stock_tracked && p.stock_status.empty() && p.last_movement_at.empty() && p.stock_units == 0, "Sin control: ambos vacíos");
    check_same_three(e, u);
    // Un preparado sin receta tampoco controla
    CreateProductDto np;
    np.sku = e.next("N"); np.name = "Preparado sin receta B16"; np.category = "Pruebas"; np.price = Money::from_cents(100); np.is_prepared = true;
    auto nr = e.catalog->create_product(np);
    TEST_CHECK(nr.is_success(), "Preparado sin receta");
    p = e.get(nr.value().id);
    TEST_CHECK(!p.stock_tracked && p.stock_status.empty() && p.last_movement_at.empty(), "Preparado sin receta: sin control");
}

void test_last_movement_changes(Env& e) {
    std::cout << "[TEST] B16: last_movement_at cambia tras una compra, una merma y una venta..." << std::endl;
    auto d = e.direct("Movimientos B16", 0);
    const std::string old = "2020-01-01 00:00:00";
    e.buy(d.second, 20, "pz");
    e.age_movements(d.second, old);
    TEST_CHECK(e.get(d.first).last_movement_at == old, "Punto de partida: hora vieja");
    e.buy(d.second, 5, "pz");
    ProductDto p = e.get(d.first);
    TEST_CHECK(p.last_movement_at > old && p.last_movement_at == e.last_of(d.second), "Tras una compra cambia: " + p.last_movement_at);
    e.age_movements(d.second, old);
    e.waste(d.second, 1, "pz");
    p = e.get(d.first);
    TEST_CHECK(p.last_movement_at > old && p.last_movement_at == e.last_of(d.second), "Tras una merma cambia");
    e.age_movements(d.second, old);
    TEST_CHECK(e.cart->add_item(d.first, 1, "").is_success(), "Agregar al carrito");
    TEST_CHECK(e.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(2000)).is_success(), "Cobrar");
    p = e.get(d.first);
    TEST_CHECK(p.last_movement_at > old && p.last_movement_at == e.last_of(d.second) && p.stock_units == 23, "Tras una venta cambia y baja la existencia");
    TEST_CHECK(p.last_movement_at.size() == 19 && p.last_movement_at[10] == ' ', "Formato AAAA-MM-DD HH:MM:SS");
}

void test_create_product_status(Env& e) {
    std::cout << "[TEST] B17: create_product devuelve estado y último movimiento reales..." << std::endl;
    // Directo nuevo: controla existencia, sin existencias ni movimientos
    CreateProductDto np;
    np.sku = e.next("CP"); np.name = "Creado directo B17"; np.category = "Pruebas"; np.price = Money::from_cents(1000);
    auto created = e.catalog->create_product(np);
    TEST_CHECK(created.is_success(), "Crear directo: " + created.error_message());
    TEST_CHECK(created.value().stock_tracked && created.value().stock_units == 0 && created.value().stock_status == "OUT" &&
               created.value().last_movement_at.empty(), "Directo nuevo: tracked, 0 unidades, OUT y sin movimiento");
    ProductDto got = e.get(created.value().id);
    TEST_CHECK(got.stock_tracked == created.value().stock_tracked && got.stock_units == created.value().stock_units &&
               got.stock_status == created.value().stock_status && got.last_movement_at == created.value().last_movement_at,
               "create_product coincide con get_product_by_id (directo nuevo)");

    // Directo enlazado a un ingrediente que ya tiene existencias y movimientos
    const std::string ing = e.ingredient("Pieza previa B17", "COUNT", "pz", 5);
    e.buy(ing, 6, "pz");
    e.waste(ing, 3, "pz");   // quedan 3, umbral 5: LOW
    CreateProductDto lp;
    lp.sku = e.next("CL"); lp.name = "Creado enlazado B17"; lp.category = "Pruebas"; lp.price = Money::from_cents(1000);
    lp.link_ingredient_id = ing;
    auto linked = e.catalog->create_product(lp);
    TEST_CHECK(linked.is_success(), "Crear enlazado: " + linked.error_message());
    TEST_CHECK(linked.value().stock_tracked && linked.value().stock_units == 3 && linked.value().stock_status == "LOW" &&
               !linked.value().last_movement_at.empty(), "Enlazado: 3 unidades, LOW y último movimiento real");
    got = e.get(linked.value().id);
    TEST_CHECK(got.stock_units == linked.value().stock_units && got.stock_status == linked.value().stock_status &&
               got.last_movement_at == linked.value().last_movement_at && got.last_movement_at == e.last_of(ing),
               "create_product coincide con get_product_by_id (enlazado)");

    // Sin control: directo con track_stock falso
    const std::string u = e.untracked("Creado sin control B17");
    ProductDto ug = e.get(u);
    TEST_CHECK(!ug.stock_tracked && ug.stock_status.empty() && ug.last_movement_at.empty(), "Sin control: vacíos al leer");
    CreateProductDto up;
    up.sku = e.next("CU"); up.name = "Devuelto sin control B17"; up.category = "Pruebas"; up.price = Money::from_cents(500); up.track_stock = false;
    auto ur = e.catalog->create_product(up);
    TEST_CHECK(ur.is_success() && !ur.value().stock_tracked && ur.value().stock_units == 0 && ur.value().stock_status.empty() &&
               ur.value().last_movement_at.empty(), "create_product sin control: vacíos");

    // Preparado sin receta
    CreateProductDto pp;
    pp.sku = e.next("CR"); pp.name = "Preparado sin receta B17"; pp.category = "Pruebas"; pp.price = Money::from_cents(500); pp.is_prepared = true;
    auto pr = e.catalog->create_product(pp);
    TEST_CHECK(pr.is_success() && !pr.value().stock_tracked && pr.value().stock_units == 0 && pr.value().stock_status.empty() &&
               pr.value().last_movement_at.empty(), "create_product preparado sin receta: vacíos");

    // Mock
    MockCatalogService mock;
    auto md = mock.create_product(np);
    TEST_CHECK(md.is_success() && md.value().stock_tracked && md.value().stock_units == 0 && md.value().stock_status == "OUT" &&
               md.value().last_movement_at.empty(), "Mock: directo nuevo");
    auto ml = mock.create_product(lp);
    TEST_CHECK(ml.is_success() && ml.value().stock_tracked && ml.value().stock_status == "OUT", "Mock: directo enlazado");
    auto mu = mock.create_product(up);
    TEST_CHECK(mu.is_success() && !mu.value().stock_tracked && mu.value().stock_status.empty() && mu.value().last_movement_at.empty(), "Mock: sin control");
    auto mp = mock.create_product(pp);
    TEST_CHECK(mp.is_success() && !mp.value().stock_tracked && mp.value().stock_status.empty() && mp.value().last_movement_at.empty(), "Mock: preparado sin receta");
}

void test_mock() {
    std::cout << "[TEST] B16: el mock trae estado y último movimiento..." << std::endl;
    MockCatalogService mock;
    auto r = mock.search_products("", std::nullopt, 0, 50, true);
    TEST_CHECK(r.is_success() && !r.value().empty(), "Mock: productos");
    for (const auto& p : r.value()) {
        TEST_CHECK(p.stock_tracked && (p.stock_status == "OK" || p.stock_status == "LOW" || p.stock_status == "OUT"), "Mock: estado válido");
        TEST_CHECK(p.last_movement_at.size() == 19, "Mock: último movimiento con formato");
    }
    auto one = mock.get_product_by_id(r.value()[0].id);
    TEST_CHECK(one.is_success() && one.value().stock_status == r.value()[0].stock_status, "Mock: get_product_by_id igual que search_products");
}

void test_no_n_plus_one(Env& e) {
    std::cout << "[TEST] B16: 500 productos sin una consulta por producto..." << std::endl;
    // 40 ingredientes con existencia; 450 preparados con 2 ingredientes en su receta; 50 combos con 3 partes.
    e.exec("BEGIN;");
    e.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 40) "
           "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold) "
           "SELECT 'pi-' || i, 'Ingrediente rend ' || i, 'MASS', 'mg', 1000 + i * 100, (i % 5) * 500 FROM n;");
    e.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 450) "
           "INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at) "
           "SELECT 'pp-' || i, 'PSKU-' || i, NULL, 'Producto rend ' || printf('%04d', i), 'Rendimiento', 1500, 1, 1, '2026-11-18 08:00:00' FROM n;");
    e.exec("INSERT INTO recipes (id, product_id, version, created_at) SELECT 'pr-' || id, id, 1, '2026-11-18 08:00:00' FROM products WHERE id LIKE 'pp-%';");
    e.exec("INSERT INTO recipe_items (id, recipe_id, ingredient_id, quantity_base_units) "
           "SELECT 'pri-' || r.id || '-1', r.id, 'pi-' || (1 + (CAST(substr(r.product_id, 4) AS INTEGER) % 40)), 50 FROM recipes r WHERE r.id LIKE 'pr-pp-%';");
    e.exec("INSERT INTO recipe_items (id, recipe_id, ingredient_id, quantity_base_units) "
           "SELECT 'pri-' || r.id || '-2', r.id, 'pi-' || (1 + ((CAST(substr(r.product_id, 4) AS INTEGER) + 7) % 40)), 30 FROM recipes r WHERE r.id LIKE 'pr-pp-%';");
    e.exec("WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 50) "
           "INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at, is_combo) "
           "SELECT 'pc-' || i, 'CSKU-' || i, NULL, 'Combo rend ' || printf('%03d', i), 'Rendimiento', 5000, 0, 1, '2026-11-18 08:00:00', 1 FROM n;");
    e.exec("INSERT INTO combo_items (combo_product_id, component_product_id, quantity) "
           "SELECT 'pc-' || i, 'pp-' || (i * 3 + k), 1 FROM (WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 50) SELECT i FROM n), "
           "(SELECT 0 AS k UNION ALL SELECT 1 UNION ALL SELECT 2);");
    e.exec("COMMIT;");

    // B17: 30 000 movimientos de existencias repartidos entre los 40 ingredientes (como un mes de 30 000 ventas),
    // con fechas distintas por ingrediente (cada ingrediente tiene su propio mes y día más reciente).
    e.exec("WITH RECURSIVE n(i) AS (SELECT 0 UNION ALL SELECT i + 1 FROM n WHERE i < 29999) "
           "INSERT INTO stock_movements (id, ingredient_id, movement_type, quantity_base_units, reason, created_at) "
           "SELECT 'psm-' || i, 'pi-' || (1 + (i % 40)), 'PURCHASE', 1, 'Rendimiento', "
           "printf('2026-%02d-%02d %02d:%02d:00', 1 + ((i % 40) % 10), 1 + ((i / 40) % 28), (i / 1120) % 24, (i / 40) % 60) FROM n;");
    TEST_CHECK(e.text("SELECT COUNT(*) FROM stock_movements WHERE id LIKE 'psm-%';") == "30000", "30 000 movimientos sembrados");
    TEST_CHECK(e.text("SELECT COUNT(DISTINCT ingredient_id) FROM stock_movements WHERE id LIKE 'psm-%';") == "40", "Repartidos entre 40 ingredientes");

    auto ms_since = [](std::chrono::steady_clock::time_point t0) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
    };
    const auto q0 = e.db.prepared_statement_count();
    auto small = e.catalog->search_products("Producto rend 000", std::nullopt, 0, 20, true);
    const auto q_small = e.db.prepared_statement_count() - q0;
    TEST_CHECK(small.is_success() && !small.value().empty(), "Búsqueda pequeña");
    const auto q1 = e.db.prepared_statement_count();
    const auto t0 = std::chrono::steady_clock::now();
    auto big = e.catalog->search_products("rend", std::nullopt, 0, 500, true);
    const auto ms = ms_since(t0);
    const auto q_big = e.db.prepared_statement_count() - q1;
    TEST_CHECK(big.is_success() && big.value().size() == 500, "Se leyeron 500 productos de rendimiento: " + std::to_string(big.value().size()));
    std::cout << "[MEDIDA] search_products con 500 productos: " << ms << " ms y " << q_big << " consultas (con " << small.value().size()
              << " productos: " << q_small << " consultas)" << std::endl;
    TEST_CHECK(q_big <= 6, "Pocas consultas con 500 productos (hizo " + std::to_string(q_big) + ")");
    TEST_CHECK(q_big <= q_small + 2, "El número de consultas no crece con el número de productos");
    int tracked = 0, low = 0, ok = 0, out = 0;
    for (const auto& p : big.value()) {
        TEST_CHECK(p.stock_tracked, "Todos controlan existencia: " + p.name);
        TEST_CHECK(p.stock_status == "OK" || p.stock_status == "LOW" || p.stock_status == "OUT", "Estado válido");
        ++tracked;
        (p.stock_status == "LOW" ? low : p.stock_status == "OK" ? ok : out)++;
    }
    TEST_CHECK(tracked == 500 && low > 0 && ok > 0, "Hay productos en LOW y en OK (OK=" + std::to_string(ok) + " LOW=" + std::to_string(low) + " OUT=" + std::to_string(out) + ")");
    // (a) last_movement_at coincide con el MAX(created_at) real de los ingredientes de cada producto o de las partes de cada combo
    int with_movement = 0;
    for (const auto& p : big.value()) {
        const std::string sql = p.is_combo
            ? "SELECT MAX(m.created_at) FROM stock_movements m JOIN recipe_items ri ON ri.ingredient_id = m.ingredient_id "
              "JOIN recipes r ON r.id = ri.recipe_id JOIN combo_items ci ON ci.component_product_id = r.product_id "
              "WHERE ci.combo_product_id = '" + p.id + "';"
            : "SELECT MAX(m.created_at) FROM stock_movements m JOIN recipe_items ri ON ri.ingredient_id = m.ingredient_id "
              "JOIN recipes r ON r.id = ri.recipe_id WHERE r.product_id = '" + p.id + "';";
        const std::string expected = e.text(sql);
        TEST_CHECK(p.last_movement_at == expected, "last_movement_at de " + p.name + ": '" + p.last_movement_at + "' y debía ser '" + expected + "'");
        if (!expected.empty()) ++with_movement;
    }
    TEST_CHECK(with_movement == 500, "Los 500 productos tienen movimiento");

    // (c) Plan de consulta (solo se imprime como diagnóstico: cambia entre versiones de SQLite, no se afirma)
    {
        const char* plans[] = {
            "EXPLAIN QUERY PLAN SELECT MAX(m.created_at) FROM stock_movements m WHERE m.ingredient_id = 'pi-1';",
            "EXPLAIN QUERY PLAN SELECT r.product_id, ri.quantity_base_units, ing.current_stock, ing.low_stock_threshold, "
            "COALESCE((SELECT MAX(m.created_at) FROM stock_movements m WHERE m.ingredient_id = ing.id), '') "
            "FROM recipes r JOIN recipe_items ri ON ri.recipe_id = r.id JOIN ingredients ing ON ing.id = ri.ingredient_id "
            "WHERE r.product_id IN ('pp-1','pp-2') AND r.id = (SELECT id FROM recipes WHERE product_id = r.product_id ORDER BY version DESC LIMIT 1);"};
        for (const char* q : plans) {
            auto prep = e.db.prepare(q);
            TEST_CHECK(prep.is_success(), "Preparar EXPLAIN");
            auto st = std::move(prep.value());
            std::cout << "[PLAN] " << (std::string(q).find("IN ('pp-1'") != std::string::npos ? "consulta de recetas" : "último movimiento por ingrediente") << std::endl;
            while (true) {
                auto step = st.step();
                TEST_CHECK(step.is_success(), "Paso de EXPLAIN");
                if (!step.value()) break;
                std::cout << "[PLAN]   " << st.get_text(3) << std::endl;
            }
        }
    }

    // El mismo producto da lo mismo leído de una en una (get_product_by_id) que en el listado grande
    for (std::size_t i = 0; i < big.value().size(); i += 47) {
        const auto& p = big.value()[i];
        auto g = e.catalog->get_product_by_id(p.id);
        TEST_CHECK(g.is_success() && g.value().stock_status == p.stock_status && g.value().stock_units == p.stock_units &&
                   g.value().last_movement_at == p.last_movement_at, "Lectura individual igual a la del listado: " + p.name);
    }
#ifdef NDEBUG
    TEST_CHECK(ms < 1000, "500 productos tardan menos de 1 s en Release (tardó " + std::to_string(ms) + " ms)");
#endif
}

} // namespace

int main() {
    std::cout << "=== PRUEBAS B16: ESTADO Y ULTIMO MOVIMIENTO DE PRODUCTOS ===" << std::endl;
    Env e;
    test_direct(e);
    test_prepared(e);
    test_combo_and_untracked(e);
    test_last_movement_changes(e);
    test_create_product_status(e);
    test_mock();
    test_no_n_plus_one(e);
    e.cleanup();
    std::cout << "\n>>> TODAS LAS PRUEBAS B16 PASARON CON EXITO! <<<\n" << std::endl;
    return 0;
}
