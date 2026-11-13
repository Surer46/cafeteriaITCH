# Pendientes y estado de backend e integración — 2026-09-29

## Seguimiento vigente

### Auditoría independiente — 2026-09-30

Consultar `Auditorias/auditoria-2026-09-30.md` para evidencia y pendientes actuales. La entrega aporta funciones compilables y casos favorables, pero no demuestra cierre completo de I2 en software. Se detectaron fusión de líneas sin transacción/precio/comprobación completa, nombre histórico del cajero dependiente de la cuenta actual, historial/reimpresión autorizados por pos.sell en lugar de pos.reprint y formatos monetarios ambiguos. Las afirmaciones de cierre/exhaustividad de la entrega del 29 de septiembre quedan limitadas por esta revisión.

### Cierre de integridad y funcionalidad I2 — 2026-09-29

La entrega de Gemini del 2026-09-29 informó los siguientes avances. La auditoría del 2026-09-30, arriba, delimita lo verificado y registra defectos restantes:
1. **Captura monetaria y parsing estricto:** `Money::try_parse` rechaza formatos inválidos, letras, caracteres especiales, >2 decimales, negativos cuando no corresponden y desbordamientos numéricos sin lanzar excepciones no controladas ni truncar datos. En UI (`CheckoutDialog`, `CashShiftPanel`), cualquier entrada inválida invalida de inmediato el importe anterior y bloquea Confirmar/Abrir.
2. **Recetas vigentes:** Disponibilidad y confirmación de venta consultan únicamente la versión activa de cada receta (`ORDER BY version DESC LIMIT 1`). Las recetas anteriores se conservan en la tabla `recipes` para trazabilidad histórica. Probado con recetas v1 y v2 que descuentan insumos correspondientes.
3. **Idempotencia contextual:** La clave de idempotencia se valida contra `user_id` y caja (`reg-01`); llamadas con clave ajena reciben `ErrorCode::Forbidden`. El reintento autorizado por el usuario original recupera la venta previa sin duplicar deducciones de existencias ni movimientos de caja, incluso tras cerrar el turno.
4. **Edición de carrito y agrupación:** Modificación de cantidades y notas de preparación directamente desde la cuadrícula; fusión automática de líneas al igualar notas y eliminación al fijar cantidad en 0.
5. **Historial de tickets e impresión nativa:** Implementado `get_recent_tickets(limit)` y `TicketHistoryDialog` para búsqueda y reimpresión por folio; integrado el subsistema de impresión nativo wxWidgets (`wxPrinter` / `wxPrintout`) con manejo de cancelación y error sin anular ventas confirmadas.
6. **Endurecimiento de pruebas gráficas:** `tests/test_i1_integration.py` y `tests/test_i2_integration.py` filtran ventanas exclusivamente por PID (`GetWindowThreadProcessId`), usan `PostMessageW` para evitar bloqueos en ShowModal, aplican timeouts diagnósticos y limpian únicamente sus procesos hijos.

Revisión de código y compilación actual. El detalle ejecutable está en `plan.md`; el orden compartido en `docs/specs.md`, sección 12. Las notas del 2026-09-25 al final son históricas, no el estado actual.

- [x] BE-02: contratos, migración 004 y servicios de correo/avatar presentes; alta inicial y creación con correo, BLOB y operaciones de consulta/cambio/eliminación de avatar.
- [x] BE-03: catálogo SQLite, CRUD de productos/ingredientes, recetas versionadas con consulta exclusiva de la activa, búsqueda paginada e índices de migración 003; cubiertos por casos de `StorageTest`.
- [x] BE-04: compra básica con unidades/costo originales y movimiento de entrada, umbral y consulta de alertas; deducción de insumos por receta vigente.
- [x] BE-05/06: servicios SQLite de carrito/notas con edición y fusión automática de líneas, registro de pago/venta/ticket, historial reciente (`get_recent_tickets`), idempotencia contextual y apertura/cierre de turnos con movimientos de caja.
- [x] BE-08: `tests/test_i1_integration.py` y `tests/test_i2_integration.py` endurecidos por PID y aprobados al 100% en Release y Debug.

