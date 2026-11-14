# Auditoría independiente — 2026-10-18

## Alcance

Contraste de la entrega Sonnet/Gemini descrita en `Auditorias/auditoria-2026-10-14.md` (redactada por el propio agente ejecutor) contra el código actual, capturas en `qa_visual/screenshots/` y una compilación desde cero. Sin cambios a código de producción ni pruebas; solo documentación.

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
6. Documentación: `docs/specs.md` rotula I2 «SOFTWARE COMPLETADO Y AUDITADO»; los planes afirman «todos los requerimientos de software de I2 resueltos». No es cierto según esta tabla.

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

`Auditorias/auditoria-2026-10-18.md` (nuevo), `docs/specs.md`, `frontend/task.md`, `backend/task.md`, `docs/antigravity-resume.md`.

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

Siguiente: prompt B (`Prompts/prompts-antigravity-2026-10-18.md`).

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

Primer bloque con dos agentes en paralelo (ver `docs/coordinacion-agentes.md`). Trabajado en el worktree `D:\Cafeteria_ITCH_backend`, rama `backend/b1`, commit `3931ba6`. Sin la skill caveman (línea base para comparar).

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

---

## Bloque F1 (AntiGravity, Gemini 3.8 Flash) e integración — 2026-11-08

F1 trabajado en `main` (`D:\Cafetería_ITCH`) en paralelo con B1. Sin la skill caveman (línea base).

Verificación propia: `cmake --build build_release|build_debug` (código 0, 0 advertencias), CTest 3/3 en ambos, `tests/test_f1_roles_panel.py` y `tests/test_fe08_fe09_visual_integration.py` aprobados en Release y Debug. Captura 09 inspeccionada.

Resultado: **aceptado con correcciones pendientes (F1b)**. Commit `4104d29`.

Correcto:
- Panel sobre `IAccountService` sin SQL ni permisos calculados en la interfaz; `role-dev` y `backup.dev_config` ocultos; solo lectura sin `roles.manage`.
- La prueba GUI verifica persistencia tras reabrir, exclusiones y botones de roles de sistema deshabilitados.
- `CMakeLists.txt`: solo se añadieron sus dos archivos, según la excepción acordada.
- El script temporal `tests/scratch_check_rect.py` se borró antes de la entrega.

Falso en el informe: «geometría estricta sin recorte». En la captura 09, las columnas de «Roles Registrados» están recortadas (Descrip…, Si…, Pe) y hay barra horizontal: anchos fijos a ojo de 68/78/38/42 px (`backoffice_roles_panel.cpp:146-149`), contra lo pedido. Las etiquetas de permisos se envuelven a unos 270 px aunque sobra espacio, y la casilla queda alineada con la segunda línea. La prueba solo mide recorte en botones, no en columnas ni casillas.

Error del orquestador: el prompt F1 pidió roles de sistema en solo lectura. El servicio permite editar los permisos del Cajero (`role-cashier`, `is_system = 1`) y solo protege Dueño y Desarrollador; `docs/specs.md` dice que el Dueño configura los permisos. Pendiente de decisión del usuario.

Desempeño: arranque lento (unos 7 min compilando y corriendo CTest sin cambios, más 2 min de razonamiento antes de escribir). Dos fallos de su prueba GUI resueltos con diagnóstico, sin relajar umbrales. Unos 45 minutos en total.

### Integración en `main`

- `eff0fba`: merge de `backend/b1` sin conflictos.
- Verificación del conjunto: build sin advertencias, CTest **4/4** en Release y Debug (incluye BackupTest) y **`tests/test_i1_integration.py` aprobado con el ejecutable real**, que cierra el pendiente de B1 por el cambio en `main_app.cpp`.
- `99b773a`: `.gitattributes` con `eol=lf`; solo los documentos tenían CRLF, sin cambios de contenido.
- La copia del backend queda en la rama `backend/b2`, partiendo de `main`.

---

## Ronda 2 con caveman: B2, F1b e integración — 2026-11-08

### B2 (Claude Code Terminal, opusplan, caveman)

Plan revisado y aprobado con un cambio de fondo: pagos atribuidos al día de su venta (JOIN a `sales`), pagos huérfanos fuera del resumen; `payments_total` aprobado para que la interfaz no sume importes. Commit `e903633`.

Verificación propia: build sin advertencias y CTest 5/5 en Release y Debug (ReportsTest 1.23 s / 6.43 s). Diff de 31 líneas en archivos existentes más 3 nuevos; consultas por rango y JOIN; permiso antes de validar la fecha; sin `float`, `double` ni `SUM()`. **Aceptado.**

Límites declarados: sin anulaciones en el esquema (toda venta cuenta), horario de verano no manejado, solo resumen diario, rendimiento sin medir.

### F1b (AntiGravity, Gemini 3.8 Flash, caveman)

Commit `7aed41a`. Empezó a editar sin compilar antes, tal como pide la memoria operativa de `AGENTS.md`. Corrigió el panel (no la prueba) ante cada fallo de la prueba estricta. **Aceptado con observaciones menores.**

Correcto: Cajero con permisos editables y nombre, descripción y Eliminar bloqueados; Dueño en solo lectura; columnas Nombre, Sistema y Permisos completas sin barra horizontal; casillas en una línea con el texto completo (capturas 09 y 10). La prueba se reforzó (`LVM_GETCOLUMNWIDTH`, `WS_HSCROLL`, casillas, Cajero y Dueño).

Observaciones:
- El encabezado de la columna Descripción se ve recortado («Descripci…») aunque la prueba dice exigir encabezado más 8 px; probablemente lo mide con la fuente de la lista, no con la del encabezado. Sobran unos 25 px a la derecha de la lista.
- La persistencia del Cajero se verifica leyendo SQLite tras guardar, no cerrando y reabriendo la app como se pidió.
- La alineación del glifo solo se comprueba en etiquetas multilínea (hoy ninguna) y se omite si no detecta el glifo; la detección por píxeles es débil.
- En Touch, Nombre y Descripción tienen el texto alineado arriba en cajas de 48 px y las filas de la lista de roles siguen en unos 24 px.

