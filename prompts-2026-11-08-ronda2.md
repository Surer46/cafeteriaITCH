# Prompts del 2026-11-08 — ronda 2 (con caveman)

Línea base: commit `eeed1aa` en `main`. Reglas en `coordinacion-agentes.md`.

- **B2** (Claude Code Terminal) y **F1b** (AntiGravity) corren en paralelo: no comparten archivos.
- **F2** (AntiGravity) se envía solo después de auditar F1b, porque usa la misma copia de trabajo.

**Caveman:** en cada agente, escribe `/caveman` una vez al inicio de la sesión, antes de pegar el prompt. Si la sesión ya lo tiene activo, no hace falta repetirlo. Compruébalo con `/caveman status`.

Copiar solo el bloque de texto de cada prompt.

---

## B2 — Claude Code Terminal (worktree `D:\Cafeteria_ITCH_backend`, rama `backend/b2`)

```text
Agente de backend de Cafeteria ITCH II. Trabaja SOLO en D:\Cafeteria_ITCH_backend, rama backend/b2 (ya creada desde main eeed1aa). Mismas reglas que B1: tus archivos son contratos, servicios, almacenamiento, migraciones y tests/test_*.cpp; no toques src/frontend/, tests/*.py ni documentos de estado; no hagas commits. Compila en build_be_debug y build_be_release (ya configurados). No ejecutes pruebas GUI.

Presenta primero un plan corto para revision.

Tarea B2: servicio de reportes real (sustituye report_service = nullptr en create_sqlite_registry).

1. SqliteReportService implementa IReportService::get_daily_sales_summary(date_iso).
   - Permiso reports.view comprobado dentro del servicio.
   - date_iso debe ser YYYY-MM-DD valido (incluye 29 de febrero solo en bisiesto); si no, InvalidInput.
   - sales.created_at y payment_records.created_at se guardan en hora local con formato "YYYY-MM-DD HH:MM:SS". Filtra por rango [fecha 00:00:00, dia siguiente 00:00:00), no con LIKE.
   - total_sales_count = ventas del dia. total_amount = suma de sales.total_cents. cash_amount, card_amount y transfer_amount = suma de payment_records.amount_cents por metodo (EFECTIVO, TARJETA, TRANSFERENCIA).
   - Todo en centavos enteros con comprobacion de desbordamiento; nada de float ni double.
   - Si la suma de pagos del dia no coincide con total_amount, no lo ocultes: devuelve el reporte y expone la discrepancia (agrega un campo al DTO, por ejemplo payments_mismatch_cents, y documentalo).
2. Registra el servicio en create_sqlite_registry. MockReportService queda solo en create_mock_registry.
3. CTest (archivo nuevo tests/test_reports.cpp; puedes registrarlo en tests/CMakeLists.txt). TEST_CHECK, no assert. Casos minimos:
   - dia con ventas en los tres metodos: conteo y sumas exactas al centavo;
   - venta a las 23:59:59 cuenta en su dia y una a las 00:00:00 del dia siguiente no;
   - dia sin ventas: todo en cero;
   - fecha invalida (2026-02-30, 2026-13-01, texto) da InvalidInput;
   - sin sesion y cajero sin reports.view dan Forbidden; Dueño si puede;
   - importes grandes cerca del limite: error de desbordamiento, no resultado truncado;
   - discrepancia entre pagos y total detectada.
   Crea las ventas con los servicios reales (turno, carrito, confirm_sale) cuando sea posible; si insertas filas directas para fijar created_at, explica por que en la entrega.

Verificacion con PATH = D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll al inicio:
   cmake --build build_be_debug --parallel 2
   ctest --test-dir build_be_debug --output-on-failure
   cmake --build build_be_release --parallel 2
   ctest --test-dir build_be_release --output-on-failure
Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual.

Entrega: archivos, contrato final, comandos con codigo de salida, conteo de pruebas y limites. Escribe la entrega completa aunque uses caveman: cada archivo, comando y limite debe aparecer.
```

---

## F1b — AntiGravity (`D:\Cafetería_ITCH`, rama `main`)