## Correcciones resueltas y pendientes, en orden

- [x] BE-05: Fusión segura de líneas en carrito y atomicidad completa: condicionada estrictamente a coincidencia de producto, notas Y precio unitario (`unit_price_cents`). Ejecución de mutaciones dentro de transacción atómica `TransactionScope` con rollback estricto ante cualquier error. Propagación de prepare, bind y step; comprobación de `carts.updated_at` vía `touch_cart`. Pre-validación de desbordamientos aritméticos de 64 bits en subtotales de línea y totales acumulados antes de persistir, preservando el borrador 100% intacto. Verificado en `test_storage` con fallos inducidos vía triggers SQLite en `DELETE` durante fusión y en `carts.updated_at`.
- [x] BE-05: Instantánea inmutable del nombre del cajero en tickets: creada migración `007_sale_cashier_snapshot.sql` agregando `cashier_name TEXT` a `sales`. Al confirmar la venta, se guarda `cur_user.display_name`. Cambios posteriores a `users.display_name` no alteran tickets emitidos ni el historial. Para registros históricos previos a la migración 007, se consulta `'Nombre histórico no disponible (' || s.user_id || ')'` o `'Nombre histórico no disponible'` sin inventar datos inexistentes ni depender de `users.display_name`. Verificado en `test_storage` y `test_i2_integration.py`.
- [x] BE-02/05: Autorización de reimpresión e historial: `get_ticket` y `get_recent_tickets` exigen estrictamente el permiso `pos.reprint` en la capa de servicios. Se mantiene el acceso al ticket recién confirmado para su vendedor autorizado (`pos.sell`). Probado con roles sin `pos.reprint` (acceso denegado con `ErrorCode::Forbidden` a ventas anteriores e historial) en `test_storage`.
- [x] BE-00: Captura monetaria estricta: `Money::try_parse` valida la estricta agrupación de miles (si se usan comas, exige bloques de 3 dígitos; rechaza `1,5` o comas ambiguas). Manejo seguro de `INT64_MIN` y aritmética de 64 bits en `Money::to_string` sin undefined behavior ni desbordamiento. Verificado en `test_contracts`.
- [x] BE-02/05: Exigir sesión en todas las operaciones protegidas (`ErrorCode::Forbidden`); eliminar selección de usuario por defecto; filtrar mutaciones de líneas por pertenencia a carrito/usuario/caja (`WHERE id = ? AND cart_id = ?`). Verificado en `test_i2_permissions_and_line_isolation`.
- [x] BE-04/05/06: Revalidar existencias de insumos, precios de catálogo contra líneas y turno abierto dentro de transacción atómica (`BEGIN IMMEDIATE`); prevenir desbordamientos numéricos; registrar conjuntamente venta, pago declarado, deducción de inventario (`stock_movements`), movimiento de caja (`cash_movements` con `SALE`) y auditoría (`SALE_CONFIRMED`); rollback estricto ante fallos preservando borrador en carrito. Verificado en `test_i2_sales_transaction_rollback_and_idempotency`.
- [x] BE-05: Idempotencia contextual vinculada a usuario/caja y previa a validación de turno (recupera venta confirmada incluso tras cierre de turno, rechaza claves ajenas con Forbidden, sin duplicar inventario ni movimientos); mutex contra confirmaciones simultáneas en curso. Verificado en `test_i2_recipes_idempotency_cart_editing_and_ticket_history`.
- [x] BE-05: Edición de cantidad y notas en cuadrícula de carrito, con fusión de líneas al igualar notas y vaciado de línea en cantidad 0. Verificado en CTest y POS.
- [x] BE-05: Historial reciente de tickets (`get_recent_tickets`), persistencia inmutable de tickets con notas y nombre de cajero tras reapertura de base de datos.
- [x] BE-06: Migración 006 con tabla `cash_movements`; apertura y cierre de turno atómicos con registro conjunto de `INITIAL_CASH` y auditoría.
- [ ] BE-03/05: Verificación física de salida en impresora térmica real en la PC de caja (hardware físico pendiente). La puerta I2 permanece formalmente parcial por este criterio.
- [ ] BE-01/02: migración correctiva para correo único normalizado, cuentas antiguas sin correos inventados y `user_avatars`; respaldo previo automático. La 004 debe conservarse intacta si ya fue aplicada.
- [ ] BE-02/03: validar imágenes completas, dimensiones y miniaturas reales; hoy se revisa cabecera/peso y se guardan bytes originales. Completar bloqueo por inactividad/límite de intentos e identidad Desarrollador.
- [ ] BE-04: umbral exclusivo del Dueño, proveedor/referencia/ubicación/autor, merma/ajuste/traslado y valuación acordada; el cálculo con última compra no es una decisión contable aprobada.
- [ ] BE-06/07: retirar `MockReportService`, terminar conciliación, reportes, restauración y respaldo remoto con permisos efectivos.
- [x] BE-00/08: aviso de manifiesto y conflicto comctl32 resuelto mediante `cmake/no-default-manifest.specs` en CMakeLists.txt (anula el `default-manifest.o` implícito de MinGW GCC e incrusta limpiamente el manifiesto v6 de `app.rc` sin advertencias de enlazador ni errores `0xC0000139`). Persistencia de perfil táctil por puesto implementada en `station_profile.cfg` dentro de `CAFETERIA_DATA_DIR` y verificada en suite Win32 `test_fe08_fe09_visual_integration.py`. Pendiente validar empaquetado/hardware cuando se confirme el destino.

