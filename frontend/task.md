# Pendientes y estado de frontend — 2026-09-29

## Seguimiento vigente

### Aislamiento de prueba, QA visual 2026-10-02 y perfiles térmicos 55/80 mm — 2026-10-02

- [x] **Aislamiento del runner de idempotencia:** Argumentos `--test-*` y fallback `admin/admin` retirados de `app_desktop.exe`. Pruebas de integración usan `test_idempotency_runner.exe` vía `stdin` con autenticación y permisos reales.
- [x] **QA Visual 2026-10-02 — Botón Agregar al Carrito:** Etiqueta limpia «Agregar al Carrito» con único icono Material `add` sin `+` textual repetido. Normalizado en `btn_new_user_`.
- [x] **QA Visual 2026-10-02 — Encabezado Usuario:** Muestra `username` de la cuenta autenticada (`Usuario: <username>`) mediante consulta a `IAccountService`, preservando `display_name` en administración de cuentas y snapshot de tickets.
- [x] **QA Visual 2026-10-02 — Padding y alineación de iconos:** Método auxiliar `Theme::setup_button_icon()` implementado para normalizar márgenes de bitmap (`SetBitmapPosition(wxLEFT)` y `SetBitmapMargins`) en sidebar, encabezado, POS y diálogos.
- [x] **QA Visual 2026-10-02 — Fondos de tarjetas y etiquetas:** `CardPanel::SetBackgroundColour` asigna el color de superficie de la tarjeta para que controles estáticos (`WM_CTLCOLORSTATIC`) hereden el fondo sin parches claros.
- [x] **QA Visual 2026-10-02 — Cantidad centrada:** Campo `txt_qty_` centrado horizontal (`wxTE_CENTRE`) y verticalmente mediante panel contenedor con espaciadores elásticos en `PosPanel`, preservando selección, teclado y accesibilidad.
- [x] **QA Visual 2026-10-02 — Ergonomía Touch:** Botones de sidebar de 56px de alto en Touch, botón de perfil en encabezado de 48px, selección holgada de filas en cuadrículas y objetivos táctiles interactivos ≥48px.
- [x] **Perfiles térmicos 55 mm y 80 mm:** Creado panel `BackofficePrintConfigPanel` en `BackOffice -> Configuración -> Impresión` con previsualización en vivo, conectado a `IPrintConfigService`. `TicketPrintLayout` distingue ancho nominal (28 cols para 55mm, 42 cols para 80mm) de área útil real del DC y aborta limpiamente si el área es insuficiente (<20 cols).
- [x] **Capturas regeneradas:** Actualizadas capturas 01 a 06 en `qa_visual/screenshots/` mediante `tests/capture_visual_qa.py`.
- [ ] Aceptación interactiva del usuario: los 7 casos de `frontend/qa-visual.md` continúan pendientes (0 casos aceptados).
- [ ] Hardware físico: impresora térmica y monitor táctil en PC de caja pendientes. Puerta I2 permanece **PARCIAL**.

### Geometría de impresión y formato dinámico de tickets — 2026-09-30

- [x] FE-03: `TicketPrintLayout` (`src/frontend/ticket_layout.hpp`/`.cpp`) implementa un único sistema de coordenadas (`MapScreenSizeToPage` y `GetLogicalPageRect`) para calcular páginas y dibujar en `wxPrintout` (`TicketPrintout`).
- [x] FE-03: medición métrica real en DC (`dc.GetTextExtent`) con cálculo dinámico de columnas físicas (`usable_w / char_w`) en vez de asumir 40 caracteres fijos.
- [x] FE-03: reserva estricta de espacio para pie de página (`footer_reserved_h_ = line_height_ * 2`) y cota superior de contenido, demostrando ausencia total de solapamiento (`content_bottom_y < footer_y`).
- [x] FE-03: paginación precalculada antes de `GetPageInfo()`, garantizando consistencia absoluta entre `GetPageInfo()`, `HasPage()` y `OnPrintPage()`. Cada línea aparece exactamente una vez a lo largo del documento.
- [x] FE-03: `TicketDto::to_formatted_text()` soporta anchos dinámicos ($\ge 20$ cols), envoltura completa sin recortes silenciosos `...` para cafetería, cajero, productos y notas, y soporte íntegro de importes grandes y cantidades grandes (modo multilínea con importe alineado a la derecha respetando `utf8_column_width <= max_cols`).
- [x] FE-03: suite CTest `test_ticket_printing` (`tests/test_ticket_printing.cpp`) agregada a CTest (3/3 superadas en Release y Debug). Cubre tickets de 1 página, multipágina, áreas estrechas de 58 mm, áreas de 80 mm, altura reducida y ciclo de vida wxPrintout.
- [ ] FE-03: validación física en la impresora térmica real de la PC de caja (avance de papel, corte de guillotina y legibilidad térmica). La puerta I2 permanece parcial en hardware.

