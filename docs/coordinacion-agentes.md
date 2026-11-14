# Coordinación de agentes — Cafetería ITCH II

Vigente desde el 2026-11-08. Complementa `AGENTS.md`; si hay conflicto, manda `AGENTS.md`.

## Roles

| Agente | Modelo | Fortalezas observadas | Debilidades observadas | Papel |
|---|---|---|---|---|
| **Orquestador** (Claude, app de escritorio) | Fable 5.2 (antes Opus 5.5) | Auditoría independiente, compilación desde cero, lectura de capturas con medición en píxeles | Caro si implementa; no debe escribir código de producción | Redacta prompts, vigila terminales, audita, mantiene `docs/specs.md`, planes, tareas y auditorías |
| **Claude Code Terminal** | `opusplan`: Opus 5.5 planifica, Sonnet 5.5 ejecuta | Razonamiento de dominio, transacciones SQLite, aritmética racional de costos, permisos en servicios, diseño de pruebas C++ | Más caro por token que Gemini | **Dueño del backend**: contratos, dominio, almacenamiento, migraciones, servicios y CTest |
| **AntiGravity** | Desde 2026-11-13: modo Orchestrator (Argon), Gemini 4 Argon planifica y Gemini 4 Hydrogen ejecuta. Antes Gemini 4 Hydrogen (F0, F5) y Gemini 3.8 Flash (high); las observaciones de esta fila son de 3.8 Flash | Rápido y barato; buen cableado de paneles wxWidgets sobre contratos existentes; scripts Win32/Python de prueba GUI y capturas | Exagera resultados; tiende a debilitar pruebas; bloques de razonamiento de 7–9 min; demasiadas lecturas | **Dueño del frontend**: paneles, diálogos, tema, pruebas GUI en Python y capturas |

## Propiedad de archivos (dos escritores nunca tocan lo mismo)

| Área | Dueño | Archivos |
|---|---|---|
| Contratos | Claude Code Terminal | `include/app/contracts/` |
| Dominio, almacenamiento, servicios | Claude Code Terminal | `src/domain/`, `src/storage/`, `src/services/`, `include/app/services/`, `migrations/` |
| Pruebas C++ | Claude Code Terminal | `tests/test_*.cpp` |
| Interfaz | AntiGravity | `src/frontend/`, `assets/` |
| Pruebas GUI y capturas | AntiGravity | `tests/*.py`, `qa_visual/` |
| Empaquetado y entrega | Claude Code Terminal | `packaging/`, `tools/`, `cmake/`. Durante la ronda 6 (B7) también `src/frontend/main_app.cpp` (carpeta de datos y línea de comandos) y `src/frontend/app.rc` (recurso de versión) |
| `CMakeLists.txt` | Claude Code Terminal | Única excepción: AntiGravity puede añadir sus propios archivos nuevos de `src/frontend/` a la lista de fuentes de `app_desktop`, y nada más |
| Documentación de estado | Orquestador | `docs/specs.md`, `*/plan.md`, `*/task.md`, `auditoria-*.md`, `docs/coordinacion-agentes.md` |

La interfaz consume contratos; nunca los cambia. Si AntiGravity necesita un método nuevo, lo pide en su entrega y el orquestador lo encarga a Claude Code Terminal.

## Compilación y pruebas sin choques

- Cada agente trabaja en su propia copia: Claude Code Terminal en el worktree hermano `..\Cafeteria_ITCH_backend` (rama `backend/<bloque>`); AntiGravity en la copia principal (rama `main`). La disposición de carpetas está en `docs/entorno.md`. Así un archivo a medias de uno no rompe la compilación del otro. El orquestador integra la rama del backend en `main` al aceptar cada bloque.

- Claude Code Terminal compila en `build_be_debug` y `build_be_release` y solo ejecuta CTest.
- AntiGravity compila en `build_debug` y `build_release` y es el único que ejecuta pruebas GUI (`tests/*.py`), porque abren ventanas y toman el foco.
- Nunca dos pruebas GUI a la vez. Las bases de prueba siempre en directorios temporales aislados.
- El orquestador repite compilación, CTest y la prueba GUI pertinente antes de aceptar un bloque.

## Protocolo de trabajo

