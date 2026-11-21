-- Migración 021 (B17): nombre de usuario del actor en la bitácora.
-- audit_events.user_id apunta a users, pero tras restaurar un respaldo la cuenta que restauró puede no existir en la
-- base restaurada (se creó después del respaldo). Con actor_username la bitácora conserva quién fue aunque el
-- usuario ya no exista: list_audit_events usa users.username y, si no hay fila, este nombre.
-- 1. Columna nueva (se aplica una sola vez por schema_migrations; SQLite no tiene ADD COLUMN IF NOT EXISTS).
-- 2. Relleno de las filas existentes desde users. No falla si user_id es nulo o la cuenta ya no existe: queda ''.
ALTER TABLE audit_events ADD COLUMN actor_username TEXT NOT NULL DEFAULT '';

UPDATE audit_events
   SET actor_username = COALESCE((SELECT u.username FROM users u WHERE u.id = audit_events.user_id), '')
 WHERE user_id IS NOT NULL AND actor_username = '';
