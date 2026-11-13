#include "app/services/mock_services.hpp"
#include "app/services/sqlite_account_service.hpp"
#include "app/services/sqlite_backup_service.hpp"
#include "app/services/sqlite_report_service.hpp"
#include "app/services/sqlite_catalog_service.hpp"
#include "app/services/sqlite_inventory_service.hpp"
#include "app/services/sqlite_shift_service.hpp"
#include "app/services/sqlite_cart_service.hpp"
#include "app/services/sqlite_sale_service.hpp"
#include "app/services/sqlite_print_config_service.hpp"
#include <algorithm>
#include <sstream>

namespace app::services {

// --- MockAccountService ---

MockAccountService::MockAccountService() {
    roles_ = {
        {"role-owner", "Dueño", "Acceso total y administración", true, {"all"}},
        {"role-cashier", "Cajero", "Ventas y turnos", true, {"pos.sell", "shift.operate"}},
        {"role-dev", "Desarrollador", "Configuración técnica remota", true, {"backup.dev_config"}}
    };

    contracts::UserDto owner;
    owner.id = "user-01";
    owner.username = "cajero01";
    owner.display_name = "Cajero 01";
    owner.email = "cajero01@cafeteria-itch.edu.mx";
    owner.role_id = "role-cashier";
    owner.role_name = "Cajero";
    owner.is_active = true;
    owner.is_owner = false;
    owner.has_avatar = false;

    users_.push_back(owner);
    current_user_ = owner;
}

contracts::Result<bool> MockAccountService::has_owner() {
    return contracts::Result<bool>::ok(has_owner_);
}

contracts::OperationResult MockAccountService::create_initial_owner(
    const std::string& username,
    const std::string& display_name,
    const std::string& raw_password,
    const std::string& email,
    const std::optional<std::vector<std::uint8_t>>& avatar_bytes
) {
    if (username.empty() || raw_password.empty() || email.empty()) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "Usuario, contraseña y correo electrónico son obligatorios"
        );
    }
    contracts::UserDto owner;
    owner.id = "user-owner-01";
    owner.username = username;
    owner.display_name = display_name;
    owner.email = email;
    owner.role_id = "role-owner";
    owner.role_name = "Dueño";
    owner.is_active = true;
    owner.is_owner = true;
    owner.has_avatar = avatar_bytes.has_value();

    if (avatar_bytes.has_value()) {
        user_avatars_[owner.id] = *avatar_bytes;
    }

    users_.push_back(owner);
    current_user_ = owner;
    has_owner_ = true;
    return contracts::OperationResult::ok();
}

contracts::Result<contracts::UserDto> MockAccountService::login(
    const std::string& username,
    const std::string& raw_password
) {
    for (const auto& u : users_) {
        if (u.username == username && u.is_active) {
            current_user_ = u;
            return contracts::Result<contracts::UserDto>::ok(u);
        }
    }
    return contracts::Result<contracts::UserDto>::fail(
        contracts::ErrorCode::NotFound,
        "Usuario o contraseña incorrectos"
    );
}

contracts::OperationResult MockAccountService::logout() {
    current_user_ = std::nullopt;
    return contracts::OperationResult::ok();
}

contracts::Result<contracts::UserDto> MockAccountService::current_user() const {
    if (!current_user_.has_value()) {
        return contracts::Result<contracts::UserDto>::fail(
            contracts::ErrorCode::Forbidden,
            "Sesión no iniciada"
        );
    }
    return contracts::Result<contracts::UserDto>::ok(*current_user_);
}

bool MockAccountService::is_authenticated() const {
    return current_user_.has_value();
}

bool MockAccountService::has_permission(std::string_view permission_key) const {
    if (!current_user_.has_value()) return false;
    if (current_user_->is_owner) return true;
    return true; // En mock permitimos la navegación general
}

contracts::Result<bool> MockAccountService::can_deactivate_user(const std::string& user_id) {
    // Si es el último dueño activo, no se puede desactivar
    int active_owners = 0;
    bool is_target_owner = false;
    for (const auto& u : users_) {
        if (u.is_owner && u.is_active) {
            active_owners++;
            if (u.id == user_id) {
                is_target_owner = true;
            }
        }
    }
    if (is_target_owner && active_owners <= 1) {
        return contracts::Result<bool>::ok(false);
    }
    return contracts::Result<bool>::ok(true);
}

contracts::OperationResult MockAccountService::deactivate_user(const std::string& user_id) {
    auto can_res = can_deactivate_user(user_id);
    if (!can_res.is_success() || !can_res.value()) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Conflict,
            "No se puede desactivar a la última cuenta Dueño activa"
        );
    }
    for (auto& u : users_) {
        if (u.id == user_id) {
            u.is_active = false;
            return contracts::OperationResult::ok();
        }
    }
    return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Usuario no encontrado");
}

