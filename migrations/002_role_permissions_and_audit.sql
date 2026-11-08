-- Migración 002: Catálogo de permisos, asignación a roles y optimización de auditoría
-- Reglas de AGENTS.md y specs.md:
-- 1. Permisos granulares por acción y módulo.
-- 2. El permiso backup.dev_config es EXCLUSIVO del rol Desarrollador y no puede ser concedido por roles configurables.
-- 3. Cero contraseñas o secretos en auditoría.
-- 4. Repetible e idempotente.

PRAGMA foreign_keys = ON;

-- 1. Catálogo maestro de permisos por módulo del sistema
INSERT OR IGNORE INTO permissions (id, description) VALUES
    ('pos.sell', 'Venta y cobro regular en punto de venta'),
    ('pos.visual_catalog', 'Navegación y selección en catálogo visual'),
    ('pos.notes', 'Captura y edición de notas específicas por producto en pedido'),
    ('pos.reprint', 'Reimpresión de tickets de ventas confirmadas'),
    ('pos.cancel_order', 'Cancelación de órdenes y carritos no cobrados'),
    ('pos.void_sale', 'Anulación autorizada de ventas ya cobradas con reversión'),
    ('inventory.view', 'Consulta de existencias, almacén general y barra'),
    ('inventory.manage_products', 'Alta, edición y baja lógica de productos y recetas'),
    ('inventory.manage_thresholds', 'Configuración de umbrales de bajo stock por ingrediente'),
    ('inventory.restock', 'Registro de compras y reabastecimiento con costo'),
    ('inventory.adjust', 'Registro de mermas, traslados y ajustes de inventario'),
    ('inventory.promotions', 'Gestión de promociones y combos'),
    ('shift.operate', 'Apertura de turno, movimientos de caja, arqueo y corte'),
    ('audit.view', 'Consulta de bitácora y eventos auditados'),
    ('reports.view', 'Generación y consulta de reportes operativos'),
    ('users.manage', 'Gestión de cuentas de usuario (alta, edición, estado)'),
    ('roles.manage', 'Gestión de roles configurables y matriz de permisos'),
    ('config.manage', 'Configuración operativa del negocio'),
    ('backup.manage', 'Activación y ejecución manual de respaldos locales'),
    ('backup.dev_config', 'Configuración técnica del servidor remoto (exclusivo Desarrollador)');

-- 2. Asignación de permisos al rol Dueño (acceso total al negocio, EXCLUYENDO backup.dev_config)
INSERT OR IGNORE INTO role_permissions (role_id, permission_id) VALUES
    ('role-owner', 'pos.sell'),
    ('role-owner', 'pos.visual_catalog'),
    ('role-owner', 'pos.notes'),
    ('role-owner', 'pos.reprint'),
    ('role-owner', 'pos.cancel_order'),
    ('role-owner', 'pos.void_sale'),
    ('role-owner', 'inventory.view'),
    ('role-owner', 'inventory.manage_products'),
    ('role-owner', 'inventory.manage_thresholds'),
    ('role-owner', 'inventory.restock'),
    ('role-owner', 'inventory.adjust'),
    ('role-owner', 'inventory.promotions'),
    ('role-owner', 'shift.operate'),
    ('role-owner', 'audit.view'),
    ('role-owner', 'reports.view'),
    ('role-owner', 'users.manage'),
    ('role-owner', 'roles.manage'),
    ('role-owner', 'config.manage'),
    ('role-owner', 'backup.manage');

-- 3. Asignación de permisos al rol Cajero
INSERT OR IGNORE INTO role_permissions (role_id, permission_id) VALUES
    ('role-cashier', 'pos.sell'),
    ('role-cashier', 'pos.visual_catalog'),
    ('role-cashier', 'pos.notes'),
    ('role-cashier', 'pos.reprint'),
    ('role-cashier', 'pos.cancel_order'),
    ('role-cashier', 'shift.operate');

-- 4. Asignación de permisos al rol Desarrollador
INSERT OR IGNORE INTO role_permissions (role_id, permission_id) VALUES
    ('role-dev', 'backup.dev_config'),
    ('role-dev', 'audit.view');

-- 5. Índices de optimización para auditoría y roles
CREATE INDEX IF NOT EXISTS idx_audit_events_created ON audit_events(created_at);
CREATE INDEX IF NOT EXISTS idx_audit_events_user ON audit_events(user_id);
CREATE INDEX IF NOT EXISTS idx_audit_events_type ON audit_events(event_type);
CREATE INDEX IF NOT EXISTS idx_role_permissions_perm ON role_permissions(permission_id);
