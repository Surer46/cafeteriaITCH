-- Migración 022 (B18): reembolsos de ventas y cargo extra por pago con tarjeta.
-- 1. sales guarda el cargo por tarjeta cobrado y el porcentaje y fijo usados (instantánea: cambiar la configuración después
--    no altera ventas pasadas). total_cents sigue siendo el neto de productos; lo cobrado es total_cents + card_surcharge_cents
--    y el pago registrado (payment_records.amount_cents) incluye el cargo.
-- 2. payment_settings: una sola fila con la configuración del cargo por tarjeta de toda la cafetería (0 % y $0 = desactivado).
-- 3. refunds y refund_items: reembolsos por pieza con el importe devuelto de cada línea.
-- 4. stock_movements.refund_id: marca los movimientos de inventario que genera un reembolso (no son ajustes manuales).
-- 5. shifts: la instantánea del corte guarda aparte los reembolsos por método y los cargos por tarjeta (las instantáneas
--    anteriores quedan en 0 y no cambian).
-- 6. Permiso pos.refund («Hacer reembolsos»): solo el Dueño por omisión, asignable a otros roles.
-- Se aplica una sola vez (schema_migrations); índices y permiso usan IF NOT EXISTS / OR IGNORE.

ALTER TABLE sales ADD COLUMN card_surcharge_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sales ADD COLUMN card_surcharge_bp INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sales ADD COLUMN card_surcharge_fixed_cents INTEGER NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS payment_settings (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    card_surcharge_bp INTEGER NOT NULL DEFAULT 0 CHECK (card_surcharge_bp >= 0 AND card_surcharge_bp <= 2000),
    card_surcharge_fixed_cents INTEGER NOT NULL DEFAULT 0 CHECK (card_surcharge_fixed_cents >= 0 AND card_surcharge_fixed_cents <= 10000),
    updated_at TEXT NOT NULL DEFAULT '',
    updated_by TEXT NOT NULL DEFAULT ''
);
INSERT OR IGNORE INTO payment_settings (id, card_surcharge_bp, card_surcharge_fixed_cents, updated_at, updated_by) VALUES (1, 0, 0, '', '');

CREATE TABLE IF NOT EXISTS refunds (
    id TEXT PRIMARY KEY,
    folio TEXT NOT NULL UNIQUE,
    sale_id TEXT NOT NULL REFERENCES sales(id),
    shift_id TEXT REFERENCES shifts(id),
    user_id TEXT NOT NULL,
    user_name TEXT NOT NULL DEFAULT '',
    payment_method TEXT NOT NULL CHECK (payment_method IN ('EFECTIVO', 'TARJETA', 'TRANSFERENCIA')),
    amount_cents INTEGER NOT NULL CHECK (amount_cents > 0),
    reason TEXT NOT NULL,
    idempotency_key TEXT UNIQUE,
    created_at TEXT NOT NULL,
    created_at_utc TEXT
);

CREATE TABLE IF NOT EXISTS refund_items (
    id TEXT PRIMARY KEY,
    refund_id TEXT NOT NULL REFERENCES refunds(id) ON DELETE CASCADE,
    sale_item_id TEXT NOT NULL REFERENCES sale_items(id),
    product_id TEXT NOT NULL,
    product_name TEXT NOT NULL,
    quantity INTEGER NOT NULL CHECK (quantity > 0),
    amount_cents INTEGER NOT NULL CHECK (amount_cents >= 0),
    already_prepared INTEGER NOT NULL DEFAULT 0
);

ALTER TABLE stock_movements ADD COLUMN refund_id TEXT;

ALTER TABLE shifts ADD COLUMN snap_cash_refunds_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE shifts ADD COLUMN snap_card_refunds_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE shifts ADD COLUMN snap_transfer_refunds_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE shifts ADD COLUMN snap_card_surcharge_cents INTEGER NOT NULL DEFAULT 0;

INSERT OR IGNORE INTO permissions (id, description, title, section, sort_order) VALUES
    ('pos.refund', 'Devolver el dinero de piezas ya vendidas. Queda registrado quién lo hizo y por qué.',
     'Hacer reembolsos', 'Punto de Venta', 65);
INSERT OR IGNORE INTO role_permissions (role_id, permission_id) VALUES ('role-owner', 'pos.refund');

CREATE INDEX IF NOT EXISTS idx_refunds_sale ON refunds(sale_id);
CREATE INDEX IF NOT EXISTS idx_refunds_created ON refunds(created_at);
CREATE INDEX IF NOT EXISTS idx_refunds_shift ON refunds(shift_id);
CREATE INDEX IF NOT EXISTS idx_refund_items_sale_item ON refund_items(sale_item_id);
CREATE INDEX IF NOT EXISTS idx_refund_items_refund ON refund_items(refund_id);
CREATE INDEX IF NOT EXISTS idx_stock_movements_refund ON stock_movements(refund_id);
