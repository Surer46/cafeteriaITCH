#ifndef APP_CRYPTO_SHA256_HPP
#define APP_CRYPTO_SHA256_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

namespace app::crypto {

// SHA-256 incremental: `Sha256 h; h.update(p, n); ... auto digest = h.finalize();`.
// Da el mismo resultado que PasswordHasher::sha256 (que sigue siendo la función de una sola pasada
// que usa PBKDF2) pero permite procesar un archivo grande por bloques sin cargarlo entero.
// Después de finalize() el objeto no admite más datos.
class Sha256 {
public:
    Sha256();
    void update(const std::uint8_t* data, std::size_t len);
    std::vector<std::uint8_t> finalize();

private:
    std::uint32_t state_[8];
    std::uint8_t buffer_[64];
    std::size_t buffer_len_{0};
    std::uint64_t total_len_{0};
    bool finished_{false};
};

} // namespace app::crypto

#endif // APP_CRYPTO_SHA256_HPP
