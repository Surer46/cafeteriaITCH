#ifndef APP_SERVICES_UPDATE_MANIFEST_HPP
#define APP_SERVICES_UPDATE_MANIFEST_HPP

#include <cstdint>
#include <string>
#include <vector>

#include "app/services/update_signature.hpp"

// Manifiesto firmado de una versión publicada (manifiesto.json + manifiesto.json.sig).
// El programa solo confía en un manifiesto cuya FIRMA verifica con una de sus claves públicas incrustadas.
namespace app::services {

struct UpdateVersion {
    int major{0};
    int minor{0};
    int patch{0};
};

// "a.b.c" con números de 1 a 4 dígitos, sin ceros de relleno raros ni espacios.
bool parse_update_version(const std::string& text, UpdateVersion& out);
// <0, 0, >0 según a < b, a == b, a > b.
int compare_update_versions(const UpdateVersion& a, const UpdateVersion& b);

struct UpdateManifest {
    std::string version;
    std::string published;                 // fecha AAAA-MM-DD
    std::vector<std::string> notes;        // lo que cambia, en lenguaje llano
    std::string installer_name;            // siempre CafeteriaITCH-<version>-instalador-win64.exe
    std::uint64_t installer_size{0};
    std::string installer_sha256;          // 64 hexadecimales en minúsculas
};

enum class ManifestError {
    None,
    NoKeys,         // el programa no trae ninguna clave pública: se rechaza todo
    TooLarge,
    BadSignature,   // no verifica con ninguna clave
    BadJson,
    BadFields,      // campos de más, de menos o con valores inválidos
    WrongProduct,
    WrongArch,
    NotNewer        // la versión es menor o igual a la instalada (válida pero se ignora)
};

struct ManifestResult {
    ManifestError error{ManifestError::None};
    std::string detail;        // técnico, solo para registros de depuración
    UpdateManifest manifest;   // válido solo si error == None
};

constexpr std::size_t kMaxManifestBytes = 64 * 1024;
constexpr std::size_t kMaxSignatureFileBytes = 1024;
constexpr std::uint64_t kMaxInstallerBytes = 500ull * 1024 * 1024;

// Decodifica el archivo de firma: 64 bytes crudos o 128 caracteres hexadecimales (con espacios o salto final).
bool decode_signature_file(const std::vector<std::uint8_t>& file_bytes, std::vector<std::uint8_t>& signature);

// Orden OBLIGATORIO: tamaño, firma (sobre los bytes exactos), JSON estricto, campos, producto, arquitectura, versión
// mayor que la instalada. Nada se interpreta antes de verificar la firma.
ManifestResult verify_and_parse_manifest(
    const std::vector<std::uint8_t>& manifest_bytes,
    const std::vector<std::uint8_t>& signature_file_bytes,
    const std::vector<PublicKey64>& keys,
    const std::string& installed_version,
    // Extensión del archivo del instalador que el manifiesto debe nombrar. El programa real usa siempre ".exe"; las
    // pruebas usan otra para no escribir ejecutables en %TEMP%. Una extensión inválida se trata como ".exe".
    const std::string& installer_extension = ".exe");

// ".exe" o un punto seguido de 1 a 8 letras o dígitos.
bool is_valid_installer_extension(const std::string& extension);

} // namespace app::services

#endif // APP_SERVICES_UPDATE_MANIFEST_HPP
