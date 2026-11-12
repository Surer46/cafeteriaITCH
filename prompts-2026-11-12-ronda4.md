# Prompts del 2026-11-12 — ronda 4

Línea base: `main` en `9b0ec95`. Reglas en `coordinacion-agentes.md`. Los dos bloques corren en paralelo sin tocar los mismos archivos.

- **B3b** (Claude Code Terminal, rama `backend/b3b`): utilidades compartidas de columnas y campo centrado, aplicadas a Roles y Bitácora.
- **F2b** (AntiGravity, `main`): subsección activa sin resaltar cuando su nombre lleva acento.

Recuerda `/caveman` al inicio de cada sesión nueva de agente.

Excepción de esta ronda: los dos agentes ejecutan pruebas GUI. Para no chocar, cada uno comprueba con `tasklist` que no haya ningún `app_desktop.exe` antes de cada ejecución.

---

## B3b — Claude Code Terminal (worktree `D:\Cafeteria_ITCH_backend`, rama `backend/b3b`)

```text
Tarea de frontend por la regla de dos pasadas (coordinacion-agentes.md, protocolo punto 6). Trabaja SOLO en D:\Cafeteria_ITCH_backend, rama backend/b3b (ya parte de main, incluye B3a y el panel de Bitacora). No hagas commits.

Motivo: el ajuste de columnas de listas y el centrado vertical de texto en campos han fallado en tres paneles distintos. B3a los resolvio dentro de backoffice_roles_panel.cpp. Hay que sacarlos a un lugar comun para que los paneles nuevos los reutilicen.

Archivos que puedes editar, y ningun otro:
- nuevos: src/frontend/ui_helpers.hpp y src/frontend/ui_helpers.cpp
- CMakeLists.txt (solo agregar ui_helpers.cpp a las fuentes de app_desktop)
- src/frontend/backoffice_roles_panel.cpp y .hpp
- src/frontend/audit_log_panel.cpp y .hpp
- tests/test_f2_audit_log.py
- tests/test_f1_roles_panel.py solo si un cambio tuyo lo obliga; nunca relajar aserciones
No toques subnav_bar.*, main_window.*, tests/capture_visual_qa.py, contratos ni servicios. El otro agente esta editando subnav_bar.*.

Presenta primero un plan corto para revision.

Trabajo:
1. ui_helpers: mueve ahi, sin cambiar comportamiento, lo que B3a dejo en backoffice_roles_panel.cpp:
   - medicion de texto con la fuente real del encabezado, alto del encabezado y deteccion de barra vertical;
   - una funcion de reparto de anchos de columnas reutilizable: recibe la lista, el contenido maximo por columna y cual columna es la flexible; garantiza encabezado completo en todas, sin barra horizontal, y la columna flexible toma todo el espacio restante;
   - la fabrica de campo de texto (make_text_field): hoy solo usa contenedor en Touch. Hazla usar el contenedor con espaciadores tambien en Estandar cuando el campo tenga alto fijo mayor que el natural, para que el texto quede centrado en ambos perfiles.
2. backoffice_roles_panel: usa las utilidades. test_f1_roles_panel.py debe seguir pasando sin tocarlo.
3. audit_log_panel (ver qa_visual/screenshots/11_audit_log_panel.png):
   - la columna Detalle mide unos 58 px y deja unos 500 px libres a su derecha: debe ser la columna flexible y tomar todo el espacio restante; recalcular al cambiar el tamano de la lista y al aparecer la barra vertical;
   - los campos Usuario, Fecha desde y Fecha hasta (34 px en Estandar, 48 en Touch) muestran el texto pegado arriba: centrarlo verticalmente con la fabrica comun. Los textos de ayuda (Username o ID, YYYY-MM-DD) y los filtros deben seguir funcionando.
4. tests/test_f2_audit_log.py, endurecer sin relajar nada:
   - encabezado de cada columna medido con la fuente del encabezado (LVM_GETHEADER y WM_GETFONT), ancho de columna al menos encabezado mas 8 px;
   - suma de columnas menor o igual al area cliente y holgura de 8 px o menos; sin WS_HSCROLL;
   - texto de Usuario, Fecha desde y Fecha hasta centrado verticalmente en su contenedor, tolerancia 3 px, en Estandar y Touch; falla si no se detecta la tinta.
   Puedes copiar las funciones de medicion de test_f1_roles_panel.py.

Verificacion, con PATH = D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll al inicio:
   cmake --build build_be_debug --parallel 2
   ctest --test-dir build_be_debug --output-on-failure
   cmake --build build_be_release --parallel 2
   ctest --test-dir build_be_release --output-on-failure
   python -u tests/test_f1_roles_panel.py
   python -u tests/test_f2_audit_log.py
Pruebas GUI: solo esas dos. Antes de cada una comprueba con tasklist que no haya ningun app_desktop.exe corriendo; si lo hay, espera 30 s y vuelve a comprobar. build_release y build_debug del worktree son enlaces a build_be_release y build_be_debug.
Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual.

Entrega corta: archivos, comandos con codigo de salida, anchos medidos de las columnas de Bitacora en Estandar y Touch, y limites de lo verificado.
```

