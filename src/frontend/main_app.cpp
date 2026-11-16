#include <wx/wx.h>
#include <wx/evtloop.h>
#include <wx/gauge.h>
#include <memory>
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
#include "app/services/database_startup.hpp"
#include "app/services/sqlite_update_service.hpp"

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

// Ventana de progreso de una actualización larga de la base (B9b). Sin padre (aún no hay ventana
// principal), sin botón de cerrar en la barra de título, sin ningún botón y sin poder cerrarse mientras dura
// la actualización. Todo ocurre en el mismo hilo: tras cada cambio se repinta de inmediato y se procesan
// SOLO eventos de interfaz (pintado), nunca entrada del usuario, temporizadores ni otras categorías.
class UpdateProgressWindow : public wxDialog {
public:
    UpdateProgressWindow()
        : wxDialog(nullptr, wxID_ANY, wxT("Cafetería ITCH"), wxDefaultPosition, wxDefaultSize,
                   wxCAPTION | wxSTAY_ON_TOP) {
        // Aún no corre el bucle principal de la aplicación: se activa uno propio, sin ejecutarlo, solo
        // para poder procesar eventos de pintado.
        if (wxEventLoopBase::GetActive() == nullptr) {
            temp_loop_.reset(new wxEventLoop());
            activator_.reset(new wxEventLoopActivator(temp_loop_.get()));
        }
        SetEscapeId(wxID_NONE);  // Esc no hace nada

        // Medidas en unidades lógicas, convertidas una sola vez.
        const int width = FromDIP(480);
        const int margin = FromDIP(24);
        const int gap = FromDIP(14);
        const int gauge_height = FromDIP(22);
        text_width_ = width - 2 * margin;

        SetBackgroundColour(Theme::ColorSurface);
        auto* sizer = new wxBoxSizer(wxVERTICAL);

        auto* warning = new wxStaticText(this, wxID_ANY, wxT("No apague el equipo."));
        warning->SetFont(Theme::get_font_title(wxFONTWEIGHT_BOLD));
        warning->SetForegroundColour(Theme::ColorText);
        warning->Wrap(text_width_);

        auto* info = new wxStaticText(this, wxID_ANY,
            wxT("Estamos actualizando los datos de la cafetería. Puede tardar varios minutos."));
        info->SetFont(Theme::get_font_body());
        info->SetForegroundColour(Theme::ColorText);
        info->Wrap(text_width_);

        phase_ = new wxStaticText(this, wxID_ANY, wxEmptyString, wxDefaultPosition, wxDefaultSize, wxST_NO_AUTORESIZE);
        phase_->SetFont(Theme::get_font_body());
        phase_->SetForegroundColour(Theme::ColorTextSecondary);
        // Altura fija de dos líneas: la ventana no cambia de tamaño al cambiar de fase.
        phase_->SetMinSize(wxSize(text_width_, 2 * phase_->GetCharHeight()));

        gauge_ = new wxGauge(this, wxID_ANY, 100, wxDefaultPosition, wxSize(text_width_, gauge_height),
                             wxGA_HORIZONTAL | wxGA_SMOOTH);

        sizer->Add(warning, 0, wxLEFT | wxRIGHT | wxTOP, margin);
        sizer->Add(info, 0, wxLEFT | wxRIGHT | wxTOP, gap);
        sizer->Add(phase_, 0, wxLEFT | wxRIGHT | wxTOP, gap);
        sizer->Add(gauge_, 0, wxLEFT | wxRIGHT | wxTOP | wxBOTTOM, margin);
        SetSizerAndFit(sizer);
        SetMinClientSize(wxSize(width, -1));

        // Mientras dure la actualización no se puede cerrar (Alt+F4, menú del sistema...).
        Bind(wxEVT_CLOSE_WINDOW, [this](wxCloseEvent& event) {
            if (!allow_close_ && event.CanVeto()) {
                event.Veto();
            } else {
                event.Skip();
            }
        });
    }

    void show_centered() {
        CenterOnScreen();
        Show(true);
        Raise();
        repaint();
    }

    // percent < 0: fase sin porcentaje real (barra en movimiento); si no, barra al porcentaje indicado.
    void set_phase(const wxString& text, int percent) {
        phase_->SetLabel(text);
        phase_->Wrap(text_width_);
        if (percent >= 0) {
            gauge_->SetValue(percent < 99 ? percent : 99);
        } else {
            gauge_->Pulse();
        }
        repaint();
    }

    void allow_close_and_finish() { allow_close_ = true; }

private:
    // Repinta ya (Update fuerza el pintado síncrono de esta ventana) y deja procesar SOLO eventos de
    // interfaz: YieldFor(wxEVT_CATEGORY_UI) excluye la entrada del usuario, los temporizadores, los hilos y
    // demás categorías. En este punto de OnInit no existe ninguna otra ventana ni servicio con acceso a la base.
    void repaint() {
        Layout();
        Refresh();
        Update();
        if (wxEventLoopBase* loop = wxEventLoopBase::GetActive()) {
            loop->YieldFor(wxEVT_CATEGORY_UI);
        }
    }

    std::unique_ptr<wxEventLoop> temp_loop_;
    std::unique_ptr<wxEventLoopActivator> activator_;  // se destruye antes que temp_loop_
    wxStaticText* phase_{nullptr};
    wxGauge* gauge_{nullptr};
    int text_width_{0};
    bool allow_close_{false};
};
} // namespace

class CafeteriaApp : public wxApp {
public:
    CafeteriaApp() {
        Bind(wxEVT_QUERY_END_SESSION, &CafeteriaApp::OnQueryEndSession, this);
        Bind(wxEVT_END_SESSION, &CafeteriaApp::OnEndSession, this);
    }

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

