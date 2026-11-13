#include "app/services/sqlite_account_service.hpp"
#include "app/services/identifiers.hpp"
#include "app/domain/clock.hpp"
#include "app/crypto/password_hasher.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <atomic>
#include <algorithm>

namespace app::services {

namespace {

bool is_valid_email(const std::string& email) {
    if (email.empty() || email.length() > 254) return false;
    auto at_pos = email.find('@');
    if (at_pos == std::string::npos || at_pos == 0 || at_pos == email.length() - 1) return false;
    if (email.find('@', at_pos + 1) != std::string::npos) return false;
    auto dot_pos = email.find('.', at_pos + 1);
    if (dot_pos == std::string::npos || dot_pos == at_pos + 1 || dot_pos == email.length() - 1) return false;
    if (email.find(' ') != std::string::npos) return false;
    return true;
}

bool validate_avatar_bytes(const std::vector<std::uint8_t>& bytes, std::string& out_err) {
    if (bytes.size() > 100 * 1024) {
        out_err = "El archivo de avatar excede el límite de 100 KB";
        return false;
    }
    if (bytes.size() < 4) {
        out_err = "Formato de imagen de avatar inválido";
        return false;
    }
    // JPEG: 0xFF, 0xD8, 0xFF
    if (bytes.size() >= 3 && bytes[0] == 0xFF && bytes[1] == 0xD8 && bytes[2] == 0xFF) return true;
    // PNG: 0x89, 'P', 'N', 'G', 0x0D, 0x0A, 0x1A, 0x0A
    if (bytes.size() >= 8 &&
        bytes[0] == 0x89 && bytes[1] == 'P' && bytes[2] == 'N' && bytes[3] == 'G' &&
        bytes[4] == 0x0D && bytes[5] == 0x0A && bytes[6] == 0x1A && bytes[7] == 0x0A) return true;
    // WebP: RIFF ... WEBP
    if (bytes.size() >= 12 &&
        bytes[0] == 'R' && bytes[1] == 'I' && bytes[2] == 'F' && bytes[3] == 'F' &&
        bytes[8] == 'W' && bytes[9] == 'E' && bytes[10] == 'B' && bytes[11] == 'P') return true;

    out_err = "Formato de imagen no soportado. Debe ser JPEG, PNG o WebP";
    return false;
}

} // namespace

SqliteAccountService::SqliteAccountService(storage::SqliteDatabase& db)
    : db_(db) {}

contracts::Result<bool> SqliteAccountService::has_owner() {
    auto prep = db_.prepare(
        "SELECT COUNT(*) FROM users u "
        "JOIN roles r ON u.role_id = r.id "
        "WHERE (r.name = 'Dueño' OR r.id = 'role-owner') AND u.is_active = 1;"
    );
    if (!prep.is_success()) {
        return contracts::Result<bool>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    auto step_res = stmt.step();
    if (!step_res.is_success()) {
        return contracts::Result<bool>::fail(step_res.error_code(), step_res.error_message());
    }

    std::int64_t count = stmt.get_int64(0);
    return contracts::Result<bool>::ok(count > 0);
}

contracts::OperationResult SqliteAccountService::create_initial_owner(
    const std::string& username,
    const std::string& display_name,
    const std::string& raw_password,
    const std::string& email,
    const std::optional<std::vector<std::uint8_t>>& avatar_bytes
) {
    if (username.empty() || raw_password.empty() || display_name.empty() || email.empty()) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "Usuario, nombre visible, correo electrónico y contraseña son obligatorios"
        );
    }

    if (!is_valid_email(email)) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "El formato del correo electrónico es inválido"
        );
    }

    if (avatar_bytes.has_value()) {
        std::string err;
        if (!validate_avatar_bytes(*avatar_bytes, err)) {
            return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, err);
        }
    }

    // Verificar si ya existe un dueño activo
    auto owner_check = has_owner();
    if (!owner_check.is_success()) {
        return contracts::OperationResult::fail(owner_check.error_code(), owner_check.error_message());
    }
    if (owner_check.value()) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Conflict,
            "Ya existe una cuenta Dueño activa en el sistema"
        );
    }

    // Verificar si el correo ya existe
    auto prep_echk = db_.prepare("SELECT COUNT(*) FROM users WHERE email = ?;");
    if (!prep_echk.is_success()) return contracts::OperationResult::fail(prep_echk.error_code(), prep_echk.error_message());
    auto stmt_echk = std::move(prep_echk.value());
    stmt_echk.bind_text(1, email);
    stmt_echk.step();
    if (stmt_echk.get_int64(0) > 0) {
        return contracts::OperationResult::fail(contracts::ErrorCode::Conflict, "El correo electrónico ya está registrado");
    }

    // Generar hash seguro con PBKDF2-HMAC-SHA256 y sal individual
    std::string password_hash = crypto::PasswordHasher::hash_password(raw_password);
    std::string user_id = "user-" + username;
    const auto ts = domain::now();
    const std::string& now_str = ts.local;

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return tx;

    auto prep = db_.prepare(
        "INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at, avatar_blob, updated_at, updated_at_utc) "
        "VALUES (?, ?, ?, ?, ?, 'role-owner', 1, ?, ?, ?, ?);"
    );
    if (!prep.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, user_id);
    stmt.bind_text(2, username);
    stmt.bind_text(3, display_name);
    stmt.bind_text(4, email);
    stmt.bind_text(5, password_hash);
    stmt.bind_text(6, now_str);
    if (avatar_bytes.has_value()) {
        stmt.bind_blob(7, *avatar_bytes);
    } else {
        stmt.bind_null(7);
    }
    stmt.bind_text(8, now_str);
    stmt.bind_text(9, ts.utc);

    auto step_res = stmt.step();
    if (!step_res.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(step_res.error_code(), step_res.error_message());
    }

    // Registrar en auditoría
    auto prep_audit = db_.prepare(
        "INSERT INTO audit_events (id, event_type, user_id, details, created_at, created_at_utc) "
        "VALUES (?, 'OWNER_CREATED', ?, 'Alta inicial de cuenta Dueño', ?, ?);"
    );
    if (prep_audit.is_success()) {
        auto stmt_aud = std::move(prep_audit.value());
        stmt_aud.bind_text(1, generate_id("audit"));
        stmt_aud.bind_text(2, user_id);
        stmt_aud.bind_text(3, now_str);
        stmt_aud.bind_text(4, ts.utc);
        stmt_aud.step();
    }

    auto commit_res = db_.commit_transaction();
    if (!commit_res.is_success()) {
        db_.rollback_transaction();
        return commit_res;
    }

    // Auto-login de la cuenta creada
    contracts::UserDto owner_dto;
    owner_dto.id = user_id;
    owner_dto.username = username;
    owner_dto.display_name = display_name;
    owner_dto.email = email;
    owner_dto.role_id = "role-owner";
    owner_dto.role_name = "Dueño";
    owner_dto.is_active = true;
    owner_dto.is_owner = true;
    owner_dto.has_avatar = avatar_bytes.has_value();
    current_user_ = owner_dto;

    auto perms = load_role_permissions("role-owner");
    if (perms.is_success()) {
        current_user_permissions_ = perms.value();
    }

    return contracts::OperationResult::ok();
}

