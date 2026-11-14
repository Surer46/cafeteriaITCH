// Pruebas B7: paquete (cmake --install), lista exacta de archivos, DLL importadas y autocomprobacion
// del ejecutable instalado con un PATH reducido a las carpetas de Windows.
//
// Solo tiene sentido en Release (el empaquetado se niega en Debug): en otros casos termina con el
// codigo 77, que CTest marca como OMITIDA (SKIP_RETURN_CODE). No depende del directorio de trabajo
// ni deja archivos fuera de una carpeta temporal propia que borra al terminar. Nunca toca ni crea
// %ProgramData%\CafeteriaITCH (solo comprueba que sigue igual).
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#ifndef _WIN32
#error "test_package.cpp solo se compila en Windows"
#endif
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int g_checks = 0;
#define TEST_CHECK(condition, message) \
    do { \
        ++g_checks; \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA DE PAQUETE] " << (message) \
                      << "\n  Condición: " << #condition \
                      << "\n  Archivo:   " << __FILE__ \
                      << "\n  Línea:     " << __LINE__ << "\n" << std::endl; \
            cleanup_and_exit(1); \
        } \
    } while (0)

namespace fs = std::filesystem;

namespace {

fs::path g_root;  // carpeta temporal propia de esta prueba

void cleanup_and_exit(int code) {
    std::error_code ec;
    if (!g_root.empty()) fs::remove_all(g_root, ec);
    std::exit(code);
}

std::wstring W(const char* s) { return fs::u8path(s).wstring(); }

std::wstring quote(const std::wstring& s) { return L"\"" + s + L"\""; }

std::wstring upper(std::wstring s) {
    for (auto& c : s) c = static_cast<wchar_t>(towupper(c));
    return s;
}

// Ejecuta un proceso y espera. Devuelve su codigo de salida, o -1 si no termino en el plazo (se mata:
// una ventana o diálogo que espera al usuario se vería exactamente así).
long run_process(const std::wstring& command_line, const std::map<std::wstring, std::wstring>& env_overrides,
                 const fs::path& stdout_file, unsigned long timeout_ms) {
    // Entorno heredado + sustituciones.
    std::map<std::wstring, std::wstring> env_upper_to_entry;
    std::vector<std::pair<std::wstring, std::wstring>> env_list;
    wchar_t* block = GetEnvironmentStringsW();
    for (wchar_t* p = block; *p;) {
        std::wstring entry(p);
        p += entry.size() + 1;
        auto eq = entry.find(L'=', 1);
        if (eq == std::wstring::npos) continue;
        env_list.emplace_back(entry.substr(0, eq), entry.substr(eq + 1));
    }
    FreeEnvironmentStringsW(block);
    for (const auto& ov : env_overrides) {
        bool found = false;
        for (auto& e : env_list) {
            if (upper(e.first) == upper(ov.first)) {
                e.second = ov.second;
                found = true;
            }
        }
        if (!found) env_list.emplace_back(ov.first, ov.second);
    }
    std::wstring env_block;
    for (const auto& e : env_list) {
        env_block += e.first + L"=" + e.second;
        env_block.push_back(L'\0');
    }
    env_block.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    HANDLE out_handle = INVALID_HANDLE_VALUE;
    if (!stdout_file.empty()) {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        out_handle = CreateFileW(stdout_file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS,
                                 FILE_ATTRIBUTE_NORMAL, nullptr);
        si.dwFlags = STARTF_USESTDHANDLES;
        si.hStdOutput = out_handle;
        si.hStdError = out_handle;
        si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    }
    PROCESS_INFORMATION pi{};
    std::wstring cmd = command_line;  // CreateProcessW puede modificar el buffer
    BOOL created = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, !stdout_file.empty(),
                                  CREATE_UNICODE_ENVIRONMENT | CREATE_NO_WINDOW, env_block.data(), nullptr, &si, &pi);
    if (!created) {
        if (out_handle != INVALID_HANDLE_VALUE) CloseHandle(out_handle);
        return -2;
    }
    long result;
    if (WaitForSingleObject(pi.hProcess, timeout_ms) == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 99);
        WaitForSingleObject(pi.hProcess, 5000);
        result = -1;
    } else {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        result = static_cast<long>(code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    if (out_handle != INVALID_HANDLE_VALUE) CloseHandle(out_handle);
    return result;
}

std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string lower(std::string s) {
    for (auto& c : s) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string generic(const fs::path& p) { return p.generic_u8string(); }

// Lista de archivos (rutas relativas con /) bajo root.
std::set<std::string> list_files(const fs::path& root) {
    std::set<std::string> out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(root, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (it->is_regular_file()) out.insert(generic(fs::relative(it->path(), root, ec)));
    }
    return out;
}

std::string join(const std::set<std::string>& s) {
    std::string r;
    for (const auto& x : s) r += "\n    " + x;
    return r;
}

// Importaciones de un PE con objdump (independiente del script de empaquetado).
std::vector<std::string> imports_of(const fs::path& pe, const fs::path& scratch) {
    const fs::path out = scratch / "objdump.txt";
    long rc = run_process(quote(W(OBJDUMP_PATH)) + L" -p " + quote(pe.wstring()), {}, out, 60000);
    TEST_CHECK(rc == 0, "objdump debe poder leer " + pe.string());
    std::vector<std::string> names;
    std::istringstream ss(read_text(out));
    std::string line;
    const std::string tag = "DLL Name: ";
    while (std::getline(ss, line)) {
        auto pos = line.find(tag);
        if (pos == std::string::npos) continue;
        std::string n = line.substr(pos + tag.size());
        while (!n.empty() && (n.back() == '\r' || n.back() == ' ')) n.pop_back();
        names.push_back(lower(n));
    }
    return names;
}

std::set<std::string> read_system_dlls() {
    std::set<std::string> out;
    std::ifstream in(SYSTEM_DLLS_FILE_PATH);
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        out.insert(lower(line));
    }
    return out;
}

bool dir_is_empty(const fs::path& p) {
    std::error_code ec;
    return fs::is_directory(p, ec) && fs::directory_iterator(p, ec) == fs::directory_iterator();
}

} // namespace