### Verificación geométrica rigurosa y corrección de recortes — 2026-09-30

- [x] QA-V04/06: subtotal y total en perfil Estándar verificados sin recortes con `GetTextExtentPoint32W` y Win32 rects (ancho de control 135px vs extensión de texto 64px para `$2,900.00`, margen libre a la derecha de la tarjeta de 16px). Se ajustaron las columnas de `list_items_` (suma 555px), se fijó el ancho de la tarjeta en 310px y se garantizó el área cliente con `SetClientSize(wxSize(1200, 720))`.
- [x] QA-V07: en `02_pos_touch.png` el campo de nota `txt_notes_` se amplió a fila completa exclusiva (608px de ancho útil, 48px de alto táctil). Las acciones del carrito se dividieron en 2 filas: acciones de línea en Fila 1 y `Más acciones ▾` (160×48px) en Fila 2, sin compresión ni recorte de texto. Se ajustaron las columnas de `list_items_` (605px) haciendo visible el encabezado `Subtotal`. Todos los objetivos táctiles cumplen con el estándar ≥48px.
- [x] BE-08/FE-09: suite `tests/test_fe08_fe09_visual_integration.py` actualizada con verificación geométrica estricta de límites Win32 (`child.right <= container.right - pad`, `child.left >= container.left + pad`), medición métrica de glifos (`control_w >= text_extent_w`), matriz completa en 1200×720 y 960×600 con importes grandes ($2,900.00 / 116 unidades) y notas largas en ambos perfiles, detección de DPI (100% verificado activamente; 125/150/200% registrados como `[NO VERIFICADO]` por limitaciones del entorno Win32). Aprobada al 100% en Release y Debug. Capturas regeneradas en `qa_visual/screenshots/`. Los casos humanos de `qa-visual.md` continúan en `[ ]` (0 aceptados) hasta inspección del usuario.

### Revisión posterior al rediseño — 2026-09-30

- [x] Auditoría repitió `python -u tests/test_i1_integration.py`: código 0 en Release y Debug, alta/cancelación, persistencia y reapertura/login; el bloqueo anterior de arranque no se reprodujo. CTest e I2 de esta entrega se revisaron por salida/logs de Gemini, no se repitieron en esta revisión.
- [x] QA-V04/06: corregida distribución de Cuenta actual en `PosPanel` (ancho 340px Estándar / 380px Touch, `AddStretchSpacer(1)` y alineación a la derecha); probado con importes grandes ($2,900.00 / 116 unidades) con margen libre ≥24px verificado automáticamente sin recortes. `btn_ticket_history_` reubicado dentro de `Más acciones ▾` para eliminar recortes en fila inferior. Botones contextuales (`Editar cantidad`, `Editar nota`, `Quitar línea`) dinámicos por selección. Distinción explícita de `Quitar línea` vs `Cancelar pedido`.
- [x] QA-V02: resuelto fallo del texto en `StatusBadge`; sustituido `wxGraphicsContext::DrawText` por GDI nativo en DC (`dc.SetFont`, `dc.SetTextForeground`, `dc.DrawText`) ya que GDI+ no resolvía la fuente privada "Roboto" en `PrintWindow`. Dimensiones mínimas fijadas a 28px/34px; verificado >250 píxeles verde oscuro detectados en capturas Win32.
- [x] FE-08: persistencia de perfil por puesto en `station_profile.cfg` dentro de `CAFETERIA_DATA_DIR` sin perder el carrito (comprobado con 116 artículos en SQLite y arranque recordado). Reorganización de distribución Touch con steppers `[−]`/`[+]` de 48×48px, campo de notas de 48px, botón primario `+ Agregar al Carrito` de 56×210px y botón `Cobrar` hero de 56px de alto. Separación de 8–14px y cero dependencia de hover/doble clic. Validación física en pantalla táctil real permanece pendiente.

