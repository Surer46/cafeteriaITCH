#include "app/services/sqlite_promotion_service.hpp"

#include <algorithm>
#include <cstdio>
#include <set>

#include "app/domain/clock.hpp"
#include "app/services/identifiers.hpp"

namespace app::services {

using contracts::ErrorCode;
using contracts::OperationResult;

struct SqlitePromotionService::Row {
    std::string id, name, kind, scope_kind;
    std::int64_t percent_bp{0}, fixed_off{0}, buy_qty{0}, pay_qty{0};
    int priority{0};
    bool is_active{true};
    std::string valid_from, valid_to;
    int weekdays_mask{0};
    int time_from_min{-1}, time_to_min{-1};
    std::vector<std::string> product_ids;
    std::string category;
};

namespace {

const char* const kWeekdayNames[7] = {"lunes", "martes", "miércoles", "jueves", "viernes", "sábado", "domingo"};

bool leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

bool valid_date(const std::string& s) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    const int y = std::stoi(s.substr(0, 4)), m = std::stoi(s.substr(5, 2)), d = std::stoi(s.substr(8, 2));
    if (y < 2000 || m < 1 || m > 12 || d < 1) return false;
    static const int dim[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return d <= dim[m - 1] + ((m == 2 && leap(y)) ? 1 : 0);
}

// "HH:MM" -> minutos desde medianoche; -1 si es inválida.
int parse_hhmm(const std::string& s) {
    if (s.size() != 5 || s[2] != ':') return -1;
    for (int i : {0, 1, 3, 4}) if (s[i] < '0' || s[i] > '9') return -1;
    const int h = std::stoi(s.substr(0, 2)), m = std::stoi(s.substr(3, 2));
    if (h > 23 || m > 59) return -1;
    return h * 60 + m;
}

std::string hhmm(int minutes) {
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", minutes / 60, minutes % 60);
    return buf;
}

std::string display_date(const std::string& iso) {  // AAAA-MM-DD -> DD/MM/AAAA
    return iso.substr(8, 2) + "/" + iso.substr(5, 2) + "/" + iso.substr(0, 4);
}

std::string percent_text(std::int64_t bp) {
    const std::int64_t whole = bp / 100, frac = bp % 100;
    if (frac == 0) return std::to_string(whole);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%lld.%02lld", static_cast<long long>(whole), static_cast<long long>(frac));
    std::string s = buf;
    if (s.back() == '0') s.pop_back();
    return s;
}

std::string capitalize(std::string s) {
    if (!s.empty() && s[0] >= 'a' && s[0] <= 'z') s[0] = static_cast<char>(s[0] - 'a' + 'A');
    return s;
}

std::vector<int> mask_to_weekdays(int mask) {
    std::vector<int> days;
    if (mask == 0) return days;
    for (int d = 1; d <= 7; ++d) if (mask & (1 << (d - 1))) days.push_back(d);
    if (days.size() == 7) days.clear();
    return days;
}

std::string summary_of(const std::string& kind, std::int64_t bp, std::int64_t fixed_off, std::int64_t buy, std::int64_t pay) {
    if (kind == "PERCENT") return percent_text(bp) + " % de descuento";
    if (kind == "FIXED") return contracts::Money::from_cents(fixed_off).to_string() + " menos en cada pieza";
    return "Lleva " + std::to_string(buy) + " y paga " + std::to_string(pay);
}

std::string validity_of(const std::string& from, const std::string& to, int mask, int tf, int tt) {
    std::vector<std::string> parts;
    const auto days = mask_to_weekdays(mask);
    if (!days.empty()) {
        bool run = days.size() >= 3;
        for (std::size_t i = 1; i < days.size() && run; ++i) if (days[i] != days[i - 1] + 1) run = false;
        if (run) {
            parts.push_back(std::string(kWeekdayNames[days.front() - 1]) + " a " + kWeekdayNames[days.back() - 1]);
        } else {
            std::string list;
            for (std::size_t i = 0; i < days.size(); ++i) {
                if (i > 0) list += (i + 1 == days.size()) ? " y " : ", ";
                list += kWeekdayNames[days[i] - 1];
            }
            parts.push_back(list);
        }
    }
    if (tf >= 0 && tt >= 0) parts.push_back("de " + hhmm(tf) + " a " + hhmm(tt));
    if (!from.empty() && !to.empty()) parts.push_back("del " + display_date(from) + " al " + display_date(to));
    else if (!from.empty()) parts.push_back("desde el " + display_date(from));
    else if (!to.empty()) parts.push_back("hasta el " + display_date(to));
    if (parts.empty()) return "Siempre";
    std::string out;
    for (std::size_t i = 0; i < parts.size(); ++i) out += (i ? ", " : "") + parts[i];
    return capitalize(out);
}

bool ranges_overlap(const std::string& a_from, const std::string& a_to, const std::string& b_from, const std::string& b_to) {
    const std::string lo = std::max(a_from, b_from);                    // "" es el menor: sin límite inferior
    const bool a_open = a_to.empty(), b_open = b_to.empty();
    if (a_open && b_open) return true;
    const std::string hi = a_open ? b_to : (b_open ? a_to : std::min(a_to, b_to));
    return lo <= hi;
}

} // namespace

SqlitePromotionService::SqlitePromotionService(
    storage::SqliteDatabase& db,
    std::shared_ptr<contracts::IAccountService> account_service,
    std::shared_ptr<contracts::ICatalogService> catalog_service,
    LocalClock clock
) : db_(db), account_service_(std::move(account_service)), catalog_service_(std::move(catalog_service)), clock_(std::move(clock)) {}

OperationResult SqlitePromotionService::check_permission_or_fail(std::string_view perm) const {
    if (!account_service_ || !account_service_->is_authenticated()) {
        return OperationResult::fail(ErrorCode::Forbidden, "Inicie sesión para continuar.");
    }
    if (!account_service_->has_permission(perm)) {
        return OperationResult::fail(ErrorCode::Forbidden, "No tiene permiso para administrar promociones y combos.");
    }
    return OperationResult::ok();
}

OperationResult SqlitePromotionService::validate(const contracts::CreatePromotionDto& in) const {
    auto bad = [](const std::string& m) { return OperationResult::fail(ErrorCode::InvalidInput, m); };
    if (in.name.empty() || in.name.size() > 80) return bad("Escriba un nombre para la promoción (hasta 80 caracteres).");
    if (in.kind == "PERCENT") {
        if (in.percent_basis_points < 1 || in.percent_basis_points > 10000) return bad("El porcentaje de descuento debe estar entre 0.01 % y 100 %.");
    } else if (in.kind == "FIXED") {
        if (in.fixed_off.cents() <= 0) return bad("El monto de descuento debe ser mayor que cero.");
    } else if (in.kind == "NXM") {
        if (in.buy_qty < 2 || in.buy_qty > 99 || in.pay_qty < 1 || in.pay_qty >= in.buy_qty) {
            return bad("En «lleva N y paga M», N debe ser mayor que M y M al menos 1 (por ejemplo, lleva 2 y paga 1).");
        }
    } else {
        return bad("Elija el tipo de promoción: porcentaje, monto fijo o lleva N y paga M.");
    }
    if (in.priority < -1000000 || in.priority > 1000000) return bad("La prioridad debe ser un número entero razonable.");

    if (in.scope_kind == "PRODUCT") {
        if (in.product_ids.empty()) return bad("Elija al menos un producto para la promoción.");
        if (!in.category.empty()) return bad("Una promoción es de productos concretos o de una categoría, no de ambos.");
        std::set<std::string> seen;
        for (const auto& pid : in.product_ids) {
            if (!seen.insert(pid).second) return bad("Un producto aparece dos veces en la promoción.");
            auto prep = db_.prepare("SELECT COUNT(*) FROM products WHERE id = ?;");
            if (!prep.is_success()) return OperationResult::fail(prep.error_code(), prep.error_message());
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, pid);
            auto st = stmt.step();
            if (!st.is_success() || !st.value() || stmt.get_int64(0) == 0) return OperationResult::fail(ErrorCode::NotFound, "Uno de los productos elegidos ya no existe.");
        }
    } else if (in.scope_kind == "CATEGORY") {
        if (in.category.empty()) return bad("Elija la categoría de la promoción.");
        if (!in.product_ids.empty()) return bad("Una promoción es de productos concretos o de una categoría, no de ambos.");
        auto prep = db_.prepare("SELECT COUNT(*) FROM products WHERE category = ?;");
        if (!prep.is_success()) return OperationResult::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, in.category);
        auto st = stmt.step();
        if (!st.is_success() || !st.value() || stmt.get_int64(0) == 0) return OperationResult::fail(ErrorCode::NotFound, "No hay productos en esa categoría.");
    } else {
        return bad("Elija si la promoción es de productos concretos o de una categoría.");
    }

    if (!in.valid_from.empty() && !valid_date(in.valid_from)) return bad("La fecha de inicio no es válida. Use AAAA-MM-DD.");
    if (!in.valid_to.empty() && !valid_date(in.valid_to)) return bad("La fecha de fin no es válida. Use AAAA-MM-DD.");
    if (!in.valid_from.empty() && !in.valid_to.empty() && in.valid_from > in.valid_to) return bad("La fecha de fin no puede ser anterior a la de inicio.");
    std::set<int> days;
    for (int d : in.weekdays) {
        if (d < 1 || d > 7) return bad("Un día de la semana no es válido.");
        days.insert(d);
    }
    if (in.time_from.empty() != in.time_to.empty()) return bad("Para limitar por horario indique la hora de inicio y la de fin.");
    if (!in.time_from.empty()) {
        const int tf = parse_hhmm(in.time_from), tt = parse_hhmm(in.time_to);
        if (tf < 0 || tt < 0) return bad("Las horas deben tener el formato HH:MM.");
        if (tf >= tt) return bad("La hora de inicio debe ser anterior a la de fin (la promoción no cruza la medianoche).");
    }
    return OperationResult::ok();
}

