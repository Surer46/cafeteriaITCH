# Plan de trabajo — Frontend de escritorio

## Propósito y límites

Implementar la interfaz **nativa** en C++17 y wxWidgets descrita en [`../specs.md`](../specs.md). Este frente construye ventanas, navegación, formularios, tablas, diálogos, impresión visual y estados de interacción. La lógica de negocio, permisos efectivos, cálculos definitivos y persistencia pertenecen al [plan de backend](../backend/plan.md). Ambos se enlazan dentro de **un solo proceso**; no hay navegador, servidor HTTP ni API de red local.

La navegación tiene **cuatro secciones principales** en la sidebar: Punto de Venta, Inventario, Caja y Reportes, BackOffice. Sus subsecciones se muestran en una barra horizontal central bajo el encabezado de la cafetería y por encima de la búsqueda o contenido. El mapa exacto está en la sección 3 de `specs.md`; ninguna subsección se agrega a la sidebar.

La PC objetivo tiene un i5 de segunda generación y una estimación de 4–6 GB de RAM. Se confirmarán sistema operativo, arquitectura, resolución y periféricos antes de cerrar el paquete distribuible. La vista Lista del punto de venta será la predeterminada; el catálogo con imágenes será opcional.

## Acuerdo de integración previo al trabajo paralelo

- El backend publica en `include/app/contracts/` tipos de intercambio y fachadas C++ para sesión/permisos, catálogo, carrito/venta, inventario/recetas, caja, reportes y respaldos. El frontend **no ejecuta SQL** ni accede a tablas directamente.
- Tipos compartidos: identificadores estables, `Money` en centavos, cantidades en unidad base entera, fecha/hora con zona definida, DTO inmutables de producto, línea de carrito, total, alerta y resultado de operación. Los errores tienen código estable y mensaje para usuario; no se muestra una excepción técnica en pantalla.
- El backend entrega implementaciones de prueba de esas fachadas y datos de ejemplo. El frontend puede construir pantallas y flujos contra esas implementaciones antes de que exista SQLite. El backend conserva la fuente de verdad para precios, descuentos, existencias y permisos.
- Cambios a contratos se acuerdan en ambos planes, se versionan y se integran con una compilación conjunta. Los servicios largos exponen progreso/cancelación cuando sea seguro; las respuestas llegan al hilo de UI por eventos.

## Tareas por bloque

Estado revisado el 2026-09-29: `[x]` marca avances concretos construidos/compilados o verificados según la evidencia de `task.md`; `[ ]` marca lo pendiente o parcial. Los paneles compilados y CTest de servicios no cierran las puertas que requieren interacción con el ejecutable real. Seguir el orden de `specs.md`, sección 12.

### FE-00 — Preparación y esqueleto nativo

- [ ] Confirmar equipo objetivo y variante de wxWidgets compatible; fijar versiones y herramientas de compilación por plataforma.
- [x] Crear objetivo `app_desktop` en CMake, ventana principal, arranque/cierre y componentes gráficos nativos básicos.
- [x] Abrir el diálogo inicial y SQLite en rutas con acentos, espacios y ñ mediante entorno o argumento, verificado con `tests/test_startup_paths.py` en Debug/Release.
- [ ] Empaquetar y probar en equipo limpio todas las bibliotecas requeridas.
- [x] Crear componentes base de barra superior, sidebar de cuatro secciones y barra horizontal de subsecciones.
- [ ] Completar componentes reutilizables de panel/tarjeta, tabla paginada, controles de dinero/cantidad, mensaje de error, confirmación y progreso.
- [ ] Implementar layouts adaptables sin medidas rígidas del SVG; comprobar resolución mínima real, teclado, foco, contraste y escalado del sistema.
- **Terminado:** la aplicación arranca sin navegador, navega entre vistas vacías y no exige instalar runtimes o base de datos.

### FE-01 — Acceso, sesión y navegación

