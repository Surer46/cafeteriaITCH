#include <wx/wx.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include "src/frontend/main_window.hpp"
#include "src/frontend/initial_owner_dialog.hpp"
#include "src/frontend/login_dialog.hpp"
#include "src/frontend/theme.hpp"
#include "app/storage/sqlite_database.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/services/mock_services.hpp"
#include "app/services/data_dir_policy.hpp"
#include "app/services/package_self_check.hpp"

#include <wx/stdpaths.h>
#include <wx/cmdline.h>

namespace app::frontend {

namespace {
const char* const kNewline = "\n";
std::filesystem::path native_path(const wxString& value) {
#ifdef _WIN32
    return std::filesystem::path(value.ToStdWstring());
#else
    return std::filesystem::u8path(value.ToUTF8().data());
#endif
}
} // namespace

class CafeteriaApp : public wxApp {
public:
    void OnInitCmdLine(wxCmdLineParser& parser) override {
        wxApp::OnInitCmdLine(parser);
        parser.AddOption(wxEmptyString, "data-dir", "Directorio de datos locales",
                         wxCMD_LINE_VAL_STRING);
        // La autocomprobación se atiende antes del analizador (ver run_self_check_if_requested);
        // se declara aquí para que figure en la ayuda y no cuente como opción desconocida.
        parser.AddSwitch(wxEmptyString, "self-check", "Comprueba el paquete sin abrir ventanas y termina");
        parser.AddOption(wxEmptyString, "self-check-report", "Archivo donde escribir el informe de --self-check",
                         wxCMD_LINE_VAL_STRING);
    }

    bool OnCmdLineParsed(wxCmdLineParser& parser) override {
        if (!wxApp::OnCmdLineParsed(parser)) return false;
        if (parser.Found("data-dir", &command_data_dir_) && command_data_dir_.empty()) {
            wxLogError("El directorio de datos no puede estar vacío.");
            return false;
        }
        return true;
    }

    // --self-check: sin ventanas ni cuentas, con carpeta temporal propia; nunca vuelve.
    // Se atiende antes de wxApp::OnInit para que ni el analizador de línea de comandos ni
    // wxLog puedan mostrar un diálogo. Termina con _Exit porque el código de salida de wxWidgets
    // cuando OnInit devuelve false no está garantizado.
    void run_self_check_if_requested() {
        namespace fs = std::filesystem;
        bool requested = false;
        std::optional<fs::path> report_path;
        for (int i = 1; i < argc; ++i) {
            const wxString arg = argv[i];
            if (arg == "--self-check") {
                requested = true;
            } else if (arg == "--self-check-report") {
                if (i + 1 < argc) report_path = native_path(argv[++i]);
            } else if (arg.StartsWith("--self-check-report=")) {
                report_path = native_path(arg.Mid(20));
            }
        }
        if (!requested) return;

        const fs::path exe_dir = native_path(wxStandardPaths::Get().GetExecutablePath()).parent_path();
        std::error_code ec;
        const fs::path temp_parent = fs::temp_directory_path(ec);

        services::SelfCheckResult result;
        if (ec) {
            result.ok = false;
            result.failure_reason = "No se pudo determinar la carpeta temporal del sistema.";
            result.report_utf8 = std::string("Autocomprobación de Cafetería ITCH") + kNewline +
                                 "Versión: " + CAFETERIA_VERSION + kNewline +
                                 "FALLO: " + result.failure_reason + kNewline +
                                 "RESULTADO: FALLÓ" + kNewline;
        } else {
            services::SelfCheckInput input;
            input.package_dir = exe_dir;
            input.temp_parent = temp_parent;
            input.version = CAFETERIA_VERSION;
            result = services::run_package_self_check(input);
        }

        int code = result.ok ? 0 : 1;
        if (report_path.has_value()) {
            std::ofstream out(*report_path, std::ios::binary | std::ios::trunc);
            out << "\xEF\xBB\xBF" << result.report_utf8;  // BOM: el Bloc de notas antiguo reconoce UTF-8
            out.flush();
            if (!out) code = 2;  // no se pudo escribir el informe
        }
        std::_Exit(code);
    }

