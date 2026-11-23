#ifndef APP_CONTRACTS_REFUND_SERVICE_HPP
#define APP_CONTRACTS_REFUND_SERVICE_HPP

#include <cstdint>
#include <string>
#include <vector>
#include "app/contracts/result.hpp"
#include "app/contracts/money.hpp"
#include "app/contracts/sale_service.hpp"

namespace app::contracts {

// ---------------------------------------------------------------------------------------------------------------
// Reembolsos (B18). Reglas:
//  - Solo ventas del mismo día local. Se elige qué líneas y cuántas piezas; nunca más de lo vendido menos lo ya
//    reembolsado. Cambiar un pedido = reembolsar lo equivocado y cobrar lo correcto como venta nueva.
//  - Importe de una pieza: el neto pagado de su línea (subtotal − descuento de la promoción) en proporción. Lo que
//    sobra por redondeo va a la última pieza de la línea, así que la suma de reembolsos de una línea nunca supera su neto.
//  - El cargo por tarjeta NO se devuelve. Los importes salen de lo guardado en la venta (aunque el producto se haya
//    dado de baja o cambiado de precio después).
//  - Efectivo: exige turno abierto, sale de la caja de ese turno y se rechaza si es mayor que el efectivo esperado.
//    Tarjeta y Transferencia: solo se registra (la devolución real se hace en Mercado Pago).
//  - Inventario: directo (sellado) regresa a existencias; preparado: ya_preparado = false regresa sus ingredientes,
//    true los deja consumidos y se registra como merma; combo: cada parte según su tipo; sin control: no mueve nada.
//    Se usa la receta vigente al reembolsar (límite: si la receta cambió desde la venta, el mismo día, regresa lo de la
//    receta nueva).
//  - Todo en una transacción (reembolso, inventario, auditoría). La misma clave de idempotencia no duplica.
// Permiso: pos.refund (solo el Dueño por omisión; asignable a otros roles).
// ---------------------------------------------------------------------------------------------------------------
struct RefundLineRequestDto {
    std::string sale_item_id;                 // id de la línea de la venta (SaleLineDetailDto::sale_item_id)
    std::int64_t quantity{0};                 // piezas a reembolsar, > 0
    bool already_prepared{false};             // preparados y combos con preparados: ¿ya se preparó?
};

struct RefundRequestDto {
    std::string folio;                        // folio de la venta
    std::vector<RefundLineRequestDto> lines;
    std::string reason;                       // 3 a 200 caracteres
    std::string idempotency_key;              // opcional; la misma clave devuelve el mismo reembolso
};

struct RefundLineDto {
    std::string sale_item_id;
    std::string product_name;                 // nombre guardado en la venta
    std::int64_t quantity{0};
    Money amount{Money::zero()};              // importe devuelto de esta línea
    bool already_prepared{false};
};

// Datos para mostrar y para imprimir el comprobante de reembolso (el formato lo hace la interfaz).
struct RefundDto {
    std::string refund_id;
    std::string refund_folio;                 // «<folio de la venta>-R<n>»
    std::string sale_folio;
    std::string business_name{"Cafetería ITCH II"};
    std::string created_at;                   // hora local
    std::string user_name;                    // quién hizo el reembolso
    PaymentMethod payment_method{PaymentMethod::Efectivo};
    Money amount{Money::zero()};
    std::string reason;
    std::vector<RefundLineDto> lines;
    Money card_surcharge_not_refunded{Money::zero()};   // cargo de la venta que NO se devuelve (solo tarjeta)