- [x] Pantalla de alta inicial del Dueño enlazada a `AccountService` e inicio de sesión obligatorio, verificados en I1.
- [x] Incorporar correo obligatorio al formulario inicial del Dueño.
- [ ] Incorporar cierre de sesión y bloqueo por inactividad en UI.
- [x] Encabezado con nombre de cafetería a la izquierda y caja/turno y usuario **arriba a la derecha**; sidebar de cuatro secciones; conectar consulta de turno a servicio SQLite.
- [ ] Mostrar correctamente turno cerrado cuando no existe turno; actualizar encabezado al abrir/cerrar caja sin recurrir a «Caja Principal · Abierta» como valor de ejemplo.
- [x] Barra de subsecciones bajo el encabezado y sobre el contenido, con el mapa previsto para las cuatro secciones; la subsección técnica solo aparece si el servicio concede `backup.dev_config`.
- [ ] Filtrar también las demás secciones y subsecciones según permisos y resolver el desbordamiento horizontal de la barra.
- [ ] Mostrar u ocultar secciones y subsecciones según permisos consultados al servicio, sin considerar el ocultamiento una barrera de seguridad. Mantener subsección activa, foco y navegación por teclado al cambiar de módulo.
- [x] Mostrar contraseña incorrecta en el diálogo de acceso y acceso denegado al intentar abrir la configuración técnica.
- [ ] Implementar estado de sesión bloqueada y diálogo técnico de Desarrollador separado de los roles del negocio.
- **Terminado:** los cambios de usuario/rol actualizan navegación y estado; la última cuenta Dueño no se puede desactivar desde la interfaz ni por servicio.

### FE-02 — Punto de venta y carrito

- [x] Dibujar pantalla base de venta con campo de búsqueda, cantidad y tabla de líneas; mostrar cuenta actual y **Cobrar** arriba a la derecha.
- [x] Construir `PosPanel` con búsqueda, selección de producto, cantidad, Agregar, quitar línea, cancelar con confirmación y cobro conectados a servicios SQLite.
- [x] Añadir edición de cantidad de línea desde cuadrícula en `PosPanel`, recalcular totales y verificar que cantidad 0 elimina la línea.
- [x] Actualizar tabla, número de artículos y total desde el resultado del carrito.
- [x] Capturar nota al agregar y mostrarla junto al artículo; recibir líneas distintas según nota desde el servicio.
- [x] Editar o quitar una nota de una línea existente desde UI (`PosPanel`); fusionar automáticamente líneas cuando las notas coinciden y mantener agrupación correcta.
- [ ] Subnavegación **Venta / Catálogo visual** y selector Lista en la venta. Vista visual por categorías con tarjetas de imagen opcional, precio, disponibilidad y botón Agregar; paginación/virtualización y miniaturas cargadas solo para elementos visibles. Vista Lista mantiene todas las funciones y se abre por defecto.
- [ ] Ambas vistas usan el mismo `CartService`: agregar dos platillos y una bebida deja tres líneas o agrupaciones equivalentes, sin sustituir artículos; restaurar el borrador al reabrir.
- [ ] Mostrar existencia insuficiente y cambios de precio/stock devueltos por servicio; impedir que un cálculo local se presente como total definitivo.
- **Terminado:** un operador puede armar, modificar y cancelar un pedido completo con ratón o teclado, en ambas vistas, con datos de prueba y con el backend real.

### FE-03 — Cobro, ticket y caja

