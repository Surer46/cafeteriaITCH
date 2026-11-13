#ifndef APP_CRYPTO_RANDOM_BYTES_HPP
#define APP_CRYPTO_RANDOM_BYTES_HPP

#include <cstddef>
#include <cstdint>

namespace app::crypto {

// Llena el búfer con bytes aleatorios del generador del sistema operativo
// (CryptGenRandom en Windows, /dev/urandom en el resto). No usa std::random_device
// ni ningún respaldo determinista. Devuelve false si el sistema no entrega entropía.
bool random_bytes(std::uint8_t* buffer, std::size_t count);

} // namespace app::crypto

#endif // APP_CRYPTO_RANDOM_BYTES_HPP
