#include "app/services/sqlite_report_service.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include "app/domain/stock_valuation.hpp"
#include "app/services/identifiers.hpp"
#include "app/services/local_date.hpp"
#include "app/services/stock_ledger.hpp"

namespace app::services {

namespace {

using contracts::ErrorCode;
using Report = contracts::DailySalesReportDto;
using R = contracts::Result<Report>;

bool parse_date(const std::string& str, int& y, int& m, int& d) { return local_date::parse(str, y, m, d); }
std::string format_date(int y, int m, int d) { return local_date::format(y, m, d); }
int days_in_month(int y, int m) { return local_date::days_in_month(y, m); }

// Suma verificada: false si desborda int64.
bool checked_add(std::int64_t a, std::int64_t b, std::int64_t& out) {
    if (b > 0 && a > INT64_MAX - b) return false;
    if (b < 0 && a < INT64_MIN - b) return false;
    out = a + b;
    return true;
}

R overflow_error() {
    return R::fail(ErrorCode::InvalidInput, "Desbordamiento numérico al sumar los importes del reporte");
}

} // namespace

SqliteReportService::SqliteReportService(
    storage::SqliteDatabase& db,
    std::shared_ptr<contracts::IAccountService> account_service
) : db_(db), account_service_(std::move(account_service)) {
    // Los reportes de un mes recorren decenas de miles de filas por índice: con la caché por omisión (2 MB) cada
    // consulta vuelve a leer del disco. 32 MB (se reservan solo si se usan) dejan las lecturas repetidas en memoria.
    db_.execute("PRAGMA cache_size = -32768;");
}

contracts::Result<contracts::DailySalesReportDto> SqliteReportService::get_daily_sales_summary(
    const std::string& date_iso) {
    if (!account_service_ || !account_service_->has_permission("reports.view")) {
        return R::fail(ErrorCode::Forbidden, "Operación denegada: falta de permisos (reports.view)");
    }
    int y = 0, m = 0, d = 0;
    if (!parse_date(date_iso, y, m, d)) {
        return R::fail(ErrorCode::InvalidInput, "Fecha inválida: use el formato AAAA-MM-DD con una fecha real");
    }
    int ny = y, nm = m, nd = d + 1;
    if (nd > days_in_month(y, m)) {
        nd = 1;
        if (++nm > 12) { nm = 1; ++ny; }
    }
    const std::string start = format_date(y, m, d) + " 00:00:00";
    const std::string end = format_date(ny, nm, nd) + " 00:00:00";

    Report report;
    report.date = date_iso;

    // Ventas del día: [start, end) sobre sales.created_at.
    {
        auto prep = db_.prepare("SELECT total_cents, discount_cents, card_surcharge_cents FROM sales WHERE created_at >= ? AND created_at < ?;");
        if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, start);
        stmt.bind_text(2, end);
        std::int64_t total = 0, count = 0, discount = 0, surcharge = 0;
        while (true) {
            auto step = stmt.step();
            if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
            if (!step.value()) break;
            if (!checked_add(total, stmt.get_int64(0), total) || !checked_add(count, 1, count) ||
                !checked_add(discount, stmt.get_int64(1), discount) || !checked_add(surcharge, stmt.get_int64(2), surcharge)) return overflow_error();
        }
        report.card_surcharge_amount = contracts::Money::from_cents(surcharge);
        report.total_sales_count = count;
        report.total_amount = contracts::Money::from_cents(total);
        std::int64_t gross = 0;
        if (!checked_add(total, discount, gross)) return overflow_error();
        report.gross_amount = contracts::Money::from_cents(gross);
        report.discount_amount = contracts::Money::from_cents(discount);
    }

    // Pagos: atribuidos al día de su venta (JOIN); un pago sin venta no entra en ningún día.
    {
        auto prep = db_.prepare(
            "SELECT p.payment_method, p.amount_cents FROM payment_records p "
            "JOIN sales s ON s.id = p.sale_id "
            "WHERE s.created_at >= ? AND s.created_at < ?;");
        if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, start);
        stmt.bind_text(2, end);
        std::int64_t cash = 0, card = 0, transfer = 0, all = 0;
        while (true) {
            auto step = stmt.step();
            if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
            if (!step.value()) break;
            const std::string method = stmt.get_text(0);
            const std::int64_t amount = stmt.get_int64(1);
            std::int64_t* bucket = nullptr;
            if (method == "EFECTIVO") bucket = &cash;
            else if (method == "TARJETA") bucket = &card;
            else if (method == "TRANSFERENCIA") bucket = &transfer;
            else return R::fail(ErrorCode::StorageFailure, "Método de pago desconocido en payment_records");
            if (!checked_add(*bucket, amount, *bucket) || !checked_add(all, amount, all)) return overflow_error();
        }
        report.cash_amount = contracts::Money::from_cents(cash);
        report.card_amount = contracts::Money::from_cents(card);
        report.transfer_amount = contracts::Money::from_cents(transfer);
        report.payments_total = contracts::Money::from_cents(all);
    }

    // Discrepancia con signo, sin ocultarla. Ambos valores son >= 0 (CHECK en las tablas): no desborda.
    // B18: lo pagado incluye el cargo por tarjeta; los reembolsos del día (atribuidos a la fecha de su venta) van aparte.
    report.payments_mismatch_cents = report.total_amount.cents() + report.card_surcharge_amount.cents() - report.payments_total.cents();
    {
        auto prep = db_.prepare("SELECT COALESCE(SUM(r.amount_cents), 0) FROM refunds r JOIN sales s ON s.id = r.sale_id "
                                "WHERE s.created_at >= ? AND s.created_at < ?;");
        if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, start);
        stmt.bind_text(2, end);
        auto step = stmt.step();
        if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
        if (step.value()) report.refunds_amount = contracts::Money::from_cents(stmt.get_int64(0));
    }
    return R::ok(report);
}

