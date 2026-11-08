#ifndef APP_SERVICES_SQLITE_SHIFT_SERVICE_HPP
#define APP_SERVICES_SQLITE_SHIFT_SERVICE_HPP

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

private:
    contracts::OperationResult check_permission_or_fail(std::string_view perm) const;

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_SHIFT_SERVICE_HPP