### Dirección visual solicitada — 2026-09-30

- [x] Implementar FE-09: tema nativo compartido Cupertino / ITCH II (`src/frontend/theme.hpp`/`.cpp`), tipografía Roboto local en memoria, 20 iconos Material SVG cargados y tintados en memoria con caché por DPI, `CardPanel`, `StatusBadge`, encabezado con estado en vivo y toggle Estándar/Touch, barra lateral burdeos `#991B1B` con píldoras de sección, subnav segmentada, controles contextuales del POS (`Editar cantidad`, `Editar nota`, `Quitar artículo`, `Más acciones...`, `Historial / Reimpresión`), diálogo de cobro y diálogo de impresión estilizados. Corrección de manifiesto MinGW (`cmake/no-default-manifest.specs`) y soporte comctl32 v6.0. Compilación en Debug y Release con CTest 2/2, I1 e I2 superados en ambos binarios, y 6 capturas generadas para QA visual en `qa_visual/screenshots/`. Los casos humanos de `qa-visual.md` y los hallazgos de integridad de `auditoria-2026-09-30.md` permanecen pendientes.

### Auditoría independiente — 2026-09-30

Consultar `../auditoria-2026-09-30.md`. Edición, historial y wxPrinter están implementados; el script I2 no acciona esas rutas, solo agrega, cobra y elige Ahora no. Quedan pruebas gráficas de edición/historial tras reapertura, permisos, cancelación/error y ticket largo sin recortes. La impresión anuncia una sola página sin ajuste de notas al ancho/alto imprimible. No considerar cerrado software ni pendiente solo hardware.

### Cierre de integridad y funcionalidad I2 — 2026-09-29

- [x] Validación estricta de efectivo: en `CheckoutDialog` y `CashShiftPanel`, una entrada inválida invalida inmediatamente el importe anterior, pone `cash_received_` a 0 e inhabilita el botón Confirmar/Abrir. `Money::try_parse` rechaza letras, símbolos, múltiples puntos, >2 decimales, desbordamientos y negativos no permitidos.
- [x] Endurecimiento de pruebas gráficas: `tests/test_i1_integration.py` y `tests/test_i2_integration.py` filtran ventanas exclusivamente por PID (`GetWindowThreadProcessId`), usan `PostMessageW` para evitar bloqueos en ShowModal, aplican timeouts con diagnóstico y terminan únicamente sus procesos en `finally`. Ambos pasaron al 100% en Release y Debug.
- [x] Edición de carrito en UI: botones en `PosPanel` para editar cantidad y notas de líneas existentes en la cuadrícula del carrito; fusiona líneas al coincidir las notas y elimina líneas si cantidad=0.
- [x] Historial de tickets y reimpresión: creado `TicketHistoryDialog` para consultar folios, ver detalles y abrir `TicketPrintDialog` para reimprimir.
- [x] Impresión nativa: integrado `wxPrinter` / `wxPrintout` (`TicketPrintout`) con ajuste de página y manejo de cancelación/fallo sin duplicar la venta.
- [ ] Validación física en hardware de caja: falta conectar y comprobar la salida física en la impresora térmica real. La puerta I2 permanece parcial en hardware.

Revisión de código y compilación actual. El detalle ejecutable está en `plan.md`; el orden compartido en `../specs.md`, sección 12. Las notas del 2026-09-25 al final son históricas. Los paneles compilados no equivalen a una demostración completa del flujo desde la ventana.

