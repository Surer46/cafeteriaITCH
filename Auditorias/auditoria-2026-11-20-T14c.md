# Auditoría T14c — memoria del teclado numérico y esperas de test_f2

Ronda 12 — T14c (Gemini 4 Neon en Medium como ejecutor, con `/caveman`; comparativa con Hydrogen) — auditado el 2026-11-20 por el orquestador

**T14c: aceptado al 125 %.** Commit del orquestador en `main` (ver el registro de `docs/ORQUESTADOR.md`).

- **Corrección del uso de memoria liberada** (seguimiento 1 de `auditoria-2026-11-18-T14b.md`):
  - `src/frontend/numeric_keypad.cpp:52-56`: las tres lambdas con `this` sobre `host_` pasan a ser funciones miembro (`on_host_show`, `on_host_size`, `on_host_destroy`, declaradas en `numeric_keypad.hpp:83-85`).
  - `numeric_keypad.cpp:93-101`: `~NumericKeypad()` las desenlaza de `host_` si sigue vivo, antes de `uninstall_filter()`. Si el anfitrión se destruye primero, `on_host_destroy` deja `host_` en nulo y el destructor no toca nada.
  - El `CallAfter` de `on_host_size` se encola sobre el propio teclado, y wxWidgets descarta las llamadas pendientes de un manejador destruido.
  - Sin cambios en la interfaz pública ni en el comportamiento visible.
- **Pruebas:**
  - `tests/test_f2_audit_log.py`: las cuatro pausas fijas de 0.8 s al pasar de Caja y Reportes a Bitácora (sesiones del Dueño y del Cajero) se sustituyen por `wait_until` de hasta 8 s sobre la misma condición. Ninguna aserción se quitó ni se relajó.
  - `tests/test_t4_numeric_keypad.py`: prueba nueva de regresión (Táctil, teclado abierto en Caja y turnos, salir y volver cinco veces; proceso vivo y ventana que responde).
- **Archivos:** solo los cuatro permitidos.
- **Verificación del orquestador en `main` (125 %, 120 DPI):** compilación de Release, Debug y hooks con `--parallel 1`, 0 advertencias; `test_f2_audit_log` 3 de 3 en verde (más las 5 de Neon); batería GUI completa 20/20 en verde: `test_t4`, `test_t14`, `test_t13`, `test_t12`, `test_t11`, `test_i2`, `test_i1`, `test_t10`, `test_t1`, `test_f7`, `test_f1`, `test_f2b`, `test_t7`, `test_fe08_fe09`, `test_t2`, `test_t5`, `test_t6`. Registros en el scratchpad de la sesión (`t14c_120`). Sin CTest: T14c no toca el backend.
- **Comparativa (Neon Medium como ejecutor):** 15 minutos (22:29 a 22:44), una compilación por configuración, 0 fallos intermedios, 0 defectos en la auditoría, cerca del 3.2 % de la cuota de 5 horas de AntiGravity (100 % → 96.82 %). La otra mitad de la comparativa es T15 con Hydrogen (High).
- **Pendiente:** pasada al 100 %.
