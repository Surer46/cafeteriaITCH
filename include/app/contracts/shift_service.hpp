#ifndef APP_CONTRACTS_SHIFT_SERVICE_HPP
#define APP_CONTRACTS_SHIFT_SERVICE_HPP

#include <string>
#include <vector>
#include "app/contracts/result.hpp"
#include "app/contracts/money.hpp"
#include "app/contracts/dto.hpp"

namespace app::contracts {

// B15: entradas y salidas de efectivo del turno.
enum class CashMovementKind { Inflow, Outflow };

struct CashMovementDto {
    std::string id;
    CashMovementKind kind{CashMovementKind::Inflow};
    Money amount{Money::zero()};   // siempre > 0; el tipo da el signo
    std::string description;
    std::string created_at;        // hora local
};

// B15: conciliación de un turno. La misma estructura sirve para la vista previa (turno abierto: is_closed = false,
// counted_cash/difference sin significado) y para la instantánea guardada al cerrar (is_closed = true).
// expected_cash = initial_cash + cash_sales + cash_inflows - cash_outflows - cash_refunds (B18).
// gross_sales - discount_total = net_sales; net_sales + card_surcharges = cash_sales + card_sales + transfer_sales (si no, payments_mismatch_cents != 0).
struct ShiftCloseoutDto {
    std::string shift_id;
    std::string register_name;
    std::string opened_by_user_id;
    std::string opened_by_name;
    std::string opened_at;
    std::string closed_at;
    std::string closed_by_user_id;
    std::string closed_by_name;
    bool is_closed{false};
    // false = turno cerrado antes de la migración 020: no tiene instantánea, los importes se calcularon al consultar.
    bool snapshot_saved{false};
    Money initial_cash{Money::zero()};
    std::int64_t sales_count{0};
    Money gross_sales{Money::zero()};
    Money discount_total{Money::zero()};
    Money net_sales{Money::zero()};
    Money cash_sales{Money::zero()};
    Money card_sales{Money::zero()};
    Money transfer_sales{Money::zero()};
    Money cash_inflows{Money::zero()};
    Money cash_outflows{Money::zero()};
    // B18: reembolsos hechos durante el turno, por método, y cargos por tarjeta cobrados. card_sales incluye los cargos
    // (es lo que se pagó con tarjeta). Las instantáneas anteriores a la migración 022 los traen en 0.
    Money cash_refunds{Money::zero()};
    Money card_refunds{Money::zero()};
    Money transfer_refunds{Money::zero()};
    Money card_surcharges{Money::zero()};
    Money expected_cash{Money::zero()};
    Money counted_cash{Money::zero()};
    std::int64_t difference_cents{0};   // contado - esperado, con signo (> 0 sobrante, < 0 faltante)
    std::string difference_reason;
    std::int64_t payments_mismatch_cents{0};  // net_sales - pagos; 0 = cuadra
    std::vector<CashMovementDto> movements;   // entradas y salidas, de la más antigua a la más reciente
};

struct ClosedShiftListDto {
    std::vector<ShiftCloseoutDto> items;   // del cierre más reciente al más antiguo; sin la lista de movimientos
    bool truncated{false};                 // hubo más turnos que el límite
};

class IShiftService {
public:
    virtual ~IShiftService() = default;

    virtual Result<ShiftDto> get_current_shift() = 0;
    virtual Result<ShiftDto> open_shift(
        const std::string& register_id,
        Money initial_cash
    ) = 0;
    // No cambia de comportamiento: cierra con cualquier diferencia. Delega en close_shift_with_count del turno
    // abierto; si hay diferencia el motivo queda como "Sin motivo registrado".
    virtual OperationResult close_shift(Money final_cash) = 0;

    // --- B15 (aditivo) ---
    // Vista previa del corte del turno abierto. shift_id vacío = el turno abierto. Requiere shift.operate; ver el
    // de otro usuario no requiere más permiso.
    virtual Result<ShiftCloseoutDto> preview_closeout(const std::string& shift_id = "") = 0;
    // Cierra con arqueo. Cierra el propio turno (shift.operate) o el de otro (shift.operate + Dueño o
    // shift.close_others). Motivo obligatorio (3 a 200 caracteres) si contado != esperado. Un turno ya cerrado
    // devuelve Conflict. Devuelve la instantánea guardada.
    virtual Result<ShiftCloseoutDto> close_shift_with_count(const std::string& shift_id, Money counted_cash,
                                                           const std::string& difference_reason) = 0;
    // Instantánea de un turno cerrado (o la vista previa si sigue abierto). shift.operate: solo los propios;
    // reports.view o Dueño: todos.
    virtual Result<ShiftCloseoutDto> get_closeout(const std::string& shift_id) = 0;
    // Turnos cerrados entre dos fechas locales AAAA-MM-DD (por fecha de cierre), con límite (omisión 100, máx. 1000).
    virtual Result<ClosedShiftListDto> list_closed_shifts(const std::string& from_date, const std::string& to_date,
                                                         int limit = 100) = 0;
    // Entrada o salida de efectivo del turno abierto. shift.operate. Importe > 0, descripción 3 a 200 caracteres.
    // Una salida mayor que el efectivo esperado en ese momento se rechaza.
    virtual Result<CashMovementDto> record_cash_movement(CashMovementKind kind, Money amount,
                                                        const std::string& description) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_SHIFT_SERVICE_HPP
