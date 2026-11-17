# Auditoría T9 y B13b — motivos de actualización en pantalla y UpdateTest sin antivirus

Ronda 9 — T9 (Gemini, `main`) y B13b (Claude Code Terminal, `backend/b13b`) — auditado el 2026-11-16 al 125 % (120 DPI)

**T9: aceptado al 125 %. B13b: aceptado** (commit `dc36b04`, merge `e542046`).

- **T9:**
  - **Franja al reabrir:** elige el aspecto por `reason` y muestra el `message` del servicio. Un `reason` vacío se trata como NOT_COMPLETED.
  - **Descarga no válida:** el texto fijo aparece solo con `download_error_kind == "INVALID"`; en cualquier otro caso, incluido vacío, se muestra el mensaje del servicio.
  - **USB:** usa `apply_update_from_files`; NOT_NEWER tiene su texto y los demás motivos muestran el mensaje del servicio.
  - **Sin búsquedas de palabras:** se quitaron `is_verification_rejection`, `is_invalid_dl_error` y las búsquedas de «autorización de Windows», «cancelado» y «más nueva». Gemini lo comprobó con `Select-String` sobre `src/frontend`.
  - **`test_t7_updates`:** comprueba los cuatro motivos al reabrir, cada uno una sola vez (con `CAFETERIA_UPDATE_TEST_LAUNCH_RESULT`), la descarga alterada, la descarga sin red y los tres casos de USB. Normaliza los espacios porque la franja ajusta el texto a varias líneas; no relaja ninguna aserción.
  - **Sin probar:** errores de descarga distintos de INVALID y NO_NETWORK, porque la fuente local de prueba no puede simularlos.
- **B13b:**
  - Los instaladores de prueba de `UpdateTest` son texto ASCII repetido con contador, distinto por versión, que nunca empieza por «MZ».
  - Nota en `docs/entorno.md`: si `UpdateTest` dice «incompleta o dañada», revisar `Get-MpThreatDetection`; nunca añadir exclusiones.
  - Revisó los demás archivos de prueba: ninguno crea `.exe` aleatorios.
- **Verificación del orquestador en `main` (`e542046` + T9):**
  - Compilación de Release, Debug y `build_hooks_release` sin advertencias.
  - **CTest 17/17 en Release y en Debug.**
  - **0 detecciones nuevas de Defender** durante la compilación y CTest.
  - Batería GUI 14/14, con `test_t7_updates` dos veces seguidas.
- **Observación:** Gemini se cruzó con `test_endsession.exe` del worktree del backend, que lanza un `app_desktop.exe` temporal. Sus pruebas lo detectaron con `tasklist`, esperaron y repitieron. Las baterías GUI del frontend y CTest del backend no deben correr a la vez: se anota para coordinar.
- **Pendiente:** batería al 100 % (T8 y T9).
