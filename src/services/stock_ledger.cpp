#include "app/services/stock_ledger.hpp"

namespace app::services {

contracts::Result<domain::StockState> read_stock_state(storage::SqliteDatabase& db, const std::string& ingredient_id) {
    auto prep = db.prepare(
        "SELECT current_stock, stock_value_micro, last_avg_value_micro, last_avg_units FROM ingredients WHERE id = ?;"
    );
    if (!prep.is_success()) {
        return contracts::Result<domain::StockState>::fail(prep.error_code(), prep.error_message());
    }
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, ingredient_id);
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::Result<domain::StockState>::fail(step.error_code(), step.error_message());
    }
    if (!step.value()) {
        return contracts::Result<domain::StockState>::fail(contracts::ErrorCode::NotFound, "Ingrediente no encontrado");
    }
    domain::StockState s;
    s.stock = stmt.get_int64(0);
    s.value_micro = stmt.get_int64(1);
    s.last_avg_value_micro = stmt.get_int64(2);
    s.last_avg_units = stmt.get_int64(3);
    stmt.reset();
    return contracts::Result<domain::StockState>::ok(s);
}

contracts::OperationResult apply_stock_change(
    storage::SqliteDatabase& db,
    const std::string& ingredient_id,
    StockChangeKind kind,
    std::int64_t units,
    std::int64_t total_cost_cents
) {
    auto current = read_stock_state(db, ingredient_id);
    if (!current.is_success()) {
        return contracts::OperationResult::fail(current.error_code(), current.error_message());
    }

    contracts::Result<domain::StockState> next = contracts::Result<domain::StockState>::fail(
        contracts::ErrorCode::InvalidInput, "Tipo de cambio de existencias desconocido");
    switch (kind) {
        case StockChangeKind::Purchase:
            next = domain::valuation_after_purchase(current.value(), units, total_cost_cents);
            break;
        case StockChangeKind::Outflow:
            next = domain::valuation_after_outflow(current.value(), units);
            break;
        case StockChangeKind::InflowAdjustment:
            next = domain::valuation_after_inflow_adjustment(current.value(), units);
            break;
    }
    if (!next.is_success()) {
        return contracts::OperationResult::fail(next.error_code(), next.error_message());
    }

    auto prep = db.prepare(
        "UPDATE ingredients SET current_stock = ?, stock_value_micro = ?, last_avg_value_micro = ?, last_avg_units = ? WHERE id = ?;"
    );
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }
    auto stmt = std::move(prep.value());
    stmt.bind_int64(1, next.value().stock);
    stmt.bind_int64(2, next.value().value_micro);
    stmt.bind_int64(3, next.value().last_avg_value_micro);
    stmt.bind_int64(4, next.value().last_avg_units);
    stmt.bind_text(5, ingredient_id);
    auto step = stmt.step();
    stmt.reset();
    if (!step.is_success()) {
        return contracts::OperationResult::fail(step.error_code(), step.error_message());
    }
    return contracts::OperationResult::ok();
}

} // namespace app::services
