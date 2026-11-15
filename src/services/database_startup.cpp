#include "app/services/database_startup.hpp"

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iterator>
#include <set>
#include <vector>

#include "app/domain/clock.hpp"
#include "app/services/file_fingerprint.hpp"
#include "app/services/identifiers.hpp"
#include "app/storage/migration_runner.hpp"

namespace app::services {

namespace fs = std::filesystem;
using Clock = std::chrono::steady_clock;

namespace {

std::int64_t elapsed_ms(Clock::time_point since) {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - since).count();
}

void remove_with_sidecars(const fs::path& p) {
    std::error_code ec;
    fs::remove(p, ec);
    fs::remove(fs::path(p.u8string() + "-wal"), ec);
    fs::remove(fs::path(p.u8string() + "-shm"), ec);
}

std::string stamp_for_filename(std::chrono::system_clock::time_point tp) {
    std::time_t t = std::chrono::system_clock::to_time_t(tp);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &t);
#else
    localtime_r(&t, &tm);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y%m%d-%H%M%S", &tm);
    return buf;
}

// Primera fila (texto) de una consulta; false si no se pudo preparar o ejecutar.
bool first_text(storage::SqliteDatabase& db, const std::string& sql, std::string& out) {
    auto prep = db.prepare(sql);
    if (!prep.is_success()) return false;
    auto stmt = std::move(prep.value());
    auto step = stmt.step();
    if (!step.is_success() || !step.value()) return false;
    out = stmt.get_text(0);
    return true;
}

// PRAGMA quick_check / integrity_check: "ok" en la primera fila significa sin problemas.
bool pragma_check_ok(storage::SqliteDatabase& db, const char* pragma_sql) {
    std::string first;
    if (!first_text(db, pragma_sql, first)) return false;
    return first == "ok";
}

bool table_exists(storage::SqliteDatabase& db, const std::string& name) {
    std::string out;
    return first_text(db, "SELECT name FROM sqlite_master WHERE type='table' AND name='" + name + "';", out);
}

// Misma regla que MigrationRunner: .sql cuyos 3 primeros caracteres son un número.
std::set<int> migration_file_versions(const fs::path& dir) {
    std::set<int> versions;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        if (!entry.is_regular_file() || entry.path().extension() != ".sql") continue;
        const std::string name = entry.path().filename().string();
        if (name.size() < 3) continue;
        try {
            versions.insert(std::stoi(name.substr(0, 3)));
        } catch (...) {
        }
    }
    return versions;
}

std::set<int> applied_versions(storage::SqliteDatabase& db) {
    std::set<int> versions;
    if (!table_exists(db, "schema_migrations")) return versions;
    auto prep = db.prepare("SELECT version FROM schema_migrations;");
    if (!prep.is_success()) return versions;
    auto stmt = std::move(prep.value());
    while (true) {
        auto step = stmt.step();
        if (!step.is_success() || !step.value()) break;
        versions.insert(static_cast<int>(stmt.get_int64(0)));
    }
    return versions;
}

int max_version(const std::set<int>& v) { return v.empty() ? 0 : *v.rbegin(); }

std::uint64_t file_size_or_zero(const fs::path& p) {
    std::error_code ec;
    auto s = fs::file_size(p, ec);
    return ec ? 0 : static_cast<std::uint64_t>(s);
}

std::string megabytes_text(std::uint64_t bytes) {
    // Redondeo hacia arriba a MB enteros: es un aviso, no un importe.
    const std::uint64_t mb = (bytes + 1024 * 1024 - 1) / (1024 * 1024);
    return std::to_string(mb) + " MB";
}

StartupResult fail(StartupResult r, StartupError kind, std::string message) {
    r.ok = false;
    r.error = kind;
    r.message_utf8 = std::move(message);
    return r;
}

