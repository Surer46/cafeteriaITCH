# Manual del orquestador — Cafetería ITCH II

Vigente desde el 2026-11-18. Reúne las reglas de trabajo del orquestador que antes solo estaban en la memoria de Claude (app de escritorio). Lo lee cualquier modelo que haga de orquestador. El estado del proyecto, las decisiones y la cola de bloques están en `docs/ORQUESTADOR.md`; los roles y la propiedad de archivos, en `docs/coordinacion-agentes.md`.

## 1. Roles (decisión del usuario, 2026-11-19)

**Claude (app de escritorio) es el único orquestador:** redacta y asigna, responde los planes del backend, audita (sección 4) y lleva Git. Es más fácil de manejar con un solo responsable.

**Gemini 4 Neon (AntiGravity) es apoyo de diagnóstico**, solo cuando el usuario lo pide porque Claude tiene los tokens medidos: investigar un fallo concreto (una prueba que falla, un error de compilación, una sospecha en el código) y dejar un informe en `Auditorias/diagnostico-neon-AAAA-MM-DD-<tema>.md` con evidencia (`archivo:línea`, comandos y código de salida). No redacta prompts, no da veredictos de aceptación, no hace commits ni edita código, pruebas ni documentos de estado, y nunca corre pruebas a la vez que otro agente. Claude decide qué hacer con el diagnóstico.

Historial: del 2026-11-18 al 2026-11-19 Neon fue primero orquestador operativo en prueba y luego auditor (B16, T14, T14b). Lo hizo bien (encontró problemas que Claude no vio, entre ellos el uso de memoria liberada de `numeric_keypad.cpp`), pero el reparto entre dos orquestadores resultó más difícil de manejar; ver sección 9.

## 2. Archivos

- **Un archivo por bloque**, nunca añadido a otro de una ronda anterior:
  - prompt: `Prompts/prompts-AAAA-MM-DD-<bloque>.md` (la respuesta al plan va en el mismo archivo);
  - auditoría: `Auditorias/auditoria-AAAA-MM-DD-<bloque>.md`.
- Los bloques se numeran sin huecos: backend `B<n>`, frontend `T<n>` (`B16` y `T14` son los últimos al 2026-11-18). Una corrección del mismo bloque lleva letra (`T10b`).
- En el repositorio, rutas con marcadores: `<copia principal>`, `<worktree del backend>`, `<toolchain>` (ver `docs/entorno.md`). Nada propio de una PC va a GitHub; eso vive en `..\notas-pc.md`.
- **`..\prompts-para-pegar.md`** (fuera del repositorio): solo los prompts pendientes de pegar, numerados, con el destino en el título, rutas reales y **solo caracteres ASCII** (sin acentos, «», ≥ ni ▾: se pierden al pegar). Se quita cada prompt cuando ya se pegó.
- **Diagnósticos de Neon:** `Auditorias/diagnostico-neon-AAAA-MM-DD-<tema>.md`; Claude los confirma en el repositorio y los cita en la auditoría del bloque.
- **Entregas de los ejecutores:** además de su terminal, cada ejecutor guarda su entrega en `..\entregas\entrega-AAAA-MM-DD-<bloque>.md` (fuera del repositorio). Todo prompt nuevo lo pide en su última línea.

## 3. Cómo se redacta un prompt

Plantilla (ver `Prompts/prompts-2026-11-18-T14.md` y `-B15.md` como ejemplos):

1. Encabezado: rol («Eres el agente de frontend…» o «Bloque B<n> de backend…»), «lee primero AGENTS.md completo y docs/coordinacion-agentes.md», bloque y etapa, último commit aceptado, dónde trabaja (copia principal en `main` o worktree en `backend/<bloque>` creada desde un commit concreto), «No hagas commits» (frontend) y «No compiles ni corras pruebas antes de editar».
2. Frontend: «anota la hora de inicio»; reglas vigentes de `AGENTS.md` en una línea.
3. Backend: **plan corto primero** (contrato, migración, permisos, dudas numeradas) y esperar respuesta, salvo bloques pequeños con las reglas ya decididas en el prompt.
4. Puntos numerados con los **métodos y campos reales del contrato**. Antes de citar un nombre, comprobarlo en `include/app/contracts/`. Si algo no tiene método: «dilo y déjalo fuera», nunca simularlo.
5. Pruebas: prueba nueva con su nombre, casos concretos verificados en SQLite, «dos corridas seguidas», capturas numeradas siguiendo la última, y las pruebas existentes que puede tocar con «sin relajar nada».
6. Archivos que puede editar (lista cerrada) y los que no.
7. Verificación: comandos exactos, PATH del toolchain, escalado de la PC, «antes de cada prueba GUI comprueba con tasklist que no haya app_desktop.exe ni ctest.exe», «si un comando falla dos veces con el mismo error sin cambiar código, detente», «no instales nada», «no accedas a la red».
8. Entrega «corta y literal»: archivos, contrato usado y lo que quedó fuera, textos nuevos exactos, pruebas cambiadas y por qué, comandos con código de salida, DPI, fallos intermedios, horas y número de compilaciones y corridas, límites, y «no escribas que algo funciona si no lo ejecutaste».

**Lecciones de la prueba de Neon (2026-11-18), para quien redacte:**
- Partir los bloques por riesgo: lo trivial y sin plan no espera a lo delicado que necesita plan (B17 juntó ambos).
- No pedir aserciones frágiles en CTest, como el texto de `EXPLAIN QUERY PLAN`, que cambia entre versiones de SQLite: medir tiempo y número de consultas, y dejar el plan de consulta como diagnóstico en la entrega.
- En un bloque con plan previo, proponer soluciones solo como ejemplo; decide la respuesta al plan.
- No narrar al ejecutor el código que puede leer él mismo; con Hydrogen, prompts cortos y con límites cerrados.
- Las preguntas al usuario van ordenadas por lo que bloquean (piloto antes que versiones posteriores).

