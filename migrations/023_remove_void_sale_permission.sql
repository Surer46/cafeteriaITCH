-- Migración 023 (T17): se quita el permiso pos.void_sale («Anular una venta cobrada»).
-- Nunca tuvo función en el programa y los reembolsos de B18 (pos.refund) cubren ese caso (decisión del usuario, 2026-11-22).
-- 1. Se quita de todos los roles (el de Dueño y los personalizados); cada rol conserva sus demás permisos.
-- 2. Se borra del catálogo de permisos, para que no aparezca en Roles y permisos.
-- MigrationRunner aplica cada migración dentro de una transacción y la registra en schema_migrations; las dos
-- sentencias son repetibles (si el permiso ya no existe no hacen nada).
DELETE FROM role_permissions WHERE permission_id = 'pos.void_sale';
DELETE FROM permissions WHERE id = 'pos.void_sale';
