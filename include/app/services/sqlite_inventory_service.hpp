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

    contracts::OperationResult register_waste(
        const std::string& ingredient_id,
        std::int64_t quantity,
        const std::string& unit,
        const std::string& reason
    ) override;
    contracts::OperationResult register_adjustment(
        const std::string& ingredient_id,
        std::int64_t signed_quantity,
        const std::string& unit,
        const std::string& reason
    ) override;
    contracts::Result<contracts::IngredientCostDto> get_ingredient_cost(
        const std::string& ingredient_id, std::int64_t quantity_base_units) override;
    contracts::Result<std::vector<contracts::StockMovementDto>> list_stock_movements(
        const contracts::StockMovementFilterDto& filter) override;
    contracts::Result<std::vector<contracts::PurchaseEntryRecordDto>> list_purchase_entries(
        const contracts::PurchaseEntryFilterDto& filter) override;

private:
    contracts::OperationResult check_permission_or_fail(std::string_view perm) const;
    // Operación común de merma y ajuste: una sola transacción (movimiento, existencia y auditoría).
    contracts::OperationResult register_stock_change(
        const char* movement_type,
        const std::string& ingredient_id,
        std::int64_t signed_quantity,
        const std::string& unit,
        const std::string& reason
    );

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_INVENTORY_SERVICE_HPP