---

## F2b — AntiGravity (`D:\Cafetería_ITCH`, rama `main`)

```text
Bloque F2b, pequeno. No compiles ni corras pruebas antes de editar.

Defecto: en la barra horizontal de subsecciones, la subseccion activa no se resalta cuando su nombre lleva acento. En qa_visual/screenshots/11_audit_log_panel.png estas en Caja y Reportes > Bitacora y ningun boton aparece resaltado. Roles y permisos (sin acento) si se resalta.

Causa probable, confirmala leyendo el codigo: en src/frontend/subnav_bar.cpp, on_button_clicked compara btn->GetLabel().ToStdString() con el nombre en UTF-8. ToStdString usa la codificacion local de Windows, asi que nombres con acento nunca coinciden. Afecta a Bitacora, Configuracion y Catalogo visual.

Archivos que puedes editar, y ningun otro:
- src/frontend/subnav_bar.cpp y src/frontend/subnav_bar.hpp
- nuevo: tests/test_f2b_subnav.py
No toques audit_log_panel.*, backoffice_roles_panel.*, tests/test_f2_audit_log.py, tests/test_f1_roles_panel.py ni tests/capture_visual_qa.py: el otro agente esta trabajando ahi. No hagas commits.

Trabajo:
1. Corrige la comparacion sin depender de la codificacion: guarda el nombre UTF-8 junto a cada boton (o compara por indice) y usa una sola funcion para pintar activo e inactivo, llamada desde rebuild_buttons y on_button_clicked. No cambies colores, tamanos ni nombres de subsecciones.
2. Prueba GUI nueva tests/test_f2b_subnav.py con el ejecutable real (Release y Debug), base en directorio temporal aislado, alta del Dueno como en las otras pruebas:
   - recorre las cuatro secciones y haz clic en cada subseccion visible para el Dueno;
   - tras cada clic mide el color de fondo de cada boton de la barra leyendo pixeles (no a ojo): exactamente un boton con el fondo de activo y es el que se pulso; los demas con el fondo de inactivo;
   - debe cubrir de forma explicita Bitacora, Configuracion y Catalogo visual;
   - al cambiar de seccion, la primera subseccion queda resaltada;
   - repetir en perfil Estandar y Touch;
   - Servidor de respaldos no debe aparecer para el Dueno.
   La prueba debe fallar con el codigo actual y pasar con el corregido: ejecutala una vez antes del arreglo para comprobarlo y reporta ese fallo.

Verificacion:
   cmake --build build_release --parallel 2
   cmake --build build_debug --parallel 2
   ctest --test-dir build_release --output-on-failure
   ctest --test-dir build_debug --output-on-failure
   python -u tests/test_f2b_subnav.py
Pruebas GUI: solo esa. Antes de cada ejecucion comprueba con tasklist que no haya ningun app_desktop.exe corriendo; si lo hay, espera 30 s y vuelve a comprobar.
Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual.

Entrega corta: archivos, comandos con codigo de salida, el fallo de la prueba antes del arreglo y su resultado despues, y limites de lo verificado.
```

---

## B4 — Claude Code Terminal (enviar cuando B3b esté aceptado; rama `backend/b4`)

Origen: `auditoria-general-2026-11-12.md`, apartado B. Aprobado por el usuario el 2026-11-12. El cliente no ha definido el tipo de versión móvil; estos ajustes sirven para cualquiera.