bool SqlitePromotionService::load_row(const std::string& promotion_id, Row& out, std::string& error) {
    auto prep = db_.prepare(
        "SELECT id, name, kind, percent_bp, fixed_off_cents, buy_qty, pay_qty, scope_kind, priority, is_active, "
        "valid_from, valid_to, weekdays_mask, time_from_min, time_to_min FROM promotions WHERE id = ?;");
    if (!prep.is_success()) { error = prep.error_message(); return false; }
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, promotion_id);
    auto st = stmt.step();
    if (!st.is_success()) { error = st.error_message(); return false; }
    if (!st.value()) { error = ""; return false; }
    out = Row{};
    out.id = stmt.get_text(0);
    out.name = stmt.get_text(1);
    out.kind = stmt.get_text(2);
    out.percent_bp = stmt.get_int64(3);
    out.fixed_off = stmt.get_int64(4);
    out.buy_qty = stmt.get_int64(5);
    out.pay_qty = stmt.get_int64(6);
    out.scope_kind = stmt.get_text(7);
    out.priority = static_cast<int>(stmt.get_int64(8));
    out.is_active = stmt.get_int64(9) != 0;
    out.valid_from = stmt.is_null(10) ? "" : stmt.get_text(10);
    out.valid_to = stmt.is_null(11) ? "" : stmt.get_text(11);
    out.weekdays_mask = static_cast<int>(stmt.get_int64(12));
    out.time_from_min = stmt.is_null(13) ? -1 : static_cast<int>(stmt.get_int64(13));
    out.time_to_min = stmt.is_null(14) ? -1 : static_cast<int>(stmt.get_int64(14));
    auto prep_t = db_.prepare("SELECT product_id, category FROM promotion_targets WHERE promotion_id = ? ORDER BY rowid;");
    if (!prep_t.is_success()) { error = prep_t.error_message(); return false; }
    auto stmt_t = std::move(prep_t.value());
    stmt_t.bind_text(1, promotion_id);
    while (true) {
        auto s2 = stmt_t.step();
        if (!s2.is_success()) { error = s2.error_message(); return false; }
        if (!s2.value()) break;
        if (!stmt_t.is_null(0)) out.product_ids.push_back(stmt_t.get_text(0));
        if (!stmt_t.is_null(1)) out.category = stmt_t.get_text(1);
    }
    return true;
}

