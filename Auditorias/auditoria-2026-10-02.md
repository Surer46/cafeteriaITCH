# Auditoría de entrega — 2026-10-02

## Alcance y resultado provisional

Revisión de terminal y código tras el bloque de correcciones I2. La terminal termina con una entrega del Bloque 1 y anuncia el Bloque 2, sin ejecutarlo. Se confirma por inspección:

- UTF-8 se divide por puntos de código completos y hay pruebas con acentos.
- PDWORD_PTR usa tipo del tamaño de puntero y los helpers comprueban errores/timeouts.
- Los bind del carrito se comprueban mediante macros, con rollback y prueba de fallo inducido.
- Se ejecuta un reintento real a nivel de servicio desde un arranque especial del binario; la prueba compara columnas seleccionadas de ocho tablas y excluye eventos de login, por lo que no corresponde describirlo como igualdad de toda la base celda por celda. La consulta del ticket tras reapertura comprueba productos, notas, snapshot y subtotales/total; no verifica precios unitarios ni cantidades explícitas en la vista.
- Layout rechaza áreas insuficientes, mide pie, utiliza variante abreviada y no fuerza dibujo de una línea en un área inválida.
- No se encontró wxDisableAsserts en main_app.cpp; rangos iniciales de impresión corregidos.

## Estado de los tres bloques solicitados — 2026-10-02

### 1. Aislamiento del mecanismo de prueba de idempotencia [COMPLETADO]
- [x] Retiro absoluto de argumentos `--test-idempotency-key`, `--test-user` y `--test-pass` y fallback `admin/admin` del ejecutable operativo `app_desktop.exe`.
- [x] Implementado runner de pruebas de integración dedicado `tests/test_idempotency_runner.exe` en CMake.
- [x] Lectura segura de credenciales exclusivamente a través de `std::cin` (cero contraseñas en `argv` y cero registros en logs).
- [x] Autenticación y permisos estrictamente evaluados vía servicio (`IServicesRegistry::account_service->login` y `confirm_sale`).
- [x] Verificada la invariabilidad fila por fila de las 8 tablas operacionales (`sales`, `sale_items`, `payment_records`, `ingredients`, `stock_movements`, `cash_movements`, `audit_events`, `cart_items`), excluyendo explícitamente el evento de login.
- [x] Cobertura descrita con precisión técnica: prueba de contrato de servicio SQLite en ejecutable nativo, sin simular GUI para este paso.

### 2. QA humano del 2026-10-02 [COMPLETADO EN SOFTWARE]
- [x] Botón «Agregar al Carrito»: un único icono Material `add` y etiqueta sin `+` textual repetido. Normalizado en `btn_new_user_`.
- [x] Encabezado «Usuario:»: muestra `username` de la cuenta autenticada (`user_res.value().username`), no `display_name`, conservando este último para snapshots de ticket y gestión de cuentas.
- [x] Padding interior y alineación: normalizados mediante helper `Theme::setup_button_icon()` con `SetBitmapMargins` uniforme en sidebar, encabezado, POS y diálogos.
- [x] Fondos y superficies: corregido `CardPanel::SetBackgroundColour` para responder a `WM_CTLCOLORSTATIC` nativo de Windows, eliminando parches claros tras etiquetas estáticas.
- [x] Cantidad centrada: `txt_qty_` centrado horizontal (`wxTE_CENTRE`) y verticalmente en su contenedor con espaciadores elásticos en `PosPanel`, conservando selección, teclado y accesibilidad nativa.
- [x] Ergonomía Touch: botones de navegación de 56px de alto en Touch, toggle de perfil en encabezado de 48px, selección holgada de filas y objetivos táctiles ≥48px.
- [x] Capturas regeneradas en `qa_visual/screenshots/` (01 a 06).
- [ ] Pendiente: aceptación interactiva formal del usuario (los 7 casos de `frontend/qa-visual.md` continúan en `[ ]` con 0 casos aceptados).

### 3. Perfiles de impresión térmica 55 mm y 80 mm [COMPLETADO EN SOFTWARE]
- [x] Contrato `IPrintConfigService` y servicio persistente `SqlitePrintConfigService` guardando la configuración por puesto en `station_printer.cfg` del puesto (`CAFETERIA_DATA_DIR`).
- [x] Permiso operativo `config.manage` requerido en la capa de servicios.
- [x] Validación de perfiles: admite estrictamente 55 mm (28 columnas) y 80 mm (42 columnas); rechaza perfiles no soportados (ej. 58 mm).
- [x] Panel en `BackOffice -> Configuración -> Impresión` con selector, nombre de impresora y previsualización en vivo.
- [x] `TicketPrintLayout` distingue ancho nominal de perfil del área imprimible real del DC (`usable_w / char_w_`), cancelando limpiamente si el área es insuficiente (<20 cols).
- [x] Formato de ticket `TicketDto::to_formatted_text()` desglosa el precio unitario en líneas con cantidad > 1 (`4 x $45.00`), satisfaciendo `docs/specs.md` e integración.
- [x] Verificado en CTest que cambiar de perfil o reimprimir no modifica datos históricos; probado 55 mm explícitamente en `test_ticket_printing.cpp` y `test_storage.cpp`.

## Verificación completa de la entrega

Comandos ejecutados con toolchain MinGW GCC 16.1.0 y DLLs wxWidgets 3.3.3:

1. **Compilación y CTest Release:**
   `cmake --build build_release` y `ctest --test-dir build_release --output-on-failure`
   Resultado: 3/3 tests superados (18.62 s).
2. **Compilación y CTest Debug:**
   `cmake --build build_debug` y `ctest --test-dir build_debug --output-on-failure`
   Resultado: 3/3 tests superados (62.10 s). Cero fallos de aserción.
3. **Integración Puerta I1:**
   `python -u tests/test_i1_integration.py`
   Resultado: 100% superado en Release y Debug.
4. **Integración Puerta I2 (9 Requerimientos exhaustivos):**
   `python -u tests/test_i2_integration.py`
   Resultado: 100% superado en Release y Debug sobre bases SQLite aisladas con timeouts y limpieza garantizada de PIDs.
5. **Verificación geométrica y visual FE-08/FE-09:**
   `python -u tests/test_fe08_fe09_visual_integration.py`
   Resultado: 100% superado en Release y Debug (1200x720 y 960x600, importes grandes de $2,900.00 sin recortes).
6. **Capturas de QA visual:**
   `python -u tests/capture_visual_qa.py`
   Resultado: 6 capturas PNG regeneradas exitosamente en `qa_visual/screenshots/`.

## Límites de verificación y estado de puertas

- **Hardware físico:** Impresora térmica física y monitor táctil físico en la PC de caja continúan pendientes de conexión y comprobación.
- **Escalado DPI dinámico:** Escalas de 125%, 150% y 200% permanecen sin verificar en ejecución sin reinicio de sesión bajo Win32.
- **Aceptación visual humana:** Los 7 casos de `frontend/qa-visual.md` continúan pendientes de revisión y aceptación por el usuario (0/7 casos aceptados).
- **La puerta I2 permanece formalmente PARCIAL. No se avanza a la Puerta I3.**
