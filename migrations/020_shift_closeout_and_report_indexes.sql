-- Migración 020 (B15): conciliación del corte de turno, permiso para cerrar el turno de otro usuario e índices de reportes.
-- 1. El turno cerrado guarda su instantánea (enteros, centavos): quién lo cerró, ventas por método, descuentos,
--    entradas y salidas, efectivo esperado, diferencia y motivo. Los turnos cerrados antes de esta migración
--    quedan con closeout_saved = 0 (sin instantánea): el servicio calcula sus importes al consultarlos.
-- 2. Permiso shift.close_others ("Cerrar turno de otro usuario"): solo el Dueño por omisión; el Dueño puede asignarlo.
-- 3. Índices para sumar ventas, pagos, partidas, movimientos y compras por rango sin recorrer las tablas completas.
-- Se aplica una sola vez (schema_migrations); los índices y el permiso usan IF NOT EXISTS / OR IGNORE.

ALTER TABLE shifts ADD COLUMN closed_by_user_id TEXT;
ALTER TABLE shifts ADD COLUMN closeout_saved INTEGER NOT NULL DEFAULT 0;
ALTER TABLE shifts ADD COLUMN snap_sales_count INTEGER NOT NULL DEFAULT 0;
ALTER TABLE shifts ADD COLUMN snap_gross_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE shifts ADD COLUMN snap_discount_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE shifts ADD COLUMN snap_cash_sales_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE shifts ADD COLUMN snap_card_sales_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE shifts ADD COLUMN snap_transfer_sales_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE shifts ADD COLUMN snap_inflow_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE shifts ADD COLUMN snap_outflow_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE shifts ADD COLUMN expected_cash_cents INTEGER;
ALTER TABLE shifts ADD COLUMN difference_cents INTEGER;
ALTER TABLE shifts ADD COLUMN difference_reason TEXT NOT NULL DEFAULT '';

INSERT OR IGNORE INTO permissions (id, description, title, section, sort_order) VALUES
    ('shift.close_others', 'Cerrar el turno que abrió otro usuario. Queda registrado quién lo cerró y de quién era.',
     'Cerrar turno de otro usuario', 'Caja y Reportes', 215);
INSERT OR IGNORE INTO role_permissions (role_id, permission_id) VALUES ('role-owner', 'shift.close_others');

CREATE INDEX IF NOT EXISTS idx_sales_shift ON sales(shift_id);
CREATE INDEX IF NOT EXISTS idx_sales_user_created ON sales(user_id, created_at);
CREATE INDEX IF NOT EXISTS idx_sale_items_sale ON sale_items(sale_id);
CREATE INDEX IF NOT EXISTS idx_sale_items_product ON sale_items(product_id);
CREATE INDEX IF NOT EXISTS idx_payment_records_sale ON payment_records(sale_id);
CREATE INDEX IF NOT EXISTS idx_shifts_closed_at ON shifts(closed_at);
CREATE INDEX IF NOT EXISTS idx_cash_movements_shift_type ON cash_movements(shift_id, movement_type);
