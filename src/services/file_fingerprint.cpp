#include "app/services/file_fingerprint.hpp"

#include <fstream>
#include <vector>

#include "app/crypto/password_hasher.hpp"
#include "app/crypto/sha256.hpp"

namespace app::services {

namespace fs = std::filesystem;

bool fingerprint_file(const fs::path& path, FileFingerprint& out, std::size_t block_bytes,
                      const std::function<void(std::uint64_t, std::uint64_t)>& progress) {
    std::error_code ec;
    if (!fs::is_regular_file(path, ec)) return false;
    const std::uint64_t total = fs::file_size(path, ec);
    if (ec) return false;
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;
    if (block_bytes == 0) block_bytes = kHashBlockBytes;

    std::vector<std::uint8_t> block(block_bytes);
    crypto::Sha256 hasher;
    std::uint64_t done = 0;
    while (in) {
        in.read(reinterpret_cast<char*>(block.data()), static_cast<std::streamsize>(block.size()));
        const std::streamsize got = in.gcount();
        if (got > 0) {
            hasher.update(block.data(), static_cast<std::size_t>(got));
            done += static_cast<std::uint64_t>(got);
            if (progress) progress(done, total);
        }
    }
    if (in.bad()) return false;
    out.size = static_cast<std::int64_t>(done);
    out.sha256 = crypto::PasswordHasher::to_hex(hasher.finalize());
    return true;
}

} // namespace app::services