contracts::Result<contracts::UserDto> MockAccountService::create_user(const contracts::CreateUserDto& input) {
    if (input.username.empty() || input.raw_password.empty() || input.email.empty()) {
        return contracts::Result<contracts::UserDto>::fail(contracts::ErrorCode::InvalidInput, "Datos incompletos");
    }
    if (input.role_id == "role-dev") {
        return contracts::Result<contracts::UserDto>::fail(contracts::ErrorCode::Forbidden, "No se puede asignar rol Desarrollador");
    }
    for (const auto& u : users_) {
        if (u.username == input.username) {
            return contracts::Result<contracts::UserDto>::fail(contracts::ErrorCode::Conflict, "Usuario ya existe");
        }
        if (u.email == input.email) {
            return contracts::Result<contracts::UserDto>::fail(contracts::ErrorCode::Conflict, "Correo ya registrado");
        }
    }
    contracts::UserDto u;
    u.id = "user-" + input.username;
    u.username = input.username;
    u.display_name = input.display_name;
    u.email = input.email;
    u.role_id = input.role_id;
    u.role_name = (input.role_id == "role-owner" ? "Dueño" : (input.role_id == "role-cashier" ? "Cajero" : input.role_id));
    u.is_active = true;
    u.is_owner = (input.role_id == "role-owner");
    u.has_avatar = input.avatar_bytes.has_value();

    if (input.avatar_bytes.has_value()) {
        user_avatars_[u.id] = *input.avatar_bytes;
    }

    users_.push_back(u);
    return contracts::Result<contracts::UserDto>::ok(u);
}

contracts::OperationResult MockAccountService::update_user(const contracts::UpdateUserDto& input) {
    for (auto& u : users_) {
        if (u.id == input.user_id) {
            if (u.is_owner && input.is_active.has_value() && !*input.is_active) {
                auto can_deact = can_deactivate_user(input.user_id);
                if (!can_deact.is_success() || !can_deact.value()) {
                    return contracts::OperationResult::fail(contracts::ErrorCode::Conflict, "No se puede desactivar al último Dueño");
                }
            }
            if (input.display_name.has_value()) u.display_name = *input.display_name;
            if (input.email.has_value()) {
                if (input.email->empty()) {
                    return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "Correo no puede ser vacío");
                }
                for (const auto& other : users_) {
                    if (other.id != u.id && other.email == *input.email) {
                        return contracts::OperationResult::fail(contracts::ErrorCode::Conflict, "Correo ya registrado por otro usuario");
                    }
                }
                u.email = *input.email;
            }
            if (input.role_id.has_value()) {
                if (input.role_id == "role-dev") {
                    return contracts::OperationResult::fail(contracts::ErrorCode::Forbidden, "No se puede asignar rol Desarrollador");
                }
                u.role_id = *input.role_id;
            }
            if (input.is_active.has_value()) u.is_active = *input.is_active;
            if (input.avatar_bytes.has_value()) {
                user_avatars_[u.id] = *input.avatar_bytes;
                u.has_avatar = true;
            }
            if (input.remove_avatar.has_value() && *input.remove_avatar) {
                user_avatars_.erase(u.id);
                u.has_avatar = false;
            }
            return contracts::OperationResult::ok();
        }
    }
    return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Usuario no encontrado");
}

contracts::OperationResult MockAccountService::change_password(
    const std::string& user_id,
    const std::string& new_raw_password
) {
    if (new_raw_password.length() < 4) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "Contraseña demasiado corta");
    }
    for (const auto& u : users_) {
        if (u.id == user_id) {
            record_audit_event("PASSWORD_CHANGED", "Cambio de contraseña para " + u.username);
            return contracts::OperationResult::ok();
        }
    }
    return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Usuario no encontrado");
}

contracts::Result<contracts::UserDto> MockAccountService::get_user_by_id(const std::string& user_id) {
    for (const auto& u : users_) {
        if (u.id == user_id) return contracts::Result<contracts::UserDto>::ok(u);
    }
    return contracts::Result<contracts::UserDto>::fail(contracts::ErrorCode::NotFound, "Usuario no encontrado");
}

contracts::Result<std::vector<contracts::UserDto>> MockAccountService::list_users() {
    return contracts::Result<std::vector<contracts::UserDto>>::ok(users_);
}

contracts::Result<std::vector<std::uint8_t>> MockAccountService::get_user_avatar(const std::string& user_id) {
    auto it = user_avatars_.find(user_id);
    if (it != user_avatars_.end()) {
        return contracts::Result<std::vector<std::uint8_t>>::ok(it->second);
    }
    return contracts::Result<std::vector<std::uint8_t>>::fail(contracts::ErrorCode::NotFound, "Avatar no encontrado");
}

contracts::Result<std::vector<contracts::RoleDto>> MockAccountService::list_roles() {
    return contracts::Result<std::vector<contracts::RoleDto>>::ok(roles_);
}

