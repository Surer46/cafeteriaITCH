#include "app/services/sqlite_inventory_service.hpp"
#include "app/services/identifiers.hpp"
#include "app/domain/clock.hpp"
#include "app/domain/unit_converter.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <algorithm>

namespace app::services {

namespace {

std::string expected_base_unit_for(domain::UnitDimension dim) {
    switch (dim) {
        case domain::UnitDimension::Mass: return "mg";
        case domain::UnitDimension::Volume: return "ml";
        case domain::UnitDimension::Count: return "pz";
        default: return "";
    }
}

} // namespace

SqliteInventoryService::SqliteInventoryService(
    storage::SqliteDatabase& db,
    std::shared_ptr<contracts::IAccountService> account_service
) : db_(db), account_service_(std::move(account_service)) {}

contracts::OperationResult SqliteInventoryService::check_permission_or_fail(std::string_view perm) const {
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

contracts::Result<contracts::IngredientDto> SqliteInventoryService::create_ingredient(
    const contracts::CreateIngredientDto& input
) {
    auto perm_res = check_permission_or_fail("inventory.manage_products");
    if (!perm_res.is_success()) {
        return contracts::Result<contracts::IngredientDto>::fail(perm_res.error_code(), perm_res.error_message());
    }

    if (input.name.empty()) {
        return contracts::Result<contracts::IngredientDto>::fail(contracts::ErrorCode::InvalidInput, "El nombre del ingrediente es obligatorio");
    }

    auto dim_res = domain::UnitConverter::parse_dimension(input.unit_dimension);
    if (!dim_res.is_success()) {
        return contracts::Result<contracts::IngredientDto>::fail(
            contracts::ErrorCode::InvalidInput,
            "Dimensión inválida: " + input.unit_dimension + ". Debe ser MASS, VOLUME o COUNT"
        );
    }

    std::string expected_base = expected_base_unit_for(dim_res.value());
    if (input.base_unit != expected_base) {
        return contracts::Result<contracts::IngredientDto>::fail(
            contracts::ErrorCode::InvalidInput,
            "Unidad base incompatible con la dimensión " + input.unit_dimension + ": se esperaba '" + expected_base + "' pero se recibió '" + input.base_unit + "'"
        );
    }

    if (input.low_stock_threshold_base < 0) {
        return contracts::Result<contracts::IngredientDto>::fail(
            contracts::ErrorCode::InvalidInput,
            "El umbral de bajo stock no puede ser negativo"
        );
    }

    // Comprobar unicidad de nombre de ingrediente
    auto prep_chk = db_.prepare("SELECT COUNT(*) FROM ingredients WHERE name = ?;");
    if (prep_chk.is_success()) {
        auto stmt_chk = std::move(prep_chk.value());
        stmt_chk.bind_text(1, input.name);
        auto step_chk = stmt_chk.step();
        if (step_chk.is_success() && step_chk.value() && stmt_chk.get_int64(0) > 0) {
            return contracts::Result<contracts::IngredientDto>::fail(
                contracts::ErrorCode::Conflict,
                "Ya existe un ingrediente con ese nombre"
            );
        }
    }

    std::string ing_id = generate_id("ing");
    std::string dim_canonical = domain::UnitConverter::to_string(dim_res.value());

    auto prep_ins = db_.prepare(
        "INSERT INTO ingredients (id, name, unit_dimension, base_unit, current_stock, low_stock_threshold, updated_at, updated_at_utc) "
        "VALUES (?, ?, ?, ?, 0, ?, ?, ?);"
    );
    if (!prep_ins.is_success()) {
        return contracts::Result<contracts::IngredientDto>::fail(prep_ins.error_code(), prep_ins.error_message());
    }

    auto stmt_ins = std::move(prep_ins.value());
    stmt_ins.bind_text(1, ing_id);
    stmt_ins.bind_text(2, input.name);
    stmt_ins.bind_text(3, dim_canonical);
    stmt_ins.bind_text(4, input.base_unit);
    stmt_ins.bind_int64(5, input.low_stock_threshold_base);
    const auto ing_ts = domain::now();
    stmt_ins.bind_text(6, ing_ts.local);
    stmt_ins.bind_text(7, ing_ts.utc);

    auto step_ins = stmt_ins.step();
    if (!step_ins.is_success()) {
        return contracts::Result<contracts::IngredientDto>::fail(step_ins.error_code(), step_ins.error_message());
    }

    if (account_service_) {
        account_service_->record_audit_event("INGREDIENT_CREATED", "Ingrediente creado: " + input.name + " (" + ing_id + ")");
    }

    contracts::IngredientDto dto;
    dto.id = ing_id;
    dto.name = input.name;
    dto.unit_dimension = dim_canonical;
    dto.base_unit = input.base_unit;
    dto.current_stock_base = 0;
    dto.low_stock_threshold_base = input.low_stock_threshold_base;

    return contracts::Result<contracts::IngredientDto>::ok(dto);
}

contracts::OperationResult SqliteInventoryService::update_ingredient(const contracts::UpdateIngredientDto& input) {
    auto perm_res = check_permission_or_fail("inventory.manage_products");
    if (!perm_res.is_success()) return perm_res;

    if (input.ingredient_id.empty()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "ID de ingrediente requerido");
    }

    if (input.low_stock_threshold_base.has_value()) {
        auto perm_thresh = check_permission_or_fail("inventory.manage_thresholds");
        if (!perm_thresh.is_success()) return perm_thresh;
        if (input.low_stock_threshold_base.value() < 0) {
            return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "El umbral no puede ser negativo");
        }
    }