contracts::PromotionDto SqlitePromotionService::to_dto(const Row& row) const {
    contracts::PromotionDto dto;
    dto.id = row.id;
    dto.name = row.name;
    dto.kind = row.kind;
    dto.percent_basis_points = row.percent_bp;
    dto.fixed_off = contracts::Money::from_cents(row.fixed_off);
    dto.buy_qty = row.buy_qty;
    dto.pay_qty = row.pay_qty;
    dto.scope_kind = row.scope_kind;
    dto.product_ids = row.product_ids;
    for (const auto& pid : row.product_ids) {
        std::string name = pid;
        auto prep = db_.prepare("SELECT name FROM products WHERE id = ?;");
        if (prep.is_success()) {
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, pid);
            auto st = stmt.step();
            if (st.is_success() && st.value()) name = stmt.get_text(0);
        }
        dto.product_names.push_back(name);
    }
    if (!row.category.empty()) dto.categories.push_back(row.category);
    dto.priority = row.priority;
    dto.is_active = row.is_active;
    dto.valid_from = row.valid_from;
    dto.valid_to = row.valid_to;
    dto.weekdays = mask_to_weekdays(row.weekdays_mask);
    if (row.time_from_min >= 0 && row.time_to_min >= 0) {
        dto.time_from = hhmm(row.time_from_min);
        dto.time_to = hhmm(row.time_to_min);
    }
    dto.summary_text = summary_of(row.kind, row.percent_bp, row.fixed_off, row.buy_qty, row.pay_qty);
    dto.validity_text = validity_of(row.valid_from, row.valid_to, row.weekdays_mask, row.time_from_min, row.time_to_min);
    dto.is_valid_now = row.is_active && promotion_in_validity(row.valid_from, row.valid_to, row.weekdays_mask,
                                                              row.time_from_min, row.time_to_min, now());
    return dto;
}

OperationResult SqlitePromotionService::check_ambiguity(const Row& cand) {
    // Productos de la candidata (y de cada otra) con su categoría, para cruzar alcance por categoría.
    auto category_of = [&](const std::string& pid) -> std::string {
        auto prep = db_.prepare("SELECT category FROM products WHERE id = ?;");
        if (!prep.is_success()) return "";
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, pid);
        auto st = stmt.step();
        return (st.is_success() && st.value()) ? stmt.get_text(0) : "";
    };
    auto prep = db_.prepare("SELECT id FROM promotions WHERE is_active = 1 AND priority = ? AND id <> ? ORDER BY rowid;");
    if (!prep.is_success()) return OperationResult::fail(prep.error_code(), prep.error_message());
    std::vector<std::string> others;
    {
        auto stmt = std::move(prep.value());
        stmt.bind_int64(1, cand.priority);
        stmt.bind_text(2, cand.id);
        while (true) {
            auto st = stmt.step();
            if (!st.is_success()) return OperationResult::fail(st.error_code(), st.error_message());
            if (!st.value()) break;
            others.push_back(stmt.get_text(0));
        }
    }
    for (const auto& oid : others) {
        Row o;
        std::string err;
        if (!load_row(oid, o, err)) continue;
        if (!ranges_overlap(cand.valid_from, cand.valid_to, o.valid_from, o.valid_to)) continue;
        if (cand.weekdays_mask != 0 && o.weekdays_mask != 0 && (cand.weekdays_mask & o.weekdays_mask) == 0) continue;
        {
            const int a_from = cand.time_from_min, a_to = cand.time_to_min, b_from = o.time_from_min, b_to = o.time_to_min;
            if (a_from >= 0 && b_from >= 0 && std::max(a_from, b_from) >= std::min(a_to, b_to)) continue;
        }
        bool targets = false;
        if (cand.scope_kind == "CATEGORY" && o.scope_kind == "CATEGORY") {
            targets = cand.category == o.category;
        } else if (cand.scope_kind == "PRODUCT" && o.scope_kind == "PRODUCT") {
            for (const auto& p : cand.product_ids) if (std::find(o.product_ids.begin(), o.product_ids.end(), p) != o.product_ids.end()) targets = true;
        } else {
            const Row& by_cat = cand.scope_kind == "CATEGORY" ? cand : o;
            const Row& by_prod = cand.scope_kind == "CATEGORY" ? o : cand;
            for (const auto& p : by_prod.product_ids) if (category_of(p) == by_cat.category) targets = true;
        }
        if (targets) {
            return OperationResult::fail(ErrorCode::Conflict,
                "La promoción «" + o.name + "» tiene la misma prioridad y se cruza con esta en productos y horarios. "
                "Cambie la prioridad o la vigencia de una de las dos.");
        }
    }
    return OperationResult::ok();
}

