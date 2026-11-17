-- Migración 018: motivos explícitos de una descarga fallida y del resultado de una instalación (B13).
-- last_install_reason: INSTALLED, PERMISSION_DENIED, LAUNCH_FAILED o NOT_COMPLETED ('' en filas anteriores; la
-- interfaz recibe NOT_COMPLETED o INSTALLED según last_install_result, que conserva sus valores SUCCESS/NOT_COMPLETED).
-- download_error_kind: INVALID, NO_NETWORK, TIMEOUT, SERVER, TLS_UNAVAILABLE, TLS_FAILURE o STORAGE.
-- Las filas anteriores a esta migración quedan con '' (motivo desconocido): la interfaz las trata como error de
-- conexión y muestra download_error. Se aplica una sola vez (schema_migrations); no se repite aunque el runner corra de nuevo.

ALTER TABLE update_state ADD COLUMN download_error_kind TEXT NOT NULL DEFAULT '';
ALTER TABLE update_state ADD COLUMN last_install_reason TEXT NOT NULL DEFAULT '';
