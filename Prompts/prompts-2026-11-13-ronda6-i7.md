# Prompts del 2026-11-13 — ronda 6 (I7: perfil táctil, instalador y entrega)

Las rutas absolutas de la PC en uso se sustituyen por marcadores (`<copia principal>`, `<worktree del backend>`, `<toolchain>`); ver `docs/entorno.md`. Al enviar un prompt se escriben las rutas reales.

Decisión del usuario del 2026-11-13: I7 es la etapa más atrasada (12 %) y se adelanta para dejarla a la par de las demás (30 % como mínimo). F8 (Reabastecimiento) espera a que termine T1.

Hechos medidos por el orquestador antes de redactar (commit `6ebab28`, `build_release`):

- `app_desktop.exe` importa solo 5 DLL que no son del sistema: `libgcc_s_seh-1.dll`, `libstdc++-6.dll`, `libwinpthread-1.dll`, `wxbase333u_gcc1610_x64.dll` y `wxmsw333u_core_gcc1610_x64.dll`. La carpeta de compilación contiene 33 (todas las de wxWidgets, incluidas las de depuración).
- Una carpeta con el ejecutable, esas 5 DLL, `migrations/` y `assets/` pesa 32 MB, arranca con un `PATH` que solo tiene las carpetas de Windows y crea su base con `--data-dir`.
- El ejecutable y las DLL declaran versión de subsistema 5: el formato del archivo no impide Windows 7. No está probado en ningún Windows distinto del de desarrollo.
- No hay reglas `install()` ni CPack en `CMakeLists.txt`; `app.rc` no tiene icono ni recurso de versión; no hay Inno Setup, NSIS ni WiX instalados.
- Sin `--data-dir` ni `CAFETERIA_DATA_DIR`, los datos van a `data/` junto al ejecutable y, si esa carpeta no se puede crear, pasan sin avisar a la carpeta de datos del usuario de Windows.
- El perfil Táctil solo se cambia con el botón del encabezado; BackOffice > Configuración muestra únicamente la impresión. Los diálogos de inicio de sesión y de alta del Dueño tienen tamaño fijo (400x320 y 460x460) y no leen el perfil.

B7 y T1 corren en paralelo. Archivos compartidos: solo `CMakeLists.txt`, en zonas distintas (T1 añade dos fuentes a `app_desktop`; B7 añade reglas de instalación). En esta ronda `src/frontend/main_app.cpp` y `src/frontend/app.rc` son de B7.

---

## B7 — Claude Code Terminal (`backend/b7`, desde `main` con B6)

Paquete portable, política de datos, autocomprobación, versión, guion de instalador y ficha de la PC de caja. Con plan previo.