## Verificación de esta revisión

Compilación en `build_debug` y `build_release`. Configuración: Ninja, GCC 16.1.0, C++17, wxWidgets 3.3.3 (`wxWidgets_ROOT_DIR=D:/Temp/wxwidgets`, `wxWidgets_LIB_DIR=D:/Temp/wxwidgets/lib/gcc1610_x64_dll`).

Comandos ejecutados:

```powershell
cmake --build build_release --target app_desktop test_storage test_contracts test_ticket_printing --parallel 2
ctest --test-dir build_release --output-on-failure
cmake --build build_debug --target app_desktop test_storage test_contracts test_ticket_printing --parallel 2
ctest --test-dir build_debug --output-on-failure
python -u tests/test_i1_integration.py
python -u tests/test_i2_integration.py
python -u tests/test_fe08_fe09_visual_integration.py
```

- Compilación Debug y Release: código de salida 0 en ambas configuraciones para todos los objetivos (`app_desktop`, `test_storage`, `test_contracts`, `test_ticket_printing`).
- CTest Release: 3/3 aprobadas (100% passed, 13.81 s totales):
  - `ContractsTest` (0.03 s): validación de dimensiones, conversor de unidades, aritmética monetaria, redondeos, casos exhaustivos de `Money::try_parse` y formateo de tickets en `TicketDto::to_formatted_text` para múltiples columnas (24, 32, 40, 48, 80), importes/cantidades grandes y ausencia de `...`.
  - `StorageTest` (13.71 s): 11 suites completas: ciclo de vida de SQLite, alta y persistencia de Dueño, gestión de identidades y roles configurables BE-02, catálogo e inventario BE-03, correo y avatar, turnos y ventas I2, permisos y aislamiento estricto de líneas, transacciones atómicas con rollback e idempotencia, recetas activas v1/v2 / idempotencia contextual / edición de carrito / historial de tickets tras reapertura, tickets históricos inmutables / ventas previas a migración 007 desacopladas de cuentas / permiso pos.reprint, y atomicidad en carrito / rollback ante fallos en DELETE y updated_at / desbordamientos aritméticos.
  - `TicketPrintingTest` (0.07 s): 4 suites completas de renderizado y paginación en `TicketPrintLayout` y `TicketPrintout`: cálculo sobre área imprimible real y métrica de fuente en DC, tickets de 1 página, tickets multipágina con cota vertical estricta del contenido y reserva garantizada de pie de página (0 solapamientos), prueba en áreas térmicas estrechas (58 mm) y anchas (80 mm), y consistencia del ciclo de vida de wxPrintout.
