# Prompts del 2026-11-12 — ronda 4, versiones enviadas tras el cambio de PC

Registro de los prompts tal como se enviaron después de mover el proyecto a otra PC. Sustituyen a las versiones de B4a, B4 y F5 de `Prompts/prompts-2026-11-12-ronda4.md`, que quedaron con rutas de la PC anterior y con un dato incorrecto sobre el filtrado de `backup.dev_config`. F0 es un bloque nuevo.

Las rutas absolutas de la PC en uso se sustituyen aquí por marcadores, porque los archivos versionados no llevan rutas de ninguna PC (`docs/entorno.md`):

- `<copia principal>`: copia de trabajo en `main` (frontend).
- `<worktree del backend>`: carpeta hermana `..\Cafeteria_ITCH_backend`.
- `<toolchain>`: carpeta hermana `..\toolchain`.

Al enviar un prompt se escriben las rutas reales.

Orden de envío: B4a (Claude Code Terminal) y F0 (Gemini 4 Hydrogen) en paralelo; tras aceptar F0 e integrar B4a, F5 (Gemini 4 Hydrogen) y B4 (Claude Code Terminal) en paralelo. Auditorías en `Auditorias/auditoria-2026-10-18.md`.

---

## B4a — Claude Code Terminal (`backend/b4a`). Aceptado en `1fc6dc5`

Cambios respecto a la versión original: lectura inicial de `AGENTS.md` (sesión sin contexto), estado actual del código, «no cambies el filtrado» en lugar de «backup.dev_config no se lista para el Dueño» (el servicio sí lo devuelve; lo oculta el panel), «idempotente» precisado como una segunda pasada del runner, y comprobación previa de los 21 ids.

