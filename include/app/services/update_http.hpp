#ifndef APP_SERVICES_UPDATE_HTTP_HPP
#define APP_SERVICES_UPDATE_HTTP_HPP

#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Cliente HTTPS mínimo para las actualizaciones. SOLO https y con validación de certificado; nada de HTTP plano.
// La interfaz es inyectable: las pruebas usan un cliente falso y nunca tocan la red real.
namespace app::services {

enum class HttpError {
    None,
    NoNetwork,        // sin conexión, DNS o servidor inalcanzable
    Timeout,
    TlsUnavailable,   // Windows no admite TLS 1.2 (Windows 7 sin la actualización correspondiente)
    TlsFailure,       // certificado o conexión segura inválidos
    NotHttps,
    BadStatus,        // la respuesta no fue 200/206 (ver status)
    TooLarge,
    Cancelled,
    Other
};

struct HttpResult {
    HttpError error{HttpError::None};
    int status{0};
    std::string detail;               // técnico, solo para registros
    std::vector<std::uint8_t> body;   // solo en get()
};

class IHttpClient {
public:
    virtual ~IHttpClient() = default;

    // GET de un recurso pequeño. `max_bytes` acota la respuesta; `total_timeout_ms` acota toda la operación.
    virtual HttpResult get(const std::string& url, std::size_t max_bytes, int total_timeout_ms) = 0;

    // Descarga a `part_file`, reanudando desde el tamaño que ya tenga si el servidor lo admite. `expected_total`
    // acota el tamaño (0 = sin dato). `on_progress(bytes_en_el_archivo)` devuelve false para cancelar.
    virtual HttpResult download(const std::string& url, const std::filesystem::path& part_file,
                                std::uint64_t expected_total,
                                const std::function<bool(std::uint64_t)>& on_progress,
                                int total_timeout_ms) = 0;
};

// Cliente WinHTTP de Windows (sin DLL adicionales): TLS 1.2, validación de certificado y tiempos cortos.
std::shared_ptr<IHttpClient> make_winhttp_client();

// Cliente por omisión de la aplicación: WinHTTP. En compilaciones con CAFETERIA_TEST_HOOKS y con la variable de
// entorno de pruebas de actualización definida, lee de una carpeta local (nunca existe en el paquete).
std::shared_ptr<IHttpClient> make_default_update_http_client();

} // namespace app::services

#endif // APP_SERVICES_UPDATE_HTTP_HPP
