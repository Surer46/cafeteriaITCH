-- Migración 009: título corto, descripción llana, sección y orden de cada permiso.
-- Reglas de AGENTS.md y docs/specs.md:
-- 1. El panel de roles muestra los permisos a personas con poca familiaridad técnica.
-- 2. No cambia ningún id de permiso ni role_permissions.
-- 3. Cada migración se aplica una sola vez (schema_migrations); los UPDATE son por id.

ALTER TABLE permissions ADD COLUMN title TEXT NOT NULL DEFAULT '';
ALTER TABLE permissions ADD COLUMN section TEXT NOT NULL DEFAULT '';
ALTER TABLE permissions ADD COLUMN sort_order INTEGER NOT NULL DEFAULT 0;

UPDATE permissions SET title = 'Vender y cobrar', section = 'Punto de Venta', sort_order = 10, description = 'Registrar ventas y cobrarlas en la caja.' WHERE id = 'pos.sell';
UPDATE permissions SET title = 'Usar el catálogo con fotos', section = 'Punto de Venta', sort_order = 20, description = 'Elegir productos desde las tarjetas con imagen, como en un menú.' WHERE id = 'pos.visual_catalog';
UPDATE permissions SET title = 'Escribir notas en un pedido', section = 'Punto de Venta', sort_order = 30, description = 'Añadir indicaciones a un producto, por ejemplo: sin azúcar.' WHERE id = 'pos.notes';
UPDATE permissions SET title = 'Reimprimir tickets', section = 'Punto de Venta', sort_order = 40, description = 'Volver a imprimir el ticket de una venta ya cobrada.' WHERE id = 'pos.reprint';
UPDATE permissions SET title = 'Cancelar un pedido sin cobrar', section = 'Punto de Venta', sort_order = 50, description = 'Vaciar el pedido en curso antes de cobrarlo.' WHERE id = 'pos.cancel_order';
UPDATE permissions SET title = 'Anular una venta cobrada', section = 'Punto de Venta', sort_order = 60, description = 'Cancelar una venta que ya se cobró. Queda registrado quién lo hizo y por qué.' WHERE id = 'pos.void_sale';
UPDATE permissions SET title = 'Ver existencias', section = 'Inventario', sort_order = 110, description = 'Consultar cuánto hay de cada producto e ingrediente.' WHERE id = 'inventory.view';
UPDATE permissions SET title = 'Administrar productos y recetas', section = 'Inventario', sort_order = 120, description = 'Crear, cambiar o dar de baja productos, sus precios y sus recetas.' WHERE id = 'inventory.manage_products';
UPDATE permissions SET title = 'Registrar compras', section = 'Inventario', sort_order = 130, description = 'Anotar lo que se compra y cuánto costó, para sumarlo al inventario.' WHERE id = 'inventory.restock';
UPDATE permissions SET title = 'Registrar mermas y ajustes', section = 'Inventario', sort_order = 140, description = 'Anotar pérdidas, traslados y correcciones del inventario.' WHERE id = 'inventory.adjust';
UPDATE permissions SET title = 'Definir avisos de poco inventario', section = 'Inventario', sort_order = 150, description = 'Fijar la cantidad mínima de cada ingrediente para que el sistema avise.' WHERE id = 'inventory.manage_thresholds';
UPDATE permissions SET title = 'Administrar promociones y combos', section = 'Inventario', sort_order = 160, description = 'Crear y cambiar promociones y combos.' WHERE id = 'inventory.promotions';
UPDATE permissions SET title = 'Abrir y cerrar caja', section = 'Caja y Reportes', sort_order = 210, description = 'Abrir turno, registrar entradas y salidas de dinero y hacer el corte.' WHERE id = 'shift.operate';
UPDATE permissions SET title = 'Ver reportes', section = 'Caja y Reportes', sort_order = 220, description = 'Consultar los reportes de ventas e inventario.' WHERE id = 'reports.view';
UPDATE permissions SET title = 'Ver la bitácora', section = 'Caja y Reportes', sort_order = 230, description = 'Consultar el historial de lo que cada persona hizo en el sistema.' WHERE id = 'audit.view';
UPDATE permissions SET title = 'Administrar cuentas', section = 'BackOffice', sort_order = 310, description = 'Crear cuentas de usuario, cambiarlas o desactivarlas.' WHERE id = 'users.manage';
UPDATE permissions SET title = 'Administrar roles y permisos', section = 'BackOffice', sort_order = 320, description = 'Crear roles y decidir qué puede hacer cada uno.' WHERE id = 'roles.manage';
UPDATE permissions SET title = 'Cambiar la configuración', section = 'BackOffice', sort_order = 330, description = 'Ajustar opciones del negocio, como la impresora y el tamaño del ticket.' WHERE id = 'config.manage';
UPDATE permissions SET title = 'Hacer respaldos', section = 'BackOffice', sort_order = 340, description = 'Activar y ejecutar copias de seguridad de la información.' WHERE id = 'backup.manage';
UPDATE permissions SET title = 'Restaurar un respaldo', section = 'BackOffice', sort_order = 350, description = 'Recuperar la información desde una copia. Reemplaza los datos actuales.' WHERE id = 'backup.restore';
UPDATE permissions SET title = 'Configurar el servidor de respaldos', section = 'BackOffice', sort_order = 990, description = 'Configuración técnica del servidor remoto (exclusivo Desarrollador).' WHERE id = 'backup.dev_config';
