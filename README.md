# Cafetería ITCH II

Punto de venta de escritorio para cafetería: ventas, caja, inventario con recetas, reportes, cuentas y respaldos. Aplicación **nativa** en C++17 con wxWidgets y SQLite embebido, en un solo proceso, pensada para una PC modesta (Intel Core i5 de 2.ª generación, 4–6 GB de RAM). No usa navegador, Electron ni servidor de base de datos.

La especificación completa está en [`docs/specs.md`](docs/specs.md); las reglas de trabajo, en [`AGENTS.md`](AGENTS.md).

## Avance

**Al 2026-11-22: cerca del 78 % hacia la versión 1.0 y del 88 % hacia un piloto en caja.**

Son estimaciones del trabajo de cada etapa, no mediciones. Una etapa solo se cierra con pruebas del ejecutable real y, donde aplica, revisión humana y hardware.

| Etapa | Avance | Situación |
|---|---|---|
| **I1** Base nativa, SQLite y alta del Dueño | 100 % | Cerrada. |
| **I2** Venta, cobro, caja e impresión | 85 % | Software de venta terminado. Nuevo: reembolsos por producto y por pieza (mismo día, motivo obligatorio, devuelven existencias o registran merma, salen del efectivo del turno) y cargo extra configurable por pago con tarjeta (porcentaje más monto fijo, 0 por omisión, no se reembolsa); el backend está completo y sus pantallas están en curso. Los precios ya incluyen impuestos y no hay pagos mixtos. Faltan las pantallas de reembolso y cargo, la revisión visual humana (0 de 7 casos), la impresora térmica real (55 y 80 mm), el monitor táctil y el escalado al 150 y 200 % (100 y 125 % verificados). La pantalla Catálogo visual sigue como marcador. |
| **I3** Inventario, ingredientes, recetas, promociones y combos | 95 % | Software completo: ingredientes, compras, mermas y ajustes, movimientos, avisos de poco inventario, costo y margen por receta con promedio móvil, combos de precio fijo y promociones (porcentaje, monto fijo y N×M, con vigencia por fechas, días y horario), con todas sus pantallas. Faltan la pasada de pruebas al 100 % de escala y la revisión humana. |
| **I4** Corte de turno y reportes | 95 % | Completo en software: corte con arqueo, diferencia y motivo, historial de cortes, entradas y salidas de efectivo, Bitácora y Reportes por rango de fechas (día, producto, método de pago, cajero, mermas y ajustes, movimientos, compras) con exportación a CSV. El corte ya descuenta los reembolsos en efectivo. Falta la revisión humana. |
| **I5** Respaldos y restauración | 90 % | Respaldo manual verificado a carpeta o USB, restauración guiada con copia previa, limpieza con vista previa, respaldo automático al cerrar turno con copia a una segunda carpeta y recuperación probada tras una restauración fallida. Falta probarlo en la PC de la caja. Servidor externo aplazado por decisión del usuario. |
| **I6** Usuarios, roles y permisos | 97 % | Pantallas de Usuarios y de Roles terminadas, con los permisos en dos columnas, agrupados por sección y con márgenes legibles. Permiso nuevo «Hacer reembolsos» (solo el Dueño por omisión). Falta la revisión humana. |
| **I7** Perfil táctil, instalador y entrega | 80 % | Perfil Táctil con controles de al menos 48, teclado numérico propio, pantalla completa con F11, paquete portable con autocomprobación, instalador de Windows probado y actualizaciones firmadas desde GitHub Releases o USB. Rediseño de pantalla completa en curso, vigilado por una prueba permanente de 17 pantallas en 6 estados (pantalla completa, 1200×720 y 960×600, en Estándar y Táctil): Usuarios, Roles, Configuración y Respaldos terminadas; Venta, Inventario y Caja en curso. Faltan además crear las claves de firma, la prueba manual con UAC, datos de la PC de caja, prueba en otro Windows, monitor táctil real e icono. |

### Pantallas reales hoy

