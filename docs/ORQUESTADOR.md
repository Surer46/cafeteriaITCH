# Memoria del orquestador — Cafetería ITCH II

Solo para el orquestador. Los agentes trabajadores no necesitan este archivo: lo compartido está en `AGENTS.md` (Memoria operativa) y `docs/coordinacion-agentes.md`. Leerlo al inicio de cada sesión en lugar de reconstruir el contexto. Actualizarlo al cerrar cada ronda.

Última actualización: 2026-11-12.

## Portabilidad (decisión del usuario, 2026-11-12)

- Los archivos versionados no llevan rutas absolutas ni estado de una PC concreta, para que cualquier agente pueda retomar en cualquier PC siguiendo `docs/entorno.md`. Lo propio de cada PC (rutas reales, qué está instalado, qué se verificó ahí, sesiones de los agentes) va en `..\notas-pc.md`, fuera del repositorio; nunca se sube a GitHub. Leerlo al iniciar sesión si existe.
- Las rutas `D:\...` de auditorías y prompts con fecha son históricas; al enviar un prompt pendiente, sustituirlas por las de la PC en uso.
- Las conversaciones de los agentes no viajan con el repositorio: en una PC nueva los ejecutores empiezan sin contexto y los prompts deben ser autosuficientes.
- Markdown reorganizados (`7ea9d50`): `Auditorias/`, `Prompts/`, `docs/`. No hay `CLAUDE.md` en el repositorio; la memoria del orquestador es este archivo.
- Pendiente menor para Gemini: las pruebas `tests/*.py` anteponen al `PATH` rutas `D:\Temp\...` fijas. No estorban (CMake copia las DLL a la carpeta de compilación), pero conviene quitarlas.

## Estado

- **Puertas:** I1 cerrada (revalidada con el ejecutable real tras integrar B1). I2: software cerrado; falta QA humano (0/7 en `frontend/qa-visual.md`), impresora térmica 55/80, monitor táctil y DPI 125/150/200. I3–I7 abiertas. **No iniciar I3 hasta el QA humano.**
- **Pantallas reales:** Venta, Caja y turnos, Usuarios y cuentas, Configuración (impresión), Roles y permisos (F1/F1b), Bitácora (F2). El resto de las 16 subsecciones son marcadores de posición.
- **Backend real nuevo:** respaldo seguro y restauración (B1, migración 008, `backup.restore` solo Dueño) y reportes diarios (B2, `payments_total` y `payments_mismatch_cents`).
- **Estimación de release** (dada el 2026-10-19): piloto en 4–6 semanas y 1.0 en 8–12, si se resuelven la sección 9 de `docs/specs.md` y los datos de la PC de caja e impresora.

## Ronda en curso

Ronda 4 parcial, integrada el 2026-11-12 (`af4e4bc`): **B3b** (`c71d586`, utilidades `ui_helpers`) y **F2b** (`dba641a`, resaltado de subsección) aceptados; capturas 10 y 11 regeneradas. En cola, prompts en `Prompts/prompts-2026-11-12-ronda4.md`: **B4a** (Claude, títulos de permisos, migración 009; rama `backend/b4a` ya al día con `main`) -> **F5** (Gemini, interruptores; requiere B4a integrado) y **B4** (Claude, ajustes de datos, migraciones desde 010; tras B4a). Después F3 (UI Respaldos) y F4 (UI Reportes).

Actualización 2026-11-12 (PC nueva): **F0 aceptado** (Gemini 4 Hydrogen) y **B4a aceptado** en `1fc6dc5` e integrado en `main` tras F0. Siguiente: **F5**. Nota histórica del orden: Orden acordado con el usuario: Gemini hace **F0** (quitar rutas fijas de `tests/*.py` y correr las pruebas GUI) sobre el `main` actual; al aceptarlo se integra B4a y se envía **F5** de inmediato, porque la 009 rompe `test_f1_roles_panel.py` hasta que F5 lo reescriba. B4 parte de `main` con B4a integrado.

## Decisiones del usuario

- 2026-11-08: el Dueño edita los permisos del Cajero; no puede renombrarlo ni eliminarlo. Dueño en solo lectura. Desarrollador nunca visible (registrado en `docs/specs.md` §9.5).
- 2026-11-08: Claude Code Terminal con `opusplan` como backend; AntiGravity como frontend; skill caveman en ambos (solo estilo).
- 2026-11-08: regla de dos pasadas. Un detalle de interfaz que Gemini no cierra en dos pasadas va a Claude Code Terminal (detalles en `docs/coordinacion-agentes.md`, protocolo punto 6). Primer caso: B3a, detalles del panel de roles tras F1 y F1b.
- El usuario quiere ver los movimientos de los agentes: no enviar prompts por `agy -p`; el usuario pega los prompts en terminales interactivas.

