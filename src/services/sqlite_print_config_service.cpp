#include "app/services/sqlite_print_config_service.hpp"
#include <fstream>
#include <sstream>
#include <cstdlib>

namespace app::services {

SqlitePrintConfigService::SqlitePrintConfigService(
    storage::SqliteDatabase& db,
    std::shared_ptr<contracts::IAccountService> account_service,
    std::string config_dir
) : db_(db), account_service_(std::move(account_service)), config_dir_(std::move(config_dir)) {
    // Valores predeterminados seguros para 80 mm
    config_.profile = contracts::ThermalPaperProfile::Profile80mm;
    config_.printer_name = "";
    config_.nominal_width_mm = 80;
    config_.characters_per_line = 42;
    load_from_disk();
}

std::filesystem::path SqlitePrintConfigService::get_config_file_path() const {
    std::filesystem::path base;
    if (!config_dir_.empty()) {
        base = std::filesystem::u8path(config_dir_);
    } else {
        const char* env_dir = std::getenv("CAFETERIA_DATA_DIR");
        if (env_dir && *env_dir) {
            base = std::filesystem::u8path(env_dir);
        } else {
            base = std::filesystem::current_path() / "data";
        }
    }
    return base / "station_printer.cfg";
}

void SqlitePrintConfigService::load_from_disk() {
    loaded_ = true;
    auto cfg_path = get_config_file_path();
    if (!std::filesystem::exists(cfg_path)) {
        return;
    }

    std::ifstream f(cfg_path);
    if (!f.is_open()) return;

    std::string line;
    while (std::getline(f, line)) {
        size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = line.substr(0, eq);
        std::string val = line.substr(eq + 1);

        // Trim
        while (!key.empty() && (key.back() == ' ' || key.back() == '\r')) key.pop_back();
        while (!val.empty() && (val.back() == ' ' || val.back() == '\r')) val.pop_back();
        while (!key.empty() && key.front() == ' ') key.erase(key.begin());
        while (!val.empty() && val.front() == ' ') val.erase(val.begin());

        if (key == "profile") {
            if (val == "55mm" || val == "55") {
                config_.profile = contracts::ThermalPaperProfile::Profile55mm;
                config_.nominal_width_mm = 55;
            } else {
                config_.profile = contracts::ThermalPaperProfile::Profile80mm;
                config_.nominal_width_mm = 80;
            }
        } else if (key == "printer_name") {
            if (val == "Impresora Térmica Predeterminada") {
                config_.printer_name = "";
            } else {
                config_.printer_name = val;
            }
        } else if (key == "characters_per_line") {
            try {
                int c = std::stoi(val);
                if (c >= 20 && c <= 80) {
                    config_.characters_per_line = c;
                }
            } catch (...) {}
        } else if (key == "nominal_width_mm") {
            try {
                int w = std::stoi(val);
                if (w == 55 || w == 80) {
                    config_.nominal_width_mm = w;
                }
            } catch (...) {}
        }
    }

    // Asegurar consistencia entre perfil y columnas por defecto si no están especificadas
    if (config_.profile == contracts::ThermalPaperProfile::Profile55mm) {
        config_.nominal_width_mm = 55;
        if (config_.characters_per_line > 34) {
            config_.characters_per_line = 28;
        }
    } else {
        config_.nominal_width_mm = 80;
        if (config_.characters_per_line < 36) {
            config_.characters_per_line = 42;
        }
    }
}

bool SqlitePrintConfigService::save_to_disk() {
    auto cfg_path = get_config_file_path();
    std::error_code ec;
    std::filesystem::create_directories(cfg_path.parent_path(), ec);
    if (ec) return false;

    // Escribir en archivo temporal y luego sustituir atómicamente para proteger
    // la configuración anterior ante fallos de escritura, flush o cierre.
    std::filesystem::path tmp_path = cfg_path;
    tmp_path += ".tmp";

    {
        std::ofstream f(tmp_path, std::ios::out | std::ios::trunc);
        if (!f.is_open()) return false;

        f << "profile=" << contracts::to_string(config_.profile) << "\n";
        f << "nominal_width_mm=" << config_.nominal_width_mm << "\n";
        f << "characters_per_line=" << config_.characters_per_line << "\n";
        f << "printer_name=" << config_.printer_name << "\n";

        f.flush();
        if (f.fail()) {
            f.close();
            std::filesystem::remove(tmp_path, ec);
            return false;
        }
        f.close();
        if (f.fail()) {
            std::filesystem::remove(tmp_path, ec);
            return false;
        }
    }

    // Sustituir el archivo anterior por el temporal: en caso de fallo se
    // conserva el archivo anterior y la configuración en memoria no se toca.
    std::filesystem::rename(tmp_path, cfg_path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return false;
    }
    return true;
}


contracts::Result<contracts::PrinterStationConfig> SqlitePrintConfigService::get_station_config() {
    if (!loaded_) {
        load_from_disk();
    }
    return contracts::Result<contracts::PrinterStationConfig>::ok(config_);
}

contracts::OperationResult SqlitePrintConfigService::set_station_config(const contracts::PrinterStationConfig& config) {
    // Comprobación de seguridad en capa de servicios (AGENTS.md):
    // "Ocultar un botón no sustituye una autorización."
    if (!account_service_ || !account_service_->has_permission("config.manage")) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::Forbidden,
            "Permiso 'config.manage' requerido para modificar la configuración de impresión de la estación."
        );
    }

    // Validar parámetros
    if (config.nominal_width_mm != 55 && config.nominal_width_mm != 80) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "Ancho nominal no soportado: solo se admiten perfiles térmicos de 55 mm y 80 mm."
        );
    }

    if (config.characters_per_line < 20 || config.characters_per_line > 80) {
        return contracts::OperationResult::fail(
            contracts::ErrorCode::InvalidInput,
            "Número de caracteres por línea inválido (debe estar entre 20 y 80)."
        );
    }

    // Preparar la nueva configuración sin modificar aún la activa en memoria
    contracts::PrinterStationConfig new_config = config;
    if (new_config.profile == contracts::ThermalPaperProfile::Profile55mm) {
        new_config.nominal_width_mm = 55;
    } else {
        new_config.nominal_width_mm = 80;
    }

    // Intercambiar temporalmente config_ para que save_to_disk serialice new_config.
    // Si falla, restaurar el valor anterior para no dejar un estado parcial en memoria.
    contracts::PrinterStationConfig previous_config = config_;
    config_ = new_config;

    if (!save_to_disk()) {
        // Restaurar configuración anterior en memoria: el archivo previo ya fue preservado
        // por la escritura atómica en save_to_disk.
        config_ = previous_config;
        return contracts::OperationResult::fail(
            contracts::ErrorCode::StorageFailure,
            "No se pudo guardar la configuración de impresión en disco. "
            "La configuración activa no fue modificada."
        );
    }

    return contracts::OperationResult::ok();
}

} // namespace app::services
