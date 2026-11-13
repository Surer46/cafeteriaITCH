# Plan de trabajo — Backend y datos locales

## Propósito y límites

Implementar en C++17 la lógica de negocio, repositorios SQLite, autenticación, permisos, cálculos, ventas, inventario, caja, reportes y respaldos definidos en [`docs/specs.md`](../docs/specs.md). “Backend” significa **capa interna de una aplicación de escritorio nativa**, enlazada con el [frontend wxWidgets](../frontend/plan.md) en un solo proceso. No se levanta un servidor HTTP local ni se exige instalar MySQL/PostgreSQL. El único servidor externo es un destino opcional de copias.

La PC objetivo tiene un i5 de segunda generación y RAM estimada en 4–6 GB. El sistema operativo y periféricos siguen pendientes de comprobar; las bibliotecas y el empaquetado final se validarán en el equipo real.

La navegación de la aplicación agrupa servicios bajo cuatro secciones: **Punto de Venta** (venta y catálogo visual), **Inventario** (existencias, reabastecimiento, productos/recetas, ingredientes, promociones y barra), **Caja y Reportes** (turnos, bitácora e informes) y **BackOffice** (cuentas, roles, configuración y respaldos). **BackOffice → Servidor de respaldos** es una subsección técnica exclusiva del Desarrollador. Esta agrupación no cambia los límites de transacciones o permisos de cada servicio.

## Contrato que se entrega al frontend

- Publicar en `include/app/contracts/` tipos y fachadas C++ estables antes de avanzar las pantallas: `AccountService`, `CatalogService`, `CartService`, `SaleService`, `InventoryService`, `ShiftService`, `ReportService` y `BackupService`.
- Definir `Money` como centavos enteros, cantidades como enteros en unidad base, IDs estables, fechas con zona, DTO para búsquedas paginadas, producto, receta, carrito, totales, alertas y estados de turno. Usar resultados tipados con códigos de error (`InvalidInput`, `Forbidden`, `NotFound`, `Conflict`, `InsufficientStock`, `StorageFailure`, `BackupFailure`) y texto seguro para mostrar.
- Exponer operaciones síncronas breves para edición normal y operaciones largas con progreso/cancelación cuando sea seguro. Los servicios no reciben controles wxWidgets; notifican resultados que la UI entrega en su hilo principal.
- Proveer implementaciones de prueba en memoria para trabajo paralelo del frontend y pruebas de contrato. Mantener los mismos resultados/errores en implementación de prueba y SQLite.
- El backend es la fuente de verdad de precios, descuentos, cambio, existencias y permisos. La UI puede previsualizar, pero debe mostrar el resultado confirmado devuelto por servicios.

## Tareas por bloque

Estado revisado el 2026-09-29: `[x]` señala un avance concreto implementado y cubierto por la verificación indicada en `task.md`; `[ ]` señala pendiente o implementación parcial. La existencia de una tabla, contrato o mock no cierra un módulo. Seguir el orden de `docs/specs.md`, sección 12, antes de extender funciones.

### BE-00 — Estructura y compilación

- [x] Crear proyecto CMake con bibliotecas `app_domain`, `app_storage`, `app_services` y pruebas, enlazadas a `app_desktop`; compilar Debug y Release.
- [x] Compilar con C++17 y SQLite embebido en el entorno de desarrollo actual.
- [ ] Fijar C++17, wxWidgets y SQLite a versiones probadas; incluir dependencias necesarias en el paquete por plataforma real, sin instaladores de motores externos.
- [ ] Corregir autodetección de wxWidgets: una configuración limpia puede elegir la carpeta genérica `lib` y desactivar `app_desktop`; la auditoría requirió `wxWidgets_LIB_DIR` explícito. Verificar además el aviso de múltiples manifiestos del enlace.
- [x] Definir resultados/errores tipados, ruta local de datos y ruta de migraciones para el arranque.
- [x] Corregir rutas Unicode por `CAFETERIA_DATA_DIR`, `CAFETERIA_MIGRATIONS_DIR` y argumento `--data-dir`; registrar la opción en wxWidgets y probar acentos/espacios/ñ y precedencia del argumento en Debug/Release.
- [ ] Completar logging sin datos sensibles y configuración persistente de rutas/servicios.
- [x] Persistir preferencia de perfil Estándar/Táctil por puesto en archivo local (`station_profile.cfg` en `CAFETERIA_DATA_DIR`), aislada por puesto y sin mezclarla con reglas de precio, inventario ni permisos (FE-08).
- **Terminado:** compila aplicación mínima con servicio en memoria y con repositorio SQLite de prueba; arranca sin conexión de red; perfil táctil persiste por puesto.

