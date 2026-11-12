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