contracts::Result<contracts::RoleDto> MockAccountService::get_role_by_id(const std::string& role_id) {
    for (const auto& r : roles_) {
        if (r.id == role_id) return contracts::Result<contracts::RoleDto>::ok(r);
    }
    return contracts::Result<contracts::RoleDto>::fail(contracts::ErrorCode::NotFound, "Rol no encontrado");
}

contracts::Result<contracts::RoleDto> MockAccountService::create_role(const contracts::CreateRoleDto& input) {
    for (const auto& perm : input.permissions) {
        if (perm == "backup.dev_config") {
            return contracts::Result<contracts::RoleDto>::fail(contracts::ErrorCode::Forbidden, "backup.dev_config no puede ser asignado");
        }
    }
    contracts::RoleDto r;
    r.id = "role-" + input.name;
    r.name = input.name;
    r.description = input.description;
    r.is_system = false;
    r.permissions = input.permissions;
    roles_.push_back(r);
    return contracts::Result<contracts::RoleDto>::ok(r);
}

contracts::OperationResult MockAccountService::update_role(const contracts::UpdateRoleDto& input) {
    if (input.role_id == "role-dev" || input.role_id == "role-owner") {
        return contracts::OperationResult::fail(contracts::ErrorCode::Forbidden, "No se pueden modificar roles protegidos del sistema");
    }
    for (const auto& perm : input.permissions) {
        if (perm == "backup.dev_config") {
            return contracts::OperationResult::fail(contracts::ErrorCode::Forbidden, "backup.dev_config no puede ser asignado");
        }
    }
    for (auto& r : roles_) {
        if (r.id == input.role_id) {
            r.name = input.name;
            r.description = input.description;
            r.permissions = input.permissions;
            return contracts::OperationResult::ok();
        }
    }
    return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Rol no encontrado");
}

contracts::OperationResult MockAccountService::delete_role(const std::string& role_id) {
    for (auto it = roles_.begin(); it != roles_.end(); ++it) {
        if (it->id == role_id) {
            if (it->is_system) {
                return contracts::OperationResult::fail(contracts::ErrorCode::Forbidden, "No se pueden eliminar roles de sistema");
            }
            roles_.erase(it);
            return contracts::OperationResult::ok();
        }
    }
    return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Rol no encontrado");
}

contracts::Result<std::vector<contracts::PermissionDto>> MockAccountService::list_permissions() {
    std::vector<contracts::PermissionDto> list = {
        {"pos.sell", "Registrar ventas y cobrarlas en la caja.", "pos", "Vender y cobrar", "Punto de Venta", 10},
        {"pos.visual_catalog", "Elegir productos desde las tarjetas con imagen, como en un menú.", "pos", "Usar el catálogo con fotos", "Punto de Venta", 20},
        {"pos.notes", "Añadir indicaciones a un producto, por ejemplo: sin azúcar.", "pos", "Escribir notas en un pedido", "Punto de Venta", 30},
        {"pos.reprint", "Volver a imprimir el ticket de una venta ya cobrada.", "pos", "Reimprimir tickets", "Punto de Venta", 40},
        {"pos.cancel_order", "Vaciar el pedido en curso antes de cobrarlo.", "pos", "Cancelar un pedido sin cobrar", "Punto de Venta", 50},
        {"pos.void_sale", "Cancelar una venta que ya se cobró. Queda registrado quién lo hizo y por qué.", "pos", "Anular una venta cobrada", "Punto de Venta", 60},
        {"inventory.view", "Consultar cuánto hay de cada producto e ingrediente.", "inventory", "Ver existencias", "Inventario", 110},
        {"inventory.manage_products", "Crear, cambiar o dar de baja productos, sus precios y sus recetas.", "inventory", "Administrar productos y recetas", "Inventario", 120},
        {"inventory.restock", "Anotar lo que se compra y cuánto costó, para sumarlo al inventario.", "inventory", "Registrar compras", "Inventario", 130},
        {"inventory.adjust", "Anotar pérdidas, traslados y correcciones del inventario.", "inventory", "Registrar mermas y ajustes", "Inventario", 140},
        {"inventory.manage_thresholds", "Fijar la cantidad mínima de cada ingrediente para que el sistema avise.", "inventory", "Definir avisos de poco inventario", "Inventario", 150},
        {"inventory.promotions", "Crear y cambiar promociones y combos.", "inventory", "Administrar promociones y combos", "Inventario", 160},
        {"shift.operate", "Abrir turno, registrar entradas y salidas de dinero y hacer el corte.", "shift", "Abrir y cerrar caja", "Caja y Reportes", 210},
        {"reports.view", "Consultar los reportes de ventas e inventario.", "reports", "Ver reportes", "Caja y Reportes", 220},
        {"audit.view", "Consultar el historial de lo que cada persona hizo en el sistema.", "audit", "Ver la bitácora", "Caja y Reportes", 230},
        {"users.manage", "Crear cuentas de usuario, cambiarlas o desactivarlas.", "users", "Administrar cuentas", "BackOffice", 310},
        {"roles.manage", "Crear roles y decidir qué puede hacer cada uno.", "roles", "Administrar roles y permisos", "BackOffice", 320},
        {"config.manage", "Ajustar opciones del negocio, como la impresora y el tamaño del ticket.", "config", "Cambiar la configuración", "BackOffice", 330},
        {"backup.manage", "Activar y ejecutar copias de seguridad de la información.", "backup", "Hacer respaldos", "BackOffice", 340},
        {"backup.restore", "Recuperar la información desde una copia. Reemplaza los datos actuales.", "backup", "Restaurar un respaldo", "BackOffice", 350},
        {"backup.dev_config", "Configuración técnica del servidor remoto (exclusivo Desarrollador).", "backup", "Configurar el servidor de respaldos", "BackOffice", 990}
    };
    return contracts::Result<std::vector<contracts::PermissionDto>>::ok(list);
}

