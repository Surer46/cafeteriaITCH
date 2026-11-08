#ifndef APP_CONTRACTS_INVENTORY_SERVICE_HPP
#define APP_CONTRACTS_INVENTORY_SERVICE_HPP

#include <string>
#include <vector>
#include <cstdint>
#include "app/contracts/result.hpp"
#include "app/contracts/money.hpp"
#include "app/contracts/dto.hpp"

namespace app::contracts {

class IInventoryService {
public:
    virtual ~IInventoryService() = default;

    virtual Result<IngredientDto> create_ingredient(const CreateIngredientDto& input) = 0;
    virtual OperationResult update_ingredient(const UpdateIngredientDto& input) = 0;
    virtual Result<std::vector<IngredientDto>> list_ingredients() = 0;
    virtual Result<IngredientDto> get_ingredient(const std::string& ingredient_id) = 0;
    virtual Result<std::vector<StockAlertDto>> get_stock_alerts() = 0;
    virtual OperationResult update_low_stock_threshold(
        const std::string& ingredient_id,
        std::int64_t new_threshold_base
    ) = 0;

    virtual OperationResult register_purchase_entry(
        const std::string& ingredient_id,
        std::int64_t quantity_purchased,
        const std::string& purchase_unit,
        Money total_cost
    ) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_INVENTORY_SERVICE_HPP