```text
Bloque B4 de backend. Trabaja SOLO en D:\Cafeteria_ITCH_backend, rama backend/b4. No hagas commits. No compiles ni corras pruebas antes de editar. Lee auditoria-general-2026-11-12.md, apartado B.

Objetivo: cuatro ajustes de datos que hoy cuestan poco y con ventas acumuladas costarian mucho. No construyas sincronizacion, servidor ni nada de red.

Archivos permitidos: src/domain/, src/storage/, src/services/, include/app/, migrations/ (solo archivos nuevos, a partir de 009), tests/test_*.cpp y CMakeLists.txt. No toques src/frontend/, tests/*.py ni migraciones ya aplicadas (001 a 008).

Presenta primero un plan para revision. El plan debe resolver las dudas marcadas como DECIDIR antes de escribir codigo.

1. Identificadores. Hoy cada servicio tiene su propia copia de generate_id (prefijo, milisegundos, contador que reinicia al arrancar): dos equipos pueden generar el mismo. Deja una sola funcion compartida que conserve el prefijo y use 128 bits aleatorios de una fuente adecuada (UUID v4 o equivalente). Los identificadores ya guardados no se tocan. DECIDIR: fuente de aleatoriedad en MinGW y fuera de Windows, sin meter dependencias de plataforma en el dominio.

2. Fechas. Hoy cada servicio tiene su propia copia de current_timestamp y guarda hora local sin zona. La columna local existente y su formato YYYY-MM-DD HH:MM:SS NO cambian: la usan la interfaz, los tickets y el reporte diario. Agrega el instante en UTC en una columna nueva en las tablas de hechos (como minimo sales, payment_records, cash_movements, stock_movements, purchase_entries, shifts y audit_events) y escribela en cada alta. Una sola funcion compartida produce ambos valores del mismo instante. DECIDIR: que hacer con las filas anteriores (convertirlas con la zona del equipo al migrar, o dejarlas en NULL y documentarlo); justifica la opcion.

3. Ultima modificacion. Agrega updated_at a products, ingredients, recipes y a las demas tablas de catalogo que se editan (revisa cuales), y actualizalo en cada cambio desde los servicios. Filas existentes: igual a created_at.

4. Folio. Hoy es V-10001, secuencia unica local calculada con COUNT: dos cajas generarian el mismo. Los folios nuevos deben incluir el codigo de la caja del turno. Los folios ya emitidos no cambian y deben seguir reimprimiendose. DECIDIR: formato exacto; no puede romper la busqueda por folio ni el ancho del ticket de 55 mm. Busca en tests/*.py si alguna prueba GUI depende del formato V-numero y reportalo en la entrega, sin editar esas pruebas.

Reglas: migraciones nuevas versionadas e idempotentes; dinero y cantidades sin float ni double; no relajar pruebas existentes.

Pruebas CTest nuevas, sin depender solo de assert:
- 100000 identificadores generados sin repeticion, y formato con prefijo;
- base creada con migraciones 001 a 008 y con datos, migrada a la nueva version: datos intactos, columnas nuevas presentes, y volver a migrar no cambia nada;
- una venta nueva guarda hora local y UTC del mismo instante;
- editar un producto cambia updated_at y no created_at;
- folios de dos cajas distintas no chocan; un folio antiguo V-numero sigue recuperable por get_ticket.

Verificacion, con PATH = D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll al inicio:
   cmake --build build_be_debug --parallel 2
   ctest --test-dir build_be_debug --output-on-failure
   cmake --build build_be_release --parallel 2
   ctest --test-dir build_be_release --output-on-failure
No ejecutes pruebas GUI. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual.

Entrega: archivos, comandos con codigo de salida, decisiones tomadas, pruebas GUI que podrian verse afectadas por el folio, y limites de lo verificado.
```

---

## F5 — AntiGravity (enviar cuando F2b esté aceptado y B3b integrado en `main`)

Origen: petición del usuario del 2026-11-12. Los permisos de un rol se activan con interruptores en lugar de casillas, más grandes en Touch, para crear un rol rápido.