// ===================================================== B15: reportes por rango y exportación a CSV

namespace {

using contracts::Money;

// Ejecuta `sql` con parámetros de texto y llama a `fn` por cada fila. Devuelve "" o el mensaje de error.
template <typename Fn>
std::string query_rows(storage::SqliteDatabase& db, const std::string& sql, const std::vector<std::string>& params, Fn fn) {
    auto prep = db.prepare(sql);
    if (!prep.is_success()) return prep.error_message();
    auto st = std::move(prep.value());
    for (std::size_t i = 0; i < params.size(); ++i) st.bind_text(static_cast<int>(i) + 1, params[i]);
    while (true) {
        auto step = st.step();
        if (!step.is_success()) return step.error_message();
        if (!step.value()) break;
        fn(st);
    }
    return "";
}

// Importe en centavos -> "1234.50" (punto, dos decimales, signo si es negativo). Solo enteros.
std::string cents_text(std::int64_t cents) {
    const bool neg = cents < 0;
    // |INT64_MIN| no cabe: se opera en unsigned.
    std::uint64_t mag = neg ? static_cast<std::uint64_t>(-(cents + 1)) + 1u : static_cast<std::uint64_t>(cents);
    std::string s = std::to_string(mag / 100) + "." + (mag % 100 < 10 ? "0" : "") + std::to_string(mag % 100);
    return neg ? "-" + s : s;
}

// Celda de texto: RFC 4180 y neutralización de fórmulas (=, +, -, @, tabulador, retorno al inicio).
std::string csv_text(const std::string& v) {
    std::string s = v;
    if (!s.empty() && (s[0] == '=' || s[0] == '+' || s[0] == '-' || s[0] == '@' || s[0] == '\t' || s[0] == '\r')) s.insert(s.begin(), '\'');
    bool quote = false;
    for (char c : s) if (c == ',' || c == '"' || c == '\r' || c == '\n') quote = true;
    if (!quote) return s;
    std::string out = "\"";
    for (char c : s) { if (c == '"') out += '"'; out += c; }
    return out + "\"";
}
std::string csv_int(std::int64_t v) { return std::to_string(v); }
std::string csv_money(std::int64_t cents) { return cents_text(cents); }

std::string method_label(const std::string& m) {
    if (m == "EFECTIVO") return "Efectivo";
    if (m == "TARJETA") return "Tarjeta";
    if (m == "TRANSFERENCIA") return "Transferencia";
    return m;
}
std::string movement_label(const std::string& t) {
    if (t == "PURCHASE") return "Compra";
    if (t == "SALE") return "Venta";
    if (t == "ADJUSTMENT") return "Ajuste";
    if (t == "WASTE") return "Merma";
    if (t == "TRANSFER") return "Traslado";
    return t;
}

const char* kind_name(contracts::ReportKind k) {
    switch (k) {
        case contracts::ReportKind::SalesByDay: return "Ventas por día";
        case contracts::ReportKind::SalesByProduct: return "Ventas por producto";
        case contracts::ReportKind::SalesByPaymentMethod: return "Ventas por método de pago";
        case contracts::ReportKind::SalesByCashier: return "Ventas por cajero";
        case contracts::ReportKind::InventoryLosses: return "Mermas y ajustes de inventario";
        case contracts::ReportKind::InventoryMovements: return "Movimientos de inventario";
        case contracts::ReportKind::PurchasesSummary: return "Resumen de compras";
    }
    return "Reporte";
}

} // namespace

#define REPORT_PRELUDE(RT)                                                                                       \
    if (!account_service_ || !account_service_->has_permission("reports.view"))                                  \
        return RT::fail(ErrorCode::Forbidden, "Operación denegada: falta de permisos (reports.view)");          \
    std::string start, end;                                                                                      \
    {                                                                                                            \
        const std::string bad_range = local_date::range_bounds(from_date, to_date, start, end);                  \
        if (!bad_range.empty()) return RT::fail(ErrorCode::InvalidInput, bad_range);                             \
    }

