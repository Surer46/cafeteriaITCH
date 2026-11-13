#ifndef APP_SERVICES_STOCK_LEDGER_HPP
#define APP_SERVICES_STOCK_LEDGER_HPP

#include <cstdint>
#include <string>
#include "app/contracts/result.hpp"
#include "app/domain/stock_valuation.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

enum class StockChangeKind {
    Purchase,        // compra: sube existencias y valuación por el costo total
    Outflow,         // venta, merma o ajuste negativo: baja existencias y valuación proporcionalmente
    InflowAdjustment // ajuste positivo: entra al costo promedio vigente
};

// ÚNICA función que escribe ingredients.current_stock y su valuación. Debe llamarse dentro de la
// transacción del cambio (venta, compra, merma o ajuste) para que existencias, valuación y el
// movimiento correspondiente se confirmen o se reviertan juntos. No abre ni cierra transacciones.
// `total_cost_cents` solo se usa en Purchase.
contracts::OperationResult apply_stock_change(
    storage::SqliteDatabase& db,
    const std::string& ingredient_id,
    StockChangeKind kind,
    std::int64_t units,
    std::int64_t total_cost_cents = 0
);

// Lee existencias y valuación vigentes de un ingrediente.
contracts::Result<domain::StockState> read_stock_state(storage::SqliteDatabase& db, const std::string& ingredient_id);

} // namespace app::services

#endif // APP_SERVICES_STOCK_LEDGER_HPP
