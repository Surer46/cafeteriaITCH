-- Migración 005: Notas por línea en carrito y ventas para personalización de preparación
-- Reglas de specs.md y AGENTS.md:
-- 1. Captura de notas opcionales de hasta 250 caracteres por línea (ej. «sin hielo», «bien cocido»).
-- 2. Dos productos iguales con diferente nota se guardan como líneas separadas.
-- 3. La nota se persiste en el borrador (cart_items) y se copia como instantánea inmutable (sale_items).
-- 4. Conserva compatibilidad con registros existentes asignando cadena vacía por defecto.

PRAGMA foreign_keys = ON;

-- 1. Agregar columna notes a cart_items
ALTER TABLE cart_items ADD COLUMN notes TEXT NOT NULL DEFAULT '';

-- 2. Agregar columna notes a sale_items
ALTER TABLE sale_items ADD COLUMN notes TEXT NOT NULL DEFAULT '';