// Totales de las cabeceras de venta del rango (una consulta sobre el índice de fecha).
std::string SqliteReportService::header_totals(const std::string& start, const std::string& end, contracts::SalesTotalsDto& t) {
    std::int64_t count = 0, total = 0, discount = 0;
    std::int64_t surcharge = 0, refunds = 0;
    std::string err = query_rows(db_, "SELECT COUNT(*), COALESCE(SUM(total_cents), 0), COALESCE(SUM(discount_cents), 0), "
                                      "COALESCE(SUM(card_surcharge_cents), 0) FROM sales "
                                      "WHERE created_at >= ? AND created_at < ?;", {start, end}, [&](storage::SqliteStatement& st) {
        count = st.get_int64(0); total = st.get_int64(1); discount = st.get_int64(2); surcharge = st.get_int64(3);
    });
    if (!err.empty()) return err;
    err = query_rows(db_, "SELECT COALESCE(SUM(r.amount_cents), 0) FROM refunds r JOIN sales s ON s.id = r.sale_id "
                          "WHERE s.created_at >= ? AND s.created_at < ?;", {start, end}, [&](storage::SqliteStatement& st) {
        refunds = st.get_int64(0);
    });
    if (!err.empty()) return err;
    std::int64_t gross = 0;
    if (!checked_add(total, discount, gross)) return "Desbordamiento numérico al sumar los importes del reporte";
    t.sales_count = count;
    t.gross = Money::from_cents(gross);
    t.discount = Money::from_cents(discount);
    t.net = Money::from_cents(total);
    t.card_surcharges = Money::from_cents(surcharge);
    t.refunds = Money::from_cents(refunds);
    t.net_after_refunds = Money::from_cents(total - refunds);
    return "";
}

// Pagos del rango por método (atribuidos al día de su venta) y la discrepancia con el neto.
std::string SqliteReportService::payment_totals(const std::string& start, const std::string& end, contracts::SalesTotalsDto& t) {
    std::int64_t cash = 0, card = 0, transfer = 0;
    const std::string err = query_rows(db_, "SELECT p.payment_method, COALESCE(SUM(p.amount_cents), 0) FROM payment_records p "
                          "JOIN sales s ON s.id = p.sale_id WHERE s.created_at >= ? AND s.created_at < ? GROUP BY p.payment_method;",
                     {start, end}, [&](storage::SqliteStatement& st) {
        const std::string m = st.get_text(0);
        if (m == "EFECTIVO") cash = st.get_int64(1);
        else if (m == "TARJETA") card = st.get_int64(1);
        else if (m == "TRANSFERENCIA") transfer = st.get_int64(1);
    });
    if (!err.empty()) return err;
    return set_payments(cash, card, transfer, t);
}

std::string SqliteReportService::set_payments(std::int64_t cash, std::int64_t card, std::int64_t transfer, contracts::SalesTotalsDto& t) {
    t.cash = Money::from_cents(cash);
    t.card = Money::from_cents(card);
    t.transfer = Money::from_cents(transfer);
    std::int64_t all = 0;
    if (!checked_add(cash, card, all) || !checked_add(all, transfer, all)) return "Desbordamiento numérico al sumar los pagos del reporte";
    t.payments_total = Money::from_cents(all);
    t.payments_mismatch_cents = t.net.cents() + t.card_surcharges.cents() - all;
    return "";
}

contracts::Result<contracts::SalesByDayReportDto> SqliteReportService::sales_by_day(const std::string& from_date, const std::string& to_date) {
    using RT = contracts::Result<contracts::SalesByDayReportDto>;
    REPORT_PRELUDE(RT)
    contracts::SalesByDayReportDto out;
    out.from_date = from_date; out.to_date = to_date;
    std::string err = query_rows(db_, "SELECT substr(created_at, 1, 10), COUNT(*), COALESCE(SUM(total_cents), 0), COALESCE(SUM(discount_cents), 0), "
                                      "COALESCE(SUM(card_surcharge_cents), 0) "
                                      "FROM sales WHERE created_at >= ? AND created_at < ? GROUP BY substr(created_at, 1, 10) ORDER BY 1;",
                                 {start, end}, [&](storage::SqliteStatement& st) {
        contracts::SalesByDayRowDto r;
        r.date = st.get_text(0);
        r.sales_count = st.get_int64(1);
        r.net = Money::from_cents(st.get_int64(2));
        r.discount = Money::from_cents(st.get_int64(3));
        r.card_surcharges = Money::from_cents(st.get_int64(4));
        r.net_after_refunds = r.net;
        r.gross = Money::from_cents(r.net.cents() + r.discount.cents());
        out.rows.push_back(r);
    });
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    err = query_rows(db_, "SELECT substr(s.created_at, 1, 10), p.payment_method, COALESCE(SUM(p.amount_cents), 0) FROM payment_records p "
                          "JOIN sales s ON s.id = p.sale_id WHERE s.created_at >= ? AND s.created_at < ? GROUP BY 1, 2;",
                     {start, end}, [&](storage::SqliteStatement& st) {
        const std::string d = st.get_text(0), m = st.get_text(1);
        for (auto& r : out.rows) {
            if (r.date != d) continue;
            if (m == "EFECTIVO") r.cash = Money::from_cents(st.get_int64(2));
            else if (m == "TARJETA") r.card = Money::from_cents(st.get_int64(2));
            else if (m == "TRANSFERENCIA") r.transfer = Money::from_cents(st.get_int64(2));
        }
    });
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    // B18: reembolsos por día (atribuidos al día de su venta)
    err = query_rows(db_, "SELECT substr(s.created_at, 1, 10), COALESCE(SUM(r.amount_cents), 0) FROM refunds r JOIN sales s ON s.id = r.sale_id "
                          "WHERE s.created_at >= ? AND s.created_at < ? GROUP BY 1;", {start, end}, [&](storage::SqliteStatement& st) {
        const std::string d = st.get_text(0);
        for (auto& r : out.rows) {
            if (r.date != d) continue;
            r.refunds = Money::from_cents(st.get_int64(1));
            r.net_after_refunds = Money::from_cents(r.net.cents() - r.refunds.cents());
        }
    });
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    err = header_totals(start, end, out.totals);
    if (err.empty()) {
        std::int64_t c = 0, k = 0, tr = 0;
        for (const auto& r : out.rows) {
            if (!checked_add(c, r.cash.cents(), c) || !checked_add(k, r.card.cents(), k) || !checked_add(tr, r.transfer.cents(), tr))
                err = "Desbordamiento numérico al sumar los pagos del reporte";
        }
        if (err.empty()) err = set_payments(c, k, tr, out.totals);
    }
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    return RT::ok(std::move(out));
}

