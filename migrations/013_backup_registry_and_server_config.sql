-- Migración 013: registro de respaldos creados por la aplicación, política de retención y
-- configuración persistente del servidor de respaldos.
-- Reglas de AGENTS.md y docs/specs.md:
-- 1. backup_files registra SOLO los respaldos que la aplicación creó y verificó (ruta exacta,
--    tamaño y SHA-256). La limpieza borra únicamente rutas registradas cuyo archivo sigue siendo
--    idéntico al registrado. Los respaldos anteriores a esta migración NO están registrados y
--    nunca se borran. (La tabla backup_jobs de la migración 001 sigue sin uso y no se toca.)
-- 2. La política de retención vive en backup_state: conservar los últimos keep_last respaldos de
--    cada tipo y todo lo de los últimos keep_days días (por omisión 30 y 90).
-- 3. backup_server_config guarda la configuración técnica del servidor de respaldos. Arranca
--    VACÍA hasta que la identidad Desarrollador la configure. No guarda credenciales ni secretos.
-- 4. Se aplica una sola vez (schema_migrations) y no modifica datos existentes.

CREATE TABLE IF NOT EXISTS backup_files (
    id TEXT PRIMARY KEY,
    kind TEXT NOT NULL CHECK(kind IN ('LOCAL', 'PRE_RESTORE')),
    path TEXT NOT NULL,
    size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0),
    sha256 TEXT NOT NULL,
    created_at TEXT NOT NULL,
    created_at_utc TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'ACTIVE' CHECK(status IN ('ACTIVE', 'DELETED', 'CHANGED', 'SUPERSEDED')),
    status_at TEXT,
    last_error TEXT NOT NULL DEFAULT ''
);

CREATE INDEX IF NOT EXISTS idx_backup_files_kind_created ON backup_files(kind, status, created_at_utc);
CREATE INDEX IF NOT EXISTS idx_backup_files_path ON backup_files(path);

ALTER TABLE backup_state ADD COLUMN keep_last INTEGER NOT NULL DEFAULT 30;
ALTER TABLE backup_state ADD COLUMN keep_days INTEGER NOT NULL DEFAULT 90;

CREATE TABLE IF NOT EXISTS backup_server_config (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    server_url TEXT NOT NULL DEFAULT '',
    retention_days TEXT NOT NULL DEFAULT '',
    is_tls_enabled INTEGER NOT NULL DEFAULT 1 CHECK (is_tls_enabled IN (0, 1)),
    updated_at TEXT,
    updated_by TEXT
);

INSERT OR IGNORE INTO backup_server_config (id) VALUES (1);
