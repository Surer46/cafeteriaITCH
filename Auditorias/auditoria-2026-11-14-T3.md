# Auditoría T3 — cierre del perfil Táctil en Usuarios, Roles, cobro e historial

Ronda 8 — T3 (AntiGravity, Gemini 4 Hydrogen high) — auditado el 2026-11-14

**T3:** aceptado. La prueba de objetivos táctiles queda **sin excepciones**: todo control pulsable de las 14 vistas recorridas mide al menos 48x48 lógicos y toda fila de lista al menos 40, al 100 y al 125 %.

- Alcance: `ui_helpers.{hpp,cpp}`, `audit_log_panel.cpp`, `backoffice_print_config_panel.cpp`, `backoffice_users_panel.cpp`, `checkout_dialog.{hpp,cpp}`, `ticket_history_dialog.cpp`, `toggle_switch.cpp`, `inventory_ingredients_panel.cpp` y `tests/test_t2_touch_targets.py` (solo vaciar `KNOWN_EXCEPTIONS`). No tocó `test_f1_roles_panel.py`, `test_i1_integration.py` ni nada fuera de lo permitido. Capturas y `touch_audit.txt` regenerados.
- Cambios:
  - `ui::TouchComboBox`, una sola clase (antes duplicada en Bitácora e impresión), usada también en el rol del usuario nuevo y en el método de pago. `ui::make_text_field` acepta un estilo (contraseña, Intro).
  - Usuarios: filas de 43.2; diálogo de usuario nuevo con campos, rol y botones de 48; el `wxFilePickerCtrl` con «Browse» se sustituyó por «Elegir foto...» con `wxFileDialog` y un texto con el archivo elegido.
  - Interruptor: caja de 60x48 en Touch con el dibujo de 32 centrado; Estándar igual.
  - Cobro: método de pago y monto recibido a 48; el diálogo se ajusta por sizer. Historial: búsqueda, Buscar y filas a 48/43.2.
  - Ingredientes a 125 % (Touch 960x600): columnas 103/111/206/152 = 572 px en 575. Para lograrlo, el margen de encabezado de `ui::distribute_list_columns` pasó de 20 a 16 lógicos (afecta a **todas** las listas) y la lista ganó proporción frente al detalle (14 → 16) con relleno de 12 → 10.
- Revisión del orquestador: `test_t2_touch_targets.py` solo pierde la lista de excepciones (35 entradas) y su comentario. `test_i2_integration.py` no maneja el método de pago, así que el cambio de control del cobro no la afecta (revisado por lectura; Gemini no lo informó). El cambio de valor del monto usa `ChangeValue`, que no dispara eventos, como antes al construirlo.
- Verificación del orquestador:
  - **125 %:** build sin advertencias; CTest 11/11 en Release y Debug (PackageTest omitida en Debug); `test_i1_integration`, `test_t1_touch_profile`, `test_t2_touch_targets`, `test_f2b_subnav`, `test_fe08_fe09_visual_integration`, `test_f2_audit_log`, `test_f7_ingredients_panel`, `test_f1_roles_panel` y `capture_visual_qa` exit 0.
  - **100 %:** las mismas ocho pruebas exit 0.
  - `test_i2_integration` sin correr (diálogo de impresión del escritorio real).
- Observaciones (no bloquean): en Roles, perfil Touch, solo caben uno o dos permisos a la vista (pendiente conocido desde F5); el margen de encabezado más corto es un cambio visual global pequeño; el resultado de la búsqueda de historial y la ventana de cobro no se probaron a mano.
- Informe: literal, con medidas por control; declaró una compilación fallida por tener el ejecutable abierto en una prueba.