contracts::Result<contracts::UserDto> SqliteAccountService::login(
    const std::string& username,
    const std::string& raw_password
) {
    auto prep = db_.prepare(
        "SELECT u.id, u.username, u.display_name, u.password_hash, u.role_id, r.name, u.is_active, u.email, (u.avatar_blob IS NOT NULL) "
        "FROM users u "
        "JOIN roles r ON u.role_id = r.id "
        "WHERE u.username = ?;"
    );
    if (!prep.is_success()) {
        return contracts::Result<contracts::UserDto>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, username);
    auto step_res = stmt.step();
    if (!step_res.is_success()) {
        return contracts::Result<contracts::UserDto>::fail(step_res.error_code(), step_res.error_message());
    }
    if (!step_res.value()) {
        record_audit_event("LOGIN_FAILED", "Intento fallido: usuario no encontrado (" + username + ")");
        return contracts::Result<contracts::UserDto>::fail(
            contracts::ErrorCode::Forbidden,
            "Usuario o contraseña incorrectos"
        );
    }

    std::string id = stmt.get_text(0);
    std::string user_name = stmt.get_text(1);
    std::string display_name = stmt.get_text(2);
    std::string stored_hash = stmt.get_text(3);
    std::string role_id = stmt.get_text(4);
    std::string role_name = stmt.get_text(5);
    bool is_active = stmt.get_int64(6) != 0;
    std::string email = stmt.get_text(7);
    bool has_avatar = stmt.get_int64(8) != 0;

    if (!is_active) {
        record_audit_event("LOGIN_FAILED", "Intento de inicio de sesión con cuenta inactiva (" + username + ")");
        return contracts::Result<contracts::UserDto>::fail(
            contracts::ErrorCode::Forbidden,
            "La cuenta de usuario está desactivada"
        );
    }

    if (!crypto::PasswordHasher::verify_password(raw_password, stored_hash)) {
        record_audit_event("LOGIN_FAILED", "Contraseña incorrecta para usuario: " + username);
        return contracts::Result<contracts::UserDto>::fail(
            contracts::ErrorCode::Forbidden,
            "Usuario o contraseña incorrectos"
        );
    }

    contracts::UserDto dto;
    dto.id = id;
    dto.username = user_name;
    dto.display_name = display_name;
    dto.email = email;
    dto.role_id = role_id;
    dto.role_name = role_name;
    dto.is_active = is_active;
    dto.is_owner = (role_id == "role-owner" || role_name == "Dueño");
    dto.has_avatar = has_avatar;

    current_user_ = dto;

    auto perms = load_role_permissions(role_id);
    if (perms.is_success()) {
        current_user_permissions_ = perms.value();
    }

    record_audit_event("LOGIN_SUCCESS", "Inicio de sesión exitoso para: " + username);
    return contracts::Result<contracts::UserDto>::ok(dto);
}

contracts::OperationResult SqliteAccountService::logout() {
    if (current_user_.has_value()) {
        record_audit_event("LOGOUT", "Cierre de sesión para: " + current_user_->username);
    }
    current_user_ = std::nullopt;
    current_user_permissions_.clear();
    return contracts::OperationResult::ok();
}

contracts::Result<contracts::UserDto> SqliteAccountService::current_user() const {
    if (!current_user_.has_value()) {
        return contracts::Result<contracts::UserDto>::fail(
            contracts::ErrorCode::Forbidden,
            "No hay sesión activa"
        );
    }
    return contracts::Result<contracts::UserDto>::ok(*current_user_);
}

bool SqliteAccountService::is_authenticated() const {
    return current_user_.has_value();
}

bool SqliteAccountService::has_permission(std::string_view permission_key) const {
    if (!current_user_.has_value()) return false;

    // Regla estricta de AGENTS.md y specs.md:
    // El permiso técnico "backup.dev_config" requiere identidad Desarrollador separada.
    // NI SIQUIERA el Dueño puede tener "backup.dev_config".
    if (permission_key == "backup.dev_config") {
        return current_user_->role_id == "role-dev";
    }

    // El Dueño tiene acceso a todas las operaciones y administración del negocio
    if (current_user_->is_owner) {
        return true;
    }

    // Para cualquier otro rol, buscar en la lista de permisos asignados
    for (const auto& p : current_user_permissions_) {
        if (p == permission_key) {
            return true;
        }
    }

    return false;
}

contracts::OperationResult SqliteAccountService::check_permission_or_fail(std::string_view permission_key) const {
    if (!is_authenticated()) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Operación denegada: no hay sesión activa autenticada"
        );
    }
    if (!has_permission(permission_key)) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Operación denegada: falta de permisos (" + std::string(permission_key) + ")"
        );
    }
    return contracts::OperationResult::ok();
}

contracts::Result<std::vector<std::string>> SqliteAccountService::load_role_permissions(const std::string& role_id) {
    auto prep = db_.prepare("SELECT permission_id FROM role_permissions WHERE role_id = ?;");
    if (!prep.is_success()) {
        return contracts::Result<std::vector<std::string>>::fail(prep.error_code(), prep.error_message());
    }
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, role_id);
    std::vector<std::string> perms;
    while (true) {
        auto step = stmt.step();
        if (!step.is_success()) return contracts::Result<std::vector<std::string>>::fail(step.error_code(), step.error_message());
        if (!step.value()) break;
        perms.push_back(stmt.get_text(0));
    }
    return contracts::Result<std::vector<std::string>>::ok(perms);
}

contracts::Result<bool> SqliteAccountService::can_deactivate_user(const std::string& user_id) {
    auto prep_target = db_.prepare(
        "SELECT u.role_id, r.name, u.is_active FROM users u "
        "JOIN roles r ON u.role_id = r.id WHERE u.id = ?;"
    );
    if (!prep_target.is_success()) return contracts::Result<bool>::fail(prep_target.error_code(), prep_target.error_message());

    auto stmt_target = std::move(prep_target.value());
    stmt_target.bind_text(1, user_id);
    auto step_target = stmt_target.step();
    if (!step_target.is_success() || !step_target.value()) {
        return contracts::Result<bool>::fail(contracts::ErrorCode::NotFound, "Usuario no encontrado");
    }

    std::string role_id = stmt_target.get_text(0);
    std::string role_name = stmt_target.get_text(1);
    bool is_active = stmt_target.get_int64(2) != 0;

    if (!is_active) {
        return contracts::Result<bool>::ok(true);
    }

    bool is_owner_user = (role_id == "role-owner" || role_name == "Dueño");
    if (!is_owner_user) {
        return contracts::Result<bool>::ok(true);
    }

    // Si es Dueño, verificar cuántos otros dueños activos existen
    auto prep_count = db_.prepare(
        "SELECT COUNT(*) FROM users u "
        "JOIN roles r ON u.role_id = r.id "
        "WHERE (r.name = 'Dueño' OR r.id = 'role-owner') AND u.is_active = 1 AND u.id != ?;"
    );
    if (!prep_count.is_success()) return contracts::Result<bool>::fail(prep_count.error_code(), prep_count.error_message());

    auto stmt_count = std::move(prep_count.value());
    stmt_count.bind_text(1, user_id);
    auto step_count = stmt_count.step();
    if (!step_count.is_success() || !step_count.value()) {
        return contracts::Result<bool>::fail(contracts::ErrorCode::StorageFailure, "Error al consultar dueños");
    }

    std::int64_t remaining_owners = stmt_count.get_int64(0);
    if (remaining_owners <= 0) {
        // Es el último dueño activo: NUNCA se permite desactivar
        return contracts::Result<bool>::ok(false);
    }

    return contracts::Result<bool>::ok(true);
}

contracts::OperationResult SqliteAccountService::deactivate_user(const std::string& user_id) {
    contracts::UpdateUserDto dto;
    dto.user_id = user_id;
    dto.is_active = false;
    return update_user(dto);
}

