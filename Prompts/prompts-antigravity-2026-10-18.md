# Prompts para AntiGravity — bloque posterior a auditoria-2026-10-18

Enviar **A** primero. Enviar **B** solo cuando A esté auditado (build + CTest + revisión del diff). No enviar ambos juntos.

Reanudar:

```powershell
Set-Location -LiteralPath 'D:\Cafetería_ITCH'
agy --conversation=36c58871-cb3f-45c6-88ad-07be0a8992e5
```

---

## Prompt A — integridad del binario e impresión (sin cambios visuales)

```text
Lee AGENTS.md y Auditorias/auditoria-2026-10-18.md antes de editar. Trabaja SOLO en los 4 puntos siguientes; no toques estilos, layouts, capturas ni otros módulos. No marques I2 como completada ni inicies I3.

1. Mock fuera del ejecutable real.
   En src/services/mock_services.cpp, ServiceRegistry::create_sqlite_registry asigna MockReportService. Quítalo: deja report_service = nullptr y verifica con grep que ninguna pantalla lo desreferencia sin comprobar nulo. No implementes el servicio de reportes en este bloque.

2. Gancho de prueba solo en pruebas.
   CAFETERIA_SIMULATE_PRINT_ERROR se lee en src/frontend/ticket_print_dialog.cpp y src/frontend/ticket_layout.cpp. Enciérralo en #ifdef CAFETERIA_TEST_HOOKS. Añade en CMakeLists.txt una opción CAFETERIA_TEST_HOOKS (OFF por defecto) que defina esa macro para app_desktop. Actualiza tests/test_i2_integration.py para que el caso de error simulado se omita con aviso explícito "[OMITIDO: binario sin CAFETERIA_TEST_HOOKS]" cuando el binario no lo soporte, en lugar de fallar o fingir éxito. Documenta cómo configurar un build con la opción encendida.

3. Impresora predeterminada sin centinela.
   Elimina la cadena "Impresora Térmica Predeterminada" de sqlite_print_config_service.cpp, ticket_print_dialog.cpp y backoffice_print_config_panel.cpp: el valor por defecto es printer_name vacío = predeterminada del sistema. Al cargar un archivo existente con ese valor antiguo, trátalo como vacío. En TicketPrintDialog::on_print, si la impresora configurada no está instalada y el usuario elige «Continuar», NO llames a SetPrinterName (usa la predeterminada) y ajusta el texto del aviso para que diga exactamente eso. Borra la línea SetOrientation(wxLANDSCAPE == wxLANDSCAPE ? ...) y deja SetOrientation(wxPORTRAIT). Intenta aplicar el tamaño de papel: SetPaperId(wxPAPER_NONE) + SetPaperSize(wxSize(80 o 55 mm, largo razonable)) según el perfil; si el controlador lo ignora, que la impresión continúe igual. No afirmes que funciona en hardware: anótalo como no verificado.

4. Prueba atómica real.
   Amplía test_i2_print_config_atomic_save_and_isolation en tests/test_storage.cpp:
   - Construye los nombres con acentos usando fs::u8path(u8"...") (no literales narrow).
   - Guarda dos veces en el mismo directorio (80 mm y luego 55 mm) y comprueba con un servicio nuevo que se leyó el segundo.
   - Con station_printer.cfg ya existente, provoca un fallo de escritura (por ejemplo, crea un DIRECTORIO llamado station_printer.cfg.tmp para que el ofstream falle) y verifica: resultado de error, contenido del archivo previo byte a byte idéntico, configuración en memoria sin cambios.
   - Prueba que un archivo con printer_name=Impresora Térmica Predeterminada se carga como vacío.
   Usa TEST_CHECK, no assert.

Verificación obligatoria, con PATH = D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll:
   cmake --build build_release --parallel 2
   ctest --test-dir build_release --output-on-failure
   cmake --build build_debug --parallel 2
   ctest --test-dir build_debug --output-on-failure
   python -u tests/test_i1_integration.py
   python -u tests/test_i2_integration.py
Compila primero; si falla, corrige antes de seguir. Si compilas más de 3 veces seguidas sin cambiar código, detente y reporta el error textual.

Entrega: lista de archivos modificados, comandos ejecutados con su código de salida y conteo de pruebas, casos omitidos y por qué, y límites (sin impresora física, papel no verificado). Actualiza solo las casillas de backend/task.md y frontend/task.md en la sección "Auditoría 2026-10-18" que correspondan a lo hecho. No reescribas otras secciones de docs/specs.md ni de los planes.
```

---

## Prompt B — recortes visuales y evidencia (enviar después de auditar A)

