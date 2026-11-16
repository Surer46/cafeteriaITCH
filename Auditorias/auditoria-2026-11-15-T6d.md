# Auditoría T6d — prueba de Respaldos fiable (y defecto al destruir la pantalla)

Ronda 9 — T6d (AntiGravity, Gemini) — auditado el 2026-11-15

**T6d:** aceptado al 125 %. Con él queda **aceptado T6c**. Falta repetir la batería al 100 %.

- **Defecto real del programa (causa del paso 9d):** al salir de la pantalla Respaldos, `wxListCtrl` dispara eventos de deselección mientras se destruye. Los manejadores del panel (`sync_action_states`, ajuste de columnas) usaban miembros ya liberados y la ventana principal dejaba de responder.
  - Corrección en `backoffice_backups_panel.{cpp,hpp}`: bandera `destroying_`, `Unbind` de los eventos de la lista y `DestroyChildren()` en el destructor de la clase derivada (lo estándar en wxWidgets).
  - Guardas `destroying_ || IsBeingDeleted()` en el temporizador, en el ajuste de columnas y en el estado de los botones.
- **Prueba (`test_t6_backups.py`):**
  - La lectura UIA fuerza UTF-8 en PowerShell y en `check_output`, y una sola función sirve a los dos sitios.
  - `sync_ui` (`WM_NULL`) antes de leer textos y después de cada clic de navegación.
  - Espera a que la pantalla termine de construirse (el botón «Borrar respaldos antiguos», el último que se crea).
  - Diagnóstico de la ventana si no aparece la barra lateral.
  - Las aserciones que parecían quitadas siguen, convertidas en esperas con condición que fallan igual; no se relajó nada (137 → 135 líneas con aserción por esa conversión).
- **Verificación del orquestador al 125 % (120 DPI), consola en página 437:**
  - Compilación de Release y Debug correcta; CTest 16/16 en las dos.
  - `test_t6_backups` pasa **dos veces seguidas** (Release y Debug cada vez).
  - Batería GUI: 11 de 11 pasan. `test_t5_fullscreen` falló a la primera porque la ventana de `claude.exe` quedó encima (el mensaje lo identifica) y pasó al repetirla.
- **Menor:** la captura 23 corta la lista de respaldos (solo se ven los encabezados y el inicio de la primera fila); conviene desplazarla cuando se regenere.
- **Pendiente:** batería al 100 % (el usuario cambia el escalado). La entrega escrita de T6c y T6d no llegó al orquestador; lo revisado aquí sale del código y de las pruebas.