- [x] Construir diálogo de cobro con total, método declarado, efectivo recibido y cambio, conectado a venta SQLite.
- [x] Sustituir captura monetaria por validación estricta en centavos (`Money::try_parse`), invalidar entradas erróneas en tiempo real e inhabilitar Confirmar ante texto inválido, negativos no permitidos o desbordamientos.
- [x] Deshabilitar reenvío de Cobrar mientras la confirmación está en curso para prevenir dobles clics y emitir clave de idempotencia contextual ligada a usuario y caja.
- [x] Construir pregunta Imprimir/Ahora no y vista previa del ticket recuperado por folio, con notas bajo sus productos y nombre del cajero inmutable.
- [x] Integrar subsistema de impresión nativo wxWidgets (`wxPrinter` / `wxPrintout`) en `TicketPrintDialog` mediante `TicketPrintLayout` (`src/frontend/ticket_layout.hpp`/`.cpp`): único sistema de coordenadas (`MapScreenSizeToPage`), medición en DC, ancho dinámico ($\ge 20$ cols) con envoltura de palabras sin recortes `...`, reserva estricta de pie de página (`usable_h - footer_reserved_h`), paginación precalculada consistente (`GetPageInfo`, `HasPage`, `OnPrintPage`), y verificación en áreas de 58 mm, 80 mm y altura reducida en CTest (`TicketPrintingTest`). Mensajes que distinguen envío al spooler vs comprobación de salida en papel físico, y manejo de cancelación/error sin anular ni duplicar la venta; `TicketHistoryDialog` para búsqueda y reimpresión autorizada de tickets con permiso `pos.reprint`.
- [ ] Confirmar salida física hacia impresora térmica real en la PC de caja una vez conectados los periféricos (hardware físico pendiente).
- [x] FE-03/06 / I2: selector de perfil térmico **55 mm / 80 mm** e impresora en BackOffice → Configuración → Impresión, con permiso operativo `config.manage` y persistencia por puesto mediante contrato/servicio (`IPrintConfigService` / `SqlitePrintConfigService` en `station_printer.cfg`). Ajustar vista previa y salida al perfil y área útil real del DC; validar márgenes, envoltura y paginación. Probado ambos anchos en CTest (`test_thermal_paper_profiles`), reinicio, cambio de perfil y reimpresión sin modificar datos históricos; probado 55 mm explícitamente (28 columnas) y rechazo de 58 mm. Validación de salida física en papel térmico real pendiente de hardware.
- [x] Retirar apertura automática de turno con fondo fijo de $500 en `PosPanel`; solicitar fondo y confirmación desde Caja y turnos; encabezado refleja estado real.
- [x] Construir vista operativa de **Caja y Reportes → Caja y turnos** (`CashShiftPanel`) para apertura explícita de turno con fondo elegido, movimientos de caja (`cash_movements`), arqueo de cierre y actualización de estado en tiempo real.
- [ ] Diálogos de cancelación de orden y anulación de venta cobrada separados, con motivo y permisos.
- [x] Ejecutar y revisar `tests/test_i2_integration.py` en Release y Debug: apertura explícita de turno con $350.00, venta con tres líneas, notas, pago, consumo de ingredientes, movimiento de caja `SALE`, snapshot de cajero y Ahora no persistidos en SQLite.
- [x] Completar integridad de software de I2 según auditoría: fusión atómica condicionada a producto, notas y precio con rollback estricto ante fallo (en `DELETE` o `updated_at`), pre-validación de desbordamiento en subtotales/totales de 64 bits, tickets históricos previos a migración 007 desacoplados de `users.display_name`, permiso `pos.reprint` obligatorio en servicios para historial y formato de ticket a columnas dinámicas con división por palabras. Probado en CTest (Debug/Release, 3/3 targets: `ContractsTest`, `StorageTest`, `TicketPrintingTest`) y suites de integración.
- [ ] Validar periférico físico de impresión de tickets (hardware físico de impresora térmica en PC de caja) y pantalla táctil física.
- **Criterio de terminado (pendiente):** I2 permanece formalmente parcial por hardware físico (impresora térmica/táctil) y aceptación humana (0/7 casos de `frontend/qa-visual.md` aceptados). Software de I2 aún parcial según `auditoria-2026-10-18.md`.

### FE-04 — Catálogo, ingredientes e inventario

- [x] Situar los accesos de Inventario, Reabastecimiento, Productos y recetas y Promociones en la barra de subsecciones, sin botones laterales independientes.
- [ ] Construir las vistas operativas correspondientes; hoy muestran pantallas preparatorias.
- [ ] Formularios de producto directo, platillo y bebida, SKU, precio, categoría, estado e imagen opcional con vista previa y validación de tamaño/formato.
- [ ] Editor de recetas: agregar ingredientes, cantidad y unidad por porción, mostrar conversiones compatibles y costo estimado devuelto por servicio.
- [ ] Vistas de ingredientes con unidad base, existencias y umbral editable solo por Dueño; alertas de bajo stock visibles tras movimientos.
- [ ] Registro de entrada con cantidad, unidad y costo total (ejemplo 1 kg de limón por $50); mostrar costo unitario normalizado calculado por servicio.
- [ ] Inventario general, barra, traslados, ajustes/mermas y movimientos con filtros y paginación.
- **Terminado:** formularios impiden datos incompletos antes de enviar, muestran errores del servicio y reflejan existencias/costos/alertas actualizados.

### FE-05 — Promociones, bitácora y reportes

