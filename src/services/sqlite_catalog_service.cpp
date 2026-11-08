#include "app/services/sqlite_catalog_service.hpp"
#include "app/domain/unit_converter.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <algorithm>

namespace app::services {

namespace {

std::string current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

std::string generate_id(const std::string& prefix) {
    static std::atomic<std::uint64_t> counter{0};
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    std::stringstream ss;
    ss << prefix << "-" << now_ms << "-" << ++counter;
    return ss.str();
}

bool validate_image_header(const std::vector<std::uint8_t>& bytes, std::string& out_mime) {
    if (bytes.size() < 4) return false;

    // JPEG: 0xFF, 0xD8, 0xFF
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) {
        out_mime = "image/jpeg";
        return true;
    }

    // PNG: 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    if (bytes.size() >= 8 &&
        bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G' &&
        bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) {
        out_mime = "image/png";
        return true;
    }

    // WebP: RIFF ... WEBP
    if (bytes.size() >= 12 &&
        bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
        bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P') {
        out_mime = "image/webp";
        return true;
    }

    return false;
}

} // namespace

SqliteCatalogService::SqliteCatalogService(
    storage::SqliteDatabase& db,
    std::shared_ptr<contracts::IAccountService> account_service
) : db_(db), account_service_(std::move(account_service)) {}

contracts::OperationResult SqliteCatalogService::check_permission_or_fail(std::string_view perm) const {
    if (!account_service_) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Servicio de cuentas no configurado"
        );
    }
    if (!account_service_->is_authenticated()) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Se requiere inicio de sesión"
        );
    }
    if (!account_service_->has_permission(perm)) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Permiso insuficiente: " + std::string(perm)
        );
    }
    return contracts::OperationResult::ok();
}

contracts::Result<std::vector<contracts::ProductDto>> SqliteCatalogService::search_products(
    const std::string& query,
    const std::optional<std::string>& category,
    std::size_t offset,
    std::size_t limit,
    bool include_inactive
) {
    if (account_service_ && account_service_->is_authenticated()) {
        if (!account_service_->has_permission("inventory.view") &&
            !account_service_->has_permission("pos.sell") &&
            !account_service_->has_permission("pos.visual_catalog")) {
            return contracts::Result<std::vector<contracts::ProductDto>>::fail(
                contracts::ErrorCode::Forbidden,
                "Permiso insuficiente para consultar catálogo"
            );
        }
    }

    std::stringstream sql;
    sql << "SELECT p.id, p.sku, p.barcode, p.name, p.category, p.price_cents, "
        << "p.is_prepared, p.is_active, (CASE WHEN img.id IS NOT NULL THEN 1 ELSE 0 END) AS has_img "
        << "FROM products p "
        << "LEFT JOIN product_images img ON p.id = img.product_id "
        << "WHERE 1=1 ";

    if (!include_inactive) {
        sql << "AND p.is_active = 1 ";
    }
    if (category.has_value() && !category.value().empty()) {
        sql << "AND p.category = ? ";
    }
    if (!query.empty()) {
        sql << "AND (p.sku LIKE ? OR p.barcode LIKE ? OR p.name LIKE ?) ";
    }
    sql << "ORDER BY p.name ASC LIMIT ? OFFSET ?;";

    auto prep = db_.prepare(sql.str());
    if (!prep.is_success()) {
        return contracts::Result<std::vector<contracts::ProductDto>>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    int param_idx = 1;
    if (category.has_value() && !category.value().empty()) {
        stmt.bind_text(param_idx++, category.value());
    }
    if (!query.empty()) {
        std::string pattern = "%" + query + "%";
        stmt.bind_text(param_idx++, pattern);
        stmt.bind_text(param_idx++, pattern);
        stmt.bind_text(param_idx++, pattern);
    }
    stmt.bind_int64(param_idx++, static_cast<std::int64_t>(limit > 0 ? limit : 50));
    stmt.bind_int64(param_idx++, static_cast<std::int64_t>(offset));

    std::vector<contracts::ProductDto> products;
    while (true) {
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<std::vector<contracts::ProductDto>>::fail(step.error_code(), step.error_message());
        }
        if (!step.value()) break;

        contracts::ProductDto dto;
        dto.id = stmt.get_text(0);
        dto.sku = stmt.get_text(1);
        dto.barcode = stmt.is_null(2) ? "" : stmt.get_text(2);
        dto.name = stmt.get_text(3);
        dto.category = stmt.get_text(4);
        dto.price = contracts::Money(stmt.get_int64(5));
        dto.is_prepared = stmt.get_int64(6) != 0;
        dto.is_active = stmt.get_int64(7) != 0;
        dto.has_image = stmt.get_int64(8) != 0;
        dto.stock_units = 0;

        // Si es producto preparado, calcular porciones disponibles según su receta vigente
        if (dto.is_prepared) {
            auto prep_rec = db_.prepare(
                "SELECT ri.quantity_base_units, i.current_stock "
                "FROM recipes r "
                "JOIN recipe_items ri ON r.id = ri.recipe_id "
                "JOIN ingredients i ON ri.ingredient_id = i.id "
                "WHERE r.id = ("
                "    SELECT id FROM recipes WHERE product_id = ? ORDER BY version DESC LIMIT 1"
                ");"
            );
            if (prep_rec.is_success()) {
                auto stmt_rec = std::move(prep_rec.value());
                stmt_rec.bind_text(1, dto.id);
                std::int64_t min_portions = -1;
                while (true) {
                    auto s = stmt_rec.step();
                    if (!s.is_success() || !s.value()) break;
                    std::int64_t qty_req = stmt_rec.get_int64(0);
                    std::int64_t stock_avail = stmt_rec.get_int64(1);
                    if (qty_req > 0) {
                        std::int64_t portions = stock_avail / qty_req;
                        if (portions < 0) portions = 0;
                        if (min_portions < 0 || portions < min_portions) {
                            min_portions = portions;
                        }
                    }
                }
                if (min_portions >= 0) {
                    dto.stock_units = min_portions;
                }
            }
        }

        products.push_back(dto);
    }

    return contracts::Result<std::vector<contracts::ProductDto>>::ok(products);
}

