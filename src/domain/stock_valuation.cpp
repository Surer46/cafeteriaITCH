#include "app/domain/stock_valuation.hpp"
#include <limits>

namespace app::domain {

namespace {

using contracts::ErrorCode;
using Int128 = __int128;
constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();

template <typename T>
contracts::Result<T> fail(ErrorCode code, const char* msg) {
    return contracts::Result<T>::fail(code, msg);
}

// (num + den/2) / den con num >= 0, den > 0.
Int128 div_round(Int128 num, Int128 den) {
    return (num + den / 2) / den;
}

bool fits(Int128 v) {
    return v >= 0 && v <= static_cast<Int128>(kMax);
}

} // namespace

contracts::Result<StockState> valuation_after_purchase(const StockState& s, std::int64_t units, std::int64_t total_cost_cents) {
    if (units <= 0) return fail<StockState>(ErrorCode::InvalidInput, "La cantidad comprada debe ser positiva");
    if (total_cost_cents < 0) return fail<StockState>(ErrorCode::InvalidInput, "El costo total no puede ser negativo");
    if (s.stock < 0 || s.value_micro < 0) return fail<StockState>(ErrorCode::InvalidState, "Valuación previa inválida");

    const Int128 new_stock = static_cast<Int128>(s.stock) + units;
    if (!fits(new_stock)) return fail<StockState>(ErrorCode::InvalidInput, "Desbordamiento numérico en las existencias");

    const Int128 cost_micro = static_cast<Int128>(total_cost_cents) * kMicroPerCent;
    Int128 new_value = 0;
    if (!s.has_cost_basis()) {
        // Primera compra con costo: las existencias previas sin costo se valoran a este precio.
        new_value = div_round(cost_micro * new_stock, units);
    } else {
        new_value = static_cast<Int128>(s.value_micro) + cost_micro;
    }
    if (!fits(new_value)) return fail<StockState>(ErrorCode::InvalidInput, "Desbordamiento numérico en la valuación de existencias");

    StockState out;
    out.stock = static_cast<std::int64_t>(new_stock);
    out.value_micro = static_cast<std::int64_t>(new_value);
    out.last_avg_value_micro = out.value_micro;
    out.last_avg_units = out.stock;
    return contracts::Result<StockState>::ok(out);
}

contracts::Result<StockState> valuation_after_outflow(const StockState& s, std::int64_t units) {
    if (units <= 0) return fail<StockState>(ErrorCode::InvalidInput, "La cantidad debe ser positiva");
    if (s.stock < 0 || s.value_micro < 0) return fail<StockState>(ErrorCode::InvalidState, "Valuación previa inválida");
    if (units > s.stock) return fail<StockState>(ErrorCode::InsufficientStock, "Existencias insuficientes");

    StockState out = s;
    out.stock = s.stock - units;
    if (out.stock == 0) {
        // Al salir todo, la valuación queda exactamente en 0 (sin residuos de redondeo).
        out.value_micro = 0;
        return contracts::Result<StockState>::ok(out);
    }
    Int128 leaving = div_round(static_cast<Int128>(s.value_micro) * units, s.stock);
    if (leaving > s.value_micro) leaving = s.value_micro;
    out.value_micro = static_cast<std::int64_t>(static_cast<Int128>(s.value_micro) - leaving);
    if (s.has_cost_basis()) {
        out.last_avg_value_micro = out.value_micro;
        out.last_avg_units = out.stock;
    }
    return contracts::Result<StockState>::ok(out);
}

contracts::Result<StockState> valuation_after_inflow_adjustment(const StockState& s, std::int64_t units) {
    if (units <= 0) return fail<StockState>(ErrorCode::InvalidInput, "La cantidad debe ser positiva");
    if (s.stock < 0 || s.value_micro < 0) return fail<StockState>(ErrorCode::InvalidState, "Valuación previa inválida");

    const Int128 new_stock = static_cast<Int128>(s.stock) + units;
    if (!fits(new_stock)) return fail<StockState>(ErrorCode::InvalidInput, "Desbordamiento numérico en las existencias");

    StockState out = s;
    out.stock = static_cast<std::int64_t>(new_stock);
    if (!s.has_cost_basis()) {
        // Sin costo conocido: entra sin valuación y el costo sigue desconocido.
        return contracts::Result<StockState>::ok(out);
    }
    Int128 avg_value = s.stock > 0 ? s.value_micro : s.last_avg_value_micro;
    Int128 avg_units = s.stock > 0 ? s.stock : s.last_avg_units;
    const Int128 entering = div_round(avg_value * units, avg_units);
    const Int128 new_value = static_cast<Int128>(s.value_micro) + entering;
    if (!fits(new_value)) return fail<StockState>(ErrorCode::InvalidInput, "Desbordamiento numérico en la valuación de existencias");
    out.value_micro = static_cast<std::int64_t>(new_value);
    out.last_avg_value_micro = out.value_micro;
    out.last_avg_units = out.stock;
    return contracts::Result<StockState>::ok(out);
}

contracts::Result<UnitCost> cost_micro_for_quantity(const StockState& s, std::int64_t quantity) {
    if (quantity < 0) return fail<UnitCost>(ErrorCode::InvalidInput, "La cantidad no puede ser negativa");
    if (!s.has_cost_basis()) return fail<UnitCost>(ErrorCode::NotFound, "Costo desconocido: el ingrediente no tiene compras");
    UnitCost uc;
    Int128 value = 0;
    Int128 units = 0;
    if (s.stock > 0) {
        value = s.value_micro;
        units = s.stock;
    } else {
        value = s.last_avg_value_micro;
        units = s.last_avg_units;
        uc.from_last_known = true;
    }
    const Int128 micro = div_round(value * quantity, units);
    if (!fits(micro)) return fail<UnitCost>(ErrorCode::InvalidInput, "Desbordamiento al calcular el costo");
    uc.micro = static_cast<std::int64_t>(micro);
    return contracts::Result<UnitCost>::ok(uc);
}

std::int64_t micro_to_cents(std::int64_t micro) {
    if (micro <= 0) return 0;
    return static_cast<std::int64_t>((static_cast<Int128>(micro) + kMicroPerCent / 2) / kMicroPerCent);
}

contracts::Result<std::int64_t> margin_basis_points(std::int64_t margin_cents, std::int64_t price_cents) {
    if (price_cents <= 0) return fail<std::int64_t>(ErrorCode::InvalidInput, "El precio debe ser positivo para calcular el porcentaje");
    const Int128 num = static_cast<Int128>(margin_cents) * 10000;
    const Int128 half = price_cents / 2;
    const Int128 bp = (num >= 0 ? (num + half) : (num - half)) / price_cents;
    if (bp > kMax || bp < -static_cast<Int128>(kMax)) return fail<std::int64_t>(ErrorCode::InvalidInput, "Desbordamiento");
    return contracts::Result<std::int64_t>::ok(static_cast<std::int64_t>(bp));
}

} // namespace app::domain