OperationResult SqlitePromotionService::write_targets(const std::string& promotion_id, const Row& row) {
    {
        auto prep = db_.prepare("DELETE FROM promotion_targets WHERE promotion_id = ?;");
        if (!prep.is_success()) return OperationResult::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, promotion_id);
        auto st = stmt.step();
        if (!st.is_success()) return OperationResult::fail(st.error_code(), st.error_message());
    }
    auto insert = [&](const std::string* product_id, const std::string* category) -> OperationResult {
        auto prep = db_.prepare("INSERT INTO promotion_targets (promotion_id, product_id, category) VALUES (?, ?, ?);");
        if (!prep.is_success()) return OperationResult::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, promotion_id);
        if (product_id) stmt.bind_text(2, *product_id); else stmt.bind_null(2);
        if (category) stmt.bind_text(3, *category); else stmt.bind_null(3);
        auto st = stmt.step();
        if (!st.is_success()) return OperationResult::fail(st.error_code(), st.error_message());
        return OperationResult::ok();
    };
    if (row.scope_kind == "PRODUCT") {
        for (const auto& pid : row.product_ids) {
            auto r = insert(&pid, nullptr);
            if (!r.is_success()) return r;
        }
    } else {
        auto r = insert(nullptr, &row.category);
        if (!r.is_success()) return r;
    }
    return OperationResult::ok();
}

namespace {
int weekdays_to_mask(const std::vector<int>& days) {
    int mask = 0;
    for (int d : days) mask |= (1 << (d - 1));
    return mask == 127 ? 0 : mask;
}
}

contracts::Result<std::vector<contracts::PromotionDto>> SqlitePromotionService::list_promotions(const contracts::PromotionFilterDto& filter) {
    using R = contracts::Result<std::vector<contracts::PromotionDto>>;
    auto perm = check_permission_or_fail("inventory.promotions");
    if (!perm.is_success()) return R::fail(perm.error_code(), perm.error_message());
    std::string sql = "SELECT p.id FROM promotions p WHERE 1=1 ";
    if (filter.only_active) sql += "AND p.is_active = 1 ";
    if (!filter.product_id.empty()) {
        sql += "AND EXISTS (SELECT 1 FROM promotion_targets t WHERE t.promotion_id = p.id AND "
               "(t.product_id = ?1 OR t.category = (SELECT category FROM products WHERE id = ?1))) ";
    }
    sql += "ORDER BY p.priority DESC, p.rowid ASC;";
    auto prep = db_.prepare(sql);
    if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
    std::vector<std::string> ids;
    {
        auto stmt = std::move(prep.value());
        if (!filter.product_id.empty()) stmt.bind_text(1, filter.product_id);
        while (true) {
            auto st = stmt.step();
            if (!st.is_success()) return R::fail(st.error_code(), st.error_message());
            if (!st.value()) break;
            ids.push_back(stmt.get_text(0));
        }
    }
    std::vector<contracts::PromotionDto> list;
    for (const auto& id : ids) {
        Row row;
        std::string err;
        if (load_row(id, row, err)) list.push_back(to_dto(row));
    }
    return R::ok(std::move(list));
}

contracts::Result<contracts::PromotionDto> SqlitePromotionService::get_promotion(const std::string& promotion_id) {
    using R = contracts::Result<contracts::PromotionDto>;
    auto perm = check_permission_or_fail("inventory.promotions");
    if (!perm.is_success()) return R::fail(perm.error_code(), perm.error_message());
    Row row;
    std::string err;
    if (!load_row(promotion_id, row, err)) {
        return R::fail(err.empty() ? ErrorCode::NotFound : ErrorCode::StorageFailure, err.empty() ? "No se encontró la promoción." : err);
    }
    return R::ok(to_dto(row));
}

