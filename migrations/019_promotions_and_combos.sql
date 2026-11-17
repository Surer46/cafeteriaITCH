-- Migración 019: promociones y combos (B14).
-- 1. promotions / promotion_targets: descuento en porcentaje (puntos base: 1 % = 100), de monto fijo por unidad o
--    NxM (lleva N, paga M), sobre productos concretos o una categoría. Vigencia opcional por fechas, días de la
--    semana (máscara de 7 bits: lunes = 1 ... domingo = 64; 0 = todos) y horario (minutos desde medianoche, hora local).
--    La prioridad la fija quien crea la promoción (número mayor gana).
-- 2. products.is_combo y combo_items: un combo es un producto con precio fijo propio, compuesto por otros productos.
-- 3. sales.discount_cents e instantánea de la promoción aplicada en sale_items: las ventas pasadas no cambian si
--    después se edita o se borra la promoción. Las ventas anteriores quedan con descuento 0 (no se reescribe nada).
-- 4. carts.last_preview_total_cents: total neto que el carrito mostró por última vez; confirm_sale lo compara con el
--    total que calcula al cobrar para detectar que las promociones cambiaron.
-- Se aplica una sola vez (schema_migrations).

CREATE TABLE IF NOT EXISTS promotions (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL,
    kind TEXT NOT NULL CHECK (kind IN ('PERCENT', 'FIXED', 'NXM')),
    percent_bp INTEGER NOT NULL DEFAULT 0 CHECK (percent_bp >= 0 AND percent_bp <= 10000),
    fixed_off_cents INTEGER NOT NULL DEFAULT 0 CHECK (fixed_off_cents >= 0),
    buy_qty INTEGER NOT NULL DEFAULT 0 CHECK (buy_qty >= 0),
    pay_qty INTEGER NOT NULL DEFAULT 0 CHECK (pay_qty >= 0),
    scope_kind TEXT NOT NULL CHECK (scope_kind IN ('PRODUCT', 'CATEGORY')),
    priority INTEGER NOT NULL DEFAULT 0,
    is_active INTEGER NOT NULL DEFAULT 1 CHECK (is_active IN (0, 1)),
    valid_from TEXT,
    valid_to TEXT,
    weekdays_mask INTEGER NOT NULL DEFAULT 0 CHECK (weekdays_mask >= 0 AND weekdays_mask <= 127),
    time_from_min INTEGER,
    time_to_min INTEGER,
    created_at TEXT NOT NULL,
    created_at_utc TEXT,
    updated_at TEXT NOT NULL,
    updated_at_utc TEXT
);

CREATE TABLE IF NOT EXISTS promotion_targets (
    promotion_id TEXT NOT NULL REFERENCES promotions(id) ON DELETE CASCADE,
    product_id TEXT REFERENCES products(id),
    category TEXT,
    CHECK ((product_id IS NOT NULL AND category IS NULL) OR (product_id IS NULL AND category IS NOT NULL))
);
CREATE INDEX IF NOT EXISTS idx_promotion_targets_promo ON promotion_targets(promotion_id);
CREATE INDEX IF NOT EXISTS idx_promotion_targets_product ON promotion_targets(product_id);
CREATE INDEX IF NOT EXISTS idx_promotion_targets_category ON promotion_targets(category);

ALTER TABLE products ADD COLUMN is_combo INTEGER NOT NULL DEFAULT 0;

CREATE TABLE IF NOT EXISTS combo_items (
    combo_product_id TEXT NOT NULL REFERENCES products(id) ON DELETE CASCADE,
    component_product_id TEXT NOT NULL REFERENCES products(id),
    quantity INTEGER NOT NULL CHECK (quantity >= 1 AND quantity <= 99),
    PRIMARY KEY (combo_product_id, component_product_id)
);

ALTER TABLE sales ADD COLUMN discount_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sale_items ADD COLUMN discount_cents INTEGER NOT NULL DEFAULT 0;
ALTER TABLE sale_items ADD COLUMN promotion_id TEXT;
ALTER TABLE sale_items ADD COLUMN promotion_name TEXT NOT NULL DEFAULT '';
ALTER TABLE sale_items ADD COLUMN promotion_kind TEXT NOT NULL DEFAULT '';
ALTER TABLE sale_items ADD COLUMN promotion_params TEXT NOT NULL DEFAULT '';

ALTER TABLE carts ADD COLUMN last_preview_total_cents INTEGER;