- CTest Debug: 3/3 aprobadas (100% passed, 50.33 s totales; `ContractsTest` 0.15 s, `StorageTest` 49.74 s, `TicketPrintingTest` 0.44 s).
- Verificación Puerta I1 con ejecutable real: `python -u tests/test_i1_integration.py` aprobado al 100% en Release y Debug, con filtrado estricto por PID, llamadas asíncronas `PostMessageW`, timeouts diagnósticos y base temporal aislada.
- Verificación Puerta I2 con ejecutable real: `python -u tests/test_i2_integration.py` aprobado al 100% en Release y Debug:
  - PID filtering y llamadas asíncronas con `PostMessageW`.
  - Apertura explícita de turno desde *Caja y Reportes > Caja y turnos* con $350.00; diálogo modal "Turno Abierto" cerrado con éxito; persistencia de `shifts` y `INITIAL_CASH` en `cash_movements`.
  - Agregado en POS de 3 líneas con notas de preparación distintas.
  - Cobro exacto en centavos (`Money::try_parse`, $200 recibidos, $155 total, $45 cambio).
  - Modal de impresión con pregunta explícita «¿Deseas imprimir el ticket?» y selección de «Ahora no».
  - Verificación exhaustiva en SQLite aislada: venta `V-10001`, 3 líneas inmutables con notas, pago en efectivo, stock deducido por recetas vigentes, movimientos en `stock_movements`, movimiento `SALE` en `cash_movements`, carrito vacío y auditoría `SALE_CONFIRMED`.
- Verificación visual y de perfil FE-08/FE-09: `python -u tests/test_fe08_fe09_visual_integration.py` aprobado al 100% en Release y Debug:
  - Persistencia de perfil por puesto en `station_profile.cfg`.
  - Conservación íntegra de 116 artículos en carrito SQLite al alternar de perfil.
  - Apertura automática de ventana en perfil Touch recordado en sesión posterior.
  - Comprobación pixelar de renderizado de texto en `StatusBadge` (>250 píxeles verde oscuro).
  - Comprobación de ausencia de recortes en Cuenta actual con importes grandes ($2,900.00) con margen libre ≥24px.
  - Acciones contextuales dinámicas y distinción `Quitar línea` vs `Cancelar pedido`.
  - Verificación en ventana estándar (1200×720) y estrecha (960×600).
- Límites de verificación: No demuestra la salida física en la impresora térmica real de la PC de caja (software de impresión nativo wxWidgets `wxPrinter` / `wxPrintout` integrado y probado en UI, pero la prueba en periférico físico real requiere hardware conectado). Tampoco constituye aceptación visual humana del usuario (QA visual formal permanece con 0 casos cerrados). La puerta I2 permanece formalmente parcial por este criterio.

## Histórico de verificación hasta el 2026-09-25

Estado anterior registrado tras BE-02 y la revalidación formal de I1. Se conserva como evidencia histórica; los pendientes vigentes son los de arriba y de `plan.md`.

---

### Histórico 1. Verificado y comprobado formalmente

- [x] **BE-02 — Permisos por llamada directa en la capa de servicios:**
  - `SqliteAccountService` comprueba permisos internamente en cada método sensible (`users.manage`, `roles.manage`, `audit.view`).
  - Llamadas no autenticadas retornan de forma inmediata `ErrorCode::Forbidden`.
  - Usuarios con rol limitado (ej. Cajero) no pueden crear/modificar/desactivar usuarios ni roles ni consultar la auditoría (`ErrorCode::Forbidden`).
  - Cada usuario puede cambiar su propia contraseña; cambiar la de terceros exige permiso `users.manage`.