contracts::Result<std::vector<contracts::AuditEventDto>> MockAccountService::list_audit_events(const contracts::AuditFilterDto& filter) {
    std::vector<contracts::AuditEventDto> filtered;
    for (const auto& e : audit_events_) {
        if (filter.user_id.has_value() && !filter.user_id->empty() && e.user_id != *filter.user_id) continue;
        if (filter.event_type.has_value() && !filter.event_type->empty() && e.event_type != *filter.event_type) continue;
        filtered.push_back(e);
    }
    return contracts::Result<std::vector<contracts::AuditEventDto>>::ok(filtered);
}

contracts::OperationResult MockAccountService::record_audit_event(
    const std::string& event_type,
    const std::string& details
) {
    contracts::AuditEventDto e;
    e.id = "audit-mock-" + std::to_string(audit_events_.size() + 1);
    e.event_type = event_type;
    e.user_id = current_user_.has_value() ? current_user_->id : "";
    e.username = current_user_.has_value() ? current_user_->username : "";
    e.details = details;
    e.created_at = "2026-09-25 00:00:00";
    audit_events_.push_back(e);
    return contracts::OperationResult::ok();
}

// --- MockCatalogService ---

MockCatalogService::MockCatalogService() {
    products_ = {
        {"prod-1", "SKU-001", "7501001", "Chocolaleche", "Bebidas", contracts::Money::from_major_minor(30, 0), 45, false, true, false},
        {"prod-2", "SKU-002", "7501002", "Torta de Jamón", "Platillos", contracts::Money::from_major_minor(45, 0), 20, true, true, false},
        {"prod-3", "SKU-003", "7501003", "Chilaquiles Rojos", "Platillos", contracts::Money::from_major_minor(65, 0), 15, true, true, false},
        {"prod-4", "SKU-004", "7501004", "Café Americano", "Bebidas", contracts::Money::from_major_minor(25, 0), 100, false, true, false},
        {"prod-5", "SKU-005", "7501005", "Molletes con Queso", "Platillos", contracts::Money::from_major_minor(40, 0), 18, true, true, false}
    };
}

contracts::Result<std::vector<contracts::ProductDto>> MockCatalogService::search_products(
    const std::string& query,
    const std::optional<std::string>& category,
    std::size_t offset,
    std::size_t limit,
    bool include_inactive
) {
    std::vector<contracts::ProductDto> matches;
    for (const auto& p : products_) {
        if (!include_inactive && !p.is_active) continue;
        if (!category.has_value() || p.category == *category) {
            if (query.empty() || p.name.find(query) != std::string::npos || p.sku.find(query) != std::string::npos) {
                matches.push_back(p);
            }
        }
    }
    return contracts::Result<std::vector<contracts::ProductDto>>::ok(matches);
}

contracts::Result<contracts::ProductDto> MockCatalogService::get_product_by_id(const std::string& product_id) {
    for (const auto& p : products_) {
        if (p.id == product_id) return contracts::Result<contracts::ProductDto>::ok(p);
    }
    return contracts::Result<contracts::ProductDto>::fail(contracts::ErrorCode::NotFound, "Producto no encontrado");
}

contracts::Result<contracts::ProductDto> MockCatalogService::get_product_by_sku_or_barcode(const std::string& code) {
    for (const auto& p : products_) {
        if (p.sku == code || p.barcode == code) return contracts::Result<contracts::ProductDto>::ok(p);
    }
    return contracts::Result<contracts::ProductDto>::fail(contracts::ErrorCode::NotFound, "Código no encontrado");
}

contracts::Result<std::vector<std::string>> MockCatalogService::list_categories() {
    return contracts::Result<std::vector<std::string>>::ok({"Todas", "Bebidas", "Platillos", "Snacks"});
}

contracts::Result<contracts::ProductDto> MockCatalogService::create_product(const contracts::CreateProductDto& input) {
    contracts::ProductDto p;
    p.id = "prod-" + std::to_string(products_.size() + 1);
    p.sku = input.sku;
    p.barcode = input.barcode;
    p.name = input.name;
    p.category = input.category;
    p.price = input.price;
    p.is_prepared = input.is_prepared;
    p.is_active = true;
    p.has_image = false;
    p.stock_units = 0;
    products_.push_back(p);
    return contracts::Result<contracts::ProductDto>::ok(p);
}

