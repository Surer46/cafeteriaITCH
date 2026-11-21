# Auditoría T16a — I7 fase 2: Actualizaciones, Configuración y Respaldos en pantalla completa

Ronda 12 — T16a (Gemini 4 Neon, Medium, `/caveman`) — auditado el 2026-11-21

**T16a: no aceptado todavía; requiere T16a2.** Cambios sin commitear en `main` (sobre `1cf2035`). Entrega en `entregas/entrega-2026-11-21-T16a.md` (00:54 a 01:19, 125 %, 1920x1200).

Revisión del orquestador: diff completo de los ocho archivos de `src/frontend/` y de `tests/measure_fullscreen_space.py`, `qa_visual/fullscreen/medidas.txt` y las capturas de pantalla completa y de ventana normal, comparadas con `4ff6905`. No recompilé ni corrí pruebas: la ronda de corrección cambia el mismo código.

## Lo que queda aceptado

- **Configuración en pantalla completa** (`backoffice_settings_panel.cpp:240-315`, `backoffice_print_config_panel.cpp:195-227`): «Pantalla de este equipo» (4) y la impresora (6) lado a lado desde 1100 px lógicos; la impresora se apila por dentro, los selectores crecen y la vista previa ocupa el alto restante. «Guardar Configuración» se ve sin desplazarse. Cumple el criterio en Estándar (93,4 % x 89,8 %) y Táctil (96,6 % x 93,0 %).
- **Actualizaciones** (`backoffice_updates_panel.cpp:299-390`): dos tarjetas lado a lado a toda la altura, sin contenido inventado, como decidió el usuario. El criterio marca NO por la franja vertical (72,5 % / 69,3 %): es el interior vacío de las tarjetas, porque no hay más contenido que mostrar. Se acepta como **excepción documentada**; T16d la reflejará en la prueba.
  - **Observación:** el botón «Actualizar desde un archivo (USB)» queda al pie de la tarjeta, a unos 900 px de su texto. Es coherente con la decisión del usuario; se le informa.
- La columna `cumple` de `measure_fullscreen_space.py` aplica los cuatro umbrales del plan, sin aserciones.
- El cambio de disposición desacopla y vuelve a añadir los mismos elementos al sizer, sin destruir ventanas; no se observa riesgo de destrucción.

## Hallazgos que impiden aceptar

1. **Respaldos en pantalla completa no cumple lo pedido, y la entrega afirma lo contrario.**
   - En `standard_16_respaldos.png`, «Ver más» y «Restaurar el seleccionado» salen cortados en el borde inferior.
   - En `touch_16_respaldos.png` no se ven.
   - En las dos sigue la barra vertical, y la tarjeta de limpieza queda fuera de la vista.
   - Causa: `card_list_` entra en `bottom_row_` con proporción 0, así que la lista no absorbe el alto; además, la altura mínima fija (260/220) empuja el resto hacia abajo.
   - El script marca SI porque mide solo lo visible y no detecta que hay desplazamiento. T16d debe incluir la barra vertical en el criterio.
2. **Regresión en ventana normal (1200x720).**
   - En `qa_visual/screenshots/14_settings_standard.png` y `15_settings_touch.png`, Configuración tiene ahora barra horizontal y la tarjeta «Táctil» sale cortada a la derecha. En `4ff6905` cabía.
   - Viola la regla del prompt («en 1200x720 y 960x600 nada se recorta») y ninguna prueba lo detectó.
   - Causa probable: `opt_sizer` y `prev_box` pasaron de proporción 0/1 a 4/5. El `wxBoxSizer` agranda su mínimo a max(mínimo/proporción) × suma, y `lbl_printer_hint` (líneas con `\n` fijo) fija un mínimo ancho.

## Siguiente paso

T16a2 (`Prompts/prompts-2026-11-21-T16a2.md`), para Neon:
- Respaldos sin desplazamiento en pantalla completa, con lista y limpieza lado a lado.
- Corregir la regresión de 1200x720 y 960x600.
- Agregar al script una pasada informativa en ventana normal que detecte barras horizontales y verticales.

## T16a2 (Neon; 01:46 a 02:47; auditado el 2026-11-21)

**T16a2: aceptado en parte; requiere T16a3.** Entrega en `entregas/entrega-2026-11-21-T16a2.md`. Revisé capturas y `medidas.txt`; no recompilé.

- **Respaldos en pantalla completa: corregido.** «Lista de respaldos» y «Limpieza de respaldos antiguos» quedan lado a lado (11:10), sin barra vertical, con todos los botones visibles, en Estándar (97,5 % x 93,9 %) y Táctil (97,5 % x 94,2 %).
  - **Observación:** en Táctil, la lista de limpieza muestra una sola fila (52 px).
- **Regresión de 1200x720: corregida.** La pasada nueva en ventana normal da `hscroll=NO` en las 12 combinaciones (tres pantallas, dos tamaños, dos perfiles). Las capturas 14 y 15 vuelven a verse como en `4ff6905`; `opt_sizer_` vuelve a proporción 0 fuera del modo apilado.
- **Regresión nueva: Configuración en pantalla completa, Táctil** (`touch_15_configuracion.png`, `20_settings_fullscreen_option.png`). En T16a cumplía y ahora no (92,3 % de ancho, franja de 76 px, `vscroll=SI`):
  - el subtítulo y la descripción de pantalla completa pierden su segunda línea, y desaparece la mención de F11;
  - la vista previa queda cortada abajo.
  - Neon lo anotó como límite en lugar de corregirlo. El script no detecta texto cortado.

Siguiente: T16a3 (`Prompts/prompts-2026-11-21-T16a3.md`).
- Configuración sin desplazamiento y sin texto cortado en los dos perfiles.
- Columna informativa `texto_cortado` en el script.
