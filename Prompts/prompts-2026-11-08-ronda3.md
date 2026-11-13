# Prompts del 2026-11-08 — ronda 3

Línea base: `main` actual. Reglas en `docs/coordinacion-agentes.md` (protocolo, incluida la regla de dos pasadas).

- **F2** (AntiGravity, Bitácora): el prompt está en `Prompts/prompts-2026-11-08-ronda2.md`, sección F2. Envíalo tal cual.
- **B3a** (Claude Code Terminal): detalles del panel de roles que Gemini no cerró en dos pasadas (F1 y F1b). Corre en paralelo con F2 sin choque: F2 crea archivos nuevos y no toca `backoffice_roles_panel.*`, `test_f1_roles_panel.py` ni el panel de roles.

Recuerda `/caveman` al inicio de cada sesión nueva de agente.

---

## B3a — Claude Code Terminal (worktree `D:\Cafeteria_ITCH_backend`, rama `backend/b3`)

```text
Tarea de frontend escalada por la regla de dos pasadas (docs/coordinacion-agentes.md, protocolo punto 6). Trabaja SOLO en D:\Cafeteria_ITCH_backend, rama backend/b3. Solo puedes editar src/frontend/backoffice_roles_panel.cpp, src/frontend/backoffice_roles_panel.hpp y tests/test_f1_roles_panel.py. No toques tests/capture_visual_qa.py, otros archivos de src/frontend/, contratos ni servicios. No hagas commits. Compila en build_be_debug y build_be_release (incluyen app_desktop) y corre CTest. NO ejecutes pruebas GUI (tests/*.py): el otro agente puede estar usando el escritorio; el orquestador las corre al integrar.

Presenta primero un plan corto para revision.

Detalles pendientes del panel BackOffice > Roles y permisos (ver capturas qa_visual/screenshots/09_backoffice_roles_panel.png y 10_backoffice_roles_panel_touch.png):

1. Encabezado de la columna Descripcion recortado (se lee Descripci...). La prueba dice exigir ancho del encabezado mas 8 px pero lo mide con la fuente de la lista. Calcula el ancho minimo de cada columna con la fuente real del encabezado (LVM_GETHEADER y WM_GETFONT, o su equivalente en wxWidgets) y con la del contenido; usa el espacio sobrante a la derecha de la lista (unos 25 px a 1216x759) para Descripcion. El contenido de Descripcion puede seguir con puntos suspensivos; el encabezado no.

2. Perfil Touch:
   - filas de la lista de roles de al menos 40 px de alto (pos_panel.cpp lo logra con una wxImageList; si usas esa tecnica, descuenta el ancho de la imagen de la columna 0 como hace pos_panel);
   - texto de Nombre y Descripcion centrado verticalmente en sus campos de 48 px, con la misma tecnica de contenedor y espaciadores que usa la busqueda de pos_panel.cpp (lineas 43 a 51). Debe seguir funcionando la edicion para roles creados por el Dueño.

3. Huecos de tests/test_f1_roles_panel.py (endurecer, nunca relajar):
   - medir el encabezado con la fuente del encabezado;
   - la alineacion de la casilla con la primera linea debe comprobarse en todas las casillas, no solo en las multilinea, y debe FALLAR si no se detecta el glifo, en lugar de omitirse. Detecta el glifo comparando contra el color de fondo real del control, no contra blanco. Ajusta la tolerancia a 3 px como dice el mensaje (hoy compara con 4);
   - persistencia del Cajero: tras guardar, cerrar la app, reabrirla con la misma base, volver a seleccionar Cajero y comprobar que la casilla sigue marcada (ademas de la lectura directa de SQLite que ya existe);
   - nuevas aserciones en Touch: alto de fila de la lista de roles al menos 40 px (LVM_GETITEMRECT) y texto de Nombre y Descripcion centrado verticalmente en su contenedor (tolerancia 3 px).

Verificacion, con PATH = D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll al inicio:
   cmake --build build_be_debug --parallel 2
   ctest --test-dir build_be_debug --output-on-failure
   cmake --build build_be_release --parallel 2
   ctest --test-dir build_be_release --output-on-failure
   python -m py_compile tests/test_f1_roles_panel.py
Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual.

Entrega: archivos, comandos con codigo de salida y limites. Di explicitamente que la prueba GUI no se ejecuto y que partes de ella no pudiste validar sin correrla.
```
