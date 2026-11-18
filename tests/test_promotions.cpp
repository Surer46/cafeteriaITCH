// Pruebas B14: promociones, combos, existencia de productos directos y descuentos en carrito, venta, ticket y reporte.
// Reloj local inyectado (LocalNow): ninguna prueba depende de la hora real.
#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "app/domain/clock.hpp"
#include "app/services/promotion_engine.hpp"
#include "app/services/sqlite_account_service.hpp"
#include "app/services/sqlite_cart_service.hpp"
#include "app/services/sqlite_catalog_service.hpp"
#include "app/services/sqlite_inventory_service.hpp"
#include "app/services/sqlite_promotion_service.hpp"
#include "app/services/sqlite_report_service.hpp"
#include "app/services/sqlite_sale_service.hpp"
#include "app/services/sqlite_shift_service.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/storage/sqlite_database.hpp"

using namespace app::storage;
using namespace app::services;
using namespace app::contracts;
namespace fs = std::filesystem;

static int g_checks = 0;
static fs::path g_root;
#define TEST_CHECK(condition, message) \
    do { \
        ++g_checks; \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA DE PROMOCIONES] " << (message) << "\n  Condición: " << #condition \
                      << "\n  Línea: " << __LINE__ << "\n" << std::endl; \
            std::exit(1); \
        } \
    } while (0)