// Avisos de progreso: los cambios de fase siempre salen; el avance dentro de una fase, a lo sumo uno
// cada `min_interval_ms`. Sin on_progress no hace nada.
class Emitter {
public:
    explicit Emitter(const StartupOptions& opt) : opt_(opt) {}
    bool enabled() const { return static_cast<bool>(opt_.on_progress); }

    void phase(StartupPhase ph, int percent, const std::string& message, int step = 0, int total = 0) {
        if (!enabled()) return;
        last_.phase = ph;
        last_.percent = percent;
        last_.step = step;
        last_.total_steps = total;
        last_.message_utf8 = message;
        send();
    }
    // Avance dentro de la fase actual (cambia solo el porcentaje).
    void percent(int pct) {
        if (!enabled()) return;
        last_.percent = pct;
        throttled();
    }
    // Latido periódico durante operaciones largas de SQLite: repite el último aviso.
    void tick() {
        if (!enabled()) return;
        throttled();
    }

private:
    void send() {
        last_time_ = Clock::now();
        try { opt_.on_progress(last_); } catch (...) {}
    }
    void throttled() {
        const auto now = Clock::now();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - last_time_).count() >= opt_.progress_min_interval_ms) {
            send();
        }
    }

    const StartupOptions& opt_;
    StartupProgress last_;
    Clock::time_point last_time_{};
};

// Quita SIEMPRE el manejador de progreso de SQLite al salir (cualquier camino, también los de error).
struct ProgressHandlerGuard {
    storage::SqliteDatabase& db;
    ~ProgressHandlerGuard() { db.clear_progress_callback(); }
};

// Respaldo consistente y verificado: instantánea por tandas en un temporal, archivo único, integridad
// y renombrado.
contracts::OperationResult create_verified_snapshot(storage::SqliteDatabase& db, const fs::path& dest, Emitter& em) {
    std::error_code ec;
    fs::create_directories(dest.parent_path(), ec);
    const fs::path tmp = fs::path(dest.u8string() + ".tmp");
    remove_with_sidecars(tmp);

    em.phase(StartupPhase::BackupCopy, 0, "Guardando una copia de seguridad de sus datos…");
    auto res = db.backup_to(tmp, [&](int remaining, int total) {
        em.percent(total > 0 ? static_cast<int>((static_cast<long long>(total - remaining) * 100) / total) : 100);
    });
    if (!res.is_success()) {
        remove_with_sidecars(tmp);
        return res;
    }
    auto single = storage::SqliteDatabase::make_single_file(tmp);
    if (!single.is_success()) {
        remove_with_sidecars(tmp);
        return contracts::OperationResult::fail(contracts::ErrorCode::BackupFailure, single.error_message());
    }
    em.phase(StartupPhase::BackupVerify, -1, "Comprobando que la copia de seguridad quedó bien…");
    auto info = storage::SqliteDatabase::inspect_file(tmp, [&] { em.tick(); });
    if (!info.is_success() || !info.value().integrity_ok) {
        remove_with_sidecars(tmp);
        return contracts::OperationResult::fail(contracts::ErrorCode::BackupFailure,
                                                "El respaldo creado no pasó la verificación y fue eliminado");
    }
    fs::rename(tmp, dest, ec);
    if (ec) {
        remove_with_sidecars(tmp);
        return contracts::OperationResult::fail(contracts::ErrorCode::BackupFailure,
                                                "No se pudo colocar el respaldo en su destino");
    }
    return contracts::OperationResult::ok();
}