Venta (con descuentos de promociones), cobro y ticket; Caja y turnos; corte con arqueo e historial de cortes; Reportes; Bitácora; Usuarios y cuentas; Roles y permisos; Respaldos; Actualizaciones; Configuración (pantalla del equipo e impresión); Ingredientes; Reabastecimiento; Barra; Productos y recetas; Existencias; Promociones y combos. Sigue como marcador: Catálogo visual.

### Lo que frena la entrega

1. Pantallas de reembolso y de cargo por tarjeta, y el resto del rediseño de pantalla completa (I7).
2. Revisión humana de I2 (casos en `frontend/qa-visual.md`), I4, I5 e I6.
3. Decisiones de negocio abiertas en la sección 9 de `docs/specs.md` (descuentos manuales y redondeo, productos por peso, facturación).
4. Datos de la PC de caja y de la impresora térmica; claves de firma de actualizaciones y prueba manual con UAC.

## Arquitectura

Un solo proceso con capas separadas; la interfaz nunca escribe SQL ni calcula importes.

| Capa | Ubicación | Contenido |
|---|---|---|
| Contratos | [`include/app/contracts/`](include/app/contracts/) | `Money` en centavos enteros, `Result<T>`, DTO y fachadas de servicios. |
| Dominio | `src/domain/` | Unidades (masa, volumen, piezas) y costos sin coma flotante. |
| Almacenamiento | `src/storage/`, [`migrations/`](migrations/) | SQLite con claves foráneas y WAL; migraciones versionadas. |
| Servicios | `src/services/` | Cuentas, catálogo, carrito, ventas, inventario, turnos, reportes, respaldos. Comprueban permisos en cada operación. |
| Interfaz | [`src/frontend/`](src/frontend/) | Paneles wxWidgets. Cuatro secciones en la barra lateral: Punto de Venta, Inventario, Caja y Reportes, BackOffice. |

Reglas que no se negocian: dinero en centavos enteros; la venta confirmada se guarda en una sola transacción y no se duplica por doble clic; no hay cuentas ni contraseñas predeterminadas; la base local es la única fuente de datos y una falla de red nunca bloquea ventas.

## Compilar y probar

Requisitos: compilador C++17, CMake 3.16 o superior, Ninja y wxWidgets 3.2 o superior. SQLite viene incluido en `third_party/sqlite/`. Entorno verificado: Windows, GCC 16.1 (MinGW-w64), wxWidgets 3.3.3.

```powershell
cmake -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release -DwxWidgets_ROOT_DIR="<toolchain>/wxwidgets" -DwxWidgets_LIB_DIR="<toolchain>/wxwidgets/lib/gcc1610_x64_dll" -DwxWidgets_CONFIGURATION=mswu
cmake --build build_release --parallel 2
ctest --test-dir build_release --output-on-failure
```

Repetir con `build_debug` y `-DCMAKE_BUILD_TYPE=Debug`. Las pruebas se ejecutan siempre en ambas configuraciones.

