-- Migración 016: respaldo automático al cerrar turno, segunda carpeta (USB) y lista de respaldos.
-- 1. backup_files acepta el tipo AUTO y registra el destino de cada copia (DATA = carpeta de datos del
--    programa; SECONDARY = la segunda carpeta que configuró el Dueño). SQLite no permite modificar una
--    restricción CHECK, así que la tabla se reconstruye conservando TODAS las filas (con su rowid, del que
--    depende el orden de la limpieza), los mismos índices y las mismas restricciones. Las filas existentes
--    quedan con destination = 'DATA'.
-- 2. backup_state guarda la segunda carpeta y el resultado del último respaldo automático.
-- 3. La activación única del respaldo automático NO se hace aquí (una migración no debe añadir filas a la
--    bitácora de los datos existentes): la hace database_startup.cpp justo después de aplicar esta migración,
--    solo si nadie lo apagó nunca (ningún evento BACKUP_DISABLED) y escribiendo el evento solo cuando lo activa.
-- Se aplica una sola vez (schema_migrations).

CREATE TABLE backup_files_new (
    id TEXT PRIMARY KEY,
    kind TEXT NOT NULL CHECK(kind IN ('LOCAL', 'PRE_RESTORE', 'PRE_MIGRATION', 'AUTO')),
    path TEXT NOT NULL,
    size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0),
    sha256 TEXT NOT NULL,
    created_at TEXT NOT NULL,
    created_at_utc TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'ACTIVE' CHECK(status IN ('ACTIVE', 'DELETED', 'CHANGED', 'SUPERSEDED')),
    status_at TEXT,
    last_error TEXT NOT NULL DEFAULT '',
    destination TEXT NOT NULL DEFAULT 'DATA' CHECK(destination IN ('DATA', 'SECONDARY'))
);

INSERT INTO backup_files_new (rowid, id, kind, path, size_bytes, sha256, created_at, created_at_utc, status, status_at, last_error, destination)
SELECT rowid, id, kind, path, size_bytes, sha256, created_at, created_at_utc, status, status_at, last_error, 'DATA'
FROM backup_files
ORDER BY rowid;

DROP TABLE backup_files;
ALTER TABLE backup_files_new RENAME TO backup_files;

CREATE INDEX idx_backup_files_kind_created ON backup_files(kind, status, created_at_utc);
CREATE INDEX idx_backup_files_path ON backup_files(path);
CREATE INDEX idx_backup_files_kind_dest_created ON backup_files(kind, destination, status, created_at_utc);

ALTER TABLE backup_state ADD COLUMN secondary_dir TEXT NOT NULL DEFAULT '';
ALTER TABLE backup_state ADD COLUMN auto_state TEXT NOT NULL DEFAULT 'IDLE'
    CHECK(auto_state IN ('IDLE', 'WAITING', 'RUNNING', 'DONE', 'FAILED', 'NOT_DONE'));
ALTER TABLE backup_state ADD COLUMN auto_started_at TEXT;
ALTER TABLE backup_state ADD COLUMN auto_finished_at TEXT;
ALTER TABLE backup_state ADD COLUMN auto_shift_id TEXT NOT NULL DEFAULT '';
ALTER TABLE backup_state ADD COLUMN auto_local_ok INTEGER NOT NULL DEFAULT 0 CHECK(auto_local_ok IN (0, 1));
ALTER TABLE backup_state ADD COLUMN auto_local_file TEXT NOT NULL DEFAULT '';
ALTER TABLE backup_state ADD COLUMN auto_secondary_status TEXT NOT NULL DEFAULT 'NONE'
    CHECK(auto_secondary_status IN ('NONE', 'OK', 'UNAVAILABLE', 'FAILED'));
ALTER TABLE backup_state ADD COLUMN auto_error TEXT NOT NULL DEFAULT '';