```text
Bloque B7 de backend. Si esta es una sesion nueva, lee primero AGENTS.md completo y docs/coordinacion-agentes.md. Trabaja SOLO en <worktree del backend>, rama backend/b7 (ya creada desde main, incluye B6 y la migracion 013). No hagas commits. No compiles ni corras pruebas antes de editar: la linea base esta verificada (CTest 5/5 en build_be_release y build_be_debug).

Objetivo: que la aplicacion se pueda entregar. Hoy no existe paquete ni instalador: el ejecutable solo corre desde la carpeta de compilacion. La PC de caja es modesta (i5 de segunda generacion, 4 a 6 GB de RAM) y su sistema operativo NO esta confirmado: puede ser Windows 7, 8.1, 10 u 11. No asumas ninguno.

El otro agente trabaja en main en el perfil tactil: edita src/frontend/ (menos main_app.cpp y app.rc), la lista de fuentes de app_desktop en CMakeLists.txt y tests/*.py. No toques nada de eso.

Archivos permitidos, y ningun otro:
- CMakeLists.txt (reglas de instalacion, CPack, version; no la lista de fuentes de app_desktop) y tests/CMakeLists.txt;
- nuevos bajo cmake/, packaging/ y tools/;
- src/frontend/main_app.cpp (solo resolucion de la carpeta de datos y opciones de linea de comandos) y src/frontend/app.rc (solo el recurso de version);
- include/app/ y src/services/ o src/storage/ para una funcion pura nueva, si la necesitas;
- tests/test_*.cpp nuevos.
No toques otros archivos de src/frontend/, tests/*.py, migrations/, docs/ ni README.md.

Hechos ya medidos, no los vuelvas a investigar:
- app_desktop.exe (Release) importa solo estas DLL que no son de Windows: libgcc_s_seh-1.dll, libstdc++-6.dll, libwinpthread-1.dll, wxbase333u_gcc1610_x64.dll, wxmsw333u_core_gcc1610_x64.dll. CMake copia hoy 33 DLL a la carpeta de compilacion (todo wxWidgets, incluidas las ud de depuracion).
- Una carpeta con el exe, esas 5 DLL, migrations/ y assets/ pesa 32 MB y arranca con un PATH que solo contiene las carpetas de Windows.
- main_app.cpp resuelve los datos asi: --data-dir; si no, CAFETERIA_DATA_DIR; si no, data/ junto al exe (o en la raiz del repo si el exe esta en build_release o build_debug); y si esa carpeta no se puede crear, cae SIN AVISAR a la carpeta de datos del usuario de Windows.
- No hay Inno Setup, NSIS ni WiX en esta PC. No instales nada.

Presenta primero un plan para revision y espera aprobacion antes de escribir codigo. El plan debe resolver los puntos marcados como DECIDIR, con una recomendacion en cada uno.

1. Paquete en carpeta. Reglas de instalacion de CMake que dejen en un prefijo cualquiera una carpeta lista para copiar a otra PC: el ejecutable, solo las DLL que necesita, migrations/, assets/ y los avisos de licencias de terceros.
   - Nada de DLL de depuracion, ejecutables de prueba, bases de datos, respaldos, archivos .cfg ni mocks de datos.
   - La lista de DLL no se mantiene a ojo: el empaquetado debe fallar si el ejecutable importa una DLL que no es de Windows y no esta en el paquete.
   - El empaquetado debe negarse si CAFETERIA_TEST_HOOKS esta activado.
   - DECIDIR: si el paquete se genera solo desde Release y que pasa al pedirlo desde Debug.
   - Licencias: wxWidgets, SQLite, el runtime de GCC y winpthreads, Roboto y los iconos. Copia los textos que ya existan en el repo o en el toolchain; no redactes ni inventes textos de licencia. Lo que no encuentres, listalo como pendiente en la entrega.

2. Paquete ZIP con CPack (generador ZIP, que no necesita herramientas externas), con la version en el nombre.

3. Carpeta de datos. La regla actual es peligrosa una vez instalado: si la carpeta junto al exe no se puede escribir, la aplicacion abre una base vacia en otro sitio y parece que se perdieron las ventas.
   - Con --data-dir o CAFETERIA_DATA_DIR el comportamiento no cambia. Desde build_release y build_debug tampoco: las pruebas GUI existentes dependen de eso y no se tocan.
   - Nunca se cambia de carpeta en silencio. Si la carpeta elegida no se puede crear o escribir, mensaje claro en lenguaje llano con la ruta, y la aplicacion no arranca.
   - DECIDIR: donde viven los datos de una copia instalada (por equipo o por usuario de Windows) y como se distingue una copia portable, con datos junto al exe, de una instalada. El orquestador confirmara la respuesta con el usuario.
   - DECIDIR: que pasa si la carpeta elegida no tiene base pero la ubicacion anterior si la tiene. No se crea una base vacia encima de datos existentes sin que el usuario lo sepa.
   - Saca la decision a una funcion pura, sin wxWidgets, que reciba sus entradas (argumento, variable de entorno, ruta del exe, rutas candidatas, que existe y que se puede escribir) y devuelva la carpeta o el error. main_app.cpp solo la llama. Asi se prueba en CTest.

4. Autocomprobacion del paquete. Una opcion de linea de comandos, por ejemplo --self-check, que sin abrir ninguna ventana ni pedir cuenta: crea una carpeta temporal propia, abre SQLite, aplica todas las migraciones desde la carpeta del paquete, comprueba que estan las fuentes y los iconos, borra la carpeta temporal y termina con codigo 0, o distinto de 0 con el motivo.
   - El ejecutable es de subsistema grafico y no tiene consola: el resultado se escribe tambien en un archivo que se indica por argumento.
   - Nunca toca la carpeta de datos real, nunca crea cuentas, no usa red.
   - Sirve para verificar el paquete aqui y, sobre todo, en la PC de caja antes de instalar.

5. Version. Una sola fuente: la de project() en CMakeLists.txt. Debe llegar al recurso de version del ejecutable (lo que muestra Windows en Propiedades) y al informe de la autocomprobacion. No agregues un icono: no hay uno aprobado; dejalo anotado como pendiente.
   - DECIDIR: nombre del ejecutable en el paquete. Renombrar app_desktop.exe dentro del arbol de compilacion rompe todas las pruebas GUI; si propones otro nombre, que sea solo al instalar, y explica si eso cambia alguna ruta de datos.

6. Guion de instalador para Windows, bajo packaging/. Que instale la carpeta del punto 1, cree accesos directos, y que al actualizar o desinstalar NUNCA borre ni mueva los datos.
   - DECIDIR y recomendar la herramienta (Inno Setup, NSIS, WiX u otra), mirando sobre todo que funcione en Windows 7 y no exija instalar nada mas en la caja.
   - Como la herramienta no esta instalada, el guion queda escrito y marcado como NO VERIFICADO. No lo presentes como probado. Indica el comando exacto con el que se compilara cuando el usuario apruebe instalarla.
   - Sin servicios, sin tareas programadas, sin red, sin cuentas ni contrasenas.

7. Ficha de la PC de caja. Bajo tools/, un guion que el usuario lleva en una USB, ejecuta con doble clic en la PC de caja y deja un archivo de texto junto al guion con: version y edicion de Windows, 32 o 64 bits, procesador, RAM total, discos (tamano, espacio libre y si es SSD o disco duro cuando se pueda saber), resolucion y escalado de cada pantalla, si hay pantalla tactil, e impresoras instaladas (nombre, controlador, puerto, cual es la predeterminada).
   - Solo lee: no instala, no cambia configuracion, no usa red, no pide permisos de administrador.
   - Debe funcionar en un Windows 7 sin actualizar: PowerShell 2.0, sin cmdlets ni sintaxis posteriores. Si un dato no se puede obtener, escribe "no disponible" y sigue; nunca aborta a medias.
   - No recoge nombres de personas, cuentas, numeros de serie ni direcciones de red.
   - Si junto al guion esta el paquete, ejecuta tambien la autocomprobacion del punto 4 y anade su resultado.

Pruebas, con TEST_CHECK y sin assert:
- funcion de carpeta de datos: cada rama de la decision, incluidas carpeta no escribible (error, nunca otra carpeta), datos previos en la ubicacion anterior y rutas con espacios y acentos;
- paquete: instalar en un prefijo temporal y comprobar la lista exacta de archivos contra una lista permitida, que no hay DLL de depuracion ni bases de datos, que todas las DLL importadas que no son de Windows estan dentro, y que el ejecutable instalado pasa la autocomprobacion con un PATH reducido a las carpetas de Windows;
- autocomprobacion negativa: sin la carpeta de migraciones termina con codigo distinto de 0 y explica el motivo; tras cualquier ejecucion no queda la carpeta temporal.
Las pruebas no dejan archivos en el directorio desde el que se ejecutan y no dependen del directorio de trabajo. Si la prueba de paquete tarda mucho o solo tiene sentido en Release, dilo en el plan.

Verificacion. Antepon al PATH <toolchain>\mingw64\bin y <toolchain>\wxwidgets\lib\gcc1610_x64_dll:
   cmake --build build_be_debug --parallel 2
   ctest --test-dir build_be_debug --output-on-failure
   cmake --build build_be_release --parallel 2
   ctest --test-dir build_be_release --output-on-failure
y los comandos de instalacion y de CPack que definas, sobre build_be_release. Ejecuta la ficha del punto 7 en esta PC y pega su salida. No ejecutes pruebas GUI (tests/*.py). No instales nada. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual.

Entrega: archivos; comandos con codigo de salida; decisiones tomadas; lista de archivos del paquete con tamano total y del ZIP; opciones nuevas de linea de comandos con una linea de uso; numero de TEST_CHECK nuevos; salida de la ficha en esta PC; que queda NO VERIFICADO (instalador, otros Windows) y licencias pendientes. No escribas que algo funciona si no lo ejecutaste.
```