#ifndef PACKAGE_TEST_ENABLED
#define PACKAGE_TEST_ENABLED 0
#endif

int main() {
    if (!PACKAGE_TEST_ENABLED) {
        std::cout << "OMITIDA: la prueba de paquete solo corre en una compilacion Release con wxWidgets y sin "
                     "CAFETERIA_TEST_HOOKS (el empaquetado se niega en Debug)." << std::endl;
        return 77;
    }

    std::error_code ec;
    g_root = fs::temp_directory_path(ec) / (L"cafeteria b7 paquete ñ " + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);
    const fs::path portable = g_root / "portable zip";   // lo que contiene el ZIP (todos los componentes)
    const fs::path installer = g_root / "instalador";    // lo que instala el instalador (componente app)
    const fs::path private_tmp = g_root / "tmp privado";
    const fs::path scratch = g_root / "scratch";
    fs::create_directories(private_tmp, ec);
    fs::create_directories(scratch, ec);

    // Carpeta de datos por equipo: solo se comprueba que NO aparece por culpa de la prueba.
    const char* program_data_env = std::getenv("ProgramData");
    const fs::path program_data_dir = program_data_env ? fs::path(program_data_env) / "CafeteriaITCH" : fs::path();
    const bool program_data_existed_before = !program_data_dir.empty() && fs::exists(program_data_dir, ec);

    // 1. Instalacion en dos prefijos temporales.
    const std::wstring cmake = quote(W(CMAKE_COMMAND_PATH));
    const std::wstring build = quote(W(BUILD_DIR_PATH));
    long rc = run_process(cmake + L" --install " + build + L" --config Release --prefix " + quote(portable.wstring()),
                          {}, scratch / "install_portable.log", 300000);
    TEST_CHECK(rc == 0, "cmake --install (todos los componentes) debe terminar con 0. Ver:\n" +
                            read_text(scratch / "install_portable.log"));
    rc = run_process(cmake + L" --install " + build + L" --config Release --component app --prefix " +
                         quote(installer.wstring()),
                     {}, scratch / "install_app.log", 300000);
    TEST_CHECK(rc == 0, "cmake --install --component app debe terminar con 0. Ver:\n" +
                            read_text(scratch / "install_app.log"));

    // 2. Lista exacta. Los archivos esperados salen del repositorio y de las importaciones reales.
    const fs::path source = fs::u8path(SOURCE_DIR_PATH);
    std::set<std::string> expected_common = {"CafeteriaITCH.exe", "paquete.manifest"};
    for (const auto& f : list_files(source / "assets")) expected_common.insert("assets/" + f);
    for (const auto& entry : fs::directory_iterator(source / "migrations", ec)) {
        if (entry.path().extension() == ".sql") expected_common.insert("migrations/" + generic(entry.path().filename()));
    }
    for (const auto& f : list_files(source / "packaging" / "licencias")) expected_common.insert("licencias/" + f);

    // DLL: cierre transitivo de importaciones que no son de Windows, calculado aqui con objdump.
    const auto system_dlls = read_system_dlls();
    TEST_CHECK(!system_dlls.empty(), "La lista de DLL de Windows permitidas no debe estar vacía");
    std::set<std::string> dll_closure;
    std::vector<fs::path> queue = {portable / "CafeteriaITCH.exe"};
    std::set<std::string> missing;
    while (!queue.empty()) {
        fs::path current = queue.back();
        queue.pop_back();
        for (const auto& dll : imports_of(current, scratch)) {
            if (system_dlls.count(dll) || dll_closure.count(dll)) continue;
            if (!fs::exists(portable / dll, ec)) {
                missing.insert(dll);
                continue;
            }
            dll_closure.insert(dll);
            queue.push_back(portable / dll);
        }
    }
    TEST_CHECK(missing.empty(), "Toda DLL importada que no es de Windows debe estar dentro del paquete. Faltan:" + join(missing));
    TEST_CHECK(dll_closure.count("libgcc_s_seh-1.dll") && dll_closure.count("libstdc++-6.dll") &&
                   dll_closure.count("libwinpthread-1.dll"),
               "El cierre debe incluir el runtime de GCC y winpthreads");
    for (const auto& d : dll_closure) expected_common.insert(d);

    std::set<std::string> expected_portable = expected_common;
    expected_portable.insert("portable.flag");

    const auto actual_portable = list_files(portable);
    const auto actual_installer = list_files(installer);
    TEST_CHECK(actual_portable == expected_portable,
               "Lista del ZIP (portable) distinta de la esperada.\n  Real:" + join(actual_portable) +
                   "\n  Esperada:" + join(expected_portable));
    TEST_CHECK(actual_installer == expected_common,
               "Lista del instalador distinta de la esperada.\n  Real:" + join(actual_installer) +
                   "\n  Esperada:" + join(expected_common));
    TEST_CHECK(actual_portable.count("portable.flag") == 1, "El ZIP debe llevar portable.flag");
    TEST_CHECK(actual_installer.count("portable.flag") == 0, "El instalador NO debe llevar portable.flag");

    // Nada prohibido (en ambas listas).
    for (const auto* listing : {&actual_portable, &actual_installer}) {
        for (const auto& f : *listing) {
            const std::string l = lower(f);
            auto ends = [&](const char* ext) { return l.size() >= std::strlen(ext) && l.compare(l.size() - std::strlen(ext), std::strlen(ext), ext) == 0; };
            TEST_CHECK(!ends(".db") && !ends(".sqlite") && !ends(".cfg") && !ends(".bak") && !ends("-wal") && !ends("-shm"),
                       "No debe haber bases, respaldos ni .cfg: " + f);
            TEST_CHECK(l.find("333ud_") == std::string::npos, "No debe haber DLL de depuración: " + f);
            TEST_CHECK(l.rfind("test_", 0) != 0 && l.find("/test_") == std::string::npos,
                       "No debe haber ejecutables de prueba: " + f);
            TEST_CHECK(!ends(".exe") || l == "cafeteriaitch.exe", "Solo CafeteriaITCH.exe puede ser ejecutable: " + f);
        }
    }

    // 3. Recurso de version: llega al ejecutable instalado.
    {
        const std::wstring exe = (portable / "CafeteriaITCH.exe").wstring();
        DWORD handle = 0;
        DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &handle);
        TEST_CHECK(size > 0, "El ejecutable debe tener recurso de versión");
        std::vector<char> data(size);
        TEST_CHECK(GetFileVersionInfoW(exe.c_str(), 0, size, data.data()), "Debe poder leerse el recurso de versión");
        VS_FIXEDFILEINFO* fixed = nullptr;
        UINT len = 0;
        TEST_CHECK(VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &len) && fixed,
                   "Debe existir VS_FIXEDFILEINFO");
        const std::string version = PACKAGE_VERSION;
        unsigned maj = 0, min = 0, pat = 0;
        TEST_CHECK(std::sscanf(version.c_str(), "%u.%u.%u", &maj, &min, &pat) == 3, "Versión del proyecto con formato a.b.c");
        TEST_CHECK(HIWORD(fixed->dwFileVersionMS) == maj && LOWORD(fixed->dwFileVersionMS) == min &&
                       HIWORD(fixed->dwFileVersionLS) == pat,
                   "La versión del recurso debe coincidir con project(VERSION)");
        wchar_t* file_version = nullptr;
        TEST_CHECK(VerQueryValueW(data.data(), L"\\StringFileInfo\\040904b0\\FileVersion",
                                  reinterpret_cast<void**>(&file_version), &len) && file_version,
                   "Debe existir la cadena FileVersion");
        TEST_CHECK(std::wstring(file_version) == std::wstring(version.begin(), version.end()),
                   "FileVersion debe ser la versión del proyecto");
    }

    // 4. Autocomprobacion del ejecutable instalado, con PATH reducido a las carpetas de Windows.
    const char* sysroot_c = std::getenv("SystemRoot");
    TEST_CHECK(sysroot_c != nullptr, "Se necesita SystemRoot");
    const fs::path sysroot = sysroot_c;
    const std::map<std::wstring, std::wstring> reduced_env = {
        {L"PATH", (sysroot / "System32").wstring() + L";" + sysroot.wstring()},
        {L"TMP", private_tmp.wstring()},
        {L"TEMP", private_tmp.wstring()},
    };
    const fs::path exe = portable / "CafeteriaITCH.exe";
    const fs::path report = scratch / "informe autocomprobación.txt";

    rc = run_process(quote(exe.wstring()) + L" --self-check \"--self-check-report=" + report.wstring() + L"\"",
                     reduced_env, {}, 60000);
    TEST_CHECK(rc == 0, "La autocomprobación del paquete debe terminar con 0 (código real: " + std::to_string(rc) +
                            "; -1 = no terminó, posible ventana). Informe: " + read_text(report));
    const std::string ok_report = read_text(report);
    TEST_CHECK(ok_report.find("RESULTADO: CORRECTO") != std::string::npos, "El informe debe decir CORRECTO:\n" + ok_report);
    TEST_CHECK(ok_report.find(std::string("Versión: ") + PACKAGE_VERSION) != std::string::npos,
               "El informe debe incluir la versión");
    {
        size_t migration_files = 0;
        for (const auto& f : expected_common) if (f.rfind("migrations/", 0) == 0) ++migration_files;
        const std::string both = "Migraciones encontradas: " + std::to_string(migration_files) +
                                 "\nMigraciones aplicadas: " + std::to_string(migration_files) + "\n";
        TEST_CHECK(ok_report.find(both) != std::string::npos,
                   "El informe debe comparar migraciones aplicadas con archivos:\n" + ok_report);
    }
    TEST_CHECK(ok_report.find("[OK] assets/fonts/Roboto-Regular.ttf") != std::string::npos &&
                   ok_report.find("[OK] assets/icons/") != std::string::npos,
               "El informe debe listar fuentes e iconos esperados");
    TEST_CHECK(dir_is_empty(private_tmp), "No debe quedar carpeta temporal tras la autocomprobación");
    TEST_CHECK(list_files(portable) == expected_portable, "La autocomprobación no debe crear archivos en el paquete (ni data\\)");

    // Variantes: informe como argumento separado, opcion desconocida y sin informe; todas sin ventanas.
    const fs::path report2 = scratch / "informe2.txt";
    rc = run_process(quote(exe.wstring()) + L" --self-check --self-check-report " + quote(report2.wstring()),
                     reduced_env, {}, 60000);
    TEST_CHECK(rc == 0 && read_text(report2).find("RESULTADO: CORRECTO") != std::string::npos,
               "El informe también debe aceptar el valor como argumento separado");
    rc = run_process(quote(exe.wstring()) + L" --opcion-inexistente --self-check", reduced_env, {}, 60000);
    TEST_CHECK(rc == 0, "Una opción desconocida no debe abrir el diálogo del analizador (código " + std::to_string(rc) + ")");
    TEST_CHECK(dir_is_empty(private_tmp), "Sin informe tampoco debe quedar carpeta temporal");

    // Informe que no se puede escribir (es una carpeta): codigo 2, sin ventana.
    rc = run_process(quote(exe.wstring()) + L" --self-check \"--self-check-report=" + scratch.wstring() + L"\"",
                     reduced_env, {}, 60000);
    TEST_CHECK(rc == 2, "Si no se puede escribir el informe el código debe ser 2 (fue " + std::to_string(rc) + ")");

    // 5. Negativas: sin migraciones; sin una fuente.
    fs::rename(portable / "migrations", portable / "migraciones-ausentes", ec);
    TEST_CHECK(!ec, "No se pudo apartar la carpeta de migraciones para la prueba negativa");
    const fs::path bad_report = scratch / "informe malo.txt";
    rc = run_process(quote(exe.wstring()) + L" --self-check \"--self-check-report=" + bad_report.wstring() + L"\"",
                     reduced_env, {}, 60000);
    TEST_CHECK(rc != 0 && rc != -1 && rc != -2, "Sin migraciones el código debe ser distinto de 0 y sin ventana (fue " +
                                                    std::to_string(rc) + ")");
    const std::string bad_text = read_text(bad_report);
    TEST_CHECK(bad_text.find("RESULTADO: FALLÓ") != std::string::npos &&
                   bad_text.find("No se encontró la carpeta de migraciones") != std::string::npos,
               "El informe debe explicar el motivo:\n" + bad_text);
    TEST_CHECK(dir_is_empty(private_tmp), "Tras un fallo tampoco debe quedar carpeta temporal");
    fs::rename(portable / "migraciones-ausentes", portable / "migrations", ec);
    TEST_CHECK(!ec, "No se pudo restaurar migrations");

    fs::rename(portable / "assets" / "fonts" / "Roboto-Bold.ttf", portable / "assets" / "fonts" / "Roboto-Bold.ttf.off", ec);
    TEST_CHECK(!ec, "No se pudo apartar una fuente para la prueba negativa");
    rc = run_process(quote(exe.wstring()) + L" --self-check \"--self-check-report=" + bad_report.wstring() + L"\"",
                     reduced_env, {}, 60000);
    const std::string font_text = read_text(bad_report);
    TEST_CHECK(rc == 1 && font_text.find("[FALTA] assets/fonts/Roboto-Bold.ttf") != std::string::npos,
               "Sin una fuente el código debe ser 1 y el informe nombrarla:\n" + font_text);
    TEST_CHECK(dir_is_empty(private_tmp), "Tras faltar una fuente tampoco debe quedar carpeta temporal");

    // 6. La prueba no creo la carpeta de datos por equipo.
    if (!program_data_dir.empty() && !program_data_existed_before) {
        TEST_CHECK(!fs::exists(program_data_dir, ec), "La prueba no debe crear %ProgramData%\\CafeteriaITCH");
    }

    std::cout << "Pruebas de paquete (B7) correctas. TEST_CHECK ejecutados: " << g_checks << std::endl;
    cleanup_and_exit(0);
}
