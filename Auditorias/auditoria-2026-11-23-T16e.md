# Auditoría T16e — Cierre del rediseño de pantalla completa (I7)

Ronda 15 — T16e (Gemini 4 Neon, Medium, `/caveman`) — auditado el 2026-11-23

**T16e: aceptado, con un pendiente de verificación (T16f).** Plan en `Prompts/plan-2026-11-23-T16e-neon.md` (aprobado con ajustes); dos bloqueos en `test_f7` resueltos por el orquestador; entrega en `entregas/entrega-2026-11-23-T16e.md` (00:42 a 02:56).

- **Promociones:** reparto 58:42 y Nombre con prioridad; el nombre más largo cabe entero en Estándar (275 px de columna para 257 de texto) y Táctil (314 para 287). Descuento y «Aplica a» quedan cortos (aceptado: la línea de resumen bajo la lista muestra la promoción completa).
- **Caja y Bitácora:** sin el margen exterior duplicado (quedan a 16 px del borde, como Reportes); tarjetas con relleno de 16; textos envueltos con `wxClientDC` (corrige «Motivo de diferencia...» y «Registro cronológico...» en 960x600 Táctil); `caja_historial` sin la altura fija de 98; Bitácora con destrucción segura de T6d.
- **Existencias, Reabastecimiento, Ingredientes, Barra y Reportes:** relleno de 16 y separaciones de al menos 6. Ingredientes con formulario desplazable solo en 960x600 Táctil y Guardar en un pie fijo.
- **Pruebas:**
  - `test_t16_fullscreen_layout.py`: las 17 pantallas en «terminada» salvo Actualizaciones (excepción aprobada), `KNOWN_EXCEPTIONS` vacío, comprobación de listas en 1200x720 para 12 pantallas.
  - `test_f7_ingredients_panel.py` (autorizado): el solapamiento se mide con el rectángulo visible y se agregó una comprobación de que todo el formulario es alcanzable al desplazarlo; las demás aserciones no cambian. Revisé el diff.
- **Verificación del orquestador sobre `main` con T17b integrado (125 %, con `gui-test.lock`):** Release y Debug sin advertencias; CTest 23/23 en ambos; 21 pruebas gráficas en verde (`test_t5_fullscreen` falló una vez porque la ventana de Claude quedó encima del cobro, el fallo intermitente conocido, y pasó al repetirla).
- **Capturas revisadas:** `touch_06_promociones.png` (Nombre entero), `touch_11_bitacora.png` (márgenes correctos).
- **Pendiente (T16f):** `touch_10_caja_historial.png` sale vacía. `tests/fullscreen_layout_lib.py` siembra el corte cerrado «ayer» y filtra con «Esta semana»; el 2026-11-23 es lunes, así que el corte del domingo queda fuera y el historial se midió sin datos. La corrección de la barra vertical del historial no está verificada con datos. Además la prueba depende del día de la semana.
