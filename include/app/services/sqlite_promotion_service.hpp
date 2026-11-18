#ifndef APP_SERVICES_SQLITE_PROMOTION_SERVICE_HPP
#define APP_SERVICES_SQLITE_PROMOTION_SERVICE_HPP

#include <memory>
#include <string>
#include <vector>
#include "app/contracts/account_service.hpp"
#include "app/contracts/catalog_service.hpp"
#include "app/contracts/promotion_service.hpp"
#include "app/services/promotion_engine.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

class SqlitePromotionService : public contracts::IPromotionService {
public:
    SqlitePromotionService(
        storage::SqliteDatabase& db,
        std::shared_ptr<contracts::IAccountService> account_service,
        std::shared_ptr<contracts::ICatalogService> catalog_service,
        LocalClock clock = {}   // hora local para is_valid_now; por omisión la del sistema
    );

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
    struct Row;  // promoción tal como está guardada
    contracts::OperationResult check_permission_or_fail(std::string_view perm) const;
    contracts::OperationResult validate(const contracts::CreatePromotionDto& in) const;
    bool load_row(const std::string& promotion_id, Row& out, std::string& error);
    contracts::PromotionDto to_dto(const Row& row) const;
    // Conflict (mensaje llano que nombra la otra promoción) si `candidate` choca con otra activa de igual prioridad.
    contracts::OperationResult check_ambiguity(const Row& candidate);
    contracts::OperationResult write_targets(const std::string& promotion_id, const Row& row);
    contracts::Result<contracts::ComboDto> read_combo(const std::string& product_id);
    contracts::OperationResult validate_components(const std::vector<contracts::ComboComponentDto>& components,
                                                   const std::string& combo_id_or_empty) const;
    LocalNow now() const { return clock_ ? clock_() : system_local_now(); }

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
    std::shared_ptr<contracts::ICatalogService> catalog_service_;
    LocalClock clock_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_PROMOTION_SERVICE_HPP
