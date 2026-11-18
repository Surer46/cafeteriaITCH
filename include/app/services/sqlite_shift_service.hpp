#ifndef APP_SERVICES_SQLITE_SHIFT_SERVICE_HPP
#define APP_SERVICES_SQLITE_SHIFT_SERVICE_HPP

#include <functional>
#include <memory>
#include <string>
#include "app/contracts/shift_service.hpp"
#include "app/contracts/account_service.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

class SqliteShiftService : public contracts::IShiftService {
public:
    SqliteShiftService(
        storage::SqliteDatabase& db,
        std::shared_ptr<contracts::IAccountService> account_service
    );

    contracts::Result<contracts::ShiftDto> get_current_shift() override;
    contracts::Result<contracts::ShiftDto> open_shift(
        const std::string& register_id,
        contracts::Money initial_cash
    ) override;
    contracts::OperationResult close_shift(contracts::Money final_cash) override;

    // B15
    contracts::Result<contracts::ShiftCloseoutDto> preview_closeout(const std::string& shift_id = "") override;
    contracts::Result<contracts::ShiftCloseoutDto> close_shift_with_count(
        const std::string& shift_id, contracts::Money counted_cash, const std::string& difference_reason) override;
    contracts::Result<contracts::ShiftCloseoutDto> get_closeout(const std::string& shift_id) override;
    contracts::Result<contracts::ClosedShiftListDto> list_closed_shifts(
        const std::string& from_date, const std::string& to_date, int limit = 100) override;
    contracts::Result<contracts::CashMovementDto> record_cash_movement(
        contracts::CashMovementKind kind, contracts::Money amount, const std::string& description) override;

    // Gancho opcional que se llama DESPUÉS de confirmar un cierre de turno (no forma parte del contrato).
    // Recibe el id del turno cerrado y el id del usuario que lo cerró. Su resultado y sus excepciones
    // nunca cambian el resultado de close_shift: el cierre ya está confirmado cuando se invoca.
    using AfterCloseHook = std::function<void(const std::string& shift_id, const std::string& user_id)>;
    void set_after_close_hook(AfterCloseHook hook) { after_close_hook_ = std::move(hook); }

private:
    contracts::OperationResult check_permission_or_fail(std::string_view perm) const;
    std::string live_totals(const std::string& shift_id, contracts::ShiftCloseoutDto& out);
    contracts::Result<contracts::ShiftCloseoutDto> load_closeout(const std::string& shift_id, bool with_movements);
    contracts::Result<contracts::ShiftCloseoutDto> close_impl(const std::string& shift_id, contracts::Money counted,
                                                             const std::string& reason, bool default_reason_when_different);
    bool is_owner_user(const contracts::UserDto& u) const;
    AfterCloseHook after_close_hook_;

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_SHIFT_SERVICE_HPP