```text
Eres el agente de backend de este proyecto. Empiezas sin contexto previo: antes de nada lee AGENTS.md completo y docs/coordinacion-agentes.md (roles y propiedad de archivos). No necesitas leer nada mas para este bloque.

Bloque B4a de backend, pequeno. Trabaja SOLO en <worktree del backend>, rama backend/b4a. No hagas commits. No compiles ni corras pruebas antes de editar: la linea base ya esta verificada (compila y CTest 5/5 en build_be_release y build_be_debug). No hace falta plan previo: es mecanico.

Objetivo: cada permiso tendra titulo corto, descripcion en lenguaje llano, seccion y orden, para que el panel de roles los muestre a una persona mayor con poca familiaridad con la tecnologia. Los textos ya estan decididos; copialos tal cual, con sus acentos.

Estado actual que debes conocer:
- La tabla permissions solo tiene id y description (migracion 001; filas en 002 y 008). Son 21 filas.
- PermissionDto (include/app/contracts/dto.hpp) tiene id, description y module. module no es columna: SqliteAccountService::list_permissions lo deriva del prefijo del id y ordena por id.
- El runner aplica cada migracion una sola vez (tabla schema_migrations), igual que 004, 005 y 007, que ya usan ALTER TABLE ADD COLUMN.

Archivos permitidos: migrations/009_permission_titles.sql (nuevo), include/app/contracts/dto.hpp, src/services/ e include/app/services/ (servicio de cuentas real y su mock), tests/test_*.cpp. No toques src/frontend/ ni tests/*.py. No edites migraciones 001 a 008.

1. Migracion 009: agrega a permissions las columnas title (texto), section (texto) y sort_order (entero), y actualiza las 21 filas con la tabla de abajo, incluida la descripcion nueva. No cambies los id ni role_permissions. Sigue el estilo de cabecera de las migraciones existentes.

2. PermissionDto gana title, section y sort_order. list_permissions los devuelve ordenados por sort_order. Devuelve las mismas filas que hoy: no agregues ni quites filtros (el panel es quien oculta backup.dev_config). El campo module se conserva con su calculo actual. El mock devuelve los mismos textos, secciones y orden que la migracion.

3. Si la interfaz deja de compilar por el cambio del DTO, no la edites: reportalo. No deberia pasar: solo se agregan campos.

4. CTest (TEST_CHECK, no assert): tras migrar, los 21 permisos tienen title, section y sort_order no vacios; los title son unicos; section es uno de los cuatro valores; list_permissions sale en orden de sort_order; ejecutar el runner dos veces no cambia nada; los permisos asignados a role-owner y role-cashier no cambian. Si alguna prueba existente fija 8 como ultima version del esquema, actualizala a 9 y reportalo; no relajes ninguna otra asercion.

Tabla (id | section | orden | title | description):
pos.sell | Punto de Venta | 10 | Vender y cobrar | Registrar ventas y cobrarlas en la caja.
pos.visual_catalog | Punto de Venta | 20 | Usar el catálogo con fotos | Elegir productos desde las tarjetas con imagen, como en un menú.
pos.notes | Punto de Venta | 30 | Escribir notas en un pedido | Añadir indicaciones a un producto, por ejemplo: sin azúcar.
pos.reprint | Punto de Venta | 40 | Reimprimir tickets | Volver a imprimir el ticket de una venta ya cobrada.
pos.cancel_order | Punto de Venta | 50 | Cancelar un pedido sin cobrar | Vaciar el pedido en curso antes de cobrarlo.
pos.void_sale | Punto de Venta | 60 | Anular una venta cobrada | Cancelar una venta que ya se cobró. Queda registrado quién lo hizo y por qué.
inventory.view | Inventario | 110 | Ver existencias | Consultar cuánto hay de cada producto e ingrediente.
inventory.manage_products | Inventario | 120 | Administrar productos y recetas | Crear, cambiar o dar de baja productos, sus precios y sus recetas.
inventory.restock | Inventario | 130 | Registrar compras | Anotar lo que se compra y cuánto costó, para sumarlo al inventario.
inventory.adjust | Inventario | 140 | Registrar mermas y ajustes | Anotar pérdidas, traslados y correcciones del inventario.
inventory.manage_thresholds | Inventario | 150 | Definir avisos de poco inventario | Fijar la cantidad mínima de cada ingrediente para que el sistema avise.
inventory.promotions | Inventario | 160 | Administrar promociones y combos | Crear y cambiar promociones y combos.
shift.operate | Caja y Reportes | 210 | Abrir y cerrar caja | Abrir turno, registrar entradas y salidas de dinero y hacer el corte.
reports.view | Caja y Reportes | 220 | Ver reportes | Consultar los reportes de ventas e inventario.
audit.view | Caja y Reportes | 230 | Ver la bitácora | Consultar el historial de lo que cada persona hizo en el sistema.
users.manage | BackOffice | 310 | Administrar cuentas | Crear cuentas de usuario, cambiarlas o desactivarlas.
roles.manage | BackOffice | 320 | Administrar roles y permisos | Crear roles y decidir qué puede hacer cada uno.
config.manage | BackOffice | 330 | Cambiar la configuración | Ajustar opciones del negocio, como la impresora y el tamaño del ticket.
backup.manage | BackOffice | 340 | Hacer respaldos | Activar y ejecutar copias de seguridad de la información.
backup.restore | BackOffice | 350 | Restaurar un respaldo | Recuperar la información desde una copia. Reemplaza los datos actuales.
backup.dev_config | BackOffice | 990 | Configurar el servidor de respaldos | Configuración técnica del servidor remoto (exclusivo Desarrollador).

Si algun id de la tabla no existe en la base tras las migraciones 001 a 008, o existe alguno que no esta en la tabla, detente y reportalo antes de escribir la migracion.

Verificacion. Antepon al PATH de tu terminal <toolchain>\mingw64\bin y <toolchain>\wxwidgets\lib\gcc1610_x64_dll. Las carpetas ya estan configuradas:
   cmake --build build_be_debug --parallel 2
   ctest --test-dir build_be_debug --output-on-failure
   cmake --build build_be_release --parallel 2
   ctest --test-dir build_be_release --output-on-failure
No ejecutes pruebas GUI. No instales nada. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual.

Entrega corta: archivos modificados, comandos con codigo de salida, numero de TEST_CHECK nuevos y limites de lo verificado.
```

