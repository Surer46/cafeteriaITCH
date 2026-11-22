#ifndef APP_SERVICES_SQLITE_PAYMENT_SETTINGS_SERVICE_HPP
#define APP_SERVICES_SQLITE_PAYMENT_SETTINGS_SERVICE_HPP

#include <memory>
#include "app/contracts/payment_settings_service.hpp"
#include "app/contracts/account_service.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

// Configuración del cargo por tarjeta (B18), guardada en SQLite (una fila, para toda la cafetería).
class SqlitePaymentSettingsService : public contracts::IPaymentSettingsService {
public:
    SqlitePaymentSettingsService(storage::SqliteDatabase& db, std::shared_ptr<contracts::IAccountService> account_service);

    contracts::Result<contracts::CardSurchargeConfigDto> get_card_surcharge_config() override;
    contracts::OperationResult set_card_surcharge_config(std::int64_t percent_hundredths, contracts::Money fixed) override;

private:
    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
};

// Lectura sin comprobar permisos: la usa la venta DENTRO de su transacción (el permiso ya se comprobó). Si la fila
// no existe o la base es anterior a la migración 022, devuelve 0 % y $0 (desactivado).
contracts::CardSurchargeConfigDto read_card_surcharge_config(storage::SqliteDatabase& db);

} // namespace app::services

#endif // APP_SERVICES_SQLITE_PAYMENT_SETTINGS_SERVICE_HPP