namespace {

const char* kOwnerPwd = "Clave-Dueno-B14-1";
const char* kCashierPwd = "Clave-Cajero-B14-1";
const char* kManagerPwd = "Clave-Encargado-B14-1";

std::vector<std::string> g_messages;  // todo texto que los servicios devuelven para mostrar
void seen(const std::string& m) { if (!m.empty()) g_messages.push_back(m); }

bool contains_word_ci(const std::string& text, const std::string& word) {
    auto lower = [](std::string v) { for (auto& c : v) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a'); return v; };
    const std::string t = lower(text), w = lower(word);
    auto letter = [](char c) { return (c >= 'a' && c <= 'z') || (static_cast<unsigned char>(c) >= 0x80) || (c >= '0' && c <= '9'); };
    for (std::size_t pos = t.find(w); pos != std::string::npos; pos = t.find(w, pos + 1)) {
        const bool left = pos == 0 || !letter(t[pos - 1]);
        const bool right = pos + w.size() >= t.size() || !letter(t[pos + w.size()]);
        if (left && right) return true;
    }
    return false;
}

struct Env {
    fs::path root;
    SqliteDatabase db;
    LocalNow now{"2026-11-18", 3, 9 * 60 + 30};  // miércoles 09:30
    std::shared_ptr<SqliteAccountService> account;
    std::shared_ptr<SqliteCatalogService> catalog;
    std::shared_ptr<SqliteInventoryService> inventory;
    std::shared_ptr<SqliteShiftService> shift;
    std::shared_ptr<SqliteCartService> cart;
    std::shared_ptr<SqliteSaleService> sale;
    std::shared_ptr<SqliteReportService> report;
    std::shared_ptr<SqlitePromotionService> promo;
    std::string cafe, pan, galleta;  // ids de producto

    explicit Env(const std::string& name) {
        root = g_root / name;
        fs::create_directories(root);
        TEST_CHECK(db.open(root / "cafeteria.db").is_success(), "Abrir base");
        MigrationRunner runner(db);
        auto m = runner.run_pending_migrations_from_dir(fs::u8path(SOURCE_MIGRATIONS_DIR));
        TEST_CHECK(m.is_success(), "Migraciones: " + m.error_message());
        auto clock = [this] { return now; };
        account = std::make_shared<SqliteAccountService>(db);
        catalog = std::make_shared<SqliteCatalogService>(db, account);
        inventory = std::make_shared<SqliteInventoryService>(db, account);
        shift = std::make_shared<SqliteShiftService>(db, account);
        cart = std::make_shared<SqliteCartService>(db, account, catalog, clock);
        sale = std::make_shared<SqliteSaleService>(db, account, cart, shift, clock);
        report = std::make_shared<SqliteReportService>(db, account);
        promo = std::make_shared<SqlitePromotionService>(db, account, catalog, clock);

        TEST_CHECK(account->create_initial_owner("dueno_b14", "Dueño B14", kOwnerPwd, "dueno@cafeteria-itch.edu.mx").is_success(), "Alta del Dueño");
        TEST_CHECK(account->login("dueno_b14", kOwnerPwd).is_success(), "Dueño entra");
        CreateUserDto c{"cajero_b14", "Cajero B14", kCashierPwd, "role-cashier", "cajero@cafeteria-itch.edu.mx"};
        TEST_CHECK(account->create_user(c).is_success(), "Alta del Cajero");
        CreateRoleDto role;
        role.name = "Encargado";
        role.description = "Promociones";
        role.permissions = {"inventory.promotions", "inventory.view"};
        auto r = account->create_role(role);
        TEST_CHECK(r.is_success(), "Rol propio: " + r.error_message());
        CreateUserDto e{"encargado_b14", "Encargado B14", kManagerPwd, r.value().id, "encargado@cafeteria-itch.edu.mx"};
        TEST_CHECK(account->create_user(e).is_success(), "Alta del Encargado");

        cafe = make_direct("CAFE", "Café", "Bebidas", 3000);
        pan = make_direct("PAN", "Pan", "Panadería", 2000);
        galleta = make_direct("GALL", "Galleta", "Panadería", 1000);
        TEST_CHECK(shift->open_shift("reg-01", Money::from_cents(50000)).is_success(), "Abrir turno");
    }

    // Producto directo SIN existencia controlada (se vende sin descontar).
    std::string make_direct(const std::string& sku, const std::string& name, const std::string& category, std::int64_t cents) {
        CreateProductDto p;
        p.sku = sku;
        p.name = name;
        p.category = category;
        p.price = Money::from_cents(cents);
        p.track_stock = false;
        auto r = catalog->create_product(p);
        TEST_CHECK(r.is_success(), "Crear producto " + sku + ": " + r.error_message());
        return r.value().id;
    }

    void login(const std::string& user, const std::string& pwd) {
        account->logout();
        TEST_CHECK(account->login(user, pwd).is_success(), "Entrar como " + user);
    }
    void owner() { login("dueno_b14", kOwnerPwd); }

    void exec(const std::string& sql) {
        auto r = db.execute(sql);
        TEST_CHECK(r.is_success(), "SQL: " + sql + " -> " + r.error_message());
    }
    std::string scalar(const std::string& sql) {
        auto prep = db.prepare(sql);
        TEST_CHECK(prep.is_success(), "consulta: " + sql);
        auto stmt = std::move(prep.value());
        auto st = stmt.step();
        TEST_CHECK(st.is_success() && st.value(), "consulta sin fila: " + sql);
        return stmt.is_null(0) ? "" : stmt.get_text(0);
    }
    long long count_audit(const std::string& type) { return std::stoll(scalar("SELECT COUNT(*) FROM audit_events WHERE event_type = '" + type + "';")); }

    CreatePromotionDto percent(const std::string& name, std::int64_t bp, const std::vector<std::string>& products, int priority = 0) {
        CreatePromotionDto d;
        d.name = name; d.kind = "PERCENT"; d.percent_basis_points = bp; d.scope_kind = "PRODUCT"; d.product_ids = products; d.priority = priority;
        return d;
    }
    CreatePromotionDto fixed(const std::string& name, std::int64_t cents, const std::vector<std::string>& products, int priority = 0) {
        CreatePromotionDto d;
        d.name = name; d.kind = "FIXED"; d.fixed_off = Money::from_cents(cents); d.scope_kind = "PRODUCT"; d.product_ids = products; d.priority = priority;
        return d;
    }
    CreatePromotionDto nxm(const std::string& name, std::int64_t n, std::int64_t m, const std::vector<std::string>& products, int priority = 0) {
        CreatePromotionDto d;
        d.name = name; d.kind = "NXM"; d.buy_qty = n; d.pay_qty = m; d.scope_kind = "PRODUCT"; d.product_ids = products; d.priority = priority;
        return d;
    }
    PromotionDto make(const CreatePromotionDto& d) {
        auto r = promo->create_promotion(d);
        TEST_CHECK(r.is_success(), "Crear promoción " + d.name + ": " + r.error_message());
        return r.value();
    }
    CartSummaryDto cart_now() {
        auto r = cart->get_cart();
        TEST_CHECK(r.is_success(), "get_cart: " + r.error_message());
        return r.value();
    }
    void add(const std::string& pid, std::int64_t qty, const std::string& notes = "") {
        auto r = cart->add_item(pid, qty, notes);
        TEST_CHECK(r.is_success(), "add_item: " + r.error_message());
    }
    void clear_cart() { TEST_CHECK(cart->clear_cart().is_success(), "Vaciar carrito"); }
    void deactivate_all() { exec("UPDATE promotions SET is_active = 0;"); }
};

} // namespace

// ===============================================================================================
static void test_pure_functions() {
    std::cout << "[TEST] B14: porcentaje, NxM y vigencia (funciones puras)..." << std::endl;
    TEST_CHECK(percent_discount_cents(1250, 1000) == 125, "10 % de 12.50");
    TEST_CHECK(percent_discount_cents(333, 1000) == 33, "33.3 se redondea a 33");
    TEST_CHECK(percent_discount_cents(335, 1000) == 34, "33.5 sube a 34 (mitad hacia arriba)");
    TEST_CHECK(percent_discount_cents(1, 5000) == 1, "0.5 centavo sube a 1");
    TEST_CHECK(percent_discount_cents(1, 4999) == 0, "0.4999 baja a 0");
    TEST_CHECK(percent_discount_cents(3399, 3333) == 1133, "33.33 % de 33.99 = 11.329... = 1133");
    TEST_CHECK(percent_discount_cents(0, 5000) == 0 && percent_discount_cents(-5, 5000) == 0 && percent_discount_cents(100, 0) == 0, "Entradas nulas");
    TEST_CHECK(percent_discount_cents(100, 10000) == 100, "100 % = todo");
    TEST_CHECK(percent_discount_cents(INT64_MAX, 10000) == INT64_MAX, "El 100 % de un importe enorme no desborda");
    TEST_CHECK(percent_discount_cents(INT64_MAX, 5000) == INT64_MAX / 2 + 1, "50 % de un importe enorme sin desbordar y con mitad hacia arriba");
    TEST_CHECK(nxm_free_units(3, 2, 1) == 1 && nxm_free_units(5, 2, 1) == 2 && nxm_free_units(1, 2, 1) == 0, "2x1");
    TEST_CHECK(nxm_free_units(5, 3, 2) == 1 && nxm_free_units(6, 3, 2) == 2 && nxm_free_units(4, 3, 2) == 1 && nxm_free_units(2, 3, 2) == 0, "3x2");
    TEST_CHECK(nxm_free_units(0, 2, 1) == 0 && nxm_free_units(5, 1, 0) == 0 && nxm_free_units(5, 2, 2) == 0 && nxm_free_units(5, 2, 3) == 0, "NxM inválido no regala");

    const LocalNow wed{"2026-11-18", 3, 9 * 60 + 30};
    TEST_CHECK(promotion_in_validity("", "", 0, -1, -1, wed), "Sin límites siempre vale");
    TEST_CHECK(promotion_in_validity("2026-11-18", "2026-11-18", 0, -1, -1, wed), "Un solo día, inclusive");
    TEST_CHECK(!promotion_in_validity("2026-11-19", "", 0, -1, -1, wed) && !promotion_in_validity("", "2026-11-17", 0, -1, -1, wed), "Fuera de fechas");
    TEST_CHECK(promotion_in_validity("", "", 1 << 2, -1, -1, wed) && !promotion_in_validity("", "", 1 << 3, -1, -1, wed), "Día de la semana (miércoles = bit 2)");
    TEST_CHECK(promotion_in_validity("", "", 0, 9 * 60 + 30, 11 * 60, wed), "La hora de inicio es inclusiva");
    TEST_CHECK(!promotion_in_validity("", "", 0, 7 * 60, 9 * 60 + 30, wed), "La hora de fin es exclusiva");
    TEST_CHECK(!promotion_in_validity("", "", 0, 10 * 60, 11 * 60, wed), "Antes del horario");
}

static void test_permissions() {
    std::cout << "[TEST] B14: permisos (Cajero sin acceso; rol propio con inventory.promotions sí)..." << std::endl;
    Env env("permisos");
    CreateComboDto combo;
    combo.sku = "CMB-P"; combo.name = "Combo P"; combo.category = "Combos"; combo.price = Money::from_cents(4500);
    combo.components = {{env.cafe, "", 1}, {env.pan, "", 1}};
    auto all_denied = [&](const std::string& who) {
        auto l = env.promo->list_promotions({});
        TEST_CHECK(!l.is_success() && l.error_code() == ErrorCode::Forbidden, who + ": listar");
        seen(l.error_message());
        TEST_CHECK(env.promo->get_promotion("x").error_code() == ErrorCode::Forbidden, who + ": ver");
        auto c = env.promo->create_promotion(env.percent("Nada", 1000, {env.cafe}));
        TEST_CHECK(c.error_code() == ErrorCode::Forbidden, who + ": crear");
        seen(c.error_message());
        TEST_CHECK(env.promo->update_promotion({"x", env.percent("Nada", 1000, {env.cafe})}).error_code() == ErrorCode::Forbidden, who + ": editar");
        TEST_CHECK(env.promo->set_promotion_active("x", false).error_code() == ErrorCode::Forbidden, who + ": activar");
        TEST_CHECK(env.promo->list_combos(true).error_code() == ErrorCode::Forbidden, who + ": listar combos");
        TEST_CHECK(env.promo->get_combo("x").error_code() == ErrorCode::Forbidden, who + ": ver combo");
        TEST_CHECK(env.promo->create_combo(combo).error_code() == ErrorCode::Forbidden, who + ": crear combo");
        TEST_CHECK(env.promo->update_combo({"x", "n", "c", Money::zero(), combo.components}).error_code() == ErrorCode::Forbidden, who + ": editar combo");
        TEST_CHECK(env.promo->get_combo_costing("x").error_code() == ErrorCode::Forbidden, who + ": costo");
    };
    env.account->logout();
    all_denied("sin sesión");
    env.login("cajero_b14", kCashierPwd);
    all_denied("Cajero");
    TEST_CHECK(env.promo->list_promotions({}).error_message() == "No tiene permiso para administrar promociones y combos.", "Texto de permiso");
    // El Cajero no registra mermas ni da existencia a productos
    TEST_CHECK(env.inventory->register_waste("x", 1, "pz", "motivo").error_code() == ErrorCode::Forbidden, "El Cajero no registra mermas");
    TEST_CHECK(env.catalog->enable_stock_for_direct_product(env.cafe).error_code() == ErrorCode::Forbidden, "El Cajero no da existencia a productos");

    env.login("encargado_b14", kManagerPwd);
    TEST_CHECK(env.promo->list_promotions({}).is_success(), "El rol propio con inventory.promotions entra");
    auto made = env.promo->create_promotion(env.percent("Del encargado", 1000, {env.cafe}));
    TEST_CHECK(made.is_success(), "…y crea promociones: " + made.error_message());
    auto cc = env.promo->create_combo(combo);
    TEST_CHECK(cc.is_success(), "…y combos: " + cc.error_message());
    auto costing = env.promo->get_combo_costing(cc.value().product_id);
    TEST_CHECK(costing.error_code() == ErrorCode::Forbidden, "Sin inventory.manage_products no ve costos");
    seen(costing.error_message());
    TEST_CHECK(env.account->has_permission("inventory.promotions") && !env.account->has_permission("inventory.adjust"), "El rol propio no tiene mermas");
    env.owner();
    TEST_CHECK(env.promo->get_combo_costing(cc.value().product_id).is_success(), "El Dueño sí ve costos");
    // Permisos de sistema: no se crearon permisos ni roles nuevos
    TEST_CHECK(env.scalar("SELECT COUNT(*) FROM permissions;") == "22", "Siguen 22 permisos (sin permisos nuevos)");
    TEST_CHECK(env.scalar("SELECT COUNT(*) FROM roles;") == "4", "Roles: Dueño, Cajero, Desarrollador y el creado por el Dueño");
}

static void test_promotion_crud_and_validation() {
    std::cout << "[TEST] B14: crear, editar, activar, listar y validar promociones; bitácora..." << std::endl;
    Env env("crud");
    auto p10 = env.make(env.percent("Diez por ciento", 1000, {env.cafe}, 5));
    TEST_CHECK(p10.summary_text == "10 % de descuento" && p10.validity_text == "Siempre" && p10.is_valid_now && p10.product_names == std::vector<std::string>{"Café"}, "Porcentaje: textos");
    auto fx = env.make(env.fixed("Cinco menos", 500, {env.pan}, 1));
    TEST_CHECK(fx.summary_text == "$5.00 menos en cada pieza", "Monto fijo: " + fx.summary_text);
    auto x21 = env.make(env.nxm("Dos por uno", 2, 1, {env.galleta}));
    TEST_CHECK(x21.summary_text == "Lleva 2 y paga 1", "NxM: " + x21.summary_text);
    auto dec = env.make(env.percent("Doce y medio", 1250, {env.cafe}, 99));
    TEST_CHECK(dec.summary_text == "12.5 % de descuento", "Porcentaje con decimales: " + dec.summary_text);
    env.exec("UPDATE promotions SET is_active = 0 WHERE name = 'Doce y medio';");

    auto list = env.promo->list_promotions({}).value();
    TEST_CHECK(list.size() == 4 && list[0].name == "Doce y medio" && list[1].name == "Diez por ciento", "Orden: mayor prioridad primero");
    TEST_CHECK(env.promo->list_promotions({true, ""}).value().size() == 3, "Solo activas");
    TEST_CHECK(env.promo->list_promotions({false, env.pan}).value().size() == 1, "Por producto");

    // Vigencia en lenguaje llano
    auto vd = env.percent("Lun a vie mañana", 1000, {env.galleta}, 10);
    vd.weekdays = {1, 2, 3, 4, 5};
    vd.time_from = "07:00";
    vd.time_to = "11:00";
    auto lv = env.make(vd);
    TEST_CHECK(lv.validity_text == "Lunes a viernes, de 07:00 a 11:00" && lv.is_valid_now, "Vigencia: " + lv.validity_text);
    auto vd2 = env.percent("Fines de semana", 1000, {env.galleta}, 11);
    vd2.weekdays = {6, 7};
    auto lv2 = env.make(vd2);
    TEST_CHECK(lv2.validity_text == "Sábado y domingo" && !lv2.is_valid_now, "Vigencia fin de semana: " + lv2.validity_text);
    auto vd3 = env.percent("Diciembre", 1000, {env.galleta}, 12);
    vd3.valid_from = "2026-12-01";
    vd3.valid_to = "2026-12-31";
    TEST_CHECK(env.make(vd3).validity_text == "Del 01/12/2026 al 31/12/2026", "Vigencia por fechas");
    auto vd4 = env.percent("Desde", 1000, {env.galleta}, 13);
    vd4.valid_from = "2026-12-01";
    auto lv4 = env.make(vd4);
    TEST_CHECK(lv4.validity_text == "Desde el 01/12/2026" && !lv4.is_valid_now, "Vigencia desde");
    auto vd5 = env.percent("Lunes miércoles viernes", 1000, {env.galleta}, 14);
    vd5.weekdays = {1, 3, 5};
    TEST_CHECK(env.make(vd5).validity_text == "Lunes, miércoles y viernes", "Lista de días");
    env.now = LocalNow{"2026-11-18", 3, 12 * 60};
    TEST_CHECK(!env.promo->get_promotion(lv.id).value().is_valid_now, "El reloj inyectado cambia is_valid_now (12:00 fuera de horario)");
    env.now = LocalNow{"2026-11-18", 3, 9 * 60 + 30};

    // Validaciones (mensajes llanos)
    auto bad = [&](const std::string& what, CreatePromotionDto d) {
        auto r = env.promo->create_promotion(d);
        TEST_CHECK(!r.is_success() && r.error_code() == ErrorCode::InvalidInput, what + ": " + r.error_message());
        seen(r.error_message());
    };
    bad("sin nombre", env.percent("", 1000, {env.cafe}));
    bad("porcentaje 0", env.percent("x", 0, {env.cafe}));
    bad("porcentaje 101", env.percent("x", 10001, {env.cafe}));
    bad("monto 0", env.fixed("x", 0, {env.cafe}));
    bad("NxM 1x1", env.nxm("x", 1, 1, {env.cafe}));
    bad("NxM 2x2", env.nxm("x", 2, 2, {env.cafe}));
    bad("NxM paga 0", env.nxm("x", 2, 0, {env.cafe}));
    bad("sin productos", env.percent("x", 1000, {}));
    bad("producto repetido", env.percent("x", 1000, {env.cafe, env.cafe}));
    { auto d = env.percent("x", 1000, {env.cafe}); d.kind = "OTRA"; bad("tipo inválido", d); }
    { auto d = env.percent("x", 1000, {env.cafe}); d.scope_kind = "TODO"; bad("alcance inválido", d); }
    { auto d = env.percent("x", 1000, {env.cafe}); d.category = "Bebidas"; bad("producto y categoría a la vez", d); }
    { auto d = env.percent("x", 1000, {}); d.scope_kind = "CATEGORY"; bad("categoría vacía", d); }
    { auto d = env.percent("x", 1000, {}); d.scope_kind = "CATEGORY"; d.category = "Inexistente"; auto r = env.promo->create_promotion(d); TEST_CHECK(r.error_code() == ErrorCode::NotFound, "categoría sin productos"); seen(r.error_message()); }
    { auto d = env.percent("x", 1000, {"no-existe"}); auto r = env.promo->create_promotion(d); TEST_CHECK(r.error_code() == ErrorCode::NotFound, "producto inexistente"); seen(r.error_message()); }
    { auto d = env.percent("x", 1000, {env.cafe}); d.valid_from = "2026-02-30"; bad("fecha inexistente", d); }
    { auto d = env.percent("x", 1000, {env.cafe}); d.valid_from = "2026-12-10"; d.valid_to = "2026-12-01"; bad("fin antes del inicio", d); }
    { auto d = env.percent("x", 1000, {env.cafe}); d.weekdays = {0}; bad("día 0", d); }
    { auto d = env.percent("x", 1000, {env.cafe}); d.weekdays = {8}; bad("día 8", d); }
    { auto d = env.percent("x", 1000, {env.cafe}); d.time_from = "07:00"; bad("solo hora de inicio", d); }
    { auto d = env.percent("x", 1000, {env.cafe}); d.time_from = "11:00"; d.time_to = "07:00"; bad("horario que cruza la medianoche", d); }
    { auto d = env.percent("x", 1000, {env.cafe}); d.time_from = "25:00"; d.time_to = "26:00"; bad("hora inválida", d); }

    // Editar y activar/desactivar
    auto upd = env.percent("Diez renombrada", 1500, {env.cafe, env.pan}, 5);
    auto edited = env.promo->update_promotion({p10.id, upd});
    TEST_CHECK(edited.is_success() && edited.value().name == "Diez renombrada" && edited.value().summary_text == "15 % de descuento" && edited.value().product_ids.size() == 2, "Editar");
    TEST_CHECK(!env.promo->update_promotion({"no-existe", upd}).is_success() && env.promo->update_promotion({"no-existe", upd}).error_code() == ErrorCode::NotFound, "Editar inexistente");
    TEST_CHECK(env.promo->set_promotion_active(p10.id, false).is_success() && !env.promo->get_promotion(p10.id).value().is_active && !env.promo->get_promotion(p10.id).value().is_valid_now, "Desactivar");
    TEST_CHECK(env.promo->set_promotion_active(p10.id, true).is_success() && env.promo->get_promotion(p10.id).value().is_active, "Activar");
    TEST_CHECK(env.promo->set_promotion_active("no-existe", true).error_code() == ErrorCode::NotFound, "Activar inexistente");
    // Categoría
    auto cat = env.percent("Toda la panadería", 2000, {}, 3);
    cat.scope_kind = "CATEGORY";
    cat.category = "Panadería";
    auto cp = env.make(cat);
    TEST_CHECK(cp.scope_kind == "CATEGORY" && cp.categories == std::vector<std::string>{"Panadería"} && cp.product_ids.empty(), "Alcance por categoría");

    // Bitácora: quién y cuándo
    TEST_CHECK(env.count_audit("PROMOTION_CREATED") >= 9 && env.count_audit("PROMOTION_UPDATED") == 1 &&
                   env.count_audit("PROMOTION_DEACTIVATED") == 1 && env.count_audit("PROMOTION_ACTIVATED") == 1, "Eventos de bitácora");
    TEST_CHECK(env.scalar("SELECT user_id FROM audit_events WHERE event_type = 'PROMOTION_UPDATED';") != "", "La bitácora guarda quién");
    TEST_CHECK(env.scalar("SELECT created_at FROM audit_events WHERE event_type = 'PROMOTION_UPDATED';") != "", "…y cuándo");
    TEST_CHECK(env.scalar("SELECT details FROM audit_events WHERE event_type = 'PROMOTION_UPDATED';").find("Diez renombrada") != std::string::npos, "…y de qué promoción");
    for (const auto& p : env.promo->list_promotions({}).value()) { seen(p.summary_text); seen(p.validity_text); }
}

static void test_ambiguity() {
    std::cout << "[TEST] B14: promociones ambiguas (misma prioridad y se cruzan) se rechazan..." << std::endl;
    Env env("ambiguas");
    auto base = env.make(env.percent("Base", 1000, {env.cafe}, 5));
    auto same = env.promo->create_promotion(env.percent("Choca", 2000, {env.cafe}, 5));
    TEST_CHECK(!same.is_success() && same.error_code() == ErrorCode::Conflict && same.error_message().find("«Base»") != std::string::npos, "Misma prioridad y mismo producto: " + same.error_message());
    seen(same.error_message());
    TEST_CHECK(env.promo->create_promotion(env.percent("Otra prioridad", 2000, {env.cafe}, 6)).is_success(), "Otra prioridad: se acepta");
    TEST_CHECK(env.promo->create_promotion(env.percent("Otro producto", 2000, {env.pan}, 5)).is_success(), "Mismo número, otro producto: se acepta");
    auto m = env.percent("Mañana", 2000, {env.galleta}, 7);
    m.time_from = "07:00"; m.time_to = "11:00";
    TEST_CHECK(env.promo->create_promotion(m).is_success(), "Horario mañana");
    auto t = env.percent("Tarde", 2000, {env.galleta}, 7);
    t.time_from = "11:00"; t.time_to = "15:00";
    TEST_CHECK(env.promo->create_promotion(t).is_success(), "Horario tarde (no se cruza: 11:00 es exclusivo en el primero)");
    auto o = env.percent("Se cruza", 2000, {env.galleta}, 7);
    o.time_from = "10:00"; o.time_to = "12:00";
    TEST_CHECK(env.promo->create_promotion(o).error_code() == ErrorCode::Conflict, "Horario que se cruza");
    auto d1 = env.percent("Días A", 1000, {env.galleta}, 8);
    d1.weekdays = {1, 2};
    auto d2 = env.percent("Días B", 1000, {env.galleta}, 8);
    d2.weekdays = {3, 4};
    TEST_CHECK(env.promo->create_promotion(d1).is_success() && env.promo->create_promotion(d2).is_success(), "Días distintos: se aceptan");
    auto d3 = env.percent("Días C", 1000, {env.galleta}, 8);
    d3.weekdays = {2, 3};
    TEST_CHECK(env.promo->create_promotion(d3).error_code() == ErrorCode::Conflict, "Días que se cruzan");
    auto f1 = env.percent("Fechas A", 1000, {env.galleta}, 9);
    f1.valid_to = "2026-11-30";
    auto f2 = env.percent("Fechas B", 1000, {env.galleta}, 9);
    f2.valid_from = "2026-12-01";
    TEST_CHECK(env.promo->create_promotion(f1).is_success() && env.promo->create_promotion(f2).is_success(), "Fechas que no se tocan");
    // Categoría contra producto
    auto cat = env.percent("Categoría", 1000, {}, 5);
    cat.scope_kind = "CATEGORY"; cat.category = "Bebidas";
    auto rc = env.promo->create_promotion(cat);
    TEST_CHECK(rc.error_code() == ErrorCode::Conflict && rc.error_message().find("«Base»") != std::string::npos, "Categoría contra un producto de esa categoría: " + rc.error_message());
    // Activar de nuevo choca
    env.deactivate_all();
    TEST_CHECK(env.promo->set_promotion_active(base.id, true).is_success(), "Activar la primera");
    auto other = env.promo->list_promotions({}).value();
    std::string choca_id;
    for (const auto& p : other) if (p.name == "Otra prioridad") choca_id = p.id;
    TEST_CHECK(env.promo->update_promotion({choca_id, env.percent("Otra prioridad", 2000, {env.cafe}, 5)}).error_code() == ErrorCode::Conflict, "Editar hacia una prioridad que choca");
    env.exec("UPDATE promotions SET priority = 5 WHERE id = '" + choca_id + "';");
    auto act = env.promo->set_promotion_active(choca_id, true);
    TEST_CHECK(act.error_code() == ErrorCode::Conflict && act.error_message().find("«Base»") != std::string::npos, "Activar una que choca: " + act.error_message());
    seen(act.error_message());
}

static void test_cart_discounts() {
    std::cout << "[TEST] B14: descuento en el carrito (porcentaje, monto fijo, NxM, prioridad, vigencia, nunca negativo)..." << std::endl;
    Env env("carrito");
    // Porcentaje
    auto p10 = env.make(env.percent("Diez café", 1000, {env.cafe}));
    env.add(env.cafe, 2);
    auto c = env.cart_now();
    TEST_CHECK(c.items.size() == 1 && c.items[0].subtotal.cents() == 6000 && c.items[0].line_discount.cents() == 600 && c.items[0].line_total.cents() == 5400 &&
                   c.items[0].promotion_name == "Diez café" && c.items[0].unit_price.cents() == 3000, "Línea: original, descuento y promoción");
    TEST_CHECK(c.subtotal.cents() == 6000 && c.discount.cents() == 600 && c.total.cents() == 5400, "Resumen: subtotal, descuento y total");
    // Redondeo del descuento: 33.3 -> 33 y 33.5 -> 34
    env.clear_cart();
    std::string odd1 = env.make_direct("O333", "Raro 3.33", "Raros", 333);
    std::string odd2 = env.make_direct("O335", "Raro 3.35", "Raros", 335);
    env.make(env.percent("Diez raros", 1000, {odd1, odd2}));
    env.add(odd1, 1);
    TEST_CHECK(env.cart_now().discount.cents() == 33, "3.33 con 10 % descuenta 0.33");
    env.clear_cart();
    env.add(odd2, 1);
    TEST_CHECK(env.cart_now().discount.cents() == 34 && env.cart_now().total.cents() == 301, "3.35 con 10 % descuenta 0.34 (mitad hacia arriba)");
    env.clear_cart();
    env.add(odd2, 3);
    TEST_CHECK(env.cart_now().discount.cents() == 101, "El redondeo es por línea: 10.05 al 10 % = 1.005 = 1.01");

    // Monto fijo por pieza, nunca más que el precio
    env.deactivate_all();
    env.clear_cart();
    env.make(env.fixed("Cinco menos pan", 500, {env.pan}));
    env.add(env.pan, 3);
    TEST_CHECK(env.cart_now().discount.cents() == 1500 && env.cart_now().total.cents() == 4500, "Monto fijo por pieza: 3 × 5.00");
    env.deactivate_all();
    env.make(env.fixed("Treinta menos pan", 3000, {env.pan}));
    {
        auto c2 = env.cart_now();
        TEST_CHECK(c2.discount.cents() == 6000 && c2.total.cents() == 0 && c2.items[0].line_total.cents() == 0, "El precio nunca baja de cero (descuento mayor que la pieza)");
    }
    // 100 %
    env.deactivate_all();
    env.make(env.percent("Gratis", 10000, {env.pan}));
    TEST_CHECK(env.cart_now().total.cents() == 0, "100 % deja la línea en cero, no negativa");

    // NxM con cantidades no múltiplo
    env.deactivate_all();
    env.clear_cart();
    env.make(env.nxm("Dos por uno", 2, 1, {env.cafe}));
    env.add(env.cafe, 1);
    TEST_CHECK(env.cart_now().discount.cents() == 0, "2x1 con 1 pieza: nada gratis");
    env.clear_cart();
    env.add(env.cafe, 3);
    TEST_CHECK(env.cart_now().discount.cents() == 3000 && env.cart_now().total.cents() == 6000, "2x1 con 3 piezas: 1 gratis");
    env.clear_cart();
    env.add(env.cafe, 5);
    TEST_CHECK(env.cart_now().discount.cents() == 6000, "2x1 con 5 piezas: 2 gratis");
    env.deactivate_all();
    env.make(env.nxm("Tres por dos", 3, 2, {env.cafe}));
    TEST_CHECK(env.cart_now().discount.cents() == 3000, "3x2 con 5 piezas: 1 gratis");
    env.clear_cart();
    env.add(env.cafe, 6);
    TEST_CHECK(env.cart_now().discount.cents() == 6000, "3x2 con 6 piezas: 2 gratis");
    // Las líneas del mismo producto (notas distintas) se cuentan juntas y el descuento se reparte en orden
    env.deactivate_all();
    env.exec("UPDATE promotions SET is_active = 0;");
    env.make(env.nxm("Dos por uno juntas", 2, 1, {env.cafe}));
    env.clear_cart();
    env.add(env.cafe, 1, "sin azúcar");
    env.add(env.cafe, 1, "con hielo");
    {
        auto c3 = env.cart_now();
        TEST_CHECK(c3.items.size() == 2 && c3.discount.cents() == 3000, "Dos líneas con notas distintas: se juntan para el 2x1");
        TEST_CHECK(c3.items[0].line_discount.cents() == 3000 && c3.items[1].line_discount.cents() == 0, "El descuento se reparte en orden de líneas (primero la primera)");
    }
    env.add(env.cafe, 2, "sin azúcar");
    {
        auto c4 = env.cart_now();
        // 4 piezas en total (1 + 1 + 2 que se funden con la primera línea): 2 gratis
        TEST_CHECK(c4.discount.cents() == 6000, "Cuatro piezas en total: dos gratis");
    }

    // Prioridad: solo una por línea, la de mayor prioridad; nunca se suman
    env.deactivate_all();
    env.clear_cart();
    env.make(env.percent("Baja", 5000, {env.cafe}, 1));
    env.make(env.percent("Alta", 1000, {env.cafe}, 9));
    env.make(env.fixed("Media", 2000, {env.cafe}, 5));
    env.add(env.cafe, 1);
    {
        auto c5 = env.cart_now();
        TEST_CHECK(c5.items[0].promotion_name == "Alta" && c5.discount.cents() == 300, "Gana la de mayor prioridad aunque descuente menos (10 % de 30.00)");
    }
    // Por categoría y por producto a la vez: gana la prioridad
    env.deactivate_all();
    env.clear_cart();
    auto cat = env.percent("Panadería 20", 2000, {}, 2);
    cat.scope_kind = "CATEGORY"; cat.category = "Panadería";
    env.make(cat);
    env.make(env.percent("Pan 50", 5000, {env.pan}, 3));
    env.add(env.pan, 1);
    env.add(env.galleta, 1);
    {
        auto c6 = env.cart_now();
        TEST_CHECK(c6.items[0].promotion_name == "Pan 50" && c6.items[0].line_discount.cents() == 1000, "Pan: gana la promoción del producto (prioridad 3)");
        TEST_CHECK(c6.items[1].promotion_name == "Panadería 20" && c6.items[1].line_discount.cents() == 200, "Galleta: le toca la de la categoría");
        TEST_CHECK(c6.discount.cents() == 1200 && c6.total.cents() == 3000 - 1200, "Descuento total del carrito");
    }
    // Inactiva no aplica
    env.deactivate_all();
    TEST_CHECK(env.cart_now().discount.cents() == 0, "Sin promociones activas: sin descuento");

    // Vigencia con el reloj inyectado
    env.clear_cart();
    auto w = env.percent("Mañanas entre semana", 1000, {env.cafe}, 1);
    w.weekdays = {1, 2, 3, 4, 5};
    w.time_from = "07:00"; w.time_to = "11:00";
    w.valid_from = "2026-11-01"; w.valid_to = "2026-11-30";
    env.make(w);
    env.add(env.cafe, 1);
    env.now = LocalNow{"2026-11-18", 3, 9 * 60 + 30};
    TEST_CHECK(env.cart_now().discount.cents() == 300, "Dentro de fecha, día y hora");
    env.now = LocalNow{"2026-11-18", 3, 11 * 60};
    TEST_CHECK(env.cart_now().discount.cents() == 0, "11:00 ya no (fin exclusivo)");
    env.now = LocalNow{"2026-11-18", 3, 6 * 60 + 59};
    TEST_CHECK(env.cart_now().discount.cents() == 0, "06:59 aún no");
    env.now = LocalNow{"2026-11-21", 6, 9 * 60};
    TEST_CHECK(env.cart_now().discount.cents() == 0, "Sábado: no");
    env.now = LocalNow{"2026-12-01", 2, 9 * 60};
    TEST_CHECK(env.cart_now().discount.cents() == 0, "Diciembre: fuera de fechas");
    env.now = LocalNow{"2026-11-30", 1, 7 * 60};
    TEST_CHECK(env.cart_now().discount.cents() == 300, "Último día y primera hora: sí");
    env.now = LocalNow{"2026-11-18", 3, 9 * 60 + 30};
}

static void test_sale_with_discount() {
    std::cout << "[TEST] B14: venta con descuento: una transacción, instantánea, ticket, reporte, idempotencia, bitácora..." << std::endl;
    Env env("venta");
    auto promo = env.make(env.percent("Diez café", 1000, {env.cafe}));
    env.make(env.nxm("Dos por uno pan", 2, 1, {env.pan}));
    env.add(env.cafe, 2);
    env.add(env.pan, 3);
    auto summary = env.cart_now();
    // café 2 × 30.00 = 60.00 − 6.00; pan 3 × 20.00 = 60.00 − 20.00 (2x1: 1 gratis)
    TEST_CHECK(summary.subtotal.cents() == 12000 && summary.discount.cents() == 2600 && summary.total.cents() == 9400, "Vista previa del carrito");
    auto sale = env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), "venta-b14-1");
    TEST_CHECK(sale.is_success(), "confirm_sale: " + sale.error_message());
    TEST_CHECK(sale.value().total.cents() == 9400 && sale.value().change.cents() == 600, "Total neto y cambio sobre el neto");
    const std::string folio = sale.value().folio;
    TEST_CHECK(env.scalar("SELECT discount_cents FROM sales WHERE folio = '" + folio + "';") == "2600", "sales.discount_cents");
    TEST_CHECK(env.scalar("SELECT total_cents FROM sales WHERE folio = '" + folio + "';") == "9400", "sales.total_cents es el neto");
    TEST_CHECK(env.scalar("SELECT amount_cents FROM payment_records;") == "9400", "El pago declarado es el neto");
    TEST_CHECK(env.scalar("SELECT amount_cents FROM cash_movements WHERE movement_type = 'SALE';") == "9400", "El movimiento de caja es el neto");
    TEST_CHECK(env.scalar("SELECT COUNT(*) FROM sale_items;") == "2" && env.scalar("SELECT SUM(discount_cents) FROM sale_items;") == "2600", "Descuento por línea suma el total");
    TEST_CHECK(env.scalar("SELECT promotion_name || '|' || promotion_kind || '|' || promotion_params FROM sale_items WHERE product_name = 'Café';") == "Diez café|PERCENT|bp=1000", "Instantánea del porcentaje");
    TEST_CHECK(env.scalar("SELECT promotion_name || '|' || promotion_kind || '|' || promotion_params FROM sale_items WHERE product_name = 'Pan';") == "Dos por uno pan|NXM|n=2;m=1", "Instantánea del NxM");
    TEST_CHECK(env.cart_now().items.empty(), "El carrito queda vacío");
    // Bitácora de la venta con el descuento y las promociones por nombre
    const auto audit = env.scalar("SELECT details FROM audit_events WHERE event_type = 'SALE_CONFIRMED';");
    TEST_CHECK(audit.find("$26.00") != std::string::npos && audit.find("Diez café") != std::string::npos && audit.find("Dos por uno pan") != std::string::npos, "Bitácora de la venta: " + audit);

    // Ticket: instantánea
    auto ticket = env.sale->get_ticket(folio).value();
    TEST_CHECK(ticket.subtotal.cents() == 12000 && ticket.discount.cents() == 2600 && ticket.total.cents() == 9400, "Ticket: subtotal, descuento y total");
    TEST_CHECK(ticket.items.size() == 2 && ticket.items[0].discount.cents() == 600 && ticket.items[0].promotion_name == "Diez café" &&
                   ticket.items[1].discount.cents() == 2000 && ticket.items[1].promotion_name == "Dos por uno pan", "Ticket: descuento y promoción por línea");
    const std::string text42 = ticket.to_formatted_text(42);
    TEST_CHECK(text42.find("Desc. Diez café") != std::string::npos && text42.find("-$6.00") != std::string::npos && text42.find("Descuento:") != std::string::npos, "El texto del ticket muestra el descuento");

    // Editar la promoción después NO cambia la venta ni su ticket
    TEST_CHECK(env.promo->update_promotion({promo.id, env.percent("Cambiada", 5000, {env.cafe})}).is_success(), "Editar la promoción");
    auto ticket2 = env.sale->get_ticket(folio).value();
    TEST_CHECK(ticket2.discount.cents() == 2600 && ticket2.items[0].discount.cents() == 600 && ticket2.items[0].promotion_name == "Diez café" && ticket2.total.cents() == 9400,
               "La venta pasada conserva la promoción tal como era");
    TEST_CHECK(env.scalar("SELECT promotion_params FROM sale_items WHERE product_name = 'Café';") == "bp=1000", "La instantánea guarda los parámetros de entonces");

    // Reintento idempotente tras editar: devuelve la venta guardada, sin duplicar ni conflicto
    env.add(env.cafe, 1);  // un carrito nuevo no cambia el reintento
    auto retry = env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), "venta-b14-1");
    TEST_CHECK(retry.is_success() && retry.value().folio == folio && retry.value().total.cents() == 9400, "Reintento: la misma venta");
    TEST_CHECK(env.scalar("SELECT COUNT(*) FROM sales;") == "1" && env.scalar("SELECT COUNT(*) FROM payment_records;") == "1", "Sin duplicados");
    env.clear_cart();

    // Reporte: neto = bruto - descuento y los pagos cuadran
    const std::string today = app::domain::now().local.substr(0, 10);
    auto rep = env.report->get_daily_sales_summary(today);
    TEST_CHECK(rep.is_success(), "Reporte: " + rep.error_message());
    TEST_CHECK(rep.value().total_amount.cents() == 9400 && rep.value().gross_amount.cents() == 12000 && rep.value().discount_amount.cents() == 2600, "Reporte: neto, bruto y descuento");
    TEST_CHECK(rep.value().gross_amount.cents() - rep.value().discount_amount.cents() == rep.value().total_amount.cents(), "bruto - descuento = neto");
    TEST_CHECK(rep.value().payments_mismatch_cents == 0 && rep.value().cash_amount.cents() == 9400, "Los pagos cuadran con el neto");
    // Venta sin promoción en el mismo día
    env.deactivate_all();
    env.add(env.galleta, 1);
    TEST_CHECK(env.sale->confirm_sale(PaymentMethod::Tarjeta, Money::zero(), "venta-b14-2").is_success(), "Venta sin promoción");
    auto rep2 = env.report->get_daily_sales_summary(today).value();
    TEST_CHECK(rep2.total_amount.cents() == 10400 && rep2.gross_amount.cents() == 13000 && rep2.discount_amount.cents() == 2600 && rep2.payments_mismatch_cents == 0, "Reporte con dos ventas");
    // Corte de caja: lo vendido en efectivo es el neto
    auto closed = env.shift->close_shift(Money::from_cents(50000 + 9400));
    TEST_CHECK(closed.is_success(), "Cierre de turno con el efectivo neto: " + closed.error_message());
}

