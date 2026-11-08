# Auditoría independiente — 2026-10-18

## Alcance

Contraste de la entrega Sonnet/Gemini descrita en `auditoria-2026-10-14.md` (redactada por el propio agente ejecutor) contra el código actual, capturas en `qa_visual/screenshots/` y una compilación desde cero. Sin cambios a código de producción ni pruebas; solo documentación.

## Verificación ejecutada en esta auditoría

Toolchain: GCC 16.1.0 (winlibs, `D:/Temp/winlibs_msvcrt/mingw64/bin`), wxWidgets **3.3.3** (DLL `wx*333u*`, no 3.2 como dice la auditoría del 14), CMake/Ninja del mismo toolchain.

```bash
cmake --build build_release --clean-first --parallel 4
ctest --test-dir build_release --output-on-failure
cmake --build build_debug --clean-first --parallel 4
ctest --test-dir build_debug --output-on-failure
```

- Release: compilación desde cero código 0, 0 advertencias; CTest 3/3 (19.07 s).
- Debug: compilación desde cero código 0, 0 advertencias; CTest 3/3 (62.63 s).
- Sonda aparte: `std::filesystem::rename` sobre archivo existente funciona con este toolchain (el reemplazo atómico de `station_printer.cfg` es viable).
- **No ejecutado:** `test_i1_integration.py`, `test_i2_integration.py`, `test_fe08_fe09_visual_integration.py` (abren ventanas nativas en el escritorio del usuario). Sus resultados siguen siendo los reportados por el agente, no verificados aquí.

## Pendientes de auditoria-2026-10-03 — estado real

| # | Pendiente | Estado | Evidencia |
|---|---|---|---|
| 1 | Impresora y papel aplicados | **Parcial** | `SetPrinterName` sí se aplica (`ticket_print_dialog.cpp`). **No** se configura tamaño de papel 55/80 (`SetPaperId`/`SetPaperSize` ausentes): depende del controlador. Al elegir «Continuar» con impresora no instalada se sigue usando ese nombre; la auditoría del 14 afirma falsamente que cae a la predeterminada. |
| 2 | Vista previa nominal | Resuelto en código | Rotulada como estimación; «Columnas efectivas» eliminada. |
| 3 | Ruta del puesto | Resuelto | `main_app.cpp` inyecta `data_dir` resuelto a `create_sqlite_registry`. |
| 4 | Persistencia ante fallos | Código correcto, **prueba insuficiente** | `.tmp` + `rename`, memoria solo cambia tras éxito. Pero `test_i2_print_config_atomic_save_and_isolation` solo prueba fallo de `create_directories` sin archivo previo: no comprueba que un archivo existente se conserve ni que un segundo guardado reemplace al primero. El literal `"cafeteria_cfg_test_áéíóú"` se convierte por página ANSI en C++17, así que no prueba realmente UTF-8. |
| 5 | Correcciones visuales | **Parcial** | Sidebar Touch (56 px) y subnav: hecho. Cantidad centrada: sí en 01/02. Persisten: búsqueda y notas alineadas arriba en campos altos Touch (02); «Más acciones» recortado en Estándar (01); columna «Ca…» y «Subtota» truncadas; filas de tabla sin altura táctil; «Cambiar Contraseña» recortado (06); texto de ayuda del diálogo de ticket cortado tras «guardada» (04). |
| 6 | Evidencia | **Inválida en parte** | `08_print_config_55mm.png` muestra 55 mm seleccionado pero vista previa de 42 columnas, e impresora «OneNote (Desktop)»: `capture_visual_qa.py` fuerza índice 1 en *todos* los combos y el `CBN_SELCHANGE` sintético no dispara `update_preview()`. La captura no demuestra 28 columnas. |

## Hallazgos nuevos

