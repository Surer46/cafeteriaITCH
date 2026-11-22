#ifndef APP_SERVICES_SHIFT_TOTALS_HPP
#define APP_SERVICES_SHIFT_TOTALS_HPP

#include <cstdint>
#include <string>
#include "app/storage/sqlite_database.hpp"

namespace app::services {

// Importes en vivo de un turno, en centavos enteros (B15, ampliado en B18). Los usan el corte de turno y el servicio de
// reembolsos, para que el «efectivo esperado» sea el mismo en los dos.
struct ShiftCashTotals {
    std::int64_t sales_count{0};
    std::int64_t net{0};             // neto de productos (sales.total_cents)
    std::int64_t discount{0};
    std::int64_t gross{0};           // net + discount
    std::int64_t cash_sales{0};      // pagos en efectivo
    std::int64_t card_sales{0};      // pagos con tarjeta (incluyen el cargo por tarjeta)
    std::int64_t transfer_sales{0};
    std::int64_t card_surcharges{0}; // cargos por tarjeta cobrados (parte de card_sales)
    std::int64_t inflows{0};
    std::int64_t outflows{0};
    std::int64_t cash_refunds{0};    // reembolsos hechos en este turno, por método
    std::int64_t card_refunds{0};
    std::int64_t transfer_refunds{0};
};

// Devuelve "" si salió bien o el mensaje de error.
std::string compute_shift_totals(storage::SqliteDatabase& db, const std::string& shift_id, ShiftCashTotals& out);

// efectivo esperado = fondo + ventas en efectivo + entradas - salidas - reembolsos en efectivo. false si desborda.
bool expected_cash_cents(std::int64_t initial_cash_cents, const ShiftCashTotals& t, std::int64_t& out);

} // namespace app::services

#endif // APP_SERVICES_SHIFT_TOTALS_HPP
