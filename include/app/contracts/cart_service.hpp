#ifndef APP_CONTRACTS_CART_SERVICE_HPP
#define APP_CONTRACTS_CART_SERVICE_HPP

#include <string>
#include <cstdint>
#include "app/contracts/result.hpp"
#include "app/contracts/dto.hpp"

namespace app::contracts {

class ICartService {
public:
    virtual ~ICartService() = default;

    virtual Result<CartSummaryDto> get_cart() = 0;
    virtual Result<CartSummaryDto> add_item(
        const std::string& product_id,
        std::int64_t quantity = 1,
        const std::string& notes = ""
    ) = 0;
    virtual Result<CartSummaryDto> update_quantity(const std::string& line_id, std::int64_t new_quantity) = 0;
    virtual Result<CartSummaryDto> update_item_notes(const std::string& line_id, const std::string& notes) = 0;
    virtual Result<CartSummaryDto> remove_item(const std::string& line_id) = 0;
    virtual OperationResult clear_cart() = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_CART_SERVICE_HPP
