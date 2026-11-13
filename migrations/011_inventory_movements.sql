-- Migración 011: actor en movimientos de existencias y compras, índices y respaldo contra existencias negativas.
-- Reglas de AGENTS.md y docs/specs.md:
-- 1. stock_movements y purchase_entries guardan quién hizo el movimiento (actor_user_id).
--    Las filas anteriores a esta migración quedan en NULL (actor desconocido).
-- 2. Índices para listar movimientos y compras por ingrediente y por fecha.
-- 3. Ninguna operación puede dejar existencias negativas: los servicios lo validan antes con un
--    error claro; estos triggers son solo un respaldo en la base de datos.
-- 4. Se aplica una sola vez (schema_migrations) y no modifica datos existentes.

ALTER TABLE stock_movements ADD COLUMN actor_user_id TEXT;
ALTER TABLE purchase_entries ADD COLUMN actor_user_id TEXT;

CREATE INDEX IF NOT EXISTS idx_stock_movements_ingredient ON stock_movements(ingredient_id, created_at);
CREATE INDEX IF NOT EXISTS idx_stock_movements_created ON stock_movements(created_at);
CREATE INDEX IF NOT EXISTS idx_purchase_entries_ingredient ON purchase_entries(ingredient_id, created_at);
CREATE INDEX IF NOT EXISTS idx_purchase_entries_created ON purchase_entries(created_at);

CREATE TRIGGER IF NOT EXISTS trg_ingredients_no_negative_stock_update
BEFORE UPDATE OF current_stock ON ingredients
WHEN NEW.current_stock < 0
BEGIN
    SELECT RAISE(ABORT, 'Las existencias no pueden quedar negativas');
END;

CREATE TRIGGER IF NOT EXISTS trg_ingredients_no_negative_stock_insert
BEFORE INSERT ON ingredients
WHEN NEW.current_stock < 0
BEGIN
    SELECT RAISE(ABORT, 'Las existencias no pueden quedar negativas');
END;