contracts::Result<contracts::ProductDto> SqliteCatalogService::get_product_by_id(const std::string& product_id) {
    auto prep = db_.prepare(
        "SELECT p.id, p.sku, p.barcode, p.name, p.category, p.price_cents, "
        "p.is_prepared, p.is_active, (CASE WHEN img.id IS NOT NULL THEN 1 ELSE 0 END) "
        "FROM products p "
        "LEFT JOIN product_images img ON p.id = img.product_id "
        "WHERE p.id = ?;"
    );
    if (!prep.is_success()) {
        return contracts::Result<contracts::ProductDto>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, product_id);
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::Result<contracts::ProductDto>::fail(step.error_code(), step.error_message());
    }
    if (!step.value()) {
        return contracts::Result<contracts::ProductDto>::fail(contracts::ErrorCode::NotFound, "Producto no encontrado");
    }

    contracts::ProductDto dto;
    dto.id = stmt.get_text(0);
    dto.sku = stmt.get_text(1);
    dto.barcode = stmt.is_null(2) ? "" : stmt.get_text(2);
    dto.name = stmt.get_text(3);
    dto.category = stmt.get_text(4);
    dto.price = contracts::Money(stmt.get_int64(5));
    dto.is_prepared = stmt.get_int64(6) != 0;
    dto.is_active = stmt.get_int64(7) != 0;
    dto.has_image = stmt.get_int64(8) != 0;
    dto.stock_units = 0;

    return contracts::Result<contracts::ProductDto>::ok(dto);
}

contracts::Result<contracts::ProductDto> SqliteCatalogService::get_product_by_sku_or_barcode(const std::string& code) {
    auto prep = db_.prepare(
        "SELECT p.id, p.sku, p.barcode, p.name, p.category, p.price_cents, "
        "p.is_prepared, p.is_active, (CASE WHEN img.id IS NOT NULL THEN 1 ELSE 0 END) "
        "FROM products p "
        "LEFT JOIN product_images img ON p.id = img.product_id "
        "WHERE p.sku = ? OR p.barcode = ?;"
    );
    if (!prep.is_success()) {
        return contracts::Result<contracts::ProductDto>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, code);
    stmt.bind_text(2, code);
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::Result<contracts::ProductDto>::fail(step.error_code(), step.error_message());
    }
    if (!step.value()) {
        return contracts::Result<contracts::ProductDto>::fail(contracts::ErrorCode::NotFound, "Producto no encontrado con el código especificado");
    }

    contracts::ProductDto dto;
    dto.id = stmt.get_text(0);
    dto.sku = stmt.get_text(1);
    dto.barcode = stmt.is_null(2) ? "" : stmt.get_text(2);
    dto.name = stmt.get_text(3);
    dto.category = stmt.get_text(4);
    dto.price = contracts::Money(stmt.get_int64(5));
    dto.is_prepared = stmt.get_int64(6) != 0;
    dto.is_active = stmt.get_int64(7) != 0;
    dto.has_image = stmt.get_int64(8) != 0;
    dto.stock_units = 0;

    return contracts::Result<contracts::ProductDto>::ok(dto);
}