contracts::Result<contracts::PromotionDto> SqlitePromotionService::create_promotion(const contracts::CreatePromotionDto& in) {
    using R = contracts::Result<contracts::PromotionDto>;
    auto perm = check_permission_or_fail("inventory.promotions");
    if (!perm.is_success()) return R::fail(perm.error_code(), perm.error_message());
    auto valid = validate(in);
    if (!valid.is_success()) return R::fail(valid.error_code(), valid.error_message());

    Row row;
    row.id = generate_id("promo");
    row.name = in.name;
    row.kind = in.kind;
    row.percent_bp = in.kind == "PERCENT" ? in.percent_basis_points : 0;
    row.fixed_off = in.kind == "FIXED" ? in.fixed_off.cents() : 0;
    row.buy_qty = in.kind == "NXM" ? in.buy_qty : 0;
    row.pay_qty = in.kind == "NXM" ? in.pay_qty : 0;
    row.scope_kind = in.scope_kind;
    row.priority = in.priority;
    row.is_active = in.is_active;
    row.valid_from = in.valid_from;
    row.valid_to = in.valid_to;
    row.weekdays_mask = weekdays_to_mask(in.weekdays);
    if (!in.time_from.empty()) { row.time_from_min = parse_hhmm(in.time_from); row.time_to_min = parse_hhmm(in.time_to); }
    row.product_ids = in.product_ids;
    row.category = in.category;
    if (row.is_active) {
        auto amb = check_ambiguity(row);
        if (!amb.is_success()) return R::fail(amb.error_code(), amb.error_message());
    }

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return R::fail(tx.error_code(), tx.error_message());
    const auto ts = domain::now();
    auto prep = db_.prepare(
        "INSERT INTO promotions (id, name, kind, percent_bp, fixed_off_cents, buy_qty, pay_qty, scope_kind, priority, is_active, "
        "valid_from, valid_to, weekdays_mask, time_from_min, time_to_min, created_at, created_at_utc, updated_at, updated_at_utc) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");
    if (!prep.is_success()) { db_.rollback_transaction(); return R::fail(prep.error_code(), prep.error_message()); }
    {
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, row.id);
        stmt.bind_text(2, row.name);
        stmt.bind_text(3, row.kind);
        stmt.bind_int64(4, row.percent_bp);
        stmt.bind_int64(5, row.fixed_off);
        stmt.bind_int64(6, row.buy_qty);
        stmt.bind_int64(7, row.pay_qty);
        stmt.bind_text(8, row.scope_kind);
        stmt.bind_int64(9, row.priority);
        stmt.bind_int64(10, row.is_active ? 1 : 0);
        if (row.valid_from.empty()) stmt.bind_null(11); else stmt.bind_text(11, row.valid_from);
        if (row.valid_to.empty()) stmt.bind_null(12); else stmt.bind_text(12, row.valid_to);
        stmt.bind_int64(13, row.weekdays_mask);
        if (row.time_from_min < 0) stmt.bind_null(14); else stmt.bind_int64(14, row.time_from_min);
        if (row.time_to_min < 0) stmt.bind_null(15); else stmt.bind_int64(15, row.time_to_min);
        stmt.bind_text(16, ts.local);
        stmt.bind_text(17, ts.utc);
        stmt.bind_text(18, ts.local);
        stmt.bind_text(19, ts.utc);
        auto st = stmt.step();
        if (!st.is_success()) { db_.rollback_transaction(); return R::fail(st.error_code(), st.error_message()); }
    }
    auto tg = write_targets(row.id, row);
    if (!tg.is_success()) { db_.rollback_transaction(); return R::fail(tg.error_code(), tg.error_message()); }
    auto commit = db_.commit_transaction();
    if (!commit.is_success()) { db_.rollback_transaction(); return R::fail(commit.error_code(), commit.error_message()); }

    const auto dto = to_dto(row);
    account_service_->record_audit_event("PROMOTION_CREATED", "Promoción creada: " + dto.name + " (" + dto.summary_text + "; " + dto.validity_text + ")");
    return R::ok(dto);
}

contracts::Result<contracts::PromotionDto> SqlitePromotionService::update_promotion(const contracts::UpdatePromotionDto& input) {
    using R = contracts::Result<contracts::PromotionDto>;
    auto perm = check_permission_or_fail("inventory.promotions");
    if (!perm.is_success()) return R::fail(perm.error_code(), perm.error_message());
    Row existing;
    std::string err;
    if (!load_row(input.promotion_id, existing, err)) {
        return R::fail(err.empty() ? ErrorCode::NotFound : ErrorCode::StorageFailure, err.empty() ? "No se encontró la promoción." : err);
    }
    const auto& in = input.data;
    auto valid = validate(in);
    if (!valid.is_success()) return R::fail(valid.error_code(), valid.error_message());

    Row row = existing;
    row.name = in.name;
    row.kind = in.kind;
    row.percent_bp = in.kind == "PERCENT" ? in.percent_basis_points : 0;
    row.fixed_off = in.kind == "FIXED" ? in.fixed_off.cents() : 0;
    row.buy_qty = in.kind == "NXM" ? in.buy_qty : 0;
    row.pay_qty = in.kind == "NXM" ? in.pay_qty : 0;
    row.scope_kind = in.scope_kind;
    row.priority = in.priority;
    row.is_active = in.is_active;
    row.valid_from = in.valid_from;
    row.valid_to = in.valid_to;
    row.weekdays_mask = weekdays_to_mask(in.weekdays);
    row.time_from_min = in.time_from.empty() ? -1 : parse_hhmm(in.time_from);
    row.time_to_min = in.time_to.empty() ? -1 : parse_hhmm(in.time_to);
    row.product_ids = in.product_ids;
    row.category = in.category;
    if (row.is_active) {
        auto amb = check_ambiguity(row);
        if (!amb.is_success()) return R::fail(amb.error_code(), amb.error_message());
    }

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return R::fail(tx.error_code(), tx.error_message());
    const auto ts = domain::now();
    auto prep = db_.prepare(
        "UPDATE promotions SET name = ?, kind = ?, percent_bp = ?, fixed_off_cents = ?, buy_qty = ?, pay_qty = ?, scope_kind = ?, "
        "priority = ?, is_active = ?, valid_from = ?, valid_to = ?, weekdays_mask = ?, time_from_min = ?, time_to_min = ?, "
        "updated_at = ?, updated_at_utc = ? WHERE id = ?;");
    if (!prep.is_success()) { db_.rollback_transaction(); return R::fail(prep.error_code(), prep.error_message()); }
    {
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, row.name);
        stmt.bind_text(2, row.kind);
        stmt.bind_int64(3, row.percent_bp);
        stmt.bind_int64(4, row.fixed_off);
        stmt.bind_int64(5, row.buy_qty);
        stmt.bind_int64(6, row.pay_qty);
        stmt.bind_text(7, row.scope_kind);
        stmt.bind_int64(8, row.priority);
        stmt.bind_int64(9, row.is_active ? 1 : 0);
        if (row.valid_from.empty()) stmt.bind_null(10); else stmt.bind_text(10, row.valid_from);
        if (row.valid_to.empty()) stmt.bind_null(11); else stmt.bind_text(11, row.valid_to);
        stmt.bind_int64(12, row.weekdays_mask);
        if (row.time_from_min < 0) stmt.bind_null(13); else stmt.bind_int64(13, row.time_from_min);
        if (row.time_to_min < 0) stmt.bind_null(14); else stmt.bind_int64(14, row.time_to_min);
        stmt.bind_text(15, ts.local);
        stmt.bind_text(16, ts.utc);
        stmt.bind_text(17, row.id);
        auto st = stmt.step();
        if (!st.is_success()) { db_.rollback_transaction(); return R::fail(st.error_code(), st.error_message()); }
    }
    auto tg = write_targets(row.id, row);
    if (!tg.is_success()) { db_.rollback_transaction(); return R::fail(tg.error_code(), tg.error_message()); }
    auto commit = db_.commit_transaction();
    if (!commit.is_success()) { db_.rollback_transaction(); return R::fail(commit.error_code(), commit.error_message()); }

    const auto dto = to_dto(row);
    account_service_->record_audit_event("PROMOTION_UPDATED", "Promoción editada: " + dto.name + " (" + dto.summary_text + "; " + dto.validity_text + ")");
    return R::ok(dto);
}

