# Mensaje para Neon — cambio de papel a auditor

Decisión del usuario (2026-11-18): Neon audita; Claude redacta, asigna y lleva Git. Ver `docs/manual-orquestador.md`, sección 1.

```text
Cambio de papel (decision del usuario): desde ahora eres el AUDITOR del proyecto. Tu revision de B16 encontro cosas que el orquestador no vio, asi que te quedas con la revision de codigo y las auditorias. Claude (app de escritorio) se queda con la redaccion de prompts, la asignacion de trabajo, las respuestas a los planes del backend y Git. Ya no redactas prompts.

Relee docs\manual-orquestador.md secciones 1, 4 y 5 (cambiaron hoy, commit 1403a7e o posterior). En resumen:
- Auditas cada entrega: revisas el diff, corres la verificacion de la seccion 4 y escribes Auditorias\auditoria-AAAA-MM-DD-<bloque>.md con evidencia (comandos y codigo de salida, rutas de registros, archivo:linea) y veredicto: aceptado, aceptado con observaciones o no aceptado. Los hallazgos que no bloquean van al final como "Seguimientos" numerados, para que Claude los convierta en bloques.
- Backend: revisas la rama en <worktree del backend>; para la verificacion del conjunto pides a Claude (por medio del usuario) que integre la rama en main, y luego verificas en main.
- Frontend: auditas los cambios sin confirmar de main cuando Gemini termina.
- Sigues sin commits, merges ni push, y sin editar docs\ORQUESTADOR.md, prompts-para-pegar.md, codigo ni pruebas.
- Nunca verificas en main mientras Gemini edita main, ni a la vez que CTest u otra prueba GUI (tasklist antes de cada prueba).
- Las entregas de los ejecutores estaran en <raiz>\entregas\ (T14 no la tendra ahi: el usuario te la pegara).

Siguiente tarea: cuando el usuario te pegue la entrega de Gemini de T14 (prompt en Prompts\prompts-2026-11-18-T14.md), auditala y escribe Auditorias\auditoria-2026-11-18-T14.md. Hasta entonces, espera.

Responde solo "Entendido" y una linea con lo que haras al recibir T14.
```
