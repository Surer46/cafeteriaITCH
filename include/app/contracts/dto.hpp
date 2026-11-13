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

struct BackupStatusDto {
    bool is_enabled{false};
    std::string last_backup_time;
    std::string last_status;
    std::string last_error;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_DTO_HPP