### Integración

`8644088`: merge de `backend/b2` sin conflictos. Verificación del conjunto: build sin advertencias, CTest 5/5 en Release y Debug, y `test_i1_integration.py`, `test_f1_roles_panel.py` y `test_fe08_fe09_visual_integration.py` aprobados con el ejecutable real.

### Comparación con la ronda 1 (sin caveman)

| | Ronda 1 | Ronda 2 (caveman) |
|---|---|---|
| Claude, plan a entrega | B1: unos 30 min, bloque grande | B2: unos 10 min, bloque más pequeño |
| Claude, calidad del informe | Exacto (contó mal los `TEST_CHECK`) | Exacto y completo |
| Gemini, hasta primera edición | F1: unos 9 min (compiló sin cambios) | F1b: unos 2 min |
| Gemini, total | F1: unos 45 min | F1b: unos 45 min (más ciclos de prueba estricta) |
| Gemini, informe | Afirmación falsa de «sin recorte» | Coherente con las capturas, salvo detalles menores |

Conclusión provisional: la mejora visible viene de la memoria operativa de `AGENTS.md`, los prompts acotados y las pruebas estrictas; no hay evidencia separable de ahorro por caveman con dos bloques de tamaño distinto.

## Ronda 3 — B3a y F2 (auditado el 2026-11-12)

**F2 (AntiGravity, Bitácora):** aceptado en `1353162`. Pendientes menores: columna Detalle a ~58 px con ~500 px libres, texto arriba en Usuario y fechas (Estándar), subsección «Bitácora» sin resaltar.

**B3a (Claude Code Terminal, regla de dos pasadas):** aceptado en `9ff0e9e`, integrado en `1bb0a4d`.

- Alcance: solo `src/frontend/backoffice_roles_panel.{cpp,hpp}` y `tests/test_f1_roles_panel.py`.
- Panel: anchos calculados con la fuente real del encabezado (`ListView_GetHeader` + `WM_GETFONT`); Descripción toma el espacio restante; filas de 42 px en Touch con `wxImageList`; Nombre y Descripción centrados en contenedores de 48 px.
- Prueba: ninguna aserción relajada. Encabezado medido con su fuente; holgura de columnas ≤ 8 px; glifo de casilla comparado por centros (≤ 3 px) en las 20 casillas y falla si no detecta glifo o tinta; persistencia del Cajero tras reabrir; fila ≥ 40 px y campos centrados (≤ 3 px) en Touch.
- Primera ejecución del orquestador (2026-11-08): falló la aserción del glifo (`y=6 > 3`) porque comparaba bordes superiores; corregida por Claude a comparación de centros.
- Verificación del orquestador en el worktree: build Release/Debug 0 advertencias, CTest 5/5 en ambos, `test_f1_roles_panel.py` exit 0 en Release y Debug. Medidas: Estándar N=74 D=101 S=75 P=84 (cliente 336, holgura 2); Touch N=76 D=124 S=78 P=86 (cliente 366, holgura 2); sin `WS_HSCROLL`.
- Verificación integrada en `main`: build 0 advertencias, CTest 5/5 en Release y Debug, `test_i1_integration`, `test_f1_roles_panel`, `test_f2_audit_log`, `test_fe08_fe09_visual_integration` y `capture_visual_qa` exit 0. Capturas 09 y 10 regeneradas: encabezado «Descripción» completo.
- No verificado: `test_i2_integration.py` no se repitió (B3a no toca venta ni caja). DPI 125/150/200 sin probar.
- Observación cosmética (Touch): el campo deshabilitado se ve como un recuadro gris dentro del contenedor blanco con borde.

## Ronda 4 — B3b y F2b (auditado el 2026-11-12)

**B3b (Claude Code Terminal):** aceptado en `c71d586`, integrado en `af4e4bc`.

- Alcance: `src/frontend/ui_helpers.{hpp,cpp}` (nuevos), `CMakeLists.txt` (solo esas dos fuentes), `backoffice_roles_panel.{cpp,hpp}`, `audit_log_panel.cpp`, `tests/test_f2_audit_log.py`.
- Plan revisado antes de ejecutar, con tres ajustes aplicados: contenedor siempre que haya alto fijo (sin campo de prueba desechable), reparto de columnas idempotente sin estado por lista, y localización de campos por etiqueta en la prueba. Añadido `set_text_field_enabled` para el fondo del campo deshabilitado.
- Prueba de Bitácora endurecida, ninguna aserción relajada: encabezado con su fuente, holgura ≤ 8 px, sin `WS_HSCROLL`, tres campos centrados (≤ 3 px) y falla si no hay tinta.
- Medidas del orquestador (Release y Debug iguales): Estándar F=148 U=105 E=141 D=526 (cliente 922, holgura 2); Touch F=162 U=111 E=147 D=454 (cliente 876, holgura 2). Antes Detalle medía ~58 px.
- No verificado: Bitácora con barra vertical visible; el color del campo deshabilitado no lo mide ninguna prueba (en la captura 10 el contenedor y el campo ya se ven del mismo gris).

**F2b (AntiGravity):** aceptado en `dba641a`.

- Alcance: `src/frontend/subnav_bar.{cpp,hpp}` y `tests/test_f2b_subnav.py` (nuevo).
- Causa confirmada: `GetLabel().ToStdString()` contra el nombre UTF-8 no coincidía con acentos. Ahora cada botón guarda su nombre y `update_button_styles` pinta activo e inactivo.
- Comprobación del orquestador: con el código anterior la prueba falla (`Clic en Catálogo visual ... 0 activos`); con el arreglo pasa en Release y Debug.

**Verificación integrada en `main` (`af4e4bc`):** build 0 advertencias y CTest 5/5 en Release y Debug; `test_f1_roles_panel`, `test_f2b_subnav`, `test_fe08_fe09_visual_integration` y `capture_visual_qa` exit 0. Capturas 10 y 11 regeneradas: «Bitácora» resaltada, Detalle a todo el ancho, texto de filtros centrado.

