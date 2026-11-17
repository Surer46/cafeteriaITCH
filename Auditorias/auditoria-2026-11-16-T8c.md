# Auditoría T8c — cierre de T8 (impresión en i2, barra de subsecciones y pulidos de T7)

Ronda 9 — T8c (Gemini/AntiGravity, en `main`) — auditado el 2026-11-16 al 125 % (120 DPI)

**T8, T8b y T8c: aceptados al 125 %.** Integrados por el orquestador en un solo commit, sobre `main` con B13 ya integrado (`14e4e03`). Al 100 % se repite la batería en la próxima ocasión en que el usuario cambie el escalado.

- **T8c:**
  - `test_t7_updates` importa `max_px`.
  - `test_f2b_subnav` toca «>» y «<» tantas veces como haga falta: en Inventario, táctil a 960 × 600, fueron 2 toques en cada dirección.
  - Comprueba que cada toque desplaza la barra, que al final la última (o la primera) pestaña queda completa, y que al elegir una pestaña parcialmente oculta la barra la trae completa a la vista. `subnav_bar.cpp` sin cambios.
- **Conjunto de T8:**
  - **Cancelación del diálogo de impresión moderno:** por UI Automation, con Escape real como respaldo.
  - **Criterio de `PrintDialog.exe`:** que no quede ninguna ventana visible; el proceso en caché de Windows solo se informa.
  - **Barra de subsecciones:** una fila de altura fija con flechas táctiles cuando no cabe.
  - **Mensajes y pruebas de T7:** mensaje único de descarga no válida; la prueba de una sola vez revisa los dos textos.
  - **Seis pestañas de BackOffice:** medidas.
- **Verificación del orquestador en `main` (con B13):**
  - Compilación de Release, Debug y `build_hooks_release` sin advertencias.
  - CTest 17/17 en Release. En Debug, `UpdateTest` falla por Windows Defender (ver abajo), ajeno a T8.
  - Batería GUI 15/15:
    - pasan dos veces seguidas `test_t7_updates` y `test_i2_integration`;
    - también pasan `f2b`, `f7`, `f1`, `f2`, `fe08_fe09`, `i1`, `t1`, `t2`, `t4`, `t5` y `t6`.
  - Capturas regeneradas al 125 %; la 13 (Ingredientes táctil) muestra las seis pestañas en una fila y Guardar a tamaño completo.
- **Ajeno a T8 — `UpdateTest`:**
  - Con la semilla nueva de B13, Windows Defender vuelve a detectar el instalador de prueba pseudoaleatorio como `Trojan:Win32/Bearfoos.A!ml` (30 detecciones, todas sobre archivos `.part` de la prueba).
  - Falla en Release y Debug de `main` y en `build_be_debug`.
  - Se corrige en **B13b** (contenido de prueba de baja entropía, sin exclusiones del antivirus).
- **Pendiente:**
  - **T9:** la interfaz usa los motivos de B13.
  - Batería al 100 %.
