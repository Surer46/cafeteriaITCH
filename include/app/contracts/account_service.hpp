#ifndef APP_CONTRACTS_ACCOUNT_SERVICE_HPP
#define APP_CONTRACTS_ACCOUNT_SERVICE_HPP

#include <string>
#include <vector>
#include <string_view>
#include "app/contracts/result.hpp"
#include "app/contracts/dto.hpp"

namespace app::contracts {

class IAccountService {
public:
    virtual ~IAccountService() = default;

    virtual Result<bool> has_owner() = 0;
    virtual OperationResult create_initial_owner(
        const std::string& username,
        const std::string& display_name,
        const std::string& raw_password,
        const std::string& email,
        const std::optional<std::vector<std::uint8_t>>& avatar_bytes = std::nullopt
    ) = 0;

    virtual Result<UserDto> login(
        const std::string& username,
        const std::string& raw_password
    ) = 0;

    // --- Sesión y permisos ---
    virtual OperationResult logout() = 0;
    virtual Result<UserDto> current_user() const = 0;
    virtual bool is_authenticated() const = 0;
    virtual bool has_permission(std::string_view permission_key) const = 0;

    // --- Gestión de cuentas de usuario ---
    virtual Result<UserDto> create_user(const CreateUserDto& input) = 0;
    virtual OperationResult update_user(const UpdateUserDto& input) = 0;
    virtual OperationResult change_password(
        const std::string& user_id,
        const std::string& new_raw_password
    ) = 0;
    virtual Result<bool> can_deactivate_user(const std::string& user_id) = 0;
    virtual OperationResult deactivate_user(const std::string& user_id) = 0;
    virtual Result<UserDto> get_user_by_id(const std::string& user_id) = 0;
    virtual Result<std::vector<UserDto>> list_users() = 0;
    virtual Result<std::vector<std::uint8_t>> get_user_avatar(const std::string& user_id) = 0;

    // --- Gestión de roles y permisos ---
    virtual Result<std::vector<RoleDto>> list_roles() = 0;
    virtual Result<RoleDto> get_role_by_id(const std::string& role_id) = 0;
    virtual Result<RoleDto> create_role(const CreateRoleDto& input) = 0;
    virtual OperationResult update_role(const UpdateRoleDto& input) = 0;
    virtual OperationResult delete_role(const std::string& role_id) = 0;
    virtual Result<std::vector<PermissionDto>> list_permissions() = 0;

    // --- Bitácora y auditoría ---
    virtual Result<std::vector<AuditEventDto>> list_audit_events(const AuditFilterDto& filter) = 0;
    virtual OperationResult record_audit_event(
        const std::string& event_type,
        const std::string& details
    ) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_ACCOUNT_SERVICE_HPP
