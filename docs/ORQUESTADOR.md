# Memoria del orquestador — Cafetería ITCH II

Solo para el orquestador. Los agentes trabajadores no necesitan este archivo: lo compartido está en `AGENTS.md` (Memoria operativa) y `docs/coordinacion-agentes.md`. Leerlo al inicio de cada sesión en lugar de reconstruir el contexto. Actualizarlo al cerrar cada ronda.

Última actualización: 2026-11-13.

## Portabilidad (decisión del usuario, 2026-11-12)

- Los archivos versionados no llevan rutas absolutas ni estado de una PC concreta, para que cualquier agente pueda retomar en cualquier PC siguiendo `docs/entorno.md`. Lo propio de cada PC (rutas reales, qué está instalado, qué se verificó ahí, sesiones de los agentes) va en `..\notas-pc.md`, fuera del repositorio; nunca se sube a GitHub. Leerlo al iniciar sesión si existe.
- Las rutas `D:\...` de auditorías y prompts con fecha son históricas; al enviar un prompt pendiente, sustituirlas por las de la PC en uso.
- Las conversaciones de los agentes no viajan con el repositorio: en una PC nueva los ejecutores empiezan sin contexto y los prompts deben ser autosuficientes.
- Markdown reorganizados (`7ea9d50`): `Auditorias/`, `Prompts/`, `docs/`. No hay `CLAUDE.md` en el repositorio; la memoria del orquestador es este archivo.
- Pendiente menor para Gemini: las pruebas `tests/*.py` anteponen al `PATH` rutas `D:\Temp\...` fijas. No estorban (CMake copia las DLL a la carpeta de compilación), pero conviene quitarlas.

## Estado

- **Puertas:** I1 cerrada (revalidada con el ejecutable real tras integrar B1). I2: software cerrado; falta QA humano (0/7 en `frontend/qa-visual.md`), impresora térmica 55/80, monitor táctil y DPI 125/150/200. I3–I7 abiertas. **Desde el 2026-11-13 I3 se desarrolla sin esperar el QA humano de I2 (decisión del usuario); el QA humano y el hardware siguen pendientes para cerrar I2.**
- **Pantallas reales:** Venta, Caja y turnos, Usuarios y cuentas, Configuración (impresión), Roles y permisos (F1/F1b), Bitácora (F2). El resto de las 16 subsecciones son marcadores de posición.
- **Backend real nuevo:** respaldo seguro y restauración (B1, migración 008, `backup.restore` solo Dueño) y reportes diarios (B2, `payments_total` y `payments_mismatch_cents`).
- **Estimación de release** (dada el 2026-10-19): piloto en 4–6 semanas y 1.0 en 8–12, si se resuelven la sección 9 de `docs/specs.md` y los datos de la PC de caja e impresora.

## Ronda en curso

Ronda 4 parcial, integrada el 2026-11-12 (`af4e4bc`): **B3b** (`c71d586`, utilidades `ui_helpers`) y **F2b** (`dba641a`, resaltado de subsección) aceptados; capturas 10 y 11 regeneradas. En cola, prompts en `Prompts/prompts-2026-11-12-ronda4.md`: **B4a** (Claude, títulos de permisos, migración 009; rama `backend/b4a` ya al día con `main`) -> **F5** (Gemini, interruptores; requiere B4a integrado) y **B4** (Claude, ajustes de datos, migraciones desde 010; tras B4a). Después F3 (UI Respaldos) y F4 (UI Reportes).

Actualización 2026-11-12 (PC nueva): **F0 aceptado** (Gemini 4 Hydrogen) y **B4a aceptado** en `1fc6dc5` e integrado en `main` tras F0. **F5 aceptado** (auditoría del 2026-11-12). Enviados en paralelo **F5** (Gemini 4 Hydrogen, `main`) y **B4** (Claude, rama `backend/b4`, con plan previo). Los prompts tal como se enviaron en esta ronda (B4a, F0, F5, B4) están en `Prompts/prompts-2026-11-12-ronda4-enviados.md`; a partir de ahora cada prompt enviado se registra en `Prompts/` con marcadores en lugar de rutas absolutas. Nota histórica del orden: Orden acordado con el usuario: Gemini hace **F0** (quitar rutas fijas de `tests/*.py` y correr las pruebas GUI) sobre el `main` actual; al aceptarlo se integra B4a y se envía **F5** de inmediato, porque la 009 rompe `test_f1_roles_panel.py` hasta que F5 lo reescriba. B4 parte de `main` con B4a integrado.