contracts::Result<std::vector<std::string>> SqliteCatalogService::list_categories() {
    auto prep = db_.prepare("SELECT DISTINCT category FROM products WHERE is_active = 1 ORDER BY category ASC;");
    if (!prep.is_success()) {
        return contracts::Result<std::vector<std::string>>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    std::vector<std::string> cats;
    while (true) {
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<std::vector<std::string>>::fail(step.error_code(), step.error_message());
        }
        if (!step.value()) break;
        cats.push_back(stmt.get_text(0));
    }
    return contracts::Result<std::vector<std::string>>::ok(cats);
}

contracts::Result<contracts::ProductDto> SqliteCatalogService::create_product(const contracts::CreateProductDto& input) {
    auto perm_res = check_permission_or_fail("inventory.manage_products");
    if (!perm_res.is_success()) {
        return contracts::Result<contracts::ProductDto>::fail(perm_res.error_code(), perm_res.error_message());
    }

    if (input.sku.empty()) {
        return contracts::Result<contracts::ProductDto>::fail(contracts::ErrorCode::InvalidInput, "El SKU es obligatorio");
    }
    if (input.name.empty()) {
        return contracts::Result<contracts::ProductDto>::fail(contracts::ErrorCode::InvalidInput, "El nombre del producto es obligatorio");
    }
    if (input.category.empty()) {
        return contracts::Result<contracts::ProductDto>::fail(contracts::ErrorCode::InvalidInput, "La categoría es obligatoria");
    }
    if (input.price.cents() < 0) {
        return contracts::Result<contracts::ProductDto>::fail(contracts::ErrorCode::InvalidInput, "El precio no puede ser negativo");
    }

    // Comprobar unicidad de SKU
    auto prep_sku = db_.prepare("SELECT COUNT(*) FROM products WHERE sku = ?;");
    if (prep_sku.is_success()) {
        auto stmt_sku = std::move(prep_sku.value());
        stmt_sku.bind_text(1, input.sku);
        if (stmt_sku.step().is_success() && stmt_sku.get_int64(0) > 0) {
            return contracts::Result<contracts::ProductDto>::fail(contracts::ErrorCode::Conflict, "El SKU ya existe");
        }
    }

    // Comprobar unicidad de código de barras si fue provisto
    if (!input.barcode.empty()) {
        auto prep_bc = db_.prepare("SELECT COUNT(*) FROM products WHERE barcode = ?;");
        if (prep_bc.is_success()) {
            auto stmt_bc = std::move(prep_bc.value());
            stmt_bc.bind_text(1, input.barcode);
            if (stmt_bc.step().is_success() && stmt_bc.get_int64(0) > 0) {
                return contracts::Result<contracts::ProductDto>::fail(contracts::ErrorCode::Conflict, "El código de barras ya existe");
            }
        }
    }

    std::string product_id = generate_id("prod");
    std::string now_str = current_timestamp();

    auto prep_ins = db_.prepare(
        "INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 1, ?);"
    );
    if (!prep_ins.is_success()) {
        return contracts::Result<contracts::ProductDto>::fail(prep_ins.error_code(), prep_ins.error_message());
    }

    auto stmt_ins = std::move(prep_ins.value());
    stmt_ins.bind_text(1, product_id);
    stmt_ins.bind_text(2, input.sku);
    if (input.barcode.empty()) {
        stmt_ins.bind_null(3);
    } else {
        stmt_ins.bind_text(3, input.barcode);
    }
    stmt_ins.bind_text(4, input.name);
    stmt_ins.bind_text(5, input.category);
    stmt_ins.bind_int64(6, input.price.cents());
    stmt_ins.bind_int64(7, input.is_prepared ? 1 : 0);
    stmt_ins.bind_text(8, now_str);

    auto step_ins = stmt_ins.step();
    if (!step_ins.is_success()) {
        return contracts::Result<contracts::ProductDto>::fail(step_ins.error_code(), step_ins.error_message());
    }

    if (account_service_) {
        account_service_->record_audit_event("PRODUCT_CREATED", "Producto creado: " + input.name + " (" + input.sku + ")");
    }

    contracts::ProductDto dto;
    dto.id = product_id;
    dto.sku = input.sku;
    dto.barcode = input.barcode;
    dto.name = input.name;
    dto.category = input.category;
    dto.price = input.price;
    dto.is_prepared = input.is_prepared;
    dto.is_active = true;
    dto.has_image = false;
    dto.stock_units = 0;

    return contracts::Result<contracts::ProductDto>::ok(dto);
}

