-- Migración 010: datos preparados para varios dispositivos (UTC, última modificación, código de caja).
-- Reglas de AGENTS.md y docs/specs.md:
-- 1. Las columnas de hora local existentes y su formato YYYY-MM-DD HH:MM:SS NO cambian.
-- 2. Columnas *_utc (formato YYYY-MM-DDTHH:MM:SS.mmmZ): el mismo instante que su columna local.
--    Las filas anteriores a esta migración quedan con *_utc en NULL. NULL significa
--    "UTC desconocido, usar la columna local". No se convierten con la zona del equipo
--    porque SQLite solo conoce las reglas de horario vigentes de este equipo, la hora
--    repetida del cambio de horario es ambigua y una base restaurada en otra PC se
--    convertiría mal sin avisar.
-- 3. updated_at / updated_at_utc en tablas de catálogo y cuentas: cambian solo por ediciones
--    desde los servicios (no por iniciar sesión, consultar ni por ventas/compras que mueven
--    existencias). En filas anteriores updated_at = created_at; ingredients, roles y
--    product_images no tenían created_at y toman la hora local de esta migración.
-- 4. cash_registers.code: código corto de caja para los folios (V-<código>-<5 dígitos>).
--    reg-01 recibe 'C1'. Las demás cajas existentes reciben 'C' + ordinal determinista por
--    rowid, empezando en 2 (C2, C3, ...), único y de máximo 4 caracteres A-Z0-9.
--    Los folios ya emitidos (p. ej. V-10001) no cambian.
-- 5. Se aplica una sola vez (schema_migrations) y no cambia identificadores existentes.

-- 1. Instante UTC en tablas de hechos
ALTER TABLE sales ADD COLUMN created_at_utc TEXT;
ALTER TABLE payment_records ADD COLUMN created_at_utc TEXT;
ALTER TABLE cash_movements ADD COLUMN created_at_utc TEXT;
ALTER TABLE stock_movements ADD COLUMN created_at_utc TEXT;
ALTER TABLE purchase_entries ADD COLUMN created_at_utc TEXT;
ALTER TABLE audit_events ADD COLUMN created_at_utc TEXT;
ALTER TABLE shifts ADD COLUMN opened_at_utc TEXT;
ALTER TABLE shifts ADD COLUMN closed_at_utc TEXT;

-- 2. Última modificación en catálogo y cuentas
ALTER TABLE products ADD COLUMN updated_at TEXT NOT NULL DEFAULT '';
ALTER TABLE products ADD COLUMN updated_at_utc TEXT;
ALTER TABLE ingredients ADD COLUMN updated_at TEXT NOT NULL DEFAULT '';
ALTER TABLE ingredients ADD COLUMN updated_at_utc TEXT;
ALTER TABLE recipes ADD COLUMN updated_at TEXT NOT NULL DEFAULT '';
ALTER TABLE recipes ADD COLUMN updated_at_utc TEXT;
ALTER TABLE product_images ADD COLUMN updated_at TEXT NOT NULL DEFAULT '';
ALTER TABLE product_images ADD COLUMN updated_at_utc TEXT;
ALTER TABLE roles ADD COLUMN updated_at TEXT NOT NULL DEFAULT '';
ALTER TABLE roles ADD COLUMN updated_at_utc TEXT;
ALTER TABLE users ADD COLUMN updated_at TEXT NOT NULL DEFAULT '';
ALTER TABLE users ADD COLUMN updated_at_utc TEXT;

UPDATE products SET updated_at = created_at;
UPDATE recipes SET updated_at = created_at;
UPDATE users SET updated_at = created_at;
UPDATE ingredients SET updated_at = strftime('%Y-%m-%d %H:%M:%S', 'now', 'localtime');
UPDATE roles SET updated_at = strftime('%Y-%m-%d %H:%M:%S', 'now', 'localtime');
UPDATE product_images SET updated_at = strftime('%Y-%m-%d %H:%M:%S', 'now', 'localtime');

-- 3. Código de caja
ALTER TABLE cash_registers ADD COLUMN code TEXT NOT NULL DEFAULT '';

UPDATE cash_registers SET code = 'C1' WHERE id = 'reg-01';
UPDATE cash_registers
   SET code = 'C' || (
       SELECT COUNT(*) + 1 FROM cash_registers r2
        WHERE r2.id <> 'reg-01' AND r2.rowid <= cash_registers.rowid
   )
 WHERE id <> 'reg-01';

CREATE UNIQUE INDEX IF NOT EXISTS idx_cash_registers_code ON cash_registers(code) WHERE code <> '';
