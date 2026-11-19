# Auditoría T14b — cierre de T14 (corte con arqueo, historial, Reportes CSV y Existencias B16)

Ronda 12 — T14 + T14b (Gemini 4 Hydrogen, en `main`) — auditado el 2026-11-18 / 2026-11-19 al 125 % (120 DPI)

**Veredicto: T14 + T14b aceptado con observaciones (al 125 %).** Con este bloque, **I4 queda completo en software** (falta la pasada al 100 % y la revisión humana). Queda un seguimiento prioritario fuera del alcance de archivos de T14b: un *use-after-free* preexistente en `src/frontend/numeric_keypad.cpp` al convivir con `DestroyChildren()` en 7 paneles táctiles (ver sección 5, seguimiento #1).

## 1. Alcance y propiedad de archivos

`git status -sb` y `git diff --ignore-cr-at-eol --stat` sobre `main` (`a17f451`):

- Código y compilación: `CMakeLists.txt` (+2, solo fuentes nuevas), `src/frontend/cash_shift_panel.hpp` (+132), `src/frontend/cash_shift_panel.cpp` (+1399), `src/frontend/reports_panel.hpp` (nuevo), `src/frontend/reports_panel.cpp` (nuevo), `src/frontend/inventory_stock_panel.hpp` (+6), `src/frontend/inventory_stock_panel.cpp` (+69), `src/frontend/main_window.cpp` (+4).
- Pruebas y QA visual: `tests/test_t14_closeout_reports.py` (nuevo), `tests/test_t4_numeric_keypad.py` (+8), `tests/test_t6_backups.py` (+11), `tests/test_t2_touch_targets.py` (+44), `tests/capture_visual_qa.py` (+96), `qa_visual/touch_audit.txt` y capturas `qa_visual/screenshots/` (incluyendo `34_cash_closeout.png`, `35_cash_closeout_history.png` y `36_reports.png`).
- Todos los archivos pertenecen a la lista permitida de `Prompts/prompts-2026-11-18-T14.md` y `Prompts/prompts-2026-11-18-T14b.md`. No se tocaron `main_app.cpp`, contratos, servicios ni migraciones.
- **Pruebas existentes revisadas por diff:** `test_t4_numeric_keypad.py`, `test_t6_backups.py`, `test_t2_touch_targets.py` y `capture_visual_qa.py` no relajan ni eliminan ninguna aserción; únicamente localizan los campos de arqueo tras fijar el botón en el pie de `closeout_card_` y añaden las vistas/capturas de T14.

## 2. Verificación de los cuatro puntos de T14b en el código

1. **Columnas de «Cortes anteriores» y altura de `hist_movements_list_`:**
   - `CashShiftPanel::switch_tab` (`src/frontend/cash_shift_panel.cpp:774-787`) ejecuta `Layout()` antes de `load_history()` y refuerza con `CallAfter` tras `Layout()` para ajustar `history_list_` y `hist_movements_list_`.
   - `fit_list_columns` (`:819-863`) y `ReportsPanel::fit_columns` (`src/frontend/reports_panel.cpp:454-490`) delegan en `ui::distribute_list_columns`, recortando la columna flexible si una barra vertical transitoria excede `client_w`.
   - `hist_movements_list_` salió de `hist_detail_scroll_` (`cash_shift_panel.cpp:670-756`) con `SetMinSize` adaptado (`88`/`92` lógicos) y `hist_detail_scroll_` acotado (`74`–`98` lógicos), evitando recorte vertical y barra horizontal en 1200×720 y 960×600 (Estándar y Táctil).
2. **Botón «Cerrar Turno de Caja» fijo en pie de `closeout_card_`:**
   - `lbl_close_feedback_` y `btn_close_shift_` se colocaron fuera de `closeout_scroll_` en el pie de `closeout_card_` (`cash_shift_panel.cpp:530-543`), visibles y tocables en Táctil 1200×720 (`test_t4_numeric_keypad.py` pasa en Release y Debug).
3. **«Efectivo contado» vacío al inicio:**
   - `txt_final_cash_` inicia vacío con pista `"Ej. 500.00"` (`cash_shift_panel.cpp:504-505`) y `lbl_diff_preview_` permanece oculto hasta capturar un valor (`:511,1050-1080`), con `FitInside()` + `Layout()` en `on_final_cash_changed` (`:1101-1110`).
4. **Endurecimiento de `tests/test_t14_closeout_reports.py` (sin relajar nada):**
   - Rechazo a Cajero (`:755-761`): comprueba la cadena exacta de `src/services/sqlite_shift_service.cpp:569` («Ese turno lo abrió otra persona. Solo quien lo abrió, el Dueño o un rol con permiso para cerrar turnos de otros puede cerrarlo.») en las etiquetas y en el diálogo si entrega texto.
   - Segundo turno «Cuadra» (`:822-827`): verifica en SQLite `(0, 20000, 20000, 0, "")`.
   - Existencias B16 (`:613,628,637,640-646,1007-1026`): siembra «Agua Botella T14» (directo con 3 piezas ≤ umbral 5) y verifica en pantalla `('Directo', '3 piezas', 'Poco', db_last_agua)` contra `MAX(created_at)` en SQLite.
   - Comprobación vertical de `hist_movements_list_` (`>= 90 px`) en 1200×720 y 960×600 (`:844-848`), bloque 8 (7 reportes + CSV BOM/coma/Conflict) y corrida completa en Release y Debug.
   - Sin `double` ni `float`; sin llamadas a `close_shift(Money)`.

## 3. Capturas medidas con PIL (`qa_visual/screenshots/`, 125 %, 120 DPI)

| Captura | Dimensiones / bytes | Medición PIL y verificación visual |
|---|---|---|
| `31_inventory_stock.png` | `1518×947`, 83 425 B | 17.5 % contenido no blanco; preparados muestran `stock_status` («Suficiente») y `last_movement_at` real de B16. |
| `34_cash_closeout.png` | `1518×947`, 92 340 B | 14.8 % contenido; movimientos a ancho completo y «Cerrar Turno de Caja» fijo al pie de `closeout_card_`. |
| `35_cash_closeout_history.png` | `1518×947`, 68 326 B | 13.1 % contenido; `history_list_` con divisores en `523, 722, 921, 1016, 1104, 1243, 1467` (**181, 199, 199, 95, 88, 139, 224 px** = **1125 de 1128 px, 99.7 %** del ancho, antes 49.7 %); `hist_movements_list_` con divisores en `431, 513, 598, 1465` (**89, 82, 85, 867 px** = **1123 de 1128 px, 99.6 %**), ambas filas visibles y sin recortes. |
| `36_reports.png` | `1518×947`, 51 805 B | 7.9 % contenido; 8 columnas de «Ventas por día» con divisores en `830, 906, 991, 1097, 1182, 1267, 1344, 1475`, sin barra horizontal y fila Total en negrita. |

## 4. Verificación completa del conjunto en `<copia principal>` (`main`, 125 %, 120 DPI)

Con `<toolchain>\mingw64\bin` y `<toolchain>\wxwidgets\lib\gcc1610_x64_dll` al inicio del `PATH`, comprobando `tasklist` (0 `app_desktop.exe`, 0 `ctest.exe`) antes de cada corrida y ejecutando una prueba GUI a la vez.

### Compilación y CTest

| Comando | Código de salida | Resultado observado |
|---|---|---|
| `cmake --build build_release --parallel 1` | 0 | Al día, 0 advertencias. |
| `cmake --build build_debug --parallel 1` | 0 | Al día, 0 advertencias. |
| `cmake --build build_hooks_release --parallel 1` | 0 | Recompiló `cash_shift_panel.cpp` y `reports_panel.cpp` y enlazó `app_desktop.exe`, 0 advertencias. |
| `(Get-MpThreatDetection).Count` + `Test-Path ctest.exe` (antes) | 0 | Defender = **37**; `ctest.exe` = `True`. |
| `ctest --test-dir build_release --output-on-failure` | 0 | **21/21 aprobadas** (248.90 s). |
| `ctest --test-dir build_debug --output-on-failure` | 0 | **18/21 aprobadas + 3 omitidas** esperadas en Debug (`PackageTest`, `InstallerTest`, `EndSessionTest`; 340.69 s). |
| `(Get-MpThreatDetection).Count` + `Test-Path ctest.exe` (después) | 0 | Defender = **37** (0 detecciones nuevas); `ctest.exe` = `True`. |

### Batería GUI completa (18/18 pruebas + capturas, al 125 %)

| Prueba | Código de salida | Resultado observado |
|---|---|---|
| `tests/test_t14_closeout_reports.py` (corrida 1) | 0 | Aprobada en Release y Debug. |
| `tests/test_t14_closeout_reports.py` (corrida 2, seguida) | 0 | Aprobada en Release y Debug. |
| `tests/test_t4_numeric_keypad.py` | 0 | Aprobada en Release y Debug. |
| `tests/test_t6_backups.py` | 0 | Aprobada en Release y Debug. |
| `tests/test_t2_touch_targets.py` | 0 | Aprobada en Release, Debug y HooksRelease (`marcados=0` en 36 vistas). |
| `tests/test_t12_discounts_stock.py` | 0 | Aprobada en Release y Debug. |
| `tests/test_t13_promotions_combos.py` | 0 | Aprobada (94.5 s). |
| `tests/test_t11_products_recipes.py` | 0 | Aprobada (87.7 s). |
| `tests/test_i2_integration.py` | 0 | Aprobada (53.4 s). |
| `tests/test_i1_integration.py` | 0 | Aprobada (10.6 s). |
| `tests/test_t10_restock_bar.py` | 0 | Aprobada (88.7 s). |
| `tests/test_t1_touch_profile.py` (tras `test_t10`) | 0 | Aprobada (36.7 s). |
| `tests/test_f7_ingredients_panel.py` | 0 | Aprobada (34.7 s). |
| `tests/test_f1_roles_panel.py` | 0 | Aprobada (195.9 s). |
| `tests/test_f2b_subnav.py` | 0 | Aprobada (123.5 s). |
| `tests/test_t7_updates.py` | 0 | Aprobada (64.4 s). |
| `tests/test_fe08_fe09_visual_integration.py` | 0 | Aprobada (31.2 s). |
| `tests/test_t5_fullscreen.py` | 0 | Aprobada (32.6 s). |
| `tests/capture_visual_qa.py` | 0 | 36 capturas generadas (99.0 s). |
| `tests/test_f2_audit_log.py` | **1 / 0** (intermitente) | Falló en batería y al repetir directo (Release línea 574 ×3; Debug línea 637 ×1 con `excepthook`); **aprobó completo en Release y Debug** en dos ejecuciones completas (`f2.main()` y `run_f2_test_for_binary` con DPI). Causa analizada abajo (seguimientos #1 y #2). |

### Análisis de `test_f2_audit_log.py` y hallazgo de memoria en `NumericKeypad`

Al investigar por qué `test_f2_audit_log.py` falló repetidamente justo al pasar de `Caja y turnos` (`CashShiftPanel`, en perfil Touch) a `Bitácora`, se identificaron dos causas distintas fuera de los archivos editables de T14b:

1. ***Use-after-free* real al destruir paneles táctiles (`src/frontend/numeric_keypad.cpp:52-76,113-115` + `DestroyChildren()`):** `NumericKeypad` enlaza tres lambdas que capturan `[this]` crudo sobre `host_` (`wxEVT_SHOW` en `:53`, `wxEVT_SIZE` en `:59` y `wxEVT_DESTROY` en `:69-75`), pero `~NumericKeypad()` (`:113-115`) **no los desenlaza**. Cuando un panel llama `DestroyChildren()` en su destructor derivado (`cash_shift_panel.cpp:126` añadido en T14 siguiendo T6d, y ya presente en `backoffice_backups_panel.cpp:327`, `pos_panel.cpp:50`, `inventory_bar_panel.cpp:137`, `inventory_products_panel.cpp:215`, `inventory_promotions_panel.cpp:258` e `inventory_restock_panel.cpp:163`), `keypad_` se destruye y libera **antes** de que `~wxWindow()` dispare `wxEVT_DESTROY` en `host_`. Al dispararse `wxEVT_DESTROY`, el lambda ejecuta `uninstall_filter()` y escribe `nullptr` sobre el objeto `NumericKeypad` ya liberado. Esto puede corromper memoria o cerrar inesperadamente la ventana en perfil Táctil (compatible también con el cierre `IsWindow=0` observado antes en `test_t6_backups.py` en `Auditorias/auditoria-2026-11-17-B14.md`).
2. **Carrera de esperas fijas en `tests/test_f2_audit_log.py:566-574,627-637`:** tras hacer clic en `Caja y Reportes` y luego en `Bitácora`, la prueba usa `time.sleep(0.8)` fijos sin sondeo antes de leer `p_list = [...][0]` y `warn_lbl`. Con el `CashShiftPanel` más grande de T14 en Touch, el cambio de vista puede superar o competir con esa ventana fija.

## 5. Lo que no se verificó

- **Pasada al 100 % de escalado (96 DPI):** pendiente para T8–T14 cuando el usuario cambie el escalado de Windows.
- **Revisión humana** de I4 (corte con arqueo, historial y los siete reportes + apertura del CSV en Excel).

## 6. Seguimientos (numerados para el orquestador)

1. **Prioritario (seguridad de memoria, bloque corto antes del piloto): corregir el *use-after-free* en `src/frontend/numeric_keypad.cpp:52-76,113-115`.** Guardar los manejadores o usar una bandera compartida (`std::shared_ptr<bool>`) / `Unbind` en `~NumericKeypad()` para que `wxEVT_SHOW`, `wxEVT_SIZE` y `wxEVT_DESTROY` en `host_` nunca toquen un `NumericKeypad` destruido por `DestroyChildren()` en los 7 paneles táctiles (`cash_shift_panel`, `backoffice_backups_panel`, `pos_panel`, `inventory_bar_panel`, `inventory_products_panel`, `inventory_promotions_panel`, `inventory_restock_panel`).
2. **Carrera de prueba en `tests/test_f2_audit_log.py:566-574,627-637`:** sustituir los `time.sleep(0.8)` fijos al navegar `Caja y Reportes` → `Bitácora` por esperas por sondeo (con la misma aserción de lista/aviso) para que `test_f2_audit_log.py` sea determinista en Release y Debug.
3. **Pasada al 100 % (96 DPI):** repetir la batería GUI al 100 % (T8 a T14) cuando el usuario cambie el escalado.
4. **Cobertura opcional de casos límite de I4 en UI:** `payments_mismatch_cents != 0` y `snapshot_saved == false` están implementados (`cash_shift_panel.cpp:964-970,1361-1372`, `reports_panel.cpp:395-403`) y revisados por lectura, pero no sembrados en `test_t14_closeout_reports.py`.
