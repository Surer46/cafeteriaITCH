-- Migración 003: Índices para catálogo, recetas y compras
-- Optimización para búsquedas por categoría y cálculos proporcionales de recetas e inventario.

PRAGMA foreign_keys = ON;

CREATE INDEX IF NOT EXISTS idx_products_category ON products(category);
CREATE INDEX IF NOT EXISTS idx_products_active ON products(is_active);
CREATE INDEX IF NOT EXISTS idx_recipes_product ON recipes(product_id);
CREATE INDEX IF NOT EXISTS idx_recipe_items_recipe ON recipe_items(recipe_id);
CREATE INDEX IF NOT EXISTS idx_purchase_entries_ing ON purchase_entries(ingredient_id);
CREATE INDEX IF NOT EXISTS idx_stock_movements_ing ON stock_movements(ingredient_id);
