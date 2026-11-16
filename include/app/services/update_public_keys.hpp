#ifndef APP_SERVICES_UPDATE_PUBLIC_KEYS_HPP
#define APP_SERVICES_UPDATE_PUBLIC_KEYS_HPP

#include <vector>
#include "app/services/update_signature.hpp"

namespace app::services {

// Claves públicas incrustadas en el programa (src/services/update_public_keys.inc), ya validadas. Vacío si no se
// configuró ninguna: en ese caso no se hace ninguna petición de red de actualizaciones.
// En compilaciones con CAFETERIA_TEST_HOOKS y con la variable de entorno de pruebas de actualización definida se
// añade una ranura con la clave PÚBLICA de prueba (la privada vive en tests/ y solo vale en esas compilaciones).
std::vector<PublicKey64> configured_update_keys();

} // namespace app::services

#endif
