#include "app/domain/unit_converter.hpp"
#include <algorithm>
#include <cctype>

namespace app::domain {

namespace {

std::string to_lower(const std::string& str) {
    std::string out = str;
    std::transform(out.begin(), out.end(), out.begin(),
        [](unsigned char c){ return std::tolower(c); });
    return out;
}

std::optional<UnitDimension> get_unit_dimension(const std::string& unit) {
    std::string u = to_lower(unit);
    if (u == "mg" || u == "g" || u == "gr" || u == "kg") {
        return UnitDimension::Mass;
    }
    if (u == "ml" || u == "l" || u == "lt" || u == "litro" || u == "litros") {
        return UnitDimension::Volume;
    }
    if (u == "pz" || u == "pieza" || u == "piezas" || u == "u" || u == "unidades") {
        return UnitDimension::Count;
    }
    return std::nullopt;
}

} // namespace

contracts::Result<contracts::Money> RationalCost::cost_for_quantity(std::int64_t quantity_base_units) const {
    return UnitConverter::calculate_proportional_cost(total_cost, total_base_units, quantity_base_units);
}

contracts::Result<UnitDimension> UnitConverter::parse_dimension(const std::string& dim_str) {
    std::string s = to_lower(dim_str);
    if (s == "mass" || s == "masa") return contracts::Result<UnitDimension>::ok(UnitDimension::Mass);
    if (s == "volume" || s == "volumen") return contracts::Result<UnitDimension>::ok(UnitDimension::Volume);
    if (s == "count" || s == "pieza" || s == "piezas") return contracts::Result<UnitDimension>::ok(UnitDimension::Count);
    return contracts::Result<UnitDimension>::fail(contracts::ErrorCode::InvalidInput, "Dimensión desconocida: " + dim_str);
}

const char* UnitConverter::to_string(UnitDimension dim) {
    switch (dim) {
        case UnitDimension::Mass: return "MASS";
        case UnitDimension::Volume: return "VOLUME";
        case UnitDimension::Count: return "COUNT";
        default: return "UNKNOWN";
    }
}

contracts::Result<std::int64_t> UnitConverter::to_base_units(
    std::int64_t quantity,
    const std::string& unit
) {
    if (quantity < 0) {
        return contracts::Result<std::int64_t>::fail(
            contracts::ErrorCode::InvalidInput,
            "La cantidad no puede ser negativa"
        );
    }

    std::string u = to_lower(unit);

    // Verificación de desbordamiento en multiplicaciones
    constexpr std::int64_t max_val = std::numeric_limits<std::int64_t>::max();

    // Masa: Unidad base es miligramo (mg)
    if (u == "mg") {
        return contracts::Result<std::int64_t>::ok(quantity);
    }
    if (u == "g" || u == "gr") {
        if (quantity > max_val / 1000) {
            return contracts::Result<std::int64_t>::fail(contracts::ErrorCode::InvalidInput, "Desbordamiento aritmético en conversión de gramos");
        }
        return contracts::Result<std::int64_t>::ok(quantity * 1000);
    }
    if (u == "kg") {
        if (quantity > max_val / 1000000) {
            return contracts::Result<std::int64_t>::fail(contracts::ErrorCode::InvalidInput, "Desbordamiento aritmético en conversión de kilogramos");
        }
        return contracts::Result<std::int64_t>::ok(quantity * 1000000);
    }

    // Volumen: Unidad base es mililitro (ml)
    if (u == "ml") {
        return contracts::Result<std::int64_t>::ok(quantity);
    }
    if (u == "l" || u == "lt" || u == "litro" || u == "litros") {
        if (quantity > max_val / 1000) {
            return contracts::Result<std::int64_t>::fail(contracts::ErrorCode::InvalidInput, "Desbordamiento aritmético en conversión de litros");
        }
        return contracts::Result<std::int64_t>::ok(quantity * 1000);
    }

    // Conteo: Unidad base es pieza (pz)
    if (u == "pz" || u == "pieza" || u == "piezas" || u == "u" || u == "unidades") {
        return contracts::Result<std::int64_t>::ok(quantity);
    }

    return contracts::Result<std::int64_t>::fail(
        contracts::ErrorCode::InvalidInput,
        "Unidad de medida no compatible: " + unit
    );
}

contracts::Result<contracts::Money> UnitConverter::calculate_proportional_cost(
    contracts::Money total_cost,
    std::int64_t total_base_units,
    std::int64_t target_base_units
) {
    if (total_base_units <= 0) {
        return contracts::Result<contracts::Money>::fail(
            contracts::ErrorCode::InvalidInput,
            "Las unidades base totales de compra deben ser mayores a cero"
        );
    }
    if (target_base_units < 0) {
        return contracts::Result<contracts::Money>::fail(
            contracts::ErrorCode::InvalidInput,
            "La cantidad objetivo no puede ser negativa"
        );
    }
    if (total_cost.cents() < 0) {
        return contracts::Result<contracts::Money>::fail(
            contracts::ErrorCode::InvalidInput,
            "El costo total no puede ser negativo"
        );
    }

    if (target_base_units == 0 || total_cost.cents() == 0) {
        return contracts::Result<contracts::Money>::ok(contracts::Money::zero());
    }

    // Usar aritmética de 128 bits para el producto intermedio y evitar desbordamiento:
    // (target_base_units * total_cost_cents + (total_base_units / 2)) / total_base_units
    // El redondeo se aplica al obtener el importe final en centavos enteros sin usar punto flotante.
#if defined(__SIZEOF_INT128__)
    __int128 num = static_cast<__int128>(target_base_units) * static_cast<__int128>(total_cost.cents());
    // Redondeo comercial al centavo más cercano: sumar la mitad del divisor antes de dividir
    __int128 rounded_cents = (num + (total_base_units / 2)) / total_base_units;
    if (rounded_cents > std::numeric_limits<std::int64_t>::max()) {
        return contracts::Result<contracts::Money>::fail(
            contracts::ErrorCode::InvalidInput,
            "Desbordamiento al calcular el importe monetario"
        );
    }
    return contracts::Result<contracts::Money>::ok(contracts::Money::from_cents(static_cast<std::int64_t>(rounded_cents)));
#else
    // Multiplicación y división entera de 128 bits portable sin usar punto flotante
    std::uint64_t u = static_cast<std::uint64_t>(target_base_units);
    std::uint64_t v = static_cast<std::uint64_t>(total_cost.cents());
    std::uint64_t den = static_cast<std::uint64_t>(total_base_units);

    std::uint64_t u1 = u >> 32, u0 = u & 0xFFFFFFFFULL;
    std::uint64_t v1 = v >> 32, v0 = v & 0xFFFFFFFFULL;

    std::uint64_t t = u0 * v0;
    std::uint64_t w0 = t & 0xFFFFFFFFULL;
    std::uint64_t k = t >> 32;

    t = u1 * v0 + k;
    std::uint64_t w1 = t & 0xFFFFFFFFULL;
    std::uint64_t w2 = t >> 32;

    t = u0 * v1 + w1;
    k = t >> 32;

    std::uint64_t prod_hi = u1 * v1 + w2 + k;
    std::uint64_t prod_lo = (t << 32) + w0;

    // Sumar mitad del divisor para redondeo comercial
    std::uint64_t half = den / 2;
    prod_lo += half;
    if (prod_lo < half) {
        prod_hi += 1;
    }

    if (prod_hi >= den) {
        return contracts::Result<contracts::Money>::fail(
            contracts::ErrorCode::InvalidInput,
            "Desbordamiento al calcular el importe monetario"
        );
    }

    std::uint64_t quot = 0;
    std::uint64_t rem = 0;
    for (int i = 127; i >= 0; --i) {
        std::uint64_t bit = (i >= 64 ? (prod_hi >> (i - 64)) : (prod_lo >> i)) & 1ULL;
        rem = (rem << 1) | bit;
        if (rem >= den) {
            rem -= den;
            if (i < 64) {
                quot |= (1ULL << i);
            }
        }
    }

    if (quot > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return contracts::Result<contracts::Money>::fail(
            contracts::ErrorCode::InvalidInput,
            "Desbordamiento al calcular el importe monetario"
        );
    }

    return contracts::Result<contracts::Money>::ok(contracts::Money::from_cents(static_cast<std::int64_t>(quot)));
#endif
}

contracts::Result<RationalCost> UnitConverter::make_rational_cost(
    contracts::Money total_cost,
    std::int64_t total_base_units
) {
    if (total_base_units <= 0) {
        return contracts::Result<RationalCost>::fail(
            contracts::ErrorCode::InvalidInput,
            "Las unidades base deben ser mayores a cero"
        );
    }
    return contracts::Result<RationalCost>::ok(RationalCost{total_cost, total_base_units});
}

bool UnitConverter::are_compatible(const std::string& unit_a, const std::string& unit_b) {
    auto dim_a = get_unit_dimension(unit_a);
    auto dim_b = get_unit_dimension(unit_b);
    if (!dim_a.has_value() || !dim_b.has_value()) {
        return false;
    }
    return dim_a.value() == dim_b.value();
}

} // namespace app::domain
