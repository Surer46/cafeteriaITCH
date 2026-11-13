#include "app/services/sqlite_shift_service.hpp"
#include "app/services/identifiers.hpp"
#include "app/domain/clock.hpp"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <atomic>

namespace app::services {

namespace {

} // namespace

SqliteShiftService::SqliteShiftService(
    storage::SqliteDatabase& db,
    std::shared_ptr<contracts::IAccountService> account_service
) : db_(db), account_service_(std::move(account_service)) {}

contracts::OperationResult SqliteShiftService::check_permission_or_fail(std::string_view perm) const {
    if (!account_service_) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Servicio de cuentas no configurado"
        );
    }
    if (!account_service_->is_authenticated()) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Se requiere inicio de sesión"
        );
    }
    if (!account_service_->has_permission(perm)) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Permiso insuficiente: " + std::string(perm)
        );
    }
    return contracts::OperationResult::ok();
}

contracts::Result<contracts::ShiftDto> SqliteShiftService::get_current_shift() {
    const char* sql =
        "SELECT s.id, s.register_id, r.name, s.cashier_user_id, u.display_name, "
        "s.opened_at, s.closed_at, s.initial_cash_cents, s.is_open "
        "FROM shifts s "
        "JOIN cash_registers r ON s.register_id = r.id "
        "JOIN users u ON s.cashier_user_id = u.id "
        "WHERE s.is_open = 1 "
        "ORDER BY s.opened_at DESC LIMIT 1;";

    auto prep = db_.prepare(sql);
    if (!prep.is_success()) {
        return contracts::Result<contracts::ShiftDto>::fail(prep.error_code(), prep.error_message());
    }

    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    if (!step.is_success()) {
        return contracts::Result<contracts::ShiftDto>::fail(step.error_code(), step.error_message());
    }
    if (!step.value()) {
        return contracts::Result<contracts::ShiftDto>::fail(
            contracts::ErrorCode::NotFound,
            "No hay ningún turno abierto actualmente"
        );
    }

    contracts::ShiftDto dto;
    dto.id = stmt.get_text(0);
    dto.register_id = stmt.get_text(1);
    dto.register_name = stmt.get_text(2);
    dto.cashier_user_id = stmt.get_text(3);
    dto.cashier_name = stmt.get_text(4);
    dto.opened_at = stmt.get_text(5);
    dto.closed_at = stmt.is_null(6) ? "" : stmt.get_text(6);
    dto.initial_cash = contracts::Money(stmt.get_int64(7));
    dto.is_open = stmt.get_int64(8) == 1;

    return contracts::Result<contracts::ShiftDto>::ok(dto);
}