contracts::Result<contracts::UserDto> SqliteAccountService::create_user(const contracts::CreateUserDto& input) {
    auto perm_res = check_permission_or_fail("users.manage");
    if (!perm_res.is_success()) {
        return contracts::Result<contracts::UserDto>::fail(perm_res.error_code(), perm_res.error_message());
    }

    if (input.username.empty() || input.display_name.empty() || input.raw_password.empty() || input.role_id.empty() || input.email.empty()) {
        return contracts::Result<contracts::UserDto>::fail(
            contracts::ErrorCode::InvalidInput,
            "Todos los campos de usuario son requeridos, incluido el correo electrónico"
        );
    }

    if (!is_valid_email(input.email)) {
        return contracts::Result<contracts::UserDto>::fail(
            contracts::ErrorCode::InvalidInput,
            "El formato del correo electrónico es inválido"
        );
    }

    if (input.avatar_bytes.has_value()) {
        std::string err;
        if (!validate_avatar_bytes(*input.avatar_bytes, err)) {
            return contracts::Result<contracts::UserDto>::fail(contracts::ErrorCode::InvalidInput, err);
        }
    }

    // Regla de AGENTS.md y specs.md: La identidad Desarrollador no puede ser asignada ni gestionada por usuarios normales/Dueño
    if (input.role_id == "role-dev") {
        return contracts::Result<contracts::UserDto>::fail(
            contracts::ErrorCode::Forbidden,
            "No se puede crear una cuenta con rol Desarrollador mediante la gestión regular de usuarios"
        );
    }

    // Verificar si el rol existe
    auto prep_role = db_.prepare("SELECT name FROM roles WHERE id = ?;");
    if (!prep_role.is_success()) return contracts::Result<contracts::UserDto>::fail(prep_role.error_code(), prep_role.error_message());
    auto stmt_role = std::move(prep_role.value());
    stmt_role.bind_text(1, input.role_id);
    auto step_role = stmt_role.step();
    if (!step_role.is_success() || !step_role.value()) {
        return contracts::Result<contracts::UserDto>::fail(
            contracts::ErrorCode::NotFound,
            "El rol especificado no existe"
        );
    }
    std::string role_name = stmt_role.get_text(0);

    // Verificar si el username ya existe
    auto prep_check = db_.prepare("SELECT COUNT(*) FROM users WHERE username = ?;");
    if (!prep_check.is_success()) return contracts::Result<contracts::UserDto>::fail(prep_check.error_code(), prep_check.error_message());
    auto stmt_chk = std::move(prep_check.value());
    stmt_chk.bind_text(1, input.username);
    auto step_chk = stmt_chk.step();
    if (step_chk.is_success() && stmt_chk.get_int64(0) > 0) {
        return contracts::Result<contracts::UserDto>::fail(
            contracts::ErrorCode::Conflict,
            "Ya existe un usuario con ese nombre de usuario"
        );
    }

    // Verificar si el email ya existe
    auto prep_echk = db_.prepare("SELECT COUNT(*) FROM users WHERE email = ?;");
    if (!prep_echk.is_success()) return contracts::Result<contracts::UserDto>::fail(prep_echk.error_code(), prep_echk.error_message());
    auto stmt_echk = std::move(prep_echk.value());
    stmt_echk.bind_text(1, input.email);
    auto step_echk = stmt_echk.step();
    if (step_echk.is_success() && stmt_echk.get_int64(0) > 0) {
        return contracts::Result<contracts::UserDto>::fail(
            contracts::ErrorCode::Conflict,
            "Ya existe un usuario con ese correo electrónico"
        );
    }

    std::string password_hash = crypto::PasswordHasher::hash_password(input.raw_password);
    std::string user_id = "user-" + input.username;
    const auto ts = domain::now();
    const std::string& now_str = ts.local;

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return contracts::Result<contracts::UserDto>::fail(tx.error_code(), tx.error_message());

    auto prep_ins = db_.prepare(
        "INSERT INTO users (id, username, display_name, email, password_hash, role_id, is_active, created_at, avatar_blob, updated_at, updated_at_utc) "
        "VALUES (?, ?, ?, ?, ?, ?, 1, ?, ?, ?, ?);"
    );
    if (!prep_ins.is_success()) {
        db_.rollback_transaction();
        return contracts::Result<contracts::UserDto>::fail(prep_ins.error_code(), prep_ins.error_message());
    }

    auto stmt_ins = std::move(prep_ins.value());
    stmt_ins.bind_text(1, user_id);
    stmt_ins.bind_text(2, input.username);
    stmt_ins.bind_text(3, input.display_name);
    stmt_ins.bind_text(4, input.email);
    stmt_ins.bind_text(5, password_hash);
    stmt_ins.bind_text(6, input.role_id);
    stmt_ins.bind_text(7, now_str);
    if (input.avatar_bytes.has_value()) {
        stmt_ins.bind_blob(8, *input.avatar_bytes);
    } else {
        stmt_ins.bind_null(8);
    }
    stmt_ins.bind_text(9, now_str);
    stmt_ins.bind_text(10, ts.utc);

    auto step_ins = stmt_ins.step();
    if (!step_ins.is_success()) {
        db_.rollback_transaction();
        return contracts::Result<contracts::UserDto>::fail(step_ins.error_code(), step_ins.error_message());
    }

    // Registrar en auditoría dentro de la transacción (SIN contraseñas)
    std::string audit_id = generate_id("audit");
    auto prep_aud = db_.prepare(
        "INSERT INTO audit_events (id, event_type, user_id, details, created_at, created_at_utc) "
        "VALUES (?, 'USER_CREATED', ?, ?, ?, ?);"
    );
    if (prep_aud.is_success()) {
        auto stmt_aud = std::move(prep_aud.value());
        stmt_aud.bind_text(1, audit_id);
        if (current_user_.has_value()) {
            stmt_aud.bind_text(2, current_user_->id);
        } else {
            stmt_aud.bind_null(2);
        }
        stmt_aud.bind_text(3, "Usuario creado: " + input.username + " (" + input.email + ") con rol: " + role_name);
        stmt_aud.bind_text(4, now_str);
        stmt_aud.bind_text(5, ts.utc);
        stmt_aud.step();
    }

    auto commit_res = db_.commit_transaction();
    if (!commit_res.is_success()) {
        db_.rollback_transaction();
        return contracts::Result<contracts::UserDto>::fail(commit_res.error_code(), commit_res.error_message());
    }

    contracts::UserDto dto;
    dto.id = user_id;
    dto.username = input.username;
    dto.display_name = input.display_name;
    dto.email = input.email;
    dto.role_id = input.role_id;
    dto.role_name = role_name;
    dto.is_active = true;
    dto.is_owner = (input.role_id == "role-owner" || role_name == "Dueño");
    dto.has_avatar = input.avatar_bytes.has_value();

    return contracts::Result<contracts::UserDto>::ok(dto);
}

