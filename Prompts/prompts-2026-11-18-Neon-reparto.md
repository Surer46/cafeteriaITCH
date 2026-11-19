# Prompt para Neon — reparto de la ronda (plan, B17 y T15)

Segunda tarea de la prueba de Neon (2026-11-18): comprobar que sus prompts son correctos. El supervisor revisa el plan y los dos borradores antes de pegarlos.

```text
Tarea nueva de orquestador operativo: reparte el trabajo de la siguiente ronda entre los dos ejecutores y redacta sus prompts. El supervisor revisara cada borrador antes de que el usuario lo pegue (sigue la seccion 2 y 3 de docs\manual-orquestador.md).

Situacion al empezar (compruebala en el repositorio, no la des por buena):
- main en 9ea91c7 o posterior. T14 (corte con arqueo, historial y Reportes) ya esta redactado en Prompts\prompts-2026-11-18-T14.md y es lo proximo de Gemini; no lo reescribas.
- Claude Code Terminal esta libre; su worktree sigue en la rama backend/b16, ya integrada en main.
- Tu revision de B16 dejo dos seguimientos aprobados por el supervisor: create_product sin stock_status ni last_movement_at, y medir search_products con miles de filas en stock_movements.
- La seccion "Pendientes conocidos (no asignados)" de docs\ORQUESTADOR.md tiene entradas antiguas: varias ya se resolvieron en bloques posteriores. Verifica cada una contra el codigo y las auditorias antes de usarla.

Lo que tienes que entregar (todo como borrador, sin commits):

1. Plan de la ronda en Prompts\plan-ronda-2026-11-18-neon.md: que hace cada agente, en que orden, que corre en paralelo y por que no chocan (archivos, compilacion en carpetas distintas, pruebas GUI nunca a la vez que CTest). Incluye la tabla de pendientes conocidos con tu veredicto por fila: resuelto (con commit o auditoria que lo prueba), sigue abierto (y a que bloque lo asignas) o es del usuario. Lista al final las preguntas para el usuario que bloqueen algo, numeradas.

2. Prompt B17 para Claude Code Terminal (backend), en Prompts\prompts-2026-11-18-B17.md: los dos seguimientos de B16 y lo que tu veredicto del punto 1 asigne al backend, siempre que no dependa de una decision pendiente del usuario. Rama backend/b17 creada desde main en el commit que verifiques. Decide si necesita plan previo segun el manual y dilo. Debe poder correr en paralelo con T14 sin tocar src\frontend ni tests\*.py.

3. Prompt T15 para Gemini (frontend, etapa I7), en Prompts\prompts-2026-11-18-T15.md, para despues de T14: redisenar el modo pantalla completa para aprovechar el 100 % de la pantalla sin espacios vacios, mas alto de la lista de permisos de Roles y permisos (I6) y las notas de espacio de Auditorias\auditoria-2026-11-16-T11.md. Antes de redactarlo, lee los paneles reales de src\frontend que afecta y las pruebas de pantalla completa y tactiles (test_t5_fullscreen.py, test_t2_touch_targets.py, tests\gui_dpi.py) y di que mediras para probar que "no hay espacios vacios" (criterio medible, no a ojo). Si el trabajo es demasiado grande para un solo bloque de Gemini, partelo (T15, T16...) y explica el corte. Las decisiones de diseno que sean del usuario van como preguntas en el plan del punto 1, no inventadas.

Reglas para los tres archivos:
- Cada prompt con su version para pegar dentro del mismo archivo: bloque ```text con rutas reales y SOLO caracteres ASCII (comprueba que no haya ninguno fuera de ASCII). En la parte con marcadores, usa <copia principal>, <worktree del backend> y <toolchain>.
- Cada prompt pide que la entrega se guarde tambien en ..\entregas\entrega-AAAA-MM-DD-<bloque>.md.
- Solo metodos y campos que existan en include\app\contracts\ (citalos con archivo:linea en el plan); nada de anchos, umbrales ni textos inventados sin decir de donde salen.
- Primera linea de cada archivo: "> Borrador de Neon, sin aprobar."
- No edites docs\ORQUESTADOR.md, prompts-para-pegar.md ni codigo. No compiles ni corras pruebas para esta tarea.

Al terminar, responde aqui corto: archivos escritos, resumen del reparto en cinco lineas, preguntas para el usuario y lo que no pudiste comprobar.
```