### BE-01 — Contratos y base SQLite

- [x] Publicar DTO, fachadas de servicios e implementaciones simuladas para integración de interfaz.
- [x] Publicar contratos de correo/avatar, notas de carrito y datos de ticket.
- [ ] Completar equivalencia de errores/autorización entre mocks y servicios reales; retirar `MockReportService` del ejecutable distribuible.
- [x] Crear migraciones versionadas con tablas base de usuarios/roles, catálogo, recetas, imágenes, carrito, ventas, pagos registrados, compras, movimientos, caja, auditoría y respaldos.
- [x] Agregar correo/avatar mediante migración 004 y notas de borrador/venta mediante migración 005.
- [ ] Corregir mediante nueva migración los correos inventados de cuentas anteriores, la unicidad sin distinguir mayúsculas y la separación de avatar en `user_avatars`; completar restricciones de invariantes.
- [x] Activar claves foráneas, `busy_timeout` y WAL en la conexión SQLite; disponer de transacciones y primeros índices.
- [ ] Validar soporte real de WAL, límites de conexiones/escrituras e índices para todas las búsquedas y reportes.
- [x] Aplicar migraciones por versión sin repetir una versión ya aplicada y usar la API de instantánea consistente de SQLite para respaldos.
- [ ] Crear respaldo previo automático antes de migrar una base existente; comprobar integridad al arrancar y gestionar disco lleno/base ocupada sin perder datos confirmados.
- **Terminado:** datos persistidos sobreviven cierre/reinicio; migración repetida es segura; restricciones impiden referencias huérfanas.

### BE-02 — Identidad, roles y auditoría

- [x] Alta inicial obligatoria de Dueño con contraseña elegida por él y hash seguro; sin credenciales predeterminadas.
- [x] Solicitar correo en alta inicial y creación de cuentas, validar formato básico y persistir correo, contraseña con hash y rol asignado.
- [ ] Normalizar y exigir correo único sin distinguir mayúsculas; conservar cuentas antiguas sin datos de contacto inventados.
- [x] Guardar, consultar, sustituir y quitar avatar BLOB por servicio, con límite de 100 KB y validación básica de cabecera.
- [ ] Migrar avatar a tabla separada, decodificar formato real, limitar dimensiones y optimizar miniaturas; respaldar antes de migrar. La cabecera por sí sola no valida una imagen.
- [x] Implementar autenticación local, sesión en memoria y cierre de sesión en el servicio de cuentas.
- [ ] Implementar bloqueo por inactividad y limitación de intentos.
- [x] Crear/editar roles configurables y asignar permisos por acción; proteger `backup.dev_config` y al último Dueño en el servicio de cuentas.
- [ ] Exigir autenticación y autorización en todos los servicios y lecturas protegidas: actualmente catálogo/inventario/carrito/venta omiten varios controles cuando no hay sesión. Eliminar selección del primer usuario como alternativa y comprobar pertenencia de cada línea al carrito de la cuenta/caja actual.
- [ ] Identidad técnica separada para Desarrollador en configuración remota, con provisionamiento y recuperación por definir antes de implementación; auditar cambios.
- [x] Registrar auditoría de altas/cambios de cuentas, contraseñas, roles e inicio de sesión sin contraseñas ni hashes.
- [ ] Registrar eventos de ventas, ajustes, precios, recetas, anulaciones y respaldos cuando existan esos servicios persistentes.
- **Terminado:** una llamada directa a servicio sin permiso falla aunque el control UI esté visible; existe siempre un Dueño activo.

