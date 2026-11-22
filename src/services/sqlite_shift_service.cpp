#include "app/services/sqlite_shift_service.hpp"
#include "app/services/identifiers.hpp"
#include "app/domain/clock.hpp"
#include "app/services/local_date.hpp"
#include "app/services/shift_totals.hpp"
#include <climits>
#include <cstdint>
#include <vector>
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
    if (!account_service_ || !account_service_->is_authenticated()) {
        return contracts::Result<contracts::ShiftDto>::fail(contracts::ErrorCode::Forbidden, "Se requiere inicio de sesión");
    }
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

namespace {

using contracts::ErrorCode;
using contracts::Money;
using contracts::ShiftCloseoutDto;
using CloseR = contracts::Result<ShiftCloseoutDto>;

bool add_checked(std::int64_t a, std::int64_t b, std::int64_t& out) {
    if (b > 0 && a > INT64_MAX - b) return false;
    if (b < 0 && a < INT64_MIN - b) return false;
    out = a + b;
    return true;
}

std::string trim_copy(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

// Caracteres (no bytes) de un texto UTF-8.
std::size_t utf8_chars(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

bool has_control_chars(const std::string& s) {
    for (unsigned char c : s) if (c < 0x20 || c == 0x7F) return true;
    return false;
}

const char* kNoReasonText = "Sin motivo registrado";

} // namespace

// Importes en vivo de un turno (ventas, pagos, entradas, salidas y reembolsos), todo en centavos enteros.
// Devuelve "" si salió bien o el mensaje de error. El cálculo vive en shift_totals (lo comparte el servicio de reembolsos).
std::string SqliteShiftService::live_totals(const std::string& shift_id, ShiftCloseoutDto& out) {
    ShiftCashTotals t;
    const std::string err = compute_shift_totals(db_, shift_id, t);
    if (!err.empty()) return err;
    out.sales_count = t.sales_count;
    out.net_sales = Money::from_cents(t.net);
    out.discount_total = Money::from_cents(t.discount);
    out.gross_sales = Money::from_cents(t.gross);
    out.cash_sales = Money::from_cents(t.cash_sales);
    out.card_sales = Money::from_cents(t.card_sales);
    out.transfer_sales = Money::from_cents(t.transfer_sales);
    out.card_surcharges = Money::from_cents(t.card_surcharges);
    out.cash_inflows = Money::from_cents(t.inflows);
    out.cash_outflows = Money::from_cents(t.outflows);
    out.cash_refunds = Money::from_cents(t.cash_refunds);
    out.card_refunds = Money::from_cents(t.card_refunds);
    out.transfer_refunds = Money::from_cents(t.transfer_refunds);
    return "";
}

// Lee un turno y arma su conciliación: la instantánea guardada si está cerrado con ella, o los importes en vivo.
CloseR SqliteShiftService::load_closeout(const std::string& shift_id, bool with_movements) {
    ShiftCloseoutDto d;
    bool saved = false;
    std::int64_t snap_count = 0, snap_gross = 0, snap_disc = 0, snap_cash = 0, snap_card = 0, snap_tr = 0, snap_in = 0, snap_out = 0;
    std::int64_t final_cash = 0, expected_saved = 0, diff_saved = 0;
    std::int64_t snap_cash_ref = 0, snap_card_ref = 0, snap_tr_ref = 0, snap_surcharge = 0;
    bool has_final = false;
    {
        auto prep = db_.prepare(
            "SELECT s.id, r.name, s.cashier_user_id, u.display_name, s.opened_at, s.closed_at, s.initial_cash_cents, s.is_open, "
            "s.closed_by_user_id, cu.display_name, s.closeout_saved, s.snap_sales_count, s.snap_gross_cents, s.snap_discount_cents, "
            "s.snap_cash_sales_cents, s.snap_card_sales_cents, s.snap_transfer_sales_cents, s.snap_inflow_cents, s.snap_outflow_cents, "
            "s.final_cash_cents, s.expected_cash_cents, s.difference_cents, s.difference_reason, "
            "s.snap_cash_refunds_cents, s.snap_card_refunds_cents, s.snap_transfer_refunds_cents, s.snap_card_surcharge_cents "
            "FROM shifts s JOIN cash_registers r ON r.id = s.register_id JOIN users u ON u.id = s.cashier_user_id "
            "LEFT JOIN users cu ON cu.id = s.closed_by_user_id WHERE s.id = ?;");
        if (!prep.is_success()) return CloseR::fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        st.bind_text(1, shift_id);
        auto step = st.step();
        if (!step.is_success()) return CloseR::fail(step.error_code(), step.error_message());
        if (!step.value()) return CloseR::fail(ErrorCode::NotFound, "No se encontró ese turno");
        d.shift_id = st.get_text(0);
        d.register_name = st.get_text(1);
        d.opened_by_user_id = st.get_text(2);
        d.opened_by_name = st.get_text(3);
        d.opened_at = st.get_text(4);
        d.closed_at = st.is_null(5) ? "" : st.get_text(5);
        d.initial_cash = Money::from_cents(st.get_int64(6));
        d.is_closed = st.get_int64(7) == 0;
        d.closed_by_user_id = st.is_null(8) ? "" : st.get_text(8);
        d.closed_by_name = st.is_null(9) ? "" : st.get_text(9);
        saved = st.get_int64(10) == 1;
        snap_count = st.get_int64(11); snap_gross = st.get_int64(12); snap_disc = st.get_int64(13);
        snap_cash = st.get_int64(14); snap_card = st.get_int64(15); snap_tr = st.get_int64(16);
        snap_in = st.get_int64(17); snap_out = st.get_int64(18);
        has_final = !st.is_null(19);
        final_cash = has_final ? st.get_int64(19) : 0;
        expected_saved = st.is_null(20) ? 0 : st.get_int64(20);
        diff_saved = st.is_null(21) ? 0 : st.get_int64(21);
        d.difference_reason = st.is_null(22) ? "" : st.get_text(22);
        snap_cash_ref = st.get_int64(23); snap_card_ref = st.get_int64(24); snap_tr_ref = st.get_int64(25); snap_surcharge = st.get_int64(26);
    }

    if (d.is_closed && saved) {
        d.snapshot_saved = true;
        d.sales_count = snap_count;
        d.gross_sales = Money::from_cents(snap_gross);
        d.discount_total = Money::from_cents(snap_disc);
        d.net_sales = Money::from_cents(snap_gross - snap_disc);
        d.cash_sales = Money::from_cents(snap_cash);
        d.card_sales = Money::from_cents(snap_card);
        d.transfer_sales = Money::from_cents(snap_tr);
        d.cash_inflows = Money::from_cents(snap_in);
        d.cash_outflows = Money::from_cents(snap_out);
        d.cash_refunds = Money::from_cents(snap_cash_ref);
        d.card_refunds = Money::from_cents(snap_card_ref);
        d.transfer_refunds = Money::from_cents(snap_tr_ref);
        d.card_surcharges = Money::from_cents(snap_surcharge);
        d.expected_cash = Money::from_cents(expected_saved);
        d.counted_cash = Money::from_cents(final_cash);
        d.difference_cents = diff_saved;
    } else {
        const std::string err = live_totals(shift_id, d);
        if (!err.empty()) return CloseR::fail(ErrorCode::StorageFailure, err);
        std::int64_t exp = d.initial_cash.cents();
        if (!add_checked(exp, d.cash_sales.cents(), exp) || !add_checked(exp, d.cash_inflows.cents(), exp) ||
            !add_checked(exp, -d.cash_outflows.cents(), exp) || !add_checked(exp, -d.cash_refunds.cents(), exp))
            return CloseR::fail(ErrorCode::InvalidInput, "Desbordamiento numérico al calcular el efectivo esperado");
        d.expected_cash = Money::from_cents(exp);
        if (d.is_closed) {   // turno cerrado antes de la migración 020: sin instantánea
            d.counted_cash = Money::from_cents(final_cash);
            d.difference_cents = has_final ? final_cash - exp : 0;
        }
    }
    d.payments_mismatch_cents = d.net_sales.cents() + d.card_surcharges.cents() -
                                (d.cash_sales.cents() + d.card_sales.cents() + d.transfer_sales.cents());

    if (with_movements) {
        auto prep = db_.prepare(
            "SELECT id, movement_type, amount_cents, COALESCE(description, ''), created_at FROM cash_movements "
            "WHERE shift_id = ? AND movement_type IN ('INFLOW', 'OUTFLOW') ORDER BY created_at, rowid;");
        if (!prep.is_success()) return CloseR::fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        st.bind_text(1, shift_id);
        while (true) {
            auto step = st.step();
            if (!step.is_success()) return CloseR::fail(step.error_code(), step.error_message());
            if (!step.value()) break;
            contracts::CashMovementDto m;
            m.id = st.get_text(0);
            m.kind = st.get_text(1) == "INFLOW" ? contracts::CashMovementKind::Inflow : contracts::CashMovementKind::Outflow;
            m.amount = Money::from_cents(st.get_int64(2));
            m.description = st.get_text(3);
            m.created_at = st.get_text(4);
            d.movements.push_back(std::move(m));
        }
    }
    return CloseR::ok(std::move(d));
}

bool SqliteShiftService::is_owner_user(const contracts::UserDto& u) const {
    return u.is_owner;
}

CloseR SqliteShiftService::preview_closeout(const std::string& shift_id) {
    auto perm = check_permission_or_fail("shift.operate");
    if (!perm.is_success()) return CloseR::fail(perm.error_code(), perm.error_message());
    std::string id = shift_id;
    if (id.empty()) {
        auto cur = get_current_shift();
        if (!cur.is_success()) return CloseR::fail(ErrorCode::NotFound, "No hay un turno abierto");
        id = cur.value().id;
    }
    return load_closeout(id, true);
}

CloseR SqliteShiftService::get_closeout(const std::string& shift_id) {
    if (!account_service_ || !account_service_->is_authenticated())
        return CloseR::fail(ErrorCode::Forbidden, "Se requiere inicio de sesión");
    auto user = account_service_->current_user();
    if (!user.is_success()) return CloseR::fail(ErrorCode::Forbidden, "No hay usuario activo");
    auto r = load_closeout(shift_id, true);
    if (!r.is_success()) return r;
    const bool sees_all = is_owner_user(user.value()) || account_service_->has_permission("reports.view");
    const bool operates = account_service_->has_permission("shift.operate");
    const bool mine = r.value().opened_by_user_id == user.value().id;
    const bool allowed = sees_all || (operates && (mine || !r.value().is_closed));
    if (!allowed) return CloseR::fail(ErrorCode::Forbidden, "No tiene permiso para ver el corte de ese turno");
    return r;
}

contracts::Result<contracts::ClosedShiftListDto> SqliteShiftService::list_closed_shifts(
    const std::string& from_date, const std::string& to_date, int limit) {
    using LR = contracts::Result<contracts::ClosedShiftListDto>;
    if (!account_service_ || !account_service_->is_authenticated())
        return LR::fail(ErrorCode::Forbidden, "Se requiere inicio de sesión");
    auto user = account_service_->current_user();
    if (!user.is_success()) return LR::fail(ErrorCode::Forbidden, "No hay usuario activo");
    const bool sees_all = is_owner_user(user.value()) || account_service_->has_permission("reports.view");
    if (!sees_all && !account_service_->has_permission("shift.operate"))
        return LR::fail(ErrorCode::Forbidden, "No tiene permiso para consultar los turnos cerrados");
    std::string start, end;
    const std::string bad = local_date::range_bounds(from_date, to_date, start, end, 3660);
    if (!bad.empty()) return LR::fail(ErrorCode::InvalidInput, bad);
    if (limit <= 0) limit = 100;
    if (limit > 1000) limit = 1000;

    std::vector<std::string> ids;
    {
        std::string sql = "SELECT id FROM shifts WHERE is_open = 0 AND closed_at >= ? AND closed_at < ? ";
        if (!sees_all) sql += "AND cashier_user_id = ? ";
        sql += "ORDER BY closed_at DESC, rowid DESC LIMIT ?;";
        auto prep = db_.prepare(sql);
        if (!prep.is_success()) return LR::fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        int idx = 1;
        st.bind_text(idx++, start);
        st.bind_text(idx++, end);
        if (!sees_all) st.bind_text(idx++, user.value().id);
        st.bind_int64(idx++, static_cast<std::int64_t>(limit) + 1);
        while (true) {
            auto step = st.step();
            if (!step.is_success()) return LR::fail(step.error_code(), step.error_message());
            if (!step.value()) break;
            ids.push_back(st.get_text(0));
        }
    }
    contracts::ClosedShiftListDto out;
    if (ids.size() > static_cast<std::size_t>(limit)) {
        out.truncated = true;
        ids.resize(static_cast<std::size_t>(limit));
    }
    for (const auto& id : ids) {
        auto c = load_closeout(id, false);
        if (!c.is_success()) return LR::fail(c.error_code(), c.error_message());
        out.items.push_back(std::move(c.value()));
    }
    return LR::ok(std::move(out));
}

CloseR SqliteShiftService::close_impl(const std::string& shift_id, Money counted, const std::string& reason_in,
                                      bool default_reason_when_different) {
    auto perm_check = check_permission_or_fail("shift.operate");
    if (!perm_check.is_success()) return CloseR::fail(perm_check.error_code(), perm_check.error_message());
    if (counted.cents() < 0) {
        return CloseR::fail(ErrorCode::InvalidInput, "El saldo final de corte no puede ser negativo");
    }
    auto user = account_service_->current_user();
    if (!user.is_success()) return CloseR::fail(ErrorCode::Forbidden, "No hay usuario activo");

    auto tx_begin = db_.execute("BEGIN IMMEDIATE;");
    if (!tx_begin.is_success()) {
        return CloseR::fail(tx_begin.error_code(), "Error al iniciar transacción de cierre: " + tx_begin.error_message());
    }
    auto fail = [&](ErrorCode code, const std::string& msg) {
        db_.execute("ROLLBACK;");
        return CloseR::fail(code, msg);
    };

    std::string id = shift_id;
    if (id.empty()) {
        auto prep = db_.prepare("SELECT id FROM shifts WHERE is_open = 1 ORDER BY opened_at DESC, rowid DESC LIMIT 1;");
        if (!prep.is_success()) return fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        auto step = st.step();
        if (!step.is_success()) return fail(step.error_code(), step.error_message());
        if (!step.value()) return fail(ErrorCode::NotFound, "No hay un turno abierto para cerrar");
        id = st.get_text(0);
    }

    auto snap = load_closeout(id, false);
    if (!snap.is_success()) return fail(snap.error_code(), snap.error_message());
    ShiftCloseoutDto d = std::move(snap.value());
    if (d.is_closed) {
        return fail(ErrorCode::Conflict, "Ese turno ya fue cerrado. No se puede cerrar dos veces.");
    }

    const bool other = d.opened_by_user_id != user.value().id;
    if (other && !is_owner_user(user.value()) && !account_service_->has_permission("shift.close_others")) {
        return fail(ErrorCode::Forbidden,
                    "Ese turno lo abrió otra persona. Solo quien lo abrió, el Dueño o un rol con permiso para cerrar turnos de otros puede cerrarlo.");
    }

    const std::int64_t diff = counted.cents() - d.expected_cash.cents();
    std::string reason;
    if (diff != 0) {
        reason = trim_copy(reason_in);
        if (reason.empty() && default_reason_when_different) reason = kNoReasonText;
        const std::size_t n = utf8_chars(reason);
        if (n < 3 || n > 200 || has_control_chars(reason)) {
            return fail(ErrorCode::InvalidInput, "Indica el motivo de la diferencia (de 3 a 200 caracteres)");
        }
    }

    const auto ts = domain::now();
    {
        auto prep = db_.prepare(
            "UPDATE shifts SET is_open = 0, closed_at = ?, closed_at_utc = ?, final_cash_cents = ?, closed_by_user_id = ?, "
            "closeout_saved = 1, snap_sales_count = ?, snap_gross_cents = ?, snap_discount_cents = ?, snap_cash_sales_cents = ?, "
            "snap_card_sales_cents = ?, snap_transfer_sales_cents = ?, snap_inflow_cents = ?, snap_outflow_cents = ?, "
            "expected_cash_cents = ?, difference_cents = ?, difference_reason = ?, snap_cash_refunds_cents = ?, snap_card_refunds_cents = ?, "
            "snap_transfer_refunds_cents = ?, snap_card_surcharge_cents = ? WHERE id = ? AND is_open = 1;");
        if (!prep.is_success()) return fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        int i = 1;
        st.bind_text(i++, ts.local);
        st.bind_text(i++, ts.utc);
        st.bind_int64(i++, counted.cents());
        st.bind_text(i++, user.value().id);
        st.bind_int64(i++, d.sales_count);
        st.bind_int64(i++, d.gross_sales.cents());
        st.bind_int64(i++, d.discount_total.cents());
        st.bind_int64(i++, d.cash_sales.cents());
        st.bind_int64(i++, d.card_sales.cents());
        st.bind_int64(i++, d.transfer_sales.cents());
        st.bind_int64(i++, d.cash_inflows.cents());
        st.bind_int64(i++, d.cash_outflows.cents());
        st.bind_int64(i++, d.expected_cash.cents());
        st.bind_int64(i++, diff);
        st.bind_text(i++, reason);
        st.bind_int64(i++, d.cash_refunds.cents());
        st.bind_int64(i++, d.card_refunds.cents());
        st.bind_int64(i++, d.transfer_refunds.cents());
        st.bind_int64(i++, d.card_surcharges.cents());
        st.bind_text(i++, id);
        auto step = st.step();
        if (!step.is_success()) return fail(step.error_code(), step.error_message());
    }

    std::string details = "Cierre de turno " + id + " con arqueo final de " + counted.to_string() +
                          "; esperado " + d.expected_cash.to_string() + ", diferencia " + std::to_string(diff) + " centavos";
    if (!reason.empty()) details += ". Motivo: " + reason;
    if (other) {
        details = "Turno " + id + " abierto por " + d.opened_by_name + " cerrado por " + user.value().display_name +
                  " con arqueo final de " + counted.to_string() + "; esperado " + d.expected_cash.to_string() +
                  ", diferencia " + std::to_string(diff) + " centavos";
        if (!reason.empty()) details += ". Motivo: " + reason;
    }
    auto audit_res = account_service_->record_audit_event(other ? "SHIFT_CLOSED_BY_OTHER" : "SHIFT_CLOSED", details);
    if (!audit_res.is_success()) {
        return fail(audit_res.error_code(), "Error al registrar auditoría de cierre: " + audit_res.error_message());
    }

    auto tx_commit = db_.execute("COMMIT;");
    if (!tx_commit.is_success()) {
        db_.execute("ROLLBACK;");
        return CloseR::fail(tx_commit.error_code(), "Error al confirmar transacción de cierre: " + tx_commit.error_message());
    }

    // El cierre ya está confirmado. Lo que haga el gancho (p. ej. iniciar el respaldo automático) nunca
    // lo revierte ni lo bloquea: se llama dentro de un try y su resultado se ignora.
    if (after_close_hook_) {
        try {
            after_close_hook_(id, user.value().id);
        } catch (...) {
        }
    }
    return load_closeout(id, true);
}

CloseR SqliteShiftService::close_shift_with_count(const std::string& shift_id, Money counted_cash,
                                                  const std::string& difference_reason) {
    return close_impl(shift_id, counted_cash, difference_reason, false);
}

contracts::OperationResult SqliteShiftService::close_shift(contracts::Money final_cash) {
    auto r = close_impl("", final_cash, "", true);
    if (!r.is_success()) return contracts::OperationResult::fail(r.error_code(), r.error_message());
    return contracts::OperationResult::ok();
}

contracts::Result<contracts::CashMovementDto> SqliteShiftService::record_cash_movement(
    contracts::CashMovementKind kind, Money amount, const std::string& description) {
    using MR = contracts::Result<contracts::CashMovementDto>;
    auto perm = check_permission_or_fail("shift.operate");
    if (!perm.is_success()) return MR::fail(perm.error_code(), perm.error_message());
    if (amount.cents() <= 0) return MR::fail(ErrorCode::InvalidInput, "El importe debe ser mayor que cero");
    const std::string desc = trim_copy(description);
    const std::size_t n = utf8_chars(desc);
    if (n < 3 || n > 200 || has_control_chars(desc))
        return MR::fail(ErrorCode::InvalidInput, "Escribe una descripción de 3 a 200 caracteres");

    auto tx_begin = db_.execute("BEGIN IMMEDIATE;");
    if (!tx_begin.is_success())
        return MR::fail(tx_begin.error_code(), "Error al iniciar el movimiento de caja: " + tx_begin.error_message());
    auto fail = [&](ErrorCode code, const std::string& msg) {
        db_.execute("ROLLBACK;");
        return MR::fail(code, msg);
    };

    std::string shift_id;
    {
        auto prep = db_.prepare("SELECT id FROM shifts WHERE is_open = 1 ORDER BY opened_at DESC, rowid DESC LIMIT 1;");
        if (!prep.is_success()) return fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        auto step = st.step();
        if (!step.is_success()) return fail(step.error_code(), step.error_message());
        if (!step.value()) return fail(ErrorCode::NotFound, "Abre un turno antes de registrar movimientos de caja");
        shift_id = st.get_text(0);
    }
    if (kind == contracts::CashMovementKind::Outflow) {
        auto cur = load_closeout(shift_id, false);
        if (!cur.is_success()) return fail(cur.error_code(), cur.error_message());
        if (amount.cents() > cur.value().expected_cash.cents())
            return fail(ErrorCode::InvalidInput, "No hay tanto efectivo en caja para esa salida");
    }

    contracts::CashMovementDto dto;
    dto.id = generate_id("cmov");
    dto.kind = kind;
    dto.amount = amount;
    dto.description = desc;
    const auto ts = domain::now();
    dto.created_at = ts.local;
    {
        auto prep = db_.prepare(
            "INSERT INTO cash_movements (id, shift_id, movement_type, amount_cents, description, created_at, created_at_utc) "
            "VALUES (?, ?, ?, ?, ?, ?, ?);");
        if (!prep.is_success()) return fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        st.bind_text(1, dto.id);
        st.bind_text(2, shift_id);
        st.bind_text(3, kind == contracts::CashMovementKind::Inflow ? "INFLOW" : "OUTFLOW");
        st.bind_int64(4, amount.cents());
        st.bind_text(5, desc);
        st.bind_text(6, ts.local);
        st.bind_text(7, ts.utc);
        auto step = st.step();
        if (!step.is_success()) return fail(step.error_code(), step.error_message());
    }
    auto audit = account_service_->record_audit_event(
        kind == contracts::CashMovementKind::Inflow ? "CASH_INFLOW" : "CASH_OUTFLOW",
        std::string(kind == contracts::CashMovementKind::Inflow ? "Entrada" : "Salida") + " de efectivo de " +
            amount.to_string() + " en el turno " + shift_id + ": " + desc);
    if (!audit.is_success())
        return fail(audit.error_code(), "Error al registrar auditoría del movimiento de caja: " + audit.error_message());
    auto tx_commit = db_.execute("COMMIT;");
    if (!tx_commit.is_success()) {
        db_.execute("ROLLBACK;");
        return MR::fail(tx_commit.error_code(), "Error al confirmar el movimiento de caja: " + tx_commit.error_message());
    }
    return MR::ok(std::move(dto));
}

} // namespace app::services