1. **Mock en el ejecutable distribuible (viola AGENTS.md):** `ServiceRegistry::create_sqlite_registry` asigna `MockReportService` (12 ventas, $2,930 fijos). Ninguna pantalla lo usa aún; riesgo alto al construir Reportes.
2. **Gancho de prueba en producción:** `CAFETERIA_SIMULATE_PRINT_ERROR` en `ticket_print_dialog.cpp` y `ticket_layout.cpp` altera el binario real (sin diálogo + fallo forzado). Debe quedar tras una opción de compilación solo para pruebas.
3. **Nombre de impresora ficticio:** el valor por defecto `"Impresora Térmica Predeterminada"` es un centinela mágico repetido en tres archivos; el diálogo de ticket lo muestra como «Impresora configurada» (captura 04), mientras el panel muestra «(Predeterminada del sistema)». Usar cadena vacía como «predeterminada».
4. Código sin sentido: `SetOrientation(wxLANDSCAPE == wxLANDSCAPE ? wxPORTRAIT : wxPORTRAIT)`.
5. `SqlitePrintConfigService` no usa SQLite (archivo plano); `db_` sin uso. El cambio de configuración no genera evento de auditoría.
6. Documentación: `specs.md` rotula I2 «SOFTWARE COMPLETADO Y AUDITADO»; los planes afirman «todos los requerimientos de software de I2 resueltos». No es cierto según esta tabla.

## Estado de la puerta I2

**Parcial en software**, no solo en hardware. No marcar I2 ni iniciar I3. Siguen pendientes: hardware térmico 55/80, monitor táctil, DPI 125/150/200 y aceptación humana (0/7).

## Siguiente bloque recomendado (acotado, un solo agente)

1. Quitar `MockReportService` del registro real (servicio SQLite mínimo o `nullptr` con pantalla «no disponible»).
2. Mover `CAFETERIA_SIMULATE_PRINT_ERROR` tras `#ifdef CAFETERIA_TEST_HOOKS` definido solo en un objetivo de prueba.
3. Sustituir el centinela por cadena vacía; «Continuar» con impresora ausente debe limpiar `SetPrinterName`. Eliminar la línea de orientación absurda.
4. Ampliar la prueba atómica: guardar dos veces en el mismo directorio, provocar fallo con archivo previo y verificar contenido intacto; usar `fs::u8path` para el nombre Unicode.
5. Corregir recortes visibles (Más acciones, Cambiar Contraseña, ayuda del ticket, columnas) y alineación vertical de búsqueda/notas en Touch.
6. Arreglar `capture_visual_qa.py` (seleccionar solo el combo de perfil y verificar el texto de la vista previa) y regenerar 01, 02, 04, 06, 07, 08.

Compilar Debug/Release, CTest en ambos y repetir I1/I2/FE-08 antes de declarar terminado.

## Archivos modificados por esta auditoría

`auditoria-2026-10-18.md` (nuevo), `specs.md`, `frontend/task.md`, `backend/task.md`, `antigravity-resume.md`.

---

## Revisión del prompt A (Gemini 3.8 Flash) — 2026-10-18, noche

Verificado por inspección de código y ejecución propia:

| Punto | Resultado |
|---|---|
| 1. Mock fuera del binario | **Aceptado.** `create_sqlite_registry` deja `report_service = nullptr`; ninguna pantalla lo usa. `MockReportService` solo queda en `create_mock_registry`. |
| 2. Gancho de prueba | **Aceptado.** Lecturas de `CAFETERIA_SIMULATE_PRINT_ERROR` bajo `#ifdef CAFETERIA_TEST_HOOKS`; opción CMake OFF en ambos builds; la cadena no aparece en `build_release/app_desktop.exe` ni en `build_debug/app_desktop.exe`. `test_i2_integration.py` omite el requerimiento 8 con aviso y resumen «8 requerimientos». Documentado en README. |
| 3. Impresora sin centinela | **Aceptado con observaciones.** Valor por defecto vacío; centinela legado se convierte a vacío al cargar; «Continuar» ya no llama a `SetPrinterName`; orientación corregida; `SetPaperId(wxPAPER_NONE)` + `SetPaperSize(55/80 × 200 mm)`. |
| 4. Prueba atómica | **Aceptado.** Doble guardado en el mismo directorio, fallo con directorio `.tmp` bloqueador y archivo previo comparado byte a byte, memoria sin cambios, carga del centinela legado, rutas con `fs::u8path(u8"...")`, solo `TEST_CHECK`. |

Comandos propios: `cmake --build build_release|build_debug --parallel 4` (código 0, 0 advertencias) y `ctest` en ambos (3/3; StorageTest 23.56 s Release, 52.49 s Debug). No repetí I1/I2 GUI; sus resultados son los reportados por el agente.

