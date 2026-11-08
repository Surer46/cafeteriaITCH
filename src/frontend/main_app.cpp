#include <wx/wx.h>
#include <filesystem>
#include <iostream>
#include "src/frontend/main_window.hpp"
#include "src/frontend/initial_owner_dialog.hpp"
#include "src/frontend/login_dialog.hpp"
#include "src/frontend/theme.hpp"
#include "app/storage/sqlite_database.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/services/mock_services.hpp"

#include <wx/stdpaths.h>
#include <wx/cmdline.h>

namespace app::frontend {

namespace {
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
    }

    bool OnCmdLineParsed(wxCmdLineParser& parser) override {
        if (!wxApp::OnCmdLineParsed(parser)) return false;
        if (parser.Found("data-dir", &command_data_dir_) && command_data_dir_.empty()) {
            wxLogError("El directorio de datos no puede estar vacío.");
            return false;
        }
        return true;
    }

    bool OnInit() override {
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

        // Permitir sobreescribir el directorio de datos para pruebas automatizadas o ejecución portable
        fs::path data_dir;
        bool explicit_data_dir = false;

        if (!command_data_dir_.empty()) {
            data_dir = native_path(command_data_dir_);
            explicit_data_dir = true;
        }

        if (!explicit_data_dir) {
            wxString env_data;
            if (wxGetEnv("CAFETERIA_DATA_DIR", &env_data) && !env_data.empty()) {
                data_dir = native_path(env_data);
                explicit_data_dir = true;
            }
        }

        if (!explicit_data_dir) {
            data_dir = exe_dir / "data";
            // Si se ejecuta desde build_release o build_debug en desarrollo, resolver respecto a raíz
            if (exe_dir.filename() == "build_release" || exe_dir.filename() == "build_debug") {
                data_dir = exe_dir.parent_path() / "data";
            }
        }

        std::error_code ec;
        if (!fs::exists(data_dir, ec)) {
            fs::create_directories(data_dir, ec);
        }
        // Si no se especificó ruta manual y falla escritura en directorio del binario, usar GetUserDataDir
        if (ec && !explicit_data_dir) {
            ec.clear();
            wxString user_data_wx = wxStandardPaths::Get().GetUserDataDir();
            data_dir = native_path(user_data_wx);
            fs::create_directories(data_dir, ec);
        }
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
        auto registry = services::ServiceRegistry::create_sqlite_registry(db_, data_dir.u8string());
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
