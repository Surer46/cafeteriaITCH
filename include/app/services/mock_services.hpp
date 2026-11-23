#ifndef APP_SERVICES_MOCK_SERVICES_HPP
#define APP_SERVICES_MOCK_SERVICES_HPP

#include <memory>
#include <map>
#include "app/domain/stock_valuation.hpp"
#include "app/contracts/account_service.hpp"
#include "app/contracts/catalog_service.hpp"
#include "app/contracts/cart_service.hpp"
#include "app/contracts/sale_service.hpp"
#include "app/contracts/inventory_service.hpp"
#include "app/contracts/shift_service.hpp"
#include "app/contracts/report_service.hpp"
#include "app/contracts/backup_service.hpp"
#include "app/contracts/update_service.hpp"
#include "app/contracts/promotion_service.hpp"
#include "app/contracts/refund_service.hpp"
#include "app/contracts/payment_settings_service.hpp"
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
    contracts::Result<contracts::RecipeCostingDto> get_recipe_costing(const std::string& product_id) override;
    contracts::OperationResult enable_stock_for_direct_product(const std::string& product_id, const std::string& link_ingredient_id = "") override;
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
    contracts::Result<contracts::ShiftCloseoutDto> preview_closeout(const std::string& shift_id = "") override;
    contracts::Result<contracts::ShiftCloseoutDto> close_shift_with_count(
        const std::string& shift_id, contracts::Money counted_cash, const std::string& difference_reason) override;
    contracts::Result<contracts::ShiftCloseoutDto> get_closeout(const std::string& shift_id) override;
    contracts::Result<contracts::ClosedShiftListDto> list_closed_shifts(
        const std::string& from_date, const std::string& to_date, int limit = 100) override;
    contracts::Result<contracts::CashMovementDto> record_cash_movement(
        contracts::CashMovementKind kind, contracts::Money amount, const std::string& description) override;

private:
    contracts::ShiftCloseoutDto build_closeout() const;
    contracts::ShiftDto current_shift_;
    std::vector<contracts::CashMovementDto> movements_;
    std::vector<contracts::ShiftCloseoutDto> closed_;
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
    contracts::OperationResult apply_mock_change(
        const char* type, const std::string& ingredient_id, std::int64_t signed_quantity,
        const std::string& unit, const std::string& reason);

    domain::StockState& state_for(const contracts::IngredientDto& ing);

    std::vector<contracts::IngredientDto> ingredients_;
    std::map<std::string, domain::StockState> states_;
    std::vector<contracts::StockMovementDto> movements_;
    std::vector<contracts::PurchaseEntryRecordDto> purchases_;
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
    contracts::Result<contracts::CheckoutPreviewDto> preview_checkout(contracts::PaymentMethod payment_method) override;

    // B18: el mock comparte la configuración del cargo por tarjeta con MockPaymentSettingsService.
    void set_card_surcharge(std::int64_t percent_hundredths, std::int64_t fixed_cents) { surcharge_bp_ = percent_hundredths; surcharge_fixed_ = fixed_cents; }

private:
    std::int64_t next_folio_{1001};
    std::int64_t surcharge_bp_{0};
    std::int64_t surcharge_fixed_{0};
};

// B18: configuración del cargo por tarjeta (simulada en memoria; 0 % y $0 por omisión).
class MockPaymentSettingsService : public contracts::IPaymentSettingsService {
public:
    contracts::Result<contracts::CardSurchargeConfigDto> get_card_surcharge_config() override;
    contracts::OperationResult set_card_surcharge_config(std::int64_t percent_hundredths, contracts::Money fixed) override;
    contracts::Result<contracts::Money> preview_card_surcharge(std::int64_t percent_hundredths, contracts::Money fixed, contracts::Money sale_amount) override;

private:
    contracts::CardSurchargeConfigDto config_;
};

// B18: reembolsos simulados sobre una venta de demostración (V-20260924-1001). Mismas reglas de importe y mismos
// mensajes que el servicio real; no mueve inventario ni caja.
class MockRefundService : public contracts::IRefundService {
public:
    MockRefundService();
    contracts::Result<contracts::SaleDetailDto> get_sale_for_refund(const std::string& folio) override;
    contracts::Result<contracts::RefundDto> refund_sale(const contracts::RefundRequestDto& request) override;
    contracts::Result<contracts::RefundPreviewDto> preview_refund(const contracts::RefundRequestDto& request) override;
    contracts::Result<std::vector<contracts::RefundDto>> list_refunds(const std::string& sale_folio) override;
    contracts::Result<contracts::RefundDto> get_refund(const std::string& refund_id) override;

private:
    contracts::Result<contracts::RefundDto> run(const contracts::RefundRequestDto& request, bool dry_run, std::string* reason_error);
    contracts::SaleDetailDto sale_;
    std::vector<contracts::RefundDto> refunds_;
};

