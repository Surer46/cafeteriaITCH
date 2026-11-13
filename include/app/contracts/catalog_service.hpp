#ifndef APP_CONTRACTS_CATALOG_SERVICE_HPP
#define APP_CONTRACTS_CATALOG_SERVICE_HPP

#include <string>
#include <vector>
#include <optional>
#include "app/contracts/result.hpp"
#include "app/contracts/dto.hpp"

namespace app::contracts {

class ICatalogService {
public:
    virtual ~ICatalogService() = default;

    virtual Result<std::vector<ProductDto>> search_products(
        const std::string& query,
        const std::optional<std::string>& category,
        std::size_t offset,
        std::size_t limit,
        bool include_inactive = false
    ) = 0;

    virtual Result<ProductDto> get_product_by_id(const std::string& product_id) = 0;
    virtual Result<ProductDto> get_product_by_sku_or_barcode(const std::string& code) = 0;
    virtual Result<std::vector<std::string>> list_categories() = 0;

    // Gestión de productos (CRUD y baja lógica)
    virtual Result<ProductDto> create_product(const CreateProductDto& input) = 0;
    virtual OperationResult update_product(const UpdateProductDto& input) = 0;
    virtual OperationResult deactivate_product(const std::string& product_id) = 0;
    virtual OperationResult reactivate_product(const std::string& product_id) = 0;

    // Imágenes opcionales
    virtual OperationResult set_product_image(
        const std::string& product_id,
        const std::string& mime_type,
        const std::vector<std::uint8_t>& image_bytes
    ) = 0;
    virtual Result<ProductImageDto> get_product_image(const std::string& product_id) = 0;
    virtual OperationResult remove_product_image(const std::string& product_id) = 0;

    // Recetas de platillos y bebidas preparados
    virtual Result<RecipeDto> get_recipe(const std::string& product_id) = 0;
    // Costo por porción y margen del producto con el promedio móvil de cada ingrediente.
    // Requiere inventory.manage_products (Forbidden en otro caso). Un ingrediente sin compras
    // es de costo desconocido: la receta lo lista y no presenta margen.
    virtual Result<RecipeCostingDto> get_recipe_costing(const std::string& product_id) = 0;
    virtual OperationResult set_recipe(
        const std::string& product_id,
        const std::vector<RecipeItemDto>& items
    ) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_CATALOG_SERVICE_HPP