bool register_pre_migration_backup(storage::SqliteDatabase& db, const fs::path& backup,
                                   std::chrono::system_clock::time_point now, Emitter& em) {
    if (!table_exists(db, "backup_files")) return false;
    std::error_code ec;
    fs::path abs = fs::absolute(backup, ec);
    if (ec) abs = backup;
    abs = abs.lexically_normal();

    em.phase(StartupPhase::BackupRegister, 0, "Registrando la copia de seguridad…");
    FileFingerprint fp;
    if (!fingerprint_file(abs, fp, kHashBlockBytes, [&](std::uint64_t done, std::uint64_t total) {
            em.percent(total > 0 ? static_cast<int>((done * 100) / total) : 100);
        })) {
        return false;
    }
    const auto ts = domain::timestamp_from(now);

    auto prep = db.prepare(
        "INSERT INTO backup_files (id, kind, path, size_bytes, sha256, created_at, created_at_utc) "
        "VALUES (?, 'PRE_MIGRATION', ?, ?, ?, ?, ?);");
    if (!prep.is_success()) return false;
    auto stmt = std::move(prep.value());
    stmt.bind_text(1, generate_id("bkf"));
    stmt.bind_text(2, abs.u8string());
    stmt.bind_int64(3, fp.size);
    stmt.bind_text(4, fp.sha256);
    stmt.bind_text(5, ts.local);
    stmt.bind_text(6, ts.utc);
    auto step = stmt.step();
    return step.is_success();
}

// Activación única del respaldo automático (B12): al pasar por la migración 016 se activa si nadie lo apagó
// nunca (ningún evento BACKUP_DISABLED en la bitácora); si el Dueño lo apagó alguna vez se respeta su decisión.
// El evento de bitácora se escribe solo cuando lo activa. Un fallo aquí no impide arrancar.
void activate_auto_backup_once(storage::SqliteDatabase& db) {
    if (!db.begin_transaction().is_success()) return;
    const bool ok =
        db.execute(
            "INSERT INTO audit_events (id, event_type, user_id, details, created_at, created_at_utc) "
            "SELECT 'audit-' || lower(hex(randomblob(16))), 'BACKUP_AUTO_ACTIVADO', NULL, "
            "'El respaldo automático al cerrar turno se activó con la actualización del programa', "
            "datetime('now', 'localtime'), strftime('%Y-%m-%dT%H:%M:%fZ', 'now') "
            "WHERE EXISTS (SELECT 1 FROM backup_state WHERE id = 1 AND is_enabled = 0) "
            "AND NOT EXISTS (SELECT 1 FROM audit_events WHERE event_type = 'BACKUP_DISABLED');").is_success() &&
        db.execute(
            "UPDATE backup_state SET is_enabled = 1 WHERE id = 1 AND is_enabled = 0 "
            "AND NOT EXISTS (SELECT 1 FROM audit_events WHERE event_type = 'BACKUP_DISABLED');").is_success();
    if (ok) {
        db.commit_transaction();
    } else {
        db.rollback_transaction();
    }
}

} // namespace

