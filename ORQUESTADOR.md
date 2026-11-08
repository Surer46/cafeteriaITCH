# Memoria del orquestador — Cafetería ITCH II

Solo para el orquestador. Los agentes trabajadores no necesitan este archivo: lo compartido está en `AGENTS.md` (Memoria operativa) y `coordinacion-agentes.md`. Leerlo al inicio de cada sesión en lugar de reconstruir el contexto. Actualizarlo al cerrar cada ronda.

Última actualización: 2026-11-08.

## Estado

- **Puertas:** I1 cerrada (revalidada con el ejecutable real tras integrar B1). I2: software cerrado; falta QA humano (0/7 en `frontend/qa-visual.md`), impresora térmica 55/80, monitor táctil y DPI 125/150/200. I3–I7 abiertas. **No iniciar I3 hasta el QA humano.**
- **Pantallas reales:** Venta, Caja y turnos, Usuarios y cuentas, Configuración (impresión), Roles y permisos (F1). El resto de las 16 subsecciones son marcadores de posición.
- **Backend real nuevo:** respaldo seguro y restauración (B1, migración 008, permiso `backup.restore` solo Dueño).
- **Estimación de release** (dada el 2026-10-19): piloto en 4–6 semanas y 1.0 en 8–12, si se resuelven la sección 9 de `specs.md` y los datos de la PC de caja e impresora.

## Ronda en curso

Ronda 2 (`prompts-2026-11-08-ronda2.md`), primera con caveman: B2 (reportes, Claude) en paralelo con F1b (correcciones de roles, Gemini); F2 (Bitácora) después de auditar F1b. Después: F3 (UI de Respaldos, depende de B1) y F4 (UI de Reportes, depende de B2). Plan de reparto en `coordinacion-agentes.md`.

## Decisiones del usuario

- 2026-11-08: el Dueño edita los permisos del Cajero; no puede renombrarlo ni eliminarlo. Dueño en solo lectura. Desarrollador nunca visible (registrado en `specs.md` §9.5).
- 2026-11-08: Claude Code Terminal con `opusplan` como backend; AntiGravity como frontend; skill caveman en ambos (solo estilo).
- El usuario quiere ver los movimientos de los agentes: no enviar prompts por `agy -p`; el usuario pega los prompts en terminales interactivas.

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
- **Caveman:** su README mide ~3 % de ahorro de salida (ruido). Comparar la ronda 2 contra la ronda 1 (B1/F1 sin caveman): tiempos, lecturas y calidad de la entrega.

## Pendientes conocidos (no asignados)

- Sección 9 de `specs.md`: 10 decisiones de negocio abiertas (bloquean I3/I4).
- Datos de la PC de caja e impresora (bloquean el empaquetado y el cierre de I2). Sin instalador todavía.
- Requerimiento 8 de `test_i2_integration.py` solo corre con un build `-DCAFETERIA_TEST_HOOKS=ON` (no existe aún un `build_hooks`).
- Página de impresión fija de 200 mm en rollo térmico: revisar con hardware.
- Aviso de impresora ausente usa `wxOK|wxCANCEL` (botón «Aceptar», texto dice «Continuar»).
- B1: retención/limpieza de `backups/` sin implementar; recuperación tras fallo a mitad de copia sin probar; el actor de `BACKUP_RESTORED` puede no existir en la base restaurada.
- `get_developer_server_config` sigue en memoria.

## Reanudar agentes

- AntiGravity: `agy` (nueva) o `agy --conversation=<id>`; historial anterior en la conversación `36c58871-cb3f-45c6-88ad-07be0a8992e5`; más antiguas `c94b5e25-55b3-490a-b547-abad9fb453b1` y `4c6c3e44-1c0a-40a4-8124-59943158b872`.
- Claude Code Terminal: abrir en `D:\Cafeteria_ITCH_backend` con el modelo `opusplan`.
- QA manual aislado: `.\build_release\app_desktop.exe --data-dir="D:\Cafeteria_QA_Manual"`.