- [x] FE-01: alta inicial incluye correo, contraseña y confirmación; navegación nativa de cuatro secciones y subsección técnica filtrada.
- [x] FE-02: `PosPanel` conecta búsqueda, agregado con nota, edición de cantidad/nota, quitar, cancelar y consulta de carrito a servicios SQLite; contador y total se actualizan desde DTO.
- [x] FE-03: diálogo de cobro con validación estricta de centavos, pregunta Imprimir/Ahora no, vista previa del ticket recuperado por folio, reimpresión vía `TicketHistoryDialog` e integración de impresión nativa wxWidgets.
- [x] FE-06: Usuarios y cuentas permite alta con correo/rol/avatar opcional, listado, cambio de contraseña, estado y consulta de avatar.
- [x] Compilación Debug/Release y CTest: 3/3 en ambas (`ContractsTest`, `StorageTest`, `TicketPrintingTest`), en directorios aislados; detalles y comandos en `../backend/task.md`.

## Pendientes, en orden de integración

- [x] FE-03: Sustituir `ToDouble` y cálculo de dinero en UI por parsing exacto de centavos (`Money::try_parse`) y cálculos devueltos por el servicio; eliminar fondo automático de $500 y requerir apertura explícita de turno desde *Caja y turnos* (`CashShiftPanel`) con fondo elegido y registro en `cash_movements`; el encabezado refleja el estado real del turno dinámicamente.
- [x] FE-02/03: Generar clave de idempotencia contextual en `PosPanel`, deshabilitar botón Cobrar durante la confirmación para bloquear dobles clics y reintentar con la misma clave en caso de fallo transitorio.
- [x] FE-02: Editar cantidad y notas de líneas existentes directamente desde la cuadrícula del carrito, con fusión automática de líneas al coincidir las notas.
- [x] FE-03: Diálogo de historial de tickets para reimpresión autorizada (`TicketHistoryDialog`) e integración con subsistema de impresión nativo (`wxPrinter`/`wxPrintout`). `TicketPrintLayout` (`src/frontend/ticket_layout.hpp`/`.cpp`) con sistema único de coordenadas (`MapScreenSizeToPage`), medición métrica en DC, columnas dinámicas ($\ge 20$), reserva estricta de pie de página sin solapamiento, paginación precalculada consistente y formateo íntegro en `TicketDto::to_formatted_text()` sin truncamiento `...`. Mensajes de diálogo distinguen envío al spooler vs comprobación de salida en papel físico, manejo de cancelación/error sin anular ni duplicar venta, y restricción estricta de `pos.reprint` en servicios. Probado en CTest `TicketPrintingTest`.
- [ ] FE-03: Verificación física de la salida en impresora térmica real de la PC de caja una vez conectado el hardware.
- [ ] FE-06: confirmación de contraseña en alta de empleado; editar correo/nombre/rol/avatar, vista previa e iniciales; construir Roles y permisos y completar filtrado de todas las subsecciones.
- [ ] FE-04/05/06: sustituir pantallas preparatorias de Inventario, Caja y Reportes, Configuración y Respaldos por sus flujos reales.
- [x] FE-08 (parcial software verificado): distribución reorganizada para Venta en perfil Touch, controles táctiles mínimos de 48px (steppers `[−]`/`[+]`) y botones hero de 56px (`Agregar`, `Cobrar`), separación cómoda de 8–14px, acciones contextuales dinámicas y persistencia por puesto en `station_profile.cfg` conservando el carrito al 100%. Validación en monitor táctil físico real permanece pendiente.
- [x] FE-09 (correcciones visuales verificadas): eliminación de recortes en Cuenta actual para importes grandes, renderizado nítido de `StatusBadge` en GDI nativo, y agrupación contextual de acciones del carrito; verificado al 100% con ejecutable real en `tests/test_fe08_fe09_visual_integration.py` en Release y Debug.
- [ ] FE-07/08: accesibilidad, DPI, teclado, ergonomía, rendimiento y hardware de destino; demostrar puertas I2–I7 en ejecutable real.
- [x] Repetir I1 con `python -u tests/test_i1_integration.py`: comprobado al 100% en Release y Debug, verificando cancelación, alta obligatoria de Dueño, persistencia en SQLite, bloqueo de login en reapertura, rechazo de contraseña incorrecta y MainWindow sin menú técnico en bases temporales aisladas con aislamiento de ventanas por PID.
- [x] Ejecutar `tests/test_i2_integration.py` con `python -u`: comprobado al 100% en Release y Debug. Valida con el ejecutable nativo real: alta de Dueño, navegación a Caja y turnos, apertura explícita de turno con $350.00, manejo asíncrono del diálogo modal "Turno Abierto", catálogo, 3 líneas agregadas con notas distintas ("sin cebolla", "con extra picante" en línea separada, "con poco hielo"), cobro en efectivo con cálculo exacto en centavos ($200 recibidos, $155 total, $45 cambio), modal de impresión con pregunta explícita «¿Deseas imprimir el ticket?», selección de «Ahora no», venta persistida en SQLite con folio `V-10001`, 3 líneas con notas exactas, pago declarado, deducción automática de insumos en inventario por recetas, movimiento `INITIAL_CASH` y `SALE` en `cash_movements`, snapshot inmutable del cajero (`cashier_name`), vaciado de carrito y auditoría `SALE_CONFIRMED`.
- [ ] QA visual humano: seguir `qa-visual.md`, que inicia con 0 casos aceptados, y registrar retroalimentación/aceptación por pantalla. Preparar catálogo aislado para revisar venta y ticket completos.

