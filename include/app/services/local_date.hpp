#ifndef APP_SERVICES_LOCAL_DATE_HPP
#define APP_SERVICES_LOCAL_DATE_HPP

// Fechas locales AAAA-MM-DD para los reportes y el historial de turnos (B15). Solo enteros; sin zona horaria:
// las fechas del sistema se guardan en hora local ("YYYY-MM-DD HH:MM:SS").
#include <cstdint>
#include <cstdio>
#include <string>

namespace app::services::local_date {

inline bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

inline int days_in_month(int y, int m) {
    static const int d[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    return (m == 2 && is_leap(y)) ? 29 : d[m - 1];
}

// Valida AAAA-MM-DD estricto (año >= 1970).
inline bool parse(const std::string& s, int& y, int& m, int& d) {
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

inline std::string format(int y, int m, int d) {
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d", y, m, d);
    return buf;
}

// Día siguiente (AAAA-MM-DD válido).
inline std::string next_day(int y, int m, int d) {
    int ny = y, nm = m, nd = d + 1;
    if (nd > days_in_month(y, m)) {
        nd = 1;
        if (++nm > 12) { nm = 1; ++ny; }
    }
    return format(ny, nm, nd);
}

// Días desde 1970-01-01 (algoritmo de calendario civil).
inline std::int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? -3 : 9)) + 2u) / 5u + static_cast<unsigned>(d) - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

// Rango [start, end) para filtrar texto "YYYY-MM-DD HH:MM:SS". Devuelve "" si es válido o un mensaje en lenguaje llano.
inline std::string range_bounds(const std::string& from, const std::string& to, std::string& start, std::string& end,
                                int max_days = 366) {
    int fy = 0, fm = 0, fd = 0, ty = 0, tm = 0, td = 0;
    if (!parse(from, fy, fm, fd) || !parse(to, ty, tm, td))
        return "Fecha inválida: use el formato AAAA-MM-DD con una fecha real";
    const std::int64_t days = days_from_civil(ty, tm, td) - days_from_civil(fy, fm, fd) + 1;
    if (days < 1) return "La fecha inicial no puede ser posterior a la fecha final";
    if (days > max_days) return "El rango es demasiado largo: elija como máximo 366 días";
    start = format(fy, fm, fd) + " 00:00:00";
    end = next_day(ty, tm, td) + " 00:00:00";
    return "";
}

} // namespace app::services::local_date

#endif // APP_SERVICES_LOCAL_DATE_HPP