- **Intermitencia observada:** en la primera pasada `test_i1_integration` y `test_f2_audit_log` (sesión 3, Debug) fallaron por tiempo de espera: tras enviar el inicio de sesión, la ventana principal no apareció en 12–15 s. Repetidas a solas pasaron (I1 dos veces, F2 una). Ningún bloque de la ronda toca el inicio de sesión. Causa no determinada; sospecha: pérdida de foco del escritorio durante la prueba. Queda como pendiente.
- No verificado: `test_i2_integration.py` no se repitió.

## Ronda 4 — B4a (auditado el 2026-11-12, PC nueva)

**B4a (Claude Code Terminal):** aceptado en `1fc6dc5` (rama `backend/b4a`), integrado en `2bdd6ae` tras F0.

- Alcance: `migrations/009_permission_titles.sql` (nuevo), `include/app/contracts/dto.hpp`, `src/services/sqlite_account_service.cpp`, `src/services/mock_services.cpp`, `tests/test_storage.cpp`. Nada fuera de lo permitido.
- Comprobación independiente del orquestador: migraciones 001 a 009 aplicadas con Python sobre una base en memoria y comparadas con la tabla aprobada (`Prompts/prompts-2026-11-12-ronda4.md`): 21 de 21 permisos iguales en sección, orden, título y descripción; `role_permissions` idéntico antes y después de la 009 (28 filas).
- `list_permissions` ordena por `sort_order`; filtros y cálculo de `module` sin cambios. Al mock le faltaba `backup.restore` (tenía 20); ahora devuelve los 21 y la prueba lo compara campo por campo con el servicio real.
- Pruebas nuevas en `StorageTest`: 32 `TEST_CHECK` (el informe dijo 31). Migran primero solo hasta la 008 para tener línea base real; ninguna aserción existente relajada.
- Verificación del orquestador en el worktree: CTest 5/5 en Release y Debug.
- **Efecto conocido al integrar:** `tests/test_f1_roles_panel.py` localiza casillas por el texto antiguo de la descripción ("Consulta de bitácora", "Venta y cobro", "Consulta de existencias"); con la 009 dejará de pasar hasta que F5 reescriba el panel y la prueba. `capture_visual_qa.py` puede verse afectado igual.
- No verificado: pruebas GUI; el panel de roles con los textos nuevos (trabajo de F5).
- Menor: ejecutar `test_storage.exe` desde la raíz de la copia deja `test_snapshot.bak.tmp-shm` y `-wal` en el directorio actual; alguna prueba escribe relativo al directorio de trabajo. Candidato para un bloque de backend.

## F0 (AntiGravity, Gemini 4 Hydrogen) — auditado el 2026-11-12, PC nueva

**F0:** aceptado; primer bloque con Gemini 4 Hydrogen.

- Alcance: exactamente los ocho `tests/*.py` permitidos; 8 inserciones y 20 eliminaciones, todas la línea del `PATH`. Ninguna aserción, espera ni umbral tocados. Búsqueda de rutas fijas en `tests/*.py`: 0.
- Informe literal: reportó 2 pruebas aprobadas y 4 fallidas dos veces, con el error textual, sin intentar arreglarlas.
- Resultados en esta PC (1920x1200, escalado 125 %, 120 DPI): `test_i1_integration` y `test_i2_integration` pasan (I1 repetida por el orquestador: exit 0, Release y Debug; I2 no repetida). Fallan dos veces, siempre en Release: `test_fe08_fe09_visual_integration` (campo de notas Touch 38 px < 40), `test_f1_roles_panel` (el clic no selecciona la fila), `test_f2_audit_log` (columnas 920 px > cliente 737 px) y `test_f2b_subnav` (no mide el color de fondo).
- **Causa (diagnóstico del orquestador, sin repetir las pruebas):** el proceso Python de las pruebas no declara conciencia de DPI y ve la pantalla virtualizada (1536x960), mientras la aplicación trabaja en píxeles físicos. Se mezclan los dos espacios: 920/737 = 1.25 y 48 px físicos / 1.25 = 38. Los umbrales en píxeles fijos tampoco escalan. Es un defecto del arnés de pruebas, nunca ejercitado a más de 100 %; no demuestra por sí mismo un defecto de la aplicación, pero tampoco lo descarta.
- Consecuencia: la batería GUI solo es válida al 100 %. Corresponde al pendiente de I2 «DPI 125/150/200 sin probar».
- **Confirmación a 100 %:** con la pantalla a 1920x1080 y escalado 100 % (96 DPI), el orquestador repitió las cuatro pruebas fallidas con el código de F0: `test_fe08_fe09_visual_integration`, `test_f1_roles_panel`, `test_f2_audit_log` y `test_f2b_subnav` terminan con exit 0 en Release y Debug. El diagnóstico de DPI queda confirmado y la línea base GUI en esta PC es válida a 100 %. `test_i2_integration` no se repitió (pasó a 125 % según Gemini).

**Verificación integrada en `main` (`2bdd6ae`, F0 + B4a):** build 0 advertencias y CTest 5/5 en Release y Debug; `test_i1_integration` exit 0 en ambos, a 100 % de escalado. No se corrió `test_f1_roles_panel` tras integrar: fallará por los textos nuevos hasta F5 (ver B4a). Tampoco `test_i2_integration`, `test_f2_audit_log`, `test_f2b_subnav` ni `test_fe08_fe09_visual_integration` tras la integración (B4a no toca venta, bitácora ni navegación).

## Ronda 4 — F5 (AntiGravity, Gemini 4 Hydrogen) — auditado el 2026-11-12

**F5:** aceptado. Interruptores con título y descripción en BackOffice > Roles y permisos.

