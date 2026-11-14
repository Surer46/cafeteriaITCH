#ifndef APP_SERVICES_DATA_DIR_POLICY_HPP
#define APP_SERVICES_DATA_DIR_POLICY_HPP

#include <filesystem>
#include <functional>
#include <optional>
#include <string>

// Decide en que carpeta viven los datos (cafeteria.db). Funcion pura: no depende de wxWidgets y
// solo toca el disco a traves de los callbacks recibidos, de modo que cada rama se puede probar.
//
// Orden: --data-dir, CAFETERIA_DATA_DIR, desarrollo (exe en build_release o build_debug),
// portable (portable.flag junto al exe), instalada (carpeta por equipo).
// Nunca se cambia de carpeta en silencio: si la elegida no sirve, devuelve un error.
namespace app::services {

enum class DataDirMode { Explicit, Development, Portable, Installed };

enum class DataDirError {
    None,
    EmptyArgument,         // --data-dir presente pero vacio
    NotWritable,           // la carpeta elegida no se puede crear o escribir
    DataInOtherLocation,   // la elegida no tiene base y otra ubicacion si
    InstalledDirUnknown    // copia instalada y no se conoce la carpeta por equipo
};

struct DataDirCallbacks {
    // Existe el archivo o carpeta.
    std::function<bool(const std::filesystem::path&)> path_exists;
    // Existe el archivo y no esta vacio.
    std::function<bool(const std::filesystem::path&)> file_nonempty;
    // Crea la carpeta si falta y comprueba que se puede escribir en ella. Solo se llama sobre la
    // carpeta ya elegida.
    std::function<bool(const std::filesystem::path&)> prepare_writable;
};

struct DataDirInputs {
    std::optional<std::filesystem::path> command_line_dir;  // valor de --data-dir
    std::optional<std::filesystem::path> env_dir;           // CAFETERIA_DATA_DIR (vacio = ausente)
    std::filesystem::path exe_dir;
    std::filesystem::path installed_dir;  // p. ej. %ProgramData%\CafeteriaITCH\data; vacio si se desconoce
    DataDirCallbacks fs;
};

struct DataDirDecision {
    bool ok{false};
    DataDirMode mode{DataDirMode::Explicit};
    DataDirError error{DataDirError::None};
    std::filesystem::path data_dir;     // carpeta elegida (tambien en error, si ya se habia elegido)
    std::filesystem::path other_dir;    // con DataInOtherLocation: carpeta que si tiene datos
    std::string message_utf8;           // texto en lenguaje llano para el usuario cuando !ok
};

constexpr const char* kDatabaseFileName = "cafeteria.db";
constexpr const char* kPortableFlagName = "portable.flag";

DataDirDecision resolve_data_dir(const DataDirInputs& inputs);

// Callbacks reales sobre std::filesystem.
DataDirCallbacks make_filesystem_callbacks();

} // namespace app::services

#endif