contracts::Result<contracts::ShiftDto> SqliteShiftService::open_shift(
    const std::string& register_id,
    contracts::Money initial_cash
) {
    auto perm_check = check_permission_or_fail("shift.operate");
    if (!perm_check.is_success()) {
        return contracts::Result<contracts::ShiftDto>::fail(perm_check.error_code(), perm_check.error_message());
    }

    if (initial_cash.cents() < 0) {
        return contracts::Result<contracts::ShiftDto>::fail(
            contracts::ErrorCode::InvalidInput,
            "El fondo inicial de caja no puede ser negativo"
        );
    }

    auto cur_user = account_service_->current_user();
    if (!cur_user.is_success()) {
        return contracts::Result<contracts::ShiftDto>::fail(
            contracts::ErrorCode::Forbidden,
            "No hay usuario activo"
        );
    }

    // Iniciar transacción atómica (BEGIN IMMEDIATE)
    auto tx_begin = db_.execute("BEGIN IMMEDIATE;");
    if (!tx_begin.is_success()) {
        return contracts::Result<contracts::ShiftDto>::fail(tx_begin.error_code(), "Error al iniciar transacción de apertura: " + tx_begin.error_message());
    }

    // 1. Verificar si existe la caja
    std::string reg_name;
    {
        auto prep = db_.prepare("SELECT name FROM cash_registers WHERE id = ?;");
        if (!prep.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::ShiftDto>::fail(prep.error_code(), prep.error_message());
        }
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, register_id);
        auto step = stmt.step();
        if (!step.is_success() || !step.value()) {
            stmt.reset();
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::ShiftDto>::fail(
                contracts::ErrorCode::NotFound,
                "Caja física no encontrada"
            );
        }
        reg_name = stmt.get_text(0);
        stmt.reset();
    }

    // 2. Verificar que no haya un turno abierto para esta caja
    {
        auto prep = db_.prepare("SELECT COUNT(*) FROM shifts WHERE register_id = ? AND is_open = 1;");
        if (!prep.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::ShiftDto>::fail(prep.error_code(), prep.error_message());
        }
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, register_id);
        auto step = stmt.step();
        if (step.is_success() && step.value() && stmt.get_int64(0) > 0) {
            stmt.reset();
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::ShiftDto>::fail(
                contracts::ErrorCode::Conflict,
                "Ya existe un turno abierto para esta caja"
            );
        }
        stmt.reset();
    }

    // 3. Insertar el nuevo turno
    std::string shift_id = generate_id("shift");
    const auto ts = domain::now();
    const std::string& now = ts.local;

    {
        auto prep = db_.prepare(
            "INSERT INTO shifts (id, register_id, cashier_user_id, opened_at, opened_at_utc, initial_cash_cents, is_open) "
            "VALUES (?, ?, ?, ?, ?, ?, 1);"
        );
        if (!prep.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::ShiftDto>::fail(prep.error_code(), prep.error_message());
        }
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, shift_id);
        stmt.bind_text(2, register_id);
        stmt.bind_text(3, cur_user.value().id);
        stmt.bind_text(4, now);
        stmt.bind_text(5, ts.utc);
        stmt.bind_int64(6, initial_cash.cents());

        auto step = stmt.step();
        if (!step.is_success()) {
            stmt.reset();
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::ShiftDto>::fail(step.error_code(), step.error_message());
        }
        stmt.reset();
    }

    // 3b. Registrar movimiento en cash_movements
    {
        std::string move_id = generate_id("cm");
        auto prep_cm = db_.prepare(
            "INSERT INTO cash_movements (id, shift_id, movement_type, amount_cents, description, created_at, created_at_utc) "
            "VALUES (?, ?, 'INITIAL_CASH', ?, 'Fondo inicial de turno', ?, ?);"
        );
        if (!prep_cm.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::ShiftDto>::fail(prep_cm.error_code(), prep_cm.error_message());
        }
        auto stmt_cm = std::move(prep_cm.value());
        stmt_cm.bind_text(1, move_id);
        stmt_cm.bind_text(2, shift_id);
        stmt_cm.bind_int64(3, initial_cash.cents());
        stmt_cm.bind_text(4, now);
        stmt_cm.bind_text(5, ts.utc);
        auto step_cm = stmt_cm.step();
        if (!step_cm.is_success()) {
            stmt_cm.reset();
            db_.execute("ROLLBACK;");
            return contracts::Result<contracts::ShiftDto>::fail(step_cm.error_code(), step_cm.error_message());
        }
        stmt_cm.reset();
    }

    // 4. Registrar auditoría conjunta
    auto audit_res = account_service_->record_audit_event(
        "SHIFT_OPENED",
        "Apertura de turno en " + reg_name + " con fondo inicial de " + initial_cash.to_string()
    );
    if (!audit_res.is_success()) {
        db_.execute("ROLLBACK;");
        return contracts::Result<contracts::ShiftDto>::fail(audit_res.error_code(), "Error al registrar auditoría de apertura de turno: " + audit_res.error_message());
    }

    // 5. Confirmar transacción
    auto tx_commit = db_.execute("COMMIT;");
    if (!tx_commit.is_success()) {
        db_.execute("ROLLBACK;");
        return contracts::Result<contracts::ShiftDto>::fail(tx_commit.error_code(), "Error al confirmar transacción de apertura: " + tx_commit.error_message());
    }

    contracts::ShiftDto result;
    result.id = shift_id;
    result.register_id = register_id;
    result.register_name = reg_name;
    result.cashier_user_id = cur_user.value().id;
    result.cashier_name = cur_user.value().display_name;
    result.opened_at = now;
    result.initial_cash = initial_cash;
    result.is_open = true;

    return contracts::Result<contracts::ShiftDto>::ok(result);
}

contracts::OperationResult SqliteShiftService::close_shift(contracts::Money final_cash) {
    auto perm_check = check_permission_or_fail("shift.operate");
    if (!perm_check.is_success()) return perm_check;

    if (final_cash.cents() < 0) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "El saldo final de corte no puede ser negativo"
        );
    }

    // Buscar turno abierto
    auto cur_shift = get_current_shift();
    if (!cur_shift.is_success()) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::NotFound,
            "No hay un turno abierto para cerrar"
        );
    }

    auto tx_begin = db_.execute("BEGIN IMMEDIATE;");
    if (!tx_begin.is_success()) {
        return contracts::OperationResult::fail(tx_begin.error_code(), "Error al iniciar transacción de cierre: " + tx_begin.error_message());
    }

    const auto ts = domain::now();
    const std::string& now = ts.local;
    {
        auto prep = db_.prepare(
            "UPDATE shifts SET is_open = 0, closed_at = ?, closed_at_utc = ?, final_cash_cents = ? WHERE id = ?;"
        );
        if (!prep.is_success()) {
            db_.execute("ROLLBACK;");
            return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
        }
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, now);
        stmt.bind_text(2, ts.utc);
        stmt.bind_int64(3, final_cash.cents());
        stmt.bind_text(4, cur_shift.value().id);

        auto step = stmt.step();
        if (!step.is_success()) {
            stmt.reset();
            db_.execute("ROLLBACK;");
            return contracts::OperationResult::fail(step.error_code(), step.error_message());
        }
        stmt.reset();
    }

    auto audit_res = account_service_->record_audit_event(
        "SHIFT_CLOSED",
        "Cierre de turno " + cur_shift.value().id + " con arqueo final de " + final_cash.to_string()
    );
    if (!audit_res.is_success()) {
        db_.execute("ROLLBACK;");
        return contracts::OperationResult::fail(audit_res.error_code(), "Error al registrar auditoría de cierre: " + audit_res.error_message());
    }

    auto tx_commit = db_.execute("COMMIT;");
    if (!tx_commit.is_success()) {
        db_.execute("ROLLBACK;");
        return contracts::OperationResult::fail(tx_commit.error_code(), "Error al confirmar transacción de cierre: " + tx_commit.error_message());
    }

    return contracts::OperationResult::ok();
}

} // namespace app::services