Las decisiones de negocio no se inventan: si falta una, se pregunta al usuario y se anota en `docs/specs.md` (sección 9) y en `docs/ORQUESTADOR.md`.

## 4. Cómo se audita

1. `git status` y `git diff --ignore-cr-at-eol --stat` desde el último commit aceptado: ¿solo archivos del dueño del bloque?
2. Leer el código clave: la pantalla no calcula importes ni permisos; nada de `double` con dinero; permisos comprobados dentro del servicio y dentro de la transacción; ganchos de prueba fuera de producción; anchos fijados a ojo.
3. Pruebas nuevas: ¿miden lo que afirman? ¿Se relajó o quitó alguna aserción? Comparar con `git diff` de los `.py` y `.cpp`.
4. Verificación del conjunto en `main` (tras integrar):
   - `cmake --build` de `build_release`, `build_debug` y `build_hooks_release`, contando advertencias;
   - `ctest` en Release y Debug, con `Get-MpThreatDetection` contado antes y después, y comprobando que `toolchain\mingw64\bin\ctest.exe` sigue presente;
   - batería GUI completa, **una prueba a la vez**, nunca mientras corra CTest o una prueba de otro agente (`tasklist` antes de cada una). Lista actual: `test_t13_promotions_combos`, `test_t12_discounts_stock`, `test_t11_products_recipes`, `test_i2_integration`, `test_i1_integration`, `test_t10_restock_bar`, `test_t1_touch_profile`, `test_f7_ingredients_panel`, `test_f1_roles_panel`, `test_f2b_subnav`, `test_t7_updates`, `test_f2_audit_log`, `test_fe08_fe09_visual_integration`, `test_t2_touch_targets`, `test_t4_numeric_keypad`, `test_t5_fullscreen`, `test_t6_backups` (más la prueba nueva del bloque, dos veces);
   - escalado: la PC está al 125 % (120 DPI); la pasada al 100 % se hace cuando el usuario cambia el escalado (nunca cambiarlo).
5. Si algo falla: repetirlo solo antes de culpar al bloque. Si pasa solo, buscar la causa en el código de la prueba (por ejemplo, leer SQLite sin esperar tras un diálogo) y clasificarlo como intermitente o como carrera de la prueba; la corrección de una carrera es una espera por sondeo con la misma aserción.
6. Capturas: medir en píxeles con PIL, no a ojo.
7. Auditoría escrita con **evidencia**: comandos y código de salida, ruta de los registros y `archivo:línea` de lo revisado; veredicto (aceptado, aceptado con observaciones o no aceptado), cambios no previstos en el plan y pendientes.

## 5. Integración y Git

- Solo Claude (orquestador) hace commits, merges y subidas; Neon y el frontend no hacen commits, y el backend solo en su rama.
- Backend: commit del ejecutor en `backend/<bloque>`; `git merge --no-ff backend/<bloque>` en `main` con mensaje «Merge branch 'backend/<bloque>' (…)»; verificar el conjunto.
- Frontend: un commit del orquestador por bloque aceptado.
- No integrar en `main` mientras Gemini edita `main`.
- Subir a GitHub **solo cuando el usuario lo pide**, agrupando commits; su regla es como máximo dos veces al día.
- `git stash` está compartido con el worktree: no usar `git stash` sin etiqueta.

## 6. Seguridad y operación (decisiones del usuario)

- No modificar el antivirus ni añadir exclusiones. Las pruebas no escriben `.exe` (B13c: extensión `.bin` y contenido de cabecera más ceros).
- Toda descarga necesita aprobación explícita del usuario; preguntar antes de instalar algo con UAC y antes de borrar `%ProgramData%\CafeteriaITCH`.
- La clave privada de firma de actualizaciones nunca va en el repositorio ni en la compilación; las versiones las publica el usuario.
- El usuario pega los prompts a mano en las terminales (no usar `agy -p`) y cambia el escalado de Windows él mismo.
- No correr pruebas GUI a la vez que CTest: `EndSessionTest` también abre el programa.

## 7. Comunicación con el usuario

Español llano. Resultado primero, luego lo necesario para decidir. Sin afirmar nada que no se haya ejecutado o leído. Avisar de lo que queda sin subir al cerrar el día.

## 8. Agentes y su perfil

- **Claude Code Terminal** (backend): fiable en contratos, transacciones y pruebas C++; hacer plan primero en bloques grandes.
- **Gemini 4 Hydrogen** (frontend): bueno cableando paneles sobre contratos; necesita límites de archivos y pruebas muy precisos; tiende a corregir sus pruebas en lugar del código (vigilar aserciones).

## 9. Resultado de la prueba de Neon (2026-11-18)

- **Calibración con B16 (auditoría a ciegas):** todas sus afirmaciones fueron correctas y encontró seis observaciones que el supervisor no vio (`Auditorias/revision-neon-2026-11-18-B16.md`).
- **Redacción de B17:** precisa (citas exactas, pendientes cerrados con evidencia), con dos correcciones menores y las lecciones de la sección 3.
- **Auditor (T14 en High, T14b en Medium):** igual de minucioso en los dos esfuerzos; llamó «intermitente» a una prueba que falló 4 de 6 veces.
- **Decisión final del usuario (2026-11-19):** Claude, único orquestador; Neon, solo diagnósticos a pedido (sección 1).