- 2026-11-12: los permisos de un rol se activan con interruptores (estilo ajustes de teléfono) en lugar de casillas, más grandes en Touch. Bloque **F5** para Gemini, prompt al final de `Prompts/prompts-2026-11-12-ronda4.md`; enviar tras aceptar F2b e integrar B3b (`c71d586`, pendiente de merge en `main`). Además cada permiso lleva título y descripción en lenguaje llano, separador entre permisos y agrupación por las cuatro secciones de la barra lateral (el Dueño es una persona mayor): bloque **B4a** (Claude, migración 009 con `title`, `section`, `sort_order`), que debe integrarse antes de F5. B4 usa migraciones desde 010 y va después de B4a.

- 2026-11-12: títulos y descripciones de los 20 permisos aprobados por el usuario (tabla en el prompt B4a).
- 2026-11-12: salió Gemini 4 Hydrogen en AntiGravity. Recomendación del orquestador: no cambiar a mitad de F2b; probarlo en F5 como ensayo y comparar contra el perfil de 3.8 Flash (exageración en informes, anchos a ojo, pruebas débiles). Decisión del usuario: seguir con 3.8 Flash; en su plan Pro los modelos Gemini 4 (Carbon, Argon, Hydrogen) aparecen como «Unavailable» en el selector. Revisar de nuevo si se habilitan.
- 2026-11-12 (más tarde): Gemini 4 Hydrogen ya está disponible para el usuario. Plan del orquestador: usarlo en F0 (bloque mecánico: quitar rutas fijas de `tests/*.py` y correr las pruebas GUI) como calibración barata y, si respeta límites de archivos e informa de forma literal, en F5 como ensayo real. Mantener los prompts estrictos del perfil de 3.8 Flash hasta tener evidencia; anotar aquí su perfil tras auditar F0.

## Git e integración

- `main` en la copia principal; worktree del backend en `..\Cafeteria_ITCH_backend` (rama `backend/<bloque>`). Al aceptar un bloque de backend: commit en su rama, `git merge` en `main`, verificar el conjunto (build, CTest, I1) y luego `git merge --ff-only main` en el worktree con la rama siguiente.
- `.gitattributes` fija `eol=lf`. Auditar con `git diff --ignore-cr-at-eol` contra el último commit aceptado.
- Los commits llevan la línea `Co-Authored-By` de Claude.

- **Historial reescrito el 2026-11-12** para publicar sin el correo personal: autor y confirmador pasan a `Surer46` con su correo privado de GitHub. Los identificadores de commit citados en este archivo y en las auditorías con fecha anterior ya no existen en el repositorio; el historial original está en `Cafeteria_ITCH_antes_de_cambiar_correo.bundle` (solo en la PC original, unidad `D:`). Remoto: `origin` = `https://github.com/Surer46/cafeteriaITCH.git` (público). La identidad git local es ahora `Surer46`.

## Cómo auditar (lista corta)

1. `git status` y `git diff --ignore-cr-at-eol --stat`: ¿solo archivos del dueño del bloque?
2. Leer el código clave; buscar anchos o umbrales fijados a ojo, ganchos de prueba en producción, mocks en el registro real y permisos que falten dentro del servicio.
3. Pruebas nuevas: ¿miden lo que afirman? ¿Se relajó algún umbral?
4. Compilar (desde cero si el bloque toca mucho) y CTest en Release y Debug; las pruebas GUI pertinentes con el escritorio libre.
5. Capturas: medir en píxeles con PIL, no a ojo (error propio en la ronda del prompt B).
6. Registrar en `Auditorias/auditoria-2026-10-18.md` (sección por bloque), hacer commit y actualizar este archivo.

## Perfil de los agentes

