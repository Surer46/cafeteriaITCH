// Pruebas B9 / punto 2: SHA-256 incremental, hash de archivos por bloques y compatibilidad con los
// hashes ya registrados por B6.
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "app/crypto/password_hasher.hpp"
#include "app/crypto/sha256.hpp"
#include "app/services/file_fingerprint.hpp"
#include "app/services/sqlite_account_service.hpp"
#include "app/services/sqlite_backup_service.hpp"
#include "app/storage/migration_runner.hpp"
#include "app/storage/sqlite_database.hpp"

namespace fs = std::filesystem;
using namespace app::crypto;
using namespace app::services;
using namespace app::storage;

namespace {
fs::path g_root;
int g_checks = 0;
void cleanup_and_exit(int code) {
    std::error_code ec;
    if (!g_root.empty()) fs::remove_all(g_root, ec);
    std::exit(code);
}
} // namespace

#define TEST_CHECK(condition, message) \
    do { \
        ++g_checks; \
        if (!(condition)) { \
            std::cerr << "\n[FALLO DE PRUEBA SHA-256] " << (message) \
                      << "\n  Condición: " << #condition \
                      << "\n  Archivo:   " << __FILE__ \
                      << "\n  Línea:     " << __LINE__ << "\n" << std::endl; \
            cleanup_and_exit(1); \
        } \
    } while (0)