Corrección de lanzamiento del 2026-09-29: `src/frontend/main_app.cpp` lee variables/rutas como Unicode nativo y registra `--data-dir` en wxWidgets. `tests/test_startup_paths.py` pasó tres escenarios por binario (entorno, argumento con `=`, argumento separado) en Debug/Release: diálogo inicial, SQLite en la ruta solicitada con acentos/espacios/ñ, migraciones Unicode y cancelación sin cuenta. Esto verifica arranque y rutas, no todo I1 ni ergonomía visual. El comando de `qa-visual.md` ya usa el Release corregido.

## Histórico de evidencia y pendientes hasta el 2026-09-25

Estado anterior registrado tras BE-02 y la revalidación de I1. Se conserva la evidencia; algunas carencias históricas (script I1 ausente, botones sin conectar, contador fijo) ya tienen avance y su estado vigente es el de arriba.

---

### Histórico 1. Comprobado con el ejecutable real (Debug y Release)

- [x] **Arranque del binario nativo (0xC0000139 resuelto):**
  - Se confirmó que el cargador NT fallaba por la falta de manifiesto Common-Controls v6 al enlazar `wxmsw333u_core_gcc1610_x64.dll` (`GetWindowSubclass`).
  - Resuelto con `src/frontend/app.rc` (`wxUSE_RC_MANIFEST 1`). Binarios `build_debug/app_desktop.exe` y `build_release/app_desktop.exe` inician sin error.
- [x] **Flujo de primer inicio obligatorio:**
  - El ejecutable abre en primer plano la ventana nativa `Primer Inicio — Alta de Cuenta Dueño`.
  - Si el usuario cancela o cierra la ventana, la aplicación termina de inmediato (código de salida 255) y la tabla `users` queda vacía (0 usuarios creados).
- [x] **Alta de Dueño y persistencia:**
  - Envío de formulario vía controles Win32 nativos con credenciales dinámicas generadas en memoria (sin claves hardcodeadas).
  - Creación del usuario con rol `Dueño` (`role-owner`), hash PBKDF2 y estado activo en SQLite.
  - Apertura exitosa de la ventana principal nativa `Cafetería ITCH II`.
- [x] **Reapertura y autenticación obligatoria:**
  - Al iniciar de nuevo el ejecutable, la base de datos detecta la existencia del Dueño y bloquea el acceso mostrando `Iniciar Sesión — Cafetería ITCH II`.
  - Una contraseña errónea es rechazada y el diálogo permanece bloqueando la aplicación.
  - La contraseña correcta autentica la sesión y permite el despliegue de `MainWindow`.
- [x] **Seguridad de interfaz y menú técnico:**
  - Inspección del árbol de controles de la ventana principal: la subsección `BackOffice → Servidor de respaldos` **no** está presente en la interfaz para la cuenta Dueño.
- [x] **Aislamiento de pruebas:**
  - Las pruebas de integración operan en directorios temporales aislados (`CAFETERIA_DATA_DIR`), sin borrar ni sobreescribir `build_debug/data/cafeteria.db` ni `build_release/data/cafeteria.db`.

---

### Histórico 2. Pendientes registrados al 2026-09-25

