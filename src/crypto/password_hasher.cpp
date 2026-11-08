#include "app/crypto/password_hasher.hpp"
#include <sstream>
#include <iomanip>
#include <random>
#include <cstring>
#include <array>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace app::crypto {

namespace {

// Rotación a la derecha
inline uint32_t rotr(uint32_t x, uint32_t n) {
    return (x >> n) | (x << (32 - n));
}

// Constantes SHA-256 K
constexpr uint32_t K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

void sha256_transform(uint32_t state[8], const uint8_t data[64]) {
    uint32_t a = state[0], b = state[1], c = state[2], d = state[3];
    uint32_t e = state[4], f = state[5], g = state[6], h = state[7];
    uint32_t w[64];

    for (int i = 0; i < 16; ++i) {
        w[i] = (static_cast<uint32_t>(data[i * 4]) << 24) |
               (static_cast<uint32_t>(data[i * 4 + 1]) << 16) |
               (static_cast<uint32_t>(data[i * 4 + 2]) << 8) |
               (static_cast<uint32_t>(data[i * 4 + 3]));
    }
    for (int i = 16; i < 64; ++i) {
        uint32_t s0 = rotr(w[i - 15], 7) ^ rotr(w[i - 15], 18) ^ (w[i - 15] >> 3);
        uint32_t s1 = rotr(w[i - 2], 17) ^ rotr(w[i - 2], 19) ^ (w[i - 2] >> 10);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }

    for (int i = 0; i < 64; ++i) {
        uint32_t S1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + S1 + ch + K[i] + w[i];
        uint32_t S0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = S0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

// Comparación en tiempo constante para mitigar ataques de temporización
bool constant_time_equals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    unsigned char result = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        result |= static_cast<unsigned char>(a[i] ^ b[i]);
    }
    return result == 0;
}

} // namespace

std::vector<uint8_t> PasswordHasher::sha256(const uint8_t* data, size_t len) {
    uint32_t state[8] = {
        0x6a09e667, 0xbb67ae85, 0x3c6ef372, 0xa54ff53a,
        0x510e527f, 0x9b05688c, 0x1f83d9ab, 0x5be0cd19
    };

    uint64_t total_bits = static_cast<uint64_t>(len) * 8;
    size_t processed = 0;

    while (processed + 64 <= len) {
        sha256_transform(state, data + processed);
        processed += 64;
    }

    uint8_t buffer[128] = {0};
    size_t remaining = len - processed;
    if (remaining > 0) {
        std::memcpy(buffer, data + processed, remaining);
    }
    buffer[remaining] = 0x80;

    size_t pad_len = (remaining < 56) ? 64 : 128;
    for (int i = 0; i < 8; ++i) {
        buffer[pad_len - 1 - i] = static_cast<uint8_t>(total_bits >> (i * 8));
    }

    sha256_transform(state, buffer);
    if (pad_len == 128) {
        sha256_transform(state, buffer + 64);
    }

    std::vector<uint8_t> hash(32);
    for (int i = 0; i < 8; ++i) {
        hash[i * 4] = static_cast<uint8_t>(state[i] >> 24);
        hash[i * 4 + 1] = static_cast<uint8_t>(state[i] >> 16);
        hash[i * 4 + 2] = static_cast<uint8_t>(state[i] >> 8);
        hash[i * 4 + 3] = static_cast<uint8_t>(state[i]);
    }
    return hash;
}

std::vector<uint8_t> PasswordHasher::hmac_sha256(
    const uint8_t* key, size_t key_len,
    const uint8_t* data, size_t data_len
) {
    uint8_t k0[64] = {0};
    if (key_len > 64) {
        auto key_hash = sha256(key, key_len);
        std::memcpy(k0, key_hash.data(), key_hash.size());
    } else {
        std::memcpy(k0, key, key_len);
    }

    uint8_t ipad[64];
    uint8_t opad[64];
    for (int i = 0; i < 64; ++i) {
        ipad[i] = k0[i] ^ 0x36;
        opad[i] = k0[i] ^ 0x5c;
    }

    std::vector<uint8_t> inner(64 + data_len);
    std::memcpy(inner.data(), ipad, 64);
    if (data_len > 0) {
        std::memcpy(inner.data() + 64, data, data_len);
    }
    auto inner_hash = sha256(inner.data(), inner.size());

    std::vector<uint8_t> outer(64 + 32);
    std::memcpy(outer.data(), opad, 64);
    std::memcpy(outer.data() + 64, inner_hash.data(), 32);

    return sha256(outer.data(), outer.size());
}

