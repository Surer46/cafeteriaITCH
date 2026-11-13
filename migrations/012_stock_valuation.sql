-- Migración 012: valuación de existencias por ingrediente (promedio móvil sobre lo que queda).
-- Reglas de AGENTS.md y docs/specs.md:
-- 1. La valuación se guarda como ENTERO en micro-centavos (1 centavo = 1 000 000), nunca como
--    costo unitario redondeado, y sin float ni double.
--    stock_value_micro:    valor de las existencias actuales.
--    last_avg_value_micro / last_avg_units: último promedio conocido como fracción; sirve cuando
--    la existencia es 0. last_avg_units = 0 significa "costo desconocido" (nunca es costo 0).
-- 2. Desde esta migración toda compra, venta, merma y ajuste actualiza estas columnas en la
--    misma transacción que las existencias, mediante una sola función de los servicios.
-- 3. RELLENO APROXIMADO de datos anteriores a la 012: no se conoce el historial de consumo,
--    así que cada ingrediente con compras recibe como valuación el promedio sobre TODAS sus
--    entradas (suma de costos / suma de unidades) aplicado a su existencia actual, y ese mismo
--    promedio como último promedio conocido. Ingredientes sin compras quedan con valuación 0 y
--    costo desconocido. A partir de aquí cada movimiento nuevo es exacto.
--    Límite: el relleno supone que la suma de costos por 1 000 000 cabe en un entero de 64 bits
--    (unos 92 mil millones de pesos por ingrediente).
-- 4. Los triggers son un respaldo: los servicios validan antes con un error claro.
-- 5. Se aplica una sola vez (schema_migrations).

ALTER TABLE ingredients ADD COLUMN stock_value_micro INTEGER NOT NULL DEFAULT 0;
ALTER TABLE ingredients ADD COLUMN last_avg_value_micro INTEGER NOT NULL DEFAULT 0;
ALTER TABLE ingredients ADD COLUMN last_avg_units INTEGER NOT NULL DEFAULT 0;

UPDATE ingredients
   SET last_avg_value_micro = (SELECT SUM(p.total_cost_cents) * 1000000 FROM purchase_entries p WHERE p.ingredient_id = ingredients.id),
       last_avg_units = (SELECT SUM(p.base_units_purchased) FROM purchase_entries p WHERE p.ingredient_id = ingredients.id)
 WHERE EXISTS (SELECT 1 FROM purchase_entries p WHERE p.ingredient_id = ingredients.id);

-- Valuación = promedio * existencia. Se multiplica antes de dividir (con redondeo) cuando cabe
-- en 64 bits; si no cabe, se divide primero para no desbordar.
UPDATE ingredients
   SET stock_value_micro = CASE
       WHEN current_stock = 0 THEN 0
       WHEN last_avg_value_micro <= (9223372036854775807 - last_avg_units) / current_stock
            THEN (last_avg_value_micro * current_stock + last_avg_units / 2) / last_avg_units
       ELSE (last_avg_value_micro / last_avg_units) * current_stock
   END
 WHERE last_avg_units > 0;

CREATE TRIGGER IF NOT EXISTS trg_ingredients_no_negative_value_update
BEFORE UPDATE OF stock_value_micro ON ingredients
WHEN NEW.stock_value_micro < 0
BEGIN
    SELECT RAISE(ABORT, 'La valuacion de existencias no puede ser negativa');
END;
