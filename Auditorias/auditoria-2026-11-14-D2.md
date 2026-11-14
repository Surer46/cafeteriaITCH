# Auditoría D2 — pruebas GUI al 100 y al 125 %

Ronda 7 — D2 (AntiGravity, Gemini 4 Hydrogen high) — auditado el 2026-11-14

**D2:** aceptado. Todas las pruebas GUI son válidas con Windows al 100 % (96 DPI) y al 125 % (120 DPI); cualquier otro escalado se rechaza con un mensaje claro. Descubrió un defecto real del programa a 125 % (abajo), que no bloquea la aceptación.

- Alcance: `tests/gui_dpi.py` (nuevo, módulo común) y las once pruebas y guiones de `tests/` permitidos; capturas y `qa_visual/touch_audit.txt` regenerados. No tocó `src/`, `CMakeLists.txt` ni documentos. 27 minutos.
- Módulo común: proceso consciente de DPI por monitor; DPI de la ventana; solo 96 y 120; `min_px` (redondeo hacia arriba para mínimos), `max_px` (hacia abajo para máximos y holguras) y `to_px` (coordenadas y tamaños de ventana); tamaño de ventana en unidades lógicas; selección y medida de filas con `LVM_GETITEMRECT`; medida de texto con las fuentes Roboto del proyecto, leyendo el `LOGFONT` real del control.
- **Fallo de `test_f1_roles_panel` resuelto:** era de medida, no un recorte. La prueba medía con la fuente del sistema porque la fuente obtenida con `WM_GETFONT` no se puede seleccionar desde otro proceso; con Roboto el texto de `pos.sell` mide 206 px y cabe en 219. Es el mismo hallazgo de F7, ahora en el módulo común para todas las pruebas.
- Comprobación nueva (anexo de D1): en `test_t2_touch_targets` el texto de cada botón recorrido, más su icono y margen, cabe en el botón.
- Revisión del orquestador:
  - Aserciones antes y después, por archivo: `test_f1` 150→142, `test_t1` 45→40, `test_t2` 18→16 (más la nueva), `test_f7` 52→51; el resto igual. Todas las que faltan son de funciones auxiliares movidas a `gui_dpi.py` (10 aserciones allí). Ninguna comprobación de resultado se quitó.
  - Cada número fijo pasó a una variable escalada que a 100 % vale exactamente lo mismo. A 125 %, los mínimos exigen igual o más; holguras y tolerancias crecen como máximo un píxel (2→2, 4→5, 8→10); la tolerancia de centrado pasa de 3 a 3.75.
- Verificación del orquestador (build y CTest ya verificados con D1 y B8; D2 no toca el programa):
  - **100 %:** `test_i1_integration`, `test_t1_touch_profile`, `test_t2_touch_targets`, `test_f2b_subnav`, `test_fe08_fe09_visual_integration`, `test_f2_audit_log`, `test_f7_ingredients_panel`, `test_f1_roles_panel` y `capture_visual_qa` exit 0.
  - **125 %:** todas las anteriores exit 0 salvo `test_f7_ingredients_panel`: en Touch a 960x600 lógicos, las columnas de la lista suman 578 px y el área cliente mide 575 (barra horizontal). Repetida: mismo resultado. **Defecto del programa** (reparto de columnas de Ingredientes en la ventana estrecha), no de la prueba; no se relaja. Pasa a T3.
  - `test_i2_integration` adaptada pero sin correr (pendiente del diálogo de impresión en el escritorio real).
  - Las capturas del repositorio quedan generadas a 125 %.
- Informe: literal; declaró tres ejecuciones fallidas de su prueba de Roles por errores de importación y de nombres antes de la buena, y que no pudo verificar a 125 %.