- [ ] **Reproducibilidad de I1 y aviso de enlace:**
  - Incorporar al repositorio la prueba automatizada de I1 citada en `backend/task.md` para repetir alta, reapertura y login tras cambios de código. La compilación Debug/Release del 2026-09-25 terminó con código 0 y CTest 2/2 en ambas variantes, pero el enlazador mostró `.rsrc merge failure: multiple non-default manifests`; verificar que el manifiesto final sigue siendo correcto al ejecutar de nuevo la ventana real.
- [ ] **Punto de venta aún demostrativo:**
  - Los botones de búsqueda/Agregar/Cobrar/Cancelar no están conectados a ventas reales; el contador «3 artículos en carrito» es fijo. Falta reemplazar `MockCartService` y `MockSaleService` y completar la puerta I2.
- [ ] **Inspección visual y ergonomía en monitor real:**
  - Comprobar proporciones, espaciados y contraste de colores (burdeos institucional, fondo gris claro `#F0F2F5`, botones activos `#B4141E`).
  - Verificar nitidez y escalado de fuentes en pantalla con escalado DPI activo.
- [ ] **Navegación interactiva por secciones:**
  - Recorrer manualmente con mouse o pantalla táctil las 4 secciones de la barra lateral (Punto de Venta, Inventario, Caja y Reportes, BackOffice) y verificar la transición fluida de las subsecciones horizontales.
- [ ] **Identidad Desarrollador en interfaz:**
  - La protección a nivel de servicios y filtrado de botones está verificada; falta diseñar el flujo en pantalla para autenticación técnica de Desarrollador sin credenciales maestras predeterminadas (requerido para habilitar visualmente la pestaña técnica de respaldos).
- [ ] **Validación en hardware de destino:**
  - Comprobar periféricos (pantalla táctil de caja, gaveta de dinero e impresora térmica) una vez definida la PC física de la cafetería.

### Auditoría de la última entrega visual — 2026-09-30

- [x] Inspección independiente de las capturas actuales de Venta en Estándar y Touch: subtotal y total completos; texto de Más acciones visible; notas en una fila amplia en Touch y acciones repartidas en dos filas.
- [ ] Completar revisión Touch fuera de las acciones de Venta: las capturas aún muestran controles compactos en encabezado, navegación y subsecciones. La prueba de siete botones de Venta no demuestra que todos los controles de la aplicación alcancen el mínimo táctil.
- [ ] Verificar escalas 125%, 150% y 200%, y ergonomía en monitor táctil real. Esta entrega declara comprobación automatizada únicamente a 96 DPI (100%).

La terminal informa compilación correcta, CTest 2/2, I1, escenario automatizado I2 y suite geométrica aprobados en Debug y Release. Esta auditoría revisó salida y capturas; no repitió esas ejecuciones. Se mantienen abiertos los hallazgos de integridad de `../auditoria-2026-09-30.md`, I2 completo y aceptación humana (0 casos).

### Resolución de software e integración gráfica I2 — 2026-09-30

- [x] **Integración gráfica I2 con ejecutable real (`tests/test_i2_integration.py`):**
  - Verificados al 100% en Debug y Release los 9 requerimientos utilizando `app_desktop.exe` y SQLite aislada:
    1. Edición de cantidad (2->3) y notas ("con poco hielo"->"sin hielo") mediante `wxTextEntryDialog`.
    2. Fusión compatible de líneas al igualar notas en UI y SQLite (4x Torta consolidada a $180.00).
    3. Eliminación de línea (`Quitar línea`) y verificación del estado restante ($200.00).
    4. Cobro multidominio con registro atómico en venta, pago, inventario según receta, movimiento de caja y auditoría.
    5. Cierre, reapertura y recuperación íntegra del ticket histórico desde `TicketHistoryDialog`.
    6. Autorización estricta: bloqueo por `ErrorCode::Forbidden` a usuario sin `pos.reprint` y acceso garantizado del cajero a su ticket recién confirmado.
    7. Cancelación de diálogo del sistema (`#32770`, `IDCANCEL`) con preservación íntegra de la venta confirmada una sola vez.
    8. Simulación determinista de error de impresión mediante `CAFETERIA_SIMULATE_PRINT_ERROR=1` (`TicketPrintout::OnBeginDocument` retorna `false`) manejado limpiamente con `Aviso del Subsistema de Impresión` sin corromper la venta.
    9. Idempotencia estricta sin duplicación de ventas, pagos, caja ni deducciones de stock.
  - Llamadas Win32 aseguradas con timeouts estrictos (`SendMessageTimeoutW` con `SMTO_ABORTIFHUNG`, `PostMessageW`), diagnósticos por PID y limpieza en `finally`.