contracts::Result<contracts::SalesByProductReportDto> SqliteReportService::sales_by_product(const std::string& from_date, const std::string& to_date) {
    using RT = contracts::Result<contracts::SalesByProductReportDto>;
    REPORT_PRELUDE(RT)
    contracts::SalesByProductReportDto out;
    out.from_date = from_date; out.to_date = to_date;
    std::string err = query_rows(db_, "SELECT si.product_id, MAX(si.product_name), SUM(si.quantity), SUM(si.subtotal_cents), SUM(si.discount_cents) "
                                      "FROM sale_items si JOIN sales s ON s.id = si.sale_id WHERE s.created_at >= ? AND s.created_at < ? "
                                      "GROUP BY si.product_id ORDER BY SUM(si.subtotal_cents) - SUM(si.discount_cents) DESC, MAX(si.product_name);",
                                 {start, end}, [&](storage::SqliteStatement& st) {
        contracts::SalesByProductRowDto r;
        r.product_id = st.get_text(0);
        r.product_name = st.get_text(1);
        r.pieces = st.get_int64(2);
        r.gross = Money::from_cents(st.get_int64(3));
        r.discount = Money::from_cents(st.get_int64(4));
        r.net = Money::from_cents(r.gross.cents() - r.discount.cents());
        r.net_after_refunds = r.net;
        out.rows.push_back(r);
    });
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    // B18: piezas e importes reembolsados por producto; pieces pasa a ser vendidas MENOS reembolsadas.
    err = query_rows(db_, "SELECT ri.product_id, COALESCE(SUM(ri.quantity), 0), COALESCE(SUM(ri.amount_cents), 0) FROM refund_items ri "
                          "JOIN refunds r ON r.id = ri.refund_id JOIN sales s ON s.id = r.sale_id "
                          "WHERE s.created_at >= ? AND s.created_at < ? GROUP BY ri.product_id;", {start, end}, [&](storage::SqliteStatement& st) {
        const std::string pid = st.get_text(0);
        for (auto& r : out.rows) {
            if (r.product_id != pid) continue;
            r.pieces_refunded = st.get_int64(1);
            r.pieces -= r.pieces_refunded;
            r.refunds = Money::from_cents(st.get_int64(2));
            r.net_after_refunds = Money::from_cents(r.net.cents() - r.refunds.cents());
        }
    });
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    err = header_totals(start, end, out.totals);
    if (err.empty()) err = payment_totals(start, end, out.totals);
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    for (const auto& r : out.rows) {
        if (!checked_add(out.totals.pieces, r.pieces, out.totals.pieces)) return RT::fail(ErrorCode::InvalidInput, "Desbordamiento numérico al sumar las piezas");
    }
    return RT::ok(std::move(out));
}

contracts::Result<contracts::SalesByPaymentReportDto> SqliteReportService::sales_by_payment_method(const std::string& from_date, const std::string& to_date) {
    using RT = contracts::Result<contracts::SalesByPaymentReportDto>;
    REPORT_PRELUDE(RT)
    contracts::SalesByPaymentReportDto out;
    out.from_date = from_date; out.to_date = to_date;
    std::string err = query_rows(db_, "SELECT p.payment_method, COUNT(*), COALESCE(SUM(p.amount_cents), 0) FROM payment_records p "
                                      "JOIN sales s ON s.id = p.sale_id WHERE s.created_at >= ? AND s.created_at < ? "
                                      "GROUP BY p.payment_method ORDER BY p.payment_method;",
                                 {start, end}, [&](storage::SqliteStatement& st) {
        contracts::SalesByPaymentRowDto r;
        r.method = st.get_text(0);
        r.payments_count = st.get_int64(1);
        r.amount = Money::from_cents(st.get_int64(2));
        out.rows.push_back(r);
    });
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    err = query_rows(db_, "SELECT r.payment_method, COALESCE(SUM(r.amount_cents), 0) FROM refunds r JOIN sales s ON s.id = r.sale_id "
                          "WHERE s.created_at >= ? AND s.created_at < ? GROUP BY r.payment_method;", {start, end}, [&](storage::SqliteStatement& st) {
        const std::string m = st.get_text(0);
        for (auto& r : out.rows) if (r.method == m) r.refunds = Money::from_cents(st.get_int64(1));
    });
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    err = header_totals(start, end, out.totals);
    if (err.empty()) {
        std::int64_t c = 0, k = 0, tr = 0;
        for (const auto& r : out.rows) {
            std::int64_t* b = r.method == "EFECTIVO" ? &c : (r.method == "TARJETA" ? &k : &tr);
            if (!checked_add(*b, r.amount.cents(), *b)) err = "Desbordamiento numérico al sumar los pagos del reporte";
        }
        if (err.empty()) err = set_payments(c, k, tr, out.totals);
    }
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    return RT::ok(std::move(out));
}