---

## F0 — Gemini 4 Hydrogen (`main`). Aceptado en `ba8bd75`

Bloque nuevo: quitar de `tests/*.py` la ruta fija del toolchain de la PC anterior y correr la batería GUI por primera vez en la PC nueva. Sirvió además de calibración de Gemini 4 Hydrogen.

```text
Eres el agente de frontend de este proyecto. Empiezas sin contexto previo: antes de nada lee AGENTS.md completo y docs/coordinacion-agentes.md (roles y propiedad de archivos). No leas nada mas para este bloque.

Bloque F0, pequeno y mecanico. Trabaja SOLO en <copia principal>, rama main. No hagas commits. No compiles ni corras pruebas antes de editar: la linea base ya esta verificada (compila y CTest 5/5 en build_release y build_debug, que ya existen).

Contexto: el proyecto se movio a otra PC. Los archivos versionados ya no deben llevar rutas absolutas de ninguna PC. Ocho scripts de tests/ todavia anteponen al PATH del proceso hijo una ruta fija de la PC anterior:
   D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll;
Esa ruta no existe aqui y no hace falta: CMake copia las DLL del compilador y de wxWidgets a la carpeta de compilacion, junto al ejecutable.

Archivos que puedes editar, y ningun otro:
- tests/capture_visual_qa.py
- tests/diagnose_startup.py
- tests/test_f1_roles_panel.py
- tests/test_f2_audit_log.py
- tests/test_f2b_subnav.py
- tests/test_fe08_fe09_visual_integration.py
- tests/test_i1_integration.py
- tests/test_i2_integration.py
No toques src/, include/, migrations/, CMakeLists.txt, tests/*.cpp ni documentos. Otro agente esta trabajando en el backend.

Trabajo:
1. En cada uno de los ocho archivos quita SOLO ese prefijo fijo D:\Temp\... del PATH. Conserva el resto de la expresion tal cual (la carpeta del ejecutable o work_dir, y el PATH heredado). No cambies nada mas: ni tiempos de espera, ni aserciones, ni umbrales, ni textos.
2. Comprueba con una busqueda que no queda ninguna ruta D:\ en tests/*.py y reporta el resultado de esa busqueda.
3. Ejecuta las pruebas GUI, una a la vez y en este orden. Antes de cada una comprueba con tasklist que no haya ningun app_desktop.exe corriendo; si lo hay, espera 30 s y vuelve a comprobar:
   python -u tests/test_i1_integration.py
   python -u tests/test_i2_integration.py
   python -u tests/test_fe08_fe09_visual_integration.py
   python -u tests/test_f1_roles_panel.py
   python -u tests/test_f2_audit_log.py
   python -u tests/test_f2b_subnav.py
   No ejecutes tests/capture_visual_qa.py: regenera capturas y no toca en este bloque.
4. Si una prueba falla, repitela UNA vez a solas (hay un intermitente conocido por tiempo de espera tras el inicio de sesion en test_i1_integration y test_f2_audit_log). Si falla dos veces, NO la arregles ni relajes nada: copia el error textual y continua con la siguiente. Esta es una PC distinta, con otra pantalla y otro escalado; un fallo aqui es informacion util, no algo que debas ocultar.

No instales nada. Las pruebas solo necesitan Python con Pillow, que ya esta instalado. Si falta algun modulo, detente y reporta cual.

Entrega corta y literal:
- archivos modificados y cuantas lineas cambio cada uno;
- resultado de la busqueda de D:\ en tests/*.py;
- por cada prueba: comando, codigo de salida, y si paso a la primera, a la segunda o fallo dos veces, con el error textual en ese caso;
- resolucion de pantalla y escalado de Windows de esta PC;
- limites de lo verificado. No escribas que algo funciona si no lo ejecutaste.
```

---

## F5 — Gemini 4 Hydrogen (`main`). Enviado tras integrar B4a (`2bdd6ae`)