        // 2-3. Abrir la base y migrarla de forma segura (integridad, respaldo previo y migraciones):
        // la lógica y los mensajes viven en services::open_and_migrate_database.
        services::StartupOptions startup;
        startup.db_path = db_path;
        startup.migrations_dir = mig_dir;
        startup.backups_dir = data_dir / "backups";

        // Ventana de progreso: solo cuando hay una actualización larga (base con datos y migraciones
        // pendientes). Ver UpdateProgressWindow. Los avisos llegan en este mismo hilo y el servicio ya los
        // limita; en este punto ningún otro objeto toca la base (los servicios aún no existen).
        std::unique_ptr<UpdateProgressWindow> progress_window;
        startup.on_progress = [&progress_window, this](const services::StartupProgress& p) {
            if (p.phase == services::StartupPhase::UpdateStarting && !progress_window) {
                SetExitOnFrameDelete(false);  // cerrar esta ventana nunca debe terminar la aplicación
                progress_window.reset(new UpdateProgressWindow());
                progress_window->show_centered();
            }
            if (!progress_window || p.phase == services::StartupPhase::Finished) return;
            wxString text = wxString::FromUTF8(p.message_utf8.c_str());
            if (p.percent >= 0) text += wxString::Format(wxT(" %d %%"), p.percent);
            progress_window->set_phase(text, p.percent);
        };
        in_startup_ = true;
        const auto startup_res = services::open_and_migrate_database(db_, startup);
        in_startup_ = false;
        // La ventana se destruye antes de cualquier mensaje de error y antes del inicio de sesión.
        if (progress_window) {
            progress_window->allow_close_and_finish();
            progress_window.reset();
            SetExitOnFrameDelete(true);
        }
        if (!startup_res.ok) {
            wxMessageBox(
                wxString::FromUTF8(startup_res.message_utf8.c_str()),
                wxT("Base de datos de la cafetería"),
                wxOK | wxICON_ERROR
            );
            return false;
        }

        // 4. Crear servicios conectados a SQLite, inyectando el directorio efectivo del puesto
        // para que SqlitePrintConfigService use la ruta ya resuelta (respeta --data-dir y
        // CAFETERIA_DATA_DIR) en lugar de releer getenv o usar cwd.
        auto registry = services::ServiceRegistry::create_sqlite_registry(
            db_, data_dir.u8string(), mig_dir.u8string(), CAFETERIA_VERSION,
            dir_decision.mode == services::DataDirMode::Portable);
        auto account_service = registry.account_service;
        db_file_ = db_path;

        // Actualizaciones (B11): búsqueda automática en segundo plano (a lo sumo cada 24 h, sin red si no hay clave
        // pública configurada o CAFETERIA_UPDATE_CHECK=0). Nunca bloquea el inicio de sesión ni una venta.
        update_service_ = std::dynamic_pointer_cast<services::SqliteUpdateService>(registry.update_service);
        if (update_service_) update_service_->start_automatic_check();

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

    // Apagado o cierre forzado por Windows (apagar/reiniciar, Restart Manager del instalador).
    // wxWidgets, por omisión, cierra la ventana principal (y su veto podría BLOQUEAR el apagado) y en
    // WM_ENDSESSION borra todas las ventanas de nivel superior con `delete`: los diálogos de OnInit viven
    // en la pila, y ese `delete` corrompía el heap (0xC0000374). Aquí no se veta nunca y no se borra nada.
    // (En wxWidgets estos dos manejadores no son virtuales: se enlazan con Bind, que se procesa antes que la
    // tabla de eventos de wxApp y, al no llamar a Skip, impide que corra el manejador por omisión.)
    void OnQueryEndSession(wxCloseEvent& event) {
        event.Veto(false);
    }

    void OnEndSession(wxCloseEvent&) {
        // Sin mensajes ni diálogos. La base se cierra de forma ordenada (checkpoint del WAL); lo confirmado
        // ya está en el WAL. Durante la actualización de la base no se cierra: este aviso llegaría dentro
        // del callback de progreso de la propia conexión, y la migración se revierte sola al reabrir.
        if (!in_startup_ && db_.is_open()) db_.close();
        ::ExitProcess(0);
    }

    int OnExit() override {
        Theme::cleanup();

        // Instalación de una actualización aceptada, SOLO en un cierre normal (el apagado de Windows termina el
        // proceso en OnEndSession y nunca llega aquí). Orden: 1) se vuelve a verificar todo y se anota el intento;
        // 2) se cierra la base; 3) solo si cerró limpia, se lanza el instalador con elevación; 4) el proceso termina.
        // Sin sesión (p. ej. cierre tras restaurar un respaldo) o con la actualización pospuesta no se lanza nada.
        services::SqliteUpdateService::InstallLaunchPlan plan;
        if (update_service_) {
            plan = update_service_->prepare_install_on_close();
            if (plan.ok) update_service_->wait_idle(5000);  // que una búsqueda en curso suelte su conexión
        }
        db_.close();
        if (plan.ok && services::SqliteUpdateService::database_closed_cleanly(db_file_)) {
            update_service_->launch_install(plan);
        }
        plan = services::SqliteUpdateService::InstallLaunchPlan();  // suelta el archivo del instalador
        return wxApp::OnExit();
    }

private:
    wxString command_data_dir_;
    storage::SqliteDatabase db_;
    std::filesystem::path db_file_;
    std::shared_ptr<services::SqliteUpdateService> update_service_;
    bool in_startup_{false};  // true mientras open_and_migrate_database usa la conexión
};

} // namespace app::frontend

wxIMPLEMENT_APP(app::frontend::CafeteriaApp);
