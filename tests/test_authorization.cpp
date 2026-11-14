// Pruebas B8 / punto 3: autorización dentro de cada servicio real.
// Recorre TODOS los métodos de los contratos (lista escrita a mano desde include/app/contracts/):
//   - sin sesión: todos rechazan (salvo los públicos por diseño, listados abajo) y nada se escribe;
//   - con sesión de un rol SIN permisos: todos rechazan con Forbidden (salvo los que solo exigen
//     sesión, listados abajo) y nada se escribe (comprobado con sqlite3_total_changes);
//   - con el Dueño: los métodos con permiso NO responden Forbidden (control positivo: los rechazos de
//     arriba se deben a la autorización y no a argumentos mal formados).
//
// Públicos sin sesión por diseño:
//   has_owner            el arranque decide si pedir el alta del Dueño (solo lectura de un booleano);
//   create_initial_owner solo funciona si NO hay un Dueño activo (aquí ya existe: debe rechazar);
//   login                es la puerta de entrada; escribe auditoría y el contador de intentos a propósito;
//   current_user / is_authenticated / has_permission  consultan la sesión, no datos de negocio;
//   logout               sin sesión no hace nada.
// Solo exigen sesión (sin permiso específico; el contrato nunca les pidió uno):
//   get_user_avatar, list_roles, get_role_by_id, can_deactivate_user, record_audit_event,
//   get_product_by_id, get_product_by_sku_or_barcode, list_categories, get_product_image, get_recipe,
//   get_current_shift, get_station_config, get_status (respaldos),
//   y los de uso propio: change_password y get_user_by_id sobre la cuenta de la propia sesión.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

#include "sqlite3.h"
#include "app/services/mock_services.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/storage/sqlite_database.hpp"

namespace fs = std::filesystem;
using namespace app::services;
using namespace app::storage;
using namespace app::contracts;

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
            std::cerr << "\n[FALLO DE PRUEBA DE AUTORIZACIÓN] " << (message) \
                      << "\n  Condición: " << #condition \
                      << "\n  Archivo:   " << __FILE__ \
                      << "\n  Línea:     " << __LINE__ << "\n" << std::endl; \
            cleanup_and_exit(1); \
        } \
    } while (0)

namespace {

struct Out {
    bool ok;
    ErrorCode code;
};
template <class T> Out out(const Result<T>& r) { return {r.is_success(), r.error_code()}; }
Out out(const OperationResult& r) { return {r.is_success(), r.error_code()}; }

enum class Kind {
    Permission,   // exige sesión y un permiso
    SessionOnly,  // exige sesión, sin permiso específico
    Public        // público sin sesión por diseño (se prueba aparte)
};

struct Case {
    std::string name;
    Kind kind;
    std::function<Out()> call;
    bool owner_expected_not_forbidden{true};  // control positivo con el Dueño
    bool may_write{false};                    // el método escribe a propósito (p. ej. auditoría)
};

const std::string kOwnerPwd = "Clave-Dueno-B8-1";
const std::string kNoPermPwd = "Clave-SinPermisos-1";

} // namespace