static void test_changed_promotions_at_checkout() {
    std::cout << "[TEST] B14: promociones que cambian entre el carrito y el cobro: se rechaza sin guardar nada..." << std::endl;
    Env env("cambio");
    env.make(env.percent("Mañanas", 1000, {env.cafe}));
    {   // vence entre la vista previa y el cobro
        auto w = env.percent("Ventana", 5000, {env.pan}, 1);
        w.time_from = "09:00"; w.time_to = "10:00";
        env.make(w);
    }
    env.add(env.cafe, 1);
    env.add(env.pan, 1);
    TEST_CHECK(env.cart_now().total.cents() == 2700 + 1000, "Vista previa con ventana abierta (09:30)");
    env.now = LocalNow{"2026-11-18", 3, 10 * 60};  // la ventana se cerró
    const std::string stock_before = env.scalar("SELECT COUNT(*) FROM stock_movements;");
    auto r = env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), "k-cambio-1");
    TEST_CHECK(!r.is_success() && r.error_code() == ErrorCode::Conflict &&
                   r.error_message() == "Las promociones cambiaron desde que se armó la cuenta. Revise el total antes de cobrar.", "Rechazo: " + r.error_message());
    seen(r.error_message());
    TEST_CHECK(env.scalar("SELECT COUNT(*) FROM sales;") == "0" && env.scalar("SELECT COUNT(*) FROM payment_records;") == "0" &&
                   env.scalar("SELECT COUNT(*) FROM cash_movements WHERE movement_type = 'SALE';") == "0" && env.scalar("SELECT COUNT(*) FROM stock_movements;") == stock_before &&
                   env.scalar("SELECT COUNT(*) FROM sale_items;") == "0", "No se guardó nada");
    // El carrito sigue intacto; el cajero ve el total nuevo (el cobro lo actualiza) y vuelve a cobrar
    TEST_CHECK(env.scalar("SELECT COUNT(*) FROM cart_items;") == "2", "El carrito sigue con sus líneas");
    auto fresh = env.cart_now();
    TEST_CHECK(fresh.total.cents() == 2700 + 2000, "El carrito muestra el total nuevo: " + std::to_string(fresh.total.cents()));
    auto ok = env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), "k-cambio-1");
    TEST_CHECK(ok.is_success() && ok.value().total.cents() == 4700, "Volver a cobrar con la misma clave: " + ok.error_message());
    TEST_CHECK(env.scalar("SELECT COUNT(*) FROM sales;") == "1", "Una sola venta");

    // Una promoción editada también se detecta
    env.add(env.cafe, 1);
    env.cart_now();
    auto promos = env.promo->list_promotions({}).value();
    std::string id;
    for (const auto& p : promos) if (p.name == "Mañanas") id = p.id;
    TEST_CHECK(env.promo->update_promotion({id, env.percent("Mañanas", 2000, {env.cafe})}).is_success(), "Editar el porcentaje");
    auto r2 = env.sale->confirm_sale(PaymentMethod::Tarjeta, Money::zero(), "k-cambio-2");
    TEST_CHECK(!r2.is_success() && r2.error_code() == ErrorCode::Conflict, "Promoción editada: rechazo");
    env.cart_now();
    TEST_CHECK(env.sale->confirm_sale(PaymentMethod::Tarjeta, Money::zero(), "k-cambio-2").is_success(), "Cobrar de nuevo");
    // Y una que empieza
    env.add(env.galleta, 1);
    env.cart_now();
    env.make(env.percent("Nueva", 1000, {env.galleta}));
    TEST_CHECK(env.sale->confirm_sale(PaymentMethod::Tarjeta, Money::zero(), "k-cambio-3").error_code() == ErrorCode::Conflict, "Promoción que empieza: rechazo");
    env.cart_now();
    TEST_CHECK(env.sale->confirm_sale(PaymentMethod::Tarjeta, Money::zero(), "k-cambio-3").is_success(), "Cobrar de nuevo");
    // Un reintento idempotente de una venta ya guardada sigue devolviéndola aunque las promociones cambien
    env.exec("UPDATE promotions SET is_active = 0;");
    auto again = env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), "k-cambio-1");
    TEST_CHECK(again.is_success() && again.value().folio == ok.value().folio, "Reintento idempotente tras cambios: la venta guardada");
}