### BE-03 — Productos, ingredientes, unidades y recetas

- [x] Implementar conversor de unidades base de masa, volumen y piezas con validación de desbordamiento y compatibilidad; probarlo en `ContractsTest`.
- [x] CRUD de productos con baja/reactivación lógica, SKU/código únicos, categorías, precios y estado en SQLite.
- [x] Crear/editar ingredientes con dimensión/unidad base y validar conversiones compatibles mediante servicios persistentes.
- [x] Guardar y consultar receta por porción con cantidades positivas y número de versión.
- [x] Verificar que cambiar receta conserva consumo histórico y que disponibilidad/venta usan solo la receta vigente (`ORDER BY version DESC LIMIT 1`); probado con productos de 2 versiones en `test_i2_recipes_idempotency_cart_editing_and_ticket_history`.
- [ ] Completar inventario de artículos directos sin receta.
- [x] Guardar y recuperar imagen de producto en SQLite con verificación básica de cabecera JPEG/PNG/WebP y límite de 100 KB.
- [ ] Decodificar imagen real, limitar dimensiones y generar miniatura optimizada; hoy `thumbnail_blob` repite la imagen original. Completar carga paginada sin decodificar todo el catálogo.
- [x] Buscar por código/nombre, categoría y estado con límite/offset; añadir índices de catálogo/receta en migración 003.
- [ ] Validar planes de consulta y rendimiento de búsqueda parcial (`LIKE '%texto%'`), filtrar disponibilidad y resolver stock de productos directos.
- **Terminado:** una receta de platillo/bebida calcula ingredientes correctamente; una imagen inválida se rechaza y la vista sin imagen permanece funcional.

### BE-04 — Compras, costo, inventario y alertas

- [x] Implementar y probar cálculo proporcional de costo con enteros y costo total/cantidad originales (1 kg de limón por $50.00 → $0.05/g).
- [x] Registrar compra básica de ingrediente, cantidad/unidad/costo originales, conversión base y movimiento de entrada transaccional en SQLite.
- [ ] Registrar cada entrada con cantidad/unidad comprada, costo total pagado, proveedor/referencia opcionales, ubicación, fecha y usuario; convertir a unidad base y calcular costo unitario sin coma flotante binaria.
- [ ] Registrar movimientos de venta, entrada, merma, ajuste y traslado; no cambiar saldos sin movimiento auditable.
- [x] Persistir umbral por ingrediente y consultar alertas cuando `existencia ≤ umbral`.
- [ ] Restringir configuración del umbral exclusivamente al Dueño, también durante alta/edición de ingredientes; completar índice, indicador global y actualización visual tras movimientos.
- [ ] Calcular costo estimado por receta con el método de valuación que se confirme; conservar costos históricos de compras.
- [ ] Definir y aplicar política confirmada de sobreventa/saldos negativos, ubicación y caducidad si se requiere.
- **Terminado:** 1 kg de limón por $50.00 registra $50/kg y $0.05/g; una venta descuenta receta y dispara alerta al cruzar el umbral, sin duplicar movimiento.

### BE-05 — Carrito, precios y ventas

