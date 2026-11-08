#ifndef APP_SERVICES_SQLITE_INVENTORY_SERVICE_HPP
#define APP_SERVICES_SQLITE_INVENTORY_SERVICE_HPP

#include <memory>
#include <vector>
#include <string>
#include <optional>
#include "app/contracts/inventory_service.hpp"
#include "app/contracts/account_service.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

class SqliteInventoryService : public contracts::IInventoryService {
public:
    SqliteInventoryService(
        storage::SqliteDatabase& db,
        std::shared_ptr<contracts::IAccountService> account_service
    );

    contracts::Result<contracts::IngredientDto> create_ingredient(const contracts::CreateIngredientDto& input) override;
    contracts::OperationResult update_ingredient(const contracts::UpdateIngredientDto& input) override;
    contracts::Result<std::vector<contracts::IngredientDto>> list_ingredients() override;
    contracts::Result<contracts::IngredientDto> get_ingredient(const std::string& ingredient_id) override;

    contracts::Result<std::vector<contracts::StockAlertDto>> get_stock_alerts() override;
    contracts::OperationResult update_low_stock_threshold(
        const std::string& ingredient_id,
        std::int64_t new_threshold_base
    ) override;

    contracts::OperationResult register_purchase_entry(
        const std::string& ingredient_id,
        std::int64_t quantity_purchased,
        const std::string& purchase_unit,
        contracts::Money total_cost
    ) override;

private:
    contracts::OperationResult check_permission_or_fail(std::string_view perm) const;

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_INVENTORY_SERVICE_HPP