int main() {
    std::error_code ec;
    g_root = fs::temp_directory_path(ec) /
             fs::u8path(u8"cafeteria b8 autorización ñ " +
                        std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);

    SqliteDatabase db;
    TEST_CHECK(db.open(g_root / "cafeteria.db").is_success(), "Abrir base");
    {
        MigrationRunner runner(db);
        auto m = runner.run_pending_migrations_from_dir(fs::u8path(SOURCE_MIGRATIONS_DIR));
        TEST_CHECK(m.is_success(), "Migraciones: " + m.error_message());
    }
    auto reg = ServiceRegistry::create_sqlite_registry(db, g_root.u8string(), SOURCE_MIGRATIONS_DIR);
    auto account = reg.account_service;
    auto catalog = reg.catalog_service;
    auto inventory = reg.inventory_service;
    auto cart = reg.cart_service;
    auto sale = reg.sale_service;
    auto shift = reg.shift_service;
    auto report = reg.report_service;
    auto backup = reg.backup_service;
    auto print = reg.print_config_service;
    TEST_CHECK(account && catalog && inventory && cart && sale && shift && report && backup && print,
               "El registro real debe traer todos los servicios");

    // Datos: Dueño, un rol sin permisos y un usuario con ese rol.
    TEST_CHECK(account->create_initial_owner("dueno_a", "Dueño A", kOwnerPwd, "dueno@cafeteria-itch.edu.mx").is_success(),
               "Dueño");
    auto role = account->create_role(CreateRoleDto{"Sin permisos B8", "Rol de prueba sin permisos", {}});
    TEST_CHECK(role.is_success(), "Rol sin permisos: " + role.error_message());
    const std::string no_perm_role_id = role.value().id;
    CreateUserDto nu{"sin_permisos", "Sin Permisos", kNoPermPwd, no_perm_role_id, "sp@cafeteria-itch.edu.mx", std::nullopt};
    auto nu_res = account->create_user(nu);
    TEST_CHECK(nu_res.is_success(), "Usuario sin permisos: " + nu_res.error_message());
    auto third = account->create_user(
        CreateUserDto{"tercero_b8", "Tercero", "Clave-Tercero-1", "role-cashier", "t3@cafeteria-itch.edu.mx", std::nullopt});
    TEST_CHECK(third.is_success(), "Tercer usuario (ajeno a la sesión de prueba): " + third.error_message());
    const std::string other_user_id = third.value().id;  // ni el Dueño ni el usuario sin permisos
    const std::string owner_id = account->current_user().value().id;
    account->logout();

    const std::vector<std::uint8_t> bytes = {1, 2, 3};
    const std::string kFake = "no-existe";

    std::vector<Case> cases;
    auto add = [&](const std::string& n, Kind k, std::function<Out()> f, bool owner_ok = true, bool writes = false) {
        cases.push_back(Case{n, k, std::move(f), owner_ok, writes});
    };

    // --- Cuentas ---
    add("account.create_initial_owner", Kind::Public,
        [&] { return out(account->create_initial_owner("otro", "Otro", "Clave-Otro-1", "o@cafeteria-itch.edu.mx")); }, false);
    add("account.create_user", Kind::Permission, [&] {
        return out(account->create_user(CreateUserDto{"u_nuevo", "Nuevo", "Clave-Nuevo-1", "role-cashier", "n@cafeteria-itch.edu.mx", std::nullopt}));
    });
    add("account.update_user", Kind::Permission, [&] {
        UpdateUserDto u; u.user_id = other_user_id; u.display_name = "Cambiado"; return out(account->update_user(u));
    });
    add("account.deactivate_user", Kind::Permission, [&] { return out(account->deactivate_user(other_user_id)); });
    add("account.list_users", Kind::Permission, [&] { return out(account->list_users()); });
    add("account.get_user_by_id(ajeno)", Kind::Permission, [&] { return out(account->get_user_by_id(other_user_id)); });
    add("account.change_password(ajeno)", Kind::Permission, [&] { return out(account->change_password(owner_id, "Clave-Nueva-9")); });
    add("account.create_role", Kind::Permission, [&] { return out(account->create_role(CreateRoleDto{"R", "d", {}})); });
    add("account.update_role", Kind::Permission, [&] {
        UpdateRoleDto u; u.role_id = no_perm_role_id; u.name = "X"; return out(account->update_role(u));
    });
    add("account.delete_role", Kind::Permission, [&] { return out(account->delete_role(no_perm_role_id)); }, true);
    add("account.list_permissions", Kind::Permission, [&] { return out(account->list_permissions()); });
    add("account.list_audit_events", Kind::Permission, [&] { return out(account->list_audit_events(AuditFilterDto{})); });
    add("account.get_user_avatar", Kind::SessionOnly, [&] { return out(account->get_user_avatar(owner_id)); });
    add("account.list_roles", Kind::SessionOnly, [&] { return out(account->list_roles()); });
    add("account.get_role_by_id", Kind::SessionOnly, [&] { return out(account->get_role_by_id(no_perm_role_id)); });
    add("account.can_deactivate_user", Kind::SessionOnly, [&] { return out(account->can_deactivate_user(other_user_id)); });
    add("account.record_audit_event", Kind::SessionOnly, [&] { return out(account->record_audit_event("PRUEBA_B8", "x")); }, true, true);

    // --- Catálogo ---
    add("catalog.search_products", Kind::Permission, [&] { return out(catalog->search_products("", std::nullopt, 0, 10, false)); });
    add("catalog.get_product_by_id", Kind::SessionOnly, [&] { return out(catalog->get_product_by_id(kFake)); });
    add("catalog.get_product_by_sku_or_barcode", Kind::SessionOnly, [&] { return out(catalog->get_product_by_sku_or_barcode(kFake)); });
    add("catalog.list_categories", Kind::SessionOnly, [&] { return out(catalog->list_categories()); });
    add("catalog.create_product", Kind::Permission, [&] {
        CreateProductDto p; p.sku = "S-B8"; p.name = "Prueba B8"; p.category = "Bebidas"; p.price = Money(1000); return out(catalog->create_product(p));
    });
    add("catalog.update_product", Kind::Permission, [&] {
        UpdateProductDto p; p.product_id = kFake; p.name = "x"; return out(catalog->update_product(p));
    });
    add("catalog.deactivate_product", Kind::Permission, [&] { return out(catalog->deactivate_product(kFake)); });
    add("catalog.reactivate_product", Kind::Permission, [&] { return out(catalog->reactivate_product(kFake)); });
    add("catalog.set_product_image", Kind::Permission, [&] { return out(catalog->set_product_image(kFake, "image/png", bytes)); });
    add("catalog.get_product_image", Kind::SessionOnly, [&] { return out(catalog->get_product_image(kFake)); });
    add("catalog.remove_product_image", Kind::Permission, [&] { return out(catalog->remove_product_image(kFake)); });
    add("catalog.get_recipe", Kind::SessionOnly, [&] { return out(catalog->get_recipe(kFake)); });
    add("catalog.get_recipe_costing", Kind::Permission, [&] { return out(catalog->get_recipe_costing(kFake)); });
    add("catalog.set_recipe", Kind::Permission, [&] { return out(catalog->set_recipe(kFake, {})); });

    // --- Inventario ---
    add("inventory.create_ingredient", Kind::Permission, [&] {
        return out(inventory->create_ingredient(CreateIngredientDto{"Limón B8", "MASS", "mg", 0}));
    });
    add("inventory.update_ingredient", Kind::Permission, [&] {
        UpdateIngredientDto u; u.ingredient_id = kFake; u.name = "x"; return out(inventory->update_ingredient(u));
    });
    add("inventory.list_ingredients", Kind::Permission, [&] { return out(inventory->list_ingredients()); });
    add("inventory.get_ingredient", Kind::Permission, [&] { return out(inventory->get_ingredient(kFake)); });
    add("inventory.get_stock_alerts", Kind::Permission, [&] { return out(inventory->get_stock_alerts()); });
    add("inventory.update_low_stock_threshold", Kind::Permission, [&] { return out(inventory->update_low_stock_threshold(kFake, 5)); });
    add("inventory.register_purchase_entry", Kind::Permission, [&] {
        return out(inventory->register_purchase_entry(kFake, 1, "kg", Money(5000)));
    });
    add("inventory.register_waste", Kind::Permission, [&] { return out(inventory->register_waste(kFake, 1, "g", "prueba")); });
    add("inventory.register_adjustment", Kind::Permission, [&] { return out(inventory->register_adjustment(kFake, 1, "g", "prueba")); });
    add("inventory.get_ingredient_cost", Kind::Permission, [&] { return out(inventory->get_ingredient_cost(kFake, 1)); });
    add("inventory.list_stock_movements", Kind::Permission, [&] { return out(inventory->list_stock_movements(StockMovementFilterDto{})); });
    add("inventory.list_purchase_entries", Kind::Permission, [&] { return out(inventory->list_purchase_entries(PurchaseEntryFilterDto{})); });

    // --- Carrito ---
    add("cart.get_cart", Kind::Permission, [&] { return out(cart->get_cart()); });
    add("cart.add_item", Kind::Permission, [&] { return out(cart->add_item(kFake, 1, "")); });
    add("cart.update_quantity", Kind::Permission, [&] { return out(cart->update_quantity(kFake, 2)); });
    add("cart.update_item_notes", Kind::Permission, [&] { return out(cart->update_item_notes(kFake, "nota")); });
    add("cart.remove_item", Kind::Permission, [&] { return out(cart->remove_item(kFake)); });
    add("cart.clear_cart", Kind::Permission, [&] { return out(cart->clear_cart()); });

    // --- Venta ---
    add("sale.confirm_sale", Kind::Permission, [&] { return out(sale->confirm_sale(PaymentMethod::Efectivo, Money(1000), "clave-b8")); });
    add("sale.get_sale_by_folio", Kind::Permission, [&] { return out(sale->get_sale_by_folio("F-0001")); });
    add("sale.get_ticket", Kind::Permission, [&] { return out(sale->get_ticket("F-0001")); });
    add("sale.get_recent_tickets", Kind::Permission, [&] { return out(sale->get_recent_tickets(10)); });

    // --- Turnos ---
    add("shift.get_current_shift", Kind::SessionOnly, [&] { return out(shift->get_current_shift()); });
    add("shift.open_shift", Kind::Permission, [&] { return out(shift->open_shift(kFake, Money(0))); });
    add("shift.close_shift", Kind::Permission, [&] { return out(shift->close_shift(Money(0))); });

    // --- Reportes ---
    add("report.get_daily_sales_summary", Kind::Permission, [&] { return out(report->get_daily_sales_summary("2026-11-14")); });

    // --- Configuración de impresión ---
    add("print_config.get_station_config", Kind::SessionOnly, [&] { return out(print->get_station_config()); });
    add("print_config.set_station_config", Kind::Permission, [&] { return out(print->set_station_config(PrinterStationConfig{})); });

    // --- Respaldos ---
    add("backup.get_status", Kind::SessionOnly, [&] { return out(backup->get_status()); });
    add("backup.trigger_local_backup", Kind::Permission, [&] {
        return out(backup->trigger_local_backup((g_root / "no-debe-existir.db").u8string()));
    });
    add("backup.set_backup_enabled", Kind::Permission, [&] { return out(backup->set_backup_enabled(true)); });
    add("backup.verify_backup", Kind::Permission, [&] { return out(backup->verify_backup((g_root / "no-existe.db").u8string())); });
    add("backup.restore_from_backup", Kind::Permission, [&] { return out(backup->restore_from_backup((g_root / "no-existe.db").u8string())); });
    add("backup.get_developer_server_config", Kind::Permission, [&] { return out(backup->get_developer_server_config()); }, false);
    add("backup.update_developer_server_config", Kind::Permission, [&] {
        return out(backup->update_developer_server_config(BackupServerConfigDto{"", "", true}));
    }, false);
    add("backup.get_retention_policy", Kind::Permission, [&] { return out(backup->get_retention_policy()); });
    add("backup.set_retention_policy", Kind::Permission, [&] { return out(backup->set_retention_policy(BackupRetentionPolicyDto{30, 90})); });
    add("backup.preview_backup_cleanup", Kind::Permission, [&] { return out(backup->preview_backup_cleanup()); });
    add("backup.cleanup_old_backups", Kind::Permission, [&] { return out(backup->cleanup_old_backups()); });

    TEST_CHECK(cases.size() >= 60, "La tabla debe cubrir todos los métodos de los contratos");

    auto changes = [&] { return sqlite3_total_changes(db.handle()); };

    // === 1. Sin sesión ===
    TEST_CHECK(!account->is_authenticated(), "Empezar sin sesión");
    TEST_CHECK(account->has_owner().is_success() && account->has_owner().value(), "has_owner es público");
    TEST_CHECK(!account->current_user().is_success(), "current_user sin sesión rechaza");
    TEST_CHECK(!account->has_permission("users.manage"), "has_permission sin sesión es falso");
    for (const auto& c : cases) {
        const int before = changes();
        const Out r = c.call();
        TEST_CHECK(!r.ok, "Sin sesión debe rechazar: " + c.name);
        TEST_CHECK(r.code == ErrorCode::Forbidden || c.kind == Kind::Public,
                   "Sin sesión debe responder Forbidden: " + c.name + " (código " + to_string(r.code) + ")");
        TEST_CHECK(changes() == before, "Sin sesión no debe escribir nada: " + c.name);
    }
    account->logout();
    TEST_CHECK(account->logout().is_success(), "logout sin sesión no falla");

    // === 2. Con sesión de un rol sin permisos ===
    TEST_CHECK(account->login("sin_permisos", kNoPermPwd).is_success(), "Usuario sin permisos entra");
    TEST_CHECK(account->is_authenticated() && !account->has_permission("users.manage") &&
                   !account->has_permission("pos.sell") && !account->has_permission("inventory.view"),
               "No debe tener permisos");
    for (const auto& c : cases) {
        const int before = changes();
        const Out r = c.call();
        if (c.kind == Kind::Permission || c.kind == Kind::Public) {
            TEST_CHECK(!r.ok, "Sin permiso debe rechazar: " + c.name);
            TEST_CHECK(r.code == ErrorCode::Forbidden || c.kind == Kind::Public,
                       "Sin permiso debe responder Forbidden: " + c.name + " (código " + to_string(r.code) + ")");
        }
        if (!c.may_write) TEST_CHECK(changes() == before, "Sin permiso no debe escribir nada: " + c.name);
    }
    // Sobre la propia cuenta sí se puede (uso propio): cambiar la contraseña propia y verse a sí mismo.
    const std::string own_id = account->current_user().value().id;
    TEST_CHECK(account->get_user_by_id(own_id).is_success(), "Puede ver su propia cuenta");
    TEST_CHECK(account->change_password(own_id, kNoPermPwd).is_success(), "Puede cambiar su propia contraseña");
    account->logout();

    // === 3. Control positivo con el Dueño ===
    TEST_CHECK(account->login("dueno_a", kOwnerPwd).is_success(), "Dueño entra");
    for (const auto& c : cases) {
        if (c.kind != Kind::Permission) continue;
        const Out r = c.call();
        if (c.owner_expected_not_forbidden) {
            TEST_CHECK(r.code != ErrorCode::Forbidden, "El Dueño no debe recibir Forbidden en: " + c.name);
        } else {
            TEST_CHECK(r.code == ErrorCode::Forbidden, "El Dueño no tiene este permiso exclusivo del Desarrollador: " + c.name);
        }
    }
    TEST_CHECK(account->is_authenticated(), "El Dueño sigue con sesión tras las llamadas de control");

    std::cout << "Pruebas de autorización (B8) correctas. Métodos recorridos: " << cases.size()
              << ". TEST_CHECK ejecutados: " << g_checks << std::endl;
    account->logout();
    db.close();
    cleanup_and_exit(0);
}
