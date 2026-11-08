-- Migración 004: Correo obligatorio y avatar opcional para cuentas de usuario
-- Reglas de trabajo:
-- 1. Conserva las cuentas existentes sin alterar migraciones previas (001, 002, 003).
-- 2. Correo obligatorio (formato de correo válido).
-- 3. Avatar opcional almacenado como BLOB.

PRAGMA foreign_keys = ON;

-- 1. Agregar columnas email y avatar_blob a la tabla users
ALTER TABLE users ADD COLUMN email TEXT NOT NULL DEFAULT '';
ALTER TABLE users ADD COLUMN avatar_blob BLOB;

-- 2. Asignar correo corporativo por defecto a cuentas existentes que tuvieran email vacío
UPDATE users SET email = username || '@cafeteria-itch.edu.mx' WHERE email = '';

-- 3. Crear índice único para correo electrónico para garantizar unicidad
CREATE UNIQUE INDEX IF NOT EXISTS idx_users_email ON users(email) WHERE email != '';