    auto current_ing = get_ingredient(input.ingredient_id);
    if (!current_ing.is_success()) {
        return contracts::OperationResult::fail(current_ing.error_code(), current_ing.error_message());
    }

    if (input.name.has_value() && !input.name.value().empty()) {
        auto prep_chk = db_.prepare("SELECT COUNT(*) FROM ingredients WHERE name = ? AND id != ?;");
        if (prep_chk.is_success()) {
            auto stmt_chk = std::move(prep_chk.value());
            stmt_chk.bind_text(1, input.name.value());
            stmt_chk.bind_text(2, input.ingredient_id);
            auto step_chk = stmt_chk.step();
            if (step_chk.is_success() && step_chk.value() && stmt_chk.get_int64(0) > 0) {
                return contracts::OperationResult::fail(contracts::ErrorCode::Conflict, "Ya existe otro ingrediente con ese nombre");
            }
        }
    }

    std::string new_name = input.name.value_or(current_ing.value().name);
    std::int64_t new_threshold = input.low_stock_threshold_base.value_or(current_ing.value().low_stock_threshold_base);

    auto prep = db_.prepare("UPDATE ingredients SET name = ?, low_stock_threshold = ?, updated_at = ?, updated_at_utc = ? WHERE id = ?;");
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, new_name);
    stmt.bind_int64(2, new_threshold);
    const auto upd_ts = domain::now();
    stmt.bind_text(3, upd_ts.local);
    stmt.bind_text(4, upd_ts.utc);
    stmt.bind_text(5, input.ingredient_id);

    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }

    if (account_service_) {
        account_service_->record_audit_event("INGREDIENT_UPDATED", "Ingrediente actualizado: " + new_name + " (" + input.ingredient_id + ")");
    }

    return contracts::OperationResult::ok();
}

contracts::Result<std::vector<contracts::IngredientDto>> SqliteInventoryService::list_ingredients() {
    if (account_service_ && account_service_->is_authenticated()) {
        if (!account_service_->has_permission("inventory.view") &&
            !account_service_->has_permission("inventory.manage_products")) {
            return contracts::Result<std::vector<contracts::IngredientDto>>::fail(
                contracts::ErrorCode::Forbidden,
                "Permiso insuficiente para consultar inventario"
            );
        }
    }

    auto prep = db_.prepare(
        "SELECT id, name, unit_dimension, base_unit, current_stock, low_stock_threshold "
        "FROM ingredients ORDER BY name ASC;"
    );
    if (!prep.is_success()) {
        return contracts::Result<std::vector<contracts::IngredientDto>>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    std::vector<contracts::IngredientDto> list;
    while (true) {
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<std::vector<contracts::IngredientDto>>::fail(step.error_code(), step.error_message());
        }
        if (!step.value()) break;

        contracts::IngredientDto dto;
        dto.id = stmt.get_text(0);
        dto.name = stmt.get_text(1);
        dto.unit_dimension = stmt.get_text(2);
        dto.base_unit = stmt.get_text(3);
        dto.current_stock_base = stmt.get_int64(4);
        dto.low_stock_threshold_base = stmt.get_int64(5);
        list.push_back(dto);
    }

    return contracts::Result<std::vector<contracts::IngredientDto>>::ok(list);
}

