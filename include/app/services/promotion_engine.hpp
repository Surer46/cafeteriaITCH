#ifndef APP_SERVICES_PROMOTION_ENGINE_HPP
#define APP_SERVICES_PROMOTION_ENGINE_HPP

#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "app/storage/sqlite_database.hpp"

// Motor de descuentos de B14: el mismo cálculo para la vista previa del carrito y para el cobro. Todo en centavos
// enteros (sin double) y con la hora local inyectable para las pruebas.
namespace app::services {

// Hora local en la que se evalúa la vigencia. weekday: 1 = lunes ... 7 = domingo. minutes: minutos desde medianoche.
struct LocalNow {
    std::string date;   // AAAA-MM-DD
    int weekday{1};
    int minutes{0};
};
using LocalClock = std::function<LocalNow()>;

LocalNow system_local_now();

// Entrada y salida por línea de carrito.
struct PromoLineIn {
    std::string product_id;
    std::int64_t quantity{0};
    std::int64_t unit_price_cents{0};
};

struct PromoLineOut {
    std::int64_t discount_cents{0};
    std::string promotion_id;       // vacío si ninguna
    std::string promotion_name;
    std::string kind;               // PERCENT, FIXED, NXM
    std::string params;             // instantánea: "bp=1000", "off=500" o "n=2;m=1"
};

// Calcula el descuento de cada línea. Falla (false) solo por error de base o desbordamiento; `error` lleva el motivo.
bool compute_line_discounts(storage::SqliteDatabase& db,
                            const LocalNow& now,
                            const std::vector<PromoLineIn>& lines,
                            std::vector<PromoLineOut>& out,
                            std::string& error);

// Funciones puras (probadas por separado).
// Porcentaje: floor((subtotal * bp + 5000) / 10000) sin desbordar. bp en 0..10000.
std::int64_t percent_discount_cents(std::int64_t subtotal_cents, std::int64_t basis_points);
// NxM: piezas gratis = (cantidad / N) * (N - M).
std::int64_t nxm_free_units(std::int64_t quantity, std::int64_t buy_qty, std::int64_t pay_qty);

// Vigencia de una promoción en un instante (los campos vacíos no limitan). weekdays_mask: bit 0 = lunes.
bool promotion_in_validity(const std::string& valid_from, const std::string& valid_to,
                           int weekdays_mask, int time_from_min, int time_to_min,
                           const LocalNow& now);

} // namespace app::services

#endif // APP_SERVICES_PROMOTION_ENGINE_HPP