**Cierre de sesión 2026-11-13 (retomar aquí):** ronda 5 en curso. **B5** aceptado en `39ac304` e integrado en `a013186` (migraciones 011 y 012); conjunto verificado a 100 %: build sin advertencias, CTest 5/5 e I1 en Release y Debug. Subido a GitHub. **F6** correcto y **F6b** mejora la prueba de I2, pero I2 sigue fallando en el escritorio real: el diálogo moderno de impresión de Windows 11 se detecta y no se logra cancelar. Dato clave: AntiGravity ejecuta en un escritorio aislado donde aparece el diálogo clásico, así que sus resultados GUI no equivalen a los del escritorio real. Pasos siguientes: (1) decidir con el usuario cómo cerrar I2 (F6c para cancelar el diálogo moderno, o gancho de prueba); (2) correr la batería GUI completa a 100 % con B5 integrado (no se hizo); (3) pantallas de Inventario con Gemini, una por bloque: Ingredientes, Reabastecimiento, Productos y recetas, Existencias; quitar la subsección Barra. Avance en `README.md`: 47 % hacia 1.0 y 61 % hacia piloto.

Ensayo en curso (2026-11-13): **F7, pantalla Ingredientes, con Gemini 4 Hydrogen en esfuerzo XHigh** (sin Orchestrator), para medir tiempo, cuota y calidad frente a F5 (Hydrogen high). Tomar la cuota de AntiGravity antes y después. Para comparar con justicia: hacer F8 (Reabastecimiento, tamaño parecido) en high.

En paralelo con F7 (2026-11-13): **B6** para Claude (rama backend/b6, con plan previo): caja real en carrito y reintento, limpieza de respaldos y configuración del servidor persistida. Línea base de cuota de Gemini al empezar F7: 100 %. Comparación acordada: F7 en Hydrogen XHigh, F8 (Reabastecimiento) en Hydrogen high, F9 (Existencias) en Orchestrator (Argon); anotar cuota semanal y de 5 horas, hora, compilaciones y resultado de la auditoría.

- 2026-11-13: limpieza de respaldos locales (B6) solo bajo petición y con vista previa; por defecto conserva los últimos 30 respaldos de cada tipo y todo lo de los últimos 90 días (decisión del usuario; resuelve parte de la sección 9.10). Plan de B6 aprobado con ajustes.

**B6 auditado como aceptable (2026-11-13), sin commit ni integrar** (worktree `backend/b6`, migración 013). Integrarlo cuando F7 esté auditado, para no mover `main` mientras Gemini trabaja. La sesión de AntiGravity de F7 se cerró a mitad de la verificación (causa desconocida, la PC no se reinició); el trabajo quedó en disco y se reanudó con un prompt de continuación, así que la medición de cuota de XHigh es parcial.

**F7 aceptado en `6415c8a` y B6 en `ae8c464`, integrado en `409e974` y subido a GitHub** (conjunto verificado a 100 %: build, CTest 5/5, I1 y prueba de Ingredientes). Nota de F7: pantalla Ingredientes con Hydrogen XHigh; prueba propia en verde en el escritorio real. Perfil XHigh: resultado sólido y honesto, pero necesitó 6 ejecuciones de su prueba y 5 compilaciones por configuración. Falta la cuota final para la comparación. Siguiente: F8 (Reabastecimiento) en Hydrogen high.

**Ronda 6, I7 (2026-11-13, retomar aquí):** el usuario adelanta I7 (12 %) para dejarla a la par de las demás etapas (30 % como mínimo). Prompts en `Prompts/prompts-2026-11-13-ronda6-i7.md`, en paralelo: **B7** (Claude, rama `backend/b7` ya creada desde `6ebab28`, con plan previo: paquete en carpeta y ZIP, política de carpeta de datos, `--self-check`, versión, guion de instalador sin compilar y ficha de la PC de caja) y **T1** (Gemini 4 Hydrogen high, `main`: perfil en BackOffice > Configuración, diálogos de acceso en Táctil, prueba consciente de DPI e inventario de objetivos táctiles en `qa_visual/touch_audit.txt`). F8 (Reabastecimiento) espera a T1. En esta ronda `main_app.cpp` y `app.rc` son de B7; `packaging/`, `tools/` y `cmake/` pasan a ser del backend.