- [x] Persistir carrito de varias líneas por usuario y caja `reg-01`, con recuperación tras reapertura.
- [x] Edición de cantidad y notas en cuadrícula de carrito; fusión automática de líneas al igualar notas y eliminación de línea al establecer cantidad en 0.
- [x] Parametrizar caja, asegurar aislamiento de carritos y agrupación consistente tras editar nota/cantidad/precio.
- [x] Guardar y recuperar notas por línea, rechazar controles y separar al agregar productos con nota/precio distintos.
- [ ] Validar el límite de 250 caracteres Unicode (actualmente se cuentan bytes) e integrar reglas de promociones acordadas.
- [ ] Motor de precio y promociones/combos con reglas y vigencia definidas; total en centavos, descuentos/impuestos solo según política confirmada.
- [x] Registrar efectivo, tarjeta o transferencia declarados, recibido y cambio en SQLite, sin procesamiento electrónico.
- [x] Confirmación transaccional: revalidar turno, permisos, producto, precio y existencia; asignar folio, guardar venta/líneas/pago, descontar existencias/ingredientes según receta vigente, registrar caja (`cash_movements`) y bitácora, y vaciar carrito. Clave de idempotencia contextual persistida para reintentos.
- [x] Idempotencia contextual: ligar clave a `user_id` y caja; rechazar intentos de usuarios ajenos con `Forbidden`; reintento autorizado recupera venta original incluso tras cierre de turno.
- [ ] Cancelación de pedido sin cobrar y anulación/devolución de venta cobrada como operación separada, con permiso, motivo y reversión auditable.
- [x] Recuperar ticket por folio desde líneas de venta guardadas con productos, cantidades, precios, subtotales, total, pago, nombre de cajero y notas; inmutabilidad verificada tras reapertura de base.
- [x] Implementar `get_recent_tickets(limit)` para historial de ventas y reimpresión autorizada de tickets.
- [x] Corregir confirmación: revalidar precios/stock bajo transacción, verificar multiplicaciones, guardar movimiento de caja y comprobar errores de receta, vaciado de carrito, auditoría y commit. Recuperar idempotencia antes de exigir turno actualmente abierto y revertir ante cualquier fallo.
- **Terminado:** dos platillos y una bebida generan un ticket y tres consumos apropiados; doble clic/reintento no duplica venta ni descuento de inventario.

### BE-06 — Caja, reportes y conciliación

- [x] Implementar servicios SQLite básicos de apertura/cierre y persistencia de turno, fondo y efectivo final (migración 006 con tabla `cash_movements`).
- [ ] Abrir/cerrar turno con fondo inicial, entradas/salidas y arqueo; impedir turnos activos incompatibles y documentar diferencias.
- [ ] Reportes paginados por periodo, producto, método de pago declarado, inventario, mermas y movimientos; exportación según formato confirmado.
- [ ] Conciliar totales de ventas, pagos registrados y movimientos de caja; índices y consultas acotadas para el hardware objetivo.
- **Terminado:** corte reproduce cifras desde registros inmutables y una fecha/rango amplio no congela la ventana.

### BE-07 — Respaldo local y remoto opcional

- [x] Crear instantánea SQLite consistente con `sqlite3_backup_*` y comprobar integridad del archivo resultante en prueba de almacenamiento.
- [ ] Agregar metadatos de versión, flujo completo de copia local desde UI y restauración guiada con respaldo previo.
- [ ] Cifrado del archivo de copia y transferencia HTTPS al servidor externo opcional, autenticación, reintentos, estado y retención; el servidor no recibe consultas operativas.
- [ ] Dueño puede activar/desactivar, ejecutar y consultar copias; solo identidad técnica de Desarrollador cambia dirección, credenciales, certificados y retención.
- [ ] Exponer esas operaciones técnicas únicamente al servicio de la subsección **BackOffice → Servidor de respaldos**; una cuenta Dueño o un rol configurable no puede invocarlas aunque intente omitir la interfaz.
- [ ] Trabajo en segundo plano sin bloquear venta; fallo remoto deja base local y POS funcionando. Restauración siempre termina en la base local tras comprobar compatibilidad.
- **Terminado:** copia local y remota restauran datos de prueba; apagar la red durante el envío no afecta una venta ni corrompe la base.

### BE-08 — Pruebas, robustez y paquete

