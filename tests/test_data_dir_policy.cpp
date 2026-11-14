// Pruebas B7: decision de la carpeta de datos (funcion pura, sin wxWidgets).
// Los callbacks simulan el disco: ninguna prueba crea ni escribe %ProgramData%\CafeteriaITCH.
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <string>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "app/services/data_dir_policy.hpp"

static int g_checks = 0;
#define TEST_CHECK(condition, message) \
    do { \
        ++g_checks; \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA DE CARPETA DE DATOS] " << (message) \
                      << "\n  Condición: " << #condition \
                      << "\n  Archivo:   " << __FILE__ \
                      << "\n  Línea:     " << __LINE__ << "\n" << std::endl; \
            std::exit(1); \
        } \
    } while (0)

using namespace app::services;
namespace fs = std::filesystem;

namespace {

// Disco simulado: conjunto de rutas existentes, archivos con contenido y carpetas no escribibles.
struct FakeDisk {
    std::set<fs::path> existing;
    std::set<fs::path> nonempty_files;
    std::set<fs::path> unwritable;
    std::vector<fs::path> prepared;  // carpetas sobre las que se pidio preparar

    DataDirCallbacks callbacks() {
        DataDirCallbacks cb;
        cb.path_exists = [this](const fs::path& p) { return existing.count(p) > 0 || nonempty_files.count(p) > 0; };
        cb.file_nonempty = [this](const fs::path& p) { return nonempty_files.count(p) > 0; };
        cb.prepare_writable = [this](const fs::path& p) {
            prepared.push_back(p);
            return unwritable.count(p) == 0;
        };
        return cb;
    }
};

DataDirInputs base_inputs(FakeDisk& disk, const fs::path& exe_dir, const fs::path& installed) {
    DataDirInputs in;
    in.exe_dir = exe_dir;
    in.installed_dir = installed;
    in.fs = disk.callbacks();
    return in;
}

bool contains(const std::string& text, const std::string& part) {
    return text.find(part) != std::string::npos;
}

void test_explicit() {
    FakeDisk disk;
    const fs::path exe = fs::path("C:/Program Files/CafeteriaITCH");
    const fs::path installed = fs::path("C:/ProgramData/CafeteriaITCH/data");

    // --data-dir manda sobre todo y no consulta otras ubicaciones.
    auto in = base_inputs(disk, exe, installed);
    in.command_line_dir = fs::path("D:/otra/datos");
    in.env_dir = fs::path("E:/env/datos");
    disk.nonempty_files.insert(installed / kDatabaseFileName);
    auto d = resolve_data_dir(in);
    TEST_CHECK(d.ok && d.mode == DataDirMode::Explicit, "--data-dir debe elegir modo explícito");
    TEST_CHECK(d.data_dir == fs::path("D:/otra/datos"), "--data-dir debe ganar sobre la variable de entorno");
    TEST_CHECK(disk.prepared.size() == 1 && disk.prepared[0] == d.data_dir, "Solo se prepara la carpeta elegida");

    // Variable de entorno cuando no hay argumento; tampoco aplica D3.
    in = base_inputs(disk, exe, installed);
    in.env_dir = fs::path("E:/env/datos");
    d = resolve_data_dir(in);
    TEST_CHECK(d.ok && d.mode == DataDirMode::Explicit && d.data_dir == fs::path("E:/env/datos"),
               "CAFETERIA_DATA_DIR debe usarse sin argumento");

    // Variable de entorno vacia = ausente.
    in = base_inputs(disk, exe, installed);
    in.env_dir = fs::path();
    d = resolve_data_dir(in);
    TEST_CHECK(d.ok && d.mode == DataDirMode::Installed, "Variable vacía debe tratarse como ausente");

    // Argumento vacio: error.
    in = base_inputs(disk, exe, installed);
    in.command_line_dir = fs::path();
    d = resolve_data_dir(in);
    TEST_CHECK(!d.ok && d.error == DataDirError::EmptyArgument, "Argumento vacío debe rechazarse");

    // Carpeta explicita no escribible: error, nunca otra carpeta.
    FakeDisk blocked;
    blocked.unwritable.insert(fs::path("D:/otra/datos"));
    in = base_inputs(blocked, exe, installed);
    in.command_line_dir = fs::path("D:/otra/datos");
    d = resolve_data_dir(in);
    TEST_CHECK(!d.ok && d.error == DataDirError::NotWritable, "Carpeta explícita no escribible debe fallar");
    TEST_CHECK(blocked.prepared.size() == 1, "No debe intentar otra carpeta tras fallar la explícita");
    TEST_CHECK(contains(d.message_utf8, "D:/otra/datos") || contains(d.message_utf8, "D:\\otra\\datos"),
               "El mensaje debe mostrar la ruta");
}

void test_development() {
    FakeDisk disk;
    const fs::path installed = fs::path("C:/ProgramData/CafeteriaITCH/data");
    for (const char* leaf : {"build_release", "build_debug"}) {
        const fs::path root = fs::path("C:/repo");
        auto in = base_inputs(disk, root / leaf, installed);
        // Aunque exista una base en la ubicacion instalada, desarrollo no la consulta.
        disk.nonempty_files.insert(installed / kDatabaseFileName);
        auto d = resolve_data_dir(in);
        TEST_CHECK(d.ok && d.mode == DataDirMode::Development, std::string("Desarrollo en ") + leaf);
        TEST_CHECK(d.data_dir == root / "data", std::string("Datos en la raíz del repositorio para ") + leaf);
    }
    // build_be_* NO cuenta como desarrollo: se comporta como copia instalada.
    for (const char* leaf : {"build_be_release", "build_be_debug", "build_other"}) {
        FakeDisk other;
        auto in = base_inputs(other, fs::path("C:/repo") / leaf, installed);
        auto d = resolve_data_dir(in);
        TEST_CHECK(d.ok && d.mode == DataDirMode::Installed && d.data_dir == installed,
                   std::string(leaf) + " no es modo desarrollo");
    }
    // Desarrollo con carpeta no escribible: error, sin saltar a otra.
    FakeDisk blocked;
    blocked.unwritable.insert(fs::path("C:/repo/data"));
    auto in = base_inputs(blocked, fs::path("C:/repo/build_release"), installed);
    auto d = resolve_data_dir(in);
    TEST_CHECK(!d.ok && d.error == DataDirError::NotWritable && blocked.prepared.size() == 1,
               "Desarrollo no escribible debe fallar sin cambiar de carpeta");
}

void test_portable_and_installed() {
    const fs::path exe = fs::path("C:/Cafetería Ñandú/Programa");
    const fs::path installed = fs::path("C:/ProgramData/CafeteriaITCH/data");

    // Portable: portable.flag junto al exe -> data\ junto al exe.
    {
        FakeDisk disk;
        disk.existing.insert(exe / kPortableFlagName);
        auto d = resolve_data_dir(base_inputs(disk, exe, installed));
        TEST_CHECK(d.ok && d.mode == DataDirMode::Portable, "portable.flag debe dar modo portable");
        TEST_CHECK(d.data_dir == exe / "data", "Portable guarda en data junto al exe (ruta con espacios y acentos)");
    }
    // Instalada: sin portable.flag -> carpeta por equipo.
    {
        FakeDisk disk;
        auto d = resolve_data_dir(base_inputs(disk, exe, installed));
        TEST_CHECK(d.ok && d.mode == DataDirMode::Installed && d.data_dir == installed,
                   "Sin portable.flag debe usar la carpeta por equipo");
        TEST_CHECK(disk.prepared.size() == 1 && disk.prepared[0] == installed, "Solo se prepara la carpeta elegida");
    }
    // Instalada sin carpeta por equipo conocida: error.
    {
        FakeDisk disk;
        auto d = resolve_data_dir(base_inputs(disk, exe, fs::path()));
        TEST_CHECK(!d.ok && d.error == DataDirError::InstalledDirUnknown, "Sin ProgramData debe fallar");
        TEST_CHECK(disk.prepared.empty(), "No debe crear nada si no conoce la carpeta");
    }
    // Instalada con datos propios: se usa, aunque tambien haya datos junto al exe.
    {
        FakeDisk disk;
        disk.nonempty_files.insert(installed / kDatabaseFileName);
        disk.nonempty_files.insert(exe / "data" / kDatabaseFileName);
        auto d = resolve_data_dir(base_inputs(disk, exe, installed));
        TEST_CHECK(d.ok && d.data_dir == installed, "Con base en la carpeta elegida no se bloquea");
    }
    // Carpeta instalada no escribible: error, nunca otra carpeta.
    {
        FakeDisk disk;
        disk.unwritable.insert(installed);
        auto d = resolve_data_dir(base_inputs(disk, exe, installed));
        TEST_CHECK(!d.ok && d.error == DataDirError::NotWritable, "Carpeta por equipo no escribible debe fallar");
        TEST_CHECK(disk.prepared.size() == 1 && disk.prepared[0] == installed,
                   "No debe intentar data junto al exe ni la carpeta del usuario");
        TEST_CHECK(!d.message_utf8.empty() && contains(d.message_utf8, "Llame a quien instaló el sistema"),
                   "El mensaje debe indicar a quién llamar");
    }
    // Portable no escribible (USB protegida): error.
    {
        FakeDisk disk;
        disk.existing.insert(exe / kPortableFlagName);
        disk.unwritable.insert(exe / "data");
        auto d = resolve_data_dir(base_inputs(disk, exe, installed));
        TEST_CHECK(!d.ok && d.error == DataDirError::NotWritable && d.data_dir == exe / "data",
                   "Portable no escribible debe fallar");
    }
}

void test_data_in_other_location() {
    const fs::path exe = fs::path("C:/Archivos de programa/Cafeteria ITCH");
    const fs::path installed = fs::path("C:/ProgramData/CafeteriaITCH/data");

    // Instalada sin base propia, pero con datos junto al exe: no se crea una base vacia.
    {
        FakeDisk disk;
        disk.nonempty_files.insert(exe / "data" / kDatabaseFileName);
        auto d = resolve_data_dir(base_inputs(disk, exe, installed));
        TEST_CHECK(!d.ok && d.error == DataDirError::DataInOtherLocation, "Debe detectar datos en la otra ubicación");
        TEST_CHECK(d.other_dir == exe / "data" && d.data_dir == installed, "Debe informar ambas carpetas");
        TEST_CHECK(disk.prepared.empty(), "No debe crear la carpeta elegida cuando hay datos en otra parte");
        TEST_CHECK(contains(d.message_utf8, installed.u8string()) && contains(d.message_utf8, (exe / "data").u8string()),
                   "El mensaje debe mostrar las dos rutas");
        TEST_CHECK(contains(d.message_utf8, "Llame a quien instaló el sistema"), "El mensaje dice a quién llamar");
        TEST_CHECK(!contains(d.message_utf8, "--") && !contains(d.message_utf8, "data-dir") &&
                       !contains(d.message_utf8, "CAFETERIA_"),
                   "El mensaje no menciona opciones de línea de comandos");
    }
    // Portable sin base propia, pero con datos en la carpeta por equipo.
    {
        FakeDisk disk;
        disk.existing.insert(exe / kPortableFlagName);
        disk.nonempty_files.insert(installed / kDatabaseFileName);
        auto d = resolve_data_dir(base_inputs(disk, exe, installed));
        TEST_CHECK(!d.ok && d.error == DataDirError::DataInOtherLocation && d.other_dir == installed,
                   "Portable con datos por equipo debe bloquearse");
    }
    // Una base vacia (0 bytes) no cuenta como datos.
    {
        FakeDisk disk;
        disk.existing.insert(exe / "data" / kDatabaseFileName);  // existe pero no esta en nonempty_files
        auto d = resolve_data_dir(base_inputs(disk, exe, installed));
        TEST_CHECK(d.ok && d.data_dir == installed, "Un cafeteria.db vacío no debe bloquear");
    }
    // Sin datos en ninguna parte: arranque limpio normal.
    {
        FakeDisk disk;
        auto d = resolve_data_dir(base_inputs(disk, exe, installed));
        TEST_CHECK(d.ok && d.mode == DataDirMode::Installed, "Sin datos en ningún lado debe arrancar");
    }
    // Explicito y desarrollo no aplican D3 (ya cubierto arriba); aqui, explicito con datos en otra parte.
    {
        FakeDisk disk;
        disk.nonempty_files.insert(exe / "data" / kDatabaseFileName);
        disk.nonempty_files.insert(installed / kDatabaseFileName);
        auto in = base_inputs(disk, exe, installed);
        in.command_line_dir = fs::path("D:/mis datos");
        TEST_CHECK(resolve_data_dir(in).ok, "Con --data-dir explícito no se aplica la comprobación de otras ubicaciones");
    }
}

void test_real_filesystem_callbacks() {
    // Rutas con espacios y acentos sobre un directorio temporal propio.
    std::error_code ec;
    const fs::path root = fs::temp_directory_path(ec) / L"cafeteria_b7_datos Ñandú café";
    fs::remove_all(root, ec);
    auto cb = make_filesystem_callbacks();

    const fs::path dir = root / L"datos áéí" / "sub carpeta";
    TEST_CHECK(cb.prepare_writable(dir), "Debe crear y escribir en una ruta con espacios y acentos");
    TEST_CHECK(fs::is_directory(dir, ec), "La carpeta debe existir tras prepararla");
    TEST_CHECK(!fs::exists(dir / ".escritura-prueba.tmp", ec), "No debe dejar el archivo de prueba");
    TEST_CHECK(cb.path_exists(dir), "path_exists debe ver la carpeta");

    const fs::path db = dir / kDatabaseFileName;
    TEST_CHECK(!cb.file_nonempty(db), "Sin archivo no hay datos");
    { std::ofstream(db, std::ios::binary); }
    TEST_CHECK(cb.path_exists(db) && !cb.file_nonempty(db), "Archivo de 0 bytes no cuenta como datos");
    { std::ofstream out(db, std::ios::binary); out << "datos"; }
    TEST_CHECK(cb.file_nonempty(db), "Archivo con contenido sí cuenta");

    // Una carpeta que no se puede crear porque ya hay un ARCHIVO con ese nombre: falla (no hay otra carpeta).
    const fs::path blocker = root / "es un archivo";
    { std::ofstream out(blocker, std::ios::binary); out << "x"; }
    TEST_CHECK(!cb.prepare_writable(blocker), "Una carpeta que es un archivo no es escribible");
    TEST_CHECK(!cb.prepare_writable(blocker / "hija"), "Crear dentro de un archivo debe fallar");

#ifdef _WIN32
    // Carpeta de solo lectura real: el atributo no impide crear archivos dentro en Windows, asi que
    // se prueba con un archivo de solo lectura que ocupa el nombre de la prueba de escritura.
    const fs::path ro_dir = root / "solo lectura";
    fs::create_directories(ro_dir, ec);
    const fs::path probe = ro_dir / ".escritura-prueba.tmp";
    { std::ofstream out(probe, std::ios::binary); out << "x"; }
    SetFileAttributesW(probe.c_str(), FILE_ATTRIBUTE_READONLY);
    TEST_CHECK(!cb.prepare_writable(ro_dir), "Si no se puede escribir el archivo de prueba, debe fallar");
    SetFileAttributesW(probe.c_str(), FILE_ATTRIBUTE_NORMAL);
#endif

    fs::remove_all(root, ec);
    TEST_CHECK(!fs::exists(root, ec), "La prueba debe limpiar su carpeta temporal");
}

} // namespace

int main() {
    test_explicit();
    test_development();
    test_portable_and_installed();
    test_data_in_other_location();
    test_real_filesystem_callbacks();
    std::cout << "Pruebas de carpeta de datos (B7) correctas. TEST_CHECK ejecutados: " << g_checks << std::endl;
    return 0;
}
