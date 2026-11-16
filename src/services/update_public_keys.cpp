#include "app/services/update_public_keys.hpp"

#include <cstdlib>

namespace app::services {

namespace {
const char* const kConfiguredHex[] = {
#include "update_public_keys.inc"
};

#ifdef CAFETERIA_TEST_HOOKS
// Clave pública de PRUEBA (su privada está en tests/update_test_key.priv). Solo existe en compilaciones con ganchos.
const char* const kTestPublicKeyHex =
#include "update_test_public_key.inc"
;
#endif
} // namespace

std::vector<PublicKey64> configured_update_keys() {
    std::vector<PublicKey64> keys;
    for (const char* hex : kConfiguredHex) {
        PublicKey64 k;
        if (hex && *hex && parse_public_key_hex(hex, k)) keys.push_back(k);
    }
#ifdef CAFETERIA_TEST_HOOKS
    {
        const char* source = std::getenv("CAFETERIA_UPDATE_TEST_SOURCE");
        PublicKey64 k;
        if (source && *source && parse_public_key_hex(kTestPublicKeyHex, k)) keys.push_back(k);
    }
#endif
    return keys;
}

} // namespace app::services