contracts::Result<contracts::SalesByCashierReportDto> SqliteReportService::sales_by_cashier(const std::string& from_date, const std::string& to_date) {
    using RT = contracts::Result<contracts::SalesByCashierReportDto>;
    REPORT_PRELUDE(RT)
    contracts::SalesByCashierReportDto out;
    out.from_date = from_date; out.to_date = to_date;
    std::string err = query_rows(db_, "SELECT s.user_id, COALESCE(MAX(NULLIF(s.cashier_name, '')), MAX(u.display_name), ''), COUNT(*), "
                                      "COALESCE(SUM(s.total_cents), 0), COALESCE(SUM(s.discount_cents), 0), COALESCE(SUM(s.card_surcharge_cents), 0) FROM sales s "
                                      "LEFT JOIN users u ON u.id = s.user_id WHERE s.created_at >= ? AND s.created_at < ? "
                                      "GROUP BY s.user_id ORDER BY SUM(s.total_cents) DESC, 2;",
                                 {start, end}, [&](storage::SqliteStatement& st) {
        contracts::SalesByCashierRowDto r;
        r.user_id = st.get_text(0);
        r.cashier_name = st.get_text(1);
        r.sales_count = st.get_int64(2);
        r.net = Money::from_cents(st.get_int64(3));
        r.discount = Money::from_cents(st.get_int64(4));
        r.card_surcharges = Money::from_cents(st.get_int64(5));
        r.gross = Money::from_cents(r.net.cents() + r.discount.cents());
        r.net_after_refunds = r.net;
        out.rows.push_back(r);
    });
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    err = query_rows(db_, "SELECT s.user_id, COALESCE(SUM(r.amount_cents), 0) FROM refunds r JOIN sales s ON s.id = r.sale_id "
                          "WHERE s.created_at >= ? AND s.created_at < ? GROUP BY s.user_id;", {start, end}, [&](storage::SqliteStatement& st) {
        const std::string uid = st.get_text(0);
        for (auto& r : out.rows) {
            if (r.user_id != uid) continue;
            r.refunds = Money::from_cents(st.get_int64(1));
            r.net_after_refunds = Money::from_cents(r.net.cents() - r.refunds.cents());
        }
    });
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    err = header_totals(start, end, out.totals);
    if (err.empty()) err = payment_totals(start, end, out.totals);
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    return RT::ok(std::move(out));
}

contracts::Result<contracts::InventoryLossReportDto> SqliteReportService::inventory_losses(const std::string& from_date, const std::string& to_date) {
    using RT = contracts::Result<contracts::InventoryLossReportDto>;
    REPORT_PRELUDE(RT)
    contracts::InventoryLossReportDto out;
    out.from_date = from_date; out.to_date = to_date;
    std::string err = query_rows(db_,
        "SELECT m.ingredient_id, i.name, i.base_unit, "
        "COALESCE(SUM(CASE WHEN m.movement_type = 'WASTE' THEN -m.quantity_base_units ELSE 0 END), 0), "
        "COALESCE(SUM(CASE WHEN m.movement_type = 'ADJUSTMENT' AND m.refund_id IS NULL AND m.quantity_base_units > 0 THEN m.quantity_base_units ELSE 0 END), 0), "
        "COALESCE(SUM(CASE WHEN m.movement_type = 'ADJUSTMENT' AND m.refund_id IS NULL AND m.quantity_base_units < 0 THEN -m.quantity_base_units ELSE 0 END), 0) "
        "FROM stock_movements m JOIN ingredients i ON i.id = m.ingredient_id "
        "WHERE m.created_at >= ? AND m.created_at < ? AND m.movement_type IN ('WASTE', 'ADJUSTMENT') AND (m.movement_type = 'WASTE' OR m.refund_id IS NULL) "
        "GROUP BY m.ingredient_id ORDER BY i.name;",
        {start, end}, [&](storage::SqliteStatement& st) {
        contracts::InventoryLossRowDto r;
        r.ingredient_id = st.get_text(0);
        r.ingredient_name = st.get_text(1);
        r.base_unit = st.get_text(2);
        r.waste_quantity_base = st.get_int64(3);
        r.adjustment_in_quantity_base = st.get_int64(4);
        r.adjustment_out_quantity_base = st.get_int64(5);
        out.rows.push_back(r);
    });
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    std::int64_t tw = 0, tin = 0, tout = 0;
    for (auto& r : out.rows) {
        auto state = read_stock_state(db_, r.ingredient_id);
        if (!state.is_success() || !state.value().has_cost_basis()) continue;   // costo desconocido: se queda en 0
        auto cost = [&](std::int64_t qty, Money& dst) -> bool {
            if (qty <= 0) return true;
            auto c = domain::cost_micro_for_quantity(state.value(), qty);
            if (!c.is_success()) return false;
            dst = Money::from_cents(domain::micro_to_cents(c.value().micro));   // redondeo al final, por renglón
            return true;
        };
        if (!cost(r.waste_quantity_base, r.waste_estimated_cost) ||
            !cost(r.adjustment_in_quantity_base, r.adjustment_in_estimated_cost) ||
            !cost(r.adjustment_out_quantity_base, r.adjustment_out_estimated_cost))
            return RT::fail(ErrorCode::InvalidInput, "Desbordamiento numérico al estimar el costo de las mermas");
        r.has_known_cost = true;
        if (!checked_add(tw, r.waste_estimated_cost.cents(), tw) || !checked_add(tin, r.adjustment_in_estimated_cost.cents(), tin) ||
            !checked_add(tout, r.adjustment_out_estimated_cost.cents(), tout))
            return RT::fail(ErrorCode::InvalidInput, "Desbordamiento numérico al sumar los costos estimados");
    }
    out.total_waste_estimated_cost = Money::from_cents(tw);
    out.total_adjustment_in_estimated_cost = Money::from_cents(tin);
    out.total_adjustment_out_estimated_cost = Money::from_cents(tout);
    return RT::ok(std::move(out));
}