```text
Bloque F5. No compiles ni corras pruebas antes de editar. No hagas commits.

Cambio pedido por el usuario: en BackOffice > Roles y permisos, cada permiso se activa con un interruptor deslizante (pastilla con perilla, como los ajustes de un telefono) en lugar de una casilla. En perfil Touch debe ser mas grande y comodo para recorrer la lista y guardar.

Archivos que puedes editar, y ningun otro:
- nuevos: src/frontend/toggle_switch.hpp y src/frontend/toggle_switch.cpp
- CMakeLists.txt (solo agregar toggle_switch.cpp y .hpp a las fuentes de app_desktop)
- src/frontend/backoffice_roles_panel.cpp y .hpp
- tests/test_f1_roles_panel.py
- tests/capture_visual_qa.py solo si las capturas 09 y 10 dejan de generarse
No toques ui_helpers.*, otros paneles, contratos ni servicios. Usa ui_helpers tal como esta.

1. Control ToggleSwitch (reutilizable, dibujado por la aplicacion sobre wxControl):
   - tamano: 44 x 24 en Estandar y 60 x 32 en Touch, pasados por FromDIP;
   - activado: pastilla con Theme::ColorBrand y perilla a la derecha; desactivado: pastilla gris y perilla a la izquierda. El estado no puede depender solo del color: la posicion de la perilla lo indica, y ademas dibuja una marca dentro de la perilla cuando esta activado;
   - deshabilitado: colores atenuados y no responde;
   - teclado: recibe foco con Tab, muestra el foco y cambia con Espacio;
   - emite un evento al cambiar y expone GetValue y SetValue;
   - en Windows responde a BM_GETCHECK y BM_SETCHECK con su estado, para que las pruebas y las herramientas de accesibilidad lo lean;
   - sin animacion ni temporizadores (PC de pocos recursos). Dibujo con doble bufer, sin parpadeo.

2. Panel de roles:
   - cada permiso es una fila: texto a la izquierda (puede ocupar varias lineas) e interruptor a la derecha, centrado verticalmente respecto a la fila;
   - un clic o toque en cualquier punto de la fila cambia el interruptor;
   - alto minimo de fila: 36 en Estandar, 56 en Touch; linea separadora discreta entre filas;
   - los encabezados de modulo se conservan;
   - rol Dueno: interruptores deshabilitados. Rol Cajero: editables. Roles propios: editables. Sin cambios en nombres, textos, permisos ni reglas;
   - el boton Guardar sigue visible sin desplazarse; a su lado muestra cuantos permisos estan activados (por ejemplo: 6 de 20 permisos);
   - role-dev y backup.dev_config siguen sin aparecer.

3. tests/test_f1_roles_panel.py: sustituye las comprobaciones de casillas por las equivalentes de interruptor. Nada de lo que hoy se comprueba puede perderse (persistencia en SQLite y tras reabrir, Cajero editable, Dueno en solo lectura, columnas, filas Touch, campos centrados, geometria a 960x600). Nuevas aserciones, midiendo y no a ojo:
   - hay un interruptor por permiso visible (20 para el Dueno);
   - tamano del interruptor al menos 44 x 24 en Estandar y 60 x 32 en Touch (GetWindowRect);
   - alto de fila al menos 36 en Estandar y 56 en Touch;
   - centro vertical del interruptor a 3 px o menos del centro de su fila;
   - borde derecho de todos los interruptores alineado (diferencia de 2 px o menos) y ninguno recortado por el area visible;
   - posicion de la perilla leida por pixeles: a la derecha si BM_GETCHECK dice activado, a la izquierda si no; falla si no se detecta la perilla;
   - clic en el interruptor cambia el estado; clic sobre el texto de la fila tambien; Espacio con el foco tambien;
   - con el rol Dueno, un clic no cambia el estado;
   - el contador junto a Guardar coincide con el numero de interruptores activados.

Verificacion:
   cmake --build build_release --parallel 2
   cmake --build build_debug --parallel 2
   ctest --test-dir build_release --output-on-failure
   ctest --test-dir build_debug --output-on-failure
   python -u tests/test_f1_roles_panel.py
   python -u tests/capture_visual_qa.py
Antes de cada prueba GUI comprueba con tasklist que no haya ningun app_desktop.exe corriendo. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual.

Entrega corta: archivos, comandos con codigo de salida, medidas del interruptor y de las filas en ambos perfiles, capturas 09 y 10, y limites de lo verificado.
```