contracts::OperationResult MockCatalogService::update_product(const contracts::UpdateProductDto& input) {
    for (auto& p : products_) {
        if (p.id == input.product_id) {
            if (input.sku.has_value()) p.sku = *input.sku;
            if (input.barcode.has_value()) p.barcode = *input.barcode;
            if (input.name.has_value()) p.name = *input.name;
            if (input.category.has_value()) p.category = *input.category;
            if (input.price.has_value()) p.price = *input.price;
            if (input.is_prepared.has_value()) p.is_prepared = *input.is_prepared;
            if (input.is_active.has_value()) p.is_active = *input.is_active;
            return contracts::OperationResult::ok();
        }
    }
    return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Producto no encontrado");
}

contracts::OperationResult MockCatalogService::deactivate_product(const std::string& product_id) {
    for (auto& p : products_) {
        if (p.id == product_id) {
            p.is_active = false;
            return contracts::OperationResult::ok();
        }
    }
    return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Producto no encontrado");
}

contracts::OperationResult MockCatalogService::reactivate_product(const std::string& product_id) {
    for (auto& p : products_) {
        if (p.id == product_id) {
            p.is_active = true;
            return contracts::OperationResult::ok();
        }
    }
    return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Producto no encontrado");
}

contracts::OperationResult MockCatalogService::set_product_image(
    const std::string& product_id,
    const std::string& mime_type,
    const std::vector<std::uint8_t>& image_bytes
) {
    for (auto& p : products_) {
        if (p.id == product_id) {
            p.has_image = true;
            images_[product_id] = {product_id, mime_type, image_bytes, image_bytes};
            return contracts::OperationResult::ok();
        }
    }
    return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Producto no encontrado");
}

contracts::Result<contracts::ProductImageDto> MockCatalogService::get_product_image(const std::string& product_id) {
    auto it = images_.find(product_id);
    if (it != images_.end()) {
        return contracts::Result<contracts::ProductImageDto>::ok(it->second);
    }
    return contracts::Result<contracts::ProductImageDto>::fail(contracts::ErrorCode::NotFound, "Imagen no encontrada");
}

contracts::OperationResult MockCatalogService::remove_product_image(const std::string& product_id) {
    images_.erase(product_id);
    for (auto& p : products_) {
        if (p.id == product_id) {
            p.has_image = false;
            break;
        }
    }
    return contracts::OperationResult::ok();
}

contracts::Result<contracts::RecipeDto> MockCatalogService::get_recipe(const std::string& product_id) {
    auto it = recipes_.find(product_id);
    if (it != recipes_.end()) {
        return contracts::Result<contracts::RecipeDto>::ok(it->second);
    }
    return contracts::Result<contracts::RecipeDto>::fail(contracts::ErrorCode::NotFound, "Receta no encontrada");
}

contracts::OperationResult MockCatalogService::set_recipe(
    const std::string& product_id,
    const std::vector<contracts::RecipeItemDto>& items
) {
    contracts::RecipeDto r;
    r.recipe_id = "rcp-" + product_id;
    r.product_id = product_id;
    r.version = 1;
    r.items = items;
    r.estimated_cost_per_portion = contracts::Money::from_major_minor(10, 0);
    recipes_[product_id] = r;
    return contracts::OperationResult::ok();
}

// --- MockCartService ---

MockCartService::MockCartService() {
    // Inicializar con dos platillos y una bebida según specs.md
    contracts::CartItemDto item1{"line-1", "prod-1", "SKU-001", "Chocolaleche", contracts::Money::from_major_minor(30, 0), 1, contracts::Money::from_major_minor(30, 0)};
    contracts::CartItemDto item2{"line-2", "prod-2", "SKU-002", "Torta de Jamón", contracts::Money::from_major_minor(45, 0), 1, contracts::Money::from_major_minor(45, 0)};
    contracts::CartItemDto item3{"line-3", "prod-3", "SKU-003", "Chilaquiles Rojos", contracts::Money::from_major_minor(65, 0), 1, contracts::Money::from_major_minor(65, 0)};

    cart_.items = {item1, item2, item3};
    recalculate();
}

void MockCartService::recalculate() {
    std::int64_t items_count = 0;
    contracts::Money sum = contracts::Money::zero();
    for (auto& item : cart_.items) {
        item.subtotal = item.unit_price * item.quantity;
        items_count += item.quantity;
        sum += item.subtotal;
    }
    cart_.total_items = items_count;
    cart_.subtotal = sum;
    cart_.discount = contracts::Money::zero();
    cart_.total = sum - cart_.discount;
}

contracts::Result<contracts::CartSummaryDto> MockCartService::get_cart() {
    return contracts::Result<contracts::CartSummaryDto>::ok(cart_);
}

