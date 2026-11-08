#ifndef APP_SERVICES_SQLITE_REPORT_SERVICE_HPP
#define APP_SERVICES_SQLITE_REPORT_SERVICE_HPP

#include <memory>
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

private:
    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_REPORT_SERVICE_HPP