contracts::OperationResult SqliteAccountService::update_user(const contracts::UpdateUserDto& input) {
    auto perm_res = check_permission_or_fail("users.manage");
    if (!perm_res.is_success()) return perm_res;

    if (input.user_id.empty()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "ID de usuario requerido");
    }

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return tx;

    // Obtener datos actuales del usuario
    auto prep_cur = db_.prepare(
        "SELECT u.username, u.display_name, u.role_id, r.name, u.is_active, u.email, (u.avatar_blob IS NOT NULL) "
        "FROM users u JOIN roles r ON u.role_id = r.id WHERE u.id = ?;"
    );
    if (!prep_cur.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(prep_cur.error_code(), prep_cur.error_message());
    }
    auto stmt_cur = std::move(prep_cur.value());
    stmt_cur.bind_text(1, input.user_id);
    auto step_cur = stmt_cur.step();
    if (!step_cur.is_success() || !step_cur.value()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Usuario no encontrado");
    }

    std::string cur_username = stmt_cur.get_text(0);
    std::string cur_display = stmt_cur.get_text(1);
    std::string cur_role_id = stmt_cur.get_text(2);
    std::string cur_role_name = stmt_cur.get_text(3);
    bool cur_active = stmt_cur.get_int64(4) != 0;
    std::string cur_email = stmt_cur.get_text(5);
    bool cur_has_avatar = stmt_cur.get_int64(6) != 0;

    // Regla: No se puede modificar al usuario o rol Desarrollador
    if (cur_role_id == "role-dev" || input.user_id == "user-dev") {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(contracts::ErrorCode::Forbidden, "No se puede modificar la identidad técnica Desarrollador");
    }

    if (input.role_id.has_value() && *input.role_id == "role-dev") {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(contracts::ErrorCode::Forbidden, "No se puede asignar el rol Desarrollador");
    }

    bool is_cur_owner = (cur_role_id == "role-owner" || cur_role_name == "Dueño");

    // INVARIANTE TRANSACCIONAL: Protección de la última cuenta Dueño activa
    if (is_cur_owner && cur_active) {
        // Consultar cuántos dueños activos existen
        auto prep_owners = db_.prepare(
            "SELECT COUNT(*) FROM users u "
            "JOIN roles r ON u.role_id = r.id "
            "WHERE (r.name = 'Dueño' OR r.id = 'role-owner') AND u.is_active = 1;"
        );
        if (!prep_owners.is_success()) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(prep_owners.error_code(), prep_owners.error_message());
        }
        auto stmt_owners = std::move(prep_owners.value());
        stmt_owners.step();
        std::int64_t active_owners = stmt_owners.get_int64(0);

        // Intento de desactivar al último Dueño
        if (input.is_active.has_value() && !*input.is_active) {
            if (active_owners <= 1) {
                db_.rollback_transaction();
                return contracts::OperationResult::fail(
                    contracts::ErrorCode::Conflict,
                    "No se puede desactivar a la última cuenta Dueño activa"
                );
            }
        }

        // Intento de cambiar el rol del último Dueño
        if (input.role_id.has_value() && *input.role_id != cur_role_id) {
            if (active_owners <= 1) {
                db_.rollback_transaction();
                return contracts::OperationResult::fail(
                    contracts::ErrorCode::Conflict,
                    "No se puede quitar el rol Dueño a la última cuenta Dueño activa"
                );
            }
        }
    }

    // Validar correo electrónico si fue proporcionado
    if (input.email.has_value()) {
        if (!is_valid_email(*input.email)) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(
                contracts::ErrorCode::InvalidInput,
                "El formato del correo electrónico es inválido"
            );
        }
        auto prep_val_em = db_.prepare("SELECT COUNT(*) FROM users WHERE email = ? AND id != ?;");
        if (!prep_val_em.is_success()) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(prep_val_em.error_code(), prep_val_em.error_message());
        }
        auto stmt_ve = std::move(prep_val_em.value());
        stmt_ve.bind_text(1, *input.email);
        stmt_ve.bind_text(2, input.user_id);
        stmt_ve.step();
        if (stmt_ve.get_int64(0) > 0) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(
                contracts::ErrorCode::Conflict,
                "El correo electrónico ya está registrado por otro usuario"
            );
        }
    }

    // Validar avatar si fue proporcionado
    if (input.avatar_bytes.has_value()) {
        std::string err;
        if (!validate_avatar_bytes(*input.avatar_bytes, err)) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, err);
        }
    }

    std::string new_display = input.display_name.value_or(cur_display);
    std::string new_role_id = input.role_id.value_or(cur_role_id);
    bool new_active = input.is_active.value_or(cur_active);
    std::string new_email = input.email.value_or(cur_email);

    // Si se cambió el rol, validar que el nuevo rol exista
    if (new_role_id != cur_role_id) {
        auto prep_val = db_.prepare("SELECT COUNT(*) FROM roles WHERE id = ?;");
        if (!prep_val.is_success()) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(prep_val.error_code(), prep_val.error_message());
        }
        auto stmt_val = std::move(prep_val.value());
        stmt_val.bind_text(1, new_role_id);
        stmt_val.step();
        if (stmt_val.get_int64(0) == 0) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "El rol destino no existe");
        }
    }

    auto prep_upd = db_.prepare(
        "UPDATE users SET display_name = ?, role_id = ?, is_active = ?, email = ?, updated_at = ?, updated_at_utc = ? WHERE id = ?;"
    );
    if (!prep_upd.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(prep_upd.error_code(), prep_upd.error_message());
    }
    auto stmt_upd = std::move(prep_upd.value());
    stmt_upd.bind_text(1, new_display);
    stmt_upd.bind_text(2, new_role_id);
    stmt_upd.bind_int64(3, new_active ? 1 : 0);
    stmt_upd.bind_text(4, new_email);
    const auto user_upd_ts = domain::now();
    stmt_upd.bind_text(5, user_upd_ts.local);
    stmt_upd.bind_text(6, user_upd_ts.utc);
    stmt_upd.bind_text(7, input.user_id);
    auto step_upd = stmt_upd.step();
    if (!step_upd.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(step_upd.error_code(), step_upd.error_message());
    }

    bool new_has_avatar = cur_has_avatar;
    if (input.avatar_bytes.has_value()) {
        auto prep_av = db_.prepare("UPDATE users SET avatar_blob = ? WHERE id = ?;");
        if (!prep_av.is_success()) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(prep_av.error_code(), prep_av.error_message());
        }
        auto stmt_av = std::move(prep_av.value());
        stmt_av.bind_blob(1, *input.avatar_bytes);
        stmt_av.bind_text(2, input.user_id);
        auto step_av = stmt_av.step();
        if (!step_av.is_success()) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(step_av.error_code(), step_av.error_message());
        }
        new_has_avatar = true;
    } else if (input.remove_avatar.has_value() && *input.remove_avatar) {
        auto prep_rem = db_.prepare("UPDATE users SET avatar_blob = NULL WHERE id = ?;");
        if (!prep_rem.is_success()) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(prep_rem.error_code(), prep_rem.error_message());
        }
        auto stmt_rem = std::move(prep_rem.value());
        stmt_rem.bind_text(1, input.user_id);
        auto step_rem = stmt_rem.step();
        if (!step_rem.is_success()) {
            db_.rollback_transaction();
            return contracts::OperationResult::fail(step_rem.error_code(), step_rem.error_message());
        }
        new_has_avatar = false;
    }

    // Auditoría
    const auto ts = domain::now();
    const std::string& now_str = ts.local;
    std::string audit_id = generate_id("audit");
    std::stringstream det;
    det << "Usuario modificado: " << cur_username;
    if (new_display != cur_display) det << " [Nombre: " << cur_display << " -> " << new_display << "]";
    if (new_email != cur_email) det << " [Email: " << cur_email << " -> " << new_email << "]";
    if (new_role_id != cur_role_id) det << " [Rol: " << cur_role_id << " -> " << new_role_id << "]";
    if (new_active != cur_active) det << " [Estado activo: " << (cur_active ? "1" : "0") << " -> " << (new_active ? "1" : "0") << "]";
    if (input.avatar_bytes.has_value()) det << " [Avatar actualizado]";
    else if (input.remove_avatar.has_value() && *input.remove_avatar) det << " [Avatar eliminado]";

    auto prep_aud = db_.prepare(
        "INSERT INTO audit_events (id, event_type, user_id, details, created_at, created_at_utc) "
        "VALUES (?, 'USER_UPDATED', ?, ?, ?, ?);"
    );
    if (prep_aud.is_success()) {
        auto stmt_aud = std::move(prep_aud.value());
        stmt_aud.bind_text(1, audit_id);
        if (current_user_.has_value()) {
            stmt_aud.bind_text(2, current_user_->id);
        } else {
            stmt_aud.bind_null(2);
        }
        stmt_aud.bind_text(3, det.str());
        stmt_aud.bind_text(4, now_str);
        stmt_aud.bind_text(5, ts.utc);
        stmt_aud.step();
    }

    auto commit_res = db_.commit_transaction();
    if (!commit_res.is_success()) {
        db_.rollback_transaction();
        return commit_res;
    }

    // Si el usuario actualizado es el usuario logueado en memoria, actualizarlo
    if (current_user_.has_value() && current_user_->id == input.user_id) {
        current_user_->display_name = new_display;
        current_user_->email = new_email;
        current_user_->role_id = new_role_id;
        current_user_->is_active = new_active;
        current_user_->has_avatar = new_has_avatar;
        auto perms = load_role_permissions(new_role_id);
        if (perms.is_success()) {
            current_user_permissions_ = perms.value();
        }
    }

    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteAccountService::change_password(
    const std::string& user_id,
    const std::string& new_raw_password
) {
    if (!is_authenticated()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::Forbidden, "Se requiere autenticación");
    }

    bool is_self = (current_user_->id == user_id);
    if (!is_self && !has_permission("users.manage")) {
        return contracts::OperationResult::fail(contracts::ErrorCode::Forbidden, "Falta de permisos para cambiar contraseña a otros usuarios");
    }

    if (new_raw_password.length() < 4) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "La contraseña debe tener al menos 4 caracteres");
    }

    // Verificar si el usuario existe
    auto prep_u = db_.prepare("SELECT username, role_id FROM users WHERE id = ?;");
    if (!prep_u.is_success()) return contracts::OperationResult::fail(prep_u.error_code(), prep_u.error_message());
    auto stmt_u = std::move(prep_u.value());
    stmt_u.bind_text(1, user_id);
    auto step_u = stmt_u.step();
    if (!step_u.is_success() || !step_u.value()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Usuario no encontrado");
    }
    std::string username = stmt_u.get_text(0);
    std::string role_id = stmt_u.get_text(1);

    if (role_id == "role-dev" || user_id == "user-dev") {
        return contracts::OperationResult::fail(contracts::ErrorCode::Forbidden, "No se puede cambiar la contraseña del Desarrollador por este medio");
    }

    std::string new_hash = crypto::PasswordHasher::hash_password(new_raw_password);

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return tx;

    auto prep_upd = db_.prepare("UPDATE users SET password_hash = ?, updated_at = ?, updated_at_utc = ? WHERE id = ?;");
    if (!prep_upd.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(prep_upd.error_code(), prep_upd.error_message());
    }
    auto stmt_upd = std::move(prep_upd.value());
    const auto pwd_ts = domain::now();
    stmt_upd.bind_text(1, new_hash);
    stmt_upd.bind_text(2, pwd_ts.local);
    stmt_upd.bind_text(3, pwd_ts.utc);
    stmt_upd.bind_text(4, user_id);
    auto step_upd = stmt_upd.step();
    if (!step_upd.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(step_upd.error_code(), step_upd.error_message());
    }

    // Registrar en auditoría (SIN contraseñas ni hashes)
    const auto ts = domain::now();
    const std::string& now_str = ts.local;
    std::string audit_id = generate_id("audit");
    auto prep_aud = db_.prepare(
        "INSERT INTO audit_events (id, event_type, user_id, details, created_at, created_at_utc) "
        "VALUES (?, 'PASSWORD_CHANGED', ?, ?, ?, ?);"
    );
    if (prep_aud.is_success()) {
        auto stmt_aud = std::move(prep_aud.value());
        stmt_aud.bind_text(1, audit_id);
        stmt_aud.bind_text(2, current_user_->id);
        stmt_aud.bind_text(3, "Cambio de contraseña para usuario: " + username);
        stmt_aud.bind_text(4, now_str);
        stmt_aud.bind_text(5, ts.utc);
        stmt_aud.step();
    }

    return db_.commit_transaction();
}