contracts::Result<contracts::IngredientDto> SqliteInventoryService::get_ingredient(const std::string& ingredient_id) {
    if (account_service_ && account_service_->is_authenticated()) {
        if (!account_service_->has_permission("inventory.view") &&
            !account_service_->has_permission("inventory.manage_products")) {
            return contracts::Result<contracts::IngredientDto>::fail(
                contracts::ErrorCode::Forbidden,
                "Permiso insuficiente para consultar ingrediente"
            );
        }
    }

    auto prep = db_.prepare(
        "SELECT id, name, unit_dimension, base_unit, current_stock, low_stock_threshold "
        "FROM ingredients WHERE id = ?;"
    );
    if (!prep.is_success()) {
        return contracts::Result<contracts::IngredientDto>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, ingredient_id);
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::Result<contracts::IngredientDto>::fail(step.error_code(), step.error_message());
    }
    if (!step.value()) {
        return contracts::Result<contracts::IngredientDto>::fail(contracts::ErrorCode::NotFound, "Ingrediente no encontrado");
    }

    contracts::IngredientDto dto;
    dto.id = stmt.get_text(0);
    dto.name = stmt.get_text(1);
    dto.unit_dimension = stmt.get_text(2);
    dto.base_unit = stmt.get_text(3);
    dto.current_stock_base = stmt.get_int64(4);
    dto.low_stock_threshold_base = stmt.get_int64(5);

    return contracts::Result<contracts::IngredientDto>::ok(dto);
}