OperationResult SqlitePromotionService::set_promotion_active(const std::string& promotion_id, bool active) {
    auto perm = check_permission_or_fail("inventory.promotions");
    if (!perm.is_success()) return perm;
    Row row;
    std::string err;
    if (!load_row(promotion_id, row, err)) {
        return OperationResult::fail(err.empty() ? ErrorCode::NotFound : ErrorCode::StorageFailure, err.empty() ? "No se encontró la promoción." : err);
    }
    if (row.is_active == active) return OperationResult::ok();
    row.is_active = active;
    if (active) {
        auto amb = check_ambiguity(row);
        if (!amb.is_success()) return amb;
    }
    auto prep = db_.prepare("UPDATE promotions SET is_active = ?, updated_at = ?, updated_at_utc = ? WHERE id = ?;");
    if (!prep.is_success()) return OperationResult::fail(prep.error_code(), prep.error_message());
    const auto ts = domain::now();
    auto stmt = std::move(prep.value());
    stmt.bind_int64(1, active ? 1 : 0);
    stmt.bind_text(2, ts.local);
    stmt.bind_text(3, ts.utc);
    stmt.bind_text(4, promotion_id);
    auto st = stmt.step();
    if (!st.is_success()) return OperationResult::fail(st.error_code(), st.error_message());
    account_service_->record_audit_event(active ? "PROMOTION_ACTIVATED" : "PROMOTION_DEACTIVATED",
                                         std::string(active ? "Promoción activada: " : "Promoción desactivada: ") + row.name);
    return OperationResult::ok();
}

// ---------------------------------------------------------------------------------------------
// Combos
// ---------------------------------------------------------------------------------------------
OperationResult SqlitePromotionService::validate_components(const std::vector<contracts::ComboComponentDto>& components,
                                                            const std::string& combo_id) const {
    auto bad = [](ErrorCode c, const std::string& m) { return OperationResult::fail(c, m); };
    if (components.empty()) return bad(ErrorCode::InvalidInput, "Un combo necesita al menos un producto.");
    if (components.size() > 20) return bad(ErrorCode::InvalidInput, "Un combo puede tener hasta 20 productos distintos.");
    std::set<std::string> seen;
    for (const auto& c : components) {
        if (c.quantity < 1 || c.quantity > 99) return bad(ErrorCode::InvalidInput, "La cantidad de cada producto del combo debe estar entre 1 y 99.");
        if (!seen.insert(c.product_id).second) return bad(ErrorCode::InvalidInput, "Un producto aparece dos veces en el combo. Sume sus cantidades en una sola fila.");
        if (!combo_id.empty() && c.product_id == combo_id) return bad(ErrorCode::InvalidInput, "Un combo no puede contenerse a sí mismo.");
        auto prep = db_.prepare("SELECT name, is_active, is_combo FROM products WHERE id = ?;");
        if (!prep.is_success()) return OperationResult::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, c.product_id);
        auto st = stmt.step();
        if (!st.is_success() || !st.value()) return bad(ErrorCode::NotFound, "Uno de los productos elegidos ya no existe.");
        if (stmt.get_int64(2) != 0) return bad(ErrorCode::InvalidInput, "Un combo no puede incluir otro combo («" + stmt.get_text(0) + "»).");
        if (stmt.get_int64(1) == 0) return bad(ErrorCode::InvalidInput, "El producto «" + stmt.get_text(0) + "» está dado de baja y no puede formar parte de un combo.");
    }
    return OperationResult::ok();
}

contracts::Result<contracts::ComboDto> SqlitePromotionService::read_combo(const std::string& product_id) {
    using R = contracts::Result<contracts::ComboDto>;
    auto prep = db_.prepare("SELECT sku, name, category, price_cents, is_active, is_combo FROM products WHERE id = ?;");
    if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
    contracts::ComboDto dto;
    {
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, product_id);
        auto st = stmt.step();
        if (!st.is_success()) return R::fail(st.error_code(), st.error_message());
        if (!st.value() || stmt.get_int64(5) == 0) return R::fail(ErrorCode::NotFound, "No se encontró el combo.");
        dto.product_id = product_id;
        dto.sku = stmt.get_text(0);
        dto.name = stmt.get_text(1);
        dto.category = stmt.get_text(2);
        dto.price = contracts::Money::from_cents(stmt.get_int64(3));
        dto.is_active = stmt.get_int64(4) != 0;
    }
    auto prep_c = db_.prepare(
        "SELECT ci.component_product_id, p.name, ci.quantity FROM combo_items ci "
        "JOIN products p ON p.id = ci.component_product_id WHERE ci.combo_product_id = ? ORDER BY ci.rowid;");
    if (!prep_c.is_success()) return R::fail(prep_c.error_code(), prep_c.error_message());
    {
        auto stmt = std::move(prep_c.value());
        stmt.bind_text(1, product_id);
        while (true) {
            auto st = stmt.step();
            if (!st.is_success()) return R::fail(st.error_code(), st.error_message());
            if (!st.value()) break;
            contracts::ComboComponentDto c;
            c.product_id = stmt.get_text(0);
            c.product_name = stmt.get_text(1);
            c.quantity = stmt.get_int64(2);
            dto.components.push_back(std::move(c));
        }
    }
    auto product = catalog_service_->get_product_by_id(product_id);
    if (product.is_success()) dto.stock_units = product.value().stock_units;
    return R::ok(std::move(dto));
}

