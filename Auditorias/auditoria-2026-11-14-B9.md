# Auditoría B9 — pendientes de B8 antes de instalar en la caja

Ronda 8 — B9 (Claude Code Terminal, `backend/b9`) — auditado el 2026-11-14

**B9:** aceptado. Commit `73fd40d`, integrado en `main`.

- Alcance: `src/crypto/sha256.*` y `services/file_fingerprint.*` nuevos; cambios en `sqlite_database`, `migration_runner`, `database_startup`, cuentas y respaldos; `src/frontend/main_app.cpp` solo con la ventana de progreso; dos pruebas nuevas (`test_sha256`, `test_hash_memory`) y cambios en `test_startup`, `test_startup_timing`, `test_backup` y `test_storage`. Sin migraciones nuevas. Contratos de `contracts/` sin cambios.
- **Base más nueva que el programa:** error `DatabaseNewerThanProgram`, decidido tras `quick_check` y antes de cualquier respaldo o cambio; no crea la carpeta de respaldos ni ningún archivo. Mensaje en lenguaje llano con la ruta.
- **Huella por bloques:** SHA-256 incremental propio (bloques de 1 MiB), una sola función `fingerprint_file` para B6 y B8. Medido por Claude con un archivo de 488 MB: memoria pico de 517 MB (antes) a 6 MB, y algo más rápido. `PasswordHasher::sha256` no cambia; una prueba compara las dos implementaciones. La compresión de 64 bytes queda duplicada entre las dos.
- **Puerta de pruebas:** `set_current_user_for_testing` eliminado de la clase de producción (verificado: ya no aparece en `src/`, `include/` ni `tests/`). Las pruebas inician sesión de verdad; la fila del Desarrollador se inserta por SQL solo dentro de las pruebas. Ninguna comprobación se quitó.
- **Progreso al actualizar:** el arranque emite avisos por fase; durante `integrity_check` y la copia, un *progress handler* de SQLite (cada 20 000 instrucciones, devuelve siempre 0) da el latido. Se retira con un objeto que lo quita al salir por cualquier camino; una prueba lo comprueba en el camino feliz y en cada error. La copia por tandas da el mismo resultado que la de siempre.
- Verificación del orquestador:
  - Worktree: build sin advertencias; CTest 13/13 en Release y 12 más PackageTest omitida en Debug.
  - **Ventana a mano** (100 %, base de prueba de 250 000 ventas creada con `test_startup_timing.exe --make-update-fixture`, ejecutable de `build_be_release`): aparece a los 0.9 s; texto por fase con barra en movimiento; un `WM_CLOSE` enviado a la ventana no la cierra y el proceso sigue; a los 19.4 s se cierra y aparece el alta del Dueño, sin la ventana detrás; queda `backups\pre-migracion\cafeteria-v14-a-v15-....db`. `%ProgramData%\CafeteriaITCH` no se creó.
  - Integrado en `main`: build sin advertencias; CTest 13/13 en Release y Debug (PackageTest omitida en Debug); `test_i1_integration` y `test_t1_touch_profile` exit 0 a 100 %.
- **Detalles de la ventana para corregir** (no bloquean; bloque corto de seguimiento):
  - El diálogo nativo de Windows muestra un botón **«Close», en inglés** y deshabilitado. Para una persona mayor es confuso.
  - El aviso importante, «No apague el equipo», sale en letra pequeña y el título se repite en grande; debería ser al revés.
  - El ancho mínimo de 420 lógicos no se respeta (mide 366 px a 100 %); el texto se ajusta bien.
  - No se vio a 125 %.
- Otros límites declarados: los tiempos son de esta PC con SSD; una única instrucción SQLite muy larga dejaría la barra quieta mientras dura; los avisos llegan a veces dentro de una migración abierta, seguro solo por el orden de arranque.
- Informe: exacto; reportó las líneas cambiadas en pruebas y comprobó que su prueba del *handler* detecta el defecto.
