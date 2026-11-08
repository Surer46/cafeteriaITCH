#ifndef APP_SERVICES_MOCK_SERVICES_HPP
#define APP_SERVICES_MOCK_SERVICES_HPP

#include <memory>
#include <map>
#include "app/contracts/account_service.hpp"
#include "app/contracts/catalog_service.hpp"
#include "app/contracts/cart_service.hpp"
#include "app/contracts/sale_service.hpp"
#include "app/contracts/inventory_service.hpp"
#include "app/contracts/shift_service.hpp"
#include "app/contracts/report_service.hpp"
#include "app/contracts/backup_service.hpp"
#include "app/contracts/print_config_service.hpp"

namespace app::storage {
class SqliteDatabase;
}

namespace app::services {

class MockAccountService : public contracts::IAccountService {
public:
    MockAccountService();
    contracts::Result<bool> has_owner() override;
    contracts::OperationResult create_initial_owner(
        const std::string& username,
        const std::string& display_name,
        const std::string& raw_password,
        const std::string& email,
        const std::optional<std::vector<std::uint8_t>>& avatar_bytes = std::nullopt
    ) override;
    contracts::Result<contracts::UserDto> login(
        const std::string& username,
        const std::string& raw_password
    ) override;
    contracts::OperationResult logout() override;
    contracts::Result<contracts::UserDto> current_user() const override;
    bool is_authenticated() const override;
    bool has_permission(std::string_view permission_key) const override;
    contracts::Result<bool> can_deactivate_user(const std::string& user_id) override;
    contracts::OperationResult deactivate_user(const std::string& user_id) override;
    contracts::Result<contracts::UserDto> create_user(const contracts::CreateUserDto& input) override;
    contracts::OperationResult update_user(const contracts::UpdateUserDto& input) override;
    contracts::OperationResult change_password(
        const std::string& user_id,
        const std::string& new_raw_password
    ) override;
    contracts::Result<contracts::UserDto> get_user_by_id(const std::string& user_id) override;
    contracts::Result<std::vector<contracts::UserDto>> list_users() override;
    contracts::Result<std::vector<std::uint8_t>> get_user_avatar(const std::string& user_id) override;

    contracts::Result<std::vector<contracts::RoleDto>> list_roles() override;
    contracts::Result<contracts::RoleDto> get_role_by_id(const std::string& role_id) override;
    contracts::Result<contracts::RoleDto> create_role(const contracts::CreateRoleDto& input) override;
    contracts::OperationResult update_role(const contracts::UpdateRoleDto& input) override;
    contracts::OperationResult delete_role(const std::string& role_id) override;
    contracts::Result<std::vector<contracts::PermissionDto>> list_permissions() override;

