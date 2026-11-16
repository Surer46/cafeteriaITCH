#include "app/services/update_signature.hpp"

#include <cstring>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <bcrypt.h>
#endif

#include "app/crypto/sha256.hpp"

namespace app::services {

bool parse_public_key_hex(const std::string& hex, PublicKey64& out) {
    if (hex.size() != 128) return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (std::size_t i = 0; i < 64; ++i) {
        const int hi = nibble(hex[2 * i]);
        const int lo = nibble(hex[2 * i + 1]);
        if (hi < 0 || lo < 0) return false;
        out[i] = static_cast<std::uint8_t>((hi << 4) | lo);
    }
    return true;
}

bool verify_signature_p256(const std::uint8_t* data, std::size_t length,
                           const std::uint8_t* signature, std::size_t signature_length,
                           const PublicKey64& key) {
#ifdef _WIN32
    if (signature_length != 64 || (!data && length > 0)) return false;

    app::crypto::Sha256 hasher;
    hasher.update(data, length);
    const auto digest = hasher.finalize();

    BCRYPT_ALG_HANDLE alg = nullptr;
    if (BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0) < 0) return false;

    // BCRYPT_ECCKEY_BLOB { dwMagic, cbKey } seguido de X || Y
    std::uint8_t blob[sizeof(BCRYPT_ECCKEY_BLOB) + 64];
    BCRYPT_ECCKEY_BLOB header;
    header.dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header.cbKey = 32;
    std::memcpy(blob, &header, sizeof(header));
    std::memcpy(blob + sizeof(header), key.data(), 64);

    BCRYPT_KEY_HANDLE hkey = nullptr;
    bool ok = false;
    if (BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &hkey, blob, static_cast<ULONG>(sizeof(blob)), 0) >= 0) {
        const NTSTATUS st = BCryptVerifySignature(hkey, nullptr, const_cast<PUCHAR>(digest.data()),
                                                  static_cast<ULONG>(digest.size()),
                                                  const_cast<PUCHAR>(signature), static_cast<ULONG>(signature_length), 0);
        ok = (st == 0);  // STATUS_SUCCESS; STATUS_INVALID_SIGNATURE y cualquier otro valor = rechazada
        BCryptDestroyKey(hkey);
    }
    BCryptCloseAlgorithmProvider(alg, 0);
    return ok;
#else
    (void)data; (void)length; (void)signature; (void)signature_length; (void)key;
    return false;
#endif
}

bool verify_signature_any(const std::vector<std::uint8_t>& data, const std::vector<std::uint8_t>& signature,
                          const std::vector<PublicKey64>& keys) {
    for (const auto& k : keys) {
        if (verify_signature_p256(data.data(), data.size(), signature.data(), signature.size(), k)) return true;
    }
    return false;
}

} // namespace app::services
