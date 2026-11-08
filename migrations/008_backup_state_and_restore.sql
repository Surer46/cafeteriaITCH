-- Migración 008: estado persistente de respaldos y permiso de restauración.
-- Reglas de AGENTS.md y specs.md:
-- 1. El estado del respaldo sobrevive al reinicio (sin rutas absolutas del usuario).
-- 2. backup.restore se asigna solo al rol Dueño; nunca a role-dev por defecto.
-- 3. Repetible e idempotente.

CREATE TABLE IF NOT EXISTS backup_state (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    is_enabled INTEGER NOT NULL DEFAULT 0 CHECK (is_enabled IN (0, 1)),
    last_backup_at TEXT,
    last_status TEXT NOT NULL DEFAULT 'PENDIENTE',
    last_error TEXT NOT NULL DEFAULT ''
);

INSERT OR IGNORE INTO backup_state (id) VALUES (1);

INSERT OR IGNORE INTO permissions (id, description) VALUES
    ('backup.restore', 'Restauración de la base local desde un respaldo verificado');

INSERT OR IGNORE INTO role_permissions (role_id, permission_id) VALUES
    ('role-owner', 'backup.restore');