- Alcance: `src/frontend/toggle_switch.{hpp,cpp}` (nuevos), `CMakeLists.txt` (solo esas dos fuentes), `backoffice_roles_panel.{cpp,hpp}`, `tests/test_f1_roles_panel.py` y las capturas regeneradas por `capture_visual_qa.py` (script sin tocar). Nada fuera de lo permitido.
- `ToggleSwitch`: dibujado propio con doble búfer, sin temporizadores; 44x24 y 60x32 por `FromDIP`; estado por posición de la perilla y marca; foco y Espacio; responde a `BM_GETCHECK`, `BM_SETCHECK` y `BM_CLICK`.
- Panel: cuatro grupos en el orden de la barra lateral desde `section` y `sort_order`; título en negrita y descripción secundaria con ajuste de línea; separador de 1 px; clic en toda la fila; contador junto a Guardar; filtro de `backup.dev_config` conservado; `get_module_display_name` eliminado.
- Prueba: 36 aserciones de casillas sustituidas por 57; ninguna relajada. Las de columnas de la lista (encabezado, holgura, sin `WS_HSCROLL`) se conservan. Los permisos se localizan por el título leído de SQLite. Se quitó `BS_TOP`, que solo aplicaba a `wxCheckBox`. La cadena de referencia de la columna Permisos pasó de "16" a "20".
- Verificación del orquestador a 1920x1080 y 100 %: build sin advertencias y CTest 5/5 en Release y Debug; `test_f1_roles_panel` exit 0 en ambos; `test_i1_integration` exit 0 en ambos. Medidas: Estándar interruptor 44x24, filas de 66 px o más (86 a 960x600); Touch 60x32, filas de 78 px o más (120 a 960x600). Columnas: Estándar N=74 D=101 S=75 P=84 (cliente 336, holgura 2); Touch N=76 D=124 S=78 P=86 (cliente 366, holgura 2).
- Capturas 09 y 10 revisadas: legibles, sin recortes ni solapes.
- **Observación de uso (no bloquea):** el área visible de permisos es baja. A 1200x720 caben unas 3,5 filas en Estándar y menos de 2 en Touch; hay que desplazarse por 20 permisos. Candidato a un bloque corto: dar más alto a la lista (por ejemplo, compactar Nombre y Descripción o desplazar toda la tarjeta).
- Menores: los márgenes y altos mínimos de fila (12/16, 52/68, 24) no pasan por `FromDIP`, solo el interruptor; dos colores del estado deshabilitado están fijos en `toggle_switch.cpp` porque `theme.*` no era editable. Ambos para el bloque de DPI.
- No verificado: Tab entre interruptores (sí Espacio con foco); escalados distintos de 100 %; `test_i2_integration`, `test_f2_audit_log`, `test_f2b_subnav` y `test_fe08_fe09_visual_integration` no se repitieron (F5 no toca esos paneles).
- Informe de Gemini 4 Hydrogen: literal; declaró el fallo inicial de compilación, las dos corridas fallidas de la prueba y su causa, y las aserciones cambiadas.

## Ronda 4 — B4 (Claude Code Terminal) — auditado el 2026-11-12

**B4:** aceptado en `112dc40`, integrado en `3bbf40b` por indicación del usuario.

- Alcance: migración `010_sync_readiness.sql`; nuevos `identifiers` (servicios), `clock` (dominio) y `random_bytes` (crypto, autorizado en la respuesta al plan); los 6 servicios SQLite, `sqlite_sale_service.hpp`, `CMakeLists.txt` (bibliotecas, no `app_desktop`), `tests/test_storage.cpp` y `tests/test_backup.cpp`. Sin cambios en contratos, frontend, `tests/*.py` ni migraciones 001 a 009.
- Ajustes del plan cumplidos: `generate_id` en servicios (el dominio no enlaza crypto), folio con el código de la caja del turno y calculado dentro de la transacción, respaldo de código limitado a 4 caracteres, cajas extra C2, C3... por rowid, cabecera de la 010 con el significado de UTC en NULL.
- Pruebas: 101 `TEST_CHECK` nuevos. Tres existentes cambiados, todos justificados: versión de esquema 9 a 10 (dos) y el alta del Dueño en la prueba de esquema v7, ahora por SQL porque el servicio escribe columnas que v7 no tiene. Ninguna relajada.
- Verificación del orquestador en el worktree: build sin advertencias y CTest 5/5 en Release y Debug.
- **Al integrar:** `tests/test_i2_integration.py` línea 980 busca `V-10001` y fallará (primer folio nuevo `V-C1-00001`); bloque corto de Gemini para leer el folio de la base. Tras integrar, correr build, CTest, I1 e I2 a 100 % de escalado.
- Pendientes de backend que deja a la vista:
  - `create_verified_snapshot` (`sqlite_backup_service.cpp`) no borra `<destino>.tmp-wal` y `-shm` en la ruta de éxito: origen de los archivos sueltos vistos en B4a.
  - `generate_next_folio` usa `substr(folio, 1, n) = ?`, que recorre toda la tabla `sales` en cada venta; cambiar a un rango sobre el índice único de `folio` antes de acumular muchas ventas.
  - Carrito y reintento idempotente siguen fijos a `reg-01`: con una segunda caja el folio sería correcto pero el carrito y el reintento no.
  - `generate_id` lanza `std::runtime_error` si no hay entropía y nadie lo captura (dentro de una transacción quedaría sin `ROLLBACK`); improbable en Windows.
- No verificado: flujo real en el ejecutable ni pruebas GUI con B4; rendimiento con muchas ventas.

**Verificación integrada en `main` (`3bbf40b`, F5 + B4):** build sin advertencias y CTest 5/5 en Release y Debug; `test_i1_integration` exit 0 en ambos, a 100 % de escalado. No se corrieron tras integrar: `test_i2_integration` (fallo esperado por el folio), `test_f1_roles_panel`, `test_f2_audit_log`, `test_f2b_subnav` ni `test_fe08_fe09_visual_integration`.

## Ronda 5 — F6 (AntiGravity, modo Orchestrator (Argon)) — auditado el 2026-11-13

**F6:** cambio aceptado, pero `test_i2_integration` NO queda verificada en verde. Primer bloque en el modo Orchestrator (Argon).

