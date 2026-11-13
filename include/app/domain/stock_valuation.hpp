#ifndef APP_DOMAIN_STOCK_VALUATION_HPP
#define APP_DOMAIN_STOCK_VALUATION_HPP

#include <cstdint>
#include "app/contracts/result.hpp"

namespace app::domain {

// Un micro-centavo es 1/1 000 000 de centavo. Todas las valuaciones y costos intermedios
// son enteros en esta escala fija; nunca se guarda un costo unitario ya redondeado a centavos.
constexpr std::int64_t kMicroPerCent = 1000000;

// Estado de valuación de un ingrediente (promedio móvil sobre lo que queda en existencia).
//   stock:            existencias en unidades base.
//   value_micro:      valor de esas existencias, en micro-centavos (>= 0; exactamente 0 si stock == 0).
//   last_avg_*:       último promedio conocido como fracción (value / units). last_avg_units == 0
//                     significa que el ingrediente nunca tuvo un costo conocido.
struct StockState {
    std::int64_t stock{0};
    std::int64_t value_micro{0};
    std::int64_t last_avg_value_micro{0};
    std::int64_t last_avg_units{0};

    bool has_cost_basis() const { return last_avg_units > 0; }
};

// Compra de `units` (> 0) por `total_cost_cents` (>= 0). La valuación sube por el costo total.
// Si el ingrediente no tenía costo conocido, sus existencias previas sin costo se valoran al
// precio de esta primera compra.
contracts::Result<StockState> valuation_after_purchase(const StockState& s, std::int64_t units, std::int64_t total_cost_cents);

// Salida de `units` (> 0): venta, merma o ajuste negativo. La valuación baja de forma
// proporcional; al salir todo queda exactamente en 0.
contracts::Result<StockState> valuation_after_outflow(const StockState& s, std::int64_t units);

// Ajuste positivo de `units` (> 0): entra al costo promedio vigente (el último conocido si la
// existencia es 0). Sin costo conocido entra sin valuación.
contracts::Result<StockState> valuation_after_inflow_adjustment(const StockState& s, std::int64_t units);

// Costo en micro-centavos de `quantity` unidades al promedio vigente. NotFound si el costo es
// desconocido (nunca devuelve 0 en su lugar). `from_last_known` indica que se usó el último
// promedio conocido porque la existencia es 0.
struct UnitCost {
    std::int64_t micro{0};
    bool from_last_known{false};
};
contracts::Result<UnitCost> cost_micro_for_quantity(const StockState& s, std::int64_t quantity);

// Redondeo comercial (mitad hacia arriba) de micro-centavos a centavos. `micro` >= 0.
std::int64_t micro_to_cents(std::int64_t micro);

// Margen en puntos base (1 % = 100) = margen * 10000 / precio, redondeado (mitad lejos de cero).
// Falla si el precio no es positivo.
contracts::Result<std::int64_t> margin_basis_points(std::int64_t margin_cents, std::int64_t price_cents);

} // namespace app::domain

#endif // APP_DOMAIN_STOCK_VALUATION_HPP