// limit <= 0: sin límite (solo para la exportación).
contracts::Result<contracts::InventoryMovementsReportDto> SqliteReportService::movements_impl(
    const std::string& from_date, const std::string& to_date, std::int64_t limit) {
    using RT = contracts::Result<contracts::InventoryMovementsReportDto>;
    REPORT_PRELUDE(RT)
    contracts::InventoryMovementsReportDto out;
    out.from_date = from_date; out.to_date = to_date;
    std::string sql = "SELECT m.created_at, i.name, m.movement_type, m.quantity_base_units, i.base_unit, COALESCE(m.reason, ''), "
                      "COALESCE(u.display_name, '') FROM stock_movements m JOIN ingredients i ON i.id = m.ingredient_id "
                      "LEFT JOIN users u ON u.id = m.actor_user_id WHERE m.created_at >= ? AND m.created_at < ? "
                      "ORDER BY m.created_at DESC, m.rowid DESC";
    if (limit > 0) sql += " LIMIT " + std::to_string(limit + 1);
    sql += ";";
    const std::string err = query_rows(db_, sql, {start, end}, [&](storage::SqliteStatement& st) {
        if (limit > 0 && static_cast<std::int64_t>(out.rows.size()) >= limit) { out.truncated = true; return; }
        contracts::InventoryMovementRowDto r;
        r.created_at = st.get_text(0);
        r.ingredient_name = st.get_text(1);
        r.movement_type = st.get_text(2);
        r.quantity_base = st.get_int64(3);
        r.base_unit = st.get_text(4);
        r.reason = st.get_text(5);
        r.actor_name = st.get_text(6);
        out.rows.push_back(std::move(r));
    });
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    return RT::ok(std::move(out));
}

contracts::Result<contracts::InventoryMovementsReportDto> SqliteReportService::inventory_movements(
    const std::string& from_date, const std::string& to_date, int limit) {
    if (limit <= 0) limit = 1000;
    if (limit > 5000) limit = 5000;
    return movements_impl(from_date, to_date, limit);
}

contracts::Result<contracts::PurchasesSummaryReportDto> SqliteReportService::purchases_summary(const std::string& from_date, const std::string& to_date) {
    using RT = contracts::Result<contracts::PurchasesSummaryReportDto>;
    REPORT_PRELUDE(RT)
    contracts::PurchasesSummaryReportDto out;
    out.from_date = from_date; out.to_date = to_date;
    std::int64_t total = 0;
    bool overflow = false;
    const std::string err = query_rows(db_,
        "SELECT p.ingredient_id, i.name, i.base_unit, COUNT(*), SUM(p.base_units_purchased), SUM(p.total_cost_cents) "
        "FROM purchase_entries p JOIN ingredients i ON i.id = p.ingredient_id "
        "WHERE p.created_at >= ? AND p.created_at < ? GROUP BY p.ingredient_id ORDER BY i.name;",
        {start, end}, [&](storage::SqliteStatement& st) {
        contracts::PurchasesSummaryRowDto r;
        r.ingredient_id = st.get_text(0);
        r.ingredient_name = st.get_text(1);
        r.base_unit = st.get_text(2);
        r.purchases_count = st.get_int64(3);
        r.base_units_purchased = st.get_int64(4);
        r.total_cost = Money::from_cents(st.get_int64(5));
        if (!checked_add(total, r.total_cost.cents(), total)) overflow = true;
        out.rows.push_back(r);
    });
    if (!err.empty()) return RT::fail(ErrorCode::StorageFailure, err);
    if (overflow) return RT::fail(ErrorCode::InvalidInput, "Desbordamiento numérico al sumar las compras");
    out.total_cost = Money::from_cents(total);
    return RT::ok(std::move(out));
}