Observaciones abiertas (no bloquean A):
- El requerimiento 8 (error de impresión) ya no se ejecuta con los builds habituales. Falta un build dedicado (`cmake -B build_hooks -DCAFETERIA_TEST_HOOKS=ON`) y correr ahí `test_i2_integration.py` para que el camino de error siga cubierto.
- Largo de página fijo de 200 mm: en rollo térmico un ticket largo se paginará con pie «Página X de Y» cada 200 mm. Revisar con hardware real.
- El aviso de impresora ausente usa `wxOK | wxCANCEL`: el botón dice «Aceptar», no «Continuar» como indica el texto. Ajustar etiqueta con `wxMessageDialog::SetOKCancelLabels`.
- Comportamiento del papel personalizado: **no verificado en hardware**.

Desempeño del agente: dentro del alcance, sin bucles ni compilaciones repetidas; una llamada perdida (`git grep` en carpeta sin git) y una búsqueda recursiva costosa en `D:\Temp\wxwidgets`. Informe final coherente con el código en esta ocasión.

Siguiente: prompt B (`prompts-antigravity-2026-10-18.md`).

---

## Revisión del prompt B (Gemini 3.8 Flash) — 2026-10-18, 23:40

Verificado por inspección de capturas regeneradas, código y ejecución propia (`cmake --build` Release/Debug código 0, 0 advertencias; CTest 3/3 en ambos). No repetí `test_fe08_fe09_visual_integration.py` ni `capture_visual_qa.py`.

| Punto | Resultado | Evidencia |
|---|---|---|
| «Más acciones» en Estándar | **Aceptado** | 01: texto completo, icono separado. |
| «Cambiar Contraseña» | **Aceptado** | 06: texto completo. |
| Ayuda del diálogo de ticket | **Aceptado** | 04: texto completo hasta «…desde el historial»; muestra «Impresora: predeterminada del sistema». |
| Columnas Cant./Subtotal | **Aceptado** | 01 y 02 completas; sin barra horizontal en Touch. |
| Búsqueda y notas centradas en Touch | **Aceptado** | 02: texto centrado verticalmente en campos de 48 px. |
| Altura de filas Touch | **Aceptado** | 02: filas de unos 42 px (truco `wxImageList` 1×42). |
| Selector de producto a la altura de la búsqueda | **Rechazado — probablemente error del auditor (ver corrección en la revisión del prompt C)** | El informe dice «exactamente 48 px», pero en 02 el selector mide unos 30 px frente a los 48 px de la búsqueda. `CB_SETITEMHEIGHT` (`pos_panel.cpp:122`) no surtió efecto visible. |
| Captura 08 y script | **Aceptado** | Solo cambia el combo de perfil; impresora intacta; vista previa a 28 columnas; aserción estricta `sep_len == 28` que detectó el fallo intermedio en vez de dejarlo pasar. |

Regresiones y observaciones nuevas:
- **Columna SKU truncada** («BAG…», «CAP…» en Touch; «BA…», «CA…» en Estándar) y encabezado «Notas de Prep…». Probable efecto de la imagen de la `wxImageList` en la primera columna, que resta ancho. Antes se leía «BAG-01».
- `tests/test_fe08_fe09_visual_integration.py` imprime «objetivos táctiles ≥48px», pero las aserciones exigen `>= 46` (líneas 433 y 463). Sin git no se puede saber si ya estaba así; la prueba afirma más de lo que comprueba.
- El prompt perdió «≥» al pegarse en la terminal; el agente lo interpretó bien (≥ 40 px).

Desempeño del agente: alcance respetado y sin tocar servicios ni pruebas C++. Costo alto: 43 lecturas antes de 6 ediciones, bloques de razonamiento de 7 y 9 minutos y una compactación de contexto al inicio. Hubo 3 ejecuciones fallidas seguidas de capturas sin editar, pero luego diagnosticó y corrigió sin relajar la aserción. El informe final contiene una afirmación falsa (altura del selector).

Pendiente para el siguiente bloque (pequeño): igualar la altura real del selector de producto en Touch (o sustituirlo por un control que la admita), restaurar el ancho de la columna SKU y alinear el texto de la prueba FE-08 con sus aserciones (o subirlas a 48). Después, QA humano de los 7 casos de `frontend/qa-visual.md`.

I2 sigue parcial: hardware térmico, monitor táctil, DPI 125/150/200 y aceptación humana (0/7). No avanzar a I3.

---

## Revisión del prompt C (Gemini 3.8 Flash) — 2026-10-19, 00:15

Verificado por inspección de código, medición de píxeles en las capturas y ejecución propia:

```bash
cmake --build build_release --parallel 4   # código 0, 0 advertencias
ctest --test-dir build_release             # 3/3
cmake --build build_debug --parallel 4     # código 0, 0 advertencias
ctest --test-dir build_debug               # 3/3
python -u tests/test_fe08_fe09_visual_integration.py   # código 0, Release y Debug
```

Es la primera vez en esta serie que ejecuto la prueba GUI FE-08 por mi cuenta.

| Punto | Resultado | Evidencia |
|---|---|---|
| 1. Altura del selector en Touch | **Aceptado** | `wxChoice` sustituido por `wxOwnerDrawnComboBox`. FE-08 propio: selector H=48 px, búsqueda H=48 px. En `02_pos_touch.png` ambos bordes van de y=167 a y=214. |
| 2. Columna SKU | **Aceptado** | BAG-01 y CAP-01 completos en 01 y 02; Cant. y Subtotal completos; sin barra horizontal. FE-08 propio: columna 0 = 84 px frente a un mínimo de 56 px. |
| 3. Coherencia FE-08 | **Aceptado** | Umbrales subidos a `>= 48` (líneas 433 y 463) sin fallos; nuevas aserciones estrictas del selector (tolerancia 2 px) y del ancho de la columna SKU (`LVM_GETCOLUMNWIDTH`). |

**Corrección del auditor:** en la revisión del prompt B califiqué de falsa la afirmación de que el selector medía 48 px. Lo juzgué a ojo en la captura completa, donde el borde gris claro del combo apenas se distingue. Medido en píxeles en la captura actual, la altura coincide. La captura del prompt B ya se sobrescribió y no puedo volver a medirla, así que aquella afirmación de Gemini probablemente era cierta. Desde ahora, las alturas y anchos se miden en píxeles o con la prueba geométrica, no a ojo.

Observaciones menores:
- Gemini compiló solo el objetivo `app_desktop`. CTest siguió siendo válido porque las pruebas C++ no dependen de `pos_panel`, pero conviene compilar todo.
- `capture_visual_qa.py` no aparece en su tabla de comandos, aunque las capturas 01 y 02 sí se regeneraron con el binario actual (00:10).

Desempeño del agente: alcance respetado, informe exacto en esta ocasión y casilla marcada correctamente.

Estado: los pendientes de software de la auditoría del 2026-10-18 quedan cerrados. I2 sigue **parcial solo por hardware** (impresora térmica 55/80 y monitor táctil), DPI 125/150/200 y aceptación humana (0/7 en `frontend/qa-visual.md`). No avanzar a I3 hasta el QA humano.

---

## Bloque B1 (Claude Code Terminal, opusplan) — 2026-11-08

Primer bloque con dos agentes en paralelo (ver `coordinacion-agentes.md`). Trabajado en el worktree `D:\Cafeteria_ITCH_backend`, rama `backend/b1`, commit `3931ba6`. Sin la skill caveman (línea base para comparar).

Plan revisado antes de ejecutar; aprobado con la opción A y cinco ajustes: `main_app.cpp` pasa `migrations_dir` en el mismo bloque; rechazo previo por `page_size`; rechazo de origen sin Dueño activo; invalidación del estado en memoria; auditoría `BACKUP_RESTORE_FAILED`. Los cinco quedaron implementados y probados.

Verificación propia: `cmake --build build_be_release|build_be_debug --clean-first` (código 0, 0 advertencias) y `ctest` 4/4 en ambos (BackupTest 4.12 s Release, 15.90 s Debug). Revisé el diff sin ruido de fin de línea (`git diff --ignore-cr-at-eol`): 627 líneas añadidas y 25 quitadas en 12 archivos más 2 nuevos; en `src/frontend/` solo la línea autorizada de `main_app.cpp`.

Resultado: **aceptado**.

Observaciones:
- `BACKUP_RESTORED` guarda como actor el usuario de la sesión previa, que puede no existir en la base restaurada (`audit_events.user_id` no tiene clave foránea, así que no falla). Trazabilidad mejorable más adelante.
- La recuperación desde el respaldo previo tras un fallo a mitad de copia está implementada, pero no probada: requeriría inyección de fallos.
- Claude reescribió los archivos con otro fin de línea (diff de archivo completo). Se corregirá con `.gitattributes` al integrar.
- El reporte decía 80 `TEST_CHECK`; el archivo tiene 102.
- Pendiente: prueba I1 con el ejecutable real tras integrar en `main` (B1 tocó `main_app.cpp`).
- No implementado: retención y limpieza de `backups/`, cifrado y envío remoto (fuera de alcance).