    contracts::Result<std::vector<contracts::AuditEventDto>> list_audit_events(const contracts::AuditFilterDto& filter) override;
    contracts::OperationResult record_audit_event(
        const std::string& event_type,
        const std::string& details
    ) override;

private:
    bool has_owner_{true};
    std::optional<contracts::UserDto> current_user_;
    std::vector<contracts::UserDto> users_;
    std::vector<contracts::RoleDto> roles_;
    std::vector<contracts::AuditEventDto> audit_events_;
    std::map<std::string, std::vector<std::uint8_t>> user_avatars_;
};

class MockCatalogService : public contracts::ICatalogService {
public:
    MockCatalogService();
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
    contracts::OperationResult set_recipe(
        const std::string& product_id,
        const std::vector<contracts::RecipeItemDto>& items
    ) override;

private:
    std::vector<contracts::ProductDto> products_;
    std::map<std::string, contracts::ProductImageDto> images_;
    std::map<std::string, contracts::RecipeDto> recipes_;
};

class MockCartService : public contracts::ICartService {
public:
    MockCartService();
    contracts::Result<contracts::CartSummaryDto> get_cart() override;
    contracts::Result<contracts::CartSummaryDto> add_item(const std::string& product_id, std::int64_t quantity = 1, const std::string& notes = "") override;
    contracts::Result<contracts::CartSummaryDto> update_quantity(const std::string& line_id, std::int64_t new_quantity) override;
    contracts::Result<contracts::CartSummaryDto> update_item_notes(const std::string& line_id, const std::string& notes) override;
    contracts::Result<contracts::CartSummaryDto> remove_item(const std::string& line_id) override;
    contracts::OperationResult clear_cart() override;

private:
    void recalculate();
    contracts::CartSummaryDto cart_;
};

class MockShiftService : public contracts::IShiftService {
public:
    MockShiftService();
    contracts::Result<contracts::ShiftDto> get_current_shift() override;
    contracts::Result<contracts::ShiftDto> open_shift(
        const std::string& register_id,
        contracts::Money initial_cash
    ) override;
    contracts::OperationResult close_shift(contracts::Money final_cash) override;

private:
    contracts::ShiftDto current_shift_;
};

class MockInventoryService : public contracts::IInventoryService {
public:
    MockInventoryService();
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
    std::vector<contracts::IngredientDto> ingredients_;
};

class MockSaleService : public contracts::ISaleService {
public:
    MockSaleService();
    contracts::Result<contracts::SaleConfirmationDto> confirm_sale(
        contracts::PaymentMethod payment_method,
        contracts::Money cash_received,
        const std::string& idempotency_key = ""
    ) override;
    contracts::Result<contracts::SaleConfirmationDto> get_sale_by_folio(const std::string& folio) override;
    contracts::Result<contracts::TicketDto> get_ticket(const std::string& folio) override;
    contracts::Result<std::vector<contracts::TicketSummaryDto>> get_recent_tickets(int limit = 50) override;

private:
    std::int64_t next_folio_{1001};
};

class MockReportService : public contracts::IReportService {
public:
    MockReportService();
    contracts::Result<contracts::DailySalesReportDto> get_daily_sales_summary(const std::string& date_iso) override;
};

class MockBackupService : public contracts::IBackupService {
public:
    MockBackupService();
    contracts::Result<contracts::BackupStatusDto> get_status() override;
    contracts::OperationResult trigger_local_backup(const std::string& destination_path) override;
    contracts::OperationResult set_backup_enabled(bool enabled) override;
    contracts::Result<contracts::BackupVerificationDto> verify_backup(const std::string& path) override;
    contracts::Result<contracts::RestoreResultDto> restore_from_backup(const std::string& path) override;
    contracts::Result<contracts::BackupServerConfigDto> get_developer_server_config() override;
    contracts::OperationResult update_developer_server_config(const contracts::BackupServerConfigDto& config) override;

private:
    contracts::BackupStatusDto status_;
    contracts::BackupServerConfigDto dev_config_;
};

class MockPrintConfigService : public contracts::IPrintConfigService {
public:
    MockPrintConfigService() {
        config_.profile = contracts::ThermalPaperProfile::Profile80mm;
        config_.printer_name = "Impresora Mock 80mm";
        config_.nominal_width_mm = 80;
        config_.characters_per_line = 42;
    }

    contracts::Result<contracts::PrinterStationConfig> get_station_config() override {
        return contracts::Result<contracts::PrinterStationConfig>::ok(config_);
    }

    contracts::OperationResult set_station_config(const contracts::PrinterStationConfig& config) override {
        config_ = config;
        return contracts::OperationResult::ok();
    }

private:
    contracts::PrinterStationConfig config_;
};

struct ServiceRegistry {
    std::shared_ptr<contracts::IAccountService> account_service;
    std::shared_ptr<contracts::ICatalogService> catalog_service;
    std::shared_ptr<contracts::ICartService> cart_service;
    std::shared_ptr<contracts::ISaleService> sale_service;
    std::shared_ptr<contracts::IInventoryService> inventory_service;
    std::shared_ptr<contracts::IShiftService> shift_service;
    std::shared_ptr<contracts::IReportService> report_service;
    std::shared_ptr<contracts::IBackupService> backup_service;
    std::shared_ptr<contracts::IPrintConfigService> print_config_service;

    static ServiceRegistry create_mock_registry();
    static ServiceRegistry create_sqlite_registry(storage::SqliteDatabase& db,
                                                   const std::string& config_dir = "",
                                                   const std::string& migrations_dir = "");
};

} // namespace app::services

#endif // APP_SERVICES_MOCK_SERVICES_HPP
