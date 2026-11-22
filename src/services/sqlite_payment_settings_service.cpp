#include "app/services/sqlite_payment_settings_service.hpp"
#include "app/domain/card_surcharge.hpp"
#include "app/domain/clock.hpp"

namespace app::services {

using contracts::ErrorCode;

contracts::CardSurchargeConfigDto read_card_surcharge_config(storage::SqliteDatabase& db) {
    contracts::CardSurchargeConfigDto cfg;
    auto prep = db.prepare("SELECT card_surcharge_bp, card_surcharge_fixed_cents, updated_at FROM payment_settings WHERE id = 1;");
    if (!prep.is_success()) return cfg;
    auto st = std::move(prep.value());
    auto step = st.step();
    if (!step.is_success() || !step.value()) return cfg;
    cfg.percent_hundredths = st.get_int64(0);
    cfg.fixed = contracts::Money::from_cents(st.get_int64(1));
    cfg.updated_at = st.is_null(2) ? "" : st.get_text(2);
    return cfg;
}

SqlitePaymentSettingsService::SqlitePaymentSettingsService(
    storage::SqliteDatabase& db, std::shared_ptr<contracts::IAccountService> account_service)
    : db_(db), account_service_(std::move(account_service)) {}

contracts::Result<contracts::CardSurchargeConfigDto> SqlitePaymentSettingsService::get_card_surcharge_config() {
    using R = contracts::Result<contracts::CardSurchargeConfigDto>;
    if (!account_service_ || !account_service_->is_authenticated()) return R::fail(ErrorCode::Forbidden, "Se requiere inicio de sesión");
    if (!account_service_->has_permission("pos.sell") && !account_service_->has_permission("config.manage")) {
        return R::fail(ErrorCode::Forbidden, "No tienes permiso para ver la configuración del cargo por tarjeta.");
    }
    return R::ok(read_card_surcharge_config(db_));
}

contracts::OperationResult SqlitePaymentSettingsService::set_card_surcharge_config(std::int64_t percent_hundredths, contracts::Money fixed) {
    using contracts::OperationResult;
    if (!account_service_ || !account_service_->is_authenticated()) return OperationResult::fail(ErrorCode::Forbidden, "Se requiere inicio de sesión");
    if (!account_service_->has_permission("config.manage")) {
        return OperationResult::fail(ErrorCode::Forbidden, "No tienes permiso para cambiar el cargo por tarjeta.");
    }
    if (percent_hundredths < 0 || percent_hundredths > domain::kMaxCardSurchargeHundredths) {
        return OperationResult::fail(ErrorCode::InvalidInput, "El porcentaje del cargo por tarjeta debe estar entre 0 % y 20.00 %.");
    }
    if (fixed.cents() < 0 || fixed.cents() > domain::kMaxCardSurchargeFixedCents) {
        return OperationResult::fail(ErrorCode::InvalidInput, "El monto fijo del cargo por tarjeta debe estar entre $0.00 y $100.00.");
    }
    auto user = account_service_->current_user();
    if (!user.is_success()) return OperationResult::fail(ErrorCode::Forbidden, "No hay usuario activo");

    auto begin = db_.execute("BEGIN IMMEDIATE;");
    if (!begin.is_success()) return OperationResult::fail(begin.error_code(), "No se pudo guardar la configuración: " + begin.error_message());
    const auto before = read_card_surcharge_config(db_);
    const auto ts = domain::now();
    {
        auto prep = db_.prepare(
            "INSERT INTO payment_settings (id, card_surcharge_bp, card_surcharge_fixed_cents, updated_at, updated_by) VALUES (1, ?, ?, ?, ?) "
            "ON CONFLICT(id) DO UPDATE SET card_surcharge_bp = excluded.card_surcharge_bp, "
            "card_surcharge_fixed_cents = excluded.card_surcharge_fixed_cents, updated_at = excluded.updated_at, updated_by = excluded.updated_by;");
        if (!prep.is_success()) { db_.execute("ROLLBACK;"); return OperationResult::fail(prep.error_code(), prep.error_message()); }
        auto st = std::move(prep.value());
        st.bind_int64(1, percent_hundredths);
        st.bind_int64(2, fixed.cents());
        st.bind_text(3, ts.local);
        st.bind_text(4, user.value().id);
        auto step = st.step();
        if (!step.is_success()) { db_.execute("ROLLBACK;"); return OperationResult::fail(step.error_code(), step.error_message()); }
    }
    auto audit = account_service_->record_audit_event(
        "CARD_SURCHARGE_CONFIGURED",
        "Cargo por tarjeta: antes " + std::to_string(before.percent_hundredths) + " centésimas de punto y " + before.fixed.to_string() +
            "; ahora " + std::to_string(percent_hundredths) + " centésimas de punto y " + fixed.to_string());
    if (!audit.is_success()) {
        db_.execute("ROLLBACK;");
        return OperationResult::fail(audit.error_code(), "No se pudo registrar la configuración en la bitácora: " + audit.error_message());
    }
    auto commit = db_.execute("COMMIT;");
    if (!commit.is_success()) { db_.execute("ROLLBACK;"); return OperationResult::fail(commit.error_code(), commit.error_message()); }
    return OperationResult::ok();
}

} // namespace app::services
