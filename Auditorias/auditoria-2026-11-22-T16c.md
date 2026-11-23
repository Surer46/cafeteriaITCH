# Auditoría T16c — Venta, Productos y recetas, Promociones y Combos

Ronda 14 — T16c (Gemini 4 Neon, High, `/caveman`) — auditado el 2026-11-22

**T16c: aceptado, con una observación para el bloque corto final.** Plan en `Prompts/plan-2026-11-21-T16c-neon.md` (aprobado con 8 ajustes); entrega en `entregas/entrega-2026-11-22-T16c.md` (20:16 a 22:38).

- **Cambios:** `pos_panel.cpp`, `inventory_products_panel.*`, `inventory_promotions_panel.*`.
  - Relleno interior de 16 lógicos en todas las tarjetas (ajuste 1, lección de T16b3).
  - Reparto 52:48 (Productos) y 55:45 (Promociones y Combos) en pantalla completa; columnas medidas con los datos cargados.
  - Categoría con un desplegable y «Nueva categoría...»; vacío no guarda; la opción nunca se guarda como nombre.
  - Costo y margen encima de la receta, visibles sin desplazarse en 1200x720 y pantalla completa; texto envuelto con `wxClientDC` (corrige «Falta registrar una compra...»).
  - Promociones: filas en línea, vigencia en 4 columnas, días en una fila (48 lógicos de alto en Táctil). Mensajes ocultos cuando están vacíos, sin barras al mostrarse.
  - Venta: Producto con tope max(280, 65 % del resto) antes de Notas; el nombre más largo cabe entero en pantalla completa.
- **Pruebas:** `test_t16_fullscreen_layout.py` pasa las 4 pantallas a «terminada», quita sus 12 excepciones y agrega dos comprobaciones (listas en 1200x720 y costo visible). t11, t12 y t13 solo cambian cómo localizan la categoría y la cantidad; revisé el diff: ninguna aserción se relaja.
- **Verificación del orquestador (125 %, con `gui-test.lock`):** Release y Debug sin advertencias; CTest 23/23 en ambos; en verde `test_t16_fullscreen_layout`, `test_t11`, `test_t12`, `test_t13`, `test_i2`, `test_t4`, `test_t2`, `test_f1` y `test_i1`.
- **Capturas revisadas:** `touch_04_productos_y_recetas.png`, `touch_06_promociones.png`, `standard_06_promociones.png`, `touch_07_combos.png`, `touch_01_venta.png`: márgenes correctos y sin barras.
- **Observación (no la detecta la prueba):** en Promociones Táctil a pantalla completa, Nombre y «Aplica a» siguen en unos 95 px («2x1 en ...», «Galleta ...»), igual que antes; el plan prometía unos 188 px. Descuento recibe su ancho completo (unos 270 px), así que ninguna columna tiene 40 lógicos de sobra y el criterio CLIP+SOBRA no salta. El cálculo del plan subestimó las columnas compactas. Va al bloque corto final (T16e), junto con las 4 excepciones de Caja y Bitácora.