contracts::Result<contracts::CartSummaryDto> MockCartService::add_item(const std::string& product_id, std::int64_t quantity, const std::string& notes) {
    for (auto& item : cart_.items) {
        if (item.product_id == product_id && item.notes == notes) {
            item.quantity += quantity;
            recalculate();
            return contracts::Result<contracts::CartSummaryDto>::ok(cart_);
        }
    }
    // Agregar nueva línea
    contracts::CartItemDto item;
    item.line_id = "line-" + std::to_string(cart_.items.size() + 1);
    item.product_id = product_id;
    item.sku = "SKU-NEW";
    item.name = "Artículo agregado";
    item.unit_price = contracts::Money::from_major_minor(25, 0);
    item.quantity = quantity;
    item.notes = notes;
    cart_.items.push_back(item);
    recalculate();
    return contracts::Result<contracts::CartSummaryDto>::ok(cart_);
}

contracts::Result<contracts::CartSummaryDto> MockCartService::update_item_notes(const std::string& line_id, const std::string& notes) {
    for (auto& item : cart_.items) {
        if (item.line_id == line_id) {
            item.notes = notes;
            return contracts::Result<contracts::CartSummaryDto>::ok(cart_);
        }
    }
    return contracts::Result<contracts::CartSummaryDto>::fail(contracts::ErrorCode::NotFound, "Línea de carrito no encontrada");
}

contracts::Result<contracts::CartSummaryDto> MockCartService::update_quantity(const std::string& line_id, std::int64_t new_quantity) {
    for (auto it = cart_.items.begin(); it != cart_.items.end(); ++it) {
        if (it->line_id == line_id) {
            if (new_quantity <= 0) {
                cart_.items.erase(it);
            } else {
                it->quantity = new_quantity;
            }
            recalculate();
            return contracts::Result<contracts::CartSummaryDto>::ok(cart_);
        }
    }
    return contracts::Result<contracts::CartSummaryDto>::fail(contracts::ErrorCode::NotFound, "Línea de carrito no encontrada");
}

contracts::Result<contracts::CartSummaryDto> MockCartService::remove_item(const std::string& line_id) {
    return update_quantity(line_id, 0);
}

contracts::OperationResult MockCartService::clear_cart() {
    cart_.items.clear();
    recalculate();
    return contracts::OperationResult::ok();
}

// --- MockShiftService ---

MockShiftService::MockShiftService() {
    current_shift_.id = "shift-001";
    current_shift_.register_id = "reg-01";
    current_shift_.register_name = "Caja Principal";
    current_shift_.cashier_user_id = "user-01";
    current_shift_.cashier_name = "Cajero 01";
    current_shift_.opened_at = "2026-09-24 08:00:00";
    current_shift_.initial_cash = contracts::Money::from_major_minor(500, 0);
    current_shift_.is_open = true;
}

contracts::Result<contracts::ShiftDto> MockShiftService::get_current_shift() {
    return contracts::Result<contracts::ShiftDto>::ok(current_shift_);
}

contracts::Result<contracts::ShiftDto> MockShiftService::open_shift(
    const std::string& register_id,
    contracts::Money initial_cash
) {
    current_shift_.is_open = true;
    current_shift_.initial_cash = initial_cash;
    return contracts::Result<contracts::ShiftDto>::ok(current_shift_);
}

contracts::OperationResult MockShiftService::close_shift(contracts::Money) {
    current_shift_.is_open = false;
    return contracts::OperationResult::ok();
}

// --- MockInventoryService ---

MockInventoryService::MockInventoryService() {
    ingredients_ = {
        {"ing-1", "Limón con cáscara", "MASS", "mg", 500000, 1000000},
        {"ing-2", "Leche entera", "VOLUME", "ml", 5000, 2000},
        {"ing-3", "Café de grano", "MASS", "mg", 3500000, 1000000},
        {"ing-4", "Jamón de pavo", "MASS", "mg", 1200000, 500000},
        {"ing-5", "Totopos de maíz", "MASS", "mg", 4000000, 1500000}
    };
}

contracts::Result<contracts::IngredientDto> MockInventoryService::create_ingredient(const contracts::CreateIngredientDto& input) {
    contracts::IngredientDto ing;
    ing.id = "ing-" + std::to_string(ingredients_.size() + 1);
    ing.name = input.name;
    ing.unit_dimension = input.unit_dimension;
    ing.base_unit = input.base_unit;
    ing.current_stock_base = 0;
    ing.low_stock_threshold_base = input.low_stock_threshold_base;
    ingredients_.push_back(ing);
    return contracts::Result<contracts::IngredientDto>::ok(ing);
}

contracts::OperationResult MockInventoryService::update_ingredient(const contracts::UpdateIngredientDto& input) {
    for (auto& ing : ingredients_) {
        if (ing.id == input.ingredient_id) {
            if (input.name.has_value()) ing.name = *input.name;
            if (input.low_stock_threshold_base.has_value()) ing.low_stock_threshold_base = *input.low_stock_threshold_base;
            return contracts::OperationResult::ok();
        }
    }
    return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Ingrediente no encontrado");
}

