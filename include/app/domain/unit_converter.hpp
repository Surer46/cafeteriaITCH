#ifndef APP_DOMAIN_UNIT_CONVERTER_HPP
#define APP_DOMAIN_UNIT_CONVERTER_HPP

#include <string>
#include <cstdint>
#include <limits>
#include "app/contracts/result.hpp"
#include "app/contracts/money.hpp"

namespace app::domain {

enum class UnitDimension {
    Mass,
    Volume,
    Count
};

struct RationalCost {
    contracts::Money total_cost{contracts::Money::zero()};
    std::int64_t total_base_units{1};

    contracts::Result<contracts::Money> cost_for_quantity(std::int64_t quantity_base_units) const;
};

class UnitConverter {
public:
    static contracts::Result<UnitDimension> parse_dimension(const std::string& dim_str);
    static const char* to_string(UnitDimension dim);

    // Convierte una cantidad y unidad a su unidad base entera canónica:
    // Masa base: miligramo (mg)  [1 g = 1,000 mg; 1 kg = 1,000,000 mg]
    // Volumen base: mililitro (ml) [1 L = 1,000 ml]
    // Conteo base: pieza (pz) [1 pz = 1]
    static contracts::Result<std::int64_t> to_base_units(
        std::int64_t quantity,
        const std::string& unit
    );

    // Calcula costo proporcional exacto sin pérdida de precisión a 0 centavos.
    // Ejemplo: 1 kg de limón (1,000,000 mg) por $50 (5,000 centavos)
    // Para 1 g (1,000 mg) -> retorna exactamente $0.05 (5 centavos).
    static contracts::Result<contracts::Money> calculate_proportional_cost(
        contracts::Money total_cost,
        std::int64_t total_base_units,
        std::int64_t target_base_units
    );

    // Construye la representación de costo racional preservando numerador y denominador
    static contracts::Result<RationalCost> make_rational_cost(
        contracts::Money total_cost,
        std::int64_t total_base_units
    );

    // Comprueba compatibilidad dimensional estricta
    static bool are_compatible(const std::string& unit_a, const std::string& unit_b);
};

} // namespace app::domain

#endif // APP_DOMAIN_UNIT_CONVERTER_HPP
