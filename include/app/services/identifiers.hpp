#ifndef APP_SERVICES_IDENTIFIERS_HPP
#define APP_SERVICES_IDENTIFIERS_HPP

#include <string>

namespace app::services {

// Identificador "<prefijo>-<UUID v4>" con 122 bits aleatorios del generador del
// sistema operativo (app::crypto::random_bytes). Lanza std::runtime_error si el
// sistema no entrega entropía: nunca produce un identificador predecible.
std::string generate_id(const std::string& prefix);

} // namespace app::services

#endif // APP_SERVICES_IDENTIFIERS_HPP
