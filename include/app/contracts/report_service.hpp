#ifndef APP_CONTRACTS_REPORT_SERVICE_HPP
#define APP_CONTRACTS_REPORT_SERVICE_HPP

#include <cstdint>
#include <string>
#include <vector>
#include "app/contracts/result.hpp"
#include "app/contracts/money.hpp"

namespace app::contracts {

struct DailySalesReportDto {
    std::string date;
    std::int64_t total_sales_count{0};
    // total_amount es el NETO cobrado (ya con descuentos): coincide con los pagos. B14: gross_amount es la suma de
    // los subtotales sin descuento y discount_amount la suma de descuentos; gross_amount - discount_amount == total_amount.
    Money total_amount{Money::zero()};
    Money gross_amount{Money::zero()};
    Money discount_amount{Money::zero()};
    Money cash_amount{Money::zero()};
    Money card_amount{Money::zero()};
    Money transfer_amount{Money::zero()};
    // Suma de pagos registrados de las ventas del día (efectivo + tarjeta + transferencia).
    // Cada pago se atribuye al día de su venta (sales.created_at), no a su propia hora.
    Money payments_total{Money::zero()};
    // total_amount - payments_total, con signo. 0 = los pagos cuadran con las ventas.
    // > 0: faltan pagos registrados. < 0: hay pagos de más. No se oculta: el reporte se devuelve igual.
    std::int64_t payments_mismatch_cents{0};
    // B18: cargos por tarjeta cobrados y reembolsos del día (los reembolsos se atribuyen al día de su venta).
    // payments_mismatch_cents = total_amount + card_surcharge_amount - payments_total.
    Money card_surcharge_amount{Money::zero()};
    Money refunds_amount{Money::zero()};
};

// ---------------------------------------------------------------------------------------------------------------
// B15: reportes por rango de fechas (AAAA-MM-DD, hora local, ambos extremos incluidos; máximo 366 días).
// Todos los importes son centavos enteros. En los reportes de ventas: gross - discount = net, y net coincide con
// los pagos registrados (payments_mismatch_cents = 0). Las ventas anuladas no existen en el sistema todavía.
// ---------------------------------------------------------------------------------------------------------------
struct SalesTotalsDto {
    std::int64_t sales_count{0};
    std::int64_t pieces{0};   // solo se llena en el reporte por producto
    Money gross{Money::zero()};
    Money discount{Money::zero()};
    Money net{Money::zero()};
    Money cash{Money::zero()};
    Money card{Money::zero()};
    Money transfer{Money::zero()};
    Money payments_total{Money::zero()};       // lo pagado: neto de productos + cargos por tarjeta
    std::int64_t payments_mismatch_cents{0};   // net + card_surcharges - payments_total
    // B18: cargos por tarjeta (aparte: no inflan las ventas de productos) y reembolsos del rango.
    Money card_surcharges{Money::zero()};
    Money refunds{Money::zero()};
    Money net_after_refunds{Money::zero()};    // net - refunds
};

struct SalesByDayRowDto {
    std::string date;   // AAAA-MM-DD
    std::int64_t sales_count{0};
    Money gross{Money::zero()};
    Money discount{Money::zero()};
    Money net{Money::zero()};
    Money cash{Money::zero()};
    Money card{Money::zero()};
    Money transfer{Money::zero()};
    Money card_surcharges{Money::zero()};   // B18
    Money refunds{Money::zero()};           // B18
    Money net_after_refunds{Money::zero()}; // B18: net - refunds
};
struct SalesByDayReportDto { std::string from_date, to_date; std::vector<SalesByDayRowDto> rows; SalesTotalsDto totals; };

struct SalesByProductRowDto {
    std::string product_id;
    std::string product_name;   // nombre al momento de la venta
    std::int64_t pieces{0};            // B18: vendidas MENOS reembolsadas
    Money gross{Money::zero()};
    Money discount{Money::zero()};
    Money net{Money::zero()};
    std::int64_t pieces_refunded{0};   // B18
    Money refunds{Money::zero()};      // B18: importe reembolsado de este producto
    Money net_after_refunds{Money::zero()};   // B18: net - refunds
};
struct SalesByProductReportDto { std::string from_date, to_date; std::vector<SalesByProductRowDto> rows; SalesTotalsDto totals; };

struct SalesByPaymentRowDto {
    std::string method;   // EFECTIVO, TARJETA, TRANSFERENCIA
    std::int64_t payments_count{0};
    Money amount{Money::zero()};          // lo pagado con este método (con tarjeta incluye el cargo)
    Money refunds{Money::zero()};         // B18: reembolsos hechos con este método
};
struct SalesByPaymentReportDto { std::string from_date, to_date; std::vector<SalesByPaymentRowDto> rows; SalesTotalsDto totals; };

struct SalesByCashierRowDto {
    std::string user_id;
    std::string cashier_name;   // nombre al momento de la venta
    std::int64_t sales_count{0};
    Money gross{Money::zero()};
    Money discount{Money::zero()};
    Money net{Money::zero()};
    Money card_surcharges{Money::zero()};   // B18
    Money refunds{Money::zero()};           // B18
    Money net_after_refunds{Money::zero()}; // B18
};
struct SalesByCashierReportDto { std::string from_date, to_date; std::vector<SalesByCashierRowDto> rows; SalesTotalsDto totals; };

// Mermas y ajustes por ingrediente. Los ajustes positivos y negativos van separados (no netos). El costo estimado
// usa la valuación vigente del ingrediente (promedio móvil), no el costo de cuando ocurrió; 0 si no hay costo conocido
// (has_known_cost = false).
struct InventoryLossRowDto {
    std::string ingredient_id;
    std::string ingredient_name;
    std::string base_unit;
    std::int64_t waste_quantity_base{0};          // positivo = cantidad perdida
    std::int64_t adjustment_in_quantity_base{0};  // ajustes que sumaron existencia
    std::int64_t adjustment_out_quantity_base{0}; // ajustes que restaron (positivo = cantidad restada)
    Money waste_estimated_cost{Money::zero()};
    Money adjustment_in_estimated_cost{Money::zero()};
    Money adjustment_out_estimated_cost{Money::zero()};
    bool has_known_cost{false};
};
struct InventoryLossReportDto {
    std::string from_date, to_date;
    std::vector<InventoryLossRowDto> rows;
    Money total_waste_estimated_cost{Money::zero()};
    Money total_adjustment_in_estimated_cost{Money::zero()};
    Money total_adjustment_out_estimated_cost{Money::zero()};
};

struct InventoryMovementRowDto {
    std::string created_at;
    std::string ingredient_name;
    std::string movement_type;   // PURCHASE, SALE, ADJUSTMENT, WASTE, TRANSFER
    std::int64_t quantity_base{0};   // con signo
    std::string base_unit;
    std::string reason;
    std::string actor_name;
};
struct InventoryMovementsReportDto {
    std::string from_date, to_date;
    std::vector<InventoryMovementRowDto> rows;   // del más reciente al más antiguo
    bool truncated{false};                       // más movimientos que el límite
};

struct PurchasesSummaryRowDto {
    std::string ingredient_id;
    std::string ingredient_name;
    std::string base_unit;
    std::int64_t purchases_count{0};
    std::int64_t base_units_purchased{0};
    Money total_cost{Money::zero()};
};
struct PurchasesSummaryReportDto { std::string from_date, to_date; std::vector<PurchasesSummaryRowDto> rows; Money total_cost{Money::zero()}; };

enum class ReportKind {
    SalesByDay, SalesByProduct, SalesByPaymentMethod, SalesByCashier,
    InventoryLosses, InventoryMovements, PurchasesSummary
};

struct ReportExportResultDto {
    std::string file_name;        // solo el nombre, sin carpeta
    std::int64_t rows_written{0};
};

class IReportService {
public:
    virtual ~IReportService() = default;