- [x] **BE-02 — Gestión de roles configurables y catálogo maestro de permisos:**
  - Migración `002_role_permissions_and_audit.sql` con catálogo de 20 permisos granulares organizados por módulo (`pos.*`, `inventory.*`, `shift.*`, `audit.*`, `reports.*`, `users.*`, `roles.*`, `config.*`, `backup.*`).
  - El Dueño puede crear y modificar roles personalizados (ej. "Supervisor") asignando permisos específicos que se aplican dinámicamente en los servicios.
  - No se permite eliminar roles que tengan usuarios asignados (`ErrorCode::Conflict`).
  - No se permite eliminar ni despojar de facultades a roles protegidos del sistema (`role-owner`, `role-cashier`, `role-dev`).
- [x] **BE-02 — Aislamiento estricto de la identidad Desarrollador:**
  - El permiso `backup.dev_config` es exclusivo del rol `role-dev`.
  - Ni siquiera el Dueño puede poseer ni conceder `backup.dev_config`.
  - Intentar crear o actualizar un rol para incluir `backup.dev_config` es rechazado tajantemente (`ErrorCode::Forbidden`).
  - La cuenta y rol de Desarrollador no pueden crearse ni alterarse vía el servicio regular de usuarios (`ErrorCode::Forbidden`).
- [x] **BE-02 — Protección transaccional de la última cuenta Dueño activa:**
  - Verificado transaccionalmente dentro de SQLite: con un solo Dueño activo, cualquier intento de desactivación (`deactivate_user` / `update_user` con `is_active=false`) o degradación de rol (`update_user` con cambio de `role_id`) es rechazado con `ErrorCode::Conflict`.
  - `can_deactivate_user` reporta `false` para el único Dueño activo.
  - Al dar de alta un segundo Dueño activo, se permite desactivar a uno de ellos; pero el Dueño restante queda de inmediato protegido contra desactivación y cambio de rol.
- [x] **BE-02 — Bitácora y auditoría inmutable libre de contraseñas y secretos:**
  - Registro auditable de eventos: `OWNER_CREATED`, `USER_CREATED`, `USER_UPDATED`, `ROLE_CREATED`, `ROLE_UPDATED`, `ROLE_DELETED`, `PASSWORD_CHANGED`, `LOGIN_SUCCESS`, `LOGIN_FAILED`, `LOGOUT`.
  - Se verificó mediante prueba automatizada que **ningún** registro de auditoría almacena contraseñas en texto plano, sales ni hashes criptográficos en su campo `details`.
  - Consulta de bitácora (`list_audit_events`) con filtrado por tipo de evento, usuario y paginación, protegida por permiso `audit.view`.
- [x] **BE-02 — Persistencia tras cierre y reapertura de SQLite:**
  - Comprobado que usuarios creados, roles configurables, contraseñas modificadas y eventos de auditoría sobreviven el cierre y la reapertura completa de la base de datos en instancias independientes.
- [x] **CTest autónomo y reproducible en Debug y Release:**
  - 100% de pruebas aprobadas en Debug: `ContractsTest` (0.17s) y `StorageTest` (18.69s).
  - 100% de pruebas aprobadas en Release: `ContractsTest` (0.40s) y `StorageTest` (4.22s).
- [x] **Revalidación de Puerta I1 con ejecutable nativo real:**
  - Ejecutada prueba `test_i1_integration.py` sobre `build_release/app_desktop.exe` y `build_debug/app_desktop.exe`.
  - Validación completa: cancelación en primer inicio (0 usuarios creados), alta obligatoria de Dueño, persistencia en base aislada, bloqueo por login en reapertura, rechazo de contraseña errónea, autenticación correcta y despliegue de `MainWindow` sin menú técnico.

---

### Histórico 2. Pendientes que existían al 2026-09-25

- [ ] **BE-02 — Correo y avatar de cuentas:**
  - El esquema `users` y los contratos actuales todavía no incluyen correo ni avatar. Crear una migración nueva para correo y `user_avatars`, actualizar alta inicial y alta de cuentas, validar correo único y permitir imagen opcional según `docs/specs.md`.