contracts::Result<std::vector<contracts::IngredientDto>> MockInventoryService::list_ingredients() {
    return contracts::Result<std::vector<contracts::IngredientDto>>::ok(ingredients_);
}

contracts::Result<contracts::IngredientDto> MockInventoryService::get_ingredient(const std::string& ingredient_id) {
    for (const auto& ing : ingredients_) {
        if (ing.id == ingredient_id) return contracts::Result<contracts::IngredientDto>::ok(ing);
    }
    return contracts::Result<contracts::IngredientDto>::fail(contracts::ErrorCode::NotFound, "Ingrediente no encontrado");
}

contracts::Result<std::vector<contracts::StockAlertDto>> MockInventoryService::get_stock_alerts() {
    std::vector<contracts::StockAlertDto> alerts;
    for (const auto& ing : ingredients_) {
        if (ing.current_stock_base <= ing.low_stock_threshold_base) {
            alerts.push_back({ing.id, ing.name, ing.current_stock_base, ing.low_stock_threshold_base, ing.base_unit});
        }
    }
    return contracts::Result<std::vector<contracts::StockAlertDto>>::ok(alerts);
}

contracts::OperationResult MockInventoryService::update_low_stock_threshold(
    const std::string& ingredient_id,
    std::int64_t new_threshold_base
) {
    for (auto& ing : ingredients_) {
        if (ing.id == ingredient_id) {
            ing.low_stock_threshold_base = new_threshold_base;
            return contracts::OperationResult::ok();
        }
    }
    return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Ingrediente no encontrado");
}

contracts::OperationResult MockInventoryService::register_purchase_entry(
    const std::string&,
    std::int64_t,
    const std::string&,
    contracts::Money
) {
    return contracts::OperationResult::ok();
}

// --- MockSaleService ---

MockSaleService::MockSaleService() {}

contracts::Result<contracts::SaleConfirmationDto> MockSaleService::confirm_sale(
    contracts::PaymentMethod payment_method,
    contracts::Money cash_received,
    const std::string&
) {
    contracts::SaleConfirmationDto confirmation;
    confirmation.sale_id = "sale-" + std::to_string(next_folio_);
    confirmation.folio = "TK-" + std::to_string(next_folio_++);
    confirmation.total = contracts::Money::from_major_minor(140, 0);
    confirmation.cash_received = cash_received;
    confirmation.change = cash_received >= confirmation.total ? cash_received - confirmation.total : contracts::Money::zero();
    confirmation.payment_method = payment_method;
    confirmation.created_at = "2026-09-24 12:00:00";
    return contracts::Result<contracts::SaleConfirmationDto>::ok(confirmation);
}

contracts::Result<contracts::SaleConfirmationDto> MockSaleService::get_sale_by_folio(const std::string& folio) {
    contracts::SaleConfirmationDto confirmation;
    confirmation.sale_id = "sale-1001";
    confirmation.folio = folio;
    confirmation.total = contracts::Money::from_major_minor(140, 0);
    return contracts::Result<contracts::SaleConfirmationDto>::ok(confirmation);
}

contracts::Result<contracts::TicketDto> MockSaleService::get_ticket(const std::string& folio) {
    contracts::TicketDto ticket;
    ticket.folio = folio;
    ticket.created_at = "2026-09-24 12:00:00";
    ticket.cashier_name = "Cajero de Prueba";
    ticket.items = {
        {"Chilaquiles Rojos", 2, contracts::Money::from_major_minor(55, 0), contracts::Money::from_major_minor(110, 0), "sin cebolla"},
        {"Café Americano", 1, contracts::Money::from_major_minor(25, 0), contracts::Money::from_major_minor(25, 0), "con hielo"}
    };
    ticket.subtotal = contracts::Money::from_major_minor(135, 0);
    ticket.total = contracts::Money::from_major_minor(135, 0);
    ticket.payment_method = contracts::PaymentMethod::Efectivo;
    ticket.cash_received = contracts::Money::from_major_minor(200, 0);
    ticket.change = contracts::Money::from_major_minor(65, 0);
    return contracts::Result<contracts::TicketDto>::ok(ticket);
}

contracts::Result<std::vector<contracts::TicketSummaryDto>> MockSaleService::get_recent_tickets(int limit) {
    (void)limit;
    std::vector<contracts::TicketSummaryDto> list;
    contracts::TicketSummaryDto item;
    item.folio = "V-20260924-1001";
    item.created_at = "2026-09-24 12:00:00";
    item.total = contracts::Money::from_major_minor(135, 0);
    item.cashier_name = "Cajero de Prueba";
    item.payment_method = contracts::PaymentMethod::Efectivo;
    item.items_count = 3;
    list.push_back(item);
    return contracts::Result<std::vector<contracts::TicketSummaryDto>>::ok(list);
}

