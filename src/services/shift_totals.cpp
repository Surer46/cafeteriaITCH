#include "app/services/shift_totals.hpp"

namespace app::services {

namespace {

bool add_checked(std::int64_t a, std::int64_t b, std::int64_t& out) {
    if (b > 0 && a > INT64_MAX - b) return false;
    if (b < 0 && a < INT64_MIN - b) return false;
    out = a + b;
    return true;
}

} // namespace

std::string compute_shift_totals(storage::SqliteDatabase& db, const std::string& shift_id, ShiftCashTotals& out) {
    out = ShiftCashTotals{};
    {
        auto prep = db.prepare("SELECT COUNT(*), COALESCE(SUM(total_cents), 0), COALESCE(SUM(discount_cents), 0), "
                               "COALESCE(SUM(card_surcharge_cents), 0) FROM sales WHERE shift_id = ?;");
        if (!prep.is_success()) return prep.error_message();
        auto st = std::move(prep.value());
        st.bind_text(1, shift_id);
        auto step = st.step();
        if (!step.is_success()) return step.error_message();
        if (step.value()) {
            out.sales_count = st.get_int64(0);
            out.net = st.get_int64(1);
            out.discount = st.get_int64(2);
            out.card_surcharges = st.get_int64(3);
        }
    }
    if (!add_checked(out.net, out.discount, out.gross)) return "Desbordamiento numérico al sumar las ventas del turno";
    {
        auto prep = db.prepare(
            "SELECT p.payment_method, COALESCE(SUM(p.amount_cents), 0) FROM payment_records p "
            "JOIN sales s ON s.id = p.sale_id WHERE s.shift_id = ? GROUP BY p.payment_method;");
        if (!prep.is_success()) return prep.error_message();
        auto st = std::move(prep.value());
        st.bind_text(1, shift_id);
        while (true) {
            auto step = st.step();
            if (!step.is_success()) return step.error_message();
            if (!step.value()) break;
            const std::string m = st.get_text(0);
            if (m == "EFECTIVO") out.cash_sales = st.get_int64(1);
            else if (m == "TARJETA") out.card_sales = st.get_int64(1);
            else if (m == "TRANSFERENCIA") out.transfer_sales = st.get_int64(1);
        }
    }
    {
        auto prep = db.prepare(
            "SELECT movement_type, COALESCE(SUM(amount_cents), 0) FROM cash_movements "
            "WHERE shift_id = ? AND movement_type IN ('INFLOW', 'OUTFLOW') GROUP BY movement_type;");
        if (!prep.is_success()) return prep.error_message();
        auto st = std::move(prep.value());
        st.bind_text(1, shift_id);
        while (true) {
            auto step = st.step();
            if (!step.is_success()) return step.error_message();
            if (!step.value()) break;
            if (st.get_text(0) == "INFLOW") out.inflows = st.get_int64(1);
            else out.outflows = st.get_int64(1);
        }
    }
    {
        auto prep = db.prepare("SELECT payment_method, COALESCE(SUM(amount_cents), 0) FROM refunds WHERE shift_id = ? GROUP BY payment_method;");
        if (!prep.is_success()) return prep.error_message();
        auto st = std::move(prep.value());
        st.bind_text(1, shift_id);
        while (true) {
            auto step = st.step();
            if (!step.is_success()) return step.error_message();
            if (!step.value()) break;
            const std::string m = st.get_text(0);
            if (m == "EFECTIVO") out.cash_refunds = st.get_int64(1);
            else if (m == "TARJETA") out.card_refunds = st.get_int64(1);
            else if (m == "TRANSFERENCIA") out.transfer_refunds = st.get_int64(1);
        }
    }
    return "";
}

bool expected_cash_cents(std::int64_t initial_cash_cents, const ShiftCashTotals& t, std::int64_t& out) {
    std::int64_t v = initial_cash_cents;
    if (!add_checked(v, t.cash_sales, v) || !add_checked(v, t.inflows, v) || !add_checked(v, -t.outflows, v) ||
        !add_checked(v, -t.cash_refunds, v)) {
        return false;
    }
    out = v;
    return true;
}

} // namespace app::services