- Medido antes de redactar: el ejecutable importa 5 DLL ajenas a Windows (la carpeta de compilación tiene 33); paquete mínimo de 32 MB que arranca con el `PATH` limpio; subsistema 5 (el formato no impide Windows 7, sin probar); sin `install()`, CPack, icono ni recurso de versión; los datos caen en silencio a la carpeta del usuario de Windows si `data/` junto al ejecutable no se puede crear.
- **Plan de B7 aprobado con ajustes (2026-11-13).** Decisiones del usuario: datos de la copia instalada por equipo (`%ProgramData%\CafeteriaITCH\data`), copia portable marcada con `portable.flag` (la lleva el ZIP, no el instalador), ejecutable entregado `CafeteriaITCH.exe` (solo al instalar) e Inno Setup (su descarga aún sin aprobar). Ajustes del orquestador: ficha en PowerShell 2.0 con lanzador `.cmd` en lugar de VBScript; autocomprobación sin ninguna ventana y con código de salida probado; lista explícita de DLL de Windows y comprobación recursiva; ninguna prueba escribe en `%ProgramData%`; instalador solo de 64 bits. Pendiente del orquestador: conseguir los textos oficiales de las licencias de wxWidgets, del runtime de GCC y de winpthreads. Riesgo declarado: Windows 7 sin probar.
- **T1 aceptado (2026-11-13)**, auditoría en `Auditorias/auditoria-2026-10-18.md`: batería GUI completa (menos I2) en verde a 100 %. Pendiente: correr `test_t1_touch_profile.py` a 125 % o más (pedir al usuario cambiar el escalado, o hacerlo en la PC de caja). Hydrogen high: 14 minutos, informe literal, sin relajar aserciones. B7 sigue en curso en `backend/b7`; integrarlo sobre este `main`.
- Cola de I7: **T2** (corregir lo marcado en el inventario táctil y volverlo aserciones), **T3** (captura numérica en Táctil; falta decidir teclado propio o el de Windows), **B8** (compilar y probar el instalador). Fuera de los agentes: ejecutar la ficha en la PC de caja, icono, prueba en equipo limpio.
- Al auditar: T1 toca los diálogos de acceso, así que repetir I1 a 100 %; la prueba nueva de T1 debe correr también al escalado real de esta PC (comprobarlo antes: el registro indicaba 120 DPI). B7 cambia la resolución de la carpeta de datos: correr la batería GUI completa al integrar. El avance de I7 en `README.md` no se sube hasta aceptar los bloques.

## Decisiones del usuario

- 2026-11-08: el Dueño edita los permisos del Cajero; no puede renombrarlo ni eliminarlo. Dueño en solo lectura. Desarrollador nunca visible (registrado en `docs/specs.md` §9.5).
- 2026-11-08: Claude Code Terminal con `opusplan` como backend; AntiGravity como frontend; skill caveman en ambos (solo estilo).
- 2026-11-08: regla de dos pasadas. Un detalle de interfaz que Gemini no cierra en dos pasadas va a Claude Code Terminal (detalles en `docs/coordinacion-agentes.md`, protocolo punto 6). Primer caso: B3a, detalles del panel de roles tras F1 y F1b.
- El usuario quiere ver los movimientos de los agentes: no enviar prompts por `agy -p`; el usuario pega los prompts en terminales interactivas.

- 2026-11-12: los permisos de un rol se activan con interruptores (estilo ajustes de teléfono) en lugar de casillas, más grandes en Touch. Bloque **F5** para Gemini, prompt al final de `Prompts/prompts-2026-11-12-ronda4.md`; enviar tras aceptar F2b e integrar B3b (`c71d586`, pendiente de merge en `main`). Además cada permiso lleva título y descripción en lenguaje llano, separador entre permisos y agrupación por las cuatro secciones de la barra lateral (el Dueño es una persona mayor): bloque **B4a** (Claude, migración 009 con `title`, `section`, `sort_order`), que debe integrarse antes de F5. B4 usa migraciones desde 010 y va después de B4a.

