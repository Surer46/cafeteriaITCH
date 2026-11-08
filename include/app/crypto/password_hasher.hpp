#ifndef APP_CRYPTO_PASSWORD_HASHER_HPP
#define APP_CRYPTO_PASSWORD_HASHER_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace app::crypto {

/**
 * Parámetros documentados de derivación de claves:
 * - Algoritmo: PBKDF2 con HMAC-SHA256 (RFC 2898)
 * - Iteraciones: 100,000 (estándar recomendado por OWASP)
 * - Sal: 16 bytes (128 bits) aleatorios criptográficos individuales por usuario
 * - Longitud de clave derivada: 32 bytes (256 bits)
 * - Formato serializado: pbkdf2_sha256$<iteraciones>$<sal_hex>$<hash_hex>
 */
class PasswordHasher {
public:
    static constexpr int DEFAULT_ITERATIONS = 100000;
    static constexpr size_t SALT_BYTES = 16;
    static constexpr size_t KEY_BYTES = 32;

    // Genera un hash seguro con sal aleatoria nueva
    static std::string hash_password(
        const std::string& raw_password,
        int iterations = DEFAULT_ITERATIONS
    );

    // Verifica una contraseña contra el hash almacenado (en tiempo constante)
    static bool verify_password(
        const std::string& raw_password,
        const std::string& stored_hash
    );

    // Funciones criptográficas primitivas
    static std::vector<uint8_t> generate_salt(size_t num_bytes = SALT_BYTES);
    static std::vector<uint8_t> sha256(const uint8_t* data, size_t len);
    static std::vector<uint8_t> hmac_sha256(
        const uint8_t* key, size_t key_len,
        const uint8_t* data, size_t data_len
    );
    static std::vector<uint8_t> pbkdf2_hmac_sha256(
        const std::string& password,
        const std::vector<uint8_t>& salt,
        int iterations,
        size_t key_length
    );

    static std::string to_hex(const std::vector<uint8_t>& bytes);
    static std::vector<uint8_t> from_hex(const std::string& hex);
};

} // namespace app::crypto

#endif // APP_CRYPTO_PASSWORD_HASHER_HPP