contracts::Result<contracts::UserDto> SqliteAccountService::get_user_by_id(const std::string& user_id) {
    auto perm_res = check_permission_or_fail("users.manage");
    if (!perm_res.is_success()) {
        // Permitir consultar si es uno mismo
        if (!is_authenticated() || current_user_->id != user_id) {
            return contracts::Result<contracts::UserDto>::fail(perm_res.error_code(), perm_res.error_message());
        }
    }

    auto prep = db_.prepare(
        "SELECT u.id, u.username, u.display_name, u.role_id, r.name, u.is_active, u.email, (u.avatar_blob IS NOT NULL) "
        "FROM users u JOIN roles r ON u.role_id = r.id WHERE u.id = ?;"
    );
    if (!prep.is_success()) return contracts::Result<contracts::UserDto>::fail(prep.error_code(), prep.error_message());
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, user_id);
    auto step = stmt.step();
    if (!step.is_success() || !step.value()) {
        return contracts::Result<contracts::UserDto>::fail(contracts::ErrorCode::NotFound, "Usuario no encontrado");
    }

    contracts::UserDto u;
    u.id = stmt.get_text(0);
    u.username = stmt.get_text(1);
    u.display_name = stmt.get_text(2);
    u.role_id = stmt.get_text(3);
    u.role_name = stmt.get_text(4);
    u.is_active = stmt.get_int64(5) != 0;
    u.is_owner = (u.role_id == "role-owner" || u.role_name == "Dueño");
    u.email = stmt.get_text(6);
    u.has_avatar = stmt.get_int64(7) != 0;

    return contracts::Result<contracts::UserDto>::ok(u);
}

contracts::Result<std::vector<contracts::UserDto>> SqliteAccountService::list_users() {
    auto perm_res = check_permission_or_fail("users.manage");
    if (!perm_res.is_success()) return contracts::Result<std::vector<contracts::UserDto>>::fail(perm_res.error_code(), perm_res.error_message());

    auto prep = db_.prepare(
        "SELECT u.id, u.username, u.display_name, u.role_id, r.name, u.is_active, u.email, (u.avatar_blob IS NOT NULL) "
        "FROM users u JOIN roles r ON u.role_id = r.id ORDER BY u.username ASC;"
    );
    if (!prep.is_success()) return contracts::Result<std::vector<contracts::UserDto>>::fail(prep.error_code(), prep.error_message());

    auto stmt = std::move(prep.value());
    std::vector<contracts::UserDto> list;
    while (true) {
        auto step_res = stmt.step();
        if (!step_res.is_success()) return contracts::Result<std::vector<contracts::UserDto>>::fail(step_res.error_code(), step_res.error_message());
        if (!step_res.value()) break;

        contracts::UserDto u;
        u.id = stmt.get_text(0);
        u.username = stmt.get_text(1);
        u.display_name = stmt.get_text(2);
        u.role_id = stmt.get_text(3);
        u.role_name = stmt.get_text(4);
        u.is_active = stmt.get_int64(5) != 0;
        u.is_owner = (u.role_id == "role-owner" || u.role_name == "Dueño");
        u.email = stmt.get_text(6);
        u.has_avatar = stmt.get_int64(7) != 0;
        list.push_back(u);
    }
    return contracts::Result<std::vector<contracts::UserDto>>::ok(list);
}