- [ ] Pantallas de promociones/combos bajo **Inventario → Promociones y combos**, con vigencia y reglas que el servicio declare configurables.
- [ ] Bitácora bajo **Caja y Reportes → Bitácora**, consultable por fecha, usuario, tipo y folio; detalle de eventos sin edición.
- [ ] Reportes bajo **Caja y Reportes → Reportes**: ventas, pagos declarados, inventario y corte con filtros, paginación, exportación e impresión disponibles.
- **Terminado:** pantallas vacías, carga, errores y acceso denegado son claros; reportes extensos no congelan la ventana.

### FE-06 — Usuarios, roles y respaldos

- [x] Crear la sección BackOffice y sus botones de subsección en la barra central; ocultar **Servidor de respaldos** cuando falta `backup.dev_config`.
- [ ] Sustituir las pantallas preparatorias de BackOffice por vistas operativas; filtrar todas sus subsecciones por permiso.
- [x] Construir Usuarios y cuentas con listado, alta con correo/contraseña/rol/avatar opcional, cambio de contraseña, activación/desactivación y consulta del avatar, enlazados a SQLite.
- [ ] Completar confirmación de contraseña en alta de empleados, edición de correo/nombre/rol y sustitución/eliminación de avatar con vista previa e iniciales; verificar imágenes reales.
- [ ] Construir Roles y permisos; el servicio ya existe pero su pantalla sigue preparatoria.
- [ ] Gestión de cuentas por Dueño: crear con nombre de usuario, nombre visible, correo obligatorio, contraseña con confirmación y rol asignado; editar, desactivar, restablecer acceso y consultar estado. Ofrecer avatar opcional con vista previa, sustitución, eliminación e iniciales como alternativa; cargar miniaturas solo cuando se muestran. Gestionar roles configurables con permisos por acción/módulo y vista previa del efecto antes de guardar.
- [ ] Configuración operativa del negocio bajo BackOffice, sin mezclarla con la configuración técnica del destino remoto.
- [ ] Pantalla de respaldos para Dueño: activar/desactivar remoto, iniciar copia manual, ver progreso, fecha, estado y error; restauración guiada con advertencia.
- [ ] Pantalla **BackOffice → Servidor de respaldos** para Desarrollador: destino remoto, credenciales/certificados, retención y prueba de conexión, accesible solo tras autenticación técnica gestionada por servicio.
- **Terminado:** el Dueño administra la operación de copias pero no puede modificar destino ni secretos técnicos; las fallas remotas no bloquean el POS.

### FE-07 — Pulido, rendimiento y accesibilidad

- [ ] Ejecutar QA visual humano siguiendo `qa-visual.md`; registrar observaciones y aceptación por caso. Estado inicial: 0 casos aceptados; las pruebas automatizadas no cierran esta tarea.
- [ ] Preparar un catálogo de prueba persistente y aislado para QA-V04/05, sin introducir datos de ejemplo en la base operativa.
- [ ] Sustituir efectos caros del SVG por estilo nativo ligero, conservar identidad rojo/amarillo y estados de foco/selección/error.
- [ ] Revisar atajos, recorrido de foco, lectura de etiquetas, navegación por teclado y tamaños de control.
- [ ] Medir arranque y memoria de la ventana, búsqueda, 20 líneas de carrito y catálogo con/sin imágenes en la PC real; ajustar tamaño de miniaturas y paginación.
- [ ] Probar pantalla estrecha, alta densidad/escala, datos largos y nombres de producto amplios.
- **Terminado:** se cumplen las metas de `specs.md` o se documentan medidas y ajustes acordados en la PC objetivo.

### FE-08 — Perfil táctil para monitores grandes

