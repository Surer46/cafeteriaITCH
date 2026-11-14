# Auditoría D1 — programa al 100 y al 125 %

Ronda 7 — D1 (AntiGravity, Gemini 4 Hydrogen high) — auditado el 2026-11-14

**D1:** aceptado, con el anexo del botón «Abrir Turno de Caja». Commit `1867d7a`.

- Alcance: solo `src/frontend/` (20 archivos, +617/−519 líneas): `theme.*`, `ui_helpers.*`, encabezado, barra lateral, subsecciones, ventana principal, interruptor y todos los paneles y diálogos. No tocó `main_app.cpp`, pruebas ni nada fuera de `src/frontend/`. `qa_visual/touch_audit.txt` cambia solo por haberse regenerado.
- Mecanismo: los métodos de medidas de `Theme` reciben la ventana y devuelven `FromDIP`; se quitaron las versiones sin ventana. `style_button_*`, `setup_button_icon`, `make_text_field`, `distribute_list_columns` y `apply_touch_row_height` reciben unidades lógicas y convierten por dentro. Nuevo `Theme::ensure_button_min_size`: un ancho fijo nunca queda por debajo de lo que pide el contenido. El dibujo propio (tarjetas, insignia de estado, interruptor) escala radios y bordes.
- Revisión del orquestador: ningún factor propio (sin `1.25`, ni DPI leído a mano); ninguna llamada a `Theme` o `ui_helpers` recibe ya un valor con `FromDIP` (las de T1 y T2 se pasaron a unidades lógicas); los tamaños de iconos se pasan en unidades lógicas, como pide `wxBitmapBundle::FromSVG`; los `wxSize` con números que quedan usan variables ya escaladas.
- Anexo: causa confirmada por Gemini. «Abrir Turno de Caja» medía 200 de ancho fijo y su texto en negrita más el icono pedía más; ahora mide 226 lógicos en Estándar y 238 en Touch.
- Medidas a 125 % (lógicas): barra lateral 56 (antes 44.8); botón de perfil 48 (antes 38.4); subsecciones 48; filas de Bitácora 43.2 (antes 34.4); interruptor 60x32.
- Verificación del orquestador, integrado con B8:
  - **125 %:** build sin advertencias; CTest 11/11 en Release y Debug (PackageTest omitida en Debug); `test_i1_integration`, `test_t1_touch_profile` y `test_t2_touch_targets` exit 0. Las 35 excepciones de T2 se siguen reproduciendo.
  - **100 %** (el usuario cambió el escalado): `test_i1_integration`, `test_t1_touch_profile`, `test_t2_touch_targets`, `test_f2b_subnav`, `test_fe08_fe09_visual_integration`, `test_f2_audit_log` y `test_f7_ingredients_panel` exit 0. `test_f1_roles_panel` exit 1 con exactamente el mismo fallo de antes de D1 (descripción de `pos.sell`, 219 px frente a 250, caso 3.2): D1 no lo cambia; lo investiga D2. `test_i2_integration` sin correr (pendiente del diálogo de impresión).
  - Las pruebas anteriores a T1 no se corrieron a 125 % después de D1: siguen sin ser válidas a ese escalado hasta D2.
- Observaciones (no bloquean): la medida de T1 en los diálogos a 125 % da 22.4 lógicos por campo en Estándar (23 a 100 %), por redondeo del control nativo; el recorrido visual de Gemini se hizo con un guion propio fuera del repositorio.
- Informe: literal, con medidas y el conteo de compilaciones y pruebas; 27 minutos.