contracts::OperationResult SqliteCatalogService::update_product(const contracts::UpdateProductDto& input) {
    auto perm_res = check_permission_or_fail("inventory.manage_products");
    if (!perm_res.is_success()) return perm_res;

    if (input.product_id.empty()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "ID de producto requerido");
    }

    auto current_prod = get_product_by_id(input.product_id);
    if (!current_prod.is_success()) {
        return contracts::OperationResult::fail(current_prod.error_code(), current_prod.error_message());
    }

    if (input.price.has_value() && input.price.value().cents() < 0) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "El precio no puede ser negativo");
    }

    if (input.sku.has_value() && !input.sku.value().empty()) {
        auto prep_sku = db_.prepare("SELECT COUNT(*) FROM products WHERE sku = ? AND id != ?;");
        if (prep_sku.is_success()) {
            auto stmt_sku = std::move(prep_sku.value());
            stmt_sku.bind_text(1, input.sku.value());
            stmt_sku.bind_text(2, input.product_id);
            if (stmt_sku.step().is_success() && stmt_sku.get_int64(0) > 0) {
                return contracts::OperationResult::fail(contracts::ErrorCode::Conflict, "El SKU ya existe en otro producto");
            }
        }
    }

    if (input.barcode.has_value() && !input.barcode.value().empty()) {
        auto prep_bc = db_.prepare("SELECT COUNT(*) FROM products WHERE barcode = ? AND id != ?;");
        if (prep_bc.is_success()) {
            auto stmt_bc = std::move(prep_bc.value());
            stmt_bc.bind_text(1, input.barcode.value());
            stmt_bc.bind_text(2, input.product_id);
            if (stmt_bc.step().is_success() && stmt_bc.get_int64(0) > 0) {
                return contracts::OperationResult::fail(contracts::ErrorCode::Conflict, "El código de barras ya existe en otro producto");
            }
        }
    }

    std::string new_sku = input.sku.value_or(current_prod.value().sku);
    std::string new_barcode = input.barcode.value_or(current_prod.value().barcode);
    std::string new_name = input.name.value_or(current_prod.value().name);
    std::string new_category = input.category.value_or(current_prod.value().category);
    contracts::Money new_price = input.price.value_or(current_prod.value().price);
    bool new_is_prepared = input.is_prepared.value_or(current_prod.value().is_prepared);
    bool new_is_active = input.is_active.value_or(current_prod.value().is_active);

    auto prep = db_.prepare(
        "UPDATE products SET sku = ?, barcode = ?, name = ?, category = ?, "
        "price_cents = ?, is_prepared = ?, is_active = ? WHERE id = ?;"
    );
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, new_sku);
    if (new_barcode.empty()) {
        stmt.bind_null(2);
    } else {
        stmt.bind_text(2, new_barcode);
    }
    stmt.bind_text(3, new_name);
    stmt.bind_text(4, new_category);
    stmt.bind_int64(5, new_price.cents());
    stmt.bind_int64(6, new_is_prepared ? 1 : 0);
    stmt.bind_int64(7, new_is_active ? 1 : 0);
    stmt.bind_text(8, input.product_id);

    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }

    if (account_service_) {
        account_service_->record_audit_event("PRODUCT_UPDATED", "Producto actualizado: " + new_name + " (" + input.product_id + ")");
    }

    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteCatalogService::deactivate_product(const std::string& product_id) {
    auto perm_res = check_permission_or_fail("inventory.manage_products");
    if (!perm_res.is_success()) return perm_res;

    auto current_prod = get_product_by_id(product_id);
    if (!current_prod.is_success()) {
        return contracts::OperationResult::fail(current_prod.error_code(), current_prod.error_message());
    }

    auto prep = db_.prepare("UPDATE products SET is_active = 0 WHERE id = ?;");
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, product_id);
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }

    if (account_service_) {
        account_service_->record_audit_event("PRODUCT_DEACTIVATED", "Producto desactivado: " + current_prod.value().name + " (" + product_id + ")");
    }

    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteCatalogService::reactivate_product(const std::string& product_id) {
    auto perm_res = check_permission_or_fail("inventory.manage_products");
    if (!perm_res.is_success()) return perm_res;

    auto current_prod = get_product_by_id(product_id);
    if (!current_prod.is_success()) {
        return contracts::OperationResult::fail(current_prod.error_code(), current_prod.error_message());
    }

    auto prep = db_.prepare("UPDATE products SET is_active = 1 WHERE id = ?;");
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, product_id);
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }

    if (account_service_) {
        account_service_->record_audit_event("PRODUCT_REACTIVATED", "Producto reactivado: " + current_prod.value().name + " (" + product_id + ")");
    }

    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteCatalogService::set_product_image(
    const std::string& product_id,
    const std::string& /*mime_type*/,
    const std::vector<std::uint8_t>& image_bytes
) {
    auto perm_res = check_permission_or_fail("inventory.manage_products");
    if (!perm_res.is_success()) return perm_res;

    auto current_prod = get_product_by_id(product_id);
    if (!current_prod.is_success()) {
        return contracts::OperationResult::fail(current_prod.error_code(), current_prod.error_message());
    }

    // Validar tamaño máximo: 100 KB = 102,400 bytes
    constexpr std::size_t MAX_IMAGE_BYTES = 100 * 1024;
    if (image_bytes.size() > MAX_IMAGE_BYTES) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "La imagen excede el límite máximo de 100 KB"
        );
    }

    // Validar bytes mágicos para formatos soportados (JPEG, PNG, WebP)
    std::string detected_mime;
    if (!validate_image_header(image_bytes, detected_mime)) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "Formato de imagen inválido o no reconocido. Se admiten JPEG, PNG y WebP"
        );
    }

    std::string img_id = "img-" + product_id;
    auto prep = db_.prepare(
        "INSERT OR REPLACE INTO product_images (id, product_id, mime_type, image_blob, thumbnail_blob) "
        "VALUES (?, ?, ?, ?, ?);"
    );
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, img_id);
    stmt.bind_text(2, product_id);
    stmt.bind_text(3, detected_mime);
    stmt.bind_blob(4, image_bytes);
    stmt.bind_blob(5, image_bytes); // Miniatura preservada

    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }

    if (account_service_) {
        account_service_->record_audit_event("PRODUCT_IMAGE_SET", "Imagen asignada para producto: " + product_id);
    }

    return contracts::OperationResult::ok();
}