- [x] Diseñar una distribución revisable de Venta, cobro y cuentas para perfiles Estándar/Táctil, conservando cuatro secciones y subsecciones centrales.
- [x] Persistir preferencia de perfil Estándar/Touch por puesto en `station_profile.cfg` dentro de `CAFETERIA_DATA_DIR`; cambiar perfil sin perder carrito (comprobado con 116 artículos en SQLite y arranque recordado).
- [x] Reorganizar distribución de Venta según espacio útil: objetivos táctiles mínimos de 48px (steppers `[−]`/`[+]` de 48×48px), 56px en acciones frecuentes (`+ Agregar al Carrito`, `Cobrar`), separación cómoda de 8–14px entre controles.
- [x] Mostrar pocas acciones primarias: `Editar cantidad`, `Editar nota`, `Quitar línea` contextuales por selección de fila y secundarios en `Más acciones ▾` (historial/reimpresión). Distinción explícita de `Quitar línea` vs `Cancelar pedido`.
- [x] Probar ambos perfiles en 1200×720 y ventana estrecha (960×600), importes grandes ($2,900.00 / 116 unidades) sin recortes; validado con suite Win32 nativa `tests/test_fe08_fe09_visual_integration.py` en Release y Debug.
- [ ] Construir tarjetas amplias y paginadas para catálogo opcional; agrupar formularios largos por secciones/pasos sin sobrecargar la pantalla.
- [ ] Integrar captura numérica/teclado en pantalla según plataforma confirmada, sin ocultar campo, total o confirmación; conservar teclado físico/escáner y no depender de hover, clic derecho o doble clic.
- [ ] Probar ambos perfiles, cambio de escala, teclado visible, datos largos y toques repetidos; validar legibilidad, precisión y flujo completo en monitor táctil grande real.
- **Terminado:** vender, editar una línea, cobrar y decidir impresión funciona con distribución táctil clara, sin botones amontonados ni solapamientos, y conserva rendimiento/persistencia; pendiente validación física en monitor táctil real.

### FE-09 — Sistema visual Cupertino y Material

- [x] Documentar dirección visual, paleta del SVG, geometría, Roboto, iconos y reglas nativas en `design-system.md` (2026-09-30).
- [x] Crear tokens de tema y componentes compartidos ligeros para tarjeta (`CardPanel`), botón estilizado (primary, secondary, danger), badge (`StatusBadge`) y subsecciones segmentadas, manteniendo teclado/foco/accesibilidad.
- [x] Empaquetar Roboto TTF (Regular, Medium, Bold con OFL) y 20 iconos Material SVG con Apache 2.0, carga local en memoria con `AddFontResourceExW` / `wxBitmapBundle` y caché por DPI.
- [x] Aplicar a encabezado/sidebar/subsecciones, POS y cuenta arriba derecha; agrupar cantidad/nota/quitar como acciones contextuales y secundarios en Más acciones.
- [x] Corregir hallazgos de auditoría (QA-V04/06 y QA-V02): Cuenta actual adaptable sin recorte en importes grandes (ancho 340/380px, margen libre ≥24px), StatusBadge con renderizado GDI nítido en DC y ventana, y eliminación de recortes en acciones de carrito.
- [x] Aplicar a cobro (`CheckoutDialog`), tickets (`TicketPrintDialog`), Caja y turnos (`CashShiftPanel`) y Usuarios y cuentas (`BackofficeUsersPanel`).
- [x] Producir comparativa Estándar/Touch y capturas para QA visual en `qa_visual/screenshots/` sin marcar aceptación humana en `qa-visual.md`.
- [x] Compilar Debug/Release con archivo specs de enlazador (`cmake/no-default-manifest.specs`), ejecutar CTest (2/2 en ambas), I1/I2 (100% aprobadas) y suite visual `test_fe08_fe09_visual_integration.py`; mantener los pendientes de auditoría I2 separados del avance visual.
- **Terminado:** estilo nativo ligero implementado y validado en ejecución real; CTest, I1, I2 e integración visual verificados; capturas generadas para QA humano en `qa_visual/screenshots/`.

## Secuencia y dependencias

1. FE-00 y acuerdo de contratos con BE-00/BE-01.
2. FE-01 y FE-02 contra servicios de prueba mientras backend construye datos y ventas.
3. FE-03 después del contrato de `SaleService` y `ShiftService`; FE-04 después de contratos de receta/inventario.
4. FE-05 y FE-06 después de contratos de reportes, roles y respaldos.
5. FE-08: diseñar el perfil táctil antes de cerrar los layouts de Venta/cuentas; implementarlo después de estabilizar los contratos y flujos de esas vistas.
6. FE-07 y pruebas de punta a punta con el backend real, incluyendo validación táctil en hardware confirmado.

## Puertas de integración con backend