    bool OnInit() override {
        run_self_check_if_requested();

        if (!wxApp::OnInit()) {
            return false;
        }

        Theme::init();

        namespace fs = std::filesystem;

        // 1. Localizar ruta de migraciones y almacenamiento de forma confiable
        wxString exe_path_wx = wxStandardPaths::Get().GetExecutablePath();
        fs::path exe_dir = native_path(exe_path_wx).parent_path();

        fs::path mig_dir = exe_dir / "migrations";
        if (!fs::exists(mig_dir) && (exe_dir.filename() == "build_release" || exe_dir.filename() == "build_debug")) {
            mig_dir = exe_dir.parent_path() / "migrations";
        }
        wxString env_mig;
        if (wxGetEnv("CAFETERIA_MIGRATIONS_DIR", &env_mig) && !env_mig.empty()) {
            mig_dir = native_path(env_mig);
        }

        // Carpeta de datos: la decisión vive en services::resolve_data_dir (función pura con pruebas).
        // Nunca se cambia de carpeta en silencio: si la elegida no sirve, se avisa y no se arranca.
        services::DataDirInputs dir_in;
        if (!command_data_dir_.empty()) dir_in.command_line_dir = native_path(command_data_dir_);
        wxString env_data;
        if (wxGetEnv("CAFETERIA_DATA_DIR", &env_data) && !env_data.empty()) {
            dir_in.env_dir = native_path(env_data);
        }
        dir_in.exe_dir = exe_dir;
        wxString program_data;
        if (wxGetEnv("ProgramData", &program_data) && !program_data.empty()) {
            dir_in.installed_dir = native_path(program_data) / "CafeteriaITCH" / "data";
        }
        dir_in.fs = services::make_filesystem_callbacks();
        const auto dir_decision = services::resolve_data_dir(dir_in);
        if (!dir_decision.ok) {
            wxMessageBox(
                wxString::FromUTF8(dir_decision.message_utf8.c_str()),
                wxT("Carpeta de datos"),
                wxOK | wxICON_ERROR
            );
            return false;
        }
        std::error_code ec;
        const fs::path data_dir = dir_decision.data_dir;
        Theme::set_data_dir(data_dir.u8string());

        fs::path db_path = data_dir / "cafeteria.db";

        // 2. Abrir base de datos SQLite embebida
        auto open_res = db_.open(db_path);
        if (!open_res.is_success()) {
            wxMessageBox(
                wxString::FromUTF8(open_res.error_message().c_str()),
                wxT("Error de Base de Datos Local"),
                wxOK | wxICON_ERROR
            );
            return false;
        }

        // 3. Aplicar migraciones versionadas de forma obligatoria
        if (!fs::exists(mig_dir, ec)) {
            wxMessageBox(
                wxT("No se encontró el directorio de migraciones del sistema.\nLa aplicación requiere sus archivos de esquema para operar."),
                wxT("Error de Inicialización"),
                wxOK | wxICON_ERROR
            );
            return false;
        }

        storage::MigrationRunner runner(db_);
        auto mig_res = runner.run_pending_migrations_from_dir(mig_dir);
        if (!mig_res.is_success()) {
            wxMessageBox(
                wxString::FromUTF8(mig_res.error_message().c_str()),
                wxT("Error al ejecutar migraciones"),
                wxOK | wxICON_ERROR
            );
            return false;
        }

        // 4. Crear servicios conectados a SQLite, inyectando el directorio efectivo del puesto
        // para que SqlitePrintConfigService use la ruta ya resuelta (respeta --data-dir y
        // CAFETERIA_DATA_DIR) en lugar de releer getenv o usar cwd.
        auto registry = services::ServiceRegistry::create_sqlite_registry(db_, data_dir.u8string(), mig_dir.u8string());
        auto account_service = registry.account_service;

        // 5. Verificar si existe cuenta Dueño (primer inicio obligatorio)
        auto has_owner_res = account_service->has_owner();
        if (!has_owner_res.is_success()) {
            wxMessageBox(
                wxString::FromUTF8(has_owner_res.error_message().c_str()),
                wxT("Error de Verificación"),
                wxOK | wxICON_ERROR
            );
            return false;
        }

        if (!has_owner_res.value()) {
            // Primer inicio: Exigir alta de cuenta Dueño con contraseña elegida
            InitialOwnerDialog owner_dlg(nullptr, account_service);
            if (owner_dlg.ShowModal() != wxID_OK) {
                // Si el usuario cancela el alta obligatoria, la aplicación no inicia
                return false;
            }
        } else {
            // Inicios posteriores: Exigir inicio de sesión
            LoginDialog login_dlg(nullptr, account_service);
            if (login_dlg.ShowModal() != wxID_OK) {
                return false;
            }
        }

        // 6. Abrir ventana principal con la sesión autenticada
        auto* window = new MainWindow(registry);
        window->Show(true);
        SetTopWindow(window);

        return true;
    }

    int OnExit() override {
        Theme::cleanup();
        db_.close();
        return wxApp::OnExit();
    }

private:
    wxString command_data_dir_;
    storage::SqliteDatabase db_;
};

} // namespace app::frontend

wxIMPLEMENT_APP(app::frontend::CafeteriaApp);