namespace {

std::string hex_of(const std::vector<std::uint8_t>& v) { return PasswordHasher::to_hex(v); }

std::string stream_hex(const std::string& data, std::size_t chunk) {
    Sha256 h;
    for (std::size_t i = 0; i < data.size(); i += chunk) {
        const std::size_t n = std::min(chunk, data.size() - i);
        h.update(reinterpret_cast<const std::uint8_t*>(data.data()) + i, n);
    }
    return hex_of(h.finalize());
}

std::string one_shot_hex(const std::string& data) {
    return hex_of(PasswordHasher::sha256(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
}

std::string pseudo_random(std::size_t n, std::uint32_t seed) {
    std::string s(n, '\0');
    std::uint32_t x = seed;
    for (std::size_t i = 0; i < n; ++i) {
        x = x * 1664525u + 1013904223u;
        s[i] = static_cast<char>(x >> 24);
    }
    return s;
}

void write_file(const fs::path& p, const std::string& data) {
    std::ofstream out(p, std::ios::binary);
    out.write(data.data(), static_cast<std::streamsize>(data.size()));
}

std::string scalar_text(SqliteDatabase& db, const std::string& sql) {
    auto prep = db.prepare(sql);
    TEST_CHECK(prep.is_success(), "Debe preparar: " + sql);
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    TEST_CHECK(step.is_success() && step.value(), "Debe devolver fila: " + sql);
    return stmt.get_text(0);
}

} // namespace

static void test_official_vectors() {
    std::cout << "[TEST] B9: vectores oficiales de SHA-256..." << std::endl;
    struct V { std::string data; const char* hex; };
    const V vectors[] = {
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
        {std::string(1000000, 'a'), "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0"},
    };
    for (const auto& v : vectors) {
        const std::string label = "longitud " + std::to_string(v.data.size());
        TEST_CHECK(stream_hex(v.data, v.data.size() == 0 ? 1 : v.data.size()) == v.hex, "Vector oficial de una vez: " + label);
        TEST_CHECK(stream_hex(v.data, 1) == v.hex || v.data.size() > 100000, "Vector oficial byte a byte: " + label);
        TEST_CHECK(stream_hex(v.data, 7) == v.hex, "Vector oficial en trozos de 7 bytes: " + label);
        TEST_CHECK(stream_hex(v.data, 64) == v.hex, "Vector oficial en trozos de 64 bytes: " + label);
        TEST_CHECK(one_shot_hex(v.data) == v.hex, "La función de una pasada (PBKDF2) da el mismo vector: " + label);
    }
    // finalize() repetido da el mismo resultado y update() posterior se ignora.
    Sha256 h;
    h.update(reinterpret_cast<const std::uint8_t*>("abc"), 3);
    const auto d1 = h.finalize();
    h.update(reinterpret_cast<const std::uint8_t*>("x"), 1);
    TEST_CHECK(hex_of(d1) == hex_of(h.finalize()), "finalize() repetido no cambia el resultado");
}

static void test_equivalent_to_one_shot() {
    std::cout << "[TEST] B9: incremental == una pasada (fronteras de bloque y trozos raros)..." << std::endl;
    for (std::size_t len : {0u, 1u, 55u, 56u, 57u, 63u, 64u, 65u, 119u, 120u, 121u, 127u, 128u, 129u, 1000u, 4097u, 100003u}) {
        const std::string data = pseudo_random(len, static_cast<std::uint32_t>(len) + 7);
        const std::string expected = one_shot_hex(data);
        for (std::size_t chunk : {1u, 3u, 13u, 63u, 64u, 65u, 100u, 1023u, 4096u}) {
            TEST_CHECK(stream_hex(data, chunk) == expected,
                       "Longitud " + std::to_string(len) + " en trozos de " + std::to_string(chunk));
        }
    }
}

static void test_file_in_odd_blocks() {
    std::cout << "[TEST] B9: archivo partido en bloques de tamaños raros..." << std::endl;
    const std::string small = pseudo_random(200001, 99);
    const std::string big = pseudo_random(3000017, 5);
    const fs::path f_small = g_root / "pequeno ñ.bin";
    const fs::path f_big = g_root / "grande ñ.bin";
    write_file(f_small, small);
    write_file(f_big, big);

    for (std::size_t block : {1u, 2u, 3u, 63u, 64u, 65u, 1000u, 4093u, 65536u}) {
        FileFingerprint fp;
        // Con bloques minúsculos se usa el archivo pequeño para no tardar.
        const bool tiny = block < 64;
        const std::string& data = tiny ? small : big;
        TEST_CHECK(fingerprint_file(tiny ? f_small : f_big, fp, block), "fingerprint_file con bloque " + std::to_string(block));
        TEST_CHECK(fp.size == static_cast<std::int64_t>(data.size()), "Tamaño con bloque " + std::to_string(block));
        TEST_CHECK(fp.sha256 == one_shot_hex(data), "Mismo hash que de una pasada con bloque " + std::to_string(block));
    }
    for (std::size_t block : {kHashBlockBytes, std::size_t(4) * 1024 * 1024, std::size_t(7000000)}) {
        FileFingerprint fp;
        TEST_CHECK(fingerprint_file(f_big, fp, block), "fingerprint_file con bloque grande");
        TEST_CHECK(fp.sha256 == one_shot_hex(big) && fp.size == static_cast<std::int64_t>(big.size()),
                   "Hash igual con bloque " + std::to_string(block));
    }
    // Bloque 0 = el de por omisión.
    {
        FileFingerprint fp;
        TEST_CHECK(fingerprint_file(f_big, fp, 0) && fp.sha256 == one_shot_hex(big), "Bloque 0 usa el valor por omisión");
    }
    // Avance monótono que termina en el total.
    {
        FileFingerprint fp;
        std::uint64_t last = 0, total_seen = 0;
        bool monotonic = true;
        int calls = 0;
        TEST_CHECK(fingerprint_file(f_big, fp, 100000, [&](std::uint64_t done, std::uint64_t total) {
            if (done <= last) monotonic = false;
            last = done;
            total_seen = total;
            ++calls;
        }), "fingerprint_file con avance");
        TEST_CHECK(monotonic && last == big.size() && total_seen == big.size() && calls == 31, "Avance: " + std::to_string(calls) + " avisos");
    }
    // Vacío, inexistente y carpeta.
    {
        const fs::path empty = g_root / "vacio.bin";
        write_file(empty, "");
        FileFingerprint fp;
        TEST_CHECK(fingerprint_file(empty, fp) && fp.size == 0 &&
                       fp.sha256 == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "Archivo vacío");
        TEST_CHECK(!fingerprint_file(g_root / "no existe.bin", fp), "Inexistente = false");
        TEST_CHECK(!fingerprint_file(g_root, fp), "Una carpeta = false");
    }
}

// Los hashes que B6 ya había registrado (calculados con la función de una pasada sobre el archivo
// completo) deben seguir coincidiendo con el cálculo por bloques: la limpieza los trata como "sin cambios".
static void test_previously_registered_hashes_still_match() {
    std::cout << "[TEST] B9: los hashes registrados por B6 siguen coincidiendo..." << std::endl;
    const fs::path dir = g_root / "b6";
    fs::create_directories(dir);
    SqliteDatabase db;
    TEST_CHECK(db.open(dir / "cafeteria.db").is_success(), "Abrir base");
    MigrationRunner runner(db);
    TEST_CHECK(runner.run_pending_migrations_from_dir(fs::u8path(SOURCE_MIGRATIONS_DIR)).is_success(), "Migraciones");
    auto account = std::make_shared<SqliteAccountService>(db);
    TEST_CHECK(account->create_initial_owner("dueno_h", "Dueño H", "Clave-Dueno-B9-1", "d@cafeteria-itch.edu.mx").is_success(), "Dueño");
    SqliteBackupService backup(db, account, dir.u8string(), SOURCE_MIGRATIONS_DIR);

    std::vector<fs::path> files;
    for (int i = 1; i <= 3; ++i) {
        const fs::path f = dir / ("respaldo " + std::to_string(i) + ".db");
        TEST_CHECK(backup.trigger_local_backup(f.u8string()).is_success(), "Respaldo " + std::to_string(i));
        files.push_back(f);
    }
    // Se sustituye el hash registrado por el que habría calculado B6: archivo completo en memoria + una pasada.
    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        const std::string old_style = hex_of(PasswordHasher::sha256(bytes.data(), bytes.size()));
        const std::string registered = scalar_text(db, "SELECT sha256 FROM backup_files WHERE path LIKE '%" +
                                                           f.filename().u8string() + "';");
        TEST_CHECK(registered == old_style, "El hash que ahora se registra es el de siempre: " + f.filename().u8string());
        auto upd = db.prepare("UPDATE backup_files SET sha256 = ?, size_bytes = ? WHERE path LIKE ?;");
        TEST_CHECK(upd.is_success(), "Actualizar registro");
        auto stmt = std::move(upd.value());
        stmt.bind_text(1, old_style);
        stmt.bind_int64(2, static_cast<std::int64_t>(bytes.size()));
        stmt.bind_text(3, "%" + f.filename().u8string());
        TEST_CHECK(stmt.step().is_success(), "Actualizar fila");
    }
    // La limpieza (que ahora compara por bloques) no los ve como cambiados y borra los antiguos.
    TEST_CHECK(backup.set_retention_policy(app::contracts::BackupRetentionPolicyDto{1, 0}).is_success(), "Política 1/0");
    auto preview = backup.preview_backup_cleanup();
    TEST_CHECK(preview.is_success(), "Vista previa");
    TEST_CHECK(preview.value().changed.empty(), "Ningún respaldo registrado aparece como cambiado");
    TEST_CHECK(preview.value().to_delete.size() == 2, "Se borrarían los 2 antiguos");
    auto clean = backup.cleanup_old_backups();
    TEST_CHECK(clean.is_success() && clean.value().to_delete.size() == 2 && clean.value().failed.empty(), "Limpieza");
    TEST_CHECK(!fs::exists(files[0]) && !fs::exists(files[1]) && fs::exists(files[2]), "Quedó solo el más reciente");
    db.close();
}

int main() {
    std::error_code ec;
    g_root = fs::temp_directory_path(ec) /
             fs::u8path(u8"cafeteria b9 sha256 ñ " +
                        std::to_string(static_cast<unsigned long long>(std::chrono::steady_clock::now().time_since_epoch().count())));
    fs::remove_all(g_root, ec);
    fs::create_directories(g_root, ec);

    test_official_vectors();
    test_equivalent_to_one_shot();
    test_file_in_odd_blocks();
    test_previously_registered_hashes_still_match();

    std::cout << "Pruebas de SHA-256 incremental (B9) correctas. TEST_CHECK ejecutados: " << g_checks << std::endl;
    cleanup_and_exit(0);
}