StartupResult open_and_migrate_database(storage::SqliteDatabase& db, const StartupOptions& opt) {
    const auto started = Clock::now();
    StartupResult r;
    auto finish = [&](StartupResult res) {
        res.total_ms = elapsed_ms(started);
        return res;
    };
    const std::string db_text = opt.db_path.u8string();
    Emitter em(opt);

    std::error_code ec;
    if (!fs::is_directory(opt.migrations_dir, ec)) {
        return finish(fail(r, StartupError::MigrationsFolderMissing,
            "No se encontró el directorio de migraciones del sistema.\n"
            "La aplicación requiere sus archivos de esquema para operar."));
    }
    const std::set<int> file_versions = migration_file_versions(opt.migrations_dir);

    const bool file_had_content = file_size_or_zero(opt.db_path) > 0;
    auto open_res = db.open(opt.db_path);
    if (!open_res.is_success()) {
        db.close();
        return finish(fail(r, StartupError::OpenFailed,
            "No se pudo abrir la base de datos de la cafetería.\n\nArchivo:\n" + db_text +
            "\n\nLlame a quien instaló el sistema."));
    }

    // El manejador de progreso de SQLite vive solo durante esta función y se quita siempre al salir.
    // Solo se instala si hay quien escuche; nunca interrumpe ninguna operación (devuelve siempre 0).
    ProgressHandlerGuard handler_guard{db};
    if (em.enabled()) db.set_progress_callback([&em] { em.tick(); });

    // Versiones aplicadas (si el archivo es ilegible quedan vacías y lo detecta la comprobación de integridad).
    const std::set<int> applied = file_had_content ? applied_versions(db) : std::set<int>{};
    r.schema_version_before = max_version(applied);
    const bool has_data = !applied.empty();
    const int newest_in_package = max_version(file_versions);
    const bool newer_than_program = has_data && !file_versions.empty() && r.schema_version_before > newest_in_package;
    std::vector<int> pending;
    for (int v : file_versions) {
        if (!applied.count(v)) pending.push_back(v);
    }
    const bool will_update = has_data && !pending.empty() && !newer_than_program;
    if (will_update) em.phase(StartupPhase::UpdateStarting, -1, "Preparando la actualización de los datos…");

    // Base existente: comprobación rápida de integridad antes de tocar nada.
    if (file_had_content) {
        em.phase(StartupPhase::QuickCheck, -1, "Revisando que los datos estén en buen estado…");
        const auto t0 = Clock::now();
        const bool quick_ok = pragma_check_ok(db, "PRAGMA quick_check;");
        r.quick_check_ms = elapsed_ms(t0);
        if (!quick_ok) {
            db.close();
            return finish(fail(r, StartupError::DatabaseDamaged,
                "La base de datos de la cafetería está dañada y no se puede abrir para vender.\n\nArchivo:\n" +
                db_text +
                "\n\nNo se hizo ningún cambio en ella. Llame a quien instaló el sistema: hay que restaurar el último respaldo."));
        }
    }

    // Datos de una versión más nueva que este programa: no se abre para vender y no se toca nada.
    if (newer_than_program) {
        db.close();
        return finish(fail(r, StartupError::DatabaseNewerThanProgram,
            "El programa instalado es más antiguo que los datos de la cafetería.\n\n"
            "Estos datos se usaron con una versión más nueva del programa, y esta versión no puede leerlos con "
            "seguridad: podría estropearlos.\n\nArchivo:\n" + db_text +
            "\n\nNo se hizo ningún cambio. Instale la versión correcta del programa. Llame a quien instaló el sistema."));
    }

    const auto now_tp = opt.now ? opt.now() : std::chrono::system_clock::now();

    // Base con datos y migraciones pendientes: respaldo verificado ANTES de la primera migración.
    if (will_update) {
        em.phase(StartupPhase::FullCheck, -1,
                 "Revisando a fondo los datos antes de actualizar. Esto puede tardar varios minutos…");
        const auto t0 = Clock::now();
        const bool full_ok = pragma_check_ok(db, "PRAGMA integrity_check;");
        r.full_check_ms = elapsed_ms(t0);
        if (!full_ok) {
            db.close();
            return finish(fail(r, StartupError::DatabaseDamaged,
                "La base de datos de la cafetería está dañada y no se puede actualizar.\n\nArchivo:\n" + db_text +
                "\n\nNo se hizo ningún cambio en ella. Llame a quien instaló el sistema: hay que restaurar el último respaldo."));
        }

        const fs::path backup_dir = opt.backups_dir / "pre-migracion";
        const std::uint64_t db_size = file_size_or_zero(opt.db_path) + file_size_or_zero(fs::path(db_text + "-wal"));
        const std::uint64_t margin = std::max<std::uint64_t>(db_size / 10, 32ull * 1024 * 1024);
        const std::uint64_t needed = db_size + margin;
        fs::create_directories(backup_dir, ec);
        if (!fs::is_directory(backup_dir, ec)) {
            db.close();
            return finish(fail(r, StartupError::BackupFailed,
                "No se pudo crear la carpeta para la copia de seguridad previa a la actualización, así que no se "
                "actualizó nada.\n\nCarpeta:\n" + backup_dir.u8string() +
                "\n\nSus datos no cambiaron. Llame a quien instaló el sistema."));
        }
        const std::uint64_t available = opt.free_space_bytes ? opt.free_space_bytes(backup_dir) : [&] {
            std::error_code e2;
            auto info = fs::space(backup_dir, e2);
            return e2 ? std::uint64_t(0) : static_cast<std::uint64_t>(info.available);
        }();
        if (available < needed) {
            db.close();
            return finish(fail(r, StartupError::NotEnoughSpace,
                "No hay espacio suficiente en el disco para guardar una copia de seguridad antes de actualizar "
                "los datos de la cafetería.\n\nSe necesitan al menos " + megabytes_text(needed) +
                " libres en:\n" + backup_dir.u8string() +
                "\n\nLibere espacio y vuelva a abrir el programa. No se hizo ningún cambio en sus datos."));
        }

        const std::string base_name = "cafeteria-v" + std::to_string(r.schema_version_before) + "-a-v" +
                                      std::to_string(newest_in_package) + "-" + stamp_for_filename(now_tp);
        fs::path dest = backup_dir / (base_name + ".db");
        for (int n = 2; fs::exists(dest, ec); ++n) {
            dest = backup_dir / (base_name + "-" + std::to_string(n) + ".db");
        }
        const auto tb = Clock::now();
        auto snap = create_verified_snapshot(db, dest, em);
        r.backup_ms = elapsed_ms(tb);
        if (!snap.is_success()) {
            db.close();
            return finish(fail(r, StartupError::BackupFailed,
                "No se pudo crear la copia de seguridad previa a la actualización, así que no se actualizó nada.\n\n"
                "Base de datos:\n" + db_text +
                "\n\nSus datos no cambiaron. Llame a quien instaló el sistema."));
        }
        r.backup_created = true;
        r.backup_path = dest;
    }

    // Migraciones (cada una en su propia transacción).
    const auto tm = Clock::now();
    storage::MigrationRunner runner(db);
    auto mig = runner.run_pending_migrations_from_dir(opt.migrations_dir, [&](int index, int total, int) {
        em.phase(StartupPhase::Migrate, total > 0 ? ((index - 1) * 100) / total : -1,
                 "Actualizando los datos (paso " + std::to_string(index) + " de " + std::to_string(total) + ")…",
                 index, total);
    });
    r.migrate_ms = elapsed_ms(tm);
    const std::set<int> applied_after = applied_versions(db);
    r.schema_version_after = max_version(applied_after);
    r.migrations_applied = static_cast<int>(applied_after.size() - applied.size());

    if (!mig.is_success()) {
        int failing = 0;
        for (int v : file_versions) {
            if (!applied_after.count(v)) { failing = v; break; }
        }
        db.close();
        std::string msg =
            "La actualización de los datos de la cafetería falló y se detuvo en el paso hacia la versión " +
            std::to_string(failing) + ".\n\n";
        if (r.backup_created) {
            msg += "Los pasos anteriores ya se habían aplicado: la base quedó en la versión " +
                   std::to_string(r.schema_version_after) + ". El programa no se abrirá.\n\n"
                   "La copia de seguridad hecha justo antes de actualizar, con todos los datos tal como estaban, está en:\n" +
                   r.backup_path.u8string() + "\n\nLlame a quien instaló el sistema y no borre ese archivo.";
        } else {
            msg += "Era una base nueva, sin datos que respaldar. El programa no se abrirá.\n\nBase de datos:\n" + db_text +
                   "\n\nLlame a quien instaló el sistema.";
        }
        return finish(fail(r, StartupError::MigrationFailed, msg));
    }

    // Migración correcta: la 016 (respaldo automático) activa el respaldo una sola vez.
    if (r.schema_version_before < 16 && r.schema_version_after >= 16) {
        activate_auto_backup_once(db);
    }

    // Migración correcta: ahora sí se puede registrar el respaldo previo.
    if (r.backup_created) {
        r.backup_registered = register_pre_migration_backup(db, r.backup_path, now_tp, em);
    }

    em.phase(StartupPhase::Finished, 100, "Listo.");
    r.ok = true;
    return finish(r);
}

} // namespace app::services