contracts::Result<std::vector<std::uint8_t>> SqliteAccountService::get_user_avatar(const std::string& user_id) {
    if (!is_authenticated()) {
        return contracts::Result<std::vector<std::uint8_t>>::fail(
            contracts::ErrorCode::Forbidden,
            "Se requiere autenticación"
        );
    }

    auto prep = db_.prepare("SELECT avatar_blob FROM users WHERE id = ?;");
    if (!prep.is_success()) return contracts::Result<std::vector<std::uint8_t>>::fail(prep.error_code(), prep.error_message());
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, user_id);
    auto step = stmt.step();
    if (!step.is_success() || !step.value()) {
        return contracts::Result<std::vector<std::uint8_t>>::fail(contracts::ErrorCode::NotFound, "Usuario no encontrado");
    }

    if (stmt.is_null(0)) {
        return contracts::Result<std::vector<std::uint8_t>>::fail(contracts::ErrorCode::NotFound, "El usuario no tiene avatar configurado");
    }

    return contracts::Result<std::vector<std::uint8_t>>::ok(stmt.get_blob(0));
}

contracts::Result<contracts::RoleDto> SqliteAccountService::create_role(const contracts::CreateRoleDto& input) {
    auto perm_res = check_permission_or_fail("roles.manage");
    if (!perm_res.is_success()) {
        return contracts::Result<contracts::RoleDto>::fail(perm_res.error_code(), perm_res.error_message());
    }

    if (input.name.empty()) {
        return contracts::Result<contracts::RoleDto>::fail(
            contracts::ErrorCode::InvalidInput,
            "El nombre del rol es requerido"
        );
    }

    // REGLA CRÍTICA DE AGENTS.md y specs.md:
    // El permiso "backup.dev_config" requiere identidad Desarrollador separada.
    // Un rol configurable por el Dueño NO puede otorgarse ese acceso.
    for (const auto& perm : input.permissions) {
        if (perm == "backup.dev_config") {
            return contracts::Result<contracts::RoleDto>::fail(
                contracts::ErrorCode::Forbidden,
                "El permiso backup.dev_config es exclusivo del rol Desarrollador y no puede ser concedido por roles configurables"
            );
        }
    }

    // Verificar si el nombre de rol ya existe
    auto prep_chk = db_.prepare("SELECT COUNT(*) FROM roles WHERE name = ?;");
    if (!prep_chk.is_success()) return contracts::Result<contracts::RoleDto>::fail(prep_chk.error_code(), prep_chk.error_message());
    auto stmt_chk = std::move(prep_chk.value());
    stmt_chk.bind_text(1, input.name);
    stmt_chk.step();
    if (stmt_chk.get_int64(0) > 0) {
        return contracts::Result<contracts::RoleDto>::fail(
            contracts::ErrorCode::Conflict,
            "Ya existe un rol con ese nombre"
        );
    }

    // Validar que todos los permisos solicitados existan en permissions
    for (const auto& perm : input.permissions) {
        auto prep_p = db_.prepare("SELECT COUNT(*) FROM permissions WHERE id = ?;");
        if (!prep_p.is_success()) return contracts::Result<contracts::RoleDto>::fail(prep_p.error_code(), prep_p.error_message());
        auto stmt_p = std::move(prep_p.value());
        stmt_p.bind_text(1, perm);
        stmt_p.step();
        if (stmt_p.get_int64(0) == 0) {
            return contracts::Result<contracts::RoleDto>::fail(
                contracts::ErrorCode::InvalidInput,
                "El permiso especificado no existe: " + perm
            );
        }
    }

    std::string role_id = "role-" + input.name;
    std::replace(role_id.begin(), role_id.end(), ' ', '-');

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return contracts::Result<contracts::RoleDto>::fail(tx.error_code(), tx.error_message());

    auto prep_ins = db_.prepare("INSERT INTO roles (id, name, description, is_system, updated_at, updated_at_utc) VALUES (?, ?, ?, 0, ?, ?);");
    if (!prep_ins.is_success()) {
        db_.rollback_transaction();
        return contracts::Result<contracts::RoleDto>::fail(prep_ins.error_code(), prep_ins.error_message());
    }
    auto stmt_ins = std::move(prep_ins.value());
    stmt_ins.bind_text(1, role_id);
    stmt_ins.bind_text(2, input.name);
    stmt_ins.bind_text(3, input.description);
    const auto role_ts = domain::now();
    stmt_ins.bind_text(4, role_ts.local);
    stmt_ins.bind_text(5, role_ts.utc);
    auto step_ins = stmt_ins.step();
    if (!step_ins.is_success()) {
        db_.rollback_transaction();
        return contracts::Result<contracts::RoleDto>::fail(step_ins.error_code(), step_ins.error_message());
    }

    // Insertar permisos en role_permissions
    for (const auto& perm : input.permissions) {
        auto prep_rp = db_.prepare("INSERT INTO role_permissions (role_id, permission_id) VALUES (?, ?);");
        if (prep_rp.is_success()) {
            auto stmt_rp = std::move(prep_rp.value());
            stmt_rp.bind_text(1, role_id);
            stmt_rp.bind_text(2, perm);
            stmt_rp.step();
        }
    }

    // Auditoría
    const auto ts = domain::now();
    const std::string& now_str = ts.local;
    std::string audit_id = generate_id("audit");
    auto prep_aud = db_.prepare("INSERT INTO audit_events (id, event_type, user_id, details, created_at, created_at_utc) VALUES (?, 'ROLE_CREATED', ?, ?, ?, ?);");
    if (prep_aud.is_success()) {
        auto stmt_aud = std::move(prep_aud.value());
        stmt_aud.bind_text(1, audit_id);
        if (current_user_.has_value()) {
            stmt_aud.bind_text(2, current_user_->id);
        } else {
            stmt_aud.bind_null(2);
        }
        stmt_aud.bind_text(3, "Rol creado: " + input.name + " (" + std::to_string(input.permissions.size()) + " permisos)");
        stmt_aud.bind_text(4, now_str);
        stmt_aud.bind_text(5, ts.utc);
        stmt_aud.step();
    }

    auto commit_res = db_.commit_transaction();
    if (!commit_res.is_success()) {
        db_.rollback_transaction();
        return contracts::Result<contracts::RoleDto>::fail(commit_res.error_code(), commit_res.error_message());
    }

    contracts::RoleDto dto;
    dto.id = role_id;
    dto.name = input.name;
    dto.description = input.description;
    dto.is_system = false;
    dto.permissions = input.permissions;

    return contracts::Result<contracts::RoleDto>::ok(dto);
}

