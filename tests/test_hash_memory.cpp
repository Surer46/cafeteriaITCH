// Medición B9: memoria pico y tiempo del SHA-256 de un archivo grande, método anterior frente al nuevo.
// Uso: test_hash_memory <old|exact|new> <MB> [bloque_en_bytes]
//   old    lectura completa con istreambuf_iterator + una pasada (B6, como estaba)
//   exact  lectura completa con tamaño exacto + una pasada (B8)
//   new    por bloques con fingerprint_file (B9); bloque por omisión 1 MiB
// Cada modo debe correr en un proceso aparte: el pico de memoria es del proceso entero.
// Sin argumentos (CTest) hace una comprobación de humo con 8 MB: los tres métodos dan el mismo hash.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <psapi.h>

#include "app/crypto/password_hasher.hpp"
#include "app/services/file_fingerprint.hpp"

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

static fs::path g_file;
static void cleanup_and_exit(int code) {
    std::error_code ec;
    if (!g_file.empty()) fs::remove(g_file, ec);
    std::exit(code);
}

static std::uint64_t peak_working_set() {
    PROCESS_MEMORY_COUNTERS pmc{};
    pmc.cb = sizeof(pmc);
    GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
    return static_cast<std::uint64_t>(pmc.PeakWorkingSetSize);
}

static std::string hash_old(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    return app::crypto::PasswordHasher::to_hex(app::crypto::PasswordHasher::sha256(bytes.data(), bytes.size()));
}

static std::string hash_exact(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(fs::file_size(p)));
    in.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    return app::crypto::PasswordHasher::to_hex(app::crypto::PasswordHasher::sha256(bytes.data(), bytes.size()));
}

static std::string hash_new(const fs::path& p, std::size_t block) {
    app::services::FileFingerprint fp;
    if (!app::services::fingerprint_file(p, fp, block)) return "ERROR";
    return fp.sha256;
}

int main(int argc, char** argv) {
    const bool smoke = argc < 3;
    const std::string mode = smoke ? "smoke" : argv[1];
    const std::uint64_t mb = smoke ? 8 : std::strtoull(argv[2], nullptr, 10);
    const std::size_t block = (argc > 3) ? static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10))
                                         : app::services::kHashBlockBytes;

    std::error_code ec;
    g_file = fs::temp_directory_path(ec) / ("cafeteria_b9_hash_" + std::to_string(GetCurrentProcessId()) + ".bin");
    {
        // Datos pseudoaleatorios escritos en trozos de 1 MiB (no se arma el archivo en memoria).
        std::ofstream out(g_file, std::ios::binary);
        std::vector<char> chunk(1024 * 1024);
        std::uint32_t x = 12345;
        for (std::uint64_t i = 0; i < mb; ++i) {
            for (auto& c : chunk) { x = x * 1664525u + 1013904223u; c = static_cast<char>(x >> 24); }
            out.write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        }
    }
    const std::uint64_t before = peak_working_set();
    const auto t0 = Clock::now();

    if (smoke) {
        const std::string a = hash_old(g_file), b = hash_exact(g_file), c = hash_new(g_file, block), d = hash_new(g_file, 777);
        if (a != b || a != c || a != d || a.size() != 64) {
            std::cerr << "[FALLO DE MEDICIÓN] Los métodos dan hashes distintos\n";
            cleanup_and_exit(1);
        }
        std::cout << "Hash de 8 MB igual con los 4 métodos. TEST_CHECK ejecutados: 1" << std::endl;
        cleanup_and_exit(0);
    }

    std::string hash;
    if (mode == "old") hash = hash_old(g_file);
    else if (mode == "exact") hash = hash_exact(g_file);
    else if (mode == "new") hash = hash_new(g_file, block);
    else { std::cerr << "modo desconocido\n"; cleanup_and_exit(2); }

    const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - t0).count();
    const std::uint64_t peak = peak_working_set();
    std::cout << "modo=" << mode << " archivo=" << mb << " MB bloque=" << (mode == "new" ? block : 0)
              << " tiempo=" << ms << " ms pico_antes_del_hash=" << before / (1024 * 1024)
              << " MB pico_total=" << peak / (1024 * 1024) << " MB hash=" << hash.substr(0, 16) << "..." << std::endl;
    cleanup_and_exit(0);
}
