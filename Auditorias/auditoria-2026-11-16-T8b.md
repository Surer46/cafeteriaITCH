# Auditoría T8b — barra de subsecciones con flechas y criterio de PrintDialog

Ronda 9 — T8b (Gemini/AntiGravity, en `main`, sin commit) — auditado el 2026-11-16 al 125 % (120 DPI)

**T8b: casi.** Las dos correcciones de fondo están bien, pero dos pruebas fallan por su propia culpa. Se cierra con **T8c** (`Prompts/prompts-2026-11-16-T8c.md`).

- **Barra de subsecciones:**
  - Ahora es de una sola fila con desplazamiento horizontal y flechas «<» y «>» del tamaño táctil, que solo aparecen si las pestañas no caben.
  - Altura fija (`bar_h` 55 px al 125 %): ya no le quita altura al contenido. **`test_f7` vuelve a pasar.**
  - En `test_f2b`, las cuatro secciones caben en una fila en estándar a 1200 y a 960. En Inventario a 960 la holgura es de 66 px.
- **`test_i2`:**
  - Pasa dos veces seguidas.
  - Solo informa el `PrintDialog.exe` en caché sin ventanas, sin matarlo.
- **También pasan:**
  - `f1`, `f2`, `fe08_fe09`, `i1`, `t1`, `t2`, `t4`, `t5` y `t6`.
  - Compilación de Release, Debug y `build_hooks_release` sin advertencias.
- **Fallos:**
  1. **`test_t7_updates`** no prueba nada: `NameError: name 'max_px' is not defined` (falta importarlo de `gui_dpi`). La prueba no pudo haberse corrido con el archivo final.
  2. **`test_f2b_subnav`**, táctil a 960 × 600, Inventario: tras un toque de «>» la última pestaña no queda visible.
     - El código avanza una pestaña por toque, que es lo correcto; la prueba esperaba la última con un solo toque.
     - Al 125 % quedan dos pestañas ocultas. Se corrige la prueba (toques repetidos), no el umbral.
- **Ajeno a T8b — `UpdateTest` (CTest) falla desde la tarde:**
  - Mensaje: «Se esperaba READY y es AVAILABLE (la descarga llegó incompleta o dañada)», en la prueba de persistencia.
  - Falla con el **mismo ejecutable de `main` (11-15, 23:59) que pasó 17/17 por la mañana y a media tarde**, en Release, Debug y con ganchos.
  - El ejecutable de B13 en curso pasa la misma prueba.
  - T8b no toca servicios.
  - Sospechas: cambio de fecha en UTC (empezó después de las 18:00 locales) o bloqueo momentáneo del archivo por el antivirus al verificarlo. Se pasa a Claude Code Terminal con la revisión de B13.