| Puerta | Estado | Demostración conjunta obligatoria |
| --- | --- | --- |
| I1: aplicación abre | [x] Comprobada con ejecutable nativo real y SQLite local en Release y Debug (`tests/test_i1_integration.py`). | Ventana nativa + SQLite local + alta de Dueño + cierre/reapertura con datos persistidos. |
| I2: venta básica | [ ] Software completado y verificado en Debug y Release. Parcial por hardware de destino (impresora térmica/táctil) y aceptación visual humana (0/7 casos aceptados). | Buscar, editar, cobrar y emitir un solo ticket; demostrar impresión/reimpresión, cancelación/error e idempotencia sin alterar venta o movimientos. |
| I3: inventario | [ ] | Crear receta, comprar ingrediente con costo, vender platillo, descontar unidad correcta y mostrar alerta. |
| I4: control | [ ] Roles y turno básico existen; faltan autorización uniforme y conciliación completa. | Rol sin permiso recibe bloqueo también en servicios; apertura/corte de turno concilia ventas. |
| I5: recuperación | [ ] Instantánea local probada; falta restauración y operación remota. | Respaldar/restaurar; pérdida de red remota no interrumpe venta; un doble clic no duplica venta. |
| I6: navegación | [ ] Cuentas tiene panel; faltan UI de roles, filtros de permiso y prueba integrada. | Solo cuatro secciones laterales; subsecciones correctas en barra central bajo título; BackOffice permite crear cuenta y rol sin exponer configuración técnica al Dueño. |
| I7: modo táctil | [ ] Diseño e implementación pendientes (FE-08). | Perfil persistente, botones/espacios cómodos, acciones contextuales, teclado sin solapamientos, venta completa por toque en monitor grande confirmado. |

El frontend no se considera completo por mostrar todas las pantallas: **cada puerta debe ejecutarse con la implementación real**, datos persistidos y evidencia de resultado. Las decisiones de negocio aún abiertas en `specs.md` se registran antes de codificar el flujo afectado.

### Estado y verificación de software I2 — 2026-10-02

Los cinco pendientes de software de I2 fueron declarados completados por el agente; `auditoria-2026-10-18.md` los deja parciales:

- [x] Corte de palabras UTF-8 en límites de puntos de código sin caracteres corruptos (`split_long_word_utf8`).
- [x] Tipado exacto `PDWORD_PTR` (`ULONG_PTR = ctypes.c_size_t`) en `SendMessageTimeoutW` y comprobación de errores Win32 en pruebas.
- [x] Propagación de bind en carrito y rollback en `TransactionScope` ante fallo.
- [x] Reintento real de confirmación en ejecutable nativo (`ISaleService::confirm_sale`) con comprobación idéntica celda por celda de todas las tablas operacionales y verificación integral de campos del ticket en UI.
- [x] Validación de límites de área imprimible física (<20 columnas, <1 línea) con error explícito y cancelación limpia.
- [x] Supresión de aserciones eliminada (`wxDisableAsserts` removido) y corrección de causa raíz en rangos de página.

Límites de verificación:
- La puerta I2 permanece **PARCIAL** en hardware físico de destino (impresora térmica real y monitor táctil en PC de caja) y en aceptación humana (0 casos de `frontend/qa-visual.md` cerrados). No avanzar a I3.


### Correcciones solicitadas por QA humano — 2026-10-02 (FE-08 / FE-09) [IMPLEMENTADAS EN SOFTWARE]

- [x] Quitar el + textual duplicado en Agregar al carrito; etiqueta limpia «Agregar al Carrito» con único icono Material `add`; normalizado en botón de alta de usuario.
- [x] Normalizar padding de botones y sidebar (56px en Touch), separación icono/texto (`SetBitmapMargins`) y alineación del conjunto mediante `Theme::setup_button_icon`.
- [x] Pulir paleta/estados y fondos de etiquetas para eliminar parches y contrastes inconsistentes (`CardPanel::SetBackgroundColour` igualando el color de tarjeta para pintar estáticos bajo `WM_CTLCOLORSTATIC`).
- [x] Centrar horizontal y verticalmente Cantidad en su caja contenedora (`wxTE_CENTRE` con espaciadores elásticos superior e inferior), conservando selección, teclado y accesibilidad.
- [x] Revisar distribución Touch y objetivos táctiles de toda la navegación (56px en sidebar, 48px en toggle de perfil en encabezado, objetivos interactivos ≥48px y filas holgadas).
- [x] Encabezado «Usuario:» muestra `username` de la cuenta autenticada (`user_res.value().username`), no `display_name`, preservando `display_name` para gestión de usuarios y snapshots históricos de tickets.
- [x] Capturas comparables Estándar/Touch regeneradas en `qa_visual/screenshots/` con base aislada (`tests/capture_visual_qa.py`).
- [ ] Usuario revisa interactivamente las correcciones en el ejecutable real; los 7 casos de `qa-visual.md` continúan pendientes de aceptación (0 casos aceptados).

