# Memoria del orquestador — Cafetería ITCH II

Solo para el orquestador. Los agentes trabajadores no necesitan este archivo: lo compartido está en `AGENTS.md` (Memoria operativa) y `coordinacion-agentes.md`. Leerlo al inicio de cada sesión en lugar de reconstruir el contexto. Actualizarlo al cerrar cada ronda.

Última actualización: 2026-11-12.

## Estado

- **Puertas:** I1 cerrada (revalidada con el ejecutable real tras integrar B1). I2: software cerrado; falta QA humano (0/7 en `frontend/qa-visual.md`), impresora térmica 55/80, monitor táctil y DPI 125/150/200. I3–I7 abiertas. **No iniciar I3 hasta el QA humano.**
- **Pantallas reales:** Venta, Caja y turnos, Usuarios y cuentas, Configuración (impresión), Roles y permisos (F1/F1b), Bitácora (F2). El resto de las 16 subsecciones son marcadores de posición.
- **Backend real nuevo:** respaldo seguro y restauración (B1, migración 008, `backup.restore` solo Dueño) y reportes diarios (B2, `payments_total` y `payments_mismatch_cents`).
- **Estimación de release** (dada el 2026-10-19): piloto en 4–6 semanas y 1.0 en 8–12, si se resuelven la sección 9 de `specs.md` y los datos de la PC de caja e impresora.

## Ronda en curso

Ronda 3 cerrada e integrada el 2026-11-12 (`1bb0a4d`): **F2** (Bitácora, `1353162`) y **B3a** (panel de roles por regla de dos pasadas, `9ff0e9e`) aceptados; capturas 09/10 regeneradas. Worktree del backend en la rama `backend/b3b`, partiendo de `main`.
**Ronda 4 enviada** (`prompts-2026-11-12-ronda4.md`): **B3b** (Claude: `ui_helpers` con reparto de columnas y campo centrado, aplicado a Roles y Bitácora; endurece `test_f2_audit_log.py`) y **F2b** (Gemini: resaltado de subsección; causa probable `GetLabel().ToStdString()` contra UTF-8 en `subnav_bar.cpp`, falla con acentos; prueba nueva `test_f2b_subnav.py`). Excepción: ambos corren pruebas GUI comprobando antes `tasklist`. Al integrar: regenerar captura 11. Después F3 (UI Respaldos) y F4 (UI Reportes). Bloque grande de backend sin elegir (corte de turno I4 u otros pendientes técnicos).

## Decisiones del usuario

- 2026-11-08: el Dueño edita los permisos del Cajero; no puede renombrarlo ni eliminarlo. Dueño en solo lectura. Desarrollador nunca visible (registrado en `specs.md` §9.5).
- 2026-11-08: Claude Code Terminal con `opusplan` como backend; AntiGravity como frontend; skill caveman en ambos (solo estilo).
- 2026-11-08: regla de dos pasadas. Un detalle de interfaz que Gemini no cierra en dos pasadas va a Claude Code Terminal (detalles en `coordinacion-agentes.md`, protocolo punto 6). Primer caso: B3a, detalles del panel de roles tras F1 y F1b.
- El usuario quiere ver los movimientos de los agentes: no enviar prompts por `agy -p`; el usuario pega los prompts en terminales interactivas.

- 2026-11-12: los permisos de un rol se activan con interruptores (estilo ajustes de teléfono) en lugar de casillas, más grandes en Touch. Bloque **F5** para Gemini, prompt al final de `prompts-2026-11-12-ronda4.md`; enviar tras aceptar F2b e integrar B3b (`c71d586`, pendiente de merge en `main`).

## Git e integración

- `main` en `D:\Cafetería_ITCH`; worktree del backend en `D:\Cafeteria_ITCH_backend` (rama `backend/<bloque>`). Al aceptar un bloque de backend: commit en su rama, `git merge` en `main`, verificar el conjunto (build, CTest, I1) y luego `git merge --ff-only main` en el worktree con la rama siguiente.
- `.gitattributes` fija `eol=lf`. Auditar con `git diff --ignore-cr-at-eol` contra el último commit aceptado.
- Identidad git local: `oscarmichelmatosmay` / correo del usuario. Los commits llevan la línea `Co-Authored-By` de Claude.