contracts::Result<std::vector<contracts::ComboDto>> SqlitePromotionService::list_combos(bool include_inactive) {
    using R = contracts::Result<std::vector<contracts::ComboDto>>;
    auto perm = check_permission_or_fail("inventory.promotions");
    if (!perm.is_success()) return R::fail(perm.error_code(), perm.error_message());
    auto prep = db_.prepare(std::string("SELECT id FROM products WHERE is_combo = 1 ") + (include_inactive ? "" : "AND is_active = 1 ") + "ORDER BY name ASC;");
    if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
    std::vector<std::string> ids;
    {
        auto stmt = std::move(prep.value());
        while (true) {
            auto st = stmt.step();
            if (!st.is_success()) return R::fail(st.error_code(), st.error_message());
            if (!st.value()) break;
            ids.push_back(stmt.get_text(0));
        }
    }
    std::vector<contracts::ComboDto> list;
    for (const auto& id : ids) {
        auto c = read_combo(id);
        if (c.is_success()) list.push_back(c.value());
    }
    return R::ok(std::move(list));
}

contracts::Result<contracts::ComboDto> SqlitePromotionService::get_combo(const std::string& product_id) {
    using R = contracts::Result<contracts::ComboDto>;
    auto perm = check_permission_or_fail("inventory.promotions");
    if (!perm.is_success()) return R::fail(perm.error_code(), perm.error_message());
    return read_combo(product_id);
}

contracts::Result<contracts::ComboDto> SqlitePromotionService::create_combo(const contracts::CreateComboDto& in) {
    using R = contracts::Result<contracts::ComboDto>;
    auto perm = check_permission_or_fail("inventory.promotions");
    if (!perm.is_success()) return R::fail(perm.error_code(), perm.error_message());
    if (in.sku.empty()) return R::fail(ErrorCode::InvalidInput, "El SKU es obligatorio.");
    if (in.name.empty()) return R::fail(ErrorCode::InvalidInput, "El nombre del combo es obligatorio.");
    if (in.category.empty()) return R::fail(ErrorCode::InvalidInput, "La categoría es obligatoria.");
    if (in.price.cents() < 0) return R::fail(ErrorCode::InvalidInput, "El precio no puede ser negativo.");
    auto comps = validate_components(in.components, "");
    if (!comps.is_success()) return R::fail(comps.error_code(), comps.error_message());
    {
        auto prep = db_.prepare("SELECT COUNT(*) FROM products WHERE sku = ?;");
        if (prep.is_success()) {
            auto stmt = std::move(prep.value());
            stmt.bind_text(1, in.sku);
            if (stmt.step().is_success() && stmt.get_int64(0) > 0) return R::fail(ErrorCode::Conflict, "El SKU ya existe.");
        }
        if (!in.barcode.empty()) {
            auto prep_b = db_.prepare("SELECT COUNT(*) FROM products WHERE barcode = ?;");
            if (prep_b.is_success()) {
                auto stmt = std::move(prep_b.value());
                stmt.bind_text(1, in.barcode);
                if (stmt.step().is_success() && stmt.get_int64(0) > 0) return R::fail(ErrorCode::Conflict, "El código de barras ya existe.");
            }
        }
    }
    const std::string product_id = generate_id("prod");
    const auto ts = domain::now();
    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return R::fail(tx.error_code(), tx.error_message());
    auto prep = db_.prepare(
        "INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at, updated_at, updated_at_utc, is_combo) "
        "VALUES (?, ?, ?, ?, ?, ?, 0, 1, ?, ?, ?, 1);");
    if (!prep.is_success()) { db_.rollback_transaction(); return R::fail(prep.error_code(), prep.error_message()); }
    {
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, product_id);
        stmt.bind_text(2, in.sku);
        if (in.barcode.empty()) stmt.bind_null(3); else stmt.bind_text(3, in.barcode);
        stmt.bind_text(4, in.name);
        stmt.bind_text(5, in.category);
        stmt.bind_int64(6, in.price.cents());
        stmt.bind_text(7, ts.local);
        stmt.bind_text(8, ts.local);
        stmt.bind_text(9, ts.utc);
        auto st = stmt.step();
        if (!st.is_success()) { db_.rollback_transaction(); return R::fail(st.error_code(), st.error_message()); }
    }
    for (const auto& c : in.components) {
        auto ins = db_.prepare("INSERT INTO combo_items (combo_product_id, component_product_id, quantity) VALUES (?, ?, ?);");
        if (!ins.is_success()) { db_.rollback_transaction(); return R::fail(ins.error_code(), ins.error_message()); }
        auto stmt = std::move(ins.value());
        stmt.bind_text(1, product_id);
        stmt.bind_text(2, c.product_id);
        stmt.bind_int64(3, c.quantity);
        auto st = stmt.step();
        if (!st.is_success()) { db_.rollback_transaction(); return R::fail(st.error_code(), st.error_message()); }
    }
    auto commit = db_.commit_transaction();
    if (!commit.is_success()) { db_.rollback_transaction(); return R::fail(commit.error_code(), commit.error_message()); }
    account_service_->record_audit_event("COMBO_CREATED", "Combo creado: " + in.name + " (" + in.sku + ") por " + in.price.to_string());
    return read_combo(product_id);
}

