#ifndef APP_DOMAIN_CARD_SURCHARGE_HPP
#define APP_DOMAIN_CARD_SURCHARGE_HPP

#include <cstdint>

namespace app::domain {

// Rangos válidos de la configuración (los mismos CHECK de payment_settings).
constexpr std::int64_t kMaxCardSurchargeHundredths = 2000;   // 20.00 % (centésimas de punto porcentual)
constexpr std::int64_t kMaxCardSurchargeFixedCents = 10000;  // $100.00

// Cargo extra por pago con tarjeta, solo con enteros:
//   cargo = redondeo_mitad_arriba(neto_productos * percent_hundredths / 10000) + fixed_cents
// Con 0 % y $0 el cargo es 0. Devuelve false si el resultado no cabe en int64 (entradas negativas también fallan).
inline bool card_surcharge_cents(std::int64_t net_cents, std::int64_t percent_hundredths, std::int64_t fixed_cents,
                                 std::int64_t& out) {
    if (net_cents < 0 || percent_hundredths < 0 || fixed_cents < 0) return false;
    using Int128 = __int128;
    const Int128 pct = (static_cast<Int128>(net_cents) * percent_hundredths + 5000) / 10000;
    const Int128 total = pct + fixed_cents;
    if (total > static_cast<Int128>(INT64_MAX)) return false;
    out = static_cast<std::int64_t>(total);
    return true;
}

} // namespace app::domain

#endif // APP_DOMAIN_CARD_SURCHARGE_HPP
