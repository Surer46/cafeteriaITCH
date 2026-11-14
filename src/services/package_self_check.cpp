#include "app/services/package_self_check.hpp"

#include <chrono>
#include <fstream>
#include <sstream>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "app/storage/migration_runner.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

namespace fs = std::filesystem;

namespace {

unsigned long current_pid() {
#ifdef _WIN32
    return static_cast<unsigned long>(GetCurrentProcessId());
#else
    return static_cast<unsigned long>(getpid());
#endif
}

// Misma regla que MigrationRunner: archivo .sql cuyos 3 primeros caracteres son un numero.
bool is_migration_file(const fs::path& p) {
    if (p.extension() != ".sql") return false;
    const std::string name = p.filename().string();
    if (name.size() < 3) return false;
    try {
        (void)std::stoi(name.substr(0, 3));
    } catch (...) {
        return false;
    }
    return true;
}

class Report {
public:
    void line(const std::string& text) { out_ << text << "\n"; }
    void problem(const std::string& text) {
        out_ << "FALLO: " << text << "\n";
        if (first_.empty()) first_ = text;
    }
    const std::string& first_problem() const { return first_; }
    std::string text() const { return out_.str(); }
private:
    std::ostringstream out_;
    std::string first_;
};

} // namespace

SelfCheckResult run_package_self_check(const SelfCheckInput& in) {
    SelfCheckResult result;
    Report rep;
    std::error_code ec;

    rep.line("Autocomprobación de Cafetería ITCH");
    rep.line("Versión: " + in.version);
    rep.line("Carpeta del paquete: " + in.package_dir.u8string());

    // 1. Manifiesto: lista de lo que debe venir en el paquete, generada al empaquetar.
    std::vector<std::string> manifest_files;
    std::string manifest_version;
    const fs::path manifest_path = in.package_dir / kPackageManifestName;
    {
        std::ifstream mf(manifest_path);
        if (!mf.is_open()) {
            rep.problem(std::string("Falta el archivo ") + kPackageManifestName +
                        " en la carpeta del paquete; no se puede saber qué fuentes e iconos deben estar.");
        } else {
            std::string line;
            while (std::getline(mf, line)) {
                while (!line.empty() && (line.back() == '\r' || line.back() == '\n')) line.pop_back();
                if (line.empty() || line[0] == '#') continue;
                if (line.rfind("version=", 0) == 0) {
                    manifest_version = line.substr(8);
                } else {
                    manifest_files.push_back(line);
                }
            }
        }
    }
    if (!manifest_version.empty()) {
        if (manifest_version == in.version) {
            rep.line("Versión del manifiesto: " + manifest_version + " (coincide)");
        } else {
            rep.problem("La versión del manifiesto (" + manifest_version + ") no coincide con la del ejecutable (" +
                        in.version + "): el paquete mezcla archivos de versiones distintas.");
        }
    }

    // 2. Cada archivo esperado existe y no está vacío (fuentes, iconos, avisos y migraciones).
    std::size_t expected_ok = 0;
    for (const auto& rel : manifest_files) {
        const fs::path p = in.package_dir / fs::u8path(rel);
        std::string state;
        if (!fs::is_regular_file(p, ec)) {
            state = "FALTA";
        } else {
            auto size = fs::file_size(p, ec);
            state = (ec || size == 0) ? "VACIO" : "OK";
        }
        rep.line("  [" + state + "] " + rel);
        if (state == "OK") {
            ++expected_ok;
        } else {
            rep.problem("Archivo esperado " + state + ": " + rel);
        }
    }
    rep.line("Archivos esperados correctos: " + std::to_string(expected_ok) + " de " +
             std::to_string(manifest_files.size()));

    // 3. Migraciones: archivos encontrados frente a aplicadas, en una base temporal propia.
    const fs::path mig_dir = in.package_dir / "migrations";
    const bool mig_dir_exists = fs::is_directory(mig_dir, ec);
    int migration_files = 0;
    if (!mig_dir_exists) {
        rep.problem("No se encontró la carpeta de migraciones del paquete: " + mig_dir.u8string());
    } else {
        for (const auto& entry : fs::directory_iterator(mig_dir, ec)) {
            if (entry.is_regular_file() && is_migration_file(entry.path())) ++migration_files;
        }
    }

    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    const fs::path temp_dir = in.temp_parent / ("CafeteriaITCH-autocomprobacion-" + std::to_string(current_pid()) +
                                                "-" + std::to_string(static_cast<long long>(stamp)));
    bool temp_created = false;
    int applied = -1;
    if (fs::create_directories(temp_dir, ec) && !ec) {
        temp_created = true;
        storage::SqliteDatabase db;
        auto open_res = db.open(temp_dir / "autocomprobacion.db");
        if (!open_res.is_success()) {
            rep.problem("No se pudo abrir SQLite en la carpeta temporal: " + open_res.error_message());
        } else if (mig_dir_exists) {
            storage::MigrationRunner runner(db);
            auto mig_res = runner.run_pending_migrations_from_dir(mig_dir);
            if (!mig_res.is_success()) {
                rep.problem("Falló una migración: " + mig_res.error_message());
            }
            auto versions = runner.get_applied_versions();
            if (versions.is_success()) applied = static_cast<int>(versions.value().size());
        }
        db.close();
    } else {
        rep.problem("No se pudo crear la carpeta temporal de la autocomprobación: " + temp_dir.u8string());
    }

    rep.line("Migraciones encontradas: " + std::to_string(migration_files));
    rep.line("Migraciones aplicadas: " + (applied >= 0 ? std::to_string(applied) : std::string("no disponible")));
    if (mig_dir_exists && migration_files == 0) {
        rep.problem("La carpeta de migraciones no contiene ninguna migración.");
    } else if (applied >= 0 && applied != migration_files) {
        rep.problem("Se aplicaron " + std::to_string(applied) + " migraciones pero hay " +
                    std::to_string(migration_files) + " archivos.");
    }

    // 4. Limpieza: la carpeta temporal propia siempre se borra.
    if (temp_created) {
        fs::remove_all(temp_dir, ec);
        result.temp_dir_removed = !fs::exists(temp_dir, ec);
        if (!result.temp_dir_removed) rep.problem("No se pudo borrar la carpeta temporal: " + temp_dir.u8string());
    } else {
        result.temp_dir_removed = true;
    }
    rep.line(std::string("Carpeta temporal borrada: ") + (result.temp_dir_removed ? "sí" : "no"));

    result.failure_reason = rep.first_problem();
    result.ok = result.failure_reason.empty();
    rep.line(result.ok ? std::string("RESULTADO: CORRECTO") : "RESULTADO: FALLÓ - " + result.failure_reason);
    result.report_utf8 = rep.text();
    return result;
}

} // namespace app::services