- Alcance: solo `tests/test_i2_integration.py`: búsqueda parametrizada por el folio real (`folio1`) en lugar del texto fijo `V-10001`, y aserción nueva de formato `V-<1 a 4 A-Z0-9>-<5 dígitos>` y de folios distintos entre ventas. Ninguna otra aserción tocada. Correcto por lectura.
- Se detuvo por su cuenta al detectar el escalado a 125 % y no ejecutó pruebas hasta que el usuario lo puso a 100 %.
- Su informe: las seis pruebas GUI pasan a la primera en Release y Debug a 100 %.
- **Verificación del orquestador: `test_i2_integration` falla 3 de 3** (dos veces desde su consola y una desde la terminal interactiva del usuario), siempre en el requerimiento 4 y 7, antes de llegar a las líneas de F6: tras pulsar Imprimir, `cancel_system_print_dialog_by_pid` no encuentra el diálogo (devuelve `None`) y el aviso «Impresión Cancelada» nunca aparece (tiempo de espera de 6 s).
- Causa probable: en esta PC (Windows 11) el diálogo de impresión se abre en un proceso aparte, `PrintDialog.exe` (arrancó durante la ejecución), y la prueba solo busca un diálogo `#32770` dentro del proceso de la aplicación. Impresora predeterminada: una impresora virtual protegida de OneNote. Además la prueba imprime «detectado y cancelado» aunque el identificador sea `None`: no lo comprueba.
- No se pudo conciliar con el informe de Gemini, que da I2 por aprobada minutos antes (su salida completa no era visible). Tampoco se verificó I2 de forma independiente en F0. Hasta aclararlo, **I2 no cuenta como verificada en esta PC**; las otras cinco pruebas no se repitieron en esta auditoría.
- Siguiente: bloque de diagnóstico para Gemini (ejecutar I2 a solas, mostrar la salida completa, comprobar el identificador del diálogo y localizarlo también cuando lo aloja `PrintDialog.exe`).
- Actualización: el usuario cambió la impresora predeterminada a Microsoft Print to PDF y la prueba falla igual (5 de 5 en total). La impresora predeterminada queda descartada como causa. Enviado el bloque F6b a Gemini (diagnóstico con datos y arreglo de la detección del diálogo).

## Ronda 5 — B5 (Claude Code Terminal) — auditado el 2026-11-13

**B5:** aceptado en `39ac304`, integrado en `a013186`. Primer bloque de backend de I3.

- Alcance: migraciones 011 (actor en movimientos y compras, índices, triggers de existencias no negativas) y 012 (valuación); `stock_valuation` (dominio) y `stock_ledger` (servicios), nuevos; contratos de inventario y catálogo; servicios de inventario, catálogo, ventas y respaldo; mock; `sqlite_database`; `tests/test_storage.cpp` y `tests/test_backup.cpp`. Nada en frontend, `tests/*.py`, crypto ni migraciones 001 a 010.
- Contratos: solo adiciones (el diff de `include/app/contracts/` no quita ninguna línea). Nuevos: `register_waste`, `register_adjustment`, `list_stock_movements`, `list_purchase_entries`, `get_ingredient_cost`, `get_recipe_costing` y sus DTO.
- Promedio móvil (decisión del usuario): revisado `stock_valuation.cpp`. Enteros de 128 bits, sin `float` ni `double`; compra suma el costo; salida resta en proporción; existencia 0 deja valuación exactamente 0; último promedio conservado; reglas (a) a (d) del anexo implementadas. `stock_ledger.cpp` es la única escritura de `current_stock` en `src/` (comprobado con búsqueda) y la usan compra, merma, ajuste y venta.
- Costos solo por `get_ingredient_cost` y `get_recipe_costing`, ambos con `inventory.manage_products`. `get_recipe` devuelve 0 en el campo obsoleto a quien no tiene el permiso.
- Comprobación independiente del orquestador (Python sobre SQLite en memoria, migraciones 001 a 012): ingrediente con compras de 1000 a 5000 centavos y 1000 a 10000 centavos y existencia 1200 queda con valuación 9 000 000 000 micro-centavos y último promedio (15 000 000 000, 2000), igual al cálculo a mano; sin compras queda sin costo; con existencia 0 conserva el promedio; el trigger rechaza existencias negativas.
- Pruebas: 229 líneas `TEST_CHECK` nuevas. Cuatro existentes cambiadas: las que fijaban la versión 10 comparan ahora con la migración más alta de la carpeta. No es una relajación del comportamiento, pero deja de detectar una migración borrada por error.
- Pendientes de B4 resueltos: folio por rango sobre el índice único y respaldo sin archivos temporales residuales (con prueba de restauración).
- Verificación del orquestador en el worktree: build sin advertencias y CTest 5/5 en Release y Debug.
- Observaciones: `__int128` es propio de GCC y Clang (no compila con MSVC); `list_purchase_entries` muestra el costo de cada compra a quien tiene `inventory.restock` (pedido así para Reabastecimiento) y la bitácora ya mostraba el costo de las compras a quien tiene `audit.view`; el relleno de la 012 es aproximado y está documentado.
- No verificado: ejecutable real, pruebas GUI y rendimiento con muchas ventas. Sigue pendiente el carrito y el reintento fijos a `reg-01`.

## Ronda 5 — F6b (AntiGravity, modo Orchestrator (Argon)) — auditado el 2026-11-13

**F6b:** mejora aceptada, pero `test_i2_integration` sigue en rojo en el escritorio real del usuario.