```text
Lee AGENTS.md, frontend/design-system.md y Auditorias/auditoria-2026-10-18.md. Trabaja SOLO en interfaz y en el script de capturas; no toques servicios, SQL ni pruebas C++ salvo que no compile. No marques I2 ni casos de qa-visual.md como aceptados.

1. Recortes a corregir (ambos perfiles, 1216×759 y 960×600):
   - «Más acciones ▾» en Estándar: la «M» queda tapada por el icono (pos_panel.cpp).
   - «Cambiar Contraseña» en BackOffice > Usuarios: la «C» y la «ñ» se recortan.
   - Diálogo de ticket: el texto de ayuda se corta tras «guardada»; el diálogo debe ajustarse al contenido (Fit/SetMinSize o wrap), no tamaño fijo que recorte.
   - Columnas del carrito «Cant.» y «Subtotal» truncadas; en Touch aparece barra horizontal. Ajusta anchos mínimos para que los encabezados se lean completos.
   Mide con GetTextExtent + márgenes de icono; no subas anchos fijos a ojo.

2. Touch:
   - Búsqueda y nota de preparación quedan alineadas arriba en campos altos. Céntralas verticalmente con la misma técnica que ya funciona en el campo de Cantidad.
   - El selector de producto debe tener la misma altura que la búsqueda en la misma fila.
   - Altura de fila de la tabla del carrito ≥ 40 px en Touch.

3. tests/capture_visual_qa.py:
   - Para 08, selecciona SOLO el combo de perfil (identifícalo por su texto "Perfil ..."), no todos los combos; deja la impresora como esté.
   - Tras seleccionar, lee el texto de la vista previa con WM_GETTEXT y falla si la línea de "=" no mide 28 caracteres. Si el evento sintético no dispara la actualización, usa una ruta que sí la dispare (por ejemplo, enfocar el combo y enviar teclas) y documenta cuál.
   - Regenera 01, 02, 04, 06, 07 y 08.

Verificación: compilar y CTest en Release y Debug, ejecutar tests/test_fe08_fe09_visual_integration.py y el script de capturas. Abre cada captura y describe en la entrega qué cambió y qué sigue mal. Entrega archivos, comandos, resultados y límites (DPI 125/150/200 y monitor táctil no verificados).
```

---

## Prompt C — tres pendientes de la auditoría del prompt B

Escrito sin comillas angulares, flechas ni el signo "mayor o igual" porque esos caracteres se pierden al pegarlos en la terminal de AntiGravity.

```text
Lee AGENTS.md y la seccion "Revision del prompt B" de Auditorias/auditoria-2026-10-18.md. Trabaja SOLO en estos 3 puntos. No toques servicios, SQL, pruebas C++, otros paneles ni capturas fuera de 01 y 02. No marques I2 ni casos de qa-visual.md.

1. Altura del selector de producto en Touch (src/frontend/pos_panel.cpp).
   En la captura 02 choice_products_ mide unos 30 px y la busqueda 48 px; CB_SETITEMHEIGHT de la linea 122 no tiene efecto visible. Antes de cambiar codigo, mide con GetWindowRect la altura real del HWND del selector en Touch y reportala. Luego corrige con UNA de estas rutas, en este orden:
   a) aplicar CB_SETITEMHEIGHT(-1, ...) despues de todo SetFont, Append/Clear y del primer Layout, y volver a aplicarlo al reconstruir la lista y al cambiar de perfil;
   b) si wxChoice sigue ignorandolo, sustituirlo por un control que admita altura (wxOwnerDrawnComboBox de solo lectura o wxBitmapComboBox), conservando el mismo comportamiento de seleccion.
   Criterio: altura real del selector igual a la de la busqueda con tolerancia de 2 px en Touch, sin romper Estandar.

2. Columna SKU truncada.
   Desde que la wxImageList (1 x 42 / 1 x 24) da altura a las filas, la columna SKU muestra BAG... / BA... y el encabezado Notas de Prep... Calcula el ancho minimo de SKU con GetTextExtent del SKU mas largo del carrito, mas el ancho de la imagen y el padding nativo del listview; no subas anchos a ojo. Redistribuye el resto de columnas para que tampoco aparezca barra horizontal en Touch a 1216x759.
   Criterio: BAG-01 y CAP-01 completos en 01 y 02; Cant. y Subtotal siguen completos.

3. Coherencia de tests/test_fe08_fe09_visual_integration.py.
   Los mensajes dicen minimo 48 px pero las aserciones exigen 46 (lineas 433 y 463). Sube las aserciones a 48 para que coincidan con Theme::get_touch_target_min(). Si algun boton falla, corrige el boton en la interfaz: esta prohibido bajar el umbral o cambiar el mensaje para que pase.
   Anade dos aserciones nuevas en Touch: altura del selector de producto igual a la de la busqueda (tolerancia 2 px) y ancho de la columna 0 del listview mayor o igual al ancho medido del texto BAG-01 mas 8 px (usa LVM_GETCOLUMNWIDTH).

Verificacion, con PATH = D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll:
   cmake --build build_release --parallel 2
   ctest --test-dir build_release --output-on-failure
   cmake --build build_debug --parallel 2
   ctest --test-dir build_debug --output-on-failure
   python -u tests/test_fe08_fe09_visual_integration.py
   python -u tests/test_i2_integration.py
   python -u tests/capture_visual_qa.py
Si un comando falla dos veces con el mismo error sin que cambies codigo, detente y reporta el error textual.

Entrega: archivos modificados, comandos con codigo de salida, la altura medida del selector antes y despues, y que se ve en 01 y 02. No afirmes nada de una captura que no hayas abierto. Marca solo la casilla "Revision prompt B (2026-10-18)" de frontend/task.md si los 3 puntos quedan cumplidos.
```
