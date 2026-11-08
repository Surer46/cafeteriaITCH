# Auditoría de los tres bloques — 2026-10-03

## Alcance

Terminal, código del runner/configuración/impresión y capturas actuales de POS Estándar/Touch. Sin cambios a código ni pruebas. Los resultados GUI I1/I2/visual de la terminal son los reportados por Gemini, no repetidos independientemente en esta revisión.

## Avances confirmados por inspección

- [x] Flags de prueba y fallback de credenciales retirados del arranque operativo; runner dedicado recibe credenciales por stdin y usa servicios reales.
- [x] Encabezado usa username; doble + textual eliminado; capturas muestran fondos de texto uniformes y número de Cantidad centrado.
- [x] Contrato/servicio de configuración, permiso config.manage para guardar, panel de selección 55/80 y persistencia básica implementados.
- [x] Ticket muestra desglose unitario cuando cantidad >1. Layout limita columnas por perfil y ancho disponible de DC; selección de perfil llega a venta y reimpresión.

## Pendientes de software

- [ ] **Impresora y papel no aplicados:** TicketPrintDialog::on_print construye wxPrintDialogData/wxPrinter sin usar config_.printer_name ni configurar/comprobar papel 55/80 mm. El nombre guardado no selecciona el dispositivo. Enlazar con impresoras reales disponibles, aplicar selección y validar formato/área del controlador. Evitar afirmar impresión 55/80 verificada solo por limitar texto a 28/42 columnas.
- [ ] **Vista previa nominal:** usa characters_per_line del archivo, mientras impresión usa min(28/42, columnas físicas del DC); puede diferir del resultado real. Mostrar vista previa con la misma geometría de impresión o identificarla como ejemplo nominal hasta seleccionar controlador. La etiqueta “Columnas efectivas” del panel no corresponde a una medición real.
- [ ] **Ruta de configuración:** el registro crea SqlitePrintConfigService sin config_dir. El servicio ignora --data-dir del arranque y usa getenv estrecho o cwd/data. Inyectar el directorio efectivo del puesto ya resuelto como Unicode; probar argumento/env con acentos y lanzamiento desde otro cwd, sin archivos compartidos entre puestos.
- [ ] **Persistencia ante fallos:** set_station_config cambia config_ antes de guardar; save_to_disk trunca directamente el archivo y no comprueba fallo de escritura/flush/cierre. Puede devolver éxito sin persistencia o devolver error dejando activa otra configuración. Usar archivo temporal y sustitución segura, comprobar todas las operaciones, actualizar memoria solo tras éxito. Probar fallo de escritura y conservación de archivo/configuración anteriores.
- [ ] **QA visual todavía incompleto:** capturas 01 y 02 recortan “Editar cantidad”; sidebar en captura Touch conserva altura de Estándar. Sidebar calcula tamaño solo al construirse: reaplicar al alternar perfil, junto a subsecciones/encabezado y entradas. Touch deja búsqueda y notas alineadas arriba en campos altos; falta cumplir alineación vertical del resto de campos. Revisar ambos perfiles con nuevas capturas e inspección humana.
- [ ] **Evidencia pendiente:** no hay capturas de configuración/previa en ambos perfiles térmicos; las pruebas de servicio/layout no demuestran selección real del controlador ni cambio/reinicio/reimpresión desde GUI. Completar esos recorridos con bases aisladas y datos históricos invariantes.

I2 sigue parcial por software de impresión/QA, hardware físico, DPI no verificado y aceptación humana. No avanzar a I3. Se mantienen los siete casos humanos sin aceptación.

## Verificación independiente de compilación y CTest

Toolchain: `D:/Temp/winlibs_msvcrt/mingw64/bin`, PATH preparado con toolchain y DLL wxWidgets.

```powershell
cmake --build build_release --parallel 2
ctest --test-dir build_release --output-on-failure
cmake --build build_debug --parallel 2
ctest --test-dir build_debug --output-on-failure
```

- Release: compilación código 0; CTest 3/3, 19.74 s.
- Debug: compilación código 0; CTest 3/3, 81.10 s.
- No se repitieron I1/I2 GUI ni suite geométrica. Se inspeccionaron las capturas actuales de Venta; no hay aceptación humana nueva.
- Archivos modificados por auditoría: este informe, specs.md, ambos plan.md/task.md y frontend/qa-visual.md. Solo documentación, sin cambios a producción/pruebas.