- [ ] **BE-02 — Sesión endurecida:**
  - Completar bloqueo por inactividad y límite de intentos; el servicio actual autentica y permite cerrar sesión, pero no cubre todavía estos controles.
- [ ] **BE-02 — Identidad Desarrollador (Pendiente por decisión de arquitectura abierta):**
  - Mantener pendiente la conclusión al 100% de BE-02 hasta definir el mecanismo formal de aprovisionamiento y recuperación de la identidad técnica Desarrollador (Decisión 11 de `docs/specs.md`), sin contraseñas maestras cableadas en código ni puertas traseras.
- [ ] **BE-03 — Catálogo, ingredientes y recetas:**
  - Implementar repositorios y servicios conectados a SQLite para productos, ingredientes y recetas, reemplazando `MockCatalogService`.
- [ ] **BE-04 — Compras e inventario:**
  - Implementar existencias, entradas y movimientos transaccionales (`stock_movements`, `purchase_entries`), reemplazando `MockInventoryService` y aplicando las decisiones operativas pendientes de `docs/specs.md`.
- [ ] **BE-05 — Carrito y venta real para I2:**
  - Sustituir los mocks de carrito y ventas por servicios SQLite, con método de pago declarado, notas por línea, confirmación idempotente y datos inmutables de ticket; integrar la pregunta de impresión definida en `frontend/plan.md`.
- [ ] **Límites de lo verificado:**
  - No declarar ventas, inventario ni turnos completos hasta que existan transacciones reales en SQLite sustituyendo a los mocks.

### Resolución de software e integración gráfica I2 — 2026-09-30

- [x] **SqliteCartService:** errores de prepare, bind y step propagados; comprobación estricta de escrituras incluyendo `carts.updated_at` vía `touch_cart(cart_id)`; mutaciones ejecutadas dentro de `TransactionScope` con rollback estricto que mantiene el borrador intacto ante cualquier fallo; validación de desbordamiento en subtotales de línea y totales acumulados antes de persistir; fusión condicionada estrictamente a producto, notas y precio unitario idénticos.
- [x] **Pruebas de atomicidad en `StorageTest` (`test_i2_cart_atomic_rollback_and_overflow_integrity`):** fallo inducido vía trigger en `DELETE` durante fusión tras `UPDATE` verifica rollback completo (ambas líneas conservan cantidades y notas originales); fallo inducido en `carts.updated_at` verifica rollback en `add_item`, `update_quantity` y `remove_item` sin cambios parciales; desbordamientos en subtotal (1,000,000 unidades con precio de 20 billones) y total acumulado (subtotal 5e18) retornan `ErrorCode::InvalidInput` preservando el borrador intacto.
- [x] **Tickets anteriores a migración 007 desacoplados de `users.display_name`:** se muestra `"Nombre histórico no disponible (" || user_id || ")"` o `"Nombre histórico no disponible"`; cambiar `display_name` en `users` no altera ventas pasadas ni tickets con snapshot.
- [x] **Formato e impresión de tickets:** `TicketDto::to_formatted_text()` ajusta encabezados y cajero largos mediante división por palabras y formatea importes grandes dinámicamente asegurando un ancho estricto de $\le 40$ columnas sin recorte `...`. `TicketPrintLayout` unifica coordenadas y garantiza ausencia de solapamiento de pie de página. CTest 3/3 (`ContractsTest`, `StorageTest`, `TicketPrintingTest`) aprobado al 100% en Debug y Release.
- [x] **Integración gráfica I2 completa con ejecutable nativo real (`tests/test_i2_integration.py`):**
  - Verificados al 100% en Release y Debug los 9 requerimientos sobre `app_desktop.exe` y SQLite aislada:
    1. Edición de cantidad y notas en UI (`wxTextEntryDialog`).
    2. Fusión compatible de líneas al igualar notas en UI y SQLite ($180.00 consolidado).
    3. Eliminación de línea (`Quitar línea`) y verificación de estado restante ($200.00).
    4. Cobro atómico con comprobación en SQLite: venta V-10001, pago, inventario, movimiento de caja `SALE` y auditoría `SALE_CONFIRMED`.
    5. Cierre, reapertura y recuperación de ticket histórico idéntico en `TicketHistoryDialog`.
    6. Bloqueo a usuario sin `pos.reprint` y acceso concedido al cajero vendedor a su ticket recién confirmado.
    7. Cancelación de diálogo nativo de impresión con venta preservada exactamente una vez.
    8. Error determinista de impresión mediante `CAFETERIA_SIMULATE_PRINT_ERROR=1` (`TicketPrintout::OnBeginDocument` retorna `false`) manejado limpiamente con `Aviso del Subsistema de Impresión` sin corromper la venta.
    9. Idempotencia estricta sin duplicación.
  - Llamadas Win32 con timeout (`SendMessageTimeoutW` con `SMTO_ABORTIFHUNG`, `PostMessageW`), sin bloqueos indefinidos y con limpieza en `finally`.
