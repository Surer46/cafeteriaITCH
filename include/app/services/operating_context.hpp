#ifndef APP_SERVICES_OPERATING_CONTEXT_HPP
#define APP_SERVICES_OPERATING_CONTEXT_HPP

#include <string>
#include "app/contracts/result.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

// Caja (y turno) con la que opera un usuario. Única función que decide la caja del carrito, de la
// venta y de la comprobación del reintento idempotente.
struct OperatingContext {
    std::string register_id;
    std::string shift_id;       // vacío si no hay turno abierto (solo con allow_no_shift)
    bool has_open_shift{false};
};

// Orden de resolución:
//  1. El turno abierto del propio usuario (el más reciente).
//  2. Si no tiene, y hay exactamente un turno abierto en total, ese turno (caso de una sola caja).
//  3. Si hay varios turnos abiertos y ninguno es del usuario: InvalidState ("abra su propio turno").
//  4. Sin ningún turno abierto:
//       - allow_no_shift = false: ShiftClosed.
//       - allow_no_shift = true (carrito): si existe una sola caja registrada se usa esa;
//         con varias cajas, InvalidState ("abra su turno").
contracts::Result<OperatingContext> resolve_operating_context(
    storage::SqliteDatabase& db,
    const std::string& user_id,
    bool allow_no_shift
);

} // namespace app::services

#endif // APP_SERVICES_OPERATING_CONTEXT_HPP
