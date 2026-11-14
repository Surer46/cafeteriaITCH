-- Migración 014: el registro de respaldos acepta un tipo nuevo, PRE_MIGRATION, para la copia que la
-- aplicación hace justo antes de actualizar una base que ya tiene datos.
-- SQLite no permite modificar una restricción CHECK, así que backup_files se reconstruye:
--   * se conservan TODAS las filas con sus valores y su rowid (la limpieza de respaldos ordena por rowid),
--   * se conservan las mismas columnas, valores por omisión y restricciones, más el tipo nuevo,
--   * se recrean los mismos índices.
-- Ninguna otra tabla referencia backup_files (sin claves foráneas hacia ella).
-- Se aplica una sola vez (schema_migrations).

CREATE TABLE backup_files_new (
    id TEXT PRIMARY KEY,
    kind TEXT NOT NULL CHECK(kind IN ('LOCAL', 'PRE_RESTORE', 'PRE_MIGRATION')),
    path TEXT NOT NULL,
    size_bytes INTEGER NOT NULL CHECK(size_bytes >= 0),
    sha256 TEXT NOT NULL,
    created_at TEXT NOT NULL,
    created_at_utc TEXT NOT NULL,
    status TEXT NOT NULL DEFAULT 'ACTIVE' CHECK(status IN ('ACTIVE', 'DELETED', 'CHANGED', 'SUPERSEDED')),
    status_at TEXT,
    last_error TEXT NOT NULL DEFAULT ''
);

INSERT INTO backup_files_new (rowid, id, kind, path, size_bytes, sha256, created_at, created_at_utc, status, status_at, last_error)
SELECT rowid, id, kind, path, size_bytes, sha256, created_at, created_at_utc, status, status_at, last_error
FROM backup_files
ORDER BY rowid;

DROP TABLE backup_files;
ALTER TABLE backup_files_new RENAME TO backup_files;

CREATE INDEX idx_backup_files_kind_created ON backup_files(kind, status, created_at_utc);
CREATE INDEX idx_backup_files_path ON backup_files(path);
