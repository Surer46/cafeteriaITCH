#ifndef APP_CONTRACTS_UPDATE_SERVICE_HPP
#define APP_CONTRACTS_UPDATE_SERVICE_HPP

#include <cstdint>
#include <string>
#include <vector>
#include "app/contracts/result.hpp"

// Actualizaciones del programa (B11). Todos los métodos exigen el permiso updates.manage (por omisión, solo el
// Dueño), se comprueban dentro del servicio y devuelven textos en lenguaje llano (sin jerga técnica).
// Las operaciones largas (buscar, descargar) son ASÍNCRONAS: arrancan y vuelven de inmediato; la interfaz
// consulta get_update_status() cada segundo o dos mientras el estado sea CHECKING o DOWNLOADING.
//
// Flujo normal: el programa busca solo al abrir (a lo sumo cada 24 h). Si hay versión nueva, state = AVAILABLE; el
// Dueño decide con accept_update() ("Descargar e instalar al cerrar") y, al cerrar el programa, se instala.
namespace app::contracts {

struct UpdateStatusDto {
    // UP_TO_DATE      sin novedad
    // CHECKING        buscando
    // AVAILABLE       hay una versión nueva (puede estar ya descargada; accepted = false)
    // DOWNLOADING     descargando (downloaded_bytes de installer_size_bytes) por haber aceptado
    // READY           descargada, verificada y aceptada: se instalará al cerrar el programa
    // ERROR           la última búsqueda falló (error_kind y error_message; el programa sigue igual)
    // NOT_CONFIGURED  este programa todavía no tiene activadas las actualizaciones por internet
    std::string state{"UP_TO_DATE"};
    std::string installed_version;
    std::string available_version;           // vacío si no hay
    std::vector<std::string> notes;          // lo que cambia, una frase cada una
    std::int64_t installer_size_bytes{0};
    std::int64_t downloaded_bytes{0};
    bool download_verified{false};           // la descarga ya está completa y comprobada
    bool accepted{false};                    // el Dueño aceptó: se instalará al cerrar
    bool will_install_on_close{false};       // accepted && download_verified && !postponed_this_session && !is_portable
    bool postponed_this_session{false};      // "Cerrar sin instalar" elegido en esta sesión
    bool is_portable{false};                 // copia portable: avisa pero no se actualiza sola
    std::string last_check_at;               // hora local de la última comprobación; vacío si nunca
    std::string next_manual_check_at;        // hora local desde la que se puede volver a buscar a mano; vacío = ya
    bool manual_check_throttled{false};      // se pidió buscar demasiado pronto (no es un error)
    // NO_NETWORK, TIMEOUT, TLS_UNAVAILABLE, TLS_FAILURE, SERVER, INVALID, DOWNLOAD o vacío
    std::string error_kind;
    std::string error_message;               // lenguaje llano; vacío si no hay error
    bool error_from_manual_check{false};     // la búsqueda fallida la pidió una persona (mostrarlo); si no, es silencioso
    std::string info_message;                // texto llano adicional (p. ej. por qué una copia portable no se actualiza)
};

struct UpdateInstallResultDto {
    bool has_result{false};   // hay un resultado de instalación que todavía no se mostró
    bool success{false};
    std::string version;      // versión objetivo
    std::string message;      // lenguaje llano
};

class IUpdateService {
public:
    virtual ~IUpdateService() = default;

    // Estado completo (ver UpdateStatusDto). Uso: auto s = svc->get_update_status().value();
    virtual Result<UpdateStatusDto> get_update_status() = 0;

    // Busca ahora, en segundo plano; vuelve de inmediato. Se salta el límite de una vez al día, pero no se puede
    // repetir antes de 10 minutos: en ese caso devuelve éxito, no busca y el estado lo dice (manual_check_throttled).
    // Uso: svc->check_for_updates(); luego sondear get_update_status().
    virtual OperationResult check_for_updates() = 0;

    // "Descargar e instalar al cerrar": descarga (si falta), verifica y deja aceptada. Si la copia es portable,
    // devuelve InvalidState con el motivo. Uso: svc->accept_update();
    virtual OperationResult accept_update() = 0;

    // Quita la aceptación: vuelve a AVAILABLE conservando la descarga verificada. Uso: svc->decline_update();
    virtual OperationResult decline_update() = 0;

    // Cancela una descarga en curso: la versión queda AVAILABLE y NO aceptada. Uso: svc->cancel_download();
    virtual OperationResult cancel_download() = 0;

    // Actualización desde archivos (USB): instalador, manifiesto y firma, con la misma verificación completa.
    // Si verifica, queda aceptada para instalar al cerrar. Uso: svc->use_update_from_files(exe, json, sig);
    virtual OperationResult use_update_from_files(
        const std::string& installer_path,
        const std::string& manifest_path,
        const std::string& signature_path) = 0;

    // "Cerrar sin instalar": ese cierre no lanza nada y la aceptación sigue para el próximo. Solo en memoria.
    // Uso: svc->postpone_install_this_session();
    virtual OperationResult postpone_install_this_session() = 0;

    // Resultado de la última instalación, para mostrarlo UNA vez en el arranque siguiente.
    // Uso: auto r = svc->get_last_install_result().value(); if (r.has_result) { mostrar(r.message); svc->acknowledge_install_result(); }
    virtual Result<UpdateInstallResultDto> get_last_install_result() = 0;
    virtual OperationResult acknowledge_install_result() = 0;
};

} // namespace app::contracts

#endif // APP_CONTRACTS_UPDATE_SERVICE_HPP