- Alcance: solo `tests/test_i2_integration.py` (+247/-28): detección del diálogo de impresión clásico (`#32770` en el proceso) y del moderno de Windows 11, cancelación con espera de cierre, y fallo explícito si no se encuentra o no se cierra. Ya no imprime «detectado» con identificador `None`.
- **Causa de la discrepancia con F6, explicada por Gemini y coherente con lo observado:** AntiGravity ejecuta los comandos en un escritorio aislado (`WinSta0\exebox-*`), donde Windows muestra el diálogo clásico dentro de la aplicación; en el escritorio interactivo del usuario aparece el diálogo moderno, alojado fuera del proceso. Por eso a Gemini le pasaba y al orquestador no. No fue un informe falso.
- Informe de Gemini: 3 corridas seguidas con exit 0 en su escritorio aislado, folio `V-C1-00001`; declaró que la rama del diálogo moderno no pudo ejercitarla.
- **Verificación del orquestador en el escritorio interactivo (100 %): 2 de 2 fallan**, ahora con un error claro: el diálogo «Impresión desde la aplicación Win32 - Impresión» (`explorer.exe`) se detecta, pero no se cierra tras la cancelación (tiempo de espera). La detección funciona; la cancelación del diálogo moderno no.
- Consecuencia: en esta PC las pruebas GUI dan resultados distintos según dónde se ejecuten. Los «pasó» de AntiGravity valen para su escritorio aislado; los pasos que abren diálogos del sistema hay que repetirlos en el escritorio real. El resto de I2 (venta, folio, reimpresión) no se alcanza en el escritorio real porque la prueba se detiene antes.
- Siguiente (F6c): cancelar de forma fiable el diálogo moderno. Gemini no puede reproducirlo en su entorno, así que la validación la hace el orquestador o el usuario en el escritorio real. Alternativa a valorar con el usuario: un gancho de prueba que evite el diálogo del sistema.

**Verificación integrada en `main` (`a013186`, B5 + F6/F6b):** build sin advertencias y CTest 5/5 en Release y Debug; `test_i1_integration` exit 0 en ambos, a 100 % de escalado. No se corrió el resto de la batería GUI con B5 integrado; `test_i2_integration` sigue fallando en el escritorio real por el diálogo de impresión.

## Ronda 5 — B6 (Claude Code Terminal) — auditado el 2026-11-13

**B6:** aceptado en `ae8c464`, integrado en `409e974`.

- Alcance: migración 013 (registro `backup_files`, política en `backup_state`, tabla `backup_server_config`); `operating_context` (servicios), nuevo; contrato `backup_service.hpp`; servicios de carrito, ventas y respaldo; mock; `tests/test_storage.cpp` y `tests/test_backup.cpp`. Nada en frontend, `tests/*.py`, crypto ni migraciones 001 a 012.
- Contratos: solo adiciones (el diff de `include/app/contracts/` no quita ninguna línea). Nuevos: `get_retention_policy`, `set_retention_policy`, `preview_backup_cleanup`, `cleanup_old_backups` y sus DTO.
- Caja real: revisado `operating_context.cpp`. Orden aprobado: turno propio; si no, el único turno abierto; si hay varios ajenos, error en lenguaje llano; sin turnos, la venta da `ShiftClosed` y el carrito usa la única caja o pide abrir turno. Ya no queda `"reg-01"` en `src/` ni `include/` salvo el mock y `cash_shift_panel.cpp` (frontend).
- Limpieza de respaldos: revisado el bucle de decisión. Protege el respaldo más reciente que coincide con su registro (tamaño y SHA-256) y pasa la verificación de integridad; si ninguno es válido no borra nada de ese tipo; conserva por cantidad (30) y por antigüedad (90 días), decisión del usuario; un archivo cambiado se marca `CHANGED` y no se borra; un borrado fallido se anota y no detiene a los demás. Exige `backup.manage` y deja `BACKUP_CLEANUP` en la bitácora. Solo bajo petición, con vista previa.
- Configuración del servidor: persistida y vacía al migrar; el servicio real ya no arranca con una URL inventada.
- Pruebas: 130 líneas `TEST_CHECK` nuevas; ninguna existente eliminada ni cambiada.
- Verificación del orquestador en el worktree: build sin advertencias y CTest 5/5 en Release y Debug.
- Pendientes que deja a la vista: `get_current_shift` y `close_shift` siguen tomando el turno abierto más reciente de cualquier usuario (con dos cajas, cerrar turno podría cerrar el de otra): bloque propio antes de permitir una segunda caja. El hash de un respaldo lee el archivo completo en memoria. Los respaldos anteriores a B6 no están registrados y no se limpian. La prueba de borrado fallido depende de un bloqueo de archivo de Windows. `backup_jobs` (migración 001) sigue sin uso.
- No verificado: ejecutable real y pruebas GUI con B6; no está integrado porque F7 está en curso en `main`.

## Ronda 5 — F7 (AntiGravity, Gemini 4 Hydrogen XHigh) — auditado el 2026-11-13

**F7:** aceptado. Pantalla Inventario > Ingredientes, primera de I3. Ensayo del esfuerzo XHigh (sin Orchestrator). Commit `6415c8a`.

