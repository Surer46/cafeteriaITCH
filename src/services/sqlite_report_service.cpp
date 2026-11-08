#include "app/services/sqlite_report_service.hpp"

#include <cstdint>
#include <cstdio>

namespace app::services {

namespace {

using contracts::ErrorCode;
using Report = contracts::DailySalesReportDto;
using R = contracts::Result<Report>;

bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

int days_in_month(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}

// Valida YYYY-MM-DD estricto.
bool parse_date(const std::string& s, int& y, int& m, int& d) {
    if (s.size() != 10 || s[4] != '-' || s[7] != '-') return false;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (i == 4 || i == 7) continue;
        if (s[i] < '0' || s[i] > '9') return false;
    }
    y = std::stoi(s.substr(0, 4));
    m = std::stoi(s.substr(5, 2));
    d = std::stoi(s.substr(8, 2));
    if (y < 1970 || m < 1 || m > 12 || d < 1) return false;
    return d <= days_in_month(y, m);
}

std::string format_date(int y, int m, int d) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
    return buf;
}

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
) : db_(db), account_service_(std::move(account_service)) {}

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
        auto prep = db_.prepare("SELECT total_cents FROM sales WHERE created_at >= ? AND created_at < ?;");
        if (!prep.is_success()) return R::fail(prep.error_code(), prep.error_message());
        auto stmt = std::move(prep.value());
        stmt.bind_text(1, start);
        stmt.bind_text(2, end);
        std::int64_t total = 0, count = 0;
        while (true) {
            auto step = stmt.step();
            if (!step.is_success()) return R::fail(step.error_code(), step.error_message());
            if (!step.value()) break;
            if (!checked_add(total, stmt.get_int64(0), total) || !checked_add(count, 1, count)) return overflow_error();
        }
        report.total_sales_count = count;
        report.total_amount = contracts::Money::from_cents(total);
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
    report.payments_mismatch_cents = report.total_amount.cents() - report.payments_total.cents();
    return R::ok(report);
}

} // namespace app::services
