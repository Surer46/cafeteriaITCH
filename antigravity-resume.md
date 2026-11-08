# Reanudar conversación de AntiGravity

## Conversación actual — salida confirmada el 2026-10-19

La CLI salió y volvió a PowerShell mostrando:

```powershell
Set-Location -LiteralPath 'D:\Cafetería_ITCH'
agy --conversation=36c58871-cb3f-45c6-88ad-07be0a8992e5
```

También sirve `agy -c` si es la conversación más reciente. Modelo en uso: Gemini 3.8 Flash (high), modo `accept-edits`. Comenzó con Claude Sonnet 4.6 Thinking; Gemini continuó cuando se agotó la cuota de Claude.

## Estado para retomar (2026-10-19)

- No hay entrega pendiente de auditar. Los prompts A, B y C de `prompts-antigravity-2026-10-18.md` se enviaron y se auditaron; resultados en `auditoria-2026-10-18.md`.
- Último estado verificado de forma independiente: compilación Debug/Release con código 0 y 0 advertencias; CTest 3/3 en ambos perfiles; `test_fe08_fe09_visual_integration.py` aprobado en Release y Debug.
- Los pendientes de software de la auditoría del 2026-10-18 quedan cerrados.
- **I2 sigue parcial** solo por: impresora térmica física 55/80 mm, monitor táctil real, DPI 125/150/200 y aceptación humana (0/7 en `frontend/qa-visual.md`). **No avanzar a I3** hasta el QA humano.

## Coordinación con dos agentes (desde 2026-11-08)

Se suma Claude Code Terminal (`opusplan`) como dueño del backend; AntiGravity queda como dueño del frontend. Reparto, propiedad de archivos, directorios de compilación y primeros bloques en `coordinacion-agentes.md`. El 2026-11-08 AntiGravity se abrió con `agy` en una conversación nueva; la conversación 36c58871… sigue disponible para consultar el historial.

## Próximo paso al volver

1. QA humano de los 7 casos de `frontend/qa-visual.md` con base aislada:
   ```powershell
   New-Item -ItemType Directory -Force -Path "D:\Cafeteria_QA_Manual"
   .\build_release\app_desktop.exe --data-dir="D:\Cafeteria_QA_Manual"
   ```
2. Resolver las decisiones de negocio de la sección 9 de `specs.md`; bloquean I3 (inventario) e I4 (control).
3. Conseguir los datos de la PC de caja (sistema operativo, RAM, disco, resolución) y el modelo de impresora; bloquean el empaquetado y el cierre de I2.
4. Solo después, preparar prompts acotados para I3, uno a la vez, y auditarlos igual que A–C.

## Notas para quien orqueste

- Al pegar prompts en la terminal de AntiGravity se pierden «», ≥ y ▾: escribir los prompts sin esos caracteres.
- Medir alturas y anchos en píxeles o con la prueba geométrica, no a ojo (ver la corrección del auditor en la revisión del prompt C).
- Patrones de Gemini observados: debilitar pruebas para que pasen, resumir con optimismo y repetir ejecuciones sin editar. Con prompts pequeños y aserciones estrictas se comportó bien (prompts A y C).

## Conversaciones históricas

Anterior, cerrada el 2026-10-03:

```powershell
agy --conversation=c94b5e25-55b3-490a-b547-abad9fb453b1
```

Más antigua:

```powershell
agy --conversation=4c6c3e44-1c0a-40a4-8124-59943158b872
```
