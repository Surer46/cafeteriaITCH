#include "app/services/operating_context.hpp"

namespace app::services {

namespace {
using R = contracts::Result<OperatingContext>;
}

contracts::Result<OperatingContext> resolve_operating_context(
    storage::SqliteDatabase& db,
    const std::string& user_id,
    bool allow_no_shift
) {
    // 1. Turno propio
    {
        auto prep = db.prepare(
            "SELECT id, register_id FROM shifts WHERE is_open = 1 AND cashier_user_id = ? "
            "ORDER BY opened_at DESC, rowid DESC LIMIT 1;");
        if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, user_id);
        auto step = stmt.step();
        if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
        if (step.value()) {
            OperatingContext ctx;
            ctx.shift_id = stmt.get_text(0);
            ctx.register_id = stmt.get_text(1);
            ctx.has_open_shift = true;
            return R::ok(ctx);
        }
    }

    // 2/3. Turnos abiertos de otros usuarios
    std::int64_t open_count = 0;
    OperatingContext only;
    {
        auto prep = db.prepare(
            "SELECT id, register_id FROM shifts WHERE is_open = 1 ORDER BY opened_at DESC, rowid DESC LIMIT 2;");
        if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        while (true) {
            auto step = stmt.step();
            if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
            if (!step.value()) break;
            if (open_count == 0) {
                only.shift_id = stmt.get_text(0);
                only.register_id = stmt.get_text(1);
                only.has_open_shift = true;
            }
            ++open_count;
        }
    }
    if (open_count == 1) return R::ok(only);
    if (open_count > 1) {
        return R::fail(
            contracts::ErrorCode::InvalidState,
            "Hay otros turnos de caja abiertos y usted no tiene uno. Abra su propio turno antes de continuar.");
    }

    // 4. Ningún turno abierto
    if (!allow_no_shift) {
        return R::fail(
            contracts::ErrorCode::ShiftClosed,
            "No hay un turno de caja abierto para registrar ventas. Debe abrir un turno primero.");
    }
    {
        auto prep = db.prepare("SELECT id FROM cash_registers ORDER BY rowid LIMIT 2;");
        if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        int count = 0;
        std::string first;
        while (true) {
            auto step = stmt.step();
            if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
            if (!step.value()) break;
            if (count == 0) first = stmt.get_text(0);
            ++count;
        }
        if (count == 1) {
            OperatingContext ctx;
            ctx.register_id = first;
            return R::ok(ctx);
        }
        if (count == 0) {
            return R::fail(contracts::ErrorCode::InvalidState, "No hay cajas registradas en el sistema.");
        }
    }
    return R::fail(
        contracts::ErrorCode::InvalidState,
        "Abra su turno de caja antes de usar el carrito: hay varias cajas y no se sabe con cuál trabajar.");
}

} // namespace app::services
