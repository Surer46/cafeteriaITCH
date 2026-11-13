#ifndef APP_CONTRACTS_INVENTORY_SERVICE_HPP
#define APP_CONTRACTS_INVENTORY_SERVICE_HPP

#include <string>
#include <vector>
#include <cstdint>
#include "app/contracts/result.hpp"
#include "app/contracts/money.hpp"
#include "app/contracts/dto.hpp"

namespace app::contracts {

class IInventoryService {
public:
    virtual ~IInventoryService() = default;

    virtual Result<IngredientDto> create_ingredient(const CreateIngredientDto& input) = 0;
    virtual OperationResult update_ingredient(const UpdateIngredientDto& input) = 0;
    virtual Result<std::vector<IngredientDto>> list_ingredients() = 0;
    virtual Result<IngredientDto> get_ingredient(const std::string& ingredient_id) = 0;
    virtual Result<std::vector<StockAlertDto>> get_stock_alerts() = 0;
    virtual OperationResult update_low_stock_threshold(
        const std::string& ingredient_id,
        std::int64_t new_threshold_base
    ) = 0;

    virtual OperationResult register_purchase_entry(
        const std::string& ingredient_id,
        std::int64_t quantity_purchased,
        const std::string& purchase_unit,
        Money total_cost
    ) = 0;

    // Merma: resta existencias. quantity > 0 en `unit` (se convierte a la unidad base).
    // Requiere inventory.adjust. Motivo obligatorio (máx. 200 caracteres).
    // Rechaza cantidades 0, desbordamientos y resultados negativos.
    virtual OperationResult register_waste(
        const std::string& ingredient_id,
        std::int64_t quantity,
        const std::string& unit,
        const std::string& reason
    ) = 0;

    // Ajuste: suma (signed_quantity > 0) o resta (< 0) existencias. Mismas reglas que la merma.
    virtual OperationResult register_adjustment(
        const std::string& ingredient_id,
        std::int64_t signed_quantity,
        const std::string& unit,
        const std::string& reason
    ) = 0;

    // Costo promedio móvil de un ingrediente y costo de `quantity_base_units` unidades.
    // Requiere inventory.manage_products (Forbidden en otro caso). Sin compras devuelve known=false.
    virtual Result<IngredientCostDto> get_ingredient_cost(
        const std::string& ingredient_id,
        std::int64_t quantity_base_units
    ) = 0;

    // Movimientos de existencias, más recientes primero. Requiere inventory.view.
    virtual Result<std::vector<StockMovementDto>> list_stock_movements(const StockMovementFilterDto& filter) = 0;

    // Entradas de compra con su costo y quién las registró, más recientes primero.
    // Requiere inventory.restock o inventory.manage_products.
    virtual Result<std::vector<PurchaseEntryRecordDto>> list_purchase_entries(const PurchaseEntryFilterDto& filter) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_INVENTORY_SERVICE_HPP
