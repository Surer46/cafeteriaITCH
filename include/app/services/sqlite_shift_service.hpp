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

    // Gancho opcional que se llama DESPUÉS de confirmar un cierre de turno (no forma parte del contrato).
    // Recibe el id del turno cerrado y el id del usuario que lo cerró. Su resultado y sus excepciones
    // nunca cambian el resultado de close_shift: el cierre ya está confirmado cuando se invoca.
    using AfterCloseHook = std::function<void(const std::string& shift_id, const std::string& user_id)>;
    void set_after_close_hook(AfterCloseHook hook) { after_close_hook_ = std::move(hook); }

private:
    contracts::OperationResult check_permission_or_fail(std::string_view perm) const;
    AfterCloseHook after_close_hook_;

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_SHIFT_SERVICE_HPP
