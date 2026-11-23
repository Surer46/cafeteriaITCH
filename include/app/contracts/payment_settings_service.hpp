#ifndef APP_CONTRACTS_PAYMENT_SETTINGS_SERVICE_HPP
#define APP_CONTRACTS_PAYMENT_SETTINGS_SERVICE_HPP

#include <cstdint>
#include <string>
#include "app/contracts/result.hpp"
#include "app/contracts/money.hpp"

namespace app::contracts {

// Cargo extra por pago con tarjeta (B18). Se cobra SOLO cuando el método es Tarjeta (no Transferencia ni Efectivo):
//   cargo = redondeo_mitad_arriba(neto de productos * percent_hundredths / 10000) + fixed
// Los precios ya incluyen impuestos; el cargo no se desglosa ni se devuelve en los reembolsos.
// Por omisión 0 % y $0 (desactivado) hasta que el Dueño lo configure. Aplica a toda la cafetería.
struct CardSurchargeConfigDto {
    // Porcentaje en centésimas de punto porcentual: 150 = 1.50 %. Rango 0 a 2000 (0 % a 20.00 %).
    std::int64_t percent_hundredths{0};
    // Monto fijo por venta con tarjeta, en centavos. Rango 0 a 10 000 ($0 a $100).
    Money fixed{Money::zero()};
    std::string updated_at;   // hora local de la última configuración; vacío si nunca se configuró
};

class IPaymentSettingsService {
public:
    virtual ~IPaymentSettingsService() = default;

    // Lo puede leer cualquier sesión con pos.sell o config.manage (la caja necesita mostrar el total con cargo).
    virtual Result<CardSurchargeConfigDto> get_card_surcharge_config() = 0;
    // Requiere config.manage (solo el Dueño por omisión). Valida rangos, audita CARD_SURCHARGE_CONFIGURED con
    // los valores anterior y nuevo.
    virtual OperationResult set_card_surcharge_config(std::int64_t percent_hundredths, Money fixed) = 0;
    // T17: el cargo que resultaría de una venta de `sale_amount` con esa configuración, sin guardarla (el ejemplo en
    // vivo de Configuración). Mismo redondeo y mismos rangos que el cobro. Requiere config.manage.
    virtual Result<Money> preview_card_surcharge(std::int64_t percent_hundredths, Money fixed, Money sale_amount) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_PAYMENT_SETTINGS_SERVICE_HPP
