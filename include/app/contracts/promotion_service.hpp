#ifndef APP_CONTRACTS_PROMOTION_SERVICE_HPP
#define APP_CONTRACTS_PROMOTION_SERVICE_HPP

#include <cstdint>
#include <string>
#include <vector>
#include "app/contracts/money.hpp"
#include "app/contracts/result.hpp"

// Promociones y combos (B14). Todo lo de este servicio exige el permiso inventory.promotions (por omisión solo el
// Dueño; asignable a un rol propio). El Cajero no entra: ve el descuento ya aplicado en el carrito, en la venta y en el
// ticket. Costo y margen de un combo exigen además inventory.manage_products, como get_recipe_costing.
//
// Reglas de descuento (las aplica el servicio de carrito/venta; aquí solo se administran):
//  - A cada línea del carrito se le aplica UNA sola promoción: entre las activas y vigentes en ese momento que
//    alcanzan al producto (por producto o por categoría), la de mayor prioridad. Nunca se suman.
//  - Porcentaje: sobre el subtotal de la línea, redondeado al centavo (la mitad se redondea hacia arriba) una sola vez.
//  - Monto fijo: ese monto menos por CADA pieza, sin pasar del precio de la pieza.
//  - NxM (lleva N, paga M): se cuentan juntas las piezas del mismo producto de TODAS las líneas (las notas no
//    cuentan); gratis = (piezas / N) * (N - M); lo que sobra de un múltiplo exacto se paga completo; el descuento
//    se reparte en orden de líneas, empezando por la primera.
//  - El precio de una línea nunca baja de cero.
//  - Todo en centavos enteros, sin decimales binarios.
namespace app::contracts {

struct PromotionDto {
    std::string id;
    std::string name;
    // PERCENT (porcentaje), FIXED (monto fijo por pieza), NXM (lleva N, paga M). Código para la forma de edición;
    // para MOSTRAR usa summary_text.
    std::string kind;
    std::int64_t percent_basis_points{0};   // PERCENT: 1 % = 100 (de 1 a 10000)
    Money fixed_off{Money::zero()};         // FIXED: monto menos por pieza
    std::int64_t buy_qty{0};                // NXM: lleva N
    std::int64_t pay_qty{0};                // NXM: paga M (M < N)
    std::string scope_kind;                 // PRODUCT (productos concretos) o CATEGORY (una categoría completa)
    std::vector<std::string> product_ids;   // alcance PRODUCT
    std::vector<std::string> product_names; // mismos productos, para mostrar (mismo orden)
    std::vector<std::string> categories;    // alcance CATEGORY (una sola categoría)
    int priority{0};                        // la fija quien crea la promoción; mayor gana
    bool is_active{true};
    std::string valid_from;                 // AAAA-MM-DD hora local; vacío = sin límite
    std::string valid_to;                   // AAAA-MM-DD hora local, inclusive; vacío = sin límite
    std::vector<int> weekdays;              // 1 = lunes ... 7 = domingo; vacío = todos los días
    std::string time_from;                  // HH:MM; vacío = sin límite (con time_to: horario completo)
    std::string time_to;                    // HH:MM, exclusivo; time_from < time_to
    // Lenguaje llano, sin códigos, listo para mostrar:
    std::string summary_text;               // «10 % de descuento», «Lleva 2 y paga 1», «$5.00 menos en cada pieza»
    std::string validity_text;              // «Lunes a viernes, de 07:00 a 11:00», «Del 01/12/2026 al 31/12/2026», «Siempre»
    bool is_valid_now{false};               // activa y dentro de su vigencia en este momento (reloj local)
};

struct CreatePromotionDto {
    std::string name;
    std::string kind;                       // PERCENT, FIXED o NXM
    std::int64_t percent_basis_points{0};
    Money fixed_off{Money::zero()};
    std::int64_t buy_qty{0};
    std::int64_t pay_qty{0};
    std::string scope_kind;                 // PRODUCT o CATEGORY
    std::vector<std::string> product_ids;   // alcance PRODUCT: al menos uno
    std::string category;                   // alcance CATEGORY
    int priority{0};
    bool is_active{true};
    std::string valid_from;
    std::string valid_to;
    std::vector<int> weekdays;
    std::string time_from;
    std::string time_to;
};

struct UpdatePromotionDto {
    std::string promotion_id;
    CreatePromotionDto data;                // reemplaza todos los campos de la promoción
};

struct PromotionFilterDto {
    bool only_active{false};
    std::string product_id;                 // vacío = todas; si no, las que alcanzan a ese producto
};

struct ComboComponentDto {
    std::string product_id;
    std::string product_name;               // solo al leer
    std::int64_t quantity{1};               // de 1 a 99
};

struct ComboDto {
    std::string product_id;                 // un combo ES un producto: este es su id de producto
    std::string sku;
    std::string name;
    std::string category;
    Money price{Money::zero()};             // precio fijo propio
    bool is_active{true};
    std::vector<ComboComponentDto> components;
    // Cuántos combos se pueden preparar con las existencias actuales: mínimo entre las partes que controlan existencia
    // (0 si alguna de ellas no tiene, o si ninguna la controla). Una parte sin existencia controlada no limita.
    std::int64_t stock_units{0};
};

struct CreateComboDto {
    std::string sku;
    std::string barcode;
    std::string name;
    std::string category;
    Money price{Money::zero()};
    std::vector<ComboComponentDto> components;  // product_id y quantity; al menos uno
};

struct UpdateComboDto {
    std::string product_id;
    std::string name;
    std::string category;
    Money price{Money::zero()};
    std::vector<ComboComponentDto> components;  // reemplaza la lista completa
};

// Costo estimado y margen de un combo: suma del costo de cada parte (por su receta, con el promedio móvil de cada
// ingrediente) por su cantidad. Mismas reglas que RecipeCostingDto: un costo desconocido no se cuenta como cero.
struct ComboCostLineDto {
    std::string product_id;
    std::string product_name;
    std::int64_t quantity{1};
    bool cost_known{false};
    Money cost{Money::zero()};              // costo de todas las piezas de esta parte; sin significado si !cost_known
};

struct ComboCostingDto {
    std::string product_id;
    Money price{Money::zero()};
    std::vector<ComboCostLineDto> lines;
    bool all_costs_known{false};
    std::vector<std::string> unknown_component_names;
    Money known_cost_subtotal{Money::zero()};
    Money cost_total{Money::zero()};        // válido solo si all_costs_known
    bool margin_known{false};
    Money margin{Money::zero()};            // precio - costo; puede ser negativo
    bool margin_percent_known{false};
    std::int64_t margin_basis_points{0};    // margen * 10000 / precio
};

class IPromotionService {
public:
    virtual ~IPromotionService() = default;

