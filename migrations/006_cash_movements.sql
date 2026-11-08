-- Migración 006: Movimientos de caja para control de efectivo y turnos
-- Reglas de AGENTS.md y specs.md:
-- 1. Registro auditable de flujo de efectivo por turno (VENTA, FONDO INICIAL, ENTRADA, SALIDA).
-- 2. Integridad referencial con turnos y ventas.
-- 3. Repetible e idempotente.

PRAGMA foreign_keys = ON;

CREATE TABLE IF NOT EXISTS cash_movements (
    id TEXT PRIMARY KEY,
    shift_id TEXT NOT NULL REFERENCES shifts(id),
    movement_type TEXT NOT NULL CHECK(movement_type IN ('INITIAL_CASH', 'SALE', 'INFLOW', 'OUTFLOW')),
    amount_cents INTEGER NOT NULL,
    description TEXT,
    created_at TEXT NOT NULL
);

CREATE INDEX IF NOT EXISTS idx_cash_movements_shift ON cash_movements(shift_id);