1. El orquestador escribe un prompt pequeño en `prompts-<agente>-AAAA-MM-DD.md`, sin «», ≥ ni ▾ (se pierden al pegar).
2. Claude Code Terminal recibe trabajo de backend en modo plan: el plan de Opus se revisa antes de que Sonnet ejecute.
3. Cada entrega lista archivos, comandos con código de salida, resultados y límites.
4. El orquestador audita (con `git diff` desde el último commit aceptado) y registra el resultado. Nada se marca `[x]` sin auditoría.
5. Los agentes **no hacen commits**. El orquestador hace un commit por bloque aceptado, así cada auditoría parte de una línea base limpia.
6. **Regla de dos pasadas (decisión del usuario, 2026-11-08):** si AntiGravity no cierra un detalle de interfaz tras dos pasadas sobre el mismo tema, el detalle se asigna a Claude Code Terminal, sin una tercera pasada de Gemini.
   - Claude lo trabaja en su worktree. Puede editar los archivos del frontend afectados, nunca otros, y solo si ningún bloque de Gemini en curso toca esos mismos archivos.
   - Claude compila y corre CTest, pero no ejecuta pruebas GUI. El orquestador las corre al integrar y le devuelve los fallos.
   - El script de capturas lo sigue manteniendo Gemini; el orquestador regenera la captura afectada al integrar.

## Skill caveman

Instalada en `~/.agents/skills/caveman` (enlazada en `~/.claude/skills/caveman`). Solo cambia el estilo de las respuestas en el chat; no ejecuta código. La propia skill excluye código, comentarios, documentos y memoria, así que las entregas y auditorías siguen en prosa normal.

Su README mide un ahorro de solo ~3 % de tokens de salida frente a la instrucción simple "responde conciso", dentro del ruido. El ahorro real viene de:

- prompts pequeños con archivos y criterios concretos (menos lecturas exploratorias);
- `opusplan`, que usa Sonnet para ejecutar;
- no repetir compilaciones ni ejecuciones sin cambiar código;
- esfuerzo `medium` en AntiGravity para tareas mecánicas de interfaz.

## Puertas y bloqueos vigentes

- **I2:** software cerrado; falta QA humano (0/7), impresora térmica, monitor táctil y DPI. Desde el 2026-11-13, por decisión del usuario, **I3 se desarrolla sin esperar el QA humano**; el QA humano y el hardware siguen pendientes para cerrar I2.
- **Sección 9 de `docs/specs.md`:** 11 decisiones de negocio abiertas; bloquean reglas de I3 (sobreventa, valuación de costos) e I4 (anulaciones, quién cierra caja).
- **PC de caja e impresora:** sin datos; bloquean empaquetado y cierre de I2.

## Primer reparto (trabajo que no depende de I3 ni de la sección 9)

| Bloque | Agente | Alcance | Depende de |
|---|---|---|---|
| B1 | Claude Code Terminal | I5: contrato y servicio de **restauración** desde respaldo local (verificar integridad, respaldo previo automático, transacción, auditoría, permiso solo Dueño) con CTest | Nada |
| B2 | Claude Code Terminal | `SqliteReportService` real para el resumen diario (sustituye el `nullptr` actual), en centavos, con CTest | Nada |
| F1 | AntiGravity | I6: panel **BackOffice > Roles y permisos** sobre `list_roles`, `create_role`, `update_role`, `delete_role` y `list_permissions` existentes, con prueba GUI | Nada |
| F2 | AntiGravity | Panel **Caja y Reportes > Bitácora** sobre `list_audit_events` existente, con filtros y prueba GUI | Nada |
| F3 | AntiGravity | Panel **BackOffice > Respaldos** (ejecutar, estado, restaurar) | B1 aceptado |
| F4 | AntiGravity | Panel **Caja y Reportes > Reportes** (resumen diario) | B2 aceptado |

Orden sugerido: B1 y F1 en paralelo; luego B2 y F2; después F3 y F4.

Tras el QA humano y las decisiones de la sección 9: I3 con Claude Code Terminal primero (dominio de inventario, costos racionales y recetas) y AntiGravity después (Existencias, Ingredientes, Productos y recetas, Reabastecimiento).