## Cómo auditar (lista corta)

1. `git status` y `git diff --ignore-cr-at-eol --stat`: ¿solo archivos del dueño del bloque?
2. Leer el código clave; buscar anchos o umbrales fijados a ojo, ganchos de prueba en producción, mocks en el registro real y permisos que falten dentro del servicio.
3. Pruebas nuevas: ¿miden lo que afirman? ¿Se relajó algún umbral?
4. Compilar (desde cero si el bloque toca mucho) y CTest en Release y Debug; las pruebas GUI pertinentes con el escritorio libre.
5. Capturas: medir en píxeles con PIL, no a ojo (error propio en la ronda del prompt B).
6. Registrar en `auditoria-2026-10-18.md` (sección por bloque), hacer commit y actualizar este archivo.

## Perfil de los agentes

- **Claude Code Terminal (opusplan):** B1 sin caveman: plan sólido, informe exacto (salvo contar 80 `TEST_CHECK` en lugar de 102) y límites declarados con honestidad. Revisar su plan antes de aprobar: aportó 5 ajustes útiles. Puede reescribir finales de línea (ya resuelto con `.gitattributes`).
- **AntiGravity (Gemini 3.8 Flash high):** rápido en cableado de UI. Patrones: compila y corre pruebas sin haber editado (unos 7 min en F1); bloques de razonamiento de 7–9 min; exagera en informes («sin recorte» falso en F1, captura 08 inválida en 2026-10-14); fija anchos a ojo; sus pruebas miden solo lo que le es fácil (F1: botones sí, columnas no). Con aserciones estrictas y prompts pequeños funciona bien (prompts A y C). Pierde «», ≥ y ▾ al pegar: escribir sus prompts sin esos caracteres.
- **Caveman:** su README mide ~3 % de ahorro de salida (ruido). Ronda 2 comparada en `auditoria-2026-10-18.md`: la mejora visible vino de la memoria operativa, los prompts acotados y las pruebas estrictas; sin evidencia separable de caveman. El orquestador no lo usa (decisión del usuario).

## Pendientes conocidos (no asignados)

- Candidatos de backend para B3: build `build_hooks` con `-DCAFETERIA_TEST_HOOKS=ON` para el requerimiento 8 de I2; retención y limpieza de `backups/`; persistir `get_developer_server_config`; conciliación del corte de turno (I4, depende en parte de la sección 9).
- F1b menores: encabezado «Descripción» recortado, filas de la lista de roles sin altura táctil, texto arriba en campos de 48 px en Touch.

- Sección 9 de `specs.md`: 10 decisiones de negocio abiertas (bloquean I3/I4).
- Datos de la PC de caja e impresora (bloquean el empaquetado y el cierre de I2). Sin instalador todavía.
- Requerimiento 8 de `test_i2_integration.py` solo corre con un build `-DCAFETERIA_TEST_HOOKS=ON` (no existe aún un `build_hooks`).
- Página de impresión fija de 200 mm en rollo térmico: revisar con hardware.
- Aviso de impresora ausente usa `wxOK|wxCANCEL` (botón «Aceptar», texto dice «Continuar»).
- B1: retención/limpieza de `backups/` sin implementar; recuperación tras fallo a mitad de copia sin probar; el actor de `BACKUP_RESTORED` puede no existir en la base restaurada.
- `get_developer_server_config` sigue en memoria.

## Reanudar agentes

- AntiGravity (actual, ronda 3): `agy --conversation=5cb24680-56c1-4cd1-9621-72c8ef342fbd`. Anteriores: `36c58871-cb3f-45c6-88ad-07be0a8992e5`, `c94b5e25-55b3-490a-b547-abad9fb453b1`, `4c6c3e44-1c0a-40a4-8124-59943158b872`.
- Claude Code Terminal (actual, ronda 3): `claude --resume CafeCH`, abierto en `D:\Cafeteria_ITCH_backend` con el modelo `opusplan`.
- QA manual aislado: `.\build_release\app_desktop.exe --data-dir="D:\Cafeteria_QA_Manual"`.