contracts::Result<std::vector<contracts::StockAlertDto>> SqliteInventoryService::get_stock_alerts() {
    if (account_service_ && account_service_->is_authenticated()) {
        if (!account_service_->has_permission("inventory.view") &&
            !account_service_->has_permission("inventory.manage_products")) {
            return contracts::Result<std::vector<contracts::StockAlertDto>>::fail(
                contracts::ErrorCode::Forbidden,
                "Permiso insuficiente para consultar alertas de inventario"
            );
        }
    }

    auto prep = db_.prepare(
        "SELECT id, name, current_stock, low_stock_threshold, base_unit "
        "FROM ingredients "
        "WHERE current_stock <= low_stock_threshold "
        "ORDER BY name ASC;"
    );
    if (!prep.is_success()) {
        return contracts::Result<std::vector<contracts::StockAlertDto>>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    std::vector<contracts::StockAlertDto> alerts;
    while (true) {
        auto step = stmt.step();
        if (!step.is_success()) {
            return contracts::Result<std::vector<contracts::StockAlertDto>>::fail(step.error_code(), step.error_message());
        }
        if (!step.value()) break;

        contracts::StockAlertDto alert;
        alert.ingredient_id = stmt.get_text(0);
        alert.name = stmt.get_text(1);
        alert.current_stock_base = stmt.get_int64(2);
        alert.threshold_base = stmt.get_int64(3);
        alert.base_unit = stmt.get_text(4);
        alerts.push_back(alert);
    }

    return contracts::Result<std::vector<contracts::StockAlertDto>>::ok(alerts);
}

contracts::OperationResult SqliteInventoryService::update_low_stock_threshold(
    const std::string& ingredient_id,
    std::int64_t new_threshold_base
) {
    // Configuración de umbrales reservada para roles autorizados (inventory.manage_thresholds)
    auto perm_res = check_permission_or_fail("inventory.manage_thresholds");
    if (!perm_res.is_success()) return perm_res;

    if (new_threshold_base < 0) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "El umbral de bajo stock no puede ser negativo"
        );
    }

    auto current_ing = get_ingredient(ingredient_id);
    if (!current_ing.is_success()) {
        return contracts::OperationResult::fail(current_ing.error_code(), current_ing.error_message());
    }

    auto prep = db_.prepare("UPDATE ingredients SET low_stock_threshold = ?, updated_at = ?, updated_at_utc = ? WHERE id = ?;");
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_int64(1, new_threshold_base);
    const auto thr_ts = domain::now();
    stmt.bind_text(2, thr_ts.local);
    stmt.bind_text(3, thr_ts.utc);
    stmt.bind_text(4, ingredient_id);

    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }

    if (account_service_) {
        account_service_->record_audit_event(
            "THRESHOLD_UPDATED",
            "Umbral de bajo stock actualizado para " + current_ing.value().name +
            " (" + ingredient_id + "): " + std::to_string(new_threshold_base) + " " + current_ing.value().base_unit
        );
    }

    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteInventoryService::register_purchase_entry(
    const std::string& ingredient_id,
    std::int64_t quantity_purchased,
    const std::string& purchase_unit,
    contracts::Money total_cost
) {
    auto perm_res = check_permission_or_fail("inventory.restock");
    if (!perm_res.is_success()) return perm_res;

    if (quantity_purchased <= 0) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "La cantidad comprada debe ser estrictamente positiva"
        );
    }
    if (total_cost.cents() < 0) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "El costo total no puede ser negativo"
        );
    }

    auto current_ing = get_ingredient(ingredient_id);
    if (!current_ing.is_success()) {
        return contracts::OperationResult::fail(current_ing.error_code(), current_ing.error_message());
    }

    // Comprobar compatibilidad de la unidad comprada con la unidad base del ingrediente
    if (!domain::UnitConverter::are_compatible(purchase_unit, current_ing.value().base_unit)) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "La unidad de compra '" + purchase_unit + "' no es dimensionalmente compatible con la unidad base '" + current_ing.value().base_unit + "'"
        );
    }

    // Convertir a unidades base enteras exactas
    auto base_units_res = domain::UnitConverter::to_base_units(quantity_purchased, purchase_unit);
    if (!base_units_res.is_success()) {
        return contracts::OperationResult::fail(base_units_res.error_code(), base_units_res.error_message());
    }
    std::int64_t base_units_purchased = base_units_res.value();

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return tx;

    std::string pe_id = generate_id("pe");
    std::string sm_id = generate_id("sm");
    const auto ts = domain::now();
    const std::string& now_str = ts.local;

    // 1. Insertar entrada de compra preservando costo total y unidades base exactas
    auto prep_pe = db_.prepare(
        "INSERT INTO purchase_entries (id, ingredient_id, quantity_purchased, purchase_unit, "
        "total_cost_cents, base_units_purchased, created_at, created_at_utc) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?);"
    );
    if (!prep_pe.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(prep_pe.error_code(), prep_pe.error_message());
    }

    auto stmt_pe = std::move(prep_pe.value());
    stmt_pe.bind_text(1, pe_id);
    stmt_pe.bind_text(2, ingredient_id);
    stmt_pe.bind_int64(3, quantity_purchased);
    stmt_pe.bind_text(4, purchase_unit);
    stmt_pe.bind_int64(5, total_cost.cents());
    stmt_pe.bind_int64(6, base_units_purchased);
    stmt_pe.bind_text(7, now_str);
    stmt_pe.bind_text(8, ts.utc);
    auto step_pe = stmt_pe.step();
    if (!step_pe.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(step_pe.error_code(), step_pe.error_message());
    }

    // 2. Incrementar stock actual en base de datos
    auto prep_upd = db_.prepare("UPDATE ingredients SET current_stock = current_stock + ? WHERE id = ?;");
    if (!prep_upd.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(prep_upd.error_code(), prep_upd.error_message());
    }

    auto stmt_upd = std::move(prep_upd.value());
    stmt_upd.bind_int64(1, base_units_purchased);
    stmt_upd.bind_text(2, ingredient_id);
    auto step_upd = stmt_upd.step();
    if (!step_upd.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(step_upd.error_code(), step_upd.error_message());
    }

    // 3. Registrar movimiento de inventario (PURCHASE)
    auto prep_sm = db_.prepare(
        "INSERT INTO stock_movements (id, ingredient_id, movement_type, quantity_base_units, reason, created_at, created_at_utc) "
        "VALUES (?, ?, 'PURCHASE', ?, 'Entrada de compra', ?, ?);"
    );
    if (!prep_sm.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(prep_sm.error_code(), prep_sm.error_message());
    }

    auto stmt_sm = std::move(prep_sm.value());
    stmt_sm.bind_text(1, sm_id);
    stmt_sm.bind_text(2, ingredient_id);
    stmt_sm.bind_int64(3, base_units_purchased);
    stmt_sm.bind_text(4, now_str);
    stmt_sm.bind_text(5, ts.utc);
    auto step_sm = stmt_sm.step();
    if (!step_sm.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(step_sm.error_code(), step_sm.error_message());
    }

    auto commit_res = db_.commit_transaction();
    if (!commit_res.is_success()) {
        db_.rollback_transaction();
        return commit_res;
    }

    if (account_service_) {
        account_service_->record_audit_event(
            "PURCHASE_REGISTERED",
            "Compra registrada: " + std::to_string(quantity_purchased) + " " + purchase_unit +
            " (" + std::to_string(base_units_purchased) + " " + current_ing.value().base_unit +
            ") para " + current_ing.value().name + " por " + total_cost.to_string()
        );
    }

    return contracts::OperationResult::ok();
}

} // namespace app::services