static void test_combos() {
    std::cout << "[TEST] B14: combos: reglas, descuento de cada parte, bloqueo sin existencia, costo y margen..." << std::endl;
    Env env("combos");
    // Ingredientes y productos con existencia controlada
    CreateIngredientDto cafe_ing{"Café molido", "MASS", "mg", 0};
    auto ing_cafe = env.inventory->create_ingredient(cafe_ing).value();
    CreateProductDto americano;
    americano.sku = "AMER"; americano.name = "Americano"; americano.category = "Bebidas"; americano.price = Money::from_cents(3500);
    americano.is_prepared = true;
    auto prod_am = env.catalog->create_product(americano).value();
    TEST_CHECK(env.catalog->set_recipe(prod_am.id, {{ing_cafe.id, "", 15000, "mg"}}).is_success(), "Receta del americano");
    CreateProductDto pan_p;
    pan_p.sku = "PAN2"; pan_p.name = "Pan dulce"; pan_p.category = "Panadería"; pan_p.price = Money::from_cents(2000);  // directo con existencia por omisión
    auto prod_pan = env.catalog->create_product(pan_p).value();
    TEST_CHECK(prod_pan.stock_tracked && !prod_pan.is_prepared, "El producto directo controla existencia por omisión");
    auto pan_ing_id = env.catalog->get_recipe(prod_pan.id).value().items.at(0).ingredient_id;

    // Validaciones
    CreateComboDto combo;
    combo.sku = "DESAYUNO"; combo.name = "Desayuno"; combo.category = "Combos"; combo.price = Money::from_cents(4500);
    combo.components = {{prod_am.id, "", 1}, {prod_pan.id, "", 2}};
    auto bad = [&](const std::string& what, CreateComboDto c, ErrorCode code) {
        auto r = env.promo->create_combo(c);
        TEST_CHECK(!r.is_success() && r.error_code() == code, what + ": " + r.error_message());
        seen(r.error_message());
    };
    { auto c = combo; c.components.clear(); bad("sin componentes", c, ErrorCode::InvalidInput); }
    { auto c = combo; c.components[0].quantity = 0; bad("cantidad 0", c, ErrorCode::InvalidInput); }
    { auto c = combo; c.components[0].quantity = 100; bad("cantidad 100", c, ErrorCode::InvalidInput); }
    { auto c = combo; c.components.push_back({prod_am.id, "", 1}); bad("componente repetido", c, ErrorCode::InvalidInput); }
    { auto c = combo; c.components[0].product_id = "no-existe"; bad("componente inexistente", c, ErrorCode::NotFound); }
    { auto c = combo; c.sku = ""; bad("sin SKU", c, ErrorCode::InvalidInput); }
    { auto c = combo; c.name = ""; bad("sin nombre", c, ErrorCode::InvalidInput); }
    { auto c = combo; c.price = Money::from_cents(-1); bad("precio negativo", c, ErrorCode::InvalidInput); }
    { auto c = combo; c.sku = "AMER"; bad("SKU repetido", c, ErrorCode::Conflict); }
    auto created = env.promo->create_combo(combo);
    TEST_CHECK(created.is_success(), "Crear combo: " + created.error_message());
    const auto cid = created.value().product_id;
    TEST_CHECK(created.value().price.cents() == 4500 && created.value().components.size() == 2 && created.value().components[1].quantity == 2 &&
                   created.value().components[0].product_name == "Americano" && created.value().is_active, "El combo guarda precio y partes");
    // Un combo no contiene a otro combo ni a sí mismo
    { auto c = combo; c.sku = "ANIDADO"; c.components = {{cid, "", 1}}; bad("combo dentro de combo", c, ErrorCode::InvalidInput); }
    {
        UpdateComboDto u{cid, "Desayuno", "Combos", Money::from_cents(4500), {{cid, "", 1}}};
        auto r = env.promo->update_combo(u);
        TEST_CHECK(r.error_code() == ErrorCode::InvalidInput && r.error_message().find("a sí mismo") != std::string::npos, "Un combo no puede contenerse a sí mismo: " + r.error_message());
        seen(r.error_message());
    }
    // Un componente dado de baja
    env.catalog->deactivate_product(prod_am.id);
    { auto c = combo; c.sku = "BAJA"; bad("componente dado de baja", c, ErrorCode::InvalidInput); }
    env.catalog->reactivate_product(prod_am.id);

    // El catálogo lo ve como producto combo
    auto pd = env.catalog->get_product_by_id(cid).value();
    TEST_CHECK(pd.is_combo && pd.price.cents() == 4500 && pd.stock_tracked, "ProductDto: is_combo y stock_tracked");
    TEST_CHECK(env.catalog->set_recipe(cid, {{ing_cafe.id, "", 1, "mg"}}).error_code() == ErrorCode::InvalidState, "Un combo no tiene receta propia");
    TEST_CHECK(env.promo->list_combos(false).value().size() == 1 && env.promo->get_combo(cid).is_success(), "list_combos y get_combo");
    TEST_CHECK(env.promo->get_combo(prod_am.id).error_code() == ErrorCode::NotFound, "Un producto que no es combo no es combo");

    // Existencias: sin compras, el combo no se puede vender
    TEST_CHECK(env.promo->get_combo(cid).value().stock_units == 0, "Sin existencia: 0 combos");
    env.add(cid, 1);
    auto blocked = env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), "k-combo-0");
    TEST_CHECK(!blocked.is_success() && blocked.error_code() == ErrorCode::InsufficientStock, "Sin existencia la venta se bloquea: " + blocked.error_message());
    env.clear_cart();

    // Compras: café 1 kg (por $200.00) y 10 panes (por $50.00)
    TEST_CHECK(env.inventory->register_purchase_entry(ing_cafe.id, 1, "kg", Money::from_cents(20000)).is_success(), "Compra de café");
    TEST_CHECK(env.inventory->register_purchase_entry(pan_ing_id, 10, "pz", Money::from_cents(5000)).is_success(), "Compra de pan");
    TEST_CHECK(env.promo->get_combo(cid).value().stock_units == 5, "Con 10 panes (2 por combo) alcanzan 5 combos");
    env.add(cid, 2);
    {
        auto c = env.cart_now();
        TEST_CHECK(c.total.cents() == 9000 && c.items[0].name == "Desayuno", "El combo cobra su precio fijo");
    }
    auto sold = env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), "k-combo-1");
    TEST_CHECK(sold.is_success(), "Vender 2 combos: " + sold.error_message());
    TEST_CHECK(env.inventory->get_ingredient(ing_cafe.id).value().current_stock_base == 1000000 - 2 * 15000, "Descontó el café de la receta × 2");
    TEST_CHECK(env.inventory->get_ingredient(pan_ing_id).value().current_stock_base == 10 - 2 * 2, "Descontó 2 panes × 2 combos");
    TEST_CHECK(env.scalar("SELECT COUNT(*) FROM stock_movements WHERE movement_type = 'SALE' AND reason LIKE 'Venta %';") == "2", "Un movimiento por ingrediente");
    TEST_CHECK(env.scalar("SELECT COUNT(*) FROM sale_items;") == "1", "El combo es UNA línea de venta");

    // Falta una parte: la venta se bloquea y no guarda nada
    TEST_CHECK(env.inventory->register_waste(pan_ing_id, 4, "pz", "se cayeron").is_success(), "Merma: quedan 2 panes");
    env.add(cid, 2);  // necesita 4 panes
    const std::string sales_before = env.scalar("SELECT COUNT(*) FROM sales;");
    const std::string cafe_before = std::to_string(env.inventory->get_ingredient(ing_cafe.id).value().current_stock_base);
    auto short_pan = env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), "k-combo-2");
    TEST_CHECK(!short_pan.is_success() && short_pan.error_code() == ErrorCode::InsufficientStock && short_pan.error_message().find("Pan dulce") != std::string::npos, "Falta pan: " + short_pan.error_message());
    TEST_CHECK(env.scalar("SELECT COUNT(*) FROM sales;") == sales_before && std::to_string(env.inventory->get_ingredient(ing_cafe.id).value().current_stock_base) == cafe_before &&
                   env.inventory->get_ingredient(pan_ing_id).value().current_stock_base == 2, "No se descontó nada (ni el café, que sí alcanzaba)");
    env.clear_cart();
    env.add(cid, 1);
    TEST_CHECK(env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), "k-combo-3").is_success(), "Con lo que alcanza, sí se vende");

    // Componente dado de baja bloquea la venta del combo
    env.add(cid, 1);
    env.catalog->deactivate_product(prod_pan.id);
    auto inactive = env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(10000), "k-combo-4");
    TEST_CHECK(inactive.error_code() == ErrorCode::Conflict && inactive.error_message().find("dado de baja") != std::string::npos, "Componente dado de baja: " + inactive.error_message());
    seen(inactive.error_message());
    env.catalog->reactivate_product(prod_pan.id);
    env.clear_cart();

    // Editar el combo
    UpdateComboDto upd{cid, "Desayuno completo", "Combos", Money::from_cents(5000), {{prod_am.id, "", 1}}};
    auto edited = env.promo->update_combo(upd);
    TEST_CHECK(edited.is_success() && edited.value().components.size() == 1 && edited.value().price.cents() == 5000 && edited.value().name == "Desayuno completo", "Editar el combo");

    // Costo y margen
    auto costing = env.promo->get_combo_costing(cid);
    TEST_CHECK(costing.is_success(), "Costo: " + costing.error_message());
    // 15 g de café a $200/kg = $3.00 por porción
    TEST_CHECK(costing.value().all_costs_known && costing.value().cost_total.cents() == 300 && costing.value().margin.cents() == 4700 && costing.value().margin_known &&
                   costing.value().margin_percent_known && costing.value().margin_basis_points == 9400, "Costo y margen del combo (café)");
    UpdateComboDto both{cid, "Desayuno completo", "Combos", Money::from_cents(5000), {{prod_am.id, "", 1}, {prod_pan.id, "", 2}}};
    TEST_CHECK(env.promo->update_combo(both).is_success(), "Combo con pan");
    auto c2 = env.promo->get_combo_costing(cid).value();
    // pan: 10 pz por $50.00 = $5.00 c/u; 2 piezas = $10.00; total $13.00
    TEST_CHECK(c2.all_costs_known && c2.cost_total.cents() == 1300 && c2.lines.size() == 2 && c2.lines[1].cost.cents() == 1000, "Costo con dos partes");
    // Parte con costo desconocido
    CreateIngredientDto sin_compra{"Mermelada", "MASS", "mg", 0};
    auto ing_m = env.inventory->create_ingredient(sin_compra).value();
    CreateProductDto tostada;
    tostada.sku = "TOST"; tostada.name = "Tostada"; tostada.category = "Panadería"; tostada.price = Money::from_cents(1500); tostada.is_prepared = true;
    auto prod_t = env.catalog->create_product(tostada).value();
    TEST_CHECK(env.catalog->set_recipe(prod_t.id, {{ing_m.id, "", 10000, "mg"}}).is_success(), "Receta de la tostada");
    UpdateComboDto withunk{cid, "Desayuno completo", "Combos", Money::from_cents(5000), {{prod_am.id, "", 1}, {prod_t.id, "", 1}}};
    TEST_CHECK(env.promo->update_combo(withunk).is_success(), "Combo con costo desconocido");
    auto c3 = env.promo->get_combo_costing(cid).value();
    TEST_CHECK(!c3.all_costs_known && !c3.margin_known && c3.unknown_component_names == std::vector<std::string>{"Tostada"} && c3.known_cost_subtotal.cents() == 300, "El costo desconocido no se cuenta como cero");
    // Bitácora
    TEST_CHECK(env.count_audit("COMBO_CREATED") == 1 && env.count_audit("COMBO_UPDATED") == 3, "Bitácora de combos");
    TEST_CHECK(env.scalar("SELECT user_id FROM audit_events WHERE event_type = 'COMBO_CREATED';") != "", "…con quién");

    // Promoción sobre un combo
    env.make(env.percent("Combo -10", 1000, {cid}));
    env.add(cid, 1);
    TEST_CHECK(env.cart_now().discount.cents() == 500, "Un combo es un producto: admite promociones");
}