Cambios respecto a la versión original: contexto de B4a ya integrado, aviso de que `test_f1_roles_panel.py` falla en `main` hasta este bloque, localizar permisos por el título leído de SQLite, conservar el filtro de `backup.dev_config` del panel, y la condición de escalado al 100 % para las pruebas GUI (hallazgo de F0).

```text
Bloque F5 de frontend. Ya leiste AGENTS.md y docs/coordinacion-agentes.md en el bloque anterior; si esta es una sesion nueva, leelos primero. Trabaja SOLO en <copia principal>, rama main. No hagas commits. No compiles ni corras pruebas antes de editar: main compila y CTest pasa 5/5 en build_release y build_debug.

Contexto: el backend ya entrego (bloque B4a, integrado en main) titulo, descripcion, seccion y orden para cada permiso. PermissionDto (include/app/contracts/dto.hpp) tiene ahora title, description, section y sort_order, y list_permissions los devuelve ordenados por sort_order. Aviso: tests/test_f1_roles_panel.py HOY FALLA en main porque busca las casillas por los textos antiguos de la descripcion (Consulta de bitacora, Venta y cobro, Consulta de existencias). Es esperado: este bloque reescribe esa parte.

Cambio pedido por el usuario: en BackOffice > Roles y permisos, cada permiso se activa con un interruptor deslizante (pastilla con perilla, como los ajustes de un telefono) en lugar de una casilla. En perfil Touch debe ser mas grande y comodo para recorrer la lista y guardar.

Archivos que puedes editar, y ningun otro:
- nuevos: src/frontend/toggle_switch.hpp y src/frontend/toggle_switch.cpp
- CMakeLists.txt (solo agregar toggle_switch.cpp y .hpp a las fuentes de app_desktop, junto a ui_helpers)
- src/frontend/backoffice_roles_panel.cpp y .hpp
- tests/test_f1_roles_panel.py
- tests/capture_visual_qa.py solo si las capturas 09 y 10 dejan de generarse
No toques ui_helpers.*, theme.*, otros paneles, contratos, servicios ni migraciones. Usa ui_helpers y Theme tal como estan. Otro agente esta trabajando en el backend.

1. Control ToggleSwitch (reutilizable, dibujado por la aplicacion sobre wxControl):
   - tamano: 44 x 24 en Estandar y 60 x 32 en Touch, pasados por FromDIP;
   - activado: pastilla con Theme::ColorBrand y perilla a la derecha; desactivado: pastilla gris y perilla a la izquierda. El estado no puede depender solo del color: la posicion de la perilla lo indica, y ademas dibuja una marca dentro de la perilla cuando esta activado;
   - deshabilitado: colores atenuados y no responde;
   - teclado: recibe foco con Tab, muestra el foco y cambia con Espacio;
   - emite un evento al cambiar y expone GetValue y SetValue;
   - en Windows responde a BM_GETCHECK y BM_SETCHECK con su estado, para que las pruebas y las herramientas de accesibilidad lo lean;
   - sin animacion ni temporizadores (PC de pocos recursos). Dibujo con doble bufer, sin parpadeo.

2. Panel de roles:
   - el usuario principal es una persona mayor con poca familiaridad con la tecnologia: prioriza texto grande, mucho aire y nada de jerga. La lista no debe verse apretada;
   - cada permiso es una fila con dos textos a la izquierda y el interruptor a la derecha, centrado verticalmente respecto a la fila. Arriba el TITULO (PermissionDto.title) en negrita y color de texto principal, con la fuente de cuerpo o mayor, nunca menor; debajo la DESCRIPCION (PermissionDto.description) en Theme::ColorTextSecondary, que puede ocupar varias lineas. No inventes ni cambies textos: usa los del servicio;
   - agrupa los permisos bajo cuatro encabezados, los mismos de la barra lateral y en ese orden: Punto de Venta, Inventario, Caja y Reportes, BackOffice. El grupo y el orden vienen en PermissionDto.section y PermissionDto.sort_order; quita la funcion get_module_display_name y la agrupacion por modulo;
   - separador visible entre cada permiso: una linea horizontal de 1 px con Theme::ColorBorderSubtle a todo el ancho de la fila, con al menos 10 px de aire arriba y abajo del texto (14 en Touch). Entre un grupo y el siguiente deja al menos 20 px de espacio adicional antes del encabezado;
   - un clic o toque en cualquier punto de la fila cambia el interruptor;
   - alto minimo de fila: 52 en Estandar, 68 en Touch;
   - rol Dueno: interruptores deshabilitados. Rol Cajero: editables. Roles propios: editables. Sin cambios en nombres, textos, permisos ni reglas;
   - el boton Guardar sigue visible sin desplazarse; a su lado muestra cuantos permisos estan activados (por ejemplo: 6 de 20 permisos);
   - role-dev y backup.dev_config siguen sin aparecer (el panel ya filtra backup.dev_config: conserva ese filtro).

3. tests/test_f1_roles_panel.py: sustituye las comprobaciones de casillas por las equivalentes de interruptor, localizando cada permiso por su titulo leido de la tabla permissions de SQLite, no por textos escritos a mano en la prueba. Nada de lo que hoy se comprueba puede perderse (persistencia en SQLite y tras reabrir, Cajero editable, Dueno en solo lectura, columnas, filas Touch, campos centrados, geometria a 960x600). Nuevas aserciones, midiendo y no a ojo:
   - hay un interruptor por permiso visible (20 para el Dueno);
   - tamano del interruptor al menos 44 x 24 en Estandar y 60 x 32 en Touch (GetWindowRect);
   - alto de fila al menos 52 en Estandar y 68 en Touch;
   - cada fila muestra titulo y descripcion no vacios, iguales a los de la tabla permissions de SQLite para ese permiso; el titulo queda encima de la descripcion y ninguno se recorta ni se solapa con el interruptor;
   - hay exactamente cuatro encabezados de grupo, con los nombres y el orden de la barra lateral;
   - entre cada par de permisos consecutivos del mismo grupo se detecta por pixeles una linea separadora horizontal; falla si falta alguna;
   - centro vertical del interruptor a 3 px o menos del centro de su fila;
   - borde derecho de todos los interruptores alineado (diferencia de 2 px o menos) y ninguno recortado por el area visible;
   - posicion de la perilla leida por pixeles: a la derecha si BM_GETCHECK dice activado, a la izquierda si no; falla si no se detecta la perilla;
   - clic en el interruptor cambia el estado; clic sobre el titulo y sobre la descripcion tambien; Espacio con el foco tambien;
   - con el rol Dueno, un clic no cambia el estado;
   - el contador junto a Guardar coincide con el numero de interruptores activados.
   Prohibido relajar o quitar una asercion para que pase. Si una medida no se puede cumplir, detente y reporta el numero medido.

Escalado de pantalla: las pruebas GUI de este proyecto solo son validas con Windows al 100 por ciento (96 DPI). Antes de ejecutar cualquier prueba GUI comprueba el escalado; si no es 100 por ciento, detente y reportalo, no adaptes nada.

Verificacion (toolchain: antepon al PATH <toolchain>\mingw64\bin y <toolchain>\wxwidgets\lib\gcc1610_x64_dll):
   cmake --build build_release --parallel 2
   cmake --build build_debug --parallel 2
   ctest --test-dir build_release --output-on-failure
   ctest --test-dir build_debug --output-on-failure
   python -u tests/test_f1_roles_panel.py
   python -u tests/capture_visual_qa.py
Antes de cada prueba GUI comprueba con tasklist que no haya ningun app_desktop.exe corriendo. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual. No instales nada.

Entrega corta y literal: archivos modificados; comandos con codigo de salida; medidas del interruptor y de las filas en Estandar y Touch; rutas de las capturas 09 y 10; aserciones de la prueba anterior que hayas eliminado o cambiado y por que; limites de lo verificado. No escribas que algo funciona si no lo ejecutaste.
```