- **CTest (23):** `ContractsTest`, `StorageTest`, `BackupTest`, `ReportsTest`, `ShiftCloseoutTest`, `ReportsRangeTest`, `RefundTest`, `CardSurchargeTest`, `TicketPrintingTest`, `DataDirPolicyTest`, `PackageTest` (solo en Release; en Debug queda omitida), `StartupTest`, `LoginLockoutTest`, `AuthorizationTest`, `StartupTimingSmokeTest`, `Sha256Test`, `HashMemorySmokeTest`, `InstallerTest` (solo en Release con Inno Setup; si no, omitida), `AutoBackupTest`, `PromotionsTest`, `ProductStockStatusTest`, `UpdateTest` y `EndSessionTest` (solo en Release).
- **Pruebas con el ejecutable real** (abren ventanas; una a la vez): `tests/test_i1_integration.py`, `test_i2_integration.py`, `test_fe08_fe09_visual_integration.py`, `test_f1_roles_panel.py`, `test_f2_audit_log.py`, `test_f2b_subnav.py`, `test_f7_ingredients_panel.py`, `test_t1_touch_profile.py`, `test_t2_touch_targets.py`, `test_t4_numeric_keypad.py`, `test_t5_fullscreen.py`, `test_t6_backups.py`, `test_t7_updates.py`, `test_t10_restock_bar.py`, `test_t11_products_recipes.py`, `test_t12_discounts_stock.py`, `test_t13_promotions_combos.py`, `test_t14_closeout_reports.py`, `test_t15_printer_missing.py`, `test_t16_fullscreen_layout.py` (pantalla completa y ventana normal de 17 pantallas). Válidas con Windows al 100 y al 125 % (módulo común `tests/gui_dpi.py`); con otro escalado se detienen. No deben correr a la vez que CTest, porque `EndSessionTest` también abre el programa. Las pantallas de T8 a T16 se verificaron al 125 %; falta su pasada al 100 %.
- **Capturas:** `tests/capture_visual_qa.py` las deja en `qa_visual/screenshots/`; `tests/measure_fullscreen_space.py` mide el espacio en pantalla completa y deja capturas y medidas en `qa_visual/fullscreen/`.
- **Turno de pruebas:** mientras corre una prueba con ventanas o CTest existe `..\gui-test.lock`; otro agente espera a que desaparezca.

Generar el paquete portable (solo desde Release):

```powershell
cd build_release
cpack -G ZIP -C Release
```

El ZIP lleva `CafeteriaITCH.exe`, sus DLL, migraciones, recursos y `portable.flag` (datos junto al programa). `CafeteriaITCH.exe --self-check --self-check-report=informe.txt` comprueba el paquete sin abrir ventanas. Un ejecutable sin `portable.flag` fuera de `build_release` y `build_debug` guarda en `%ProgramData%\CafeteriaITCH\data`.

Ejecutar con una carpeta de datos aparte:

```powershell
.\build_release\app_desktop.exe --data-dir="..\Cafeteria_QA_Manual"
```

## Retomar el trabajo en otra PC

Ningún archivo versionado depende de rutas de una PC concreta. Los pasos completos (qué instalar, dónde y cómo verificarlo) están en [`docs/entorno.md`](docs/entorno.md); un agente puede seguirlos por su cuenta, pidiendo aprobación para las descargas. En resumen:

1. Clonar el repositorio y traer también las ramas `backend/*`.
2. Dejar el compilador y wxWidgets en la carpeta hermana `..\toolchain`.
3. Crear la copia de trabajo del backend: `git worktree add ..\Cafeteria_ITCH_backend backend/<bloque>`.
4. Configurar `build_release` y `build_debug` (frontend) y, en la copia del backend, `build_be_release` y `build_be_debug`; compilar y correr CTest para verificar la línea base.
5. Leer [`docs/ORQUESTADOR.md`](docs/ORQUESTADOR.md): contiene el estado, las decisiones tomadas y los bloques en cola. Los prompts están en `Prompts/`, uno por bloque (`prompts-AAAA-MM-DD-<bloque>.md`).

Las conversaciones de los agentes no viajan con el repositorio; todo lo necesario para continuar está en esos archivos.

## Documentos

| Archivo | Contenido |
|---|---|
| [`docs/specs.md`](docs/specs.md) | Especificación funcional y técnica. |
| [`docs/entorno.md`](docs/entorno.md) | Preparar el entorno de desarrollo en cualquier PC. |
| [`frontend/plan.md`](frontend/plan.md), [`backend/plan.md`](backend/plan.md) | Listas de trabajo por capa. |
| [`docs/coordinacion-agentes.md`](docs/coordinacion-agentes.md) | Reparto de archivos y protocolo entre agentes. |
| [`Auditorias/`](Auditorias/) | Auditorías de cada bloque entregado: hasta la ronda 5 en `auditoria-2026-10-18.md`; desde la ronda 6, un archivo por bloque. |
| [`Auditorias/auditoria-general-2026-11-12.md`](Auditorias/auditoria-general-2026-11-12.md) | Revisión de alcance y preparación para una versión móvil. |
