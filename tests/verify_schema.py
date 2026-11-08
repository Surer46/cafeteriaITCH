#!/usr/bin/env python3
"""
verify_schema.py - Script de verificación técnica independiente para SQLite.
Ejecuta la migración 001, comprueba tablas, claves foráneas, restricciones de integridad
y verifica la persistencia de datos al cerrar y reabrir la base de datos local.
"""

import sqlite3
import os
import sys

DB_PATH = "verify_persistence.db"
MIGRATION_PATH = os.path.join(os.path.dirname(__file__), "..", "migrations", "001_initial_schema.sql")

def run_verification():
    print("=== INICIANDO VERIFICACIÓN DE PERSISTENCIA Y ESQUEMA SQLITE ===")

    if os.path.exists(DB_PATH):
        os.remove(DB_PATH)

    if not os.path.exists(MIGRATION_PATH):
        print(f"ERROR: Archivo de migración no encontrado en {MIGRATION_PATH}")
        sys.exit(1)

    with open(MIGRATION_PATH, "r", encoding="utf-8") as f:
        migration_sql = f.read()

    # 1. Sesión 1: Abrir base, crear tabla de versiones, aplicar migraciones
    print("\n[1] Sesión 1: Conectar a base de datos y ejecutar migraciones...")
    conn = sqlite3.connect(DB_PATH)
    conn.execute("PRAGMA foreign_keys = ON;")
    conn.execute("PRAGMA busy_timeout = 5000;")
    
    # Crear tabla de control de migraciones
    conn.execute("""
        CREATE TABLE IF NOT EXISTS schema_migrations (
            version INTEGER PRIMARY KEY,
            description TEXT NOT NULL,
            applied_at TEXT NOT NULL DEFAULT (datetime('now'))
        );
    """)

    migrations_dir = os.path.join(os.path.dirname(__file__), "..", "migrations")
    migration_files = sorted([f for f in os.listdir(migrations_dir) if f.endswith(".sql")])
    for mig_file in migration_files:
        ver = int(mig_file[:3])
        mig_path = os.path.join(migrations_dir, mig_file)
        with open(mig_path, "r", encoding="utf-8") as f:
            sql = f.read()
        conn.executescript(sql)
        conn.execute("INSERT INTO schema_migrations (version, description) VALUES (?, ?);", (ver, mig_file))
        print(f"-> Migración {mig_file} (v{ver}) aplicada con éxito.")
    conn.commit()

    # 2. Verificar que las tablas existen
    cursor = conn.cursor()
    cursor.execute("SELECT name FROM sqlite_master WHERE type='table' ORDER BY name;")
    tables = [row[0] for row in cursor.fetchall()]
    print(f"Tablas creadas ({len(tables)}): {', '.join(tables)}")

    required_tables = [
        "schema_migrations", "roles", "permissions", "role_permissions", "users",
        "products", "product_images", "ingredients", "recipes", "recipe_items",
        "cash_registers", "shifts", "carts", "cart_items", "sales", "sale_items",
        "payment_records", "purchase_entries", "stock_movements", "audit_events", "backup_jobs"
    ]
    for req in required_tables:
        assert req in tables, f"Falta tabla requerida: {req}"
    print("-> Todas las tablas requeridas por specs.md y contratos están presentes.")

    # 2.1 Verificar columnas notes en cart_items y sale_items (Migración 005)
    cursor.execute("PRAGMA table_info(cart_items);")
    cart_cols = [r[1] for r in cursor.fetchall()]
    assert "notes" in cart_cols, "Falta columna 'notes' en cart_items"
    cursor.execute("PRAGMA table_info(sale_items);")
    sale_cols = [r[1] for r in cursor.fetchall()]
    assert "notes" in sale_cols, "Falta columna 'notes' en sale_items"
    print("-> Columnas 'notes' en cart_items y sale_items verificadas.")

    # 3. Verificar que NO hay credenciales ni usuarios por defecto
    cursor.execute("SELECT COUNT(*) FROM users;")
    user_count = cursor.fetchone()[0]
    assert user_count == 0, f"Error de seguridad: Se encontraron {user_count} usuarios por defecto."
    print("-> Verificación de seguridad: 0 usuarios o credenciales predeterminadas.")

    # 3.1 Verificar catálogo de permisos cargado por migración 002
    cursor.execute("SELECT COUNT(*) FROM permissions;")
    perm_count = cursor.fetchone()[0]
    assert perm_count >= 20, f"Faltan permisos en catálogo: encontrados {perm_count}"
    cursor.execute("SELECT COUNT(*) FROM role_permissions WHERE role_id = 'role-owner';")
    owner_perms = cursor.fetchone()[0]
    assert owner_perms >= 18, f"Permisos de dueño insuficientes: {owner_perms}"
    print(f"-> Permisos del sistema verificados: {perm_count} permisos maestros, {owner_perms} asignados a Dueño.")

    # 4. Insertar dato de prueba en catálogo
    print("\n[2] Insertando producto de prueba en la sesión 1...")
    conn.execute("""
        INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at)
        VALUES ('p-001', 'SKU-CHOCO', '7501001', 'Chocolaleche Especial', 'Bebidas', 3000, 0, 1, '2026-09-24 10:00:00');
    """)
    conn.commit()
    print("-> Producto 'Chocolaleche Especial' ($30.00 = 3000 centavos) insertado.")

    # 5. Cerrar sesión 1 por completo
    print("\n[3] Cerrando conexión a base de datos...")
    conn.close()
    del conn

    # 6. Sesión 2: Reabrir y verificar persistencia
    print("\n[4] Sesión 2: Reabriendo base de datos para verificar persistencia...")
    conn2 = sqlite3.connect(DB_PATH)
    conn2.execute("PRAGMA foreign_keys = ON;")
    cursor2 = conn2.cursor()

    cursor2.execute("SELECT name, category, price_cents, is_active FROM products WHERE sku = 'SKU-CHOCO';")
    row = cursor2.fetchone()
    assert row is not None, "ERROR CRÍTICO: El dato no persistió tras el cierre."
    assert row[0] == "Chocolaleche Especial"
    assert row[1] == "Bebidas"
    assert row[2] == 3000
    assert row[3] == 1
    print(f"-> Dato verificado con éxito tras reapertura: {row[0]} | {row[1]} | {row[2]} centavos (${row[2]/100:.2f}) | Activo={row[3]}")
    print("-> Persistencia comprobada exitosamente.")

    # 7. Verificar integridad referencial (claves foráneas activas)
    print("\n[5] Verificando restricción de clave foránea...")
    try:
        conn2.execute("""
            INSERT INTO users (id, username, display_name, password_hash, role_id, is_active, created_at)
            VALUES ('u-fail', 'fake_user', 'Fake', 'hash', 'rol-no-existe', 1, '2026-09-24 10:00:00');
        """)
        conn2.commit()
        raise AssertionError("Fallo de seguridad: la clave foránea no impidió un rol huérfano.")
    except sqlite3.IntegrityError as e:
        print(f"-> Clave foránea actuó correctamente: {e}")

    # 8. Verificar restricción CHECK (precio negativo)
    print("\n[6] Verificando restricción CHECK de precio no negativo...")
    try:
        conn2.execute("""
            INSERT INTO products (id, sku, barcode, name, category, price_cents, is_prepared, is_active, created_at)
            VALUES ('p-neg', 'SKU-NEG', '0000', 'Invalido', 'Test', -500, 0, 1, '2026-09-24 10:00:00');
        """)
        conn2.commit()
        raise AssertionError("Fallo de restricción: se permitió precio negativo.")
    except sqlite3.IntegrityError as e:
        print(f"-> Restricción CHECK actuó correctamente: {e}")

    conn2.close()

    if os.path.exists(DB_PATH):
        os.remove(DB_PATH)

    print("\n>>> TODAS LAS COMPROBACIONES DE PERSISTENCIA Y REGLAS SQLITE FUERON EXITOSAS! <<<\n")

if __name__ == "__main__":
    run_verification()