contracts::Result<contracts::ProductImageDto> SqliteCatalogService::get_product_image(const std::string& product_id) {
    auto prep = db_.prepare("SELECT mime_type, image_blob, thumbnail_blob FROM product_images WHERE product_id = ?;");
    if (!prep.is_success()) {
        return contracts::Result<contracts::ProductImageDto>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, product_id);
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::Result<contracts::ProductImageDto>::fail(step.error_code(), step.error_message());
    }
    if (!step.value()) {
        return contracts::Result<contracts::ProductImageDto>::fail(contracts::ErrorCode::NotFound, "No se encontró imagen para el producto");
    }

    contracts::ProductImageDto dto;
    dto.product_id = product_id;
    dto.mime_type = stmt.get_text(0);
    dto.image_blob = stmt.get_blob(1);
    dto.thumbnail_blob = stmt.get_blob(2);

    return contracts::Result<contracts::ProductImageDto>::ok(dto);
}

contracts::OperationResult SqliteCatalogService::remove_product_image(const std::string& product_id) {
    auto perm_res = check_permission_or_fail("inventory.manage_products");
    if (!perm_res.is_success()) return perm_res;

    auto prep = db_.prepare("DELETE FROM product_images WHERE product_id = ?;");
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, product_id);
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }

    if (account_service_) {
        account_service_->record_audit_event("PRODUCT_IMAGE_REMOVED", "Imagen eliminada para producto: " + product_id);
    }

    return contracts::OperationResult::ok();
}