---

## B7 — respuesta al plan de Claude Code Terminal (2026-11-13)

Plan aprobado con ajustes. Decisiones del usuario: datos por equipo, ejecutable entregado `CafeteriaITCH.exe` e Inno Setup. La ficha se hace en PowerShell 2.0 con lanzador `.cmd`, no en VBScript (en retirada en Windows y no verificable en la PC de desarrollo).

```text
Plan de B7 aprobado con ajustes. Respuestas:
1. D2 aprobado por el usuario: datos por equipo en %ProgramData%\CafeteriaITCH\data y portable.flag como marcador de copia portable. D1, D3 y el orden de decision: si.
2. Ficha: NO VBScript. Microsoft lo esta retirando de Windows (en Windows 11 reciente es un componente opcional) y esta PC de desarrollo es Windows 11, asi que ni siquiera podriamos verificarlo aqui. Haz tools/ficha_pc.ps1 compatible con PowerShell 2.0 y un lanzador tools/ficha_pc.cmd para doble clic, que llame a powershell con -NoProfile -ExecutionPolicy Bypass -File solo para ese proceso, sin cambiar la politica del equipo. Debe aguantar rutas con espacios. Si no puede escribir junto al guion (USB protegida), escribe en el Escritorio y dice donde. Si PowerShell esta bloqueado por directiva, el .cmd lo dice en pantalla en vez de cerrarse en silencio.
3. Si: Windows 7 queda como riesgo declarado. No compiles contra otra version ni otro toolchain.
D4 aprobado por el usuario: CafeteriaITCH.exe solo al instalar. D5 aprobado por el usuario: Inno Setup.

Ajustes:
a. ZIP e instalador no son el mismo paquete. El ZIP es la copia portable y SI lleva portable.flag; el instalador NO lo instala. Si alguien descomprime el ZIP y lo ejecuta, sus datos quedan junto al exe y no en ProgramData. La prueba de paquete comprueba ambas listas.
b. Ninguna prueba ni comando de verificacion crea o escribe %ProgramData%\CafeteriaITCH en esta PC. La rama instalada se prueba solo con la funcion pura y sus callbacks. La comprobacion de D3 no se aplica en modo desarrollo, que queda exactamente como hoy (incluido que solo build_release y build_debug cuentan como desarrollo; no anadas build_be_*).
c. tiene_db significa que cafeteria.db existe y no esta vacio. El mensaje de D3 va en lenguaje llano, muestra las dos rutas y dice que hay que llamar a quien instalo el sistema; no menciona opciones de linea de comandos.
d. Autocomprobacion sin NINGUNA ventana, tambien cuando falla: ni wxMessageBox, ni el dialogo que wxWidgets muestra al terminar si hubo wxLogError, ni el de opcion desconocida de su analizador de linea de comandos. Declara --self-check y --self-check-report en el analizador existente. Comprueba en la prueba el codigo de salida real del proceso, tanto 0 como distinto de 0: que OnInit devuelva false no garantiza por si solo el codigo que esperas.
e. El informe de la autocomprobacion incluye: version, numero de migraciones aplicadas frente a archivos encontrados, y por cada fuente e icono esperado si existe y no esta vacio. La lista de fuentes e iconos esperados sale de lo que hay en assets/ al empaquetar, no de una lista escrita a mano que se desactualice.
f. package_check: la lista de DLL de Windows permitidas es explicita, no "existe en System32" (esta PC tiene cosas que la caja no). La comprobacion es recursiva: tambien las importaciones de las DLL incluidas. Usa el objdump del toolchain por CMAKE_OBJDUMP, no por PATH. La prueba de paquete recibe la ruta de cmake por definicion de compilacion, no por PATH.
g. Instalador: solo 64 bits; en un Windows de 32 bits se niega con un mensaje claro en vez de instalar algo que no arranca. Minimo Windows 7 SP1. No ejecuta la aplicacion al terminar. Crea la carpeta de datos con permiso de modificacion para Usuarios y no la toca si ya existe. Desinstalar no borra {commonappdata}\CafeteriaITCH.
h. La ficha registra tambien si el sistema es de 32 o 64 bits ANTES de intentar la autocomprobacion: en 32 bits no la intenta y lo escribe como motivo. Si la intenta, anota el codigo de salida y el contenido del informe; si el exe ni siquiera arranca, anota el codigo y sigue.
i. Licencias de wxWidgets, GCC y winpthreads: pendientes, como propones. No las descargues; el orquestador conseguira los textos oficiales.
j. No llames SetAppName ni cambies nada que dependa del nombre de la aplicacion: solo el RENAME al instalar. Si al renombrar cambia algun comportamiento observable (titulo, rutas, clase de ventana), reportalo.

Adelante con el codigo. Entrega: archivos; comandos con codigo de salida; lista de archivos del ZIP y de lo que instalaria el instalador, con tamanos; opciones nuevas de linea de comandos con una linea de uso; numero de TEST_CHECK nuevos; salida de la ficha en esta PC; que queda NO VERIFICADO y licencias pendientes. No hagas commits.
```

