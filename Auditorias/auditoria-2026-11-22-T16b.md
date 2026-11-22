# Auditoría T16b — Usuarios y Roles en pantalla completa

Ronda 12 — T16b (Gemini 4 Neon, High, `/caveman`, con plan previo aprobado) — auditado el 2026-11-22

**T16b: aceptado a la primera.** Es el primer bloque de T16 aceptado sin ronda de corrección: plan con números justificados, ajustes del orquestador aplicados y prueba de T16d como red. Entrega en `entregas/entrega-2026-11-22-T16b.md` (14:39 a 15:43).

- **Usuarios** (`backoffice_users_panel.*`):
  - Columnas al 100 % del ancho, recalculadas al cambiar de tamaño y al cargar datos.
  - «Usuario», «Rol», «Avatar» y «Estado» protegidos; el sobrante va a Nombre (45 %) y Correo (55 %).
  - Barra superior en dos filas cuando no cabe; el umbral se calcula con los controles reales.
  - `CallAfter` protegido con `alive_` (T6d).
  - Pantalla completa: `cols=100%, OK` en Estándar y Táctil, antes 65 % y 67 % con «Rol» cortado.
- **Roles** (`backoffice_roles_panel.*`):
  - Reparto 44:56 desde 1100 lógicos y 38:62 por debajo.
  - Encabezado y formulario en una o dos filas, con umbrales medidos en tiempo de ejecución.
  - Permisos en dos columnas en pantalla completa: 14 de 22 interruptores en Estándar y 12 en Táctil (antes 6 y 5).
  - En Táctil a 1200x720 se ven 4 filas completas, también con el mensaje de validación visible.
  - Desaparecen los tres textos cortados de 960x600.
  - `test_f1_roles_panel.py` sin cambios y en verde.
- **Prueba de T16d ampliada:**
  - Columnas al 95 % o más.
  - Sin columna cortada mientras otra tiene 40 px lógicos de sobra, convertidos con `gui_dpi`.
  - Mínimos de Roles comprobados.
  - Excepción de desplazamiento limitada a la lista de permisos (`scroll_perms_`, identificada por sus cuatro encabezados).
  - Usuarios y Roles pasan a «terminada»; quedan 16 excepciones, todas de T16c.
- **Observaciones:**
  - En Roles, la columna Descripción sigue cortando textos largos en pantalla completa (no hay sobra en otra columna, así que cumple el criterio).
  - El permiso «Anular una venta cobrada» (`pos.void_sale`, migración 002) aparece en Roles sin tener función: lo vio B18. Decidir si se oculta.
- **Verificación del orquestador (125 %, 1920x1200, con `gui-test.lock`):**
  - Release, Debug y hooks con `--parallel 1`, sin advertencias.
  - En verde: `test_t16_fullscreen_layout`, `test_f1_roles_panel`, `test_i1_integration`, `test_t1_touch_profile`, `test_t2_touch_targets`, `test_fe08_fe09_visual_integration`, `test_f2b_subnav` y `test_t4_numeric_keypad`.
  - `test_t5_fullscreen` falló una vez porque la ventana de la app de Claude quedó encima del diálogo de cobro (igual que el 2026-11-21); repetida dos veces, pasó las dos.
  - **Pendiente menor:** `test_t5` debería traer el programa al frente antes de comprobar el diálogo.
