# Auditoría T7 — pantallas de actualización del programa

Ronda 9 — T7 (Gemini/AntiGravity, en `main`) — auditado el 2026-11-16

**T7:** aceptado al 100 y al 125 %, con observaciones menores. Integrado en `main` por el orquestador (Gemini no hace commits).

- **Alcance:**
  - Archivos nuevos: `update_banner_panel.{cpp,hpp}` (franja bajo el encabezado), `update_notes_dialog.{cpp,hpp}` (ventana Novedades, no modal) y `backoffice_updates_panel.{cpp,hpp}` (pestaña Actualizaciones).
  - Cambios: `main_window.{cpp,hpp}` (franja, pestaña, sondeo y aviso al cerrar), `subnav_bar.cpp` (quinta pestaña tras Respaldos; márgenes de 6 a 4 y de `md` a `sm` para que quepa) y `CMakeLists.txt` (solo las fuentes).
  - Pruebas:
    - `tests/test_t7_updates.py`, nueva.
    - `CAFETERIA_UPDATE_CHECK=0` añadido en las pruebas GUI y en `diagnose_startup.py`.
    - `test_f1` pasa a 21 permisos y comprueba que `updates.manage` se llama «Actualizar el programa», está en el grupo BackOffice, viene activado para el Dueño y desactivado para el Cajero.
    - `test_f2b` incluye la pestaña nueva; `test_t2` cubre la pestaña y la ventana Novedades.
    - Capturas 24, 25 y 26.
- **Contrato:** usa solo lo que existe en `IUpdateService`:
  - `get_update_status`, `check_for_updates`, `accept_update`, `cancel_download`, `decline_update` y `use_update_from_files`;
  - `postpone_install_this_session` para «Cerrar sin instalar»;
  - `get_last_install_result` y `acknowledge_install_result` al abrir.
  
  La pantalla no lanza el instalador: el cierre sigue en `OnExit` (B11). Todo se oculta sin `updates.manage`; la pestaña sin permiso dice «Sin permiso para actualizar el programa».
- **Sondeo:**
  - En la ventana principal, cada 2,5 s solo mientras el estado sea CHECKING o DOWNLOADING; en el panel también 2,5 s y en Novedades 1,5 s.
  - Se detiene en cualquier otro estado y al destruir la ventana.
  - Paneles con `destroying_`, `Stop()` y `DestroyChildren()` en el destructor (patrón de T6d). Novedades se guarda en un `wxWeakRef`.
- **Revisado en el código:**
  - La franja no aparece en NOT_CONFIGURED, UP_TO_DATE, CHECKING ni ERROR: sin red no se ve nada.
  - «Ahora no» oculta la franja hasta el siguiente inicio, o hasta que aparezca otra versión.
  - El aviso al cerrar solo aparece si `will_install_on_close` es verdadero y el cierre se puede vetar; cerrarlo con la X cancela el cierre. «Cerrar sin instalar» llama a `postpone_install_this_session`.
  - La copia portable solo muestra el aviso, sin descargar ni USB.
- **Verificación del orquestador (100 %, 96 DPI, página de códigos 437):**
  - Compilación Release, Debug y `build_hooks_release` sin advertencias; CTest 17/17 en Release y 14 + 3 omitidas en Debug.
  - Batería GUI:
    - `test_t7_updates` pasa dos veces seguidas.
    - También pasan `f1` (21 permisos), `f2`, `f2b`, `f7`, `fe08_fe09`, `i1`, `t1`, `t2`, `t4`, `t5` y `t6`.
    - Las cinco pestañas caben en estándar y táctil, a 1200×720 y a 960×600; se midió con `GetWindowRect` y `GetTextExtent`.
  - `test_i2_integration` falla en el paso 4: el diálogo de impresión moderno de Windows aparece pero no se cierra con Escape ni con `WM_CLOSE`, y queda `PrintDialog.exe` colgado. **No es T7:** el ejecutable anterior a T7 (`build_be_release` de B11, `368cf02`) falla igual con la misma prueba. Es un cambio del entorno de Windows; hay que adaptar la forma de cancelar el diálogo en la prueba en un bloque de frontend futuro.
  - Capturas 24 a 26 revisadas: la franja no tapa el punto de venta, Novedades muestra el progreso con porcentaje y la pestaña muestra las tres tarjetas. Las capturas 01 a 23 se regeneraron al 100 % (antes estaban al 125 %).
  - Sin peticiones a GitHub: todas las pruebas GUI llevan `CAFETERIA_UPDATE_CHECK=0` y no hay clave pública configurada; T7 usa la fuente local de prueba.
- **Observaciones menores (aceptadas, para un bloque futuro de pulido):**
  1. **Textos comparados a mano:** la franja y la pestaña deciden su texto buscando palabras dentro del mensaje del servicio («autorización de Windows», «dañada», «más nueva»). Si B11 cambia la redacción, la pantalla cambia de texto sin aviso. Lo ideal sería un campo de motivo en el contrato.
  2. **Cualquier fallo se atribuye al permiso:** si la instalación falla por cualquier motivo, la franja dice «No se instaló porque no se dio permiso en Windows», porque el servicio no distingue la cancelación del permiso de otros fallos y su mensaje dice «puede que».
  3. **Mensaje doble:** una descarga dañada muestra «La descarga no es válida y se descartó. No se instaló nada.» seguido del mensaje del servicio, que dice casi lo mismo.
  4. **Prueba de una sola vez incompleta:** la comprobación de que el resultado se muestra una sola vez solo busca uno de los dos textos posibles.
  5. **Sin probar con seis pestañas:** la barra con seis pestañas (Desarrollador con «Servidor de respaldos») no se midió.
- **Al 125 % (120 DPI):** `test_t7_updates` pasa dos veces seguidas y también pasan `f1`, `f2`, `f2b`, `f7`, `fe08_fe09`, `i1`, `t1`, `t2`, `t4`, `t5`, `t6` y `test_startup_paths`; `i2` falla igual que al 100 % (ver arriba). Las capturas quedan las del 100 %.
- **Sin probar:** red real y la instalación real con el aviso de UAC (condición de B11 antes de publicar).
