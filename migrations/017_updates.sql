-- Migración 017: actualizaciones del programa (B11).
-- 1. Permiso nuevo updates.manage (buscar, descargar, aceptar y consultar actualizaciones), asignado SOLO a
--    role-owner. Un rol configurable puede recibirlo si el Dueño se lo da; el Desarrollador no lo tiene por omisión.
-- 2. update_state (una sola fila) guarda el estado de las actualizaciones: última comprobación, versión disponible
--    con su manifiesto firmado tal como se recibió (se vuelve a verificar al instalar), descarga, aceptación e
--    instalación. Es solo estado: nada de lo guardado aquí se ejecuta sin volver a verificar firma, versión y hash.
-- Se aplica una sola vez (schema_migrations).

INSERT OR IGNORE INTO permissions (id, description, title, section, sort_order) VALUES
    ('updates.manage', 'Buscar, descargar y aceptar actualizaciones del programa.', 'Actualizar el programa', 'BackOffice', 360);

INSERT OR IGNORE INTO role_permissions (role_id, permission_id) VALUES ('role-owner', 'updates.manage');

CREATE TABLE IF NOT EXISTS update_state (
    id INTEGER PRIMARY KEY CHECK (id = 1),
    last_check_ms INTEGER NOT NULL DEFAULT 0,          -- cualquier comprobación (UTC, ms desde 1970)
    last_manual_check_ms INTEGER NOT NULL DEFAULT 0,   -- última búsqueda manual
    last_check_at TEXT NOT NULL DEFAULT '',            -- hora local de la última comprobación
    last_check_result TEXT NOT NULL DEFAULT 'NONE',    -- NONE, OK, NO_NETWORK, TIMEOUT, TLS_UNAVAILABLE, TLS_FAILURE, SERVER, INVALID
    last_check_manual INTEGER NOT NULL DEFAULT 0,
    last_error TEXT NOT NULL DEFAULT '',               -- mensaje en lenguaje llano
    available_version TEXT NOT NULL DEFAULT '',
    notes TEXT NOT NULL DEFAULT '',                    -- una frase por línea
    installer_name TEXT NOT NULL DEFAULT '',
    installer_size INTEGER NOT NULL DEFAULT 0,
    installer_sha256 TEXT NOT NULL DEFAULT '',
    manifest_bytes BLOB,                               -- manifiesto firmado, tal cual
    manifest_sig TEXT NOT NULL DEFAULT '',             -- firma en hexadecimal
    download_state TEXT NOT NULL DEFAULT 'NONE' CHECK (download_state IN ('NONE', 'DOWNLOADING', 'DONE')),
    downloaded_path TEXT NOT NULL DEFAULT '',
    download_error TEXT NOT NULL DEFAULT '',
    accepted INTEGER NOT NULL DEFAULT 0 CHECK (accepted IN (0, 1)),
    accepted_at TEXT NOT NULL DEFAULT '',
    install_target_version TEXT NOT NULL DEFAULT '',   -- versión que se intentó instalar (instalación lanzada)
    install_attempt_at TEXT NOT NULL DEFAULT '',
    last_install_result TEXT NOT NULL DEFAULT 'NONE' CHECK (last_install_result IN ('NONE', 'SUCCESS', 'NOT_COMPLETED')),
    last_install_message TEXT NOT NULL DEFAULT '',
    last_install_version TEXT NOT NULL DEFAULT '',
    last_install_shown INTEGER NOT NULL DEFAULT 1 CHECK (last_install_shown IN (0, 1))
);

INSERT OR IGNORE INTO update_state (id) VALUES (1);