---

## T1 — AntiGravity, Gemini 4 Hydrogen (high) (`main`)

Primer bloque del perfil Táctil fuera de Venta: elegir el perfil en BackOffice > Configuración, diálogos de acceso en Táctil y una prueba válida con cualquier escalado de Windows que además mide todas las pantallas reales.

```text
Eres el agente de frontend de este proyecto. Si esta es una sesion nueva, lee primero AGENTS.md completo y docs/coordinacion-agentes.md. Bloque T1. Trabaja SOLO en <copia principal>, rama main. No hagas commits. No compiles ni corras pruebas antes de editar: main compila y CTest pasa 5/5 en build_release y build_debug.

Anota la hora de inicio ahora: la entrega debe incluir hora de inicio, hora de fin y cuantas veces compilaste y ejecutaste cada prueba.

Objetivo: avanzar el perfil Tactil. Hoy solo se cambia con el boton del encabezado, la especificacion pide elegirlo en BackOffice > Configuracion, y los dialogos de inicio de sesion y de alta del Dueno ignoran el perfil y tienen tamano fijo en pixeles. El usuario principal es una persona mayor: texto grande, lenguaje llano, nada de jerga.

Archivos que puedes editar, y ningun otro:
- nuevos: src/frontend/backoffice_settings_panel.hpp y .cpp
- CMakeLists.txt (solo agregar esos dos archivos a las fuentes de app_desktop)
- src/frontend/main_window.cpp y main_window.hpp (solo la rama que crea la vista de BackOffice > Configuracion y lo necesario para que cambiar el perfil desde esa pantalla tenga el mismo efecto que el boton del encabezado)
- src/frontend/header_panel.hpp y .cpp (solo para que el boton del encabezado muestre el perfil correcto cuando el cambio se hizo desde Configuracion)
- src/frontend/login_dialog.cpp y src/frontend/initial_owner_dialog.cpp
- nuevo: tests/test_t1_touch_profile.py
- tests/capture_visual_qa.py (solo para agregar las capturas 14 a 17, sin tocar las existentes)
No toques main_app.cpp ni app.rc (otro agente los esta editando), theme.*, ui_helpers.*, toggle_switch.*, backoffice_print_config_panel.*, otros paneles, contratos, servicios, migraciones ni otras pruebas. Si crees que necesitas tocar theme.*, detente y pidelo en la entrega.

1. Pantalla BackOffice > Configuracion (backoffice_settings_panel). Contiene, de arriba abajo y con desplazamiento vertical si no cabe:
   a. Tarjeta Pantalla de este equipo, con dos opciones grandes y una linea de explicacion cada una: Estandar (para raton y teclado) y Tactil (para pantallas que se tocan con el dedo). La opcion activa se distingue por algo mas que el color (marca o texto). Una linea aclara que el cambio solo afecta a este equipo y se recuerda al reiniciar.
   b. La pantalla de impresion que ya existe (BackofficePrintConfigPanel), incrustada tal cual, sin modificarla.
   - Elegir una opcion aplica el perfil al momento con Theme::set_profile, que ya lo guarda en station_profile.cfg. No escribas ese archivo por tu cuenta.
   - El cambio debe dejar toda la ventana igual que al usar el boton del encabezado: barra lateral, subsecciones, encabezado y la propia pantalla. Cuidado: si el cambio reconstruye la vista actual, el panel se destruye dentro de su propio manejador de evento; usa CallAfter o equivalente para no usar memoria liberada.
   - Tras el cambio el usuario sigue en BackOffice > Configuracion, con la opcion nueva marcada.
   - El boton del encabezado se queda y debe coincidir siempre con la opcion marcada en esta pantalla, se cambie desde donde se cambie.
   - Cambiar el perfil desde aqui no pierde el carrito de Venta.
   - No agregues permisos: hoy cualquier usuario puede cambiar el perfil con el boton del encabezado y eso no cambia en este bloque.

2. Dialogos de inicio de sesion (login_dialog.cpp) y de alta del Dueno (initial_owner_dialog.cpp):
   - Sin tamanos fijos en pixeles: toda medida pasa por FromDIP y el dialogo se ajusta a su contenido con el sizer, con un tamano minimo. Con el escalado de Windows a 125, 150 o 200 por ciento no se recorta ningun texto ni queda ningun control fuera del dialogo.
   - Si el perfil guardado del equipo es Tactil: campos de al menos 48 de alto, boton principal de al menos 56, texto centrado verticalmente en los campos y fuente de cuerpo del perfil Tactil. Medidas en unidades logicas (pixeles a 100 por ciento).
   - En Estandar se conserva el aspecto actual.
   - NO cambies el orden de creacion, la clase ni los textos de los controles: tests/test_i1_integration.py y las demas pruebas los localizan asi. Tampoco cambies la validacion ni las llamadas al servicio.

3. Prueba GUI nueva tests/test_t1_touch_profile.py con el ejecutable real (Release y Debug), base en directorio temporal aislado. Debe ser valida con CUALQUIER escalado de Windows, no solo a 100 por ciento:
   - el proceso de la prueba se declara consciente de DPI por monitor antes de crear o medir nada; lee el DPI de la ventana de la aplicacion con GetDpiForWindow, calcula escala = DPI / 96 y multiplica por esa escala cada umbral expresado en unidades logicas. Imprime el DPI y la escala al empezar;
   - NO cambies el escalado de Windows ni la resolucion.
   Casos, midiendo y no a ojo:
   a. Con station_profile.cfg inexistente: alta del Dueno en Estandar; ningun texto recortado (ancho del texto medido con la fuente del control menor o igual al ancho del control), todos los controles dentro del area cliente del dialogo, el dialogo dentro del area de trabajo de la pantalla.
   b. BackOffice > Configuracion muestra las dos opciones y la pantalla de impresion; Estandar aparece marcada. Elegir Tactil: station_profile.cfg cambia; los cuatro botones de la barra lateral pasan a medir al menos 56 logicos de alto; el boton del encabezado cambia de texto; la opcion Tactil queda marcada; la aplicacion sigue en Configuracion.
   c. Carrito: con un producto sembrado por SQL como hacen las otras pruebas, agregar una linea en Venta, cambiar el perfil desde Configuracion, volver a Venta: la linea sigue en pantalla y en SQLite.
   d. Volver a Estandar con el boton del encabezado estando en Configuracion: la opcion marcada en la pantalla cambia a Estandar.
   e. Dejar Tactil, cerrar y reabrir: el dialogo de inicio de sesion aparece en Tactil con las alturas del punto 2, sin recortes y dentro de la pantalla; tras entrar, Configuracion muestra Tactil marcada.
   f. Alta del Dueno en Tactil: en otra carpeta temporal, escribir station_profile.cfg con el contenido que produce la aplicacion para Tactil ANTES del primer arranque; mismas comprobaciones que en e.
   g. Inventario de objetivos tactiles (esta seccion NO hace fallar la prueba y NO corrijas lo que encuentre en este bloque). En perfil Tactil, a 1200x720 logicos, recorre cada pantalla real: Venta, Caja y turnos, Bitacora, Usuarios y cuentas, Roles y permisos, Configuracion e Ingredientes. Por cada control visible y habilitado que se pulsa o en el que se escribe (botones, campos de texto, listas desplegables, casillas, interruptores) anota pantalla, texto o clase, ancho y alto en unidades logicas. Marca los de menos de 48 de alto o de ancho y los pares de controles hermanos que se solapan. Escribe la tabla completa en qa_visual/touch_audit.txt, con el DPI y la escala en la primera linea, e imprime el total de controles y el total de marcados por pantalla.
   Puedes copiar funciones de medicion de tests/test_f1_roles_panel.py y tests/test_f7_ingredients_panel.py. Prohibido relajar o quitar una asercion para que pase: si una medida no se puede cumplir, detente y reporta el numero medido.

4. Capturas nuevas en tests/capture_visual_qa.py: 14_settings_standard.png, 15_settings_touch.png, 16_login_touch.png y 17_initial_owner_touch.png.

Verificacion (antepon al PATH <toolchain>\mingw64\bin y <toolchain>\wxwidgets\lib\gcc1610_x64_dll):
   cmake --build build_release --parallel 2
   cmake --build build_debug --parallel 2
   ctest --test-dir build_release --output-on-failure
   ctest --test-dir build_debug --output-on-failure
   python -u tests/test_t1_touch_profile.py
   python -u tests/test_i1_integration.py
   python -u tests/test_f2b_subnav.py
   python -u tests/capture_visual_qa.py
Las pruebas anteriores a este bloque (test_i1_integration, test_f2b_subnav y las capturas) solo son validas con Windows a 100 por ciento: comprueba el escalado antes; si no es 100 por ciento, NO las ejecutes y dilo en la entrega. La prueba nueva si se ejecuta con el escalado que haya. Antes de cada prueba GUI comprueba con tasklist que no haya ningun app_desktop.exe corriendo. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual. No instales nada.

Entrega corta y literal: archivos; comandos con codigo de salida; DPI y escala con que corrio la prueba nueva; alturas medidas de los dialogos en Estandar y Tactil; total de controles y de marcados por pantalla del inventario; rutas de las capturas 14 a 17 y de qa_visual/touch_audit.txt; fallos intermedios que tuviste y como los resolviste; hora de inicio y de fin, numero de compilaciones y de ejecuciones de cada prueba; limites de lo verificado. No escribas que algo funciona si no lo ejecutaste.
```

