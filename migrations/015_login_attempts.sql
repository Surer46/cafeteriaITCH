-- Migración 015: intentos fallidos de inicio de sesión, para limitar la adivinanza de contraseñas.
-- login_key es el nombre de usuario TAL COMO SE ESCRIBIÓ (el inicio de sesión distingue mayúsculas
-- y minúsculas, así que la clave también). Se guardan también nombres que no existen, para que un
-- usuario inexistente se comporte igual que uno real. Nunca se guarda la contraseña.
-- Tiempos en milisegundos desde 1970 en UTC (no dependen de cambios de hora local).
-- locked_until_ms = 0 significa sin bloqueo.
-- Se aplica una sola vez (schema_migrations).

CREATE TABLE IF NOT EXISTS login_attempts (
    login_key TEXT PRIMARY KEY,
    failed_count INTEGER NOT NULL DEFAULT 0 CHECK(failed_count >= 0),
    last_failed_ms INTEGER NOT NULL DEFAULT 0 CHECK(last_failed_ms >= 0),
    locked_until_ms INTEGER NOT NULL DEFAULT 0 CHECK(locked_until_ms >= 0)
);

CREATE INDEX IF NOT EXISTS idx_login_attempts_last_failed ON login_attempts(last_failed_ms);
