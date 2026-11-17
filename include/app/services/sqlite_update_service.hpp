#ifndef APP_SERVICES_SQLITE_UPDATE_SERVICE_HPP
#define APP_SERVICES_SQLITE_UPDATE_SERVICE_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "app/contracts/account_service.hpp"
#include "app/contracts/update_service.hpp"
#include "app/services/update_http.hpp"
#include "app/services/update_signature.hpp"
#include "app/storage/sqlite_database.hpp"

namespace app::services {

struct UpdateServiceOptions {
    std::string data_dir;                       // carpeta de datos: las descargas van en <datos>\updates
    std::string installed_version{"0.0.0"};     // versión del programa en ejecución (project(VERSION))
    // Extensión del archivo del instalador (nombre en el manifiesto, archivo descargado, copia desde USB y archivo que
    // se lanza). El programa real usa ".exe" (main_app no la cambia); las pruebas usan otra para no escribir .exe en
    // %TEMP% (Windows Defender lo trata como comportamiento sospechoso). Inválida = ".exe".
    std::string installer_extension{".exe"};
    bool is_portable{false};                    // copia portable: avisa, pero no se actualiza sola
    std::vector<PublicKey64> keys;              // claves públicas de confianza; vacío = no configurado
    std::shared_ptr<IHttpClient> http;          // si es null: make_default_update_http_client()
    std::function<std::int64_t()> now_ms;       // reloj UTC en ms desde 1970; si es null, el del sistema
    std::string manifest_url{"https://github.com/Surer46/cafeteriaITCH/releases/latest/download/manifiesto.json"};
    std::string signature_url{"https://github.com/Surer46/cafeteriaITCH/releases/latest/download/manifiesto.json.sig"};
    // El instalador se pide SIEMPRE en <base>/v<versión>/<nombre del manifiesto>; la URL la arma el programa.
    std::string installer_base_url{"https://github.com/Surer46/cafeteriaITCH/releases/download"};
    int http_timeout_ms{15000};
    std::int64_t auto_check_interval_ms{24LL * 3600 * 1000};
    std::int64_t manual_check_min_interval_ms{10LL * 60 * 1000};
};

class SqliteUpdateService : public contracts::IUpdateService {
public:
    SqliteUpdateService(storage::SqliteDatabase& db,
                        std::shared_ptr<contracts::IAccountService> account_service,
                        UpdateServiceOptions options);
    ~SqliteUpdateService() override;

    // --- Contrato ---
    contracts::Result<contracts::UpdateStatusDto> get_update_status() override;
    contracts::OperationResult check_for_updates() override;
    contracts::OperationResult accept_update() override;
    contracts::OperationResult decline_update() override;
    contracts::OperationResult cancel_download() override;
    contracts::OperationResult use_update_from_files(const std::string& installer_path,
                                                     const std::string& manifest_path,
                                                     const std::string& signature_path) override;
    contracts::Result<contracts::UpdateFromFilesResultDto> apply_update_from_files(
        const std::string& installer_path, const std::string& data_path, const std::string& verification_path) override;
    contracts::OperationResult postpone_install_this_session() override;
    contracts::Result<contracts::UpdateInstallResultDto> get_last_install_result() override;
    contracts::OperationResult acknowledge_install_result() override;

    // --- Para main_app.cpp y las pruebas (no forman parte del contrato) ---

    // Búsqueda automática al abrir: acción del sistema (sin sesión ni permiso). A lo sumo cada 24 h. No hace nada
    // (ni una petición de red) si no hay claves públicas, si CAFETERIA_UPDATE_CHECK=0 o si hay una en curso.
    // Si había una actualización aceptada con la descarga a medias, la reanuda. Vuelve de inmediato.
    void start_automatic_check();

    // Instalación al cerrar. Dos pasos para que la base se cierre ANTES de lanzar nada:
    //   1. prepare_install_on_close()  vuelve a verificar TODO (firma, versión, tamaño y hash del archivo, abierto con
    //      un manejador que impide modificarlo) y anota "instalación lanzada". Nunca devuelve ok si algo falla.
    //   2. el llamador cierra la base y, si database_closed_cleanly(), llama a launch_install().
    struct InstallLaunchPlan {
        bool ok{false};
        std::string reason;                      // por qué no (técnico, para registros)
        std::filesystem::path installer;
        std::string version;
        std::string arguments;                   // argumentos del instalador silencioso
        std::shared_ptr<void> lock;              // manejador del archivo abierto sin permiso de escritura
    };
    InstallLaunchPlan prepare_install_on_close();

    enum class LaunchOutcome { Launched, UacCancelled, Failed };
    // Lanza el instalador con elevación (ShellExecuteEx "runas"). Con CAFETERIA_TEST_HOOKS y la variable
    // CAFETERIA_UPDATE_TEST_LAUNCH escribe en ese archivo lo que habría lanzado y no ejecuta nada.
    LaunchOutcome launch_install(const InstallLaunchPlan& plan, std::string* error = nullptr) const;

    // Hubo plan válido pero no se pudo intentar el lanzamiento (la base no cerró limpia): el próximo arranque lo muestra
    // como LAUNCH_FAILED. Sin plan válido (sin sesión, pospuesto, portable...) no escribe nada.
    void note_launch_not_attempted(const InstallLaunchPlan& plan) const;

    // La base cerró limpia: no quedan archivos -wal ni -shm con contenido junto a ella.
    static bool database_closed_cleanly(const std::filesystem::path& db_file);

    // Espera a que no haya búsqueda ni descarga en marcha (pruebas y cierre ordenado).
    bool wait_idle(int timeout_ms);

private:
    struct Worker;
    struct Context;

    bool has_permission() const;
    contracts::OperationResult require_permission() const;
    void reconcile_install_state();
    void start_check_thread(bool manual);
    void start_download_thread();

    storage::SqliteDatabase& db_;
    std::shared_ptr<contracts::IAccountService> account_service_;
    std::shared_ptr<Context> ctx_;
    std::shared_ptr<Worker> worker_;
};

} // namespace app::services

#endif // APP_SERVICES_SQLITE_UPDATE_SERVICE_HPP
