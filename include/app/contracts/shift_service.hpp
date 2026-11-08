#ifndef APP_CONTRACTS_SHIFT_SERVICE_HPP
#define APP_CONTRACTS_SHIFT_SERVICE_HPP

#include <string>
#include <vector>
#include "app/contracts/result.hpp"
#include "app/contracts/money.hpp"
#include "app/contracts/dto.hpp"

namespace app::contracts {

class IShiftService {
public:
    virtual ~IShiftService() = default;

    virtual Result<ShiftDto> get_current_shift() = 0;
    virtual Result<ShiftDto> open_shift(
        const std::string& register_id,
        Money initial_cash
    ) = 0;
    virtual OperationResult close_shift(Money final_cash) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_SHIFT_SERVICE_HPP
