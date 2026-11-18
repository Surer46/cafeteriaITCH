#include "app/services/promotion_engine.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <ctime>
#include <map>

namespace app::services {

LocalNow system_local_now() {
    const std::time_t t = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm_local{};
#ifdef _WIN32
    localtime_s(&tm_local, &t);
#else
    localtime_r(&t, &tm_local);
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", tm_local.tm_year + 1900, tm_local.tm_mon + 1, tm_local.tm_mday);
    LocalNow now;
    now.date = buf;
    now.weekday = tm_local.tm_wday == 0 ? 7 : tm_local.tm_wday;  // tm_wday: 0 = domingo
    now.minutes = tm_local.tm_hour * 60 + tm_local.tm_min;
    return now;
}

std::int64_t percent_discount_cents(std::int64_t subtotal_cents, std::int64_t basis_points) {
    if (subtotal_cents <= 0 || basis_points <= 0) return 0;
    if (basis_points > 10000) basis_points = 10000;
    // subtotal = 10000*q + r  =>  floor((subtotal*bp + 5000)/10000) = q*bp + floor((r*bp + 5000)/10000); sin desbordar.
    const std::int64_t q = subtotal_cents / 10000;
    const std::int64_t r = subtotal_cents % 10000;
    return q * basis_points + (r * basis_points + 5000) / 10000;
}

std::int64_t nxm_free_units(std::int64_t quantity, std::int64_t buy_qty, std::int64_t pay_qty) {
    if (quantity <= 0 || buy_qty <= 1 || pay_qty < 0 || pay_qty >= buy_qty) return 0;
    return (quantity / buy_qty) * (buy_qty - pay_qty);
}

bool promotion_in_validity(const std::string& valid_from, const std::string& valid_to,
                           int weekdays_mask, int time_from_min, int time_to_min, const LocalNow& now) {
    if (!valid_from.empty() && now.date < valid_from) return false;   // AAAA-MM-DD se ordena como texto
    if (!valid_to.empty() && now.date > valid_to) return false;
    if (weekdays_mask != 0) {
        if (now.weekday < 1 || now.weekday > 7) return false;
        if ((weekdays_mask & (1 << (now.weekday - 1))) == 0) return false;
    }
    if (time_from_min >= 0 && now.minutes < time_from_min) return false;
    if (time_to_min >= 0 && now.minutes >= time_to_min) return false;
    return true;
}

namespace {

struct Candidate {
    std::string id, name, kind;
    std::int64_t percent_bp{0}, fixed_off{0}, buy_qty{0}, pay_qty{0};
    bool found{false};
};

bool checked_add(std::int64_t a, std::int64_t b, std::int64_t& out) {
    if (b > 0 && a > INT64_MAX - b) return false;
    out = a + b;
    return true;
}
bool checked_mul(std::int64_t a, std::int64_t b, std::int64_t& out) {
    if (a == 0 || b == 0) { out = 0; return true; }
    if (a > 0 && b > 0 && a > INT64_MAX / b) return false;
    out = a * b;
    return true;
}

// La promoción que le toca a un producto: entre las activas y vigentes que lo alcanzan, la de mayor prioridad
// (empate: la creada primero; la creación ya rechaza los empates ambiguos).
bool pick_promotion(storage::SqliteDatabase& db, const LocalNow& now, const std::string& product_id,
                    Candidate& out, std::string& error) {
    out = Candidate{};
    auto prep = db.prepare(
        "SELECT p.id, p.name, p.kind, p.percent_bp, p.fixed_off_cents, p.buy_qty, p.pay_qty, "
        "       p.valid_from, p.valid_to, p.weekdays_mask, p.time_from_min, p.time_to_min "
        "FROM promotions p "
        "WHERE p.is_active = 1 AND EXISTS ("
        "  SELECT 1 FROM promotion_targets t "
        "  WHERE t.promotion_id = p.id AND (t.product_id = ?1 OR t.category = (SELECT category FROM products WHERE id = ?1))) "
        "ORDER BY p.priority DESC, p.rowid ASC;");
    if (!prep.is_success()) { error = prep.error_message(); return false; }
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, product_id);
    while (true) {
        auto step = stmt.step();
        if (!step.is_success()) { error = step.error_message(); return false; }
        if (!step.value()) break;
        const int tf = stmt.is_null(10) ? -1 : static_cast<int>(stmt.get_int64(10));
        const int tt = stmt.is_null(11) ? -1 : static_cast<int>(stmt.get_int64(11));
        if (!promotion_in_validity(stmt.is_null(7) ? "" : stmt.get_text(7), stmt.is_null(8) ? "" : stmt.get_text(8),
                                   static_cast<int>(stmt.get_int64(9)), tf, tt, now)) {
            continue;
        }
        out.id = stmt.get_text(0);
        out.name = stmt.get_text(1);
        out.kind = stmt.get_text(2);
        out.percent_bp = stmt.get_int64(3);
        out.fixed_off = stmt.get_int64(4);
        out.buy_qty = stmt.get_int64(5);
        out.pay_qty = stmt.get_int64(6);
        out.found = true;
        return true;
    }
    return true;
}

std::string params_of(const Candidate& c) {
    if (c.kind == "PERCENT") return "bp=" + std::to_string(c.percent_bp);
    if (c.kind == "FIXED") return "off=" + std::to_string(c.fixed_off);
    return "n=" + std::to_string(c.buy_qty) + ";m=" + std::to_string(c.pay_qty);
}

} // namespace

