# Auditoría B9b — ventana de progreso propia

Ronda 8 — B9b (Claude Code Terminal, `backend/b9b`) — auditado el 2026-11-14

**B9b:** aceptado. Commit en `backend/b9b`; se integra en `main` después de auditar T4, para no mover `main` mientras Gemini trabaja en él.

- Alcance: solo `src/frontend/main_app.cpp` (+122/−25). Sin archivos nuevos ni cambios en `CMakeLists.txt`, en el servicio, en los avisos ni en `--self-check`.
- Cambio: el `wxProgressDialog` nativo se sustituye por `UpdateProgressWindow`, un `wxDialog` propio sin padre, sin botones, sin menú del sistema ni botón de cerrar, encima de las demás ventanas, que veta el cierre y no responde a Esc. Arriba, en grande y negrita, «No apague el equipo.»; debajo, «Estamos actualizando los datos de la cafetería. Puede tardar varios minutos.», la línea de fase de B9 y una barra (`Pulse` sin porcentaje, `SetValue` con porcentaje, nunca al 100). Medidas lógicas con `FromDIP` (ancho 480). Se repinta con `Update()` y `YieldFor(wxEVT_CATEGORY_UI)` sobre un bucle de eventos propio sin ejecutar, de modo que no procesa entrada del usuario ni temporizadores.
- Verificación del orquestador:
  - Worktree: build sin advertencias en Release y Debug; CTest 13/13 en Release.
  - **A mano** (100 %, base de prueba de 250 000 ventas de B9, ejecutable de `build_be_release`): la ventana aparece a los 0.4 s y mide 486 px de ancho (antes 366); ya no hay botón «Close»; «No apague el equipo.» es lo más visible; un `WM_CLOSE` y la tecla Esc no la cierran; la barra se mueve (entre capturas tomadas cada 1.2 s cambia la franja de la barra); a los 19.8 s se cierra y aparece el alta del Dueño sin la ventana detrás; deja el respaldo previo en `backups\pre-migracion\`.
- Observaciones (no bloquean): la barra de título dice «Cafetería ITCH» y el programa se llama «Cafetería ITCH II»; no se vio a 125 %; no se comprobó con la documentación que `YieldFor(wxEVT_CATEGORY_UI)` excluya en MSW todos los mensajes nativos de teclado y ratón (en la prueba, la ventana no reaccionó a Esc ni al cierre).
- Informe: exacto y honesto; declaró que no abrió la ventana y qué podía fallar en la animación (no falló).
