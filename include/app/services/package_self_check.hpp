#ifndef APP_SERVICES_PACKAGE_SELF_CHECK_HPP
#define APP_SERVICES_PACKAGE_SELF_CHECK_HPP

#include <filesystem>
#include <string>

// Autocomprobacion de un paquete instalado o copiado. Sin interfaz, sin red, sin cuentas y sin
// tocar la carpeta de datos real: trabaja en una carpeta temporal propia que siempre borra.
namespace app::services {

constexpr const char* kPackageManifestName = "paquete.manifest";

struct SelfCheckInput {
    std::filesystem::path package_dir;   // carpeta con el ejecutable, migrations/ y assets/
    std::filesystem::path temp_parent;   // donde crear la carpeta temporal propia
    std::string version;                 // version del ejecutable (fuente unica: project() de CMake)
};

struct SelfCheckResult {
    bool ok{false};
    std::string failure_reason;   // vacio si ok; si no, el primer motivo en lenguaje llano
    std::string report_utf8;      // informe completo
    bool temp_dir_removed{false};
};

SelfCheckResult run_package_self_check(const SelfCheckInput& input);

} // namespace app::services

#endif