static void test_direct_products_stock() {
    std::cout << "[TEST] B14: productos directos con existencia enlazada y sin ella; Existencias..." << std::endl;
    Env env("directos");
    CreateProductDto p;
    p.sku = "AGUA"; p.name = "Agua 600 ml"; p.category = "Bebidas"; p.price = Money::from_cents(1500);
    auto agua = env.catalog->create_product(p);
    TEST_CHECK(agua.is_success() && agua.value().stock_tracked && !agua.value().is_prepared, "Crear directo: enlaza existencia por omisión");
    auto ings = env.inventory->list_ingredients().value();
    TEST_CHECK(ings.size() == 1 && ings[0].name == "Agua 600 ml" && ings[0].unit_dimension == "COUNT" && ings[0].base_unit == "pz" && ings[0].current_stock_base == 0, "Ingrediente por piezas con el mismo nombre");
    TEST_CHECK(ings[0].stock_status == "OUT" && ings[0].last_movement_at.empty(), "Existencias: sin existencia y sin movimientos");
    auto recipe = env.catalog->get_recipe(agua.value().id).value();
    TEST_CHECK(recipe.items.size() == 1 && recipe.items[0].ingredient_id == ings[0].id && recipe.items[0].quantity_base_units == 1, "Receta de 1 pieza");
    TEST_CHECK(env.catalog->get_product_by_id(agua.value().id).value().stock_tracked && !env.catalog->get_product_by_id(agua.value().id).value().is_prepared, "Sigue siendo directo");

    // Sin existencia no se vende; con compra, sí y descuenta
    env.add(agua.value().id, 1);
    TEST_CHECK(env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(2000), "k-agua-0").error_code() == ErrorCode::InsufficientStock, "Sin existencia: bloqueada");
    env.clear_cart();
    TEST_CHECK(env.inventory->register_purchase_entry(ings[0].id, 12, "pz", Money::from_cents(6000)).is_success(), "Compra de 12 aguas");
    auto after = env.inventory->get_ingredient(ings[0].id).value();
    TEST_CHECK(after.current_stock_base == 12 && after.stock_status == "OK" && !after.last_movement_at.empty(), "Existencias: con existencia y último movimiento");
    TEST_CHECK(env.catalog->get_product_by_id(agua.value().id).value().stock_units == 12, "stock_units del producto directo");
    env.add(agua.value().id, 2);
    TEST_CHECK(env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(5000), "k-agua-1").is_success(), "Venta");
    TEST_CHECK(env.inventory->get_ingredient(ings[0].id).value().current_stock_base == 10, "Descontó 2 piezas");
    TEST_CHECK(env.inventory->update_low_stock_threshold(ings[0].id, 10).is_success() && env.inventory->get_ingredient(ings[0].id).value().stock_status == "LOW", "Estado LOW al llegar al mínimo");
    TEST_CHECK(env.inventory->list_ingredients().value()[0].stock_status == "LOW", "list_ingredients trae el estado");
    auto search = env.catalog->search_products("Agua", std::nullopt, 0, 10).value();
    TEST_CHECK(search.size() == 1 && search[0].stock_tracked && search[0].stock_units == 10, "search_products trae stock_tracked y stock_units");

    // Sin existencia controlada: se vende como antes
    CreateProductDto q;
    q.sku = "DULCE"; q.name = "Dulce suelto"; q.category = "Dulces"; q.price = Money::from_cents(500); q.track_stock = false;
    auto dulce = env.catalog->create_product(q);
    TEST_CHECK(dulce.is_success() && !dulce.value().stock_tracked, "track_stock = false: sin existencia controlada");
    TEST_CHECK(env.inventory->list_ingredients().value().size() == 1, "No se creó ingrediente");
    env.add(dulce.value().id, 3);
    TEST_CHECK(env.sale->confirm_sale(PaymentMethod::Efectivo, Money::from_cents(2000), "k-dulce-1").is_success(), "Se vende sin descontar, como antes");
    // Dar existencia después
    TEST_CHECK(env.catalog->enable_stock_for_direct_product(dulce.value().id).is_success(), "enable_stock_for_direct_product");
    TEST_CHECK(env.catalog->get_product_by_id(dulce.value().id).value().stock_tracked && env.inventory->list_ingredients().value().size() == 2, "Ahora controla existencia (queda en 0)");
    TEST_CHECK(env.catalog->enable_stock_for_direct_product(dulce.value().id).error_code() == ErrorCode::Conflict, "Ya la controla");
    TEST_CHECK(env.catalog->enable_stock_for_direct_product("no-existe").error_code() == ErrorCode::NotFound, "Producto inexistente");
    // Enlazar con un ingrediente existente
    CreateIngredientDto piezas{"Caja de galletas", "COUNT", "pz", 0};
    auto caja = env.inventory->create_ingredient(piezas).value();
    CreateIngredientDto masa{"Harina", "MASS", "mg", 0};
    auto harina = env.inventory->create_ingredient(masa).value();
    CreateProductDto g;
    g.sku = "GALL2"; g.name = "Galleta suelta"; g.category = "Dulces"; g.price = Money::from_cents(800); g.link_ingredient_id = caja.id;
    auto gall = env.catalog->create_product(g);
    TEST_CHECK(gall.is_success() && gall.value().stock_tracked && env.catalog->get_recipe(gall.value().id).value().items[0].ingredient_id == caja.id, "Enlazar un ingrediente existente");
    CreateProductDto g2 = g;
    g2.sku = "GALL3"; g2.name = "Otra galleta"; g2.link_ingredient_id = harina.id;
    auto bad_link = env.catalog->create_product(g2);
    TEST_CHECK(!bad_link.is_success() && bad_link.error_code() == ErrorCode::InvalidInput, "Un ingrediente que no se cuenta por piezas se rechaza: " + bad_link.error_message());
    seen(bad_link.error_message());
    TEST_CHECK(env.catalog->search_products("Otra galleta", std::nullopt, 0, 10).value().empty(), "Y no deja el producto a medias");
    // Nombre de ingrediente repetido: se distingue con el SKU
    CreateProductDto n1;
    n1.sku = "HAR"; n1.name = "Harina"; n1.category = "Insumos"; n1.price = Money::from_cents(100);
    auto har = env.catalog->create_product(n1);
    TEST_CHECK(har.is_success() && env.inventory->get_ingredient(env.catalog->get_recipe(har.value().id).value().items[0].ingredient_id).value().name == "Harina (HAR)", "Ingrediente con nombre repetido: se distingue con el SKU");
    // Preparado y combo
    CreateProductDto prep;
    prep.sku = "PREP"; prep.name = "Platillo"; prep.category = "Cocina"; prep.price = Money::from_cents(5000); prep.is_prepared = true;
    auto pr = env.catalog->create_product(prep).value();
    TEST_CHECK(!pr.stock_tracked, "Un preparado sin receta aún no controla existencia");
    TEST_CHECK(env.catalog->enable_stock_for_direct_product(pr.id).error_code() == ErrorCode::InvalidState, "Un preparado usa su receta");
    // Bitácora
    TEST_CHECK(env.count_audit("PRODUCT_STOCK_ENABLED") == 1, "Bitácora de existencia");
    // Mermas y ajustes con motivo (Barra)
    TEST_CHECK(env.inventory->register_waste(ings[0].id, 1, "pz", "se rompió").is_success() && env.inventory->get_ingredient(ings[0].id).value().current_stock_base == 9, "Merma con motivo");
    TEST_CHECK(env.inventory->register_waste(ings[0].id, 1, "pz", "").error_code() != ErrorCode::Forbidden && !env.inventory->register_waste(ings[0].id, 1, "pz", "").is_success(), "La merma exige motivo");
}