- [ ] La puerta I2 permanece formalmente **PARCIAL** en hardware (impresora térmica física y monitor táctil físico en PC de destino) y en aceptación visual humana (7 casos en `qa-visual.md`). No avanzar a I3.


### Estado y verificación de software I2 — 2026-10-02

- [x] **UTF-8 en tickets:** `split_long_word_utf8` divide en límites de puntos de código sin partir bytes de caracteres multibyte. Verificado con `wrap_text_words("áá", 1)` y pruebas en `test_contracts.cpp`.
- [x] **ABI Win32 y comprobación de errores:** `SendMessageTimeoutW` usa `ULONG_PTR` y `PDWORD_PTR` en `tests/test_i2_integration.py`. Retornos y errores comprobados (`ctypes.get_last_error()`).
- [x] **Propagación de bind en carrito:** `SqliteCartService` comprueba cada bind (`CHECK_BIND_OP` y `CHECK_BIND_RES`) y ejecuta rollback total en `TransactionScope` si falla. Verificado con simulación de fallo en `test_storage.cpp`.
- [x] **Reintento real de confirmación y ticket íntegro:** Requerimiento 9 ejecuta reintento nativo en `ISaleService::confirm_sale`, validando la igualdad total de tablas operacionales celda por celda (cero duplicación); Requerimiento 5 verifica campos completos del ticket en UI.
- [x] **Límites de área imprimible:** `TicketLayout::is_valid()` y rechazo explícito si <20 columnas o <1 línea de contenido. Cancelación limpia sin excepciones ni desbordamientos en `test_ticket_printing.cpp`.
- [x] **Eliminación de supresión de aserciones:** `wxDisableAsserts()` removido de `main_app.cpp`. Causa raíz corregida en rangos de página de `TicketPrintout` y `TicketPrintDialog`. Cero fallos de aserción en Debug.

Límites de verificación:
- [ ] Hardware físico de destino (impresora térmica real y monitor táctil en PC de caja).
- [ ] Aceptación visual humana de los 7 casos de `qa-visual.md` (permanecen 0 casos aceptados).
- La puerta I2 permanece formalmente **PARCIAL**. No avanzar a I3.

### Aislamiento de prueba de idempotencia y perfiles térmicos — 2026-10-02 [VERIFICADO]