    // T17: el comprobante de reembolso se presenta con el mismo formato de texto que un ticket (mismo ancho de
    // columnas y el mismo diálogo de impresión).
    TicketDto to_ticket() const {
        TicketDto t;
        t.business_name = business_name;
        t.folio = refund_folio;
        t.created_at = created_at;
        t.cashier_name = user_name;
        t.document_title = "COMPROBANTE DE REEMBOLSO";
        t.total_label = "TOTAL REEMBOLSADO:";
        t.show_subtotal = false;
        t.show_cash_details = false;
        t.thanks_text.clear();
        t.payment_method = payment_method;
        t.total = amount;
        t.subtotal = amount;
        t.products_total = amount;
        for (const auto& l : lines) {
            TicketLineItemDto it;
            it.product_name = l.product_name + (l.already_prepared ? " (ya preparado)" : "");
            it.quantity = l.quantity;
            it.subtotal = l.amount;
            it.show_unit_detail = false;
            t.items.push_back(std::move(it));
        }
        t.extra_lines.push_back("Venta: " + sale_folio);
        t.extra_lines.push_back("Motivo: " + reason);
        if (payment_method == PaymentMethod::Efectivo) {
            t.extra_lines.push_back("Se devolvió en efectivo de la caja.");
        } else {
            t.extra_lines.push_back("Devolución por Mercado Pago (aquí solo se registró).");
        }
        if (card_surcharge_not_refunded.cents() > 0) {
            t.extra_lines.push_back("El cargo por pago con tarjeta (" + card_surcharge_not_refunded.to_string() + ") no se devuelve.");
        }
        return t;
    }
    std::string to_formatted_text(size_t max_cols = 40) const { return to_ticket().to_formatted_text(max_cols); }
};

// T17: lo que se devolvería con una solicitud, SIN escribir nada. Aplica las mismas reglas y produce los mismos
// mensajes que refund_sale (el servicio es el único lugar con las reglas). El motivo se valida como una regla más: si
// es inválido, el importe se calcula igual y el mensaje llega en reason_error (vacío si el motivo es válido).
struct RefundPreviewDto {
    Money amount{Money::zero()};
    std::vector<RefundLineDto> lines;
    PaymentMethod payment_method{PaymentMethod::Efectivo};
    bool returns_cash{false};                           // true: sale efectivo de la caja del turno abierto
    Money card_surcharge_not_refunded{Money::zero()};
    std::string reason_error;
};

struct SaleLineDetailDto {
    std::string sale_item_id;
    std::string product_id;
    std::string product_name;
    bool is_prepared{false};                  // producto preparado: la interfaz pregunta si ya se preparó
    bool is_combo{false};
    bool has_prepared_parts{false};           // T17: preparado, o combo con alguna parte preparada («¿ya se preparó?»)
    std::int64_t quantity_sold{0};
    std::int64_t quantity_refunded{0};
    std::int64_t quantity_refundable{0};      // vendido − reembolsado
    Money unit_price{Money::zero()};
    Money subtotal{Money::zero()};            // cantidad × precio, sin descuento
    Money discount{Money::zero()};            // descuento de la promoción de la línea
    std::string promotion_name;
    Money net_paid{Money::zero()};            // subtotal − descuento
    Money net_refunded{Money::zero()};        // ya devuelto de esta línea
};

struct SaleDetailDto {
    std::string sale_id;
    std::string folio;
    std::string created_at;
    std::string cashier_name;
    PaymentMethod payment_method{PaymentMethod::Efectivo};
    Money products_total{Money::zero()};      // neto de productos
    Money discount{Money::zero()};
    Money card_surcharge{Money::zero()};
    Money charged_total{Money::zero()};       // lo que pagó el cliente
    Money refunded_total{Money::zero()};
    std::vector<SaleLineDetailDto> lines;
    std::vector<RefundDto> refunds;
    bool can_refund{false};                   // mismo día, algo por reembolsar y la sesión tiene permiso
    std::string cannot_refund_reason;         // texto llano si can_refund es falso
};

class IRefundService {
public:
    virtual ~IRefundService() = default;

    // Consulta de la venta con lo reembolsable por línea. Requiere pos.refund.
    virtual Result<SaleDetailDto> get_sale_for_refund(const std::string& folio) = 0;
    virtual Result<RefundDto> refund_sale(const RefundRequestDto& request) = 0;
    // T17: igual que refund_sale pero sin escribir nada. Requiere pos.refund. Un error de cualquier regla (excepto el
    // motivo) llega como fallo con el mismo mensaje de refund_sale.
    virtual Result<RefundPreviewDto> preview_refund(const RefundRequestDto& request) = 0;
    // Requieren pos.refund o pos.reprint.
    virtual Result<std::vector<RefundDto>> list_refunds(const std::string& sale_folio) = 0;
    virtual Result<RefundDto> get_refund(const std::string& refund_id) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_REFUND_SERVICE_HPP
