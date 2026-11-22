# Auditoría T16d — criterio de pantalla completa como prueba permanente

Ronda 12 — T16d (Gemini 4 Neon, Medium, `/caveman`) — auditado el 2026-11-22

**T16d: aceptado.** Cambios sin commitear en `main` sobre `747ee57`; entrega en `entregas/entrega-2026-11-22-T16d.md` (13:51 a 14:21, 125 %, 1920x1200). No toca `src/` (`git diff --stat src/` vacío).

- **`tests/test_t16_fullscreen_layout.py`** (nuevo) y **`tests/fullscreen_layout_lib.py`** (módulo común extraído de `measure_fullscreen_space.py`, que sigue dando las mismas 34 filas de pantalla completa).
- **Cobertura:** las 17 pantallas en pantalla completa y en ventana normal de 1200x720 y 960x600, en Estándar y Táctil. Incluye además el paso de ventana normal a pantalla completa en Configuración. Ese caso lo añadió Neon porque la primera regresión de prueba no se detectaba sin él: es un hallazgo bueno.
- **`SCREEN_STATES`:**
  - Configuración y Respaldos: terminadas.
  - Actualizaciones: excepción; solo deja de comprobar la franja vertical.
  - Usuarios y Roles: pendientes de T16b.
  - Las otras 12: pendientes de T16c.
- **Excepciones conocidas:** 17 explícitas, todas de pantallas pendientes, cada una con su bloque. `validate_config_integrity` impide añadir excepciones a una pantalla terminada.
- **Umbrales:** los de la sección 3 del plan de T16, importados como constantes (93 / 88 / 5 / 8), sin cambios.
- **Demostración de que falla:** dos regresiones temporales, ya revertidas.
  - Usar `Wrap()` en Configuración: falla por texto cortado en los dos perfiles.
  - Proporción 0 en Respaldos: falla con cobertura de alto de 81,3 % y franja vertical de 15,6 %.
- **Estabilidad:** 3 corridas seguidas en verde (unos 161 s cada una).
- **Verificación del orquestador:** con `gui-test.lock` tomado, compilé `build_release` con `--parallel 1` y corrí `test_t16_fullscreen_layout.py`: código 0 en 170 s, 17 excepciones activas, 0 avisos.
- **Hueco del prompt (del orquestador, no de Neon):** la prueba no comprueba las columnas de las listas: que llenen al menos el 95 % del ancho y que ninguna quede recortada mientras a otra le sobran 40 px lógicos. Tampoco comprueba el mínimo de permisos visibles en Roles. Las medidas ya existen (`LV(...cols=..., CLIP/SOBRA)`). Se agrega a T16b, que es donde se necesitan (Usuarios llena solo el 65 % y recorta «Rol»).
