#ifndef APP_CONTRACTS_PRINT_CONFIG_SERVICE_HPP
#define APP_CONTRACTS_PRINT_CONFIG_SERVICE_HPP

#include <string>
#include "app/contracts/result.hpp"

namespace app::contracts {

enum class ThermalPaperProfile {
    Profile80mm, // 80 mm nominal (~72 mm útil en DC, 40-48 columnas monoespaciadas)
    Profile55mm  // 55 mm nominal (~48 mm útil en DC, 24-30 columnas monoespaciadas)
};

inline const char* to_string(ThermalPaperProfile profile) {
    switch (profile) {
        case ThermalPaperProfile::Profile80mm: return "80mm";
        case ThermalPaperProfile::Profile55mm: return "55mm";
        default: return "80mm";
    }
}

inline ThermalPaperProfile profile_from_string(const std::string& str) {
    if (str == "55mm" || str == "55") {
        return ThermalPaperProfile::Profile55mm;
    }
    return ThermalPaperProfile::Profile80mm;
}

struct PrinterStationConfig {
    ThermalPaperProfile profile{ThermalPaperProfile::Profile80mm};
    std::string printer_name{""}; // Cadena vacía = impresora predeterminada del sistema
    int nominal_width_mm{80};    // 80 o 55 mm
    int characters_per_line{42}; // Ancho de columna en texto monoespaciado
};

class IPrintConfigService {
public:
    virtual ~IPrintConfigService() = default;

    virtual Result<PrinterStationConfig> get_station_config() = 0;
    virtual OperationResult set_station_config(const PrinterStationConfig& config) = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_PRINT_CONFIG_SERVICE_HPP