### Verificación de Bloque 1 y Bloque 2 — 2026-10-02

- [x] **Aislamiento del mecanismo de prueba:** Retirados `--test-idempotency-key`, `--test-user`, `--test-pass` y fallback `admin/admin` de `app_desktop.exe`. Creado target de integración dedicado `tests/test_idempotency_runner.exe` que lee credenciales exclusivamente por `stdin` (sin secretos en `argv` ni logs) y comprueba coincidencia exacta de folio e invariabilidad fila por fila de las 8 tablas operacionales (excluyendo auditoría de login).
- [x] **Perfiles térmicos 55/80 mm:** Contrato `IPrintConfigService` y servicio `SqlitePrintConfigService` con persistencia por puesto en `station_printer.cfg`. Panel de configuración en `BackOffice -> Configuración -> Impresión`. Soporta 55 mm y 80 mm; rechaza perfiles no soportados (ej. 58 mm). `TicketPrintLayout` distingue ancho nominal de área útil real de DC y aborta limpiamente si el área es insuficiente (<20 cols). Probado en CTest `TicketPrintingTest` y `StorageTest`.
- [ ] La puerta I2 permanece estrictamente **PARCIAL** por hardware físico de destino (impresora térmica real y monitor táctil en PC de caja) y aceptación visual humana. No avanzar a I3.

### Auditoría posterior a los tres bloques — 2026-10-03

Avances inspeccionados: runner aislado, username, único icono de agregado, fondos más limpios, Cantidad centrada y servicio/panel de perfiles térmicos. No equivalen a aceptación visual o impresión física.

- [ ] Aplicar impresora elegida y comprobar papel/controlador 55/80; unificar vista previa con geometría real.
- [ ] Inyectar ruta efectiva Unicode del puesto a configuración de impresión, incluido --data-dir.
- [ ] Guardar configuración sin cambios parciales ni éxito ante error de escritura; actualizar memoria después del éxito.
- [ ] Resolver recorte de Editar cantidad, actualización dinámica de Touch en navegación y centrado de otras entradas altas.
- [ ] Capturas de configuración/previa 55/80 y recorridos GUI de cambio/reinicio/reimpresión sin alterar historia.

Ver `auditoria-2026-10-03.md` en la raíz. I2 sigue parcial; no avanzar a I3.

### Resolución de pendientes de auditoría — 2026-10-14

Los pendientes de frontend de la auditoría 2026-10-03 fueron implementados y verificados:

- [x] **Impresora seleccionada y comprobada:** `BackOfficePrintConfigPanel` integra selector `wxChoice` con impresoras instaladas en Windows (`EnumPrintersW`). `TicketPrintDialog` aplica el nombre a `wxPrintData::SetPrinterName()` y emite advertencia no bloqueante si el dispositivo no existe en el sistema sin interrumpir la venta.
- [x] **Previsualización nominal explícita:** Identificada claramente como ejemplo nominal sujeto a las métricas del controlador del sistema. Removida etiqueta engañosa "Columnas efectivas".
- [x] **Ajuste ergonómico y visual de botones:** Ampliados anchos de botones contextuales de carrito en `pos_panel.cpp` (145px en estándar, 170px en táctil), evitando por completo el recorte de «Editar cantidad».
- [x] **Actualización dinámica de navegación en toggle de perfil:** `SidebarPanel::refresh_profile()` recalcula y aplica las dimensiones de botones (56px táctil, 46px estándar) en caliente; `SubnavBar::rebuild_buttons()` preserva la subsección activa.
- [x] **Evidencia visual generada e inspeccionada:** 8 capturas generadas con base aislada en `qa_visual/screenshots/` (incluyendo vistas de configuración para 55 mm y 80 mm).
- [ ] **Límites:** Impresora térmica física, monitor táctil real y 7 casos de aceptación visual humana (`qa-visual.md`) siguen pendientes. Puerta I2 permanece **PARCIAL**. No avanzar a I3.

## Auditoría 2026-10-18

Ver `../auditoria-2026-10-18.md` y la sección homónima de `task.md`. I2 parcial en software, hardware y aceptación humana; no avanzar a I3.
