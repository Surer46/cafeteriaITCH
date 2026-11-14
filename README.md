# Cafetería ITCH II

Punto de venta de escritorio para cafetería: ventas, caja, inventario con recetas, reportes, cuentas y respaldos. Aplicación **nativa** en C++17 con wxWidgets y SQLite embebido, en un solo proceso, pensada para una PC modesta (Intel Core i5 de 2.ª generación, 4–6 GB de RAM). No usa navegador, Electron ni servidor de base de datos.

La especificación completa está en [`docs/specs.md`](docs/specs.md); las reglas de trabajo, en [`AGENTS.md`](AGENTS.md).

## Avance

**Al 2026-11-14: cerca del 52 % hacia la versión 1.0 y del 66 % hacia un piloto en caja.**

Son estimaciones del trabajo de cada etapa, no mediciones. Una etapa solo se cierra con pruebas del ejecutable real y, donde aplica, revisión humana y hardware.

| Etapa | Avance | Situación |
|---|---|---|
| **I1** Base nativa, SQLite y alta del Dueño | 100 % | Cerrada. |
| **I2** Venta, cobro, caja e impresión | 85 % | Software terminado. Faltan la revisión visual humana (0 de 7 casos), la impresora térmica real (55 y 80 mm), el monitor táctil y el escalado de pantalla al 125, 150 y 200 %. |
| **I3** Inventario, ingredientes y recetas | 40 % | En desarrollo desde el 2026-11-13, sin esperar la revisión humana de I2. Backend listo: ingredientes, compras, mermas y ajustes, movimientos, avisos de poco inventario, y costo y margen por receta con promedio móvil. Pantalla de Ingredientes lista; faltan Reabastecimiento, Productos y recetas, y Existencias. Sin decidir: combos, promociones y productos por peso. |
| **I4** Corte de turno y reportes | 35 % | Listos: resumen diario real, pantalla de Caja y turnos, Bitácora. Faltan la conciliación del corte y la pantalla de Reportes. |
| **I5** Respaldos y restauración | 55 % | Respaldo consistente, restauración y limpieza de respaldos antiguos (bajo petición, con vista previa), con pruebas. Configuración del servidor persistida. Faltan su pantalla y el servidor externo opcional. |
| **I6** Usuarios, roles y permisos | 95 % | Pantallas de Usuarios y de Roles. Los permisos se muestran con título, descripción e interruptores, agrupados por sección. Falta dar más alto a la lista de permisos y la revisión humana. |
| **I7** Perfil táctil, instalador y entrega | 35 % | Perfil Táctil elegible en Configuración y en el encabezado; acceso, subsecciones, campos de efectivo y listas desplegables a medida táctil, vigilado por una prueba (quedan 35 controles pequeños en Usuarios, interruptores de Roles, cobro e historial). Paquete portable en ZIP con autocomprobación (`--self-check`), regla de carpeta de datos sin cambios silenciosos (por equipo al instalar, junto al programa en la copia portable) y ficha de la PC de caja (`tools/ficha_pc.cmd`). Guion de instalador escrito, sin compilar. Faltan: datos de la PC de caja, instalador probado, prueba en otro Windows y con escalado de 125 % o más, licencias de terceros, icono y captura numérica táctil. |

### Pantallas reales hoy

Venta, cobro y ticket; Caja y turnos; Bitácora; Usuarios y cuentas; Roles y permisos; Configuración (pantalla del equipo e impresión); Ingredientes. Las demás subsecciones son marcadores de posición.

### Lo que frena la entrega

1. Revisión visual humana de I2 (casos en `frontend/qa-visual.md`).
2. Decisiones de negocio abiertas en la sección 9 de `docs/specs.md` (sobreventa, anulaciones, quién cierra caja, valuación de costos).
3. Datos de la PC de caja y de la impresora térmica.

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

- **CTest (7):** `ContractsTest`, `StorageTest`, `BackupTest`, `ReportsTest`, `TicketPrintingTest`, `DataDirPolicyTest` y `PackageTest` (esta última solo en Release; en Debug queda omitida).
- **Pruebas con el ejecutable real** (abren ventanas; una a la vez): `tests/test_i1_integration.py`, `test_i2_integration.py`, `test_fe08_fe09_visual_integration.py`, `test_f1_roles_panel.py`, `test_f2_audit_log.py`, `test_f2b_subnav.py`, `test_f7_ingredients_panel.py`, `test_t1_touch_profile.py`, `test_t2_touch_targets.py`. Solo son válidas con el escalado de Windows al 100 %, salvo `test_t1_touch_profile.py` y `test_t2_touch_targets.py`, que se adaptan al escalado. `test_f1_roles_panel.py` falla desde el 2026-11-14 en la ventana de 960x600 (en investigación). `test_i2_integration.py` ya usa el folio nuevo, pero en un escritorio con el diálogo de impresión moderno de Windows 11 falla al cancelarlo (pendiente).
- **Capturas:** `tests/capture_visual_qa.py` las deja en `qa_visual/screenshots/`.

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
5. Leer [`docs/ORQUESTADOR.md`](docs/ORQUESTADOR.md): contiene el estado, las decisiones tomadas y los bloques en cola. Los prompts en curso están en `Prompts/prompts-2026-11-13-ronda6-i7.md`.

Las conversaciones de los agentes no viajan con el repositorio; todo lo necesario para continuar está en esos archivos.

## Documentos

| Archivo | Contenido |
|---|---|
| [`docs/specs.md`](docs/specs.md) | Especificación funcional y técnica. |
| [`docs/entorno.md`](docs/entorno.md) | Preparar el entorno de desarrollo en cualquier PC. |
| [`frontend/plan.md`](frontend/plan.md), [`backend/plan.md`](backend/plan.md) | Listas de trabajo por capa. |
| [`docs/coordinacion-agentes.md`](docs/coordinacion-agentes.md) | Reparto de archivos y protocolo entre agentes. |
| [`Auditorias/auditoria-2026-10-18.md`](Auditorias/auditoria-2026-10-18.md) | Auditorías de cada bloque entregado. |
| [`Auditorias/auditoria-general-2026-11-12.md`](Auditorias/auditoria-general-2026-11-12.md) | Revisión de alcance y preparación para una versión móvil. |
