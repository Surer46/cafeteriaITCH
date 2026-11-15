# Auditoría T6b — correcciones de la pantalla Respaldos

Ronda 9 — T6b (AntiGravity, Gemini) — auditado el 2026-11-15

**T6b:** aceptado al 100 %. Falta repetir la batería al 125 % (el usuario cambia el escalado). Quedan dos fallos intermitentes por investigar.

- **Alcance:** `backoffice_backups_panel.cpp`, `tests/test_t6_backups.py` (+234 líneas, sin quitar aserciones), `tests/test_t2_touch_targets.py` (punto 5) y las capturas regeneradas. `capture_visual_qa.py` regeneró todas las capturas, no solo la 21 y la 22; las diferencias son de datos y hora. No tocó contratos, servicios ni `main_app.cpp`.
- **Punto 1 (limpieza):** el texto ahora es verdadero: «Revisa y borra respaldos antiguos hechos por este programa, incluso los guardados en otra carpeta o en una memoria USB, según las reglas de abajo. Siempre conserva el respaldo más reciente de cada tipo y nunca borra otros archivos». El diálogo de confirmación ya no dice «no se tocarán». La vista previa gana la columna «Carpeta», y la prueba lee los encabezados y la celda con la ruta.
- **Punto 2 (revisión antes de restaurar):** si `detail` no está vacío, rechaza el archivo con su motivo antes de las confirmaciones; la prueba de un respaldo sin Dueño activo lo confirma. No compara `schema_version`, porque el contrato no da la versión de la base actual. Ese caso queda cubierto cuando B12 llene `detail` con «versión más nueva» (añadido A de la respuesta al plan).
  - Consecuencia: hoy un respaldo más nuevo sigue pasando las dos confirmaciones y falla al final sin cambiar datos.
  - Al integrar B12, el paso 7 de `test_t6_backups.py` (que espera «Restauración no aplicada» tras las confirmaciones) debe pasar a esperar el rechazo en la revisión. Va en el próximo bloque de Gemini.
  - Menor: el título del rechazo, «Respaldo inválido o dañado», no encaja con «sin Dueño» ni con «versión más nueva».
- **Punto 3 (advertencia):** la advertencia dice qué se pierde, con la fecha del respaldo, y dónde queda la copia previa («la carpeta de respaldos de la carpeta de datos»). Se quitó «(revisión N)». Captura 22 revisada.
- **Punto 4 (pruebas añadidas):**
  - Doble clic (dos `BM_CLICK` en cola): un diálogo, un archivo y un evento `BACKUP_LOCAL`.
  - `integrity_check` de los respaldos creados.
  - Cancelar en el primer y en el segundo diálogo: sin cambios en la base y sin copia previa.
  - Tras reabrir, la lista de Usuarios muestra el dato anterior al respaldo y no el posterior.
  - Respaldo sin Dueño activo rechazado con su motivo.
  - Desaparece el texto «Nunca borra».
  - El doble clic lo evita `BusyScope`, que ahora abarca también los diálogos.
- **Punto 5 (t2):** si no aparece «Listo» en Respaldos, la prueba falla con una aserción. No se recibió el motivo del cambio de T6 en la forma de cerrar las listas desplegables.
- **Verificación del orquestador al 100 % (96 DPI):**
  - Compilación de Release y Debug sin advertencias; CTest 14/14 en Release y Debug.
  - Batería GUI (11 pruebas): 9 pasan a la primera.
  - `test_t6_backups` falló en Debug: al empezar, el interruptor del automático decía «Activado» en vez de «Desactivado»; Release pasó en la misma corrida.
  - `test_t5_fullscreen` falló en Release: otra ventana quedó encima del diálogo de Cobro en el punto medido.
  - Repetidas una vez, las dos pasan (Release y Debug). Las carpetas temporales ya no estaban, así que no se encontró la causa.
  - Se registran como **intermitentes**, para investigar en el próximo bloque de Gemini: volcar la bitácora y `backup_state` cuando falle, y en T5 nombrar la ventana que tapa.
- **Verificación del orquestador al 125 % (120 DPI), el mismo día:**
  - Compilación de Release y Debug sin advertencias; CTest 14/14 en Release y Debug.
  - Batería GUI (11 pruebas): 8 pasan a la primera y tres fallan. Repetidas una vez, las tres pasan:
    - `test_t6_backups` (Release) volvió a encontrar el interruptor «Activado» al empezar. Es la segunda vez, y las dos fue la primera prueba GUI tras compilar. El interruptor se llena de forma síncrona con `get_status` al construir la pantalla, así que en ese momento el servicio devolvió `is_enabled = true` o algo lo activó antes. **Sin explicar**, no se puede dar por intermitente sin más: hay que investigarlo.
    - `test_t5_fullscreen` (Release): la ventana encima del diálogo de Cobro es `claude.exe`, la app de escritorio del orquestador, que pasó al frente. Causa del entorno, no del programa. Mismo fallo que al 100 %.
    - `test_t1_touch_profile` (Release): la barra lateral midió 58 px (menos de 70) justo después de aparecer «Táctil (Activa)». La prueba espera el texto del botón pero no el nuevo diseño, así que la carrera está en la prueba.
  - **Para el próximo bloque de Gemini:**
    - T6: conservar la carpeta temporal y volcar `backup_state` y la bitácora cuando falle el estado inicial; buscar qué deja `is_enabled = 1`.
    - T1: esperar con condición a que la barra lateral tenga la altura táctil antes de medir, sin bajar el umbral.
    - T5: si la ventana de encima es de otro proceso, decir de cuál en el mensaje.