---

## B4 — Claude Code Terminal (`backend/b4`, desde `main` con B4a). Enviado en paralelo con F5

Cambios respecto a la versión original: estado actual del código (5 copias de `generate_id`, 6 de `current_timestamp`, `cash_registers` sin código de caja, dependencia de `V-10001` en `test_i2_integration.py`), límite sobre `CMakeLists.txt` para no chocar con F5, espera explícita de aprobación del plan y el hallazgo de los archivos temporales que deja `test_storage.exe`.

```text
Bloque B4 de backend. Si esta es una sesion nueva, lee primero AGENTS.md completo y docs/coordinacion-agentes.md. Trabaja SOLO en <worktree del backend>, rama backend/b4 (ya creada desde main, incluye B4a y la migracion 009). No hagas commits. No compiles ni corras pruebas antes de editar: la linea base esta verificada (CTest 5/5 en build_be_release y build_be_debug). Lee Auditorias/auditoria-general-2026-11-12.md, apartado B.

Objetivo: cuatro ajustes de datos que hoy cuestan poco y con ventas acumuladas costarian mucho. No construyas sincronizacion, servidor ni nada de red.

Estado actual que debes conocer:
- generate_id esta copiada en 5 servicios (sqlite_cart, catalog, inventory, sale y shift): prefijo, milisegundos y un contador que reinicia al arrancar.
- current_timestamp esta copiada en 6 servicios (los 5 anteriores y sqlite_account) y guarda hora local sin zona.
- El folio se genera en sqlite_sale_service.cpp como V- mas 10001 mas COUNT de ventas, con bucle de comprobacion.
- cash_registers solo tiene id y name; shifts referencia register_id. No existe hoy un codigo corto de caja.
- tests/test_i2_integration.py (prueba GUI, no es tuya) busca la venta por el folio V-10001.

Archivos permitidos: src/domain/, src/storage/, src/services/, include/app/, migrations/ (solo archivos nuevos, a partir de 010), tests/test_*.cpp y CMakeLists.txt (solo para fuentes nuevas de bibliotecas o pruebas; no toques la lista de fuentes de app_desktop: el otro agente la esta editando). No toques src/frontend/, tests/*.py ni migraciones ya aplicadas (001 a 009).

Presenta primero un plan para revision y espera aprobacion antes de escribir codigo. El plan debe resolver las dudas marcadas como DECIDIR.

1. Identificadores. Deja una sola funcion compartida que conserve el prefijo y use 128 bits aleatorios de una fuente adecuada (UUID v4 o equivalente). Los identificadores ya guardados no se tocan. DECIDIR: fuente de aleatoriedad en MinGW y fuera de Windows, sin meter dependencias de plataforma en el dominio.

2. Fechas. La columna local existente y su formato YYYY-MM-DD HH:MM:SS NO cambian: la usan la interfaz, los tickets y el reporte diario. Agrega el instante en UTC en una columna nueva en las tablas de hechos (como minimo sales, payment_records, cash_movements, stock_movements, purchase_entries, shifts y audit_events) y escribela en cada alta. Una sola funcion compartida produce ambos valores del mismo instante. DECIDIR: que hacer con las filas anteriores (convertirlas con la zona del equipo al migrar, o dejarlas en NULL y documentarlo); justifica la opcion.

3. Ultima modificacion. Agrega updated_at a products, ingredients, recipes y a las demas tablas de catalogo que se editan (revisa cuales), y actualizalo en cada cambio desde los servicios. Filas existentes: igual a created_at.

4. Folio. Dos cajas generarian hoy el mismo. Los folios nuevos deben incluir un codigo de la caja del turno. Los folios ya emitidos no cambian y deben seguir reimprimiendose. DECIDIR: de donde sale el codigo de caja (no existe; cash_registers solo tiene id y name) y el formato exacto; no puede romper la busqueda por folio ni el ancho del ticket de 55 mm. Indica en el plan como afecta a la busqueda de V-10001 en tests/test_i2_integration.py, sin editar esa prueba.

Reglas: migraciones nuevas versionadas, aplicadas una sola vez por el runner y sin efecto al ejecutarlo de nuevo; dinero y cantidades sin float ni double; no relajar pruebas existentes.

Pruebas CTest nuevas, con TEST_CHECK y no solo assert:
- 100000 identificadores generados sin repeticion, y formato con prefijo;
- base creada con migraciones 001 a 009 y con datos, migrada a la nueva version: datos intactos, columnas nuevas presentes, y volver a ejecutar el runner no cambia nada;
- una venta nueva guarda hora local y UTC del mismo instante;
- editar un producto cambia updated_at y no created_at;
- folios de dos cajas distintas no chocan; un folio antiguo V-numero sigue recuperable por get_ticket.
Las pruebas no deben dejar archivos en el directorio desde el que se ejecutan: hoy test_storage.exe deja test_snapshot.bak.tmp-shm y -wal si se lanza desde la raiz. Localiza que prueba lo causa y reportalo; corrigelo solo si esta en tests/test_*.cpp.

Verificacion. Antepon al PATH <toolchain>\mingw64\bin y <toolchain>\wxwidgets\lib\gcc1610_x64_dll:
   cmake --build build_be_debug --parallel 2
   ctest --test-dir build_be_debug --output-on-failure
   cmake --build build_be_release --parallel 2
   ctest --test-dir build_be_release --output-on-failure
No ejecutes pruebas GUI. No instales nada. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual.

Entrega: archivos, comandos con codigo de salida, decisiones tomadas, pruebas GUI que podrian verse afectadas por el folio, y limites de lo verificado.
```

