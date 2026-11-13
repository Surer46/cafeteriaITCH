#ifndef APP_CONTRACTS_DTO_HPP
#define APP_CONTRACTS_DTO_HPP

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include "app/contracts/money.hpp"

namespace app::contracts {

struct PermissionDto {
    std::string id;
    std::string description;
    std::string module;
    std::string title;
    std::string section;
    int sort_order{0};
};

struct CreateRoleDto {
    std::string name;
    std::string description;
    std::vector<std::string> permissions;
};

struct UpdateRoleDto {
    std::string role_id;
    std::string name;
    std::string description;
    std::vector<std::string> permissions;
};

struct CreateUserDto {
    std::string username;
    std::string display_name;
    std::string raw_password;
    std::string role_id;
    std::string email;
    std::optional<std::vector<std::uint8_t>> avatar_bytes;
};

struct UpdateUserDto {
    std::string user_id;
    std::optional<std::string> display_name;
    std::optional<std::string> email;
    std::optional<std::string> role_id;
    std::optional<bool> is_active;
    std::optional<std::vector<std::uint8_t>> avatar_bytes;
    std::optional<bool> remove_avatar;
};

struct AuditEventDto {
    std::string id;
    std::string event_type;
    std::string user_id;
    std::string username;
    std::string details;
    std::string created_at;
};

struct AuditFilterDto {
    std::optional<std::string> user_id;
    std::optional<std::string> event_type;
    std::optional<std::string> start_date;
    std::optional<std::string> end_date;
    std::size_t limit{100};
    std::size_t offset{0};
};

struct ProductDto {
    std::string id;
    std::string sku;
    std::string barcode;
    std::string name;
    std::string category;
    Money price;
    std::int64_t stock_units{0};
    bool is_prepared{false};
    bool is_active{true};
    bool has_image{false};
};

struct CreateProductDto {
    std::string sku;
    std::string barcode;
    std::string name;
    std::string category;
    Money price{Money::zero()};
    bool is_prepared{false};
};

struct UpdateProductDto {
    std::string product_id;
    std::optional<std::string> sku;
    std::optional<std::string> barcode;
    std::optional<std::string> name;
    std::optional<std::string> category;
    std::optional<Money> price;
    std::optional<bool> is_prepared;
    std::optional<bool> is_active;
};

struct ProductImageDto {
    std::string product_id;
    std::string mime_type;
    std::vector<std::uint8_t> image_blob;
    std::vector<std::uint8_t> thumbnail_blob;
};

struct RecipeItemDto {
    std::string ingredient_id;
    std::string ingredient_name;
    std::int64_t quantity_base_units{0};
    std::string base_unit;
};

struct RecipeDto {
    std::string recipe_id;
    std::string product_id;
    int version{1};
    std::vector<RecipeItemDto> items;
    // OBSOLETO: se conserva solo por compatibilidad. Usa la última compra de cada ingrediente, redondea
    // por ingrediente y cuenta un costo desconocido como 0. Vale 0 para quien no tiene
    // inventory.manage_products. Las pantallas nuevas deben usar ICatalogService::get_recipe_costing.
    Money estimated_cost_per_portion{Money::zero()};
};

struct IngredientDto {
    std::string id;
    std::string name;
    std::string unit_dimension; // MASS, VOLUME, COUNT
    std::string base_unit;      // mg, ml, pz
    std::int64_t current_stock_base{0};
    std::int64_t low_stock_threshold_base{0};
};

struct CreateIngredientDto {
    std::string name;
    std::string unit_dimension; // "MASS", "VOLUME", "COUNT"
    std::string base_unit;      // "mg", "ml", "pz"
    std::int64_t low_stock_threshold_base{0};
};

struct UpdateIngredientDto {
    std::string ingredient_id;
    std::optional<std::string> name;
    std::optional<std::int64_t> low_stock_threshold_base;
};

struct CartItemDto {
    std::string line_id;
    std::string product_id;
    std::string sku;
    std::string name;
    Money unit_price;
    std::int64_t quantity{1};
    Money subtotal;
    std::string notes;
};

struct CartSummaryDto {
    std::vector<CartItemDto> items;
    std::int64_t total_items{0};
    Money subtotal{Money::zero()};
    Money discount{Money::zero()};
    Money total{Money::zero()};
};

struct RoleDto {
    std::string id;
    std::string name;
    std::string description;
    bool is_system{false};
    std::vector<std::string> permissions;
};

struct UserDto {
    std::string id;
    std::string username;
    std::string display_name;
    std::string email;
    std::string role_id;
    std::string role_name;
    bool is_active{true};
    bool is_owner{false};
    bool has_avatar{false};
};

struct ShiftDto {
    std::string id;
    std::string register_id;
    std::string register_name;
    std::string cashier_user_id;
    std::string cashier_name;
    std::string opened_at;
    std::string closed_at;
    Money initial_cash{Money::zero()};
    bool is_open{true};
};

struct StockAlertDto {
    std::string ingredient_id;
    std::string name;
    std::int64_t current_stock_base{0};
    std::int64_t threshold_base{0};
    std::string base_unit;
};

struct PurchaseEntryDto {
    std::string id;
    std::string ingredient_id;
    std::int64_t quantity_purchased{0};
    std::string purchase_unit;
    Money total_cost{Money::zero()};
    Money unit_cost_base{Money::zero()};
    std::string created_at;
};

// Origen del costo de un ingrediente.
enum class CostBasis {
    Unknown,        // sin compras: el costo es DESCONOCIDO (no es 0)
    CurrentAverage, // promedio móvil de las existencias actuales
    LastKnown       // existencia 0: se usa el último promedio conocido
};

// Costo de un ingrediente. Solo lo devuelven consultas que exigen inventory.manage_products.
struct IngredientCostDto {
    std::string ingredient_id;
    bool known{false};
    CostBasis basis{CostBasis::Unknown};
    std::int64_t quantity_base_units{0};   // cantidad consultada
    Money cost_for_quantity{Money::zero()};  // redondeado a centavos una sola vez; sin significado si !known
    std::int64_t cost_for_quantity_micro{0}; // mismo costo en micro-centavos (1 centavo = 1 000 000)
    std::int64_t stock_base_units{0};
    Money stock_value{Money::zero()};        // valor de las existencias; 0 si !known
};

struct RecipeCostLineDto {
    std::string ingredient_id;
    std::string ingredient_name;
    std::int64_t quantity_base_units{0};
    std::string base_unit;
    CostBasis basis{CostBasis::Unknown};
    Money cost{Money::zero()};          // solo para mostrar; el total se redondea una sola vez
    std::int64_t cost_micro{0};
};

// Costo estimado y margen de un producto con receta. Solo para inventory.manage_products.
struct RecipeCostingDto {
    std::string product_id;
    std::string recipe_id;
    int version{1};
    Money price{Money::zero()};
    std::vector<RecipeCostLineDto> lines;
    bool all_costs_known{false};          // false si algún ingrediente tiene costo desconocido
    bool uses_last_known_cost{false};     // true si alguna línea usa el último costo conocido
    std::vector<std::string> unknown_ingredient_names;
    Money known_cost_subtotal{Money::zero()}; // suma de lo conocido (parcial si !all_costs_known)
    Money cost_per_portion{Money::zero()};    // válido solo si all_costs_known
    bool margin_known{false};                 // false si falta algún costo: no se presenta un margen falso
    Money margin{Money::zero()};              // precio - costo; puede ser negativo
    bool margin_percent_known{false};         // false si el precio es 0
    std::int64_t margin_basis_points{0};      // margen * 10000 / precio (1 % = 100 puntos base)
};

// Filtro de movimientos de existencias. Cadena vacía = sin filtro.
// date_from / date_to: "YYYY-MM-DD" (hora local, ambos días incluidos).
// movement_type: PURCHASE, SALE, ADJUSTMENT, WASTE o TRANSFER.
// limit <= 0 usa 100; el máximo es 500.
struct StockMovementFilterDto {
    std::string ingredient_id;
    std::string movement_type;
    std::string date_from;
    std::string date_to;
    int limit{100};
};

struct StockMovementDto {
    std::string id;
    std::string ingredient_id;
    std::string ingredient_name;
    std::string movement_type;
    std::int64_t quantity_base_units{0}; // con signo: negativo = salida
    std::string base_unit;
    std::string reason;
    std::string created_at;     // hora local
    std::string actor_user_id;  // vacío en movimientos anteriores a la migración 011
    std::string actor_name;     // nombre actual de la cuenta; vacío si no se conoce
};

// Filtro de entradas de compra. Mismas reglas que StockMovementFilterDto.
struct PurchaseEntryFilterDto {
    std::string ingredient_id;
    std::string date_from;
    std::string date_to;
    int limit{100};
};

struct PurchaseEntryRecordDto {
    std::string id;
    std::string ingredient_id;
    std::string ingredient_name;
    std::int64_t quantity_purchased{0};
    std::string purchase_unit;
    Money total_cost{Money::zero()};
    std::int64_t base_units_purchased{0};
    std::string base_unit;
    std::string created_at;
    std::string actor_user_id;
    std::string actor_name;
};

struct BackupStatusDto {
    bool is_enabled{false};
    std::string last_backup_time;
    std::string last_status;
    std::string last_error;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_DTO_HPP
