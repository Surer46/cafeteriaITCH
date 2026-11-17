# Auditoría T10b — cierre de T10 (Reabastecimiento y Barra)

Ronda 9 — T10b (Gemini/AntiGravity, en `main`) — auditado el 2026-11-16 al 125 % (120 DPI)

**T10 con T10b: aceptados al 125 %.** Integrados por el orquestador en un solo commit. Al 100 % se repite la batería la próxima vez que el usuario cambie el escalado.

- **Causa de la regresión de `test_t1`:** carrera en `MainWindow::apply_profile_refresh`.
  - La barra lateral tomaba la altura táctil antes de que `update_content_view` terminara de reconstruir la vista, y la prueba (que espera esa altura) leía la vista vacía.
  - **Arreglo:** `Freeze`/`Thaw`, reconstruir primero la subnavegación y la vista y después la barra lateral, y `Refresh` + `Update`. La lógica es correcta: cuando la barra lateral cambia de altura, el contenido ya existe.
  - `test_t1` no se modificó.
  - No quedó demostrado por qué la carrera apareció justo con T10 (probablemente por el tiempo de reconstrucción); el arreglo la elimina en cualquier caso.
- **«1000 g»:** la pantalla ya mostraba la cantidad y la unidad tal como se compraron; la captura 27 usa una compra de 1000 g. `test_t10` comprueba ahora «1 kg» y «500 g» sin conversión.
- **Motivo en Barra:**
  - Es la columna flexible; a 1200 × 720 mide 273 px en estándar y 301 px en táctil.
  - A 960 × 600 el motivo completo se lee al seleccionar la fila (etiqueta con ajuste y descripción emergente).
  - La captura 28 muestra «Fruta machucada» completo.
- **Verificación del orquestador:**
  - Compilación de Release, Debug y `build_hooks_release` sin advertencias.
  - Batería GUI 16/16:
    - pasan dos veces `test_t1_touch_profile` (una de ellas después de `test_t10`) y dos veces seguidas `test_t10_restock_bar`;
    - también pasan `f7`, `f2b`, `t7`, `i2`, `f1`, `f2`, `fe08_fe09`, `i1`, `t2`, `t4`, `t5` y `t6`.
- **Pendiente:** batería al 100 % (T8, T9 y T10).
