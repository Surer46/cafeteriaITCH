#ifndef APP_SERVICES_SQLITE_SALE_SERVICE_HPP
#define APP_SERVICES_SQLITE_SALE_SERVICE_HPP

#include <memory>
#include <string>
#include <mutex>
#include <unordered_set>
#include "app/contracts/sale_service.hpp"
#include "app/contracts/account_service.hpp"
#include "app/contracts/cart_service.hpp"
#include "app/contracts/shift_service.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

class SqliteSaleService : public contracts::ISaleService {
public:
    SqliteSaleService(
        storage::SqliteDatabase& db,
        std::shared_ptr<contracts::IAccountService> account_service,
        std::shared_ptr<contracts::ICartService> cart_service,
        std::shared_ptr<contracts::IShiftService> shift_service
    );

    contracts::Result<contracts::SaleConfirmationDto> confirm_sale(
        contracts::PaymentMethod payment_method,
        contracts::Money cash_received,
        const std::string& idempotency_key = ""
    ) override;

    contracts::Result<contracts::SaleConfirmationDto> get_sale_by_folio(const std::string& folio) override;
    contracts::Result<contracts::TicketDto> get_ticket(const std::string& folio) override;
    contracts::Result<std::vector<contracts::TicketSummaryDto>> get_recent_tickets(int limit = 50) override;

    // Descarta el estado en memoria derivado de ventas (p. ej. tras restaurar la base).
    void invalidate_session_state();

private:
    contracts::OperationResult check_permission_or_fail(std::string_view perm) const;
    std::string generate_next_folio();
    contracts::Result<contracts::SaleConfirmationDto> find_sale_by_idempotency_key(const std::string& key);

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
    std::shared_ptr<contracts::ICartService> cart_service_;
    std::shared_ptr<contracts::IShiftService> shift_service_;

    mutable std::mutex in_flight_mutex_;
    std::unordered_set<std::string> in_flight_keys_;

    mutable std::mutex last_confirmed_mutex_;
    std::string last_confirmed_folio_;
    std::string last_confirmed_user_id_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_SALE_SERVICE_HPP