bool compute_line_discounts(storage::SqliteDatabase& db, const LocalNow& now, const std::vector<PromoLineIn>& lines,
                            std::vector<PromoLineOut>& out, std::string& error) {
    out.assign(lines.size(), PromoLineOut{});
    std::map<std::string, Candidate> chosen;  // por producto
    std::map<std::string, std::int64_t> pooled_qty;  // NxM: piezas del mismo producto de todas las líneas
    for (const auto& l : lines) {
        if (chosen.find(l.product_id) == chosen.end()) {
            Candidate c;
            if (!pick_promotion(db, now, l.product_id, c, error)) return false;
            chosen[l.product_id] = c;
        }
        if (!checked_add(pooled_qty[l.product_id], l.quantity, pooled_qty[l.product_id])) {
            error = "Desbordamiento numérico al contar las piezas de un producto";
            return false;
        }
    }
    std::map<std::string, std::int64_t> free_left;  // NxM: piezas gratis que aún faltan por repartir
    for (const auto& [pid, c] : chosen) {
        if (c.found && c.kind == "NXM") free_left[pid] = nxm_free_units(pooled_qty[pid], c.buy_qty, c.pay_qty);
    }

    for (std::size_t i = 0; i < lines.size(); ++i) {
        const auto& l = lines[i];
        const Candidate& c = chosen[l.product_id];
        if (!c.found || l.quantity <= 0 || l.unit_price_cents < 0) continue;
        std::int64_t subtotal = 0;
        if (!checked_mul(l.quantity, l.unit_price_cents, subtotal)) {
            error = "Desbordamiento numérico al calcular el subtotal de la línea";
            return false;
        }
        std::int64_t discount = 0;
        if (c.kind == "PERCENT") {
            discount = percent_discount_cents(subtotal, c.percent_bp);
        } else if (c.kind == "FIXED") {
            const std::int64_t per_unit = std::min(c.fixed_off, l.unit_price_cents);
            if (!checked_mul(per_unit, l.quantity, discount)) {
                error = "Desbordamiento numérico al calcular el descuento";
                return false;
            }
        } else if (c.kind == "NXM") {
            std::int64_t& left = free_left[l.product_id];
            const std::int64_t take = std::min(left, l.quantity);
            left -= take;
            if (!checked_mul(take, l.unit_price_cents, discount)) {
                error = "Desbordamiento numérico al calcular el descuento";
                return false;
            }
        }
        discount = std::max<std::int64_t>(0, std::min(discount, subtotal));  // el precio nunca baja de cero
        if (discount > 0) {
            out[i].discount_cents = discount;
            out[i].promotion_id = c.id;
            out[i].promotion_name = c.name;
            out[i].kind = c.kind;
            out[i].params = params_of(c);
        }
    }
    return true;
}

} // namespace app::services
