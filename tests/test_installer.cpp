// Prueba B10: el instalador de Windows (Inno Setup) se compila sin advertencias y lleva la version.
// NO instala nada: solo compila. Corre solo en Release y solo si ISCC.exe esta disponible; en otro caso
// termina con 77 (OMITIDA en CTest, como PackageTest).
// Trabaja en una carpeta temporal propia que borra al terminar y no depende del directorio de trabajo.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace fs = std::filesystem;

static fs::path g_root;
static int g_checks = 0;
static void cleanup_and_exit(int code) {
    std::error_code ec;
    if (!g_root.empty()) fs::remove_all(g_root, ec);
    std::exit(code);
}
#define TEST_CHECK(condition, message) \
    do { \
        ++g_checks; \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA DEL INSTALADOR] " << (message) << "\n  Condición: " << #condition \
                      << "\n  Línea: " << __LINE__ << "\n" << std::endl; \
            cleanup_and_exit(1); \
        } \
    } while (0)

#ifndef INSTALLER_TEST_ENABLED
#define INSTALLER_TEST_ENABLED 0
#endif

static std::wstring W(const char* s) { return fs::u8path(s).wstring(); }
static std::wstring quote(const std::wstring& s) { return L"\"" + s + L"\""; }

static std::string read_text(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

static long run(const std::wstring& cmdline, const fs::path& out_file, unsigned long timeout_ms) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE h = CreateFileW(out_file.c_str(), GENERIC_WRITE, FILE_SHARE_READ, &sa, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = h;
    si.hStdError = h;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    PROCESS_INFORMATION pi{};
    std::wstring cmd = cmdline;
    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(h);
        return -2;
    }
    long result;
    if (WaitForSingleObject(pi.hProcess, timeout_ms) == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, 99);
        result = -1;
    } else {
        DWORD code = 0;
        GetExitCodeProcess(pi.hProcess, &code);
        result = static_cast<long>(code);
    }
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(h);
    return result;
}

int main() {
    if (!INSTALLER_TEST_ENABLED) {
        std::cout << "OMITIDA: la prueba del instalador solo corre en Release con ISCC.exe disponible "
                     "(<toolchain>\\innosetup)." << std::endl;
        return 77;
    }
    std::error_code ec;
    g_root = fs::temp_directory_path(ec) / (L"cafeteria b10 instalador ñ " + std::to_wstring(GetCurrentProcessId()));
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);
    const fs::path stage = g_root / "stage";
    const fs::path out = g_root / "salida instalador";
    const std::string version = PACKAGE_VERSION;

    const std::wstring cmd = quote(W(CMAKE_COMMAND_PATH)) + L" -DBUILD_DIR=" + quote(W(BUILD_DIR_PATH)) + L" -DISCC=" +
                             quote(W(ISCC_PATH)) + L" -DISS=" + quote(W(ISS_PATH)) + L" -DAPP_VERSION=" + fs::u8path(version).wstring() +
                             L" -DSTAGE_DIR=" + quote(stage.wstring()) + L" -DOUT_DIR=" + quote(out.wstring()) + L" -P " +
                             quote(W(SCRIPT_PATH));
    const long rc = run(cmd, g_root / "salida.log", 600000);
    const std::string log = read_text(g_root / "salida.log");
    TEST_CHECK(rc == 0, "build_installer.cmake debe terminar con 0 (código " + std::to_string(rc) + "). Salida:\n" + log);

    // Sin advertencias del compilador de Inno Setup (el script ya falla si las hay; se comprueba el texto también).
    std::string lower = log;
    for (auto& c : lower) c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
    TEST_CHECK(lower.find("warning") == std::string::npos, "ISCC no debe emitir advertencias:\n" + log);
    TEST_CHECK(lower.find("successful compile") != std::string::npos, "ISCC debe terminar con compilación correcta:\n" + log);

    // El instalador existe, es grande (lleva el paquete) y su nombre y su recurso de versión llevan la versión.
    const fs::path installer = out / ("CafeteriaITCH-" + version + "-instalador-win64.exe");
    TEST_CHECK(fs::is_regular_file(installer, ec), "Debe existir " + installer.string());
    TEST_CHECK(fs::file_size(installer, ec) > 5ull * 1024 * 1024, "El instalador debe llevar el paquete (>5 MB)");
    {
        const std::wstring exe = installer.wstring();
        DWORD handle = 0;
        DWORD size = GetFileVersionInfoSizeW(exe.c_str(), &handle);
        TEST_CHECK(size > 0, "El instalador debe tener recurso de versión");
        std::vector<char> data(size);
        TEST_CHECK(GetFileVersionInfoW(exe.c_str(), 0, size, data.data()), "Leer el recurso de versión");
        VS_FIXEDFILEINFO* fixed = nullptr;
        UINT len = 0;
        TEST_CHECK(VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fixed), &len) && fixed, "VS_FIXEDFILEINFO");
        unsigned maj = 0, min = 0, pat = 0;
        TEST_CHECK(std::sscanf(version.c_str(), "%u.%u.%u", &maj, &min, &pat) == 3, "Versión a.b.c");
        TEST_CHECK(HIWORD(fixed->dwFileVersionMS) == maj && LOWORD(fixed->dwFileVersionMS) == min &&
                       HIWORD(fixed->dwFileVersionLS) == pat,
                   "La versión del instalador debe ser la de project(VERSION)");
    }

    // Lo que se empaqueta: el componente app (sin portable.flag) con las licencias completas.
    TEST_CHECK(fs::is_regular_file(stage / "CafeteriaITCH.exe", ec), "El paquete lleva CafeteriaITCH.exe");
    TEST_CHECK(!fs::exists(stage / "portable.flag", ec), "El instalador NO lleva portable.flag");
    for (const char* lic : {"wxWidgets-licence.txt", "wxWidgets-lgpl.txt", "GCC-COPYING.RUNTIME.txt", "GCC-COPYING3.txt",
                            "winpthreads-COPYING.txt", "SQLite-aviso.txt", "LEEME-licencias.txt"}) {
        TEST_CHECK(fs::is_regular_file(stage / "licencias" / lic, ec) && fs::file_size(stage / "licencias" / lic, ec) > 0,
                   std::string("Licencia incluida y no vacía: ") + lic);
    }
    const std::string readme = read_text(stage / "licencias" / "LEEME-licencias.txt");
    TEST_CHECK(readme.find("PENDIENTE") == std::string::npos, "El aviso de licencias no debe tener PENDIENTE");

    std::cout << "Pruebas del instalador (B10) correctas. TEST_CHECK ejecutados: " << g_checks << std::endl;
    cleanup_and_exit(0);
}
