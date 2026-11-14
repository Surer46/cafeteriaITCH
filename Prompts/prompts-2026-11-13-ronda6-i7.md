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

## En cola para I7 (sin redactar)

- **T2** (Gemini): corregir los controles marcados en `qa_visual/touch_audit.txt` y convertir el inventario en aserciones, pantalla por pantalla.
- **T3** (Gemini, tras decisión del usuario): captura numérica para cantidad y efectivo en Táctil (teclado numérico propio o teclado en pantalla de Windows), sin tapar el campo, el total ni la confirmación.
- **B8** (Claude, tras aprobar la instalación de la herramienta): compilar el instalador, probar instalar, actualizar y desinstalar conservando los datos.
- **Fuera de los agentes:** ejecutar la ficha en la PC de caja; icono de la aplicación; prueba del paquete en un equipo limpio.
