#ifndef APP_DOMAIN_CLOCK_HPP
#define APP_DOMAIN_CLOCK_HPP

#include <chrono>
#include <string>

namespace app::domain {

// Un mismo instante expresado de dos formas.
//   local: "YYYY-MM-DD HH:MM:SS" en hora local, sin zona (formato histórico; lo usan
//          la interfaz, los tickets y el reporte diario).
//   utc:   "YYYY-MM-DDTHH:MM:SS.mmmZ" en UTC, ordenable como texto.
struct Timestamp {
    std::string local;
    std::string utc;
};

Timestamp timestamp_from(std::chrono::system_clock::time_point tp);

// Instante actual. Local y UTC salen del mismo time_point.
Timestamp now();

} // namespace app::domain

#endif // APP_DOMAIN_CLOCK_HPP