contracts::Result<contracts::ComboDto> SqlitePromotionService::update_combo(const contracts::UpdateComboDto& in) {
    using R = contracts::Result<contracts::ComboDto>;
    auto perm = check_permission_or_fail("inventory.promotions");
    if (!perm.is_success()) return R::fail(perm.error_code(), perm.error_message());
    auto current = read_combo(in.product_id);
    if (!current.is_success()) return current;
    if (in.name.empty()) return R::fail(ErrorCode::InvalidInput, "El nombre del combo es obligatorio.");
    if (in.category.empty()) return R::fail(ErrorCode::InvalidInput, "La categoría es obligatoria.");
    if (in.price.cents() < 0) return R::fail(ErrorCode::InvalidInput, "El precio no puede ser negativo.");
    auto comps = validate_components(in.components, in.product_id);
    if (!comps.is_success()) return R::fail(comps.error_code(), comps.error_message());

    auto tx = db_.begin_transaction();
    if (!tx.is_success()) return R::fail(tx.error_code(), tx.error_message());
    const auto ts = domain::now();
    auto prep = db_.prepare("UPDATE products SET name = ?, category = ?, price_cents = ?, updated_at = ?, updated_at_utc = ? WHERE id = ?;");
    if (!prep.is_success()) { db_.rollback_transaction(); return R::fail(prep.error_code(), prep.error_message()); }
    {
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, in.name);
        stmt.bind_text(2, in.category);
        stmt.bind_int64(3, in.price.cents());
        stmt.bind_text(4, ts.local);
        stmt.bind_text(5, ts.utc);
        stmt.bind_text(6, in.product_id);
        auto st = stmt.step();
        if (!st.is_success()) { db_.rollback_transaction(); return R::fail(st.error_code(), st.error_message()); }
    }
    {
        auto del = db_.prepare("DELETE FROM combo_items WHERE combo_product_id = ?;");
        if (!del.is_success()) { db_.rollback_transaction(); return R::fail(del.error_code(), del.error_message()); }
        auto stmt = std::move(del.value());
        stmt.bind_text(1, in.product_id);
        auto st = stmt.step();
        if (!st.is_success()) { db_.rollback_transaction(); return R::fail(st.error_code(), st.error_message()); }
    }
    for (const auto& c : in.components) {
        auto ins = db_.prepare("INSERT INTO combo_items (combo_product_id, component_product_id, quantity) VALUES (?, ?, ?);");
        if (!ins.is_success()) { db_.rollback_transaction(); return R::fail(ins.error_code(), ins.error_message()); }
        auto stmt = std::move(ins.value());
        stmt.bind_text(1, in.product_id);
        stmt.bind_text(2, c.product_id);
        stmt.bind_int64(3, c.quantity);
        auto st = stmt.step();
        if (!st.is_success()) { db_.rollback_transaction(); return R::fail(st.error_code(), st.error_message()); }
    }
    auto commit = db_.commit_transaction();
    if (!commit.is_success()) { db_.rollback_transaction(); return R::fail(commit.error_code(), commit.error_message()); }
    account_service_->record_audit_event("COMBO_UPDATED", "Combo editado: " + in.name + " por " + in.price.to_string());
    return read_combo(in.product_id);
}

contracts::Result<contracts::ComboCostingDto> SqlitePromotionService::get_combo_costing(const std::string& product_id) {
    using R = contracts::Result<contracts::ComboCostingDto>;
    auto perm = check_permission_or_fail("inventory.promotions");
    if (!perm.is_success()) return R::fail(perm.error_code(), perm.error_message());
    if (!account_service_->has_permission("inventory.manage_products")) {
        return R::fail(ErrorCode::Forbidden, "No tiene permiso para ver costos y márgenes.");
    }
    auto combo = read_combo(product_id);
    if (!combo.is_success()) return R::fail(combo.error_code(), combo.error_message());
    contracts::ComboCostingDto dto;
    dto.product_id = product_id;
    dto.price = combo.value().price;
    std::int64_t known_total = 0;
    bool all_known = true;
    for (const auto& c : combo.value().components) {
        contracts::ComboCostLineDto line;
        line.product_id = c.product_id;
        line.product_name = c.product_name;
        line.quantity = c.quantity;
        auto costing = catalog_service_->get_recipe_costing(c.product_id);
        if (costing.is_success() && costing.value().all_costs_known &&
            costing.value().cost_per_portion.cents() <= INT64_MAX / c.quantity) {
            line.cost_known = true;
            line.cost = contracts::Money::from_cents(costing.value().cost_per_portion.cents() * c.quantity);
            if (known_total > INT64_MAX - line.cost.cents()) return R::fail(ErrorCode::InvalidInput, "Desbordamiento numérico al sumar los costos del combo.");
            known_total += line.cost.cents();
        } else {
            all_known = false;
            dto.unknown_component_names.push_back(c.product_name);
        }
        dto.lines.push_back(std::move(line));
    }
    dto.known_cost_subtotal = contracts::Money::from_cents(known_total);
    dto.all_costs_known = all_known;
    if (all_known) {
        dto.cost_total = dto.known_cost_subtotal;
        dto.margin_known = true;
        dto.margin = dto.price - dto.cost_total;
        if (dto.price.cents() > 0) {
            const std::int64_t m = dto.margin.cents();
            if (m <= INT64_MAX / 10000 && m >= INT64_MIN / 10000) {
                dto.margin_percent_known = true;
                dto.margin_basis_points = m * 10000 / dto.price.cents();
            }
        }
    }
    return R::ok(std::move(dto));
}

} // namespace app::services