    // Promociones, de mayor a menor prioridad. Uso: auto l = svc->list_promotions({}).value();
    virtual Result<std::vector<PromotionDto>> list_promotions(const PromotionFilterDto& filter) = 0;
    virtual Result<PromotionDto> get_promotion(const std::string& promotion_id) = 0;

    // Crea o edita. Rechaza con InvalidInput los datos inválidos y con Conflict (mensaje llano que nombra la otra
    // promoción) una promoción activa de la MISMA prioridad que se cruza con otra activa sobre algún producto en una
    // vigencia que se solapa. Deja un evento de bitácora con quién y cuándo.
    virtual Result<PromotionDto> create_promotion(const CreatePromotionDto& input) = 0;
    virtual Result<PromotionDto> update_promotion(const UpdatePromotionDto& input) = 0;
    // Activa o desactiva. Activar vuelve a comprobar el choque de prioridades. Uso: svc->set_promotion_active(id, false);
    virtual OperationResult set_promotion_active(const std::string& promotion_id, bool active) = 0;

    // Combos. Un combo es un producto con precio fijo propio, hecho de otros productos con su cantidad. Al venderlo
    // descuenta del inventario lo de cada parte; si falta cualquier parte la venta se bloquea. No puede contenerse a
    // sí mismo ni a otro combo. Para darlo de baja usa deactivate_product del catálogo.
    virtual Result<std::vector<ComboDto>> list_combos(bool include_inactive) = 0;
    virtual Result<ComboDto> get_combo(const std::string& product_id) = 0;
    virtual Result<ComboDto> create_combo(const CreateComboDto& input) = 0;
    virtual Result<ComboDto> update_combo(const UpdateComboDto& input) = 0;
    // Requiere además inventory.manage_products.
    virtual Result<ComboCostingDto> get_combo_costing(const std::string& product_id) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_PROMOTION_SERVICE_HPP
