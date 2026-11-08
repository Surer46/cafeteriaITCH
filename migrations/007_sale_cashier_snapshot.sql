-- Migración 007: Instantánea inmutable del nombre del cajero en ventas
-- Reglas de AGENTS.md y specs.md:
-- 1. Los tickets históricos deben conservar el nombre del cajero en el momento de la venta.
-- 2. Cambios posteriores en users.display_name no deben alterar tickets ya emitidos.
-- 3. No inventar información histórica para ventas pasadas (cashier_name será NULL en registros previos).

PRAGMA foreign_keys = ON;

ALTER TABLE sales ADD COLUMN cashier_name TEXT;
