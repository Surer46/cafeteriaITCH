#include "app/services/identifiers.hpp"
#include "app/crypto/random_bytes.hpp"
#include <cstdint>
#include <stdexcept>

namespace app::services {

std::string generate_id(const std::string& prefix) {
    std::uint8_t b[16];
    if (!crypto::random_bytes(b, sizeof(b))) {
        throw std::runtime_error("No hay fuente de entropia del sistema para generar identificadores");
    }
    b[6] = static_cast<std::uint8_t>((b[6] & 0x0F) | 0x40); // versión 4
    b[8] = static_cast<std::uint8_t>((b[8] & 0x3F) | 0x80); // variante RFC 4122

    static const char hex[] = "0123456789abcdef";
    std::string out = prefix;
    out.push_back('-');
    for (int i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(hex[b[i] >> 4]);
        out.push_back(hex[b[i] & 0x0F]);
    }
    return out;
}

} // namespace app::services