static void test_plain_language_and_leftovers() {
    std::cout << "[TEST] B14: lenguaje llano en todo lo que se muestra..." << std::endl;
    const std::vector<std::string> banned = {"PERCENT", "FIXED", "NXM", "basis points", "puntos base", "CATEGORY", "PRODUCT", "Forbidden", "InvalidInput",
                                             "inventory.promotions", "inventory.manage_products", "SQL", "NULL", "id", "bp"};
    std::vector<std::string> unique = g_messages;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    TEST_CHECK(unique.size() >= 25, "Se revisaron al menos 25 mensajes distintos (fueron " + std::to_string(unique.size()) + ")");
    for (const auto& m : unique) {
        for (const auto& w : banned) {
            TEST_CHECK(!contains_word_ci(m, w), "El mensaje \"" + m + "\" contiene la palabra técnica \"" + w + "\"");
        }
        TEST_CHECK(m.find("::") == std::string::npos && m.find('_') == std::string::npos && m.find("0x") == std::string::npos, "Sin códigos internos: " + m);
    }
}

int main() {
    std::error_code ec;
    std::random_device rd;
    g_root = fs::temp_directory_path() / fs::u8path("cafeteria b14 promos " + std::to_string(rd()) + " ñ");
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);

    test_pure_functions();
    test_permissions();
    test_promotion_crud_and_validation();
    test_ambiguity();
    test_cart_discounts();
    test_sale_with_discount();
    test_changed_promotions_at_checkout();
    test_combos();
    test_direct_products_stock();
    test_plain_language_and_leftovers();

    std::cout << "Pruebas de promociones y combos (B14) correctas. TEST_CHECK ejecutados: " << g_checks << std::endl;
    fs::remove_all(g_root, ec);
    return 0;
}