contracts::Result<contracts::RecipeDto> SqliteCatalogService::get_recipe(const std::string& product_id) {
    auto current_prod = get_product_by_id(product_id);
    if (!current_prod.is_success()) {
        return contracts::Result<contracts::RecipeDto>::fail(current_prod.error_code(), current_prod.error_message());
    }

    auto prep_rec = db_.prepare(
        "SELECT id, version FROM recipes WHERE product_id = ? ORDER BY version DESC LIMIT 1;"
    );
    if (!prep_rec.is_success()) {
        return contracts::Result<contracts::RecipeDto>::fail(prep_rec.error_code(), prep_rec.error_message());
    }

    auto stmt_rec = std::move(prep_rec.value());
    stmt_rec.bind_text(1, product_id);
    auto step_rec = stmt_rec.step();
    if (!step_rec.is_success()) {
        return contracts::Result<contracts::RecipeDto>::fail(step_rec.error_code(), step_rec.error_message());
    }
    if (!step_rec.value()) {
        return contracts::Result<contracts::RecipeDto>::fail(contracts::ErrorCode::NotFound, "No existe receta configurada para este producto");
    }

    contracts::RecipeDto recipe;
    recipe.recipe_id = stmt_rec.get_text(0);
    recipe.product_id = product_id;
    recipe.version = static_cast<int>(stmt_rec.get_int64(1));

    // Obtener los ingredientes de la receta
    auto prep_items = db_.prepare(
        "SELECT ri.ingredient_id, i.name, ri.quantity_base_units, i.base_unit "
        "FROM recipe_items ri "
        "JOIN ingredients i ON ri.ingredient_id = i.id "
        "WHERE ri.recipe_id = ? "
        "ORDER BY i.name ASC;"
    );
    if (!prep_items.is_success()) {
        return contracts::Result<contracts::RecipeDto>::fail(prep_items.error_code(), prep_items.error_message());
    }

    auto stmt_items = std::move(prep_items.value());
    stmt_items.bind_text(1, recipe.recipe_id);

    std::int64_t total_estimated_cents = 0;
    while (true) {
        auto step_it = stmt_items.step();
        if (!step_it.is_success()) {
            return contracts::Result<contracts::RecipeDto>::fail(step_it.error_code(), step_it.error_message());
        }
        if (!step_it.value()) break;

        contracts::RecipeItemDto item;
        item.ingredient_id = stmt_items.get_text(0);
        item.ingredient_name = stmt_items.get_text(1);
        item.quantity_base_units = stmt_items.get_int64(2);
        item.base_unit = stmt_items.get_text(3);
        recipe.items.push_back(item);

        // Calcular costo proporcional exacto desde la última entrada de compra
        auto prep_pe = db_.prepare(
            "SELECT total_cost_cents, base_units_purchased FROM purchase_entries "
            "WHERE ingredient_id = ? ORDER BY rowid DESC LIMIT 1;"
        );
        if (prep_pe.is_success()) {
            auto stmt_pe = std::move(prep_pe.value());
            stmt_pe.bind_text(1, item.ingredient_id);
            auto step_pe = stmt_pe.step();
            if (step_pe.is_success() && step_pe.value()) {
                // Hay una entrada de compra registrada
                std::int64_t pe_cost_cents = stmt_pe.get_int64(0);
                std::int64_t pe_base_units = stmt_pe.get_int64(1);
                if (pe_base_units > 0 && pe_cost_cents > 0) {
                    auto prop_cost = domain::UnitConverter::calculate_proportional_cost(
                        contracts::Money(pe_cost_cents),
                        pe_base_units,
                        item.quantity_base_units
                    );
                    if (prop_cost.is_success()) {
                        total_estimated_cents += prop_cost.value().cents();
                    }
                }
            }
        }
    }

    recipe.estimated_cost_per_portion = contracts::Money(total_estimated_cents);
    return contracts::Result<contracts::RecipeDto>::ok(recipe);
}

