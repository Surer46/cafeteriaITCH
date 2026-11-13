#include "app/crypto/random_bytes.hpp"

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#else
#include <cstdio>
#endif

namespace app::crypto {

bool random_bytes(std::uint8_t* buffer, std::size_t count) {
    if (count == 0) return true;
    if (buffer == nullptr) return false;
#ifdef _WIN32
    HCRYPTPROV prov = 0;
    if (!CryptAcquireContext(&prov, NULL, NULL, PROV_RSA_FULL, CRYPT_VERIFYCONTEXT)) {
        return false;
    }
    const bool ok = CryptGenRandom(prov, static_cast<DWORD>(count), buffer) != 0;
    CryptReleaseContext(prov, 0);
    return ok;
#else
    std::FILE* f = std::fopen("/dev/urandom", "rb");
    if (!f) return false;
    const std::size_t got = std::fread(buffer, 1, count, f);
    std::fclose(f);
    return got == count;
#endif
}

} // namespace app::crypto
