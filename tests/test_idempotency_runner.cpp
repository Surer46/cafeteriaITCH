#include <iostream>
#include <string>
#include <filesystem>
#include <cstdlib>

#include "app/storage/sqlite_database.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/services/mock_services.hpp"
#include "app/contracts/sale_service.hpp"
#include "app/contracts/account_service.hpp"

namespace fs = std::filesystem;
using namespace app;

int main(int argc, char* argv[]) {
    std::string data_dir_str;
    std::string idempotency_key;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--data-dir" && i + 1 < argc) {
            data_dir_str = argv[++i];
        } else if (arg == "--key" && i + 1 < argc) {
            idempotency_key = argv[++i];
        }
    }

    if (data_dir_str.empty() || idempotency_key.empty()) {
        std::cerr << "Uso: test_idempotency_runner --data-dir <path> --key <idempotency_key>" << std::endl;
        std::cerr << "Las credenciales (usuario y contraseña) deben suministrarse por stdin en líneas consecutivas." << std::endl;
        return 1;
    }

    // Lectura segura de credenciales desde stdin (sin exponer contraseñas en argv del proceso)
    std::string user;
    std::string pass;
    if (!std::getline(std::cin, user) || !std::getline(std::cin, pass)) {
        std::cerr << "CONFIRM_RETRY_ERROR: No se pudieron leer las credenciales desde stdin." << std::endl;
        return 1;
    }

    if (user.empty() || pass.empty()) {
        std::cerr << "CONFIRM_RETRY_ERROR: Credenciales vacías. No se permite autenticación sin credenciales explícitas." << std::endl;
        return 1;
    }

    fs::path db_dir = fs::u8path(data_dir_str);
    fs::path db_path = db_dir / "cafeteria.db";
    if (!fs::exists(db_path)) {
        std::cerr << "CONFIRM_RETRY_ERROR: No se encontró la base de datos en: " << db_path.u8string() << std::endl;
        return 1;
    }

    storage::SqliteDatabase db;
    auto open_res = db.open(db_path.u8string());
    if (!open_res.is_success()) {
        std::cerr << "CONFIRM_RETRY_ERROR: Fallo al abrir SQLite: " << open_res.error_message() << std::endl;
        return 1;
    }

    auto registry = services::ServiceRegistry::create_sqlite_registry(db);

    // 1. Autenticación con verificación estricta de credenciales
    auto login_res = registry.account_service->login(user, pass);
    if (!login_res.is_success()) {
        std::cerr << "CONFIRM_RETRY_ERROR: Fallo de login: " << login_res.error_message() << std::endl;
        db.close();
        return 1;
    }

    // 2. Reintento real a nivel de contrato ISaleService::confirm_sale
    auto res = registry.sale_service->confirm_sale(
        contracts::PaymentMethod::Efectivo,
        contracts::Money::from_major_minor(200, 0),
        idempotency_key
    );

    if (res.is_success()) {
        std::cout << "CONFIRM_RETRY_FOLIO: " << res.value().folio << std::endl;
        std::cout.flush();
        db.close();
        return 0;
    } else {
        std::cerr << "CONFIRM_RETRY_ERROR: " << res.error_message() << std::endl;
        std::cerr.flush();
        db.close();
        return 1;
    }
}
