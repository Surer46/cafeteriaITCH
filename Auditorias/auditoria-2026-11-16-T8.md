# Auditoría T8 — cancelar el diálogo de impresión en i2 y pulidos de T7

Ronda 9 — T8 (Gemini/AntiGravity, en `main`, sin commit) — auditado el 2026-11-16 al 125 % (120 DPI)

**T8: no aceptado todavía.** Lo principal funciona, pero hay una regresión y un criterio mío equivocado. Se corrige en **T8b** (`Prompts/prompts-2026-11-16-T8b.md`) sobre los mismos cambios sin commit.

- **Alcance:**
  - `tests/test_i2_integration.py`: cancela el diálogo moderno con UI Automation (botón «Cancelar», `InvokePattern`) y, si no puede, con Escape real por `SendInput` con el diálogo al frente. Cuenta los `PrintDialog.exe` nacidos durante la prueba.
  - `update_notes_dialog.cpp` y `backoffice_updates_panel.cpp`: la descarga no válida muestra solo el texto fijo.
  - `tests/test_t7_updates.py`: comprueba que no se repite ninguno de los dos textos y que no hay mensaje doble. Estima la barra con seis pestañas.
  - `subnav_bar.cpp`: pasa a `wxWrapSizer` (la barra puede partirse en dos filas) y calcula el ancho de cada botón según su texto.
- **Bien:**
  - **Diálogo de impresión:** se detecta y se cancela; los pasos 4 a 9 de I2 pasan en las dos corridas, sin imprimir.
  - **Mensaje doble:** resuelto.
  - **Resultado al reabrir:** se comprueba que no se repite ninguno de los dos textos.
  - **Compilación:** Release, Debug y `build_hooks_release` sin advertencias; CTest 17/17.
  - **Seis pestañas:** caben en una fila en estándar (1200 y 960) y en táctil a 1200. En táctil a 960 × 600 faltan 121 px, así que pasan a dos filas con botones de 60 px.
  - **Batería:** `test_t7_updates` pasa dos veces seguidas. También pasan `f1`, `f2`, `f2b`, `fe08_fe09`, `i1`, `t1`, `t2`, `t4`, `t5` y `t6`.
- **Regresión (bloqueante): `test_f7_ingredients_panel` falla en táctil a 960 × 600:** el botón Guardar de Ingredientes mide 22 px (mínimo 60).
  - Pasaba al 100 y al 125 % con T7.
  - Causa probable: Inventario tiene seis pestañas, y con el `wxWrapSizer` nuevo la barra baja a dos filas y le quita altura al contenido, que se comprime en lugar de desplazarse.
  - El cambio de la barra afecta a todas las secciones, no solo a BackOffice.
- **`test_i2`, falsa fuga de `PrintDialog.exe`:** las dos corridas fallan al final con «Quedó PrintDialog.exe colgado», aunque todos los pasos pasan. El orquestador lo midió:
  - **Sin ventana:** el proceso sigue vivo más de 2 minutos después de cerrar el programa, pero no tiene ninguna ventana.
  - **Windows lo reutiliza:** con ese proceso vivo, una nueva corrida de `test_i2` abrió y canceló el diálogo con el **mismo PID** y pasó (salida 0).
  - **Conclusión:** es la caché normal de Windows, no un cuelgue. El criterio que pidió T8 («falla si queda un `PrintDialog.exe` nacido durante la prueba») era equivocado: lo correcto es que no quede ninguna **ventana** de impresión visible. Matar ese proceso tampoco hace falta.
- **Observación:** en el paso con `SendInput`, la tecla Escape va a la ventana que esté al frente. Se acepta porque solo se usa si falla el camino por accesibilidad, y antes trae el diálogo al frente.
- **Sin probar:** al 100 %; capturas sin regenerar.