---

## B4 — respuesta al plan de Claude Code Terminal (2026-11-12)

El plan propuso: `generate_id` única con UUID v4 y `random_bytes` en `app_crypto`; reloj compartido con hora local y UTC; migración 010 con columnas `_utc` y `updated_at`; filas anteriores en NULL; código de caja en `cash_registers.code` y folio `V-<código>-<5 dígitos>`. Aprobado por el usuario (incluido el formato del folio) con estos ajustes del orquestador:

```text
Plan de B4 aprobado con ajustes. Si: filas anteriores en NULL y folio V-<codigo>-<5 digitos>. Aplica estos cambios al plan antes de escribir codigo:

1. Archivos: quedas autorizado a crear random_bytes en src/crypto/ e include/app/crypto/ (no estaban en la lista). No modifiques password_hasher ni su comportamiento.

2. Capas: no enlaces app_domain con app_crypto. El dominio se queda sin dependencias de plataforma, ni siquiera transitivas. Pon generate_id en la capa de servicios (include/app/services/ y src/services/), que es quien la usa. El reloj (clock) si puede ir en dominio porque solo usa la biblioteca estandar.

3. Folio:
   - el codigo sale de la caja del turno abierto (register_id del turno), no del literal reg-01;
   - el numero siguiente se calcula dentro de la misma transaccion de la venta;
   - el respaldo cuando una caja no tiene codigo no puede alargar el folio: limitalo a 4 caracteres A-Z0-9 como el codigo normal, y agrega una prueba para ese caso;
   - en la migracion, las cajas distintas de reg-01 reciben C mas un ordinal determinista (por rowid) y unico; documentalo en la cabecera;
   - un reintento idempotente de la misma venta debe devolver el mismo folio: las pruebas existentes que lo cubren no se tocan.

4. Respaldos: confirma con una prueba que un respaldo hecho con el esquema en version 9 se puede restaurar despues de este bloque y queda migrado a la version nueva. Si el flujo de restauracion no aplica migraciones pendientes, no lo cambies: reportalo.

5. Contratos: no cambies DTO ni interfaces de include/app/contracts/. Si lo necesitas, detente y reportalo antes: el frontend esta compilando contra ellos en otro bloque.

6. updated_at no cambia por iniciar sesion ni por lecturas; solo por ediciones.

7. Documenta en la cabecera de la migracion 010 que las columnas _utc de filas anteriores quedan en NULL y que NULL significa UTC desconocido, usar la columna local.

El resto del plan queda como lo propusiste. En la entrega indica donde quedo cada funcion compartida y el resultado de la investigacion de los archivos test_snapshot.bak.tmp.
```