    virtual Result<DailySalesReportDto> get_daily_sales_summary(const std::string& date_iso) = 0;

    // --- B15 (aditivo): todos requieren reports.view ---
    virtual Result<SalesByDayReportDto> sales_by_day(const std::string& from_date, const std::string& to_date) = 0;
    virtual Result<SalesByProductReportDto> sales_by_product(const std::string& from_date, const std::string& to_date) = 0;
    virtual Result<SalesByPaymentReportDto> sales_by_payment_method(const std::string& from_date, const std::string& to_date) = 0;
    virtual Result<SalesByCashierReportDto> sales_by_cashier(const std::string& from_date, const std::string& to_date) = 0;
    virtual Result<InventoryLossReportDto> inventory_losses(const std::string& from_date, const std::string& to_date) = 0;
    // limit: omisión 1000, máximo 5000.
    virtual Result<InventoryMovementsReportDto> inventory_movements(const std::string& from_date, const std::string& to_date,
                                                                    int limit = 1000) = 0;
    virtual Result<PurchasesSummaryReportDto> purchases_summary(const std::string& from_date, const std::string& to_date) = 0;

    // Escribe el reporte como CSV en `file_path` (UTF-8 con BOM, coma, CRLF, importes con dos decimales y punto, fechas locales,
    // encabezados en español). Si el archivo existe y overwrite = false devuelve Conflict. Escribe a un temporal en
    // la misma carpeta y renombra al terminar. Sin límite de filas. Audita REPORT_EXPORTED (tipo, rango, nombre).
    virtual Result<ReportExportResultDto> export_report_csv(ReportKind kind, const std::string& from_date,
                                                           const std::string& to_date, const std::string& file_path,
                                                           bool overwrite = false) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_REPORT_SERVICE_HPP