contracts::OperationResult SqliteAccountService::update_role(const contracts::UpdateRoleDto& input) {
    auto perm_res = check_permission_or_fail("roles.manage");
    if (!perm_res.is_success()) return perm_res;

    if (input.role_id.empty()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::InvalidInput, "ID de rol requerido");
    }

    // Regla: role-dev no puede ser modificado por usuarios
    if (input.role_id == "role-dev") {
        return contracts::OperationResult::fail(contracts::ErrorCode::Forbidden, "El rol Desarrollador no puede ser modificado");
    }

    // REGLA: backup.dev_config no puede ser otorgado por roles configurables
    for (const auto& perm : input.permissions) {
        if (perm == "backup.dev_config") {
            return contracts::OperationResult::fail(
                contracts::ErrorCode::Forbidden,
                "El permiso backup.dev_config es exclusivo del rol Desarrollador y no puede ser concedido por roles configurables"
            );
        }
    }

    // Validar existencia de rol
    auto prep_r = db_.prepare("SELECT id, name, description, is_system FROM roles WHERE id = ?;");
    if (!prep_r.is_success()) return contracts::OperationResult::fail(prep_r.error_code(), prep_r.error_message());
    auto stmt_r = std::move(prep_r.value());
    stmt_r.bind_text(1, input.role_id);
    auto step_r = stmt_r.step();
    if (!step_r.is_success() || !step_r.value()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Rol no encontrado");
    }
    std::string cur_name = stmt_r.get_text(1);

    // Si es Dueño, el rol es protegido y no puede perder facultades operativas
    if (input.role_id == "role-owner" || cur_name == "Dueño") {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "El rol Dueño es un rol protegido del sistema y no puede ser modificado"
        );
    }

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return tx;

    auto prep_upd = db_.prepare("UPDATE roles SET name = ?, description = ?, updated_at = ?, updated_at_utc = ? WHERE id = ?;");
    if (!prep_upd.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(prep_upd.error_code(), prep_upd.error_message());
    }
    auto stmt_upd = std::move(prep_upd.value());
    stmt_upd.bind_text(1, input.name);
    stmt_upd.bind_text(2, input.description);
    const auto role_upd_ts = domain::now();
    stmt_upd.bind_text(3, role_upd_ts.local);
    stmt_upd.bind_text(4, role_upd_ts.utc);
    stmt_upd.bind_text(5, input.role_id);
    stmt_upd.step();

    // Eliminar permisos anteriores y reasignar
    auto prep_del = db_.prepare("DELETE FROM role_permissions WHERE role_id = ?;");
    if (prep_del.is_success()) {
        auto stmt_del = std::move(prep_del.value());
        stmt_del.bind_text(1, input.role_id);
        stmt_del.step();
    }

    for (const auto& perm : input.permissions) {
        auto prep_rp = db_.prepare("INSERT INTO role_permissions (role_id, permission_id) VALUES (?, ?);");
        if (prep_rp.is_success()) {
            auto stmt_rp = std::move(prep_rp.value());
            stmt_rp.bind_text(1, input.role_id);
            stmt_rp.bind_text(2, perm);
            stmt_rp.step();
        }
    }

    // Auditoría
    const auto ts = domain::now();
    const std::string& now_str = ts.local;
    std::string audit_id = generate_id("audit");
    auto prep_aud = db_.prepare("INSERT INTO audit_events (id, event_type, user_id, details, created_at, created_at_utc) VALUES (?, 'ROLE_UPDATED', ?, ?, ?, ?);");
    if (prep_aud.is_success()) {
        auto stmt_aud = std::move(prep_aud.value());
        stmt_aud.bind_text(1, audit_id);
        if (current_user_.has_value()) {
            stmt_aud.bind_text(2, current_user_->id);
        } else {
            stmt_aud.bind_null(2);
        }
        stmt_aud.bind_text(3, "Rol modificado: " + input.name + " (" + std::to_string(input.permissions.size()) + " permisos)");
        stmt_aud.bind_text(4, now_str);
        stmt_aud.bind_text(5, ts.utc);
        stmt_aud.step();
    }

    auto commit_res = db_.commit_transaction();
    if (!commit_res.is_success()) {
        db_.rollback_transaction();
        return commit_res;
    }

    // Si el usuario actual tiene este rol, recargar sus permisos
    if (current_user_.has_value() && current_user_->role_id == input.role_id) {
        auto perms = load_role_permissions(input.role_id);
        if (perms.is_success()) {
            current_user_permissions_ = perms.value();
        }
    }

    return contracts::OperationResult::ok();
}

contracts::OperationResult SqliteAccountService::delete_role(const std::string& role_id) {
    auto perm_res = check_permission_or_fail("roles.manage");
    if (!perm_res.is_success()) return perm_res;

    // Verificar existencia y si es de sistema
    auto prep_r = db_.prepare("SELECT name, is_system FROM roles WHERE id = ?;");
    if (!prep_r.is_success()) return contracts::OperationResult::fail(prep_r.error_code(), prep_r.error_message());
    auto stmt_r = std::move(prep_r.value());
    stmt_r.bind_text(1, role_id);
    auto step_r = stmt_r.step();
    if (!step_r.is_success() || !step_r.value()) {
        return contracts::OperationResult::fail(contracts::ErrorCode::NotFound, "Rol no encontrado");
    }
    std::string role_name = stmt_r.get_text(0);
    bool is_system = stmt_r.get_int64(1) != 0;

    if (is_system) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "No se pueden eliminar roles protegidos del sistema"
        );
    }

    // Verificar si hay usuarios asignados a este rol
    auto prep_u = db_.prepare("SELECT COUNT(*) FROM users WHERE role_id = ?;");
    if (!prep_u.is_success()) return contracts::OperationResult::fail(prep_u.error_code(), prep_u.error_message());
    auto stmt_u = std::move(prep_u.value());
    stmt_u.bind_text(1, role_id);
    stmt_u.step();
    if (stmt_u.get_int64(0) > 0) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Conflict,
            "No se puede eliminar un rol que tiene usuarios asignados"
        );
    }

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return tx;

    auto prep_del_p = db_.prepare("DELETE FROM role_permissions WHERE role_id = ?;");
    if (prep_del_p.is_success()) {
        auto stmt_del_p = std::move(prep_del_p.value());
        stmt_del_p.bind_text(1, role_id);
        stmt_del_p.step();
    }

    auto prep_del_r = db_.prepare("DELETE FROM roles WHERE id = ?;");
    if (!prep_del_r.is_success()) {
        db_.rollback_transaction();
        return contracts::OperationResult::fail(prep_del_r.error_code(), prep_del_r.error_message());
    }
    auto stmt_del_r = std::move(prep_del_r.value());
    stmt_del_r.bind_text(1, role_id);
    stmt_del_r.step();

    // Auditoría
    const auto ts = domain::now();
    const std::string& now_str = ts.local;
    std::string audit_id = generate_id("audit");
    auto prep_aud = db_.prepare("INSERT INTO audit_events (id, event_type, user_id, details, created_at, created_at_utc) VALUES (?, 'ROLE_DELETED', ?, ?, ?, ?);");
    if (prep_aud.is_success()) {
        auto stmt_aud = std::move(prep_aud.value());
        stmt_aud.bind_text(1, audit_id);
        if (current_user_.has_value()) {
            stmt_aud.bind_text(2, current_user_->id);
        } else {
            stmt_aud.bind_null(2);
        }
        stmt_aud.bind_text(3, "Rol eliminado: " + role_name + " (" + role_id + ")");
        stmt_aud.bind_text(4, now_str);
        stmt_aud.bind_text(5, ts.utc);
        stmt_aud.step();
    }

    return db_.commit_transaction();
}

