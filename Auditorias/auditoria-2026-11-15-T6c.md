# Auditoría T6c — Respaldos: automático, segunda carpeta (USB) y lista

Ronda 9 — T6c (AntiGravity, Gemini) — auditado el 2026-11-15

**T6c:** código revisado y correcto, en `main` (commit del orquestador). **No aceptado todavía:** `test_t6_backups` no pasa en la PC del orquestador al 125 %, por tres defectos de la prueba (no del programa) que pasan a **T6d**. La entrega escrita de Gemini no llegó al orquestador; se pide en T6d.

- **Alcance:**
  - `backoffice_backups_panel.{cpp,hpp}` (+1284/−387): tarjetas de estado del automático, segunda carpeta o USB, lista de respaldos y restaurar desde la lista.
  - `cash_shift_panel.cpp`: línea del respaldo al cerrar turno.
  - Pruebas: `test_t6_backups.py` (+578), `test_t1_touch_profile.py` (0d), `test_t5_fullscreen.py` (0e), `test_t2_touch_targets.py` (botones nuevos) y `capture_visual_qa.py`.
  - No tocó contratos, servicios ni `main_app.cpp`.
- **Revisión del código:**
  - Sondeo con `wxTimer` cada 1.2 s, solo mientras el estado es `WAITING` o `RUNNING`. Se detiene al terminar, si la consulta falla y en el destructor, y al terminar refresca el estado y la lista.
  - Puerta de permisos: sin `backup.manage` ni `backup.restore` aparece «Sin permiso». Con solo `backup.restore` se ven la lista y restaurar. Con `backup.manage` se ve la gestión; los botones de restaurar solo aparecen con `backup.restore`.
  - Restaurar desde la lista usa el mismo flujo (`execute_restore_flow`), con revisión y dos confirmaciones.
  - El título del rechazo pasa a «Este respaldo no se puede restaurar»; la versión más nueva se rechaza en la revisión.
  - Caja: una sola llamada a `get_auto_backup_status` y una línea más en el mensaje.
  - La prueba cubre todo lo pedido: automático tras cerrar turno, segunda carpeta, USB ausente, quitar, tipos en lenguaje llano, «Falta el archivo», restaurar desde la lista y cancelar, automático apagado, cajero, solo `backup.restore` y solo `backup.manage`. Las comprobaciones de la advertencia se movieron al paso 7b sin perderse.
- **Verificación del orquestador al 125 % (120 DPI), consola en página 437:**
  - Compilación de Release y Debug correcta; CTest 16/16 en las dos.
  - Batería GUI: 9 de 11 pasan. `test_t1_touch_profile` y `test_t2_touch_targets` pasan con sus cambios.
  - `test_t5_fullscreen` falla: la ventana de encima es `claude.exe` (lo identifica el mensaje nuevo). Es el entorno; repetida, pasa.
  - `test_t6_backups` falla y, repetida, sigue fallando. Causas, comprobadas con copias temporales de la prueba fuera del repositorio:
    1. **Página de códigos:** `_read_uia_texts` decodifica la salida de PowerShell con la página OEM de la consola. «¡» llega como `\xad` y «á» como `\xa0`, así que el aviso de Caja, que es correcto, no coincide. Forzar UTF-8 en PowerShell y en `check_output` lo corrige.
    2. **Lectura antes de que termine la construcción:** `GetWindowText` sobre ventanas de otro proceso no espera al hilo de la interfaz. La prueba lee el texto inicial del interruptor antes de que `refresh_status` lo actualice. **Esto explica los dos «Activado» sin causa de T6 y T6b**: entonces el texto inicial era «Activado», ahora es «Desactivado». `BM_GETCHECK` sí espera, por eso el interruptor salía bien. La base y el programa estaban bien. El paso 12 tiene la misma carrera (busca la lista antes de que exista). Sincronizar con `WM_NULL` tras el clic lo resuelve.
    3. Con 1 y 2 aplicados, falla en el paso 9d al volver a «BackOffice» tras cancelar «Restaurar el seleccionado» (no encuentra el botón de la barra lateral en 8 s). Sin diagnosticar; pasa a T6d.
- **Capturas:** regeneradas por `capture_visual_qa.py`; se revisan con T6d.