- Alcance: `src/frontend/inventory_ingredients_panel.{hpp,cpp}` (nuevos, 998 líneas), `tests/test_f7_ingredients_panel.py` (nuevo, 1039 líneas, 51 aserciones), dos fuentes en `CMakeLists.txt`, include y rama en `main_window.cpp`, capturas 12 y 13 en `capture_visual_qa.py` (de lo existente solo quitó un comentario). Nada fuera de lo permitido.
- Código: usa solo contratos (`list_ingredients`, `get_stock_alerts`, `create_ingredient`, `update_ingredient`, `update_low_stock_threshold`); sin SQL; sin `float` ni `double`; estado de poco inventario tomado de `get_stock_alerts`; columnas con `ui::distribute_list_columns`; conversión del aviso con comprobación de desbordamiento.
- Prueba: alta de los tres tipos con valores exactos en SQLite (500 g guarda 500000), edición y persistencia tras reabrir, nombre vacío y repetido, unidades amigables (1.5 kg, 250 g, 2.5 L, 7 pz, 450 mg), estados, Cajero en solo lectura sin alterar SQLite, ausencia de jerga y geometría en Estándar y Touch a 1200x720 y 960x600.
- Verificación del orquestador en el escritorio real a 100 %: build sin advertencias y CTest 5/5 en Release y Debug; `test_f7_ingredients_panel` exit 0 en ambos, con las mismas medidas que reportó Gemini; `test_f2b_subnav` exit 0. (Una primera ejecución de `test_f2b_subnav` falló por una opción de codificación que añadió el orquestador al lanzarla; sin ella pasa.)
- Medidas: Estándar 1200x720 columnas 220/89/161/117 (cliente 589), filas 24, botones 36, campos 34. Touch 1200x720 columnas 187/92/167/122 (cliente 570), filas 43, botones y campos 48.
- Capturas 12 y 13 revisadas: legibles, sin recortes ni solapes; lenguaje llano.
- Observaciones (no bloquean): los anchos mínimos de las tarjetas (472/488 y 202) son píxeles fijos elegidos para que quepan las columnas a 960x600, sin `FromDIP`; una existencia de 0 se muestra «0 mg» (sigue la regla del encargo, pero convendría «Sin existencia» o «0 g»); el texto de la lista es verde en todas las filas «Suficiente»; las capturas no muestran ninguna fila con «Poco inventario»; sigue existiendo la subsección Barra.
- Hallazgo útil para otras pruebas: Gemini detectó que la fuente obtenida con `WM_GETFONT` no es utilizable desde otro proceso (la medición caía en la fuente del sistema) y midió cargando las fuentes Roboto del proyecto. Las pruebas anteriores que miden texto con `WM_GETFONT` conviene revisarlas.
- Informe: literal. Declaró cinco fallos intermedios de su prueba con causa y arreglo, sin relajar aserciones.

**Medición del ensayo XHigh (parcial):** la sesión de AntiGravity se cerró a mitad de la verificación y se reanudó con un prompt de continuación. Total declarado por Gemini: 5 compilaciones por configuración, 6 ejecuciones de la prueba de la pantalla (5 fallidas y 1 buena), 2 de CTest por configuración, 1 de navegación y 1 de capturas. Cuota de Gemini: 100 % al empezar; el valor final lo anota el usuario. Comparación pendiente: F8 en Hydrogen high y F9 en Orchestrator (Argon).

**Verificación integrada en `main` (`409e974`, F7 + B6):** build sin advertencias y CTest 5/5 en Release y Debug; `test_i1_integration` y `test_f7_ingredients_panel` exit 0, a 100 % de escalado en el escritorio real. No se corrió el resto de la batería GUI; `test_i2_integration` sigue pendiente por el diálogo de impresión moderno.

## Ronda 6 — T1 (AntiGravity, Gemini 4 Hydrogen high) — auditado el 2026-11-13

**T1:** aceptado. Primer bloque de I7 fuera de Venta: perfil en BackOffice > Configuración, diálogos de acceso en Táctil y prueba válida con cualquier escalado.

- Alcance: `src/frontend/backoffice_settings_panel.{hpp,cpp}` (nuevos, 275 líneas), `tests/test_t1_touch_profile.py` (nuevo, 1045 líneas), `qa_visual/touch_audit.txt` (nuevo), dos fuentes en `CMakeLists.txt`, `main_window.{hpp,cpp}`, `header_panel.{hpp,cpp}`, `login_dialog.cpp`, `initial_owner_dialog.cpp` y capturas 14 a 17 en `capture_visual_qa.py` (solo añade al final). Nada fuera de lo permitido; no tocó `main_app.cpp`, `app.rc`, `theme.*` ni `backoffice_print_config_panel.*`. Las capturas 01 a 13 cambian solo por haberse regenerado.
- Código: la pantalla de Configuración apila la tarjeta «Pantalla de este equipo» y la pantalla de impresión sin modificarla, con desplazamiento. Elegir una opción llama a `Theme::set_profile` (que guarda `station_profile.cfg`) y avisa a `MainWindow`, que reconstruye con `CallAfter`, como se pidió para no usar el panel destruido. El refresco del perfil quedó en una sola función (`apply_profile_refresh`) que usan el encabezado y Configuración. La opción activa se distingue por texto («(Activa)», «Opción activa ahora»), icono y borde, no solo por color. Los diálogos ya no tienen tamaño fijo: medidas con `FromDIP`, `SetSizerAndFit` y tamaño mínimo; en Táctil cada campo va en un contenedor de 48 de alto con el texto centrado; el orden, la clase y los textos de los controles no cambian. Los mensajes de error ahora se ajustan al ancho y el diálogo crece si hace falta.
- Prueba: consciente de DPI por monitor; cada umbral se multiplica por DPI/96. Casos a–f con aserciones sobre recorte de texto (medido con la fuente), contención en el área cliente y en el área de trabajo, solapamiento entre hermanos, alturas 48/56, fuente de cuerpo y centrado vertical por tinta; archivo de perfil, barra lateral de 56, sincronía encabezado–pantalla en los dos sentidos, carrito conservado en pantalla y en SQLite, y perfil recordado al reabrir.
- Verificación del orquestador en el escritorio real a 100 % (96 DPI): build sin advertencias y CTest 5/5 en Release y Debug; `test_t1_touch_profile` exit 0 con las mismas medidas que reportó Gemini; y toda la batería GUI salvo I2: `test_i1_integration`, `test_f2b_subnav`, `test_fe08_fe09_visual_integration`, `test_f1_roles_panel`, `test_f2_audit_log` y `test_f7_ingredients_panel`, todas exit 0.
- Medidas (lógicas): alta del Dueño 446x429 en Estándar (campos 23, botón 42) y 550x511 en Táctil (campos 48, botón 56); inicio de sesión 386x309 y 446x340, con las mismas alturas.
- Inventario táctil (`qa_visual/touch_audit.txt`, 1200x720, 93 controles, 30 marcados): botones de subsección de 44 de alto en todas las secciones; campo de fondo inicial de Caja y turnos de 44; lista desplegable de Bitácora de 29; las dos listas desplegables de impresión de 23. Sin solapamientos. Es la lista de trabajo de T2.
- Capturas 14 a 17 revisadas: legibles, sin recortes ni solapes, lenguaje llano.
- Límites y observaciones (no bloquean): **no verificado a 125, 150 ni 200 %**: la prueba está preparada, pero esta PC estaba a 100 %; falta correrla con otro escalado. El inventario mide las pantallas tal como abren (sin fila seleccionada ni formularios de detalle abiertos) y no mide filas de listas ni los diálogos de cobro y ticket: T2 debe ampliarlo. `make_dialog_text_field` está duplicada en los dos diálogos. La opción Estándar usa el icono `manage_accounts`, que no representa ratón y teclado. En Estándar los diálogos miden 14 px menos de ancho y 11 a 31 menos de alto que antes.
- Informe: literal; declaró sus dos fallos intermedios (una compilación y un índice en el guion de capturas). 14 minutos, 2 compilaciones en Release y 1 en Debug, 1 ejecución de cada prueba.

