#include "app/services/data_dir_policy.hpp"

#include <fstream>
#include <system_error>

namespace app::services {

namespace fs = std::filesystem;

namespace {

std::string path_text(const fs::path& p) {
    return p.u8string();
}

DataDirDecision fail_not_writable(DataDirMode mode, const fs::path& dir) {
    DataDirDecision d;
    d.mode = mode;
    d.error = DataDirError::NotWritable;
    d.data_dir = dir;
    d.message_utf8 =
        "No se puede guardar información en la carpeta de datos de la cafetería:\n\n" +
        path_text(dir) +
        "\n\nLa aplicación no se abrirá para no guardar las ventas en otro lugar sin que usted lo sepa.\n"
        "Llame a quien instaló el sistema.";
    return d;
}

} // namespace

DataDirDecision resolve_data_dir(const DataDirInputs& in) {
    const auto& cb = in.fs;
    DataDirDecision d;

    if (in.command_line_dir.has_value() && in.command_line_dir->empty()) {
        d.error = DataDirError::EmptyArgument;
        d.message_utf8 = "La carpeta de datos indicada está vacía.";
        return d;
    }

    // 1-2. Carpeta indicada de forma explícita: nunca se cuestiona ni se sustituye.
    if (in.command_line_dir.has_value() || (in.env_dir.has_value() && !in.env_dir->empty())) {
        d.mode = DataDirMode::Explicit;
        d.data_dir = in.command_line_dir.has_value() ? *in.command_line_dir : *in.env_dir;
        if (!cb.prepare_writable(d.data_dir)) return fail_not_writable(d.mode, d.data_dir);
        d.ok = true;
        return d;
    }

    // 3. Desarrollo: solo build_release y build_debug. Sin comprobación de otras ubicaciones.
    const auto leaf = in.exe_dir.filename();
    if (leaf == "build_release" || leaf == "build_debug") {
        d.mode = DataDirMode::Development;
        d.data_dir = in.exe_dir.parent_path() / "data";
        if (!cb.prepare_writable(d.data_dir)) return fail_not_writable(d.mode, d.data_dir);
        d.ok = true;
        return d;
    }

    // 4-5. Portable o instalada.
    const fs::path portable_dir = in.exe_dir / "data";
    fs::path other_dir;
    if (cb.path_exists(in.exe_dir / kPortableFlagName)) {
        d.mode = DataDirMode::Portable;
        d.data_dir = portable_dir;
        other_dir = in.installed_dir;
    } else {
        d.mode = DataDirMode::Installed;
        if (in.installed_dir.empty()) {
            d.error = DataDirError::InstalledDirUnknown;
            d.message_utf8 =
                "No se pudo determinar la carpeta donde esta copia instalada guarda sus datos.\n"
                "La aplicación no se abrirá. Llame a quien instaló el sistema.";
            return d;
        }
        d.data_dir = in.installed_dir;
        other_dir = portable_dir;
    }

    // D3: la carpeta elegida no tiene base y la otra ubicación sí: no se crea una base vacía.
    if (!cb.file_nonempty(d.data_dir / kDatabaseFileName) && !other_dir.empty() &&
        cb.file_nonempty(other_dir / kDatabaseFileName)) {
        d.error = DataDirError::DataInOtherLocation;
        d.other_dir = other_dir;
        d.message_utf8 =
            "Se encontraron datos de la cafetería en una carpeta distinta de la que usa esta copia del programa.\n\n"
            "Carpeta que usaría esta copia (sin datos):\n" + path_text(d.data_dir) +
            "\n\nCarpeta donde sí hay datos:\n" + path_text(other_dir) +
            "\n\nLa aplicación no se abrirá para no empezar con una base vacía encima de sus ventas.\n"
            "Llame a quien instaló el sistema.";
        return d;
    }

    if (!cb.prepare_writable(d.data_dir)) return fail_not_writable(d.mode, d.data_dir);
    d.ok = true;
    return d;
}

DataDirCallbacks make_filesystem_callbacks() {
    DataDirCallbacks cb;
    cb.path_exists = [](const fs::path& p) {
        std::error_code ec;
        return fs::exists(p, ec);
    };
    cb.file_nonempty = [](const fs::path& p) {
        std::error_code ec;
        if (!fs::is_regular_file(p, ec)) return false;
        auto size = fs::file_size(p, ec);
        return !ec && size > 0;
    };
    cb.prepare_writable = [](const fs::path& dir) {
        std::error_code ec;
        fs::create_directories(dir, ec);
        if (ec || !fs::is_directory(dir, ec)) return false;
        const fs::path probe = dir / ".escritura-prueba.tmp";
        {
            std::ofstream out(probe, std::ios::binary | std::ios::trunc);
            if (!out.is_open()) return false;
            out << "x";
            out.flush();
            if (!out) return false;
        }
        return fs::remove(probe, ec) && !ec;
    };
    return cb;
}

} // namespace app::services