contracts::Result<contracts::ReportExportResultDto> SqliteReportService::export_report_csv(
    contracts::ReportKind kind, const std::string& from_date, const std::string& to_date,
    const std::string& file_path, bool overwrite) {
    using RT = contracts::Result<contracts::ReportExportResultDto>;
    if (!account_service_ || !account_service_->has_permission("reports.view"))
        return RT::fail(ErrorCode::Forbidden, "Operación denegada: falta de permisos (reports.view)");
    if (file_path.empty()) return RT::fail(ErrorCode::InvalidInput, "Elige dónde guardar el archivo");

    std::vector<std::string> header;
    std::vector<std::vector<std::string>> rows;
    auto fail_inner = [&](const auto& r) { return RT::fail(r.error_code(), r.error_message()); };

    switch (kind) {
        case contracts::ReportKind::SalesByDay: {
            auto r = sales_by_day(from_date, to_date);
            if (!r.is_success()) return fail_inner(r);
            header = {"Fecha", "Ventas", "Importe bruto", "Descuento", "Importe neto", "Efectivo", "Tarjeta", "Transferencia",
                      "Cargo por tarjeta", "Reembolsos", "Neto después de reembolsos"};
            for (const auto& x : r.value().rows)
                rows.push_back({csv_text(x.date), csv_int(x.sales_count), csv_money(x.gross.cents()), csv_money(x.discount.cents()),
                                csv_money(x.net.cents()), csv_money(x.cash.cents()), csv_money(x.card.cents()), csv_money(x.transfer.cents()),
                                csv_money(x.card_surcharges.cents()), csv_money(x.refunds.cents()), csv_money(x.net_after_refunds.cents())});
            const auto& t = r.value().totals;
            rows.push_back({"Total", csv_int(t.sales_count), csv_money(t.gross.cents()), csv_money(t.discount.cents()), csv_money(t.net.cents()),
                            csv_money(t.cash.cents()), csv_money(t.card.cents()), csv_money(t.transfer.cents()),
                            csv_money(t.card_surcharges.cents()), csv_money(t.refunds.cents()), csv_money(t.net_after_refunds.cents())});
            break;
        }
        case contracts::ReportKind::SalesByProduct: {
            auto r = sales_by_product(from_date, to_date);
            if (!r.is_success()) return fail_inner(r);
            header = {"Producto", "Piezas", "Importe bruto", "Descuento", "Importe neto", "Piezas reembolsadas", "Reembolsos", "Neto después de reembolsos"};
            std::int64_t refunded_pieces = 0;
            for (const auto& x : r.value().rows) {
                refunded_pieces += x.pieces_refunded;
                rows.push_back({csv_text(x.product_name), csv_int(x.pieces), csv_money(x.gross.cents()), csv_money(x.discount.cents()), csv_money(x.net.cents()),
                                csv_int(x.pieces_refunded), csv_money(x.refunds.cents()), csv_money(x.net_after_refunds.cents())});
            }
            const auto& t = r.value().totals;
            rows.push_back({"Total", csv_int(t.pieces), csv_money(t.gross.cents()), csv_money(t.discount.cents()), csv_money(t.net.cents()),
                            csv_int(refunded_pieces), csv_money(t.refunds.cents()), csv_money(t.net_after_refunds.cents())});
            break;
        }
        case contracts::ReportKind::SalesByPaymentMethod: {
            auto r = sales_by_payment_method(from_date, to_date);
            if (!r.is_success()) return fail_inner(r);
            header = {"Método de pago", "Pagos", "Importe", "Reembolsos"};
            for (const auto& x : r.value().rows)
                rows.push_back({csv_text(method_label(x.method)), csv_int(x.payments_count), csv_money(x.amount.cents()), csv_money(x.refunds.cents())});
            rows.push_back({"Total", csv_int(r.value().totals.sales_count), csv_money(r.value().totals.payments_total.cents()),
                            csv_money(r.value().totals.refunds.cents())});
            break;
        }
        case contracts::ReportKind::SalesByCashier: {
            auto r = sales_by_cashier(from_date, to_date);
            if (!r.is_success()) return fail_inner(r);
            header = {"Cajero", "Ventas", "Importe bruto", "Descuento", "Importe neto", "Cargo por tarjeta", "Reembolsos", "Neto después de reembolsos"};
            for (const auto& x : r.value().rows)
                rows.push_back({csv_text(x.cashier_name), csv_int(x.sales_count), csv_money(x.gross.cents()), csv_money(x.discount.cents()), csv_money(x.net.cents()),
                                csv_money(x.card_surcharges.cents()), csv_money(x.refunds.cents()), csv_money(x.net_after_refunds.cents())});
            const auto& t = r.value().totals;
            rows.push_back({"Total", csv_int(t.sales_count), csv_money(t.gross.cents()), csv_money(t.discount.cents()), csv_money(t.net.cents()),
                            csv_money(t.card_surcharges.cents()), csv_money(t.refunds.cents()), csv_money(t.net_after_refunds.cents())});
            break;
        }
        case contracts::ReportKind::InventoryLosses: {
            auto r = inventory_losses(from_date, to_date);
            if (!r.is_success()) return fail_inner(r);
            header = {"Ingrediente", "Unidad", "Merma (cantidad)", "Merma (costo estimado)", "Ajustes que suman (cantidad)",
                      "Ajustes que suman (costo estimado)", "Ajustes que restan (cantidad)", "Ajustes que restan (costo estimado)", "Costo conocido"};
            for (const auto& x : r.value().rows)
                rows.push_back({csv_text(x.ingredient_name), csv_text(x.base_unit), csv_int(x.waste_quantity_base), csv_money(x.waste_estimated_cost.cents()),
                                csv_int(x.adjustment_in_quantity_base), csv_money(x.adjustment_in_estimated_cost.cents()),
                                csv_int(x.adjustment_out_quantity_base), csv_money(x.adjustment_out_estimated_cost.cents()), x.has_known_cost ? "Sí" : "No"});
            rows.push_back({"Total", "", "", csv_money(r.value().total_waste_estimated_cost.cents()), "",
                            csv_money(r.value().total_adjustment_in_estimated_cost.cents()), "",
                            csv_money(r.value().total_adjustment_out_estimated_cost.cents()), ""});
            break;
        }
        case contracts::ReportKind::InventoryMovements: {
            auto r = movements_impl(from_date, to_date, 0);
            if (!r.is_success()) return fail_inner(r);
            header = {"Fecha y hora", "Ingrediente", "Tipo de movimiento", "Cantidad", "Unidad", "Motivo", "Responsable"};
            for (const auto& x : r.value().rows)
                rows.push_back({csv_text(x.created_at), csv_text(x.ingredient_name), csv_text(movement_label(x.movement_type)), csv_int(x.quantity_base),
                                csv_text(x.base_unit), csv_text(x.reason), csv_text(x.actor_name)});
            break;
        }
        case contracts::ReportKind::PurchasesSummary: {
            auto r = purchases_summary(from_date, to_date);
            if (!r.is_success()) return fail_inner(r);
            header = {"Ingrediente", "Unidad", "Compras", "Cantidad comprada", "Costo total"};
            for (const auto& x : r.value().rows)
                rows.push_back({csv_text(x.ingredient_name), csv_text(x.base_unit), csv_int(x.purchases_count), csv_int(x.base_units_purchased), csv_money(x.total_cost.cents())});
            rows.push_back({"Total", "", "", "", csv_money(r.value().total_cost.cents())});
            break;
        }
    }

    namespace fs = std::filesystem;
    const fs::path target = fs::u8path(file_path);
    const std::string file_name = target.filename().u8string();
    if (file_name.empty() || file_name == "." || file_name == "..")
        return RT::fail(ErrorCode::InvalidInput, "Elige un nombre de archivo");
    std::error_code ec;
    const fs::path dir = target.has_parent_path() ? target.parent_path() : fs::path(".");
    if (!fs::is_directory(dir, ec))
        return RT::fail(ErrorCode::InvalidInput, "No se encontró la carpeta o la USB. Revisa que esté conectada y elige otra ubicación.");
    if (fs::is_directory(target, ec))
        return RT::fail(ErrorCode::InvalidInput, "Elige un nombre de archivo, no una carpeta");
    if (fs::exists(target, ec) && !overwrite)
        return RT::fail(ErrorCode::Conflict, "Ya existe un archivo con ese nombre. Elige otro nombre o confirma que quieres reemplazarlo.");

    std::string content = "\xEF\xBB\xBF";
    auto put_line = [&](const std::vector<std::string>& cells) {
        for (std::size_t i = 0; i < cells.size(); ++i) { if (i) content += ','; content += cells[i]; }
        content += "\r\n";
    };
    put_line(header);
    for (const auto& r : rows) put_line(r);

    const char* kWriteError = "No se pudo guardar el archivo. Revisa que la carpeta o la USB tenga espacio y permiso para escribir.";
    const fs::path tmp = dir / fs::u8path(file_name + ".parcial-" + generate_id("csv").substr(0, 12));
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return RT::fail(ErrorCode::StorageFailure, kWriteError);
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out) { out.close(); fs::remove(tmp, ec); return RT::fail(ErrorCode::StorageFailure, kWriteError); }
    }
    fs::rename(tmp, target, ec);
    if (ec) {
        std::error_code ec2;
        fs::remove(tmp, ec2);
        return RT::fail(ErrorCode::StorageFailure, kWriteError);
    }

    auto audit = account_service_->record_audit_event(
        "REPORT_EXPORTED",
        std::string(kind_name(kind)) + " del " + from_date + " al " + to_date + " exportado a CSV (" + file_name + ")");
    (void)audit;   // el archivo ya está guardado; una falla de la bitácora no lo borra

    contracts::ReportExportResultDto res;
    res.file_name = file_name;
    res.rows_written = static_cast<std::int64_t>(rows.size());
    return RT::ok(res);
}

} // namespace app::services