- [x] BE-05: completar comprobación de errores y atomicidad del carrito. Errores de prepare, bind y step propagados rigurosamente; escrituras comprobadas incluyendo `carts.updated_at`. Mutaciones ejecutadas dentro de `TransactionScope` con rollback completo ante cualquier error. Pre-validación de desbordamiento en subtotales de línea y totales acumulados antes de persistir, preservando el borrador 100% intacto ante fallos. Probado en `test_storage` (`test_i2_cart_atomic_rollback_and_overflow_integrity`) con fallos inducidos vía triggers SQLite en `DELETE` durante fusión y en `carts.updated_at`.
- [x] BE-05: instantánea inmutable del nombre del cajero para tickets históricos mediante migración 007; cambio de nombre posterior en `users` no altera ticket emitido ni historial. Ventas previas a migración 007 muestran explícitamente "Nombre histórico no disponible (ID)" y están desacopladas de `users.display_name`.
- [x] BE-02/05: exigir `pos.reprint` para historial/reimpresión en el servicio; acceso al ticket recién vendido reservado al vendedor autorizado (`pos.sell`). Verificado con roles sin `pos.reprint`.
- [x] BE-00: validar agrupación estricta de separadores monetarios (comas de miles estrictas de 3 dígitos, rechazo de `1,5`) y límites exactos del parser/representación (`INT64_MIN` y rango de 64 bits sin desbordamiento ni UB). Verificado en `test_contracts`.

- [x] Ejecutar `ContractsTest` y `StorageTest` en Debug y Release, con casos de unidades/costos, identidad, roles, último Dueño, migraciones y respaldo local (11 suites en StorageTest, 100% aprobadas).
- [x] Incorporar `tests/test_i1_integration.py` y `tests/test_i2_integration.py` al repositorio con pruebas del ejecutable y SQLite aisladas; resultados aprobados al 100% en Debug y Release registrados en `task.md`.
- [x] Ampliar `tests/test_i2_integration.py` con los 9 requerimientos completos de la Puerta I2 sobre el ejecutable real: edición de cantidades/notas en UI, fusión de líneas compatibles, eliminación de línea y estado restante, cobro multidominio atómico, cierre y reapertura con recuperación de ticket histórico, autorización estricta con/sin `pos.reprint`, cancelación de diálogo nativo de impresión del sistema, simulación determinista de error de impresión vía `CAFETERIA_SIMULATE_PRINT_ERROR=1` (`TicketPrintout::OnBeginDocument` retorna `false`) sin corromper la venta, e idempotencia estricta sin duplicación. Sustitución de llamadas bloqueantes Win32 por `SendMessageTimeoutW` (`SMTO_ABORTIFHUNG`) y `PostMessageW`.
- [x] Corregir en I2 el clic de apertura de turno con PostMessageW y cerrar su diálogo de éxito/error mediante búsqueda por PID y timeout; el escenario favorable termina en ambas variantes.
- [x] Validar Money::from_string / try_parse sin excepciones no controladas, truncamiento silencioso ni desbordamiento en pesos * 100; rechazar formatos inválidos y precisión no admitida.
- [x] Limitar consumo a la receta vigente: confirm_sale consulta todas las versiones por product_id (`ORDER BY version DESC LIMIT 1`). Probar dos versiones y comprobar que solo se consume la actual.
- [x] Vincular recuperación por clave de idempotencia al usuario/caja y contexto original autorizado; la consulta actual filtra por clave y revalida propietario.
- [x] Añadir pruebas negativas y de integridad en `StorageTest`: falta de sesión en servicios protegidos (`Forbidden`), aislamiento estricto de líneas ajenas en carrito (`NotFound`), stock insuficiente con rollback total y preservación de borrador, conflicto por cambio de precio en catálogo, movimientos de caja `INITIAL_CASH` y `SALE`, e idempotencia recuperable tras cierre de turno sin duplicación.
- [ ] Pruebas de dominio adicionales para promociones avanzadas y cambio de moneda mixta.
- [ ] Pruebas de integración SQLite para fallos físicos de disco/red durante respaldos remotos.
- [ ] Medir RAM, arranque, búsquedas, 20 líneas de carrito, confirmación, reportes y respaldo en la PC real con datos representativos; optimizar índices y tamaño de imágenes antes de añadir cachés grandes.
- [ ] Probar instalador/paquete en equipo limpio con sistema operativo confirmado; verificar que no requiere servicio de base de datos ni librerías instaladas manualmente.
- **Criterio de terminado (pendiente):** resolver los cinco pendientes de la auditoría posterior a los tres prompts y repetir las comprobaciones correspondientes. CTest pasa 3/3 en Debug/Release, pero I2 sigue parcial en software, hardware y aceptación humana.