- [x] **Geometría y paginación de impresión:** Resuelto en `TicketPrintLayout` y `TicketDto::to_formatted_text()`. Sistema de coordenadas único, cálculo sobre área real en DC, columnas dinámicas $\ge 20$, reserva estricta de pie de página y ausencia de solapamiento. Verificado en `TicketPrintingTest` (CTest 3/3 en Debug y Release).
- [ ] **Límites de verificación y estado de la Puerta I2:**
  - Salida física y corte de papel en la impresora térmica real de la PC de caja.
  - Validación física ergonómica en monitor táctil real.
  - Aceptación visual humana de los 7 casos de `qa-visual.md` (permanecen 0 casos aceptados).
  - La puerta I2 permanece formalmente **parcial** por hardware físico y QA visual humano. No se cierra I2 ni se avanza a I3.


### Estado y verificación de software I2 — 2026-10-02

- [x] **UTF-8 en tickets:** `split_long_word_utf8` divide respetando puntos de código multibyte.
- [x] **ABI Win32 y comprobación de errores:** `SendMessageTimeoutW` usa `ULONG_PTR` y `PDWORD_PTR`. Comprobación de timeouts y errores en `test_i2_integration.py`.
- [x] **Propagación de bind en carrito:** `SqliteCartService` comprueba cada bind y ejecuta rollback completo en `TransactionScope` si falla.
- [x] **Reintento real de confirmación y ticket íntegro:** Requerimiento 9 ejecuta reintento en ejecutable nativo validando igualdad celda por celda de tablas; Requerimiento 5 verifica campos completos del ticket en UI.
- [x] **Límites de área imprimible:** `TicketLayout::is_valid()` y error explícito ante áreas insuficientes (<20 columnas, <1 línea); medición de pie de página.
- [x] **Eliminación de supresión de aserciones:** `wxDisableAsserts()` removido de `main_app.cpp`. Causa raíz corregida en rangos de página de `TicketPrintout` y `TicketPrintDialog`. Cero fallos de aserción en Debug.

Límites de verificación:
- [ ] Hardware físico de destino (impresora térmica real y monitor táctil en PC de caja).
- [ ] Aceptación visual humana de los 7 casos de `qa-visual.md` (permanecen 0 casos aceptados).
- La puerta I2 permanece formalmente **PARCIAL**. No avanzar a I3.

### Correcciones solicitadas por QA humano — 2026-10-02 (FE-08 / FE-09)

- [ ] Quitar el + textual duplicado en Agregar al carrito; revisar otras duplicaciones de icono/etiqueta.
- [ ] Normalizar padding de botones y sidebar, separación icono/texto y alineación del conjunto; iconos alejados del borde sin inflar acciones innecesariamente.
- [ ] Pulir paleta/estados y fondos de etiquetas para eliminar parches y contrastes inconsistentes; mantener tokens del sistema de diseño.
- [ ] Centrar horizontal y verticalmente Cantidad y alinear verticalmente campos altos de una línea en ambos perfiles, conservando edición y accesibilidad.
- [ ] Revisar distribución Touch y objetivos táctiles de toda la navegación, entradas y selección de líneas; no limitarlo a aumentar altura de botones.
- [ ] Capturas comparables Estándar/Touch, pruebas de regresión pertinentes y nueva revisión del usuario. FE-08/09 permanecen pendientes de aceptación visual.

Evidencia y criterios en `qa-visual.md`, sección QA humano recibido — 2026-10-02. No hay nuevos casos humanos aceptados.
- [ ] QA humano 2026-10-02 / QA-V02: encabezado «Usuario:» usa username de la cuenta autenticada mediante el servicio de cuentas, no display_name. Comprobar valores distintos y cambio de sesión; conservar nombre visible en gestión de cuentas y snapshot de ticket.

### Revisión independiente de la entrega del Bloque 1 — 2026-10-02