std::vector<uint8_t> PasswordHasher::pbkdf2_hmac_sha256(
    const std::string& password,
    const std::vector<uint8_t>& salt,
    int iterations,
    size_t key_length
) {
    std::vector<uint8_t> result;
    result.reserve(key_length);

    const auto* pwd_bytes = reinterpret_cast<const uint8_t*>(password.data());
    size_t pwd_len = password.size();

    uint32_t block_index = 1;
    while (result.size() < key_length) {
        // U1 = HMAC(P, S || INT_32_BE(block_index))
        std::vector<uint8_t> salt_block = salt;
        salt_block.push_back(static_cast<uint8_t>(block_index >> 24));
        salt_block.push_back(static_cast<uint8_t>(block_index >> 16));
        salt_block.push_back(static_cast<uint8_t>(block_index >> 8));
        salt_block.push_back(static_cast<uint8_t>(block_index));

        auto u = hmac_sha256(pwd_bytes, pwd_len, salt_block.data(), salt_block.size());
        auto f = u;

        for (int i = 1; i < iterations; ++i) {
            u = hmac_sha256(pwd_bytes, pwd_len, u.data(), u.size());
            for (size_t j = 0; j < 32; ++j) {
                f[j] ^= u[j];
            }
        }

        size_t to_copy = std::min<size_t>(32, key_length - result.size());
        result.insert(result.end(), f.begin(), f.begin() + to_copy);
        block_index++;
    }

    return result;
}

std::vector<uint8_t> PasswordHasher::generate_salt(size_t num_bytes) {
    std::vector<uint8_t> salt(num_bytes);
#ifdef _WIN32
    HCRYPTPROV hCryptProv = 0;
    if (CryptAcquireContext(&hCryptProv, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        CryptGenRandom(hCryptProv, static_cast<DWORD>(num_bytes), salt.data());
        CryptReleaseContext(hCryptProv, 0);
        return salt;
    }
#endif
    // Fallback a std::random_device
    std::random_device rd;
    for (size_t i = 0; i < num_bytes; ++i) {
        salt[i] = static_cast<uint8_t>(rd() & 0xFF);
    }
    return salt;
}

std::string PasswordHasher::to_hex(const std::vector<uint8_t>& bytes) {
    std::ostringstream oss;
    for (uint8_t b : bytes) {
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    }
    return oss.str();
}

std::vector<uint8_t> PasswordHasher::from_hex(const std::string& hex) {
    std::vector<uint8_t> bytes;
    bytes.reserve(hex.length() / 2);
    for (size_t i = 0; i + 1 < hex.length(); i += 2) {
        std::string byte_str = hex.substr(i, 2);
        uint8_t b = static_cast<uint8_t>(std::stoul(byte_str, nullptr, 16));
        bytes.push_back(b);
    }
    return bytes;
}

std::string PasswordHasher::hash_password(const std::string& raw_password, int iterations) {
    auto salt = generate_salt(SALT_BYTES);
    auto derived_key = pbkdf2_hmac_sha256(raw_password, salt, iterations, KEY_BYTES);

    std::ostringstream oss;
    oss << "pbkdf2_sha256$" << iterations << "$"
        << to_hex(salt) << "$"
        << to_hex(derived_key);
    return oss.str();
}

bool PasswordHasher::verify_password(const std::string& raw_password, const std::string& stored_hash) {
    // Formato: pbkdf2_sha256$<iter>$<salt_hex>$<hash_hex>
    if (stored_hash.rfind("pbkdf2_sha256$", 0) != 0) {
        return false;
    }

    size_t first_dollar = stored_hash.find('$');
    if (first_dollar == std::string::npos) return false;

    size_t second_dollar = stored_hash.find('$', first_dollar + 1);
    if (second_dollar == std::string::npos) return false;

    size_t third_dollar = stored_hash.find('$', second_dollar + 1);
    if (third_dollar == std::string::npos) return false;

    std::string iter_str = stored_hash.substr(first_dollar + 1, second_dollar - (first_dollar + 1));
    std::string salt_hex = stored_hash.substr(second_dollar + 1, third_dollar - (second_dollar + 1));
    std::string hash_hex = stored_hash.substr(third_dollar + 1);

    int iterations = 0;
    try {
        iterations = std::stoi(iter_str);
    } catch (...) {
        return false;
    }

    if (iterations <= 0 || salt_hex.empty() || hash_hex.empty()) {
        return false;
    }

    auto salt = from_hex(salt_hex);
    auto expected_hash_bytes = from_hex(hash_hex);

    auto computed_key = pbkdf2_hmac_sha256(raw_password, salt, iterations, expected_hash_bytes.size());
    std::string computed_hex = to_hex(computed_key);

    return constant_time_equals(computed_hex, hash_hex);
}

} // namespace app::crypto