---

## T2 — AntiGravity, Gemini 4 Hydrogen (high) (`main`, desde T1 en `ac8348f`)

Corrige los 30 controles que el inventario de T1 marcó por debajo de 48 en Táctil, amplía la medición a los estados que T1 no vio y la convierte en aserciones. Corre en paralelo con B7; no comparten archivos.

```text
Eres el agente de frontend de este proyecto. Si esta es una sesion nueva, lee primero AGENTS.md completo y docs/coordinacion-agentes.md. Bloque T2. Trabaja SOLO en <copia principal>, rama main. No hagas commits. No compiles ni corras pruebas antes de editar: main compila, CTest pasa 5/5 y toda la bateria GUI pasa en build_release y build_debug.

Anota la hora de inicio ahora: la entrega debe incluir hora de inicio, hora de fin y cuantas veces compilaste y ejecutaste cada prueba.

Objetivo: que en perfil Tactil ningun control que se pulsa o en el que se escribe mida menos de 48 x 48 unidades logicas, y que eso quede vigilado por una prueba que falla. El inventario de T1 (qa_visual/touch_audit.txt) marco 30 de 93 controles; todos caen en cuatro causas:
- botones de subseccion: 44 de alto. Estan en src/frontend/subnav_bar.cpp (btn_h = is_touch ? 44 : 36);
- Caja y turnos: los campos de fondo inicial y de efectivo final son wxTextCtrl de 44 de alto en src/frontend/cash_shift_panel.cpp;
- Bitacora: la lista desplegable de tipo de evento es un wxChoice nativo, 29 de alto, en src/frontend/audit_log_panel.cpp;
- Configuracion de impresion: las dos listas desplegables son wxChoice nativos, 23 de alto, en src/frontend/backoffice_print_config_panel.cpp. El wxChoice nativo de Windows ignora el alto que se le pide.

Archivos que puedes editar, y ningun otro:
- src/frontend/subnav_bar.cpp, cash_shift_panel.cpp, audit_log_panel.cpp y backoffice_print_config_panel.cpp (y sus .hpp solo si cambia el tipo de un miembro);
- src/frontend/login_dialog.cpp e initial_owner_dialog.cpp (solo para el punto 5);
- nuevo: tests/test_t2_touch_targets.py;
- tests/test_t1_touch_profile.py (solo para quitar la seccion g, que pasa a la prueba nueva);
- tests/test_f2_audit_log.py, tests/test_f2b_subnav.py y tests/capture_visual_qa.py, SOLO en las lineas que localizan o manejan un control que cambio de clase o que comparan contra el alto anterior exacto de Tactil. Cada linea tocada en estas tres se lista en la entrega con su antes y su despues.
No toques main_app.cpp ni app.rc (otro agente los esta editando), theme.*, ui_helpers.*, toggle_switch.*, pos_panel.*, otros paneles, contratos, servicios ni migraciones. Si la correccion de un control exige un archivo que no esta en la lista, NO lo edites: dejalo marcado y reportalo.

1. Subsecciones: 48 de alto en Tactil. En Estandar no cambia.

2. Caja y turnos: los dos campos de efectivo a 48 de alto en Tactil con el texto centrado verticalmente, usando ui::make_text_field de ui_helpers tal como esta. El comportamiento (validacion, llamadas al servicio, habilitar y deshabilitar) no cambia; usa ui::set_text_field_enabled donde hoy se habilita o deshabilita el campo.

3. Listas desplegables de Bitacora y de impresion: 48 de alto en Tactil, con opciones tambien comodas al desplegar (al menos 40 de alto cada una). Usa el mismo tipo de control que ya usa Venta para elegir producto (wxOwnerDrawnComboBox en pos_panel.cpp; leelo, no lo edites). Usa el mismo control en Estandar y en Tactil, no dos caminos distintos. Las opciones, la seleccion inicial, lo que se guarda y lo que se filtra no cambian.
   - Esto cambia la clase Win32 del control: tests/test_f2_audit_log.py y tests/capture_visual_qa.py lo manejan como ComboBox nativo. Adapta solo la forma de localizarlo y de elegir una opcion; ninguna comprobacion de resultado se quita ni se debilita.

4. Prueba nueva tests/test_t2_touch_targets.py, con el ejecutable real (Release y Debug), base temporal aislada, consciente de DPI igual que test_t1_touch_profile.py (copia sus funciones). En perfil Tactil, a 1200x720 logicos:
   a. Mueve aqui el inventario de la seccion g de T1 y conviertelo en aserciones: la prueba FALLA si algun control visible y habilitado que se pulsa o en el que se escribe mide menos de 48 de ancho o de alto en unidades logicas, o si dos controles hermanos se solapan. Sigue escribiendo la tabla completa en qa_visual/touch_audit.txt.
   b. Amplia el recorrido a los estados que T1 no vio. Mide ademas de la pantalla recien abierta:
      - Venta con una linea del carrito seleccionada;
      - Caja y turnos con el turno abierto (campo de efectivo final y boton de cierre);
      - Bitacora con filas y con la lista desplegable abierta;
      - Usuarios y cuentas con un usuario seleccionado y con el formulario de usuario nuevo;
      - Roles y permisos con un rol seleccionado (interruptores visibles);
      - Configuracion con cada lista desplegable abierta;
      - Ingredientes con el formulario de ingrediente nuevo y con un ingrediente seleccionado;
      - dialogos de cobro, de ticket y de historial de tickets.
   c. Filas de listas: cada lista con filas mide al menos 40 de alto por fila (LVM_GETITEMRECT).
   d. Lo que la parte b o c encuentre por debajo del minimo: si esta en uno de los archivos permitidos, corrigelo. Si esta en otro archivo, NO lo corrijas: registralo en una lista de excepciones dentro de la prueba, con pantalla, control y medida, que la prueba imprime completa al terminar. La prueba falla ante cualquier control pequeno que no este en esa lista, y tambien si una excepcion de la lista ya no se reproduce. Prohibido meter en la lista algo que si puedes corregir.
   Prohibido relajar o quitar una asercion para que pase: si una medida no se puede cumplir, detente y reporta el numero medido.

5. Limpieza menor de T1: login_dialog.cpp e initial_owner_dialog.cpp tienen cada uno una copia de make_dialog_text_field. Sustituyelas por ui::make_text_field SOLO si tests/test_t1_touch_profile.py sigue pasando sin tocar ninguna de sus aserciones; si no da las mismas medidas, deja el codigo como esta y dilo en la entrega.

Verificacion (antepon al PATH <toolchain>\mingw64\bin y <toolchain>\wxwidgets\lib\gcc1610_x64_dll):
   cmake --build build_release --parallel 2
   cmake --build build_debug --parallel 2
   ctest --test-dir build_release --output-on-failure
   ctest --test-dir build_debug --output-on-failure
   python -u tests/test_t2_touch_targets.py
   python -u tests/test_t1_touch_profile.py
   python -u tests/test_f2_audit_log.py
   python -u tests/test_f2b_subnav.py
   python -u tests/test_fe08_fe09_visual_integration.py
   python -u tests/test_i1_integration.py
   python -u tests/capture_visual_qa.py
Las pruebas anteriores a T1 solo son validas con Windows a 100 por ciento: comprueba el escalado antes; si no es 100 por ciento, NO las ejecutes y dilo. Las dos pruebas de perfil tactil si se ejecutan con el escalado que haya. No cambies el escalado de Windows. Antes de cada prueba GUI comprueba con tasklist que no haya ningun app_desktop.exe corriendo. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual. No instales nada.

Entrega corta y literal: archivos; comandos con codigo de salida; DPI y escala; total de controles medidos y de marcados por pantalla y estado; la lista de excepciones completa con el archivo que habria que tocar para cada una; cada linea cambiada en las pruebas anteriores con su antes y su despues; si hiciste o no el punto 5 y por que; fallos intermedios y como los resolviste; hora de inicio y de fin, numero de compilaciones y de ejecuciones de cada prueba; limites de lo verificado. No escribas que algo funciona si no lo ejecutaste.
```

---

## En cola para I7 (sin redactar)

- **T3** (Gemini, tras decisión del usuario): captura numérica para cantidad y efectivo en Táctil (teclado numérico propio o teclado en pantalla de Windows), sin tapar el campo, el total ni la confirmación.
- **B8** (Claude, tras aprobar la instalación de la herramienta): compilar el instalador, probar instalar, actualizar y desinstalar conservando los datos.
- **Fuera de los agentes:** ejecutar la ficha en la PC de caja; icono de la aplicación; prueba del paquete en un equipo limpio.