Las cinco correcciones anteriores de I2 tienen implementación y pruebas. Antes de distribuir, queda separar --test-idempotency-key/--test-user/--test-pass del binario operativo, quitar fallback admin/admin y evitar contraseña en argumentos de proceso. Precisar alcance de snapshots y comparación de ticket. Consultar `auditoria-2026-10-02.md` en la raíz.

- [ ] Corregir aislamiento del mecanismo de prueba y documentación de cobertura.
- [ ] Bloque 2 aún pendiente: QA humano, username en encabezado y perfiles térmicos 55/80 mm.

No se cierra I2 ni se avanza a I3.

### Auditoría posterior a los tres bloques — 2026-10-03

Avances inspeccionados: runner aislado, username, único icono de agregado, fondos más limpios, Cantidad centrada y servicio/panel de perfiles térmicos. No equivalen a aceptación visual o impresión física.

- [ ] Aplicar impresora elegida y comprobar papel/controlador 55/80; unificar vista previa con geometría real.
- [ ] Inyectar ruta efectiva Unicode del puesto a configuración de impresión, incluido --data-dir.
- [ ] Guardar configuración sin cambios parciales ni éxito ante error de escritura; actualizar memoria después del éxito.
- [ ] Resolver recorte de Editar cantidad, actualización dinámica de Touch en navegación y centrado de otras entradas altas.
- [ ] Capturas de configuración/previa 55/80 y recorridos GUI de cambio/reinicio/reimpresión sin alterar historia.

Ver `auditoria-2026-10-03.md` en la raíz. I2 sigue parcial; no avanzar a I3.

### Resolución de tareas frontend — 2026-10-14

- [x] **Selector de impresoras instaladas:** `BackOfficePrintConfigPanel` enumera impresoras del sistema vía Win32 `EnumPrintersW` e inicializa el control `wxChoice` con la lista y la selección activa.
- [x] **Aplicación real en TicketPrintDialog:** `TicketPrintDialog` configura `print_data.SetPrinterName(config_.printer_name)`. Validación de disponibilidad del dispositivo con alerta visual clara si no se localiza, sin afectar la venta.
- [x] **Previsualización nominal:** Eliminado el indicador erróneo de "Columnas efectivas" del panel; rotulado explícito como previsualización nominal estimada.
- [x] **Recorte de "Editar cantidad" solucionado:** Ampliados los anchos mínimos en `pos_panel.cpp` (145px / 170px) garantizando visibilidad íntegra del texto en Estándar y Touch.
- [x] **Sidebar dinámico en toggle de perfil:** `SidebarPanel::refresh_profile()` redimensiona los botones laterales en caliente (56px en Touch, 46px en Estándar). `SubnavBar` mantiene la subsección activa al reconstruir.
- [x] **Evidencia visual 01–08 generada:** Capturas generadas en `qa_visual/screenshots/` con base aislada e inspeccionadas formalmente.
- [ ] **Límites:** Impresora física, monitor táctil real y los 7 casos de QA humano (`qa-visual.md`) permanecen sin completar. I2 sigue **PARCIAL**.

## Auditoría 2026-10-18 (ver `../auditoria-2026-10-18.md`)

- [x] Configurar tamaño de papel 55/80 en `wxPrintData` o documentar dependencia del controlador; «Continuar» con impresora ausente debe usar la predeterminada.
- [x] Sustituir centinela «Impresora Térmica Predeterminada» por cadena vacía en panel, diálogo y servicio.
- [x] Corregir recortes: «Más acciones» (Estándar), «Cambiar Contraseña», ayuda del diálogo de ticket, columnas Cant./Subtotal; alinear verticalmente búsqueda y notas en Touch; altura táctil de filas.
- [x] Arreglar `tests/capture_visual_qa.py` (solo combo de perfil, verificar vista previa de 28 columnas) y regenerar capturas 01, 02, 04, 06, 07, 08.
- [x] Revisión prompt B (2026-10-18): igualar altura real de `choice_products_` con la búsqueda en Touch (hoy ~30 px vs 48 px), restaurar ancho de columna SKU truncada tras `wxImageList`, y alinear mensajes de `test_fe08_fe09_visual_integration.py` (≥48) con sus aserciones (>= 46).
