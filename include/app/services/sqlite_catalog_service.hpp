#ifndef APP_SERVICES_SQLITE_CATALOG_SERVICE_HPP
#define APP_SERVICES_SQLITE_CATALOG_SERVICE_HPP

#include <memory>
#include <vector>
#include <string>
#include <optional>
#include "app/contracts/catalog_service.hpp"
#include "app/contracts/account_service.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

class SqliteCatalogService : public contracts::ICatalogService {
public:
    SqliteCatalogService(
        storage::SqliteDatabase& db,
        std::shared_ptr<contracts::IAccountService> account_service
    );

    contracts::Result<std::vector<contracts::ProductDto>> search_products(
        const std::string& query,
        const std::optional<std::string>& category,
        std::size_t offset,
        std::size_t limit,
        bool include_inactive = false
    ) override;

    contracts::Result<contracts::ProductDto> get_product_by_id(const std::string& product_id) override;
    contracts::Result<contracts::ProductDto> get_product_by_sku_or_barcode(const std::string& code) override;
    contracts::Result<std::vector<std::string>> list_categories() override;

    contracts::Result<contracts::ProductDto> create_product(const contracts::CreateProductDto& input) override;
    contracts::OperationResult update_product(const contracts::UpdateProductDto& input) override;
    contracts::OperationResult deactivate_product(const std::string& product_id) override;
    contracts::OperationResult reactivate_product(const std::string& product_id) override;

    contracts::OperationResult set_product_image(
        const std::string& product_id,
        const std::string& mime_type,
        const std::vector<std::uint8_t>& image_bytes
    ) override;
    contracts::Result<contracts::ProductImageDto> get_product_image(const std::string& product_id) override;
    contracts::OperationResult remove_product_image(const std::string& product_id) override;

    contracts::Result<contracts::RecipeDto> get_recipe(const std::string& product_id) override;
    contracts::Result<contracts::RecipeCostingDto> get_recipe_costing(const std::string& product_id) override;
    contracts::OperationResult set_recipe(
        const std::string& product_id,
        const std::vector<contracts::RecipeItemDto>& items
    ) override;

private:
    contracts::OperationResult check_permission_or_fail(std::string_view perm) const;

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_CATALOG_SERVICE_HPP
