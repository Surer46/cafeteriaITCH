#ifndef APP_SERVICES_SQLITE_REPORT_SERVICE_HPP
#define APP_SERVICES_SQLITE_REPORT_SERVICE_HPP

#include <cstdint>
#include <memory>
#include <string>
#include "app/contracts/report_service.hpp"
#include "app/contracts/account_service.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

class SqliteReportService : public contracts::IReportService {
public:
    SqliteReportService(
        storage::SqliteDatabase& db,
        std::shared_ptr<contracts::IAccountService> account_service
    );

    // Requiere reports.view. date_iso: YYYY-MM-DD válido.
    contracts::Result<contracts::DailySalesReportDto> get_daily_sales_summary(const std::string& date_iso) override;

    // B15: reportes por rango (requieren reports.view) y exportación a CSV.
    contracts::Result<contracts::SalesByDayReportDto> sales_by_day(const std::string& from_date, const std::string& to_date) override;
    contracts::Result<contracts::SalesByProductReportDto> sales_by_product(const std::string& from_date, const std::string& to_date) override;
    contracts::Result<contracts::SalesByPaymentReportDto> sales_by_payment_method(const std::string& from_date, const std::string& to_date) override;
    contracts::Result<contracts::SalesByCashierReportDto> sales_by_cashier(const std::string& from_date, const std::string& to_date) override;
    contracts::Result<contracts::InventoryLossReportDto> inventory_losses(const std::string& from_date, const std::string& to_date) override;
    contracts::Result<contracts::InventoryMovementsReportDto> inventory_movements(const std::string& from_date, const std::string& to_date, int limit = 1000) override;
    contracts::Result<contracts::PurchasesSummaryReportDto> purchases_summary(const std::string& from_date, const std::string& to_date) override;
    contracts::Result<contracts::ReportExportResultDto> export_report_csv(contracts::ReportKind kind, const std::string& from_date,
        const std::string& to_date, const std::string& file_path, bool overwrite = false) override;

private:
    std::string header_totals(const std::string& start, const std::string& end, contracts::SalesTotalsDto& t);
    std::string payment_totals(const std::string& start, const std::string& end, contracts::SalesTotalsDto& t);
    static std::string set_payments(std::int64_t cash, std::int64_t card, std::int64_t transfer, contracts::SalesTotalsDto& t);
    contracts::Result<contracts::InventoryMovementsReportDto> movements_impl(const std::string& from_date, const std::string& to_date, std::int64_t limit);
    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_REPORT_SERVICE_HPP