- **Claude Code Terminal (opusplan):** B1 sin caveman: plan sólido, informe exacto (salvo contar 80 `TEST_CHECK` en lugar de 102) y límites declarados con honestidad. Revisar su plan antes de aprobar: aportó 5 ajustes útiles. Puede reescribir finales de línea (ya resuelto con `.gitattributes`).
- **AntiGravity (Gemini 3.8 Flash high):** rápido en cableado de UI. Patrones: compila y corre pruebas sin haber editado (unos 7 min en F1); bloques de razonamiento de 7–9 min; exagera en informes («sin recorte» falso en F1, captura 08 inválida en 2026-10-14); fija anchos a ojo; sus pruebas miden solo lo que le es fácil (F1: botones sí, columnas no). Con aserciones estrictas y prompts pequeños funciona bien (prompts A y C). Pierde «», ≥ y ▾ al pegar: escribir sus prompts sin esos caracteres.
- **AntiGravity (Gemini 4 Hydrogen high), primer bloque F0 (2026-11-12):** respetó la lista de archivos, hizo el cambio mínimo, no tocó aserciones y reportó de forma literal cuatro pruebas fallidas sin intentar arreglarlas. Lee solo lo indicado. Un solo bloque mecánico: confirmar el perfil en un bloque de interfaz real antes de relajar los prompts.
- **Caveman:** su README mide ~3 % de ahorro de salida (ruido). Ronda 2 comparada en `Auditorias/auditoria-2026-10-18.md`: la mejora visible vino de la memoria operativa, los prompts acotados y las pruebas estrictas; sin evidencia separable de caveman. El orquestador no lo usa (decisión del usuario).

## Pendientes conocidos (no asignados)

- **Pruebas GUI no válidas con escalado distinto de 100 %** (hallado en F0, 2026-11-12): el proceso de prueba no declara conciencia de DPI y los umbrales son píxeles fijos. A 125 % fallan `test_fe08_fe09_visual_integration`, `test_f1_roles_panel`, `test_f2_audit_log` y `test_f2b_subnav`; I1 e I2 pasan. Detalle en la auditoría de F0. Decisión del usuario: correr las pruebas a 100 % (a 1920x1080 y 100 % pasan las seis). Hacer las pruebas conscientes de DPI queda como bloque futuro, ligado al pendiente de I2 de DPI 125/150/200; comprobar el escalado antes de cada tanda de pruebas GUI.
- Pruebas GUI intermitentes (2026-11-12): `test_i1_integration` y `test_f2_audit_log` fallaron una vez por tiempo de espera tras el inicio de sesión y pasaron al repetir. Investigar cómo envían el inicio de sesión (foco o tiempos fijos) antes de fiarse de una sola pasada. Al auditar, repetir a solas antes de culpar a un bloque.

- Candidatos de backend para B3: build `build_hooks` con `-DCAFETERIA_TEST_HOOKS=ON` para el requerimiento 8 de I2; retención y limpieza de `backups/`; persistir `get_developer_server_config`; conciliación del corte de turno (I4, depende en parte de la sección 9).
- F1b menores: encabezado «Descripción» recortado, filas de la lista de roles sin altura táctil, texto arriba en campos de 48 px en Touch.

- Sección 9 de `docs/specs.md`: 10 decisiones de negocio abiertas (bloquean I3/I4).
- Datos de la PC de caja e impresora (bloquean el empaquetado y el cierre de I2). Sin instalador todavía.
- Requerimiento 8 de `test_i2_integration.py` solo corre con un build `-DCAFETERIA_TEST_HOOKS=ON` (no existe aún un `build_hooks`).
- Página de impresión fija de 200 mm en rollo térmico: revisar con hardware.
- Aviso de impresora ausente usa `wxOK|wxCANCEL` (botón «Aceptar», texto dice «Continuar»).
- B1: retención/limpieza de `backups/` sin implementar; recuperación tras fallo a mitad de copia sin probar; el actor de `BACKUP_RESTORED` puede no existir en la base restaurada.
- `get_developer_server_config` sigue en memoria.

## Reanudar agentes

- AntiGravity (actual, ronda 3): `agy --conversation=5cb24680-56c1-4cd1-9621-72c8ef342fbd`. Anteriores: `36c58871-cb3f-45c6-88ad-07be0a8992e5`, `c94b5e25-55b3-490a-b547-abad9fb453b1`, `4c6c3e44-1c0a-40a4-8124-59943158b872`.
- Claude Code Terminal (actual, ronda 3): `claude --resume CafeCH`, abierto en el worktree del backend con el modelo `opusplan`. Estas conversaciones solo existen en la PC original; en otra PC se abre una sesión nueva.
- QA manual aislado: `.\build_release\app_desktop.exe --data-dir="..\Cafeteria_QA_Manual"`.
