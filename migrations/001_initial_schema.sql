-- Migración 001: Esquema inicial de Cafetería ITCH II
-- Reglas: dinero en centavos enteros, cantidades en unidades base enteras.
-- Sin credenciales ni contraseñas predeterminadas.

PRAGMA foreign_keys = ON;
PRAGMA busy_timeout = 5000;

-- 1. Roles y permisos del sistema
CREATE TABLE IF NOT EXISTS roles (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    description TEXT,
    is_system INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS permissions (
    id TEXT PRIMARY KEY,
    description TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS role_permissions (
    role_id TEXT NOT NULL REFERENCES roles(id) ON DELETE CASCADE,
    permission_id TEXT NOT NULL REFERENCES permissions(id) ON DELETE CASCADE,
    PRIMARY KEY (role_id, permission_id)
);

-- 2. Usuarios del sistema (sin cuentas predeterminadas; alta de Dueño obligatoria en primer uso)
CREATE TABLE IF NOT EXISTS users (
    id TEXT PRIMARY KEY,
    username TEXT NOT NULL UNIQUE,
    display_name TEXT NOT NULL,
    password_hash TEXT NOT NULL,
    role_id TEXT NOT NULL REFERENCES roles(id),
    is_active INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL
);

-- 3. Catálogo de productos e imágenes
CREATE TABLE IF NOT EXISTS products (
    id TEXT PRIMARY KEY,
    sku TEXT NOT NULL UNIQUE,
    barcode TEXT UNIQUE,
    name TEXT NOT NULL,
    category TEXT NOT NULL,
    price_cents INTEGER NOT NULL CHECK(price_cents >= 0),
    is_prepared INTEGER NOT NULL DEFAULT 0,
    is_active INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS product_images (
    id TEXT PRIMARY KEY,
    product_id TEXT NOT NULL UNIQUE REFERENCES products(id) ON DELETE CASCADE,
    mime_type TEXT NOT NULL,
    image_blob BLOB NOT NULL,
    thumbnail_blob BLOB NOT NULL
);

-- 4. Ingredientes y recetas
CREATE TABLE IF NOT EXISTS ingredients (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    unit_dimension TEXT NOT NULL CHECK(unit_dimension IN ('MASS', 'VOLUME', 'COUNT')),
    base_unit TEXT NOT NULL,
    current_stock INTEGER NOT NULL DEFAULT 0,
    low_stock_threshold INTEGER NOT NULL DEFAULT 0
);

CREATE TABLE IF NOT EXISTS recipes (
    id TEXT PRIMARY KEY,
    product_id TEXT NOT NULL REFERENCES products(id) ON DELETE CASCADE,
    version INTEGER NOT NULL DEFAULT 1,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS recipe_items (
    id TEXT PRIMARY KEY,
    recipe_id TEXT NOT NULL REFERENCES recipes(id) ON DELETE CASCADE,
    ingredient_id TEXT NOT NULL REFERENCES ingredients(id),
    quantity_base_units INTEGER NOT NULL CHECK(quantity_base_units > 0)
);

-- 5. Cajas y turnos
CREATE TABLE IF NOT EXISTS cash_registers (
    id TEXT PRIMARY KEY,
    name TEXT NOT NULL UNIQUE
);

CREATE TABLE IF NOT EXISTS shifts (
    id TEXT PRIMARY KEY,
    register_id TEXT NOT NULL REFERENCES cash_registers(id),
    cashier_user_id TEXT NOT NULL REFERENCES users(id),
    opened_at TEXT NOT NULL,
    closed_at TEXT,
    initial_cash_cents INTEGER NOT NULL CHECK(initial_cash_cents >= 0),
    final_cash_cents INTEGER,
    is_open INTEGER NOT NULL DEFAULT 1
);

-- 6. Carrito activo (borrador persistente por caja y usuario)
CREATE TABLE IF NOT EXISTS carts (
    id TEXT PRIMARY KEY,
    register_id TEXT NOT NULL REFERENCES cash_registers(id),
    user_id TEXT NOT NULL REFERENCES users(id),
    updated_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS cart_items (
    id TEXT PRIMARY KEY,
    cart_id TEXT NOT NULL REFERENCES carts(id) ON DELETE CASCADE,
    product_id TEXT NOT NULL REFERENCES products(id),
    quantity INTEGER NOT NULL CHECK(quantity > 0),
    unit_price_cents INTEGER NOT NULL CHECK(unit_price_cents >= 0)
);

-- 7. Ventas, líneas y pagos declarados
CREATE TABLE IF NOT EXISTS sales (
    id TEXT PRIMARY KEY,
    folio TEXT NOT NULL UNIQUE,
    shift_id TEXT NOT NULL REFERENCES shifts(id),
    user_id TEXT NOT NULL REFERENCES users(id),
    total_cents INTEGER NOT NULL CHECK(total_cents >= 0),
    idempotency_key TEXT UNIQUE,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS sale_items (
    id TEXT PRIMARY KEY,
    sale_id TEXT NOT NULL REFERENCES sales(id) ON DELETE CASCADE,
    product_id TEXT NOT NULL REFERENCES products(id),
    product_name TEXT NOT NULL,
    quantity INTEGER NOT NULL CHECK(quantity > 0),
    unit_price_cents INTEGER NOT NULL CHECK(unit_price_cents >= 0),
    subtotal_cents INTEGER NOT NULL CHECK(subtotal_cents >= 0)
);

CREATE TABLE IF NOT EXISTS payment_records (
    id TEXT PRIMARY KEY,
    sale_id TEXT NOT NULL REFERENCES sales(id) ON DELETE CASCADE,
    payment_method TEXT NOT NULL CHECK(payment_method IN ('EFECTIVO', 'TARJETA', 'TRANSFERENCIA')),
    amount_cents INTEGER NOT NULL CHECK(amount_cents >= 0),
    cash_received_cents INTEGER,
    change_cents INTEGER,
    created_at TEXT NOT NULL
);

-- 8. Entradas de compra y movimientos de inventario
CREATE TABLE IF NOT EXISTS purchase_entries (
    id TEXT PRIMARY KEY,
    ingredient_id TEXT NOT NULL REFERENCES ingredients(id),
    quantity_purchased INTEGER NOT NULL CHECK(quantity_purchased > 0),
    purchase_unit TEXT NOT NULL,
    total_cost_cents INTEGER NOT NULL CHECK(total_cost_cents >= 0),
    base_units_purchased INTEGER NOT NULL CHECK(base_units_purchased > 0),
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS stock_movements (
    id TEXT PRIMARY KEY,
    ingredient_id TEXT NOT NULL REFERENCES ingredients(id),
    movement_type TEXT NOT NULL CHECK(movement_type IN ('PURCHASE', 'SALE', 'ADJUSTMENT', 'WASTE', 'TRANSFER')),
    quantity_base_units INTEGER NOT NULL,
    reason TEXT,
    created_at TEXT NOT NULL
);

-- 9. Auditoría y respaldos
CREATE TABLE IF NOT EXISTS audit_events (
    id TEXT PRIMARY KEY,
    event_type TEXT NOT NULL,
    user_id TEXT,
    details TEXT NOT NULL,
    created_at TEXT NOT NULL
);

CREATE TABLE IF NOT EXISTS backup_jobs (
    id TEXT PRIMARY KEY,
    job_type TEXT NOT NULL,
    target_path TEXT NOT NULL,
    status TEXT NOT NULL,
    error_message TEXT,
    created_at TEXT NOT NULL
);

-- Índices para optimizar búsquedas frecuentes
CREATE INDEX IF NOT EXISTS idx_products_sku ON products(sku);
CREATE INDEX IF NOT EXISTS idx_products_name ON products(name);
CREATE INDEX IF NOT EXISTS idx_ingredients_name ON ingredients(name);
CREATE INDEX IF NOT EXISTS idx_sales_folio ON sales(folio);
CREATE INDEX IF NOT EXISTS idx_sales_created ON sales(created_at);
CREATE INDEX IF NOT EXISTS idx_shifts_active ON shifts(is_open);

-- Carga inicial de roles y caja física básica (SIN credenciales de usuario)
INSERT OR IGNORE INTO roles (id, name, description, is_system) VALUES 
    ('role-owner', 'Dueño', 'Propietario con administración total operativa', 1),
    ('role-cashier', 'Cajero', 'Operador de caja y punto de venta', 1),
    ('role-dev', 'Desarrollador', 'Identidad técnica para configuración del servidor de respaldos', 1);

INSERT OR IGNORE INTO cash_registers (id, name) VALUES 
    ('reg-01', 'Caja Principal');