contracts::OperationResult SqliteCatalogService::set_recipe(
    const std::string& product_id,
    const std::vector<contracts::RecipeItemDto>& items
) {
    auto perm_res = check_permission_or_fail("inventory.manage_products");
    if (!perm_res.is_success()) return perm_res;

    auto current_prod = get_product_by_id(product_id);
    if (!current_prod.is_success()) {
        return contracts::OperationResult::fail(current_prod.error_code(), current_prod.error_message());
    }

    if (items.empty()) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "Una receta debe contener al menos un ingrediente"
        );
    }

    for (const auto& it : items) {
        if (it.ingredient_id.empty()) {
            return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "ID de ingrediente inválido");
        }
        if (it.quantity_base_units <= 0) {
            return contracts::OperationResult::fail(
                contracts::ErrorCode::InvalidInput,
                "La cantidad de ingrediente por porción debe ser positiva"
            );
        }

        // Comprobar que el ingrediente existe
        auto prep_chk = db_.prepare("SELECT COUNT(*) FROM ingredients WHERE id = ?;");
        if (!prep_chk.is_success()) {
            return contracts::OperationResult::fail(prep_chk.error_code(), prep_chk.error_message());
        }
        auto stmt_chk = std::move(prep_chk.value());
        stmt_chk.bind_text(1, it.ingredient_id);
        if (!stmt_chk.step().is_success() || stmt_chk.get_int64(0) == 0) {
            return contracts::OperationResult::fail(
                contracts::ErrorCode::NotFound,
                "Ingrediente no encontrado: " + it.ingredient_id
            );
        }
    }

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return tx;

    // Obtener versión anterior si existe para incrementar número de versión
    int new_version = 1;
    auto prep_v = db_.prepare("SELECT version FROM recipes WHERE product_id = ? ORDER BY version DESC LIMIT 1;");
    if (prep_v.is_success()) {
        auto stmt_v = std::move(prep_v.value());
        stmt_v.bind_text(1, product_id);
        auto step_v = stmt_v.step();
        if (step_v.is_success() && step_v.value()) {
            new_version = static_cast<int>(stmt_v.get_int64(0)) + 1;
        }
    }

    // Se conservan las recetas anteriores en la base de datos para preservar el historial de versiones.
    // Las ventas y consultas vigentes utilizan automáticamente la versión más reciente (ORDER BY version DESC LIMIT 1).

    std::string recipe_id = generate_id("rcp");
    std::string now_str = current_timestamp();

    auto prep_rcp = db_.prepare("INSERT INTO recipes (id, product_id, version, created_at) VALUES (?, ?, ?, ?);");
    if (!prep_rcp.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(prep_rcp.error_code(), prep_rcp.error_message());
    }

    auto stmt_rcp = std::move(prep_rcp.value());
    stmt_rcp.bind_text(1, recipe_id);
    stmt_rcp.bind_text(2, product_id);
    stmt_rcp.bind_int64(3, new_version);
    stmt_rcp.bind_text(4, now_str);
    auto step_rcp = stmt_rcp.step();
    if (!step_rcp.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(step_rcp.error_code(), step_rcp.error_message());
    }

    // Insertar cada item de la receta
    for (const auto& it : items) {
        std::string item_id = generate_id("rcpit");
        auto prep_it = db_.prepare(
            "INSERT INTO recipe_items (id, recipe_id, ingredient_id, quantity_base_units) "
            "VALUES (?, ?, ?, ?);"
        );
        if (!prep_it.is_success()) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(prep_it.error_code(), prep_it.error_message());
        }

        auto stmt_it = std::move(prep_it.value());
        stmt_it.bind_text(1, item_id);
        stmt_it.bind_text(2, recipe_id);
        stmt_it.bind_text(3, it.ingredient_id);
        stmt_it.bind_int64(4, it.quantity_base_units);
        auto step_it = stmt_it.step();
        if (!step_it.is_success()) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(step_it.error_code(), step_it.error_message());
        }
    }

    // Si el producto no estaba marcado como preparado, actualizarlo a preparado
    if (!current_prod.value().is_prepared) {
        auto prep_prep = db_.prepare("UPDATE products SET is_prepared = 1 WHERE id = ?;");
        if (prep_prep.is_success()) {
            auto stmt_prep = std::move(prep_prep.value());
            stmt_prep.bind_text(1, product_id);
            stmt_prep.step();
        }
    }

    auto commit_res = db_.commit_transaction();
    if (!commit_res.is_success()) {
        db_.rollback_transaction();
        return commit_res;
    }

    if (account_service_) {
        account_service_->record_audit_event(
            "RECIPE_UPDATED",
            "Receta actualizada para producto: " + current_prod.value().name + " (" + product_id + ") v" + std::to_string(new_version)
        );
    }

    return contracts::OperationResult::ok();
}

} // namespace app::services
