#ifndef APP_SERVICES_UPDATE_SIGNATURE_HPP
#define APP_SERVICES_UPDATE_SIGNATURE_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Verificación de firmas de actualizaciones: ECDSA P-256 con SHA-256 mediante CNG (bcrypt.dll, disponible desde
// Windows Vista). La firma es cruda de 64 bytes (r||s) y la clave pública de 64 bytes (X||Y).
namespace app::services {

using PublicKey64 = std::array<std::uint8_t, 64>;

// Convierte 128 caracteres hexadecimales en una clave pública. false si el texto no es exactamente eso.
bool parse_public_key_hex(const std::string& hex, PublicKey64& out);

// true solo si `signature` (64 bytes) es una firma válida de SHA-256(data) con esa clave.
bool verify_signature_p256(const std::uint8_t* data, std::size_t length,
                           const std::uint8_t* signature, std::size_t signature_length,
                           const PublicKey64& key);

// true si alguna de las claves verifica la firma.
bool verify_signature_any(const std::vector<std::uint8_t>& data, const std::vector<std::uint8_t>& signature,
                          const std::vector<PublicKey64>& keys);

} // namespace app::services

#endif // APP_SERVICES_UPDATE_SIGNATURE_HPP