contracts::Result<contracts::RoleDto> SqliteAccountService::get_role_by_id(const std::string& role_id) {
    auto prep = db_.prepare("SELECT id, name, description, is_system FROM roles WHERE id = ?;");
    if (!prep.is_success()) return contracts::Result<contracts::RoleDto>::fail(prep.error_code(), prep.error_message());
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, role_id);
    auto step = stmt.step();
    if (!step.is_success() || !step.value()) {
        return contracts::Result<contracts::RoleDto>::fail(contracts::ErrorCode::NotFound, "Rol no encontrado");
    }

    contracts::RoleDto dto;
    dto.id = stmt.get_text(0);
    dto.name = stmt.get_text(1);
    dto.description = stmt.get_text(2);
    dto.is_system = stmt.get_int64(3) != 0;

    auto perms = load_role_permissions(dto.id);
    if (perms.is_success()) {
        dto.permissions = perms.value();
    }

    return contracts::Result<contracts::RoleDto>::ok(dto);
}

contracts::Result<std::vector<contracts::RoleDto>> SqliteAccountService::list_roles() {
    auto prep = db_.prepare("SELECT id, name, description, is_system FROM roles ORDER BY name ASC;");
    if (!prep.is_success()) return contracts::Result<std::vector<contracts::RoleDto>>::fail(prep.error_code(), prep.error_message());

    auto stmt = std::move(prep.value());
    std::vector<contracts::RoleDto> list;
    while (true) {
        auto step_res = stmt.step();
        if (!step_res.is_success()) return contracts::Result<std::vector<contracts::RoleDto>>::fail(step_res.error_code(), step_res.error_message());
        if (!step_res.value()) break;

        contracts::RoleDto r;
        r.id = stmt.get_text(0);
        r.name = stmt.get_text(1);
        r.description = stmt.get_text(2);
        r.is_system = stmt.get_int64(3) != 0;

        auto perms = load_role_permissions(r.id);
        if (perms.is_success()) {
            r.permissions = perms.value();
        }

        list.push_back(r);
    }
    return contracts::Result<std::vector<contracts::RoleDto>>::ok(list);
}

contracts::Result<std::vector<contracts::PermissionDto>> SqliteAccountService::list_permissions() {
    auto perm_res = check_permission_or_fail("roles.manage");
    if (!perm_res.is_success()) {
        return contracts::Result<std::vector<contracts::PermissionDto>>::fail(perm_res.error_code(), perm_res.error_message());
    }

    auto prep = db_.prepare("SELECT id, description, title, section, sort_order FROM permissions ORDER BY sort_order ASC, id ASC;");
    if (!prep.is_success()) return contracts::Result<std::vector<contracts::PermissionDto>>::fail(prep.error_code(), prep.error_message());

    auto stmt = std::move(prep.value());
    std::vector<contracts::PermissionDto> list;
    while (true) {
        auto step = stmt.step();
        if (!step.is_success()) return contracts::Result<std::vector<contracts::PermissionDto>>::fail(step.error_code(), step.error_message());
        if (!step.value()) break;

        contracts::PermissionDto p;
        p.id = stmt.get_text(0);
        p.description = stmt.get_text(1);
        p.title = stmt.get_text(2);
        p.section = stmt.get_text(3);
        p.sort_order = static_cast<int>(stmt.get_int64(4));

        auto dot_pos = p.id.find('.');
        if (dot_pos != std::string::npos) {
            p.module = p.id.substr(0, dot_pos);
        } else {
            p.module = "general";
        }

        list.push_back(p);
    }
    return contracts::Result<std::vector<contracts::PermissionDto>>::ok(list);
}

contracts::Result<std::vector<contracts::AuditEventDto>> SqliteAccountService::list_audit_events(const contracts::AuditFilterDto& filter) {
    auto perm_res = check_permission_or_fail("audit.view");
    if (!perm_res.is_success()) {
        return contracts::Result<std::vector<contracts::AuditEventDto>>::fail(perm_res.error_code(), perm_res.error_message());
    }

    std::string sql = 
        "SELECT a.id, a.event_type, COALESCE(a.user_id, ''), COALESCE(u.username, ''), a.details, a.created_at "
        "FROM audit_events a "
        "LEFT JOIN users u ON a.user_id = u.id "
        "WHERE 1=1 ";

    if (filter.user_id.has_value() && !filter.user_id->empty()) {
        sql += "AND a.user_id = ? ";
    }
    if (filter.event_type.has_value() && !filter.event_type->empty()) {
        sql += "AND a.event_type = ? ";
    }
    if (filter.start_date.has_value() && !filter.start_date->empty()) {
        sql += "AND a.created_at >= ? ";
    }
    if (filter.end_date.has_value() && !filter.end_date->empty()) {
        sql += "AND a.created_at <= ? ";
    }

    sql += "ORDER BY a.created_at DESC, a.id DESC LIMIT ? OFFSET ?;";

    auto prep = db_.prepare(sql);
    if (!prep.is_success()) {
        return contracts::Result<std::vector<contracts::AuditEventDto>>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    int param_idx = 1;
    if (filter.user_id.has_value() && !filter.user_id->empty()) {
        stmt.bind_text(param_idx++, *filter.user_id);
    }
    if (filter.event_type.has_value() && !filter.event_type->empty()) {
        stmt.bind_text(param_idx++, *filter.event_type);
    }
    if (filter.start_date.has_value() && !filter.start_date->empty()) {
        stmt.bind_text(param_idx++, *filter.start_date);
    }
    if (filter.end_date.has_value() && !filter.end_date->empty()) {
        stmt.bind_text(param_idx++, *filter.end_date);
    }
    stmt.bind_int64(param_idx++, static_cast<std::int64_t>(filter.limit > 0 ? filter.limit : 100));
    stmt.bind_int64(param_idx++, static_cast<std::int64_t>(filter.offset));

    std::vector<contracts::AuditEventDto> list;
    while (true) {
        auto step = stmt.step();
        if (!step.is_success()) return contracts::Result<std::vector<contracts::AuditEventDto>>::fail(step.error_code(), step.error_message());
        if (!step.value()) break;

        contracts::AuditEventDto evt;
        evt.id = stmt.get_text(0);
        evt.event_type = stmt.get_text(1);
        evt.user_id = stmt.get_text(2);
        evt.username = stmt.get_text(3);
        evt.details = stmt.get_text(4);
        evt.created_at = stmt.get_text(5);
        list.push_back(evt);
    }

    return contracts::Result<std::vector<contracts::AuditEventDto>>::ok(list);
}

contracts::OperationResult SqliteAccountService::record_audit_event(
    const std::string& event_type,
    const std::string& details
) {
    const auto ts = domain::now();
    std::string audit_id = generate_id("audit");

    std::string actor_id = current_user_.has_value() ? current_user_->id : "";

    auto prep = db_.prepare(
        "INSERT INTO audit_events (id, event_type, user_id, details, created_at, created_at_utc) "
        "VALUES (?, ?, ?, ?, ?, ?);"
    );
    if (!prep.is_success()) {
        return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    stmt.bind_text(1, audit_id);
    stmt.bind_text(2, event_type);
    if (actor_id.empty()) {
        stmt.bind_null(3);
    } else {
        stmt.bind_text(3, actor_id);
    }
    stmt.bind_text(4, details);
    stmt.bind_text(5, ts.local);
    stmt.bind_text(6, ts.utc);

    auto step_res = stmt.step();
    if (!step_res.is_success()) {
        return contracts::OperationResult::fail(step_res.error_code(), step_res.error_message());
    }

    return contracts::OperationResult::ok();
}

void SqliteAccountService::set_current_user_for_testing(const contracts::UserDto& user) {
    current_user_ = user;
    auto perms = load_role_permissions(user.role_id);
    if (perms.is_success()) {
        current_user_permissions_ = perms.value();
    }
}

} // namespace app::services