```text
Agente de frontend de Cafeteria ITCH II. Mismas reglas que F1 (lee coordinacion-agentes.md): solo src/frontend/, tests/*.py y qa_visual/; no toques contratos, servicios ni documentos de estado; no hagas commits. No recompiles ni corras pruebas antes de editar: el codigo actual ya esta verificado.

Tarea F1b: correcciones del panel Roles y permisos (src/frontend/backoffice_roles_panel.cpp) segun la auditoria.

1. Decision del usuario: el Dueño puede editar los PERMISOS del rol Cajero (role-cashier), pero no su nombre ni descripcion, y no puede eliminarlo.
   - Rol Cajero: casillas y Guardar habilitados; Nombre, Descripcion y Eliminar deshabilitados. Aviso: Rol de sistema: solo se pueden cambiar sus permisos.
   - Rol Dueño (role-owner): todo en solo lectura, como ahora.
   - Roles creados por el Dueño: todo editable, como ahora.
   - Sin roles.manage: todo en solo lectura, como ahora.
   - Al guardar el Cajero, envia update_role con el nombre y descripcion actuales sin cambios. Si el servicio rechaza, muestra su mensaje tal cual.
2. Lista Roles Registrados: hoy las columnas tienen anchos fijos a ojo (lineas 146 a 149) y se recortan (Descrip..., Si..., Pe) con barra horizontal. Calcula cada ancho con GetTextExtent del encabezado y del texto mas largo de esa columna, mas el padding del listview. Recalcula al cargar datos, al cambiar de perfil y al redimensionar. Si no cabe, ensancha la tarjeta izquierda o recorta solo la columna Descripcion con puntos suspensivos; Nombre, Sistema y Permisos siempre completos. Sin barra horizontal a 1216x759 en ambos perfiles.
3. Matriz de permisos: las etiquetas se envuelven a unos 270 px aunque sobra espacio, y la casilla queda alineada con la segunda linea. Usa todo el ancho disponible del area desplazable, recalcula al redimensionar, y alinea la casilla con la primera linea de su texto.

4. Amplia tests/test_f1_roles_panel.py, con aserciones estrictas (prohibido relajar umbrales):
   - cada columna de la lista: LVM_GETCOLUMNWIDTH mayor o igual al ancho medido del encabezado y del texto mas largo de la columna mas 8 px (excepto Descripcion si se eligio puntos suspensivos); sin estilo WS_HSCROLL visible en la lista a 1216x759, ambos perfiles;
   - cada casilla de permiso: texto completo visible (ancho de control mayor o igual al texto medido, o etiqueta envuelta cuyo alto cubre todas sus lineas) y la casilla alineada con la primera linea (tolerancia 3 px);
   - Cajero: marcar un permiso nuevo, guardar, cerrar, reabrir y comprobar que persiste; Nombre, Descripcion y Eliminar deshabilitados;
   - Dueño: todo deshabilitado.
5. Regenera 09_backoffice_roles_panel.png con el Cajero seleccionado y agrega 10_backoffice_roles_panel_touch.png en perfil Touch. Abre ambas antes de describirlas.

Verificacion, con PATH = D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll al inicio:
   cmake --build build_release --parallel 2
   ctest --test-dir build_release --output-on-failure
   cmake --build build_debug --parallel 2
   ctest --test-dir build_debug --output-on-failure
   python -u tests/test_f1_roles_panel.py
   python -u tests/test_fe08_fe09_visual_integration.py
   python -u tests/capture_visual_qa.py
Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual.

Entrega: archivos, comandos con codigo de salida, que se ve en 09 y 10, y limites. Escribe la entrega completa aunque uses caveman. No afirmes nada de una captura que no hayas abierto.
```

---

## F2 — AntiGravity (enviar solo después de auditar F1b)

```text
Agente de frontend de Cafeteria ITCH II. Mismas reglas que F1. No recompiles ni corras pruebas antes de editar.

Tarea F2: panel Caja y Reportes > Bitacora, solo interfaz sobre IAccountService::list_audit_events(AuditFilterDto), que ya exige audit.view.

1. Archivos nuevos src/frontend/audit_log_panel.hpp y .cpp; conectalo en MainWindow::update_content_view para la subseccion "Bitácora". En CMakeLists.txt solo agrega tus dos archivos.
2. Lista con columnas Fecha y hora, Usuario (username), Evento y Detalle. Anchos calculados con GetTextExtent como en F1b; Detalle puede usar puntos suspensivos y mostrar el texto completo al seleccionar la fila (en un campo de solo lectura debajo).
3. Filtros: tipo de evento (lista con los tipos presentes en la pagina actual mas Todos), usuario, fecha desde y hasta (YYYY-MM-DD). Boton Buscar. Paginacion con limit 100 y offset: botones Anterior y Siguiente, y texto Pagina N.
4. Sin audit.view: el servicio devuelve error; muestra su mensaje tal cual y deja la lista vacia. No decidas el permiso en la interfaz.
5. Theme por perfil Estandar y Touch; en Touch, controles de al menos 48 px de alto.
6. Prueba GUI nueva tests/test_f2_audit_log.py con base temporal aislada y ejecutable real, Release y Debug:
   - tras el alta del Dueño y un inicio de sesion aparecen eventos; el filtro por tipo reduce la lista;
   - paginacion: con mas de 100 eventos (genera cambios de perfil o inicios de sesion repetidos), Siguiente muestra otros eventos;
   - un Cajero sin audit.view ve el mensaje del servicio y la lista vacia;
   - geometria estricta como en F1b, en 1216x759 y 960x600, ambos perfiles.
7. Agrega la captura 11_audit_log_panel.png y abrela antes de describirla.

Verificacion: los mismos comandos que F1b mas python -u tests/test_f2_audit_log.py. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta.

Entrega completa aunque uses caveman: archivos, comandos con codigo de salida, que se ve en 11 y limites.
```
