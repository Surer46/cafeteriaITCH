#ifndef APP_CONTRACTS_REPORT_SERVICE_HPP
#define APP_CONTRACTS_REPORT_SERVICE_HPP

#include <string>
#include <vector>
#include "app/contracts/result.hpp"
#include "app/contracts/money.hpp"

namespace app::contracts {

struct DailySalesReportDto {
    std::string date;
    std::int64_t total_sales_count{0};
    Money total_amount{Money::zero()};
    Money cash_amount{Money::zero()};
    Money card_amount{Money::zero()};
    Money transfer_amount{Money::zero()};
};

class IReportService {
public:
    virtual ~IReportService() = default;

    virtual Result<DailySalesReportDto> get_daily_sales_summary(const std::string& date_iso) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_REPORT_SERVICE_HPP