class MockReportService : public contracts::IReportService {
public:
    MockReportService();
    contracts::Result<contracts::DailySalesReportDto> get_daily_sales_summary(const std::string& date_iso) override;
    contracts::Result<contracts::SalesByDayReportDto> sales_by_day(const std::string& from_date, const std::string& to_date) override;
    contracts::Result<contracts::SalesByProductReportDto> sales_by_product(const std::string& from_date, const std::string& to_date) override;
    contracts::Result<contracts::SalesByPaymentReportDto> sales_by_payment_method(const std::string& from_date, const std::string& to_date) override;
    contracts::Result<contracts::SalesByCashierReportDto> sales_by_cashier(const std::string& from_date, const std::string& to_date) override;
    contracts::Result<contracts::InventoryLossReportDto> inventory_losses(const std::string& from_date, const std::string& to_date) override;
    contracts::Result<contracts::InventoryMovementsReportDto> inventory_movements(const std::string& from_date, const std::string& to_date, int limit = 1000) override;
    contracts::Result<contracts::PurchasesSummaryReportDto> purchases_summary(const std::string& from_date, const std::string& to_date) override;
    contracts::Result<contracts::ReportExportResultDto> export_report_csv(contracts::ReportKind kind, const std::string& from_date,
        const std::string& to_date, const std::string& file_path, bool overwrite = false) override;
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
    contracts::Result<contracts::BackupRetentionPolicyDto> get_retention_policy() override;
    contracts::OperationResult set_retention_policy(const contracts::BackupRetentionPolicyDto& policy) override;
    contracts::Result<contracts::BackupCleanupResultDto> preview_backup_cleanup() override;
    contracts::Result<contracts::BackupCleanupResultDto> cleanup_old_backups() override;

private:
    contracts::BackupRetentionPolicyDto policy_;
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

// Promociones y combos simulados (solo para desarrollar la interfaz): en memoria, sin reglas de descuento.
class MockPromotionService : public contracts::IPromotionService {
public:
    contracts::Result<std::vector<contracts::PromotionDto>> list_promotions(const contracts::PromotionFilterDto& filter) override;
    contracts::Result<contracts::PromotionDto> get_promotion(const std::string& promotion_id) override;
    contracts::Result<contracts::PromotionDto> create_promotion(const contracts::CreatePromotionDto& input) override;
    contracts::Result<contracts::PromotionDto> update_promotion(const contracts::UpdatePromotionDto& input) override;
    contracts::OperationResult set_promotion_active(const std::string& promotion_id, bool active) override;
    contracts::Result<std::vector<contracts::ComboDto>> list_combos(bool include_inactive) override;
    contracts::Result<contracts::ComboDto> get_combo(const std::string& product_id) override;
    contracts::Result<contracts::ComboDto> create_combo(const contracts::CreateComboDto& input) override;
    contracts::Result<contracts::ComboDto> update_combo(const contracts::UpdateComboDto& input) override;
    contracts::Result<contracts::ComboCostingDto> get_combo_costing(const std::string& product_id) override;

private:
    std::vector<contracts::PromotionDto> promotions_;
    std::vector<contracts::ComboDto> combos_;
    int next_id_{1};
};

// Servicio de actualizaciones simulado (solo para desarrollar la interfaz): nunca toca la red.
class MockUpdateService : public contracts::IUpdateService {
public:
    contracts::Result<contracts::UpdateStatusDto> get_update_status() override;
    contracts::OperationResult check_for_updates() override;
    contracts::OperationResult accept_update() override;
    contracts::OperationResult decline_update() override;
    contracts::OperationResult cancel_download() override;
    contracts::OperationResult use_update_from_files(const std::string&, const std::string&, const std::string&) override;
    contracts::Result<contracts::UpdateFromFilesResultDto> apply_update_from_files(const std::string&, const std::string&, const std::string&) override;
    contracts::OperationResult postpone_install_this_session() override;
    contracts::Result<contracts::UpdateInstallResultDto> get_last_install_result() override;
    contracts::OperationResult acknowledge_install_result() override;
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
    std::shared_ptr<contracts::IUpdateService> update_service;  // B11: actualizaciones del programa
    std::shared_ptr<contracts::IPromotionService> promotion_service;  // B14: promociones y combos
    std::shared_ptr<contracts::IRefundService> refund_service;  // B18: reembolsos de ventas
    std::shared_ptr<contracts::IPaymentSettingsService> payment_settings_service;  // B18: cargo por pago con tarjeta

    static ServiceRegistry create_mock_registry();
    // app_version: versión del programa en ejecución (project(VERSION)); is_portable: copia con portable.flag.
    static ServiceRegistry create_sqlite_registry(storage::SqliteDatabase& db,
                                                   const std::string& config_dir = "",
                                                   const std::string& migrations_dir = "",
                                                   const std::string& app_version = "0.0.0",
                                                   bool is_portable = false);
};

} // namespace app::services

#endif // APP_SERVICES_MOCK_SERVICES_HPP