- [x] **Aislamiento del runner de prueba:** Argumentos `--test-idempotency-key`, `--test-user` y `--test-pass` y fallback `admin/admin` retirados de `app_desktop.exe`. Creado binario dedicado `tests/test_idempotency_runner.exe` con lectura segura de credenciales por `std::cin`. Autenticación obligatoria por servicio y comparación de invariabilidad fila por fila en 8 tablas operacionales (excluyendo evento de login).
- [x] **Servicio de configuración de impresión:** Contrato `IPrintConfigService` y servicio persistente `SqlitePrintConfigService` implementados en `station_printer.cfg`. Requiere permiso `config.manage`. Validación estricta de 55 mm y 80 mm; rechazo de 58 mm.
- [x] **Invariabilidad de datos históricos:** Alternar perfiles no modifica ventas registradas ni tickets previos. Verificado en CTest.
- [x] **Formato de ticket con precio unitario:** `TicketDto::to_formatted_text()` desglosa el precio unitario cuando cantidad > 1 (`4 x $45.00`), satisfaciendo `docs/specs.md` y la prueba de integración.
- [x] **CTest 3/3 en Release y Debug:** `ContractsTest`, `StorageTest` (con prueba de permisos de print config y fallo de bind), y `TicketPrintingTest` (con pruebas de perfiles 55/80 mm y áreas insuficientes) superadas al 100%.
- [x] **Integración I2 (9 requerimientos):** `tests/test_i2_integration.py` superado al 100% en Release y Debug con bases SQLite temporales aisladas.
- [ ] Hardware físico (impresora térmica real y monitor táctil en PC de caja) y aceptación visual humana de los 7 casos en `frontend/qa-visual.md` pendientes.
- La puerta I2 permanece formalmente **PARCIAL**. No avanzar a I3.

### Auditoría posterior a los tres bloques — 2026-10-03

Avances inspeccionados: runner aislado, username, único icono de agregado, fondos más limpios, Cantidad centrada y servicio/panel de perfiles térmicos. No equivalen a aceptación visual o impresión física.

- [ ] Aplicar impresora elegida y comprobar papel/controlador 55/80; unificar vista previa con geometría real.
- [ ] Inyectar ruta efectiva Unicode del puesto a configuración de impresión, incluido --data-dir.
- [ ] Guardar configuración sin cambios parciales ni éxito ante error de escritura; actualizar memoria después del éxito.
- [ ] Resolver recorte de Editar cantidad, actualización dinámica de Touch en navegación y centrado de otras entradas altas.
- [ ] Capturas de configuración/previa 55/80 y recorridos GUI de cambio/reinicio/reimpresión sin alterar historia.

Ver `Auditorias/auditoria-2026-10-03.md`. I2 sigue parcial; no avanzar a I3.

### Resolución de tareas backend — 2026-10-14

- [x] **Ruta de configuración por puesto con Unicode:** `SqlitePrintConfigService` recibe `config_dir` desde `main_app.cpp` (`--data-dir` o `CAFETERIA_DATA_DIR`) soportando acentos y espacios.
- [x] **Escritura atómica (.tmp) y reemplazo seguro:** Escritura en `station_printer.cfg.tmp`, validación de buffers, `flush()`, `close()`, verificación de errores de stream y renombrado con `std::filesystem::rename`.
- [x] **Protección de estado en memoria:** `SqlitePrintConfigService::set_station_config` actualiza `config_` solo si el guardado en disco concluyó con éxito. Si falla la E/S, preserva la configuración anterior en memoria y el archivo anterior en disco intacto.
- [x] **Prueba CTest `test_i2_print_config_atomic_save_and_isolation`:** Añadida a `StorageTest`, valida aislamiento de directorios con tildes y simulación de fallo de guardado sin corrupción de estado.
- [x] **Preservación estricta de historial:** Verificado que la configuración de impresión y los cambios de perfil térmico no mutan ninguna tabla operativa en SQLite.
- [ ] **Límites:** Impresora térmica física real en la PC de caja, monitor táctil real y los 7 casos de aceptación visual humana permanecen pendientes. La puerta I2 permanece **PARCIAL**. No avanzar a I3.

## Auditoría 2026-10-18 (ver `Auditorias/auditoria-2026-10-18.md`)

- [x] Retirar `MockReportService` de `ServiceRegistry::create_sqlite_registry`.
- [x] Encerrar `CAFETERIA_SIMULATE_PRINT_ERROR` tras opción de compilación exclusiva de pruebas.
- [x] Ampliar `test_i2_print_config_atomic_save_and_isolation`: dos guardados en el mismo directorio, fallo con archivo previo intacto, nombre Unicode con `fs::u8path`.
