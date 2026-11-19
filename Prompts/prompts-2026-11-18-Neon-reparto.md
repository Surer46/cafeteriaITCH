# Prompt para Neon — redactar B17 (solo backend)

Segunda tarea de la prueba de Neon (2026-11-18): comprobar que sus prompts son correctos. Por decisión del usuario, primero solo el backend (B17), en paralelo a T14; el plan de la ronda y T15 (I7) quedan para después de T14. El supervisor revisa el borrador antes de pegarlo.

```text
Tarea nueva de orquestador operativo: redacta el prompt del siguiente bloque de backend, B17, para Claude Code Terminal. Solo backend; el frontend (T14 con Gemini y despues I7) queda para otra tarea. El supervisor revisara tu borrador antes de que el usuario lo pegue (secciones 2 y 3 de docs\manual-orquestador.md).

Situacion al empezar (compruebala en el repositorio, no la des por buena):
- main en 5c0b1af o posterior. Gemini va a trabajar T14 en main (prompt en Prompts\prompts-2026-11-18-T14.md): B17 debe poder correr en paralelo sin tocar src\frontend ni tests\*.py.
- Claude Code Terminal esta libre; su worktree sigue en la rama backend/b16, ya integrada en main.
- Tu revision de B16 dejo dos seguimientos aprobados por el supervisor: create_product sin stock_status ni last_movement_at, y medir search_products con miles de filas en stock_movements.
- La seccion "Pendientes conocidos (no asignados)" de docs\ORQUESTADOR.md tiene entradas antiguas; varias ya se resolvieron en bloques posteriores.

Lo que tienes que entregar (borrador, sin commits):

1. En Prompts\prompts-2026-11-18-B17.md, antes del prompt, una tabla con cada entrada de "Pendientes conocidos" que sea de backend y tu veredicto: resuelta (con el commit o la auditoria que lo prueba), sigue abierta y entra en B17, sigue abierta pero no entra (y por que), o depende de una decision del usuario (con la pregunta numerada).

2. El prompt B17: los dos seguimientos de B16 y lo que tu tabla meta en B17. Rama backend/b17 creada desde main en el commit que verifiques. Decide si necesita plan previo segun el manual y dilo. Para cada punto: metodos y campos reales del contrato (citados con archivo:linea en la tabla o en una nota antes del prompt), que debe probar CTest y como, y que no debe tocar. Si un punto cambia el contrato, debe ser aditivo y no romper la interfaz actual.

Reglas:
- Version para pegar dentro del mismo archivo: bloque ```text con rutas reales y SOLO caracteres ASCII (comprueba que no haya ninguno fuera de ASCII). En la parte con marcadores usa <worktree del backend> y <toolchain>.
- El prompt pide que la entrega se guarde tambien en ..\entregas\entrega-AAAA-MM-DD-B17.md.
- Nada de umbrales, limites ni textos inventados sin decir de donde salen.
- Primera linea del archivo: "> Borrador de Neon, sin aprobar."
- No edites docs\ORQUESTADOR.md, prompts-para-pegar.md ni codigo. No compiles ni corras pruebas para esta tarea.

Al terminar, responde aqui corto: archivo escrito, resumen de B17 en cinco lineas, preguntas para el usuario y lo que no pudiste comprobar.
```
