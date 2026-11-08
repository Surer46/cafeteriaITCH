#ifndef APP_SERVICES_SQLITE_PRINT_CONFIG_SERVICE_HPP
#define APP_SERVICES_SQLITE_PRINT_CONFIG_SERVICE_HPP

#include <memory>
#include <string>
#include <filesystem>
#include "app/contracts/print_config_service.hpp"
#include "app/contracts/account_service.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

class SqlitePrintConfigService : public contracts::IPrintConfigService {
public:
    SqlitePrintConfigService(
        storage::SqliteDatabase& db,
        std::shared_ptr<contracts::IAccountService> account_service,
        std::string config_dir = ""
    );

    contracts::Result<contracts::PrinterStationConfig> get_station_config() override;
    contracts::OperationResult set_station_config(const contracts::PrinterStationConfig& config) override;

private:
    void load_from_disk();
    bool save_to_disk();
    std::filesystem::path get_config_file_path() const;

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
    std::string config_dir_;
    contracts::PrinterStationConfig config_;
    bool loaded_{false};
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_PRINT_CONFIG_SERVICE_HPP
