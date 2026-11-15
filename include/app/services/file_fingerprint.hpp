#ifndef APP_SERVICES_FILE_FINGERPRINT_HPP
#define APP_SERVICES_FILE_FINGERPRINT_HPP

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>

namespace app::services {

// Bloque de lectura por omisión: 1 MiB. La memoria usada no depende del tamaño del archivo.
constexpr std::size_t kHashBlockBytes = 1024 * 1024;

struct FileFingerprint {
    std::int64_t size{0};
    std::string sha256;  // hexadecimal en minúsculas
};

// Tamaño y SHA-256 de un archivo regular, leído por bloques (sin cargarlo entero en memoria).
// `progress(leidos, total)` se llama tras cada bloque. false si no existe, no es regular o falla la lectura.
bool fingerprint_file(
    const std::filesystem::path& path,
    FileFingerprint& out,
    std::size_t block_bytes = kHashBlockBytes,
    const std::function<void(std::uint64_t, std::uint64_t)>& progress = {});

} // namespace app::services

#endif // APP_SERVICES_FILE_FINGERPRINT_HPP
