#ifndef APP_SERVICES_SQLITE_REFUND_SERVICE_HPP
#define APP_SERVICES_SQLITE_REFUND_SERVICE_HPP

#include <memory>
#include <string>
#include <vector>
#include "app/contracts/refund_service.hpp"
#include "app/contracts/account_service.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

class SqliteRefundService : public contracts::IRefundService {
public:
    SqliteRefundService(storage::SqliteDatabase& db, std::shared_ptr<contracts::IAccountService> account_service);

    contracts::Result<contracts::SaleDetailDto> get_sale_for_refund(const std::string& folio) override;
    contracts::Result<contracts::RefundDto> refund_sale(const contracts::RefundRequestDto& request) override;
    contracts::Result<std::vector<contracts::RefundDto>> list_refunds(const std::string& sale_folio) override;
    contracts::Result<contracts::RefundDto> get_refund(const std::string& refund_id) override;

private:
    struct SaleRow;
    struct LineRow;
    bool load_refund(const std::string& refund_id, contracts::RefundDto& out);
    bool load_refunds_of_sale(const std::string& sale_id, std::vector<contracts::RefundDto>& out);

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_REFUND_SERVICE_HPP
