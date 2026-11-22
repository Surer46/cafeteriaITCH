#include "app/services/sqlite_refund_service.hpp"

#include <algorithm>
#include <cstdint>
#include <map>
#include <set>
#include "app/domain/clock.hpp"
#include "app/services/identifiers.hpp"
#include "app/services/shift_totals.hpp"
#include "app/services/stock_ledger.hpp"

// Límites del reembolso (B18, documentados también en la entrega):
//  - Inventario: se usa la receta VIGENTE al reembolsar. La venta no guarda cuánto consumió cada línea, así que si la
//    receta de un producto cambió entre la venta y el reembolso (el mismo día) regresa lo que dice la receta nueva.
//  - Un reembolso en tarjeta o transferencia solo se registra; la devolución real se hace en Mercado Pago.
namespace app::services {

namespace {

using contracts::ErrorCode;
using contracts::Money;

template <typename T>
contracts::Result<T> fail_r(ErrorCode code, const std::string& msg) { return contracts::Result<T>::fail(code, msg); }

contracts::PaymentMethod method_from(const std::string& s) {
    if (s == "TARJETA") return contracts::PaymentMethod::Tarjeta;
    if (s == "TRANSFERENCIA") return contracts::PaymentMethod::Transferencia;
    return contracts::PaymentMethod::Efectivo;
}

std::string trim_copy(const std::string& s) {
    std::size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' || s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

std::size_t utf8_chars(const std::string& s) {
    std::size_t n = 0;
    for (unsigned char c : s) if ((c & 0xC0) != 0x80) ++n;
    return n;
}

bool has_control_chars(const std::string& s) {
    for (unsigned char c : s) if (c < 0x20 || c == 0x7F) return true;
    return false;
}

// Importe acumulado de las primeras n piezas de una línea de neto `net` y cantidad `q`:
// floor(net * n / q) y, con las q piezas, el neto completo. El importe de las piezas r+1..r+k es
// acum(r + k) - acum(r): el residuo por redondeo cae en la última pieza y nunca se supera el neto de la línea.
std::int64_t cumulative_refund(std::int64_t net, std::int64_t q, std::int64_t n) {
    if (q <= 0 || n <= 0) return 0;
    if (n >= q) return net;
    return static_cast<std::int64_t>((static_cast<__int128>(net) * n) / q);
}

} // namespace

struct SqliteRefundService::SaleRow {
    std::string id, folio, created_at, cashier_name, method;
    std::int64_t total_cents{0}, discount_cents{0}, surcharge_cents{0};
};

struct SqliteRefundService::LineRow {
    std::string id, product_id, product_name, promotion_name;
    std::int64_t quantity{0}, unit_price{0}, subtotal{0}, discount{0};
    bool is_prepared{false}, is_combo{false};
    std::int64_t refunded_qty{0}, refunded_amount{0};
};

namespace {

bool read_sale(storage::SqliteDatabase& db, const std::string& folio, std::string& sale_id_out, std::string& err,
               std::string& folio_out, std::string& created_at, std::string& cashier, std::string& method,
               std::int64_t& total, std::int64_t& discount, std::int64_t& surcharge) {
    auto prep = db.prepare(
        "SELECT s.id, s.folio, s.created_at, COALESCE(NULLIF(s.cashier_name, ''), ''), "
        "COALESCE((SELECT p.payment_method FROM payment_records p WHERE p.sale_id = s.id LIMIT 1), 'EFECTIVO'), "
        "s.total_cents, s.discount_cents, s.card_surcharge_cents FROM sales s WHERE s.folio = ?;");
    if (!prep.is_success()) { err = prep.error_message(); return false; }
    auto st = std::move(prep.value());
    st.bind_text(1, folio);
    auto step = st.step();
    if (!step.is_success()) { err = step.error_message(); return false; }
    if (!step.value()) { err = ""; return false; }
    sale_id_out = st.get_text(0);
    folio_out = st.get_text(1);
    created_at = st.get_text(2);
    cashier = st.get_text(3);
    method = st.get_text(4);
    total = st.get_int64(5);
    discount = st.get_int64(6);
    surcharge = st.get_int64(7);
    return true;
}

} // namespace

SqliteRefundService::SqliteRefundService(storage::SqliteDatabase& db, std::shared_ptr<contracts::IAccountService> account_service)
    : db_(db), account_service_(std::move(account_service)) {}

bool SqliteRefundService::load_refund(const std::string& refund_id, contracts::RefundDto& out) {
    auto prep = db_.prepare(
        "SELECT r.id, r.folio, s.folio, r.created_at, r.user_name, r.payment_method, r.amount_cents, r.reason, s.card_surcharge_cents "
        "FROM refunds r JOIN sales s ON s.id = r.sale_id WHERE r.id = ?;");
    if (!prep.is_success()) return false;
    auto st = std::move(prep.value());
    st.bind_text(1, refund_id);
    auto step = st.step();
    if (!step.is_success() || !step.value()) return false;
    out = contracts::RefundDto{};
    out.refund_id = st.get_text(0);
    out.refund_folio = st.get_text(1);
    out.sale_folio = st.get_text(2);
    out.created_at = st.get_text(3);
    out.user_name = st.get_text(4);
    out.payment_method = method_from(st.get_text(5));
    out.amount = Money::from_cents(st.get_int64(6));
    out.reason = st.get_text(7);
    if (out.payment_method == contracts::PaymentMethod::Tarjeta) out.card_surcharge_not_refunded = Money::from_cents(st.get_int64(8));
    auto lp = db_.prepare("SELECT sale_item_id, product_name, quantity, amount_cents, already_prepared FROM refund_items WHERE refund_id = ? ORDER BY rowid;");
    if (!lp.is_success()) return false;
    auto ls = std::move(lp.value());
    ls.bind_text(1, refund_id);
    while (true) {
        auto s2 = ls.step();
        if (!s2.is_success()) return false;
        if (!s2.value()) break;
        contracts::RefundLineDto l;
        l.sale_item_id = ls.get_text(0);
        l.product_name = ls.get_text(1);
        l.quantity = ls.get_int64(2);
        l.amount = Money::from_cents(ls.get_int64(3));
        l.already_prepared = ls.get_int64(4) != 0;
        out.lines.push_back(std::move(l));
    }
    return true;
}

bool SqliteRefundService::load_refunds_of_sale(const std::string& sale_id, std::vector<contracts::RefundDto>& out) {
    std::vector<std::string> ids;
    {
        auto prep = db_.prepare("SELECT id FROM refunds WHERE sale_id = ? ORDER BY created_at, rowid;");
        if (!prep.is_success()) return false;
        auto st = std::move(prep.value());
        st.bind_text(1, sale_id);
        while (true) {
            auto step = st.step();
            if (!step.is_success()) return false;
            if (!step.value()) break;
            ids.push_back(st.get_text(0));
        }
    }
    for (const auto& id : ids) {
        contracts::RefundDto d;
        if (!load_refund(id, d)) return false;
        out.push_back(std::move(d));
    }
    return true;
}

contracts::Result<contracts::SaleDetailDto> SqliteRefundService::get_sale_for_refund(const std::string& folio) {
    using R = contracts::Result<contracts::SaleDetailDto>;
    if (!account_service_ || !account_service_->is_authenticated()) return R::fail(ErrorCode::Forbidden, "Se requiere inicio de sesión");
    if (!account_service_->has_permission("pos.refund")) return R::fail(ErrorCode::Forbidden, "No tienes permiso para hacer reembolsos.");

    contracts::SaleDetailDto d;
    std::string err, created_at, cashier, method, sale_id, sale_folio;
    std::int64_t total = 0, discount = 0, surcharge = 0;
    if (!read_sale(db_, folio, sale_id, err, sale_folio, created_at, cashier, method, total, discount, surcharge)) {
        if (!err.empty()) return R::fail(ErrorCode::StorageFailure, err);
        return R::fail(ErrorCode::NotFound, "No se encontró la venta con el folio " + folio + ".");
    }
    d.sale_id = sale_id;
    d.folio = sale_folio;
    d.created_at = created_at;
    d.cashier_name = cashier;
    d.payment_method = method_from(method);
    d.products_total = Money::from_cents(total);
    d.discount = Money::from_cents(discount);
    d.card_surcharge = Money::from_cents(surcharge);
    d.charged_total = Money::from_cents(total + surcharge);

    {
        auto prep = db_.prepare(
            "SELECT si.id, si.product_id, si.product_name, si.quantity, si.unit_price_cents, si.subtotal_cents, si.discount_cents, "
            "si.promotion_name, COALESCE(p.is_prepared, 0), COALESCE(p.is_combo, 0), "
            "COALESCE((SELECT SUM(ri.quantity) FROM refund_items ri WHERE ri.sale_item_id = si.id), 0), "
            "COALESCE((SELECT SUM(ri.amount_cents) FROM refund_items ri WHERE ri.sale_item_id = si.id), 0) "
            "FROM sale_items si LEFT JOIN products p ON p.id = si.product_id WHERE si.sale_id = ? ORDER BY si.rowid;");
        if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        st.bind_text(1, sale_id);
        while (true) {
            auto step = st.step();
            if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
            if (!step.value()) break;
            contracts::SaleLineDetailDto l;
            l.sale_item_id = st.get_text(0);
            l.product_id = st.get_text(1);
            l.product_name = st.get_text(2);
            l.quantity_sold = st.get_int64(3);
            l.unit_price = Money::from_cents(st.get_int64(4));
            l.subtotal = Money::from_cents(st.get_int64(5));
            l.discount = Money::from_cents(st.get_int64(6));
            l.promotion_name = st.get_text(7);
            l.is_prepared = st.get_int64(8) != 0;
            l.is_combo = st.get_int64(9) != 0;
            l.quantity_refunded = st.get_int64(10);
            l.quantity_refundable = l.quantity_sold - l.quantity_refunded;
            l.net_paid = Money::from_cents(st.get_int64(5) - st.get_int64(6));
            l.net_refunded = Money::from_cents(st.get_int64(11));
            d.lines.push_back(std::move(l));
        }
    }
    if (!load_refunds_of_sale(sale_id, d.refunds)) return R::fail(ErrorCode::StorageFailure, "No se pudieron leer los reembolsos de la venta.");
    for (const auto& r : d.refunds) d.refunded_total = d.refunded_total + r.amount;

    const std::string today = domain::now().local.substr(0, 10);
    bool anything = false;
    for (const auto& l : d.lines) if (l.quantity_refundable > 0) anything = true;
    if (created_at.substr(0, 10) != today) {
        d.cannot_refund_reason = "Solo se pueden reembolsar ventas del mismo día. Esta venta es del " + created_at.substr(0, 10) + ".";
    } else if (!anything) {
        d.cannot_refund_reason = "Esta venta ya se reembolsó por completo.";
    } else {
        d.can_refund = true;
    }
    return R::ok(std::move(d));
}

contracts::Result<contracts::RefundDto> SqliteRefundService::refund_sale(const contracts::RefundRequestDto& req) {
    using R = contracts::Result<contracts::RefundDto>;
    if (!account_service_ || !account_service_->is_authenticated()) return R::fail(ErrorCode::Forbidden, "Se requiere inicio de sesión");
    if (!account_service_->has_permission("pos.refund")) return R::fail(ErrorCode::Forbidden, "No tienes permiso para hacer reembolsos.");
    auto user = account_service_->current_user();
    if (!user.is_success()) return R::fail(ErrorCode::Forbidden, "No hay usuario activo.");

    const std::string reason = trim_copy(req.reason);
    if (utf8_chars(reason) < 3 || utf8_chars(reason) > 200 || has_control_chars(reason)) {
        return R::fail(ErrorCode::InvalidInput, "Escribe el motivo del reembolso (de 3 a 200 caracteres).");
    }
    if (req.folio.empty()) return R::fail(ErrorCode::InvalidInput, "Indica el folio de la venta.");
    if (req.lines.empty()) return R::fail(ErrorCode::InvalidInput, "Elige al menos una pieza para reembolsar.");
    // Una misma línea repetida se junta; cada cantidad debe ser positiva.
    std::map<std::string, std::int64_t> wanted_qty;
    std::map<std::string, bool> wanted_prepared;
    for (const auto& l : req.lines) {
        if (l.quantity <= 0) return R::fail(ErrorCode::InvalidInput, "La cantidad a reembolsar debe ser de al menos 1 pieza.");
        if (l.quantity > 1000000) return R::fail(ErrorCode::InvalidInput, "La cantidad a reembolsar es demasiado grande.");
        wanted_qty[l.sale_item_id] += l.quantity;
        wanted_prepared[l.sale_item_id] = wanted_prepared[l.sale_item_id] || l.already_prepared;
    }

    // Idempotencia previa: la misma clave devuelve el mismo reembolso
    auto existing_by_key = [&]() -> contracts::Result<contracts::RefundDto> {
        auto prep = db_.prepare("SELECT r.id, s.folio FROM refunds r JOIN sales s ON s.id = r.sale_id WHERE r.idempotency_key = ?;");
        if (!prep.is_success()) return fail_r<contracts::RefundDto>(ErrorCode::NotFound, "");
        auto st = std::move(prep.value());
        st.bind_text(1, req.idempotency_key);
        auto step = st.step();
        if (!step.is_success() || !step.value()) return fail_r<contracts::RefundDto>(ErrorCode::NotFound, "");
        const std::string rid = st.get_text(0), sale_folio = st.get_text(1);
        if (sale_folio != req.folio) {
            return fail_r<contracts::RefundDto>(ErrorCode::Conflict, "Esa clave ya se usó en el reembolso de otra venta.");
        }
        contracts::RefundDto dto;
        if (!load_refund(rid, dto)) return fail_r<contracts::RefundDto>(ErrorCode::StorageFailure, "No se pudo leer el reembolso.");
        return R::ok(std::move(dto));
    };
    if (!req.idempotency_key.empty()) {
        auto prior = existing_by_key();
        if (prior.is_success() || prior.error_code() != ErrorCode::NotFound) return prior;
    }

    auto begin = db_.execute("BEGIN IMMEDIATE;");
    if (!begin.is_success()) return R::fail(begin.error_code(), "No se pudo iniciar el reembolso: " + begin.error_message());
    auto fail = [&](ErrorCode code, const std::string& msg) {
        db_.execute("ROLLBACK;");
        return R::fail(code, msg);
    };
    // Otro reintento con la misma clave pudo terminar mientras esperábamos
    if (!req.idempotency_key.empty()) {
        auto prior = existing_by_key();
        if (prior.is_success()) { db_.execute("ROLLBACK;"); return prior; }
        if (prior.error_code() != ErrorCode::NotFound) { db_.execute("ROLLBACK;"); return prior; }
    }

    // Todo lo que decide el reembolso se lee DENTRO de la transacción (lo reembolsable y el efectivo esperado).
    std::string err, created_at, cashier, method_s, sale_id, sale_folio;
    std::int64_t total = 0, discount = 0, surcharge = 0;
    if (!read_sale(db_, req.folio, sale_id, err, sale_folio, created_at, cashier, method_s, total, discount, surcharge)) {
        if (!err.empty()) return fail(ErrorCode::StorageFailure, err);
        return fail(ErrorCode::NotFound, "No se encontró la venta con el folio " + req.folio + ".");
    }
    const auto ts = domain::now();
    if (created_at.substr(0, 10) != ts.local.substr(0, 10)) {
        return fail(ErrorCode::InvalidInput, "Solo se pueden reembolsar ventas del mismo día. Esta venta es del " + created_at.substr(0, 10) + ".");
    }

    std::vector<SqliteRefundService::LineRow> lines;
    {
        auto prep = db_.prepare(
            "SELECT si.id, si.product_id, si.product_name, si.quantity, si.unit_price_cents, si.subtotal_cents, si.discount_cents, "
            "COALESCE(p.is_prepared, 0), COALESCE(p.is_combo, 0), "
            "COALESCE((SELECT SUM(ri.quantity) FROM refund_items ri WHERE ri.sale_item_id = si.id), 0), "
            "COALESCE((SELECT SUM(ri.amount_cents) FROM refund_items ri WHERE ri.sale_item_id = si.id), 0) "
            "FROM sale_items si LEFT JOIN products p ON p.id = si.product_id WHERE si.sale_id = ? ORDER BY si.rowid;");
        if (!prep.is_success()) return fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        st.bind_text(1, sale_id);
        while (true) {
            auto step = st.step();
            if (!step.is_success()) return fail(step.error_code(), step.error_message());
            if (!step.value()) break;
            SqliteRefundService::LineRow l;
            l.id = st.get_text(0); l.product_id = st.get_text(1); l.product_name = st.get_text(2);
            l.quantity = st.get_int64(3); l.unit_price = st.get_int64(4); l.subtotal = st.get_int64(5); l.discount = st.get_int64(6);
            l.is_prepared = st.get_int64(7) != 0; l.is_combo = st.get_int64(8) != 0;
            l.refunded_qty = st.get_int64(9); l.refunded_amount = st.get_int64(10);
            lines.push_back(std::move(l));
        }
    }

    struct Chosen { const SqliteRefundService::LineRow* line; std::int64_t qty; std::int64_t amount; bool prepared; };
    std::vector<Chosen> chosen;
    std::int64_t refund_total = 0;
    for (const auto& [item_id, qty] : wanted_qty) {
        const SqliteRefundService::LineRow* found = nullptr;
        for (const auto& l : lines) if (l.id == item_id) found = &l;
        if (!found) return fail(ErrorCode::InvalidInput, "Una de las líneas elegidas no pertenece a la venta " + sale_folio + ".");
        const std::int64_t refundable = found->quantity - found->refunded_qty;
        if (qty > refundable) {
            return fail(ErrorCode::InvalidInput,
                        "No se pueden reembolsar " + std::to_string(qty) + " pieza(s) de " + found->product_name + ": " +
                            (refundable > 0 ? "solo quedan " + std::to_string(refundable) + " por reembolsar." : "ya se reembolsó todo lo vendido."));
        }
        const std::int64_t net = found->subtotal - found->discount;
        const std::int64_t amount = cumulative_refund(net, found->quantity, found->refunded_qty + qty) -
                                    cumulative_refund(net, found->quantity, found->refunded_qty);
        refund_total += amount;
        chosen.push_back(Chosen{found, qty, amount, wanted_prepared[item_id]});
    }
    if (refund_total <= 0) {
        return fail(ErrorCode::InvalidInput, "Esas piezas se vendieron sin costo; no hay importe que reembolsar.");
    }

    // Dinero: en efectivo sale de la caja del turno abierto
    const contracts::PaymentMethod method = method_from(method_s);
    std::string shift_id;
    std::int64_t initial_cash = 0;
    {
        auto prep = db_.prepare("SELECT id, initial_cash_cents FROM shifts WHERE is_open = 1 ORDER BY opened_at DESC, rowid DESC LIMIT 1;");
        if (!prep.is_success()) return fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        auto step = st.step();
        if (!step.is_success()) return fail(step.error_code(), step.error_message());
        if (step.value()) { shift_id = st.get_text(0); initial_cash = st.get_int64(1); }
    }
    if (method == contracts::PaymentMethod::Efectivo) {
        if (shift_id.empty()) return fail(ErrorCode::Conflict, "Para reembolsar en efectivo primero abre un turno.");
        ShiftCashTotals totals;
        const std::string terr = compute_shift_totals(db_, shift_id, totals);
        std::int64_t expected = 0;
        if (!terr.empty() || !expected_cash_cents(initial_cash, totals, expected)) {
            return fail(ErrorCode::StorageFailure, "No se pudo calcular el efectivo en caja.");
        }
        if (refund_total > expected) {
            return fail(ErrorCode::InvalidInput, "No hay tanto efectivo en caja para este reembolso. En caja hay " + Money::from_cents(expected).to_string() + ".");
        }
    }

    // Folio y registro del reembolso
    std::int64_t n_prev = 0;
    {
        auto prep = db_.prepare("SELECT COUNT(*) FROM refunds WHERE sale_id = ?;");
        if (!prep.is_success()) return fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        st.bind_text(1, sale_id);
        auto step = st.step();
        if (step.is_success() && step.value()) n_prev = st.get_int64(0);
    }
    const std::string refund_id = generate_id("refund");
    const std::string refund_folio = sale_folio + "-R" + std::to_string(n_prev + 1);
    {
        auto prep = db_.prepare(
            "INSERT INTO refunds (id, folio, sale_id, shift_id, user_id, user_name, payment_method, amount_cents, reason, idempotency_key, created_at, created_at_utc) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
        if (!prep.is_success()) return fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        st.bind_text(1, refund_id);
        st.bind_text(2, refund_folio);
        st.bind_text(3, sale_id);
        if (shift_id.empty()) st.bind_null(4); else st.bind_text(4, shift_id);
        st.bind_text(5, user.value().id);
        st.bind_text(6, user.value().display_name);
        st.bind_text(7, contracts::to_string(method));
        st.bind_int64(8, refund_total);
        st.bind_text(9, reason);
        if (req.idempotency_key.empty()) st.bind_null(10); else st.bind_text(10, req.idempotency_key);
        st.bind_text(11, ts.local);
        st.bind_text(12, ts.utc);
        auto step = st.step();
        if (!step.is_success()) return fail(step.error_code(), step.error_message());
    }
    for (const auto& c : chosen) {
        auto prep = db_.prepare("INSERT INTO refund_items (id, refund_id, sale_item_id, product_id, product_name, quantity, amount_cents, already_prepared) VALUES (?, ?, ?, ?, ?, ?, ?, ?);");
        if (!prep.is_success()) return fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        st.bind_text(1, generate_id("refitem"));
        st.bind_text(2, refund_id);
        st.bind_text(3, c.line->id);
        st.bind_text(4, c.line->product_id);
        st.bind_text(5, c.line->product_name);
        st.bind_int64(6, c.qty);
        st.bind_int64(7, c.amount);
        st.bind_int64(8, c.prepared ? 1 : 0);
        auto step = st.step();
        if (!step.is_success()) return fail(step.error_code(), step.error_message());
    }

    // Inventario (receta vigente): directo y no preparado regresan; preparado ya preparado se registra como merma.
    std::map<std::string, std::int64_t> to_return, to_waste;
    auto add_recipe = [&](const std::string& product_id, std::int64_t multiplier, bool waste) -> bool {
        auto prep = db_.prepare(
            "SELECT ri.ingredient_id, ri.quantity_base_units FROM recipes r JOIN recipe_items ri ON ri.recipe_id = r.id "
            "WHERE r.id = (SELECT id FROM recipes WHERE product_id = ? ORDER BY version DESC LIMIT 1);");
        if (!prep.is_success()) return false;
        auto st = std::move(prep.value());
        st.bind_text(1, product_id);
        while (true) {
            auto step = st.step();
            if (!step.is_success()) return false;
            if (!step.value()) break;
            const std::int64_t per = st.get_int64(1);
            if (per > 0 && multiplier > INT64_MAX / per) return false;
            auto& dst = waste ? to_waste : to_return;
            dst[st.get_text(0)] += per * multiplier;
        }
        return true;
    };
    for (const auto& c : chosen) {
        if (c.line->is_combo) {
            struct Part { std::string product_id; std::int64_t qty; bool prepared; };
            std::vector<Part> parts;
            {
                auto prep = db_.prepare("SELECT ci.component_product_id, ci.quantity, COALESCE(p.is_prepared, 0) FROM combo_items ci "
                                        "LEFT JOIN products p ON p.id = ci.component_product_id WHERE ci.combo_product_id = ? ORDER BY ci.rowid;");
                if (!prep.is_success()) return fail(prep.error_code(), prep.error_message());
                auto st = std::move(prep.value());
                st.bind_text(1, c.line->product_id);
                while (true) {
                    auto step = st.step();
                    if (!step.is_success()) return fail(step.error_code(), step.error_message());
                    if (!step.value()) break;
                    parts.push_back(Part{st.get_text(0), st.get_int64(1), st.get_int64(2) != 0});
                }
            }
            for (const auto& part : parts) {
                if (!add_recipe(part.product_id, part.qty * c.qty, part.prepared && c.prepared))
                    return fail(ErrorCode::InvalidInput, "No se pudo calcular el inventario de las partes del combo " + c.line->product_name + ".");
            }
        } else if (!add_recipe(c.line->product_id, c.qty, c.line->is_prepared && c.prepared)) {
            return fail(ErrorCode::InvalidInput, "No se pudo calcular el inventario de " + c.line->product_name + ".");
        }
    }
    auto insert_movement = [&](const std::string& ing, const char* type, std::int64_t qty, const std::string& why) -> contracts::OperationResult {
        auto prep = db_.prepare("INSERT INTO stock_movements (id, ingredient_id, movement_type, quantity_base_units, reason, created_at, created_at_utc, actor_user_id, refund_id) "
                                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?);");
        if (!prep.is_success()) return contracts::OperationResult::fail(prep.error_code(), prep.error_message());
        auto st = std::move(prep.value());
        st.bind_text(1, generate_id("mov"));
        st.bind_text(2, ing);
        st.bind_text(3, type);
        st.bind_int64(4, qty);
        st.bind_text(5, why);
        st.bind_text(6, ts.local);
        st.bind_text(7, ts.utc);
        st.bind_text(8, user.value().id);
        st.bind_text(9, refund_id);
        auto step = st.step();
        if (!step.is_success()) return contracts::OperationResult::fail(step.error_code(), step.error_message());
        return contracts::OperationResult::ok();
    };
    for (const auto& [ing, qty] : to_return) {
        if (qty <= 0) continue;
        auto mv = insert_movement(ing, "ADJUSTMENT", qty, "Reembolso " + sale_folio);
        if (!mv.is_success()) return fail(mv.error_code(), mv.error_message());
        auto led = apply_stock_change(db_, ing, StockChangeKind::InflowAdjustment, qty);
        if (!led.is_success()) return fail(led.error_code(), led.error_message());
    }
    for (const auto& [ing, qty] : to_waste) {
        if (qty <= 0) continue;
        // Los ingredientes de lo ya preparado se quedan consumidos: la existencia y el costo promedio no cambian.
        // Quedan dos movimientos que se compensan (el ajuste marca el reembolso; la merma lleva el motivo).
        auto m1 = insert_movement(ing, "ADJUSTMENT", qty, "Reembolso " + sale_folio + ": se registra como merma");
        if (!m1.is_success()) return fail(m1.error_code(), m1.error_message());
        auto m2 = insert_movement(ing, "WASTE", -qty, "Reembolso " + sale_folio + ": " + reason);
        if (!m2.is_success()) return fail(m2.error_code(), m2.error_message());
    }

    // Auditoría
    {
        std::string lines_txt;
        for (std::size_t i = 0; i < chosen.size(); ++i) {
            lines_txt += (i ? "; " : "") + std::to_string(chosen[i].qty) + "x " + chosen[i].line->product_name + " (" + Money::from_cents(chosen[i].amount).to_string() + ")";
        }
        auto audit = account_service_->record_audit_event(
            "SALE_REFUNDED",
            "Reembolso " + refund_folio + " de la venta " + sale_folio + " [" + contracts::to_string(method) + "] por " +
                Money::from_cents(refund_total).to_string() + ": " + lines_txt + ". Motivo: " + reason);
        if (!audit.is_success()) return fail(audit.error_code(), "No se pudo registrar el reembolso en la bitácora: " + audit.error_message());
    }

    auto commit = db_.execute("COMMIT;");
    if (!commit.is_success()) {
        db_.execute("ROLLBACK;");
        return R::fail(commit.error_code(), "No se pudo confirmar el reembolso: " + commit.error_message());
    }
    contracts::RefundDto dto;
    if (!load_refund(refund_id, dto)) return R::fail(ErrorCode::StorageFailure, "El reembolso se guardó, pero no se pudo leer.");
    return R::ok(std::move(dto));
}

contracts::Result<std::vector<contracts::RefundDto>> SqliteRefundService::list_refunds(const std::string& sale_folio) {
    using R = contracts::Result<std::vector<contracts::RefundDto>>;
    if (!account_service_ || !account_service_->is_authenticated()) return R::fail(ErrorCode::Forbidden, "Se requiere inicio de sesión");
    if (!account_service_->has_permission("pos.refund") && !account_service_->has_permission("pos.reprint")) {
        return R::fail(ErrorCode::Forbidden, "No tienes permiso para ver los reembolsos.");
    }
    std::string err, created_at, cashier, method_s, sale_id, folio_out;
    std::int64_t total = 0, discount = 0, surcharge = 0;
    if (!read_sale(db_, sale_folio, sale_id, err, folio_out, created_at, cashier, method_s, total, discount, surcharge)) {
        if (!err.empty()) return R::fail(ErrorCode::StorageFailure, err);
        return R::fail(ErrorCode::NotFound, "No se encontró la venta con el folio " + sale_folio + ".");
    }
    std::vector<contracts::RefundDto> out;
    if (!load_refunds_of_sale(sale_id, out)) return R::fail(ErrorCode::StorageFailure, "No se pudieron leer los reembolsos.");
    return R::ok(std::move(out));
}

contracts::Result<contracts::RefundDto> SqliteRefundService::get_refund(const std::string& refund_id) {
    using R = contracts::Result<contracts::RefundDto>;
    if (!account_service_ || !account_service_->is_authenticated()) return R::fail(ErrorCode::Forbidden, "Se requiere inicio de sesión");
    if (!account_service_->has_permission("pos.refund") && !account_service_->has_permission("pos.reprint")) {
        return R::fail(ErrorCode::Forbidden, "No tienes permiso para ver los reembolsos.");
    }
    contracts::RefundDto dto;
    if (!load_refund(refund_id, dto)) return R::fail(ErrorCode::NotFound, "No se encontró ese reembolso.");
    return R::ok(std::move(dto));
}

} // namespace app::services