## Ronda 6 — B7 (Claude Code Terminal) — auditado el 2026-11-13

**B7:** aceptado. Paquete portable, política de la carpeta de datos, autocomprobación, versión, guion de instalador (sin compilar) y ficha de la PC de caja. Commit en `backend/b7`; **sin integrar** porque T2 está en curso en `main`.

- Alcance: `CMakeLists.txt` (dos fuentes en `app_services`, versión, `install()` y CPack; no toca la lista de fuentes de `app_desktop`), `tests/CMakeLists.txt`, `src/frontend/main_app.cpp` (solo carpeta de datos y línea de comandos), `src/frontend/app.rc` (un `#include`), y nuevos `cmake/package_runtime.cmake`, `cmake/version.rc.in`, `cmake/windows_system_dlls.txt`, `data_dir_policy.{hpp,cpp}`, `package_self_check.{hpp,cpp}`, `tests/test_data_dir_policy.cpp`, `tests/test_package.cpp`, `packaging/cafeteria.iss`, `packaging/licencias/` y `tools/ficha_pc.{ps1,cmd}`. Nada fuera de lo permitido.
- Carpeta de datos (`resolve_data_dir`, función pura con callbacks): orden `--data-dir`, `CAFETERIA_DATA_DIR`, desarrollo (solo `build_release` y `build_debug`), portable (`portable.flag` junto al ejecutable) e instalada (`%ProgramData%\CafeteriaITCH\data`). Ya no existe el salto silencioso a la carpeta del usuario: si la carpeta no se puede crear o escribir, mensaje en lenguaje llano con la ruta y la aplicación no arranca. Si la carpeta elegida no tiene base y la otra ubicación sí, tampoco arranca y muestra las dos rutas. Cambio de comportamiento: la carpeta explícita ahora se comprueba escribiendo y borrando un archivo de prueba.
- Autocomprobación: `--self-check` se atiende antes de inicializar wxWidgets y termina con `_Exit` (0 correcto, 1 falló, 2 no se pudo escribir el informe); usa una carpeta temporal propia que siempre borra; compara migraciones encontradas con aplicadas y cada archivo del manifiesto generado al empaquetar.
- Empaquetado: solo Release y sin ganchos de prueba; las DLL se calculan al instalar leyendo las importaciones de forma recursiva con el `objdump` del toolchain, contra una lista explícita de 19 DLL de Windows; el ejecutable se renombra a `CafeteriaITCH.exe` solo al instalar; `portable.flag` va en un componente aparte que el instalador no usa.
- Instalador (`cafeteria.iss`): revisado por lectura. Solo 64 bits, mínimo Windows 7 SP1, datos en `{commonappdata}` con permiso de modificación para Usuarios y `uninsneveruninstall`, sin `[Run]`, se niega a instalar sobre una copia portable. **No compilado ni ejecutado.**
- Verificación del orquestador en el worktree: build sin advertencias en Release y Debug; CTest 7/7 en Release (PackageTest 22 s) y 6 pasan más PackageTest omitida en Debug; `cpack -G ZIP` produce `CafeteriaITCH-0.1.0-portable-win64.zip` de 10.8 MB con 48 archivos, sin DLL de depuración, bases ni `.cfg`; `cmake --install` desde Debug se rechaza. Con el ZIP descomprimido fuera del repositorio y el `PATH` reducido a las carpetas de Windows: autocomprobación con código 0 (40 de 40 archivos, 13 de 13 migraciones); sin `migrations/`, código 1 con el motivo; una opción desconocida termina sin dejar ventana; el arranque normal crea `data\cafeteria.db` junto al ejecutable. La ficha (`ficha_pc.ps1`) corre en esta PC, anexa la autocomprobación y no contiene el nombre de usuario ni identificadores de cuenta. `%ProgramData%\CafeteriaITCH` no existe antes ni después de todo lo anterior.
- No verificado: el instalador; la rama instalada con la aplicación real (solo la función pura); Windows 7, 8.1 y 10; PowerShell 2.0 (la ficha solo corrió en 5.1); USB protegida y PowerShell bloqueado por directiva; el empaquetado con `CAFETERIA_TEST_HOOKS=ON` en un árbol reconfigurado; las pruebas GUI con B7 (se corren al integrar).
- Observaciones (no bloquean): un ejecutable sin `portable.flag` fuera de `build_release` y `build_debug` (por ejemplo el de `build_be_release` o una copia suelta) usa `%ProgramData%` si se abre sin `--data-dir`: es lo decidido, pero conviene recordarlo al probar a mano. La lista de DLL de Windows afirma compatibilidad con Windows 7 por nombre de DLL, no por función importada. El informe de la autocomprobación muestra la ruta completa del paquete (la ficha enmascara el usuario; el informe suelto no). La ficha incluye el modelo del disco. `paquete.manifest` se genera desde la carpeta de fuentes, no desde lo instalado. Las licencias de wxWidgets, del runtime de GCC y de winpthreads viajan marcadas como PENDIENTE: no entregar a terceros hasta incluirlas.
- Informe de Claude: exacto en cifras (48 y 47 archivos, 10.8 MB, 7/7) y honesto en límites; declaró por su cuenta dos fugas de datos de cuenta en la primera versión de la ficha y las corrigió.

