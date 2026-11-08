#ifndef APP_SERVICES_SQLITE_CART_SERVICE_HPP
#define APP_SERVICES_SQLITE_CART_SERVICE_HPP

#include <memory>
#include <string>
#include "app/contracts/cart_service.hpp"
#include "app/contracts/account_service.hpp"
#include "app/contracts/catalog_service.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

class SqliteCartService : public contracts::ICartService {
public:
    SqliteCartService(
        storage::SqliteDatabase& db,
        std::shared_ptr<contracts::IAccountService> account_service,
        std::shared_ptr<contracts::ICatalogService> catalog_service
    );

    contracts::Result<contracts::CartSummaryDto> get_cart() override;
    contracts::Result<contracts::CartSummaryDto> add_item(
        const std::string& product_id,
        std::int64_t quantity = 1,
        const std::string& notes = ""
    ) override;
    contracts::Result<contracts::CartSummaryDto> update_quantity(
        const std::string& line_id,
        std::int64_t new_quantity
    ) override;
    contracts::Result<contracts::CartSummaryDto> update_item_notes(
        const std::string& line_id,
        const std::string& notes
    ) override;
    contracts::Result<contracts::CartSummaryDto> remove_item(const std::string& line_id) override;
    contracts::OperationResult clear_cart() override;

    // Helper público para obtener el ID del carrito activo
    contracts::Result<std::string> get_or_create_cart_id();

private:
    contracts::OperationResult check_permission_or_fail(std::string_view perm) const;
    static bool validate_line_notes(const std::string& notes, std::string& error);
    contracts::OperationResult touch_cart(const std::string& cart_id);

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
    std::shared_ptr<contracts::ICatalogService> catalog_service_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_CART_SERVICE_HPP
