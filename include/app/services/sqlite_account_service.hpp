#ifndef APP_SERVICES_SQLITE_ACCOUNT_SERVICE_HPP
#define APP_SERVICES_SQLITE_ACCOUNT_SERVICE_HPP

#include <chrono>
#include <functional>
#include <memory>
#include <optional>
#include "app/contracts/account_service.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

// Límite de intentos de inicio de sesión: tras `max_failures` fallos seguidos de un mismo usuario
// (el nombre tal como se escribe: el inicio de sesión distingue mayúsculas), ese nombre queda
// bloqueado `lock_minutes` minutos. El bloqueo siempre expira solo.
struct LoginLockoutPolicy {
    int max_failures{5};
    int lock_minutes{5};
};

class SqliteAccountService : public contracts::IAccountService {
public:
    explicit SqliteAccountService(storage::SqliteDatabase& db);

    contracts::Result<bool> has_owner() override;
    contracts::OperationResult create_initial_owner(
        const std::string& username,
        const std::string& display_name,
        const std::string& raw_password,
        const std::string& email,
        const std::optional<std::vector<std::uint8_t>>& avatar_bytes = std::nullopt
    ) override;

    contracts::Result<contracts::UserDto> login(
        const std::string& username,
        const std::string& raw_password
    ) override;

    contracts::OperationResult logout() override;
    contracts::Result<contracts::UserDto> current_user() const override;
    bool is_authenticated() const override;
    bool has_permission(std::string_view permission_key) const override;

    // --- Gestión de cuentas de usuario ---
    contracts::Result<contracts::UserDto> create_user(const contracts::CreateUserDto& input) override;
    contracts::OperationResult update_user(const contracts::UpdateUserDto& input) override;
    contracts::OperationResult change_password(
        const std::string& user_id,
        const std::string& new_raw_password
    ) override;
    contracts::Result<bool> can_deactivate_user(const std::string& user_id) override;
    contracts::OperationResult deactivate_user(const std::string& user_id) override;
    contracts::Result<contracts::UserDto> get_user_by_id(const std::string& user_id) override;
    contracts::Result<std::vector<contracts::UserDto>> list_users() override;
    contracts::Result<std::vector<std::uint8_t>> get_user_avatar(const std::string& user_id) override;

    // --- Gestión de roles y permisos ---
    contracts::Result<std::vector<contracts::RoleDto>> list_roles() override;
    contracts::Result<contracts::RoleDto> get_role_by_id(const std::string& role_id) override;
    contracts::Result<contracts::RoleDto> create_role(const contracts::CreateRoleDto& input) override;
    contracts::OperationResult update_role(const contracts::UpdateRoleDto& input) override;
    contracts::OperationResult delete_role(const std::string& role_id) override;
    contracts::Result<std::vector<contracts::PermissionDto>> list_permissions() override;

    // --- Bitácora y auditoría ---
    contracts::Result<std::vector<contracts::AuditEventDto>> list_audit_events(const contracts::AuditFilterDto& filter) override;
    contracts::OperationResult record_audit_event(
        const std::string& event_type,
        const std::string& details
    ) override;

    // Política y reloj del límite de intentos (el reloj es inyectable para pruebas; por omisión,
    // la hora del sistema en UTC). No forma parte del contrato.
    void configure_login_lockout(
        const LoginLockoutPolicy& policy,
        std::function<std::chrono::system_clock::time_point()> clock = {});

    // Método para cambiar sesión en pruebas o flujos autorizados
    void set_current_user_for_testing(const contracts::UserDto& user);

private:
    contracts::OperationResult check_permission_or_fail(std::string_view permission_key) const;
    contracts::Result<std::vector<std::string>> load_role_permissions(const std::string& role_id);

    // Escritura interna de auditoría: no exige sesión (el inicio de sesión fallido la necesita).
    contracts::OperationResult write_audit_event(const std::string& event_type, const std::string& details);

    std::int64_t lockout_now_ms() const;
    // Milisegundos que faltan para que expire el bloqueo de `key`; 0 si no está bloqueado.
    std::int64_t login_lock_remaining_ms(const std::string& key, std::int64_t now_ms);
    void record_login_failure(const std::string& key, const std::string& username, std::int64_t now_ms);
    void clear_login_failures(const std::string& key);

    storage::SqliteDatabase& db_;
    LoginLockoutPolicy lockout_policy_;
    std::function<std::chrono::system_clock::time_point()> lockout_clock_;
    std::optional<contracts::UserDto> current_user_;
    std::vector<std::string> current_user_permissions_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_ACCOUNT_SERVICE_HPP