## Secuencia y asignación

1. BE-00 y BE-01 primero: contratos y repositorio de prueba desbloquean FE-00/FE-02.
2. BE-02/BE-03 pueden desarrollarse en paralelo tras acordar esquema y permisos; BE-04 depende de ingredientes/recetas.
3. BE-05 depende de BE-02 a BE-04; la UI de venta puede avanzar con servicios de prueba.
4. BE-06 depende de ventas y caja; BE-07 depende de esquema estable y mecanismo de identidad técnica.
5. BE-08 se ejecuta por módulo y culmina con las puertas I1–I7 del plan de frontend; I7 añade validación del perfil táctil y no sustituye las pruebas de negocio.

## Decisiones bloqueantes por módulo

- Antes de BE-04: método de valuación de compras a precios diferentes, unidad/precisión por ingrediente, existencia global o por ubicación y política de sobreventa.
- Antes de BE-05: métodos declarados, pagos mixtos, impuestos/descuentos, redondeo y reglas de promociones.
- Antes de BE-06: definición de turno, formato del corte, ticket, impresora y reportes obligatorios.
- Antes de BE-07: ubicación y frecuencia de respaldos, protocolo y capacidad del servidor remoto, retención y método de provisionamiento del Desarrollador.

Se pueden implementar esquemas y contratos mientras estas decisiones se aclaran; no se debe fijar silenciosamente una regla contable o de caja. El cierre del backend requiere evidencia de **venta, inventario, caja, permisos y restauración funcionando desde la ventana nativa**, además de la puerta I6 de navegación y BackOffice.

### Estado y verificación de software I2 — 2026-10-02

Los cinco pendientes de software de I2 fueron declarados completados por el agente; `Auditorias/auditoria-2026-10-18.md` los deja parciales:

- [x] Corte seguro de caracteres UTF-8 en `split_long_word_utf8` al envolver palabras largas, sin fragmentar secuencias multibyte.
- [x] Tipado exacto `PDWORD_PTR` (`ULONG_PTR = ctypes.c_size_t`) en `SendMessageTimeoutW` y comprobación de errores en helpers Win32 de pruebas.
- [x] Propagación estricta de resultados de bind en `SqliteCartService` y rollback transaccional completo, verificado con simulación de fallo.
- [x] Reintento real de confirmación en ejecutable nativo (`ISaleService::confirm_sale`) con verificación de igualdad total de tablas operacionales y ticket histórico completo en UI.
- [x] Validación de límites físicos de área imprimible (<20 columnas, <1 línea) con error explícito y cancelación limpia; medición de pie de página.
- [x] Supresión de aserciones eliminada (`wxDisableAsserts` removido) y corrección de causa raíz en `TicketPrintout` y `TicketPrintDialog`.

Límites de verificación:
- La puerta I2 permanece **PARCIAL** en hardware físico de destino (impresora térmica real y monitor táctil en PC de caja) y en aceptación humana (0 casos de `frontend/qa-visual.md` cerrados). No avanzar a I3.


### Configuración de impresión por puesto y perfiles térmicos — 2026-10-02 [IMPLEMENTADO]

- [x] Contrato `IPrintConfigService` y servicio persistente `SqlitePrintConfigService` (`include/app/contracts/print_config_service.hpp`, `src/services/sqlite_print_config_service.cpp`) para guardar y consultar impresora y perfil térmico activo (Profile55mm / Profile80mm) en `station_printer.cfg` del puesto (`CAFETERIA_DATA_DIR`).
- [x] Exigencia de permiso operativo `config.manage` en la capa de servicios. La interfaz no escribe SQL ni archivos directamente.
- [x] Validación estricta de anchos soportados (55 mm / 80 mm) y rechazo de perfiles inválidos o no soportados (ej. 58 mm).
- [x] Probado en CTest `StorageTest` (`test_i2_print_config_service_permissions`): persistencia por puesto, bloqueo a usuarios sin `config.manage` y rechazo de 58 mm.
- [x] Invariabilidad histórica: alternar perfiles o reimprimir tickets no modifica datos de ventas o líneas emitidas. Probado en CTest `TicketPrintingTest`.