// --- MockReportService ---

MockReportService::MockReportService() {}

contracts::Result<contracts::DailySalesReportDto> MockReportService::get_daily_sales_summary(const std::string& date_iso) {
    contracts::DailySalesReportDto report;
    report.date = date_iso;
    report.total_sales_count = 12;
    report.total_amount = contracts::Money::from_major_minor(2930, 0);
    report.cash_amount = contracts::Money::from_major_minor(2000, 0);
    report.card_amount = contracts::Money::from_major_minor(930, 0);
    report.transfer_amount = contracts::Money::zero();
    report.payments_total = contracts::Money::from_major_minor(2930, 0);
    report.payments_mismatch_cents = 0;
    return contracts::Result<contracts::DailySalesReportDto>::ok(report);
}

// --- MockBackupService ---

MockBackupService::MockBackupService() {
    status_.is_enabled = true;
    status_.last_backup_time = "2026-09-24 22:00:00";
    status_.last_status = "EXITOSA";
    status_.last_error = "";

    dev_config_.server_url = "https://backup.cafeteria-itch.edu.mx/api/v1";
    dev_config_.retention_days = "30";
    dev_config_.is_tls_enabled = true;
}

contracts::Result<contracts::BackupStatusDto> MockBackupService::get_status() {
    return contracts::Result<contracts::BackupStatusDto>::ok(status_);
}

contracts::OperationResult MockBackupService::trigger_local_backup(const std::string&) {
    return contracts::OperationResult::ok();
}

contracts::OperationResult MockBackupService::set_backup_enabled(bool enabled) {
    status_.is_enabled = enabled;
    return contracts::OperationResult::ok();
}

contracts::Result<contracts::BackupVerificationDto> MockBackupService::verify_backup(const std::string&) {
    return contracts::Result<contracts::BackupVerificationDto>::fail(
        contracts::ErrorCode::InvalidState, "Servicio simulado: no verifica respaldos reales");
}

contracts::Result<contracts::RestoreResultDto> MockBackupService::restore_from_backup(const std::string&) {
    return contracts::Result<contracts::RestoreResultDto>::fail(
        contracts::ErrorCode::InvalidState, "Servicio simulado: no restaura datos reales");
}

contracts::Result<contracts::BackupServerConfigDto> MockBackupService::get_developer_server_config() {
    return contracts::Result<contracts::BackupServerConfigDto>::ok(dev_config_);
}

contracts::OperationResult MockBackupService::update_developer_server_config(const contracts::BackupServerConfigDto& config) {
    dev_config_ = config;
    return contracts::OperationResult::ok();
}

ServiceRegistry ServiceRegistry::create_mock_registry() {
    ServiceRegistry reg;
    reg.account_service = std::make_shared<MockAccountService>();
    reg.catalog_service = std::make_shared<MockCatalogService>();
    reg.cart_service = std::make_shared<MockCartService>();
    reg.sale_service = std::make_shared<MockSaleService>();
    reg.inventory_service = std::make_shared<MockInventoryService>();
    reg.shift_service = std::make_shared<MockShiftService>();
    reg.report_service = std::make_shared<MockReportService>();
    reg.backup_service = std::make_shared<MockBackupService>();
    reg.print_config_service = std::make_shared<MockPrintConfigService>();
    return reg;
}

ServiceRegistry ServiceRegistry::create_sqlite_registry(storage::SqliteDatabase& db,
                                                          const std::string& config_dir,
                                                          const std::string& migrations_dir) {
    ServiceRegistry reg;
    auto account_svc = std::make_shared<SqliteAccountService>(db);
    reg.account_service = account_svc;
    auto catalog_svc = std::make_shared<SqliteCatalogService>(db, account_svc);
    reg.catalog_service = catalog_svc;
    reg.inventory_service = std::make_shared<SqliteInventoryService>(db, account_svc);
    auto shift_svc = std::make_shared<SqliteShiftService>(db, account_svc);
    reg.shift_service = shift_svc;
    auto cart_svc = std::make_shared<SqliteCartService>(db, account_svc, catalog_svc);
    reg.cart_service = cart_svc;
    auto sale_svc = std::make_shared<SqliteSaleService>(db, account_svc, cart_svc, shift_svc);
    reg.sale_service = sale_svc;
    // Tras restaurar, el estado en memoria derivado de la base se invalida.
    std::weak_ptr<SqliteSaleService> weak_sale = sale_svc;
    reg.backup_service = std::make_shared<SqliteBackupService>(
        db, account_svc, config_dir, migrations_dir,
        [weak_sale]() { if (auto s = weak_sale.lock()) s->invalidate_session_state(); });
    reg.report_service = std::make_shared<SqliteReportService>(db, account_svc);
    reg.print_config_service = std::make_shared<SqlitePrintConfigService>(db, account_svc, config_dir);
    return reg;
}

} // namespace app::services