- 2026-11-12: títulos y descripciones de los 20 permisos aprobados por el usuario (tabla en el prompt B4a).
- 2026-11-12: salió Gemini 4 Hydrogen en AntiGravity. Recomendación del orquestador: no cambiar a mitad de F2b; probarlo en F5 como ensayo y comparar contra el perfil de 3.8 Flash (exageración en informes, anchos a ojo, pruebas débiles). Decisión del usuario: seguir con 3.8 Flash; en su plan Pro los modelos Gemini 4 (Carbon, Argon, Hydrogen) aparecen como «Unavailable» en el selector. Revisar de nuevo si se habilitan.
- 2026-11-13: se pasa de largo la revisión humana de I2 para empezar Inventario (I3) ya. Decisiones de la sección 9 para I3: sin sobreventa (la venta se bloquea); el costo de cada entrada lo introduce un usuario con permiso; existencias y avisos para toda la cafetería, sin almacén ni barra; costo y margen estimados visibles solo para quien administra productos. Promedio ponderado para el margen estimado, confirmado; el usuario eligió el **promedio móvil sobre existencias** (el plan de B5 recomendaba el de todas las entradas). Plan de B5 aprobado con ajustes; anexo del promedio móvil aprobado el 2026-11-13: valuación en micro-centavos por ingrediente; el ajuste positivo entra al costo promedio vigente; con existencia 0 la receta usa el último costo conocido y lo indica; relleno aproximado de datos previos en la migración 012; existencias sin costo se valoran al precio de la primera compra. **B5 completo y auditado como aceptable (2026-11-13), sin commit ni integrar** (worktree `backend/b5`, migraciones 011 y 012). Pendiente antes de construir pantallas: integrar B5 y resolver F6b (prueba de I2 y diálogo de impresión). Ronda 5 enviada: **F6** (Gemini, modo Orchestrator (Argon): prueba de I2 con el folio nuevo y batería GUI completa) y **B5** (Claude, rama `backend/b5`, con plan previo: mermas y ajustes, movimientos, costo y margen, y dos pendientes de B4). Prompts en `Prompts/prompts-2026-11-13-ronda5.md`. Después: F7 Ingredientes, F8 Reabastecimiento, Productos y recetas, Existencias; quitar la subsección Barra.
- 2026-11-13: el frontend prueba el modo **Orchestrator (Argon)** de AntiGravity, que combina Gemini 4 Argon (planifica) y Gemini 4 Hydrogen (ejecuta), equivalente a `opusplan`. Primer bloque (F6, mecánico): respetó el archivo único y se detuvo solo ante el escalado a 125 %. Pero reportó `test_i2_integration` aprobada y el orquestador la vio fallar 3 de 3 en el diálogo de impresión (ver auditoría de F6): sin conciliar; tratar sus «pasó» como no verificados hasta repetirlos. Falta verlo en un bloque de interfaz real: mantener prompts estrictos, pedir plan previo en bloques grandes y comparar contra el perfil de Hydrogen solo (F0 y F5).
- 2026-11-12: plan de B4 aprobado. Folio nuevo `V-<código de caja>-<5 dígitos>` (por ejemplo `V-C1-00001`); los folios ya emitidos no cambian. Las columnas UTC de registros anteriores quedan en NULL (UTC desconocido, usar la hora local). Al integrar B4, `tests/test_i2_integration.py` fallará en la búsqueda de `V-10001`: bloque corto de Gemini después de F5.
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
- **AntiGravity (Gemini 4 Hydrogen high), primer bloque F0 (2026-11-12):** respetó la lista de archivos, hizo el cambio mínimo, no tocó aserciones y reportó de forma literal cuatro pruebas fallidas sin intentar arreglarlas. Lee solo lo indicado. Confirmado en F5 (bloque de interfaz real): control propio, panel y prueba reescrita sin relajar aserciones (36 sustituidas por 57), informe literal incluidos sus propios fallos intermedios. Hasta ahora no muestra los patrones de 3.8 Flash (exagerar, anchos a ojo, pruebas débiles). Seguir auditando igual; revisar siempre las aserciones eliminadas.
- **Caveman:** su README mide ~3 % de ahorro de salida (ruido). Ronda 2 comparada en `Auditorias/auditoria-2026-10-18.md`: la mejora visible vino de la memoria operativa, los prompts acotados y las pruebas estrictas; sin evidencia separable de caveman. El orquestador no lo usa (decisión del usuario).

## Pendientes conocidos (no asignados)

- **Pruebas GUI no válidas con escalado distinto de 100 %** (hallado en F0, 2026-11-12): el proceso de prueba no declara conciencia de DPI y los umbrales son píxeles fijos. A 125 % fallan `test_fe08_fe09_visual_integration`, `test_f1_roles_panel`, `test_f2_audit_log` y `test_f2b_subnav`; I1 e I2 pasan. Detalle en la auditoría de F0. Decisión del usuario: correr las pruebas a 100 % (a 1920x1080 y 100 % pasan las seis). Hacer las pruebas conscientes de DPI queda como bloque futuro, ligado al pendiente de I2 de DPI 125/150/200; comprobar el escalado antes de cada tanda de pruebas GUI.
- Pruebas GUI intermitentes (2026-11-12): `test_i1_integration` y `test_f2_audit_log` fallaron una vez por tiempo de espera tras el inicio de sesión y pasaron al repetir. Investigar cómo envían el inicio de sesión (foco o tiempos fijos) antes de fiarse de una sola pasada. Al auditar, repetir a solas antes de culpar a un bloque.

- Candidatos de backend para B3: build `build_hooks` con `-DCAFETERIA_TEST_HOOKS=ON` para el requerimiento 8 de I2; retención y limpieza de `backups/`; persistir `get_developer_server_config`; conciliación del corte de turno (I4, depende en parte de la sección 9).
- F5: el área visible de permisos es baja (menos de 2 filas en Touch a 1200x720); candidato a bloque corto. Márgenes y altos de fila sin `FromDIP`.
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