### Verificación de Bloque 1 y Bloque 2 — 2026-10-02

- [x] **Aislamiento del mecanismo de prueba de idempotencia:**
  - Argumentos `--test-idempotency-key`, `--test-user` y `--test-pass` retirados por completo del ejecutable operativo `app_desktop.exe`.
  - Fallback `admin/admin` eliminado; cero credenciales por defecto.
  - Creado runner de prueba dedicado `tests/test_idempotency_runner.exe` que recibe credenciales estrictamente a través de `std::cin` (sin exposición en `argv` ni registros de logs).
  - Autenticación real por servicio (`IServicesRegistry::account_service->login`) y confirmación de venta (`confirm_sale`).
  - Verificada la invariabilidad fila por fila de las 8 tablas operacionales (`sales`, `sale_items`, `payment_records`, `ingredients`, `stock_movements`, `cash_movements`, `audit_events`, `cart_items`), excluyendo el evento de auditoría de login.
- [ ] La puerta I2 permanece **PARCIAL** por hardware de destino (impresora térmica real y monitor táctil en PC de caja) y aceptación visual humana. No avanzar a I3.

### Auditoría posterior a los tres bloques — 2026-10-03

Avances inspeccionados: runner aislado, username, único icono de agregado, fondos más limpios, Cantidad centrada y servicio/panel de perfiles térmicos. No equivalen a aceptación visual o impresión física.

- [ ] Aplicar impresora elegida y comprobar papel/controlador 55/80; unificar vista previa con geometría real.
- [ ] Inyectar ruta efectiva Unicode del puesto a configuración de impresión, incluido --data-dir.
- [ ] Guardar configuración sin cambios parciales ni éxito ante error de escritura; actualizar memoria después del éxito.
- [ ] Resolver recorte de Editar cantidad, actualización dinámica de Touch en navegación y centrado de otras entradas altas.
- [ ] Capturas de configuración/previa 55/80 y recorridos GUI de cambio/reinicio/reimpresión sin alterar historia.

Ver `Auditorias/auditoria-2026-10-03.md`. I2 sigue parcial; no avanzar a I3.

### Resolución de pendientes de backend — 2026-10-14

Los pendientes de backend de la auditoría 2026-10-03 fueron implementados y verificados:

- [x] **Inyección de ruta efectiva Unicode:** `SqlitePrintConfigService` recibe la ruta resuelta `config_dir` desde el arranque de la aplicación (`--data-dir` o `CAFETERIA_DATA_DIR`). Probado aislamiento por puesto y rutas con acentos y espacios en `StorageTest`.
- [x] **Guardado atómico y preservación de estado:** `SqlitePrintConfigService::save_to_disk` escribe en `.tmp`, verifica integridad de flujo (`good()`, `flush()`, `close()`), y renombra atómicamente. Si ocurre un error, la configuración en memoria no se modifica y el archivo previo en disco se preserva íntegro.
- [x] **Prueba unitaria en CTest:** Incorporada la prueba `test_i2_print_config_atomic_save_and_isolation()` en `StorageTest` verificando fallo inducido de E/S y consistencia de datos.
- [x] **Integridad histórica invariable:** Verificado que la gestión de perfiles de impresión no muta ninguna tabla operacional histórica (`sales`, `sale_items`, `payment_records`, `stock_movements`, etc.).
- [ ] **Límites:** Hardware físico (impresora térmica real y monitor táctil en PC de caja) y aceptación visual humana continúan pendientes. I2 permanece **PARCIAL**. No avanzar a I3.

## Auditoría 2026-10-18

Ver `Auditorias/auditoria-2026-10-18.md` y la sección homónima de `task.md`. I2 parcial en software, hardware y aceptación humana; no avanzar a I3.
