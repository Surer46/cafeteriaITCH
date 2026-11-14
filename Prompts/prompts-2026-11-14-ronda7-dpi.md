# Prompts del 2026-11-14 — ronda 7 (escalado de Windows al 100 y al 125 %)

Las rutas absolutas de la PC en uso se sustituyen por marcadores (`<copia principal>`, `<toolchain>`); ver `docs/entorno.md`. Al enviar un prompt se escriben las rutas reales.

Decisión del usuario del 2026-11-14: el programa y las pruebas deben funcionar con Windows al **100 % y al 125 %**. La PC de caja puede usar 125 % y esta PC de desarrollo trabaja mejor así. Otros escalados (150, 200 %) siguen fuera de alcance hasta que se pidan.

Medido por el orquestador antes de redactar (`main` en `9042715`, Windows al 125 %, 120 DPI, pantalla 1920x1200):

| Prueba | Resultado a 125 % | Causa |
|---|---|---|
| `test_i1_integration` | pasa | — |
| `test_t1_touch_profile` | falla | **programa**: la barra lateral mide 56 px físicos (44.8 lógicos, se piden 56) |
| `test_t2_touch_targets` | falla | **programa**: barra lateral 44.8, botón de perfil del encabezado 38.4 lógicos, filas de Bitácora 34.4, etc. |
| `test_fe08_fe09_visual_integration` | falla | campo de notas de Venta en Touch: 38 px físicos |
| `test_f2b_subnav` | falla | la prueba no mide el color en el punto correcto (coordenadas sin escalar) |
| `test_f2_audit_log` | falla | columnas 920 px frente a un área de 737: mezcla de unidades en la prueba |
| `test_f1_roles_panel` | falla | el clic no selecciona la fila (coordenadas sin escalar). Además falla a 100 % desde el 2026-11-14 en 960x600 Estándar |
| `test_f7_ingredients_panel` | se detiene | exige 96 DPI a propósito |

Causa común en el programa: las fuentes van en puntos y crecen con el escalado, pero casi todos los tamaños de cajas, botones, filas y márgenes son píxeles fijos. `theme.cpp` devuelve alturas y espacios sin escalar, y 10 de los 22 archivos de `src/frontend/` no usan `FromDIP` en ningún sitio. A 125 % el texto crece y su caja no.

Orden: **D1** (programa) y después **D2** (pruebas). D1 usa como regla de medir las dos pruebas que ya son conscientes de DPI (T1 y T2). Los dos son de Gemini; no corren en paralelo.

Cambiar el escalado de Windows lo hace el usuario, no los agentes. Cada agente verifica con el escalado que tenga la PC; el orquestador repite al otro escalado al auditar.

---

## D1 — AntiGravity, Gemini 4 Hydrogen (high) (`main`)

```text
Eres el agente de frontend de este proyecto. Si esta es una sesion nueva, lee primero AGENTS.md completo y docs/coordinacion-agentes.md. Bloque D1. Trabaja SOLO en <copia principal>, rama main. No hagas commits. No compiles ni corras pruebas antes de editar: main compila y CTest pasa 7/7 en build_release.

Anota la hora de inicio ahora: la entrega debe incluir hora de inicio, hora de fin y cuantas veces compilaste y ejecutaste cada prueba.

Decision del usuario: el programa debe verse y medir igual con Windows al 100 y al 125 por ciento. Esta PC esta ahora a 125 por ciento (120 DPI). No cambies el escalado de Windows.

Problema medido por el orquestador a 125 por ciento: las fuentes van en puntos y crecen con el escalado, pero casi todos los tamanos de cajas, botones, filas, margenes y radios son pixeles fijos, asi que el texto crece y su caja no. Ejemplos medidos en unidades logicas (pixeles a 100 por ciento): botones de la barra lateral 44.8 en lugar de 56; boton de perfil del encabezado 38.4 en lugar de 48; filas de la lista de Bitacora 34.4 en lugar de 40; campo de notas de Venta en Touch 30.4. Theme::get_button_height, get_spacing_*, get_touch_target_min y los demas metodos de theme.cpp devuelven pixeles sin escalar, y 10 de los 22 archivos de src/frontend/ no usan FromDIP en ningun sitio.

Objetivo: toda medida de la interfaz se escribe en unidades logicas y se convierte a pixeles una sola vez, con FromDIP de la ventana que la usa. A 125 por ciento cada medida fisica es 1.25 veces la logica; a 100 por ciento nada cambia respecto a hoy.

Archivos que puedes editar: todo src/frontend/ y nada mas. No toques contratos, servicios, migraciones, CMakeLists.txt, cmake/, packaging/, tools/ ni ninguna prueba (tests/). Las pruebas se adaptan en el bloque siguiente.

Reglas:
1. Un solo mecanismo. Los metodos de Theme que devuelven medidas (alturas, espacios, radios, objetivo tactil) reciben la ventana que las va a usar y devuelven pixeles ya escalados con su FromDIP. Quita las versiones sin ventana, para que el compilador te muestre cada llamada que falta. Las funciones de Theme y de ui_helpers que reciben una medida (style_button_*, make_text_field, setup_button_icon, etc.) la reciben SIEMPRE en unidades logicas y la convierten por dentro; ninguna llamada les pasa un valor ya convertido. Revisa las llamadas que hoy ya pasan FromDIP(...) a esas funciones (T1 y T2 lo hicieron en backoffice_settings_panel, cash_shift_panel, login_dialog e initial_owner_dialog): con la regla nueva se escalarian dos veces.
2. Prohibido multiplicar por 1.25, por un DPI leido a mano o por cualquier factor propio: solo FromDIP, ToDIP y las funciones de wxWidgets equivalentes.
3. Iconos: Theme::get_icon crea el wxBitmapBundle con wxBitmapBundle::FromSVG(svg, size). Comprueba en la documentacion de wxWidgets 3.3 si ese size es el tamano a 100 por ciento (logico); si lo es, el tamano de un icono se pasa en unidades logicas y nunca con FromDIP. Mide al menos un icono dibujado a 125 por ciento y reporta su tamano fisico.
4. Incluye todo lo que tenga tamano: wxSize de constructores y SetMinSize, SetSize, margenes y bordes de sizers, separaciones de wxFlexGridSizer, anchos de columna de listas, altura de filas (ui::apply_touch_row_height), Wrap de textos, radios y grosores en el dibujo propio (CardPanel, StatusBadge, ToggleSwitch, SubnavBar, combos dibujados por el propietario), DoGetBestSize y OnMeasureItem. Los tamanos de fuente en puntos NO se tocan.
5. El comportamiento no cambia: ni textos, ni orden de creacion, ni clases, ni llamadas a servicios. Las pruebas localizan los controles por clase, texto y orden.
6. Si encuentras un tamano que no puedes convertir sin cambiar el comportamiento, detente y reportalo con archivo y linea.

Verificacion, en esta PC a 125 por ciento (antepon al PATH <toolchain>\mingw64\bin y <toolchain>\wxwidgets\lib\gcc1610_x64_dll):
   cmake --build build_release --parallel 2
   cmake --build build_debug --parallel 2
   ctest --test-dir build_release --output-on-failure
   ctest --test-dir build_debug --output-on-failure
   python -u tests/test_t1_touch_profile.py
   python -u tests/test_t2_touch_targets.py
   python -u tests/test_i1_integration.py
Las dos pruebas de perfil tactil ya miden en unidades logicas con cualquier escalado: son la referencia de este bloque y deben pasar a 125 por ciento SIN tocarlas. test_t2_touch_targets tiene una lista de 35 excepciones (controles pequenos en backoffice_users_panel, toggle_switch, checkout_dialog y ticket_history_dialog) que fallara si alguna ya no se reproduce; si al escalar bien alguno de esos controles deja de ser pequeno, NO edites la prueba: reporta cual y con que medida. Las demas pruebas GUI aun no son validas a 125 por ciento: no las ejecutes. Antes de cada prueba GUI comprueba con tasklist que no haya ningun app_desktop.exe corriendo. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual. No instales nada.

Comprobacion visual: con python -u tests/capture_visual_qa.py NO (aun no es valido a 125). En su lugar abre build_release\app_desktop.exe con --data-dir en una carpeta temporal, recorre Venta, Caja y turnos, Bitacora, Usuarios, Roles, Configuracion e Ingredientes en Estandar y en Touch, y reporta cualquier texto recortado o control solapado que veas, con pantalla y control.

Entrega corta y literal: archivos; el mecanismo elegido en Theme con una linea de uso; numero de medidas convertidas por archivo; las llamadas que pasaban FromDIP a Theme o ui_helpers y como quedaron; que dice la documentacion sobre el tamano de FromSVG y el tamano fisico medido de un icono; comandos con codigo de salida; medidas logicas a 125 por ciento de la barra lateral, boton de perfil, subsecciones y filas de Bitacora; excepciones de test_t2 que ya no se reproducen; lo que viste en el recorrido manual; fallos intermedios y como los resolviste; hora de inicio y de fin, numero de compilaciones y de ejecuciones de cada prueba; limites de lo verificado. No escribas que algo funciona si no lo ejecutaste.
```

### Anexo a D1 (2026-11-14, QA visual del usuario)

El usuario vio a 100 % en Touch el texto de «Abrir Turno de Caja» desplazado e incompleto. Causa en el código: ancho fijo de 200 y, después, fuente de subtítulo en negrita más icono y margen. Se envía como mensaje aparte si D1 ya está en curso.

```text
Anexo a D1 (QA visual del usuario, a 100 por ciento): en Caja y turnos, perfil Touch, el texto del boton Abrir Turno de Caja se ve desplazado y se lee incompleto. Causa probable, revisala: cash_shift_panel.cpp le fija un ancho de 200 y luego Theme::style_button_success le pone la fuente de subtitulo en negrita (14 pt en Touch) y Theme::setup_button_icon le agrega icono de 18 mas margen de 10; texto mas icono ya no caben en 200. Cerrar Turno de Caja tiene el mismo patron.

Agrega al bloque esta regla y aplicala en todo src/frontend/: ningun boton puede tener un ancho fijo menor que lo que necesita su contenido (texto con su fuente final, icono, margen del icono y relleno del boton). El ancho minimo se calcula DESPUES de aplicar fuente e icono (por ejemplo con GetBestSize), o el boton se deja crecer con el sizer; un ancho fijo solo puede ser mayor que ese minimo, nunca menor. Revisa todos los botones con icono de las pantallas reales, en Estandar y en Touch.

En la entrega anade: la causa confirmada del recorte de Abrir Turno de Caja; la lista de botones a los que cambiaste el ancho, con ancho de texto mas icono y ancho final en unidades logicas, en Estandar y Touch.
```

---

## D2 — AntiGravity, Gemini 4 Hydrogen (high) (`main`, después de aceptar D1)

Redactado de antemano; revisar contra la entrega de D1 antes de enviarlo.

```text
Eres el agente de frontend de este proyecto. Si esta es una sesion nueva, lee primero AGENTS.md completo y docs/coordinacion-agentes.md. Bloque D2. Trabaja SOLO en <copia principal>, rama main. No hagas commits. No compiles ni corras pruebas antes de editar: main compila, CTest pasa y test_t1_touch_profile y test_t2_touch_targets pasan a 100 y a 125 por ciento.

Anota la hora de inicio ahora: la entrega debe incluir hora de inicio, hora de fin y cuantas veces ejecutaste cada prueba.

Decision del usuario: las pruebas GUI deben ser validas con Windows al 100 y al 125 por ciento. Hoy solo test_t1_touch_profile y test_t2_touch_targets lo son. Las demas miden en pixeles fijos, clican en coordenadas sin escalar o se detienen si no es 100 por ciento. No cambies el escalado de Windows: verifica con el que tenga la PC y di cual era.

Archivos que puedes editar, y ningun otro:
- nuevo: tests/gui_dpi.py (modulo comun);
- tests/test_i1_integration.py, test_i2_integration.py, test_fe08_fe09_visual_integration.py, test_f1_roles_panel.py, test_f2_audit_log.py, test_f2b_subnav.py, test_f7_ingredients_panel.py y capture_visual_qa.py;
- test_t1_touch_profile.py y test_t2_touch_targets.py solo para usar el modulo comun en lugar de su copia de las mismas funciones, sin cambiar ninguna asercion existente (en test_t2 se agrega la del punto 6).
No toques src/, CMakeLists.txt ni nada fuera de tests/. Si una prueba falla por el programa (un control realmente pequeno o recortado), NO cambies el programa: detente y reportalo con la medida.

1. tests/gui_dpi.py: declarar el proceso consciente de DPI por monitor antes de crear o medir nada; leer el DPI de una ventana (GetDpiForWindow); escala = DPI / 96; aceptar solo 96 y 120 DPI y en cualquier otro detenerse con un mensaje claro; convertir de unidades logicas a fisicas y al reves; y medir texto con las fuentes Roboto del proyecto (como hace test_f7, porque la fuente obtenida con WM_GETFONT no es fiable desde otro proceso). Una sola implementacion; las pruebas la importan.
2. En cada prueba: todo umbral o tamano escrito como numero (alturas minimas, holguras, tamanos de ventana como 1200x720 y 960x600, desplazamientos de clic) pasa a unidades logicas multiplicadas por la escala, en un solo sitio. Los clics y las lecturas de color usan coordenadas fisicas calculadas a partir de las medidas reales del control, no numeros fijos.
3. Prohibido relajar: un umbral escalado vale exactamente su valor a 100 por ciento multiplicado por la escala, redondeado hacia arriba cuando es un minimo y hacia abajo cuando es un maximo. Ningun umbral cambia su valor a 100 por ciento. Ninguna asercion se quita. Las tolerancias de medida (por ejemplo 2 o 3 px por redondeo) pueden escalarse, pero se listan todas en la entrega con su valor antes y despues.
4. test_f7_ingredients_panel deja de exigir 96 DPI y usa el modulo comun.
5. test_f1_roles_panel falla tambien a 100 por ciento desde el 2026-11-14 (caso 3.2, 960x600 Estandar: descripcion de pos.sell 219 px frente a 250 de texto) y el orquestador comprobo que no lo causo T2. Averigua si es la medicion (fuente de WM_GETFONT) o un recorte real visible en pantalla: haz una captura de esa zona y mide el texto con la fuente Roboto real. Si es la medicion, corrigela con el modulo comun. Si es un recorte real, NO lo arregles: reportalo con la captura.
6. Recorte de texto en botones (hallazgo del QA visual del usuario: Abrir Turno de Caja se leia incompleto en Touch y ninguna prueba lo detecto). En tests/test_t2_touch_targets.py anade una asercion para cada boton visible que recorre: ancho del texto medido con la fuente Roboto real, mas el icono y su margen si tiene, cabe en el area cliente del boton. Si falla por un boton que D1 no corrigio, NO cambies el programa: reportalo. Esta asercion es nueva; las existentes no cambian.
7. capture_visual_qa.py: las capturas se generan con cualquiera de los dos escalados; el nombre del archivo no cambia y la entrega dice con cual se generaron.
8. No edites AGENTS.md ni ningun documento: el orquestador actualiza la regla de escalado al aceptar el bloque.

Verificacion (antepon al PATH <toolchain>\mingw64\bin y <toolchain>\wxwidgets\lib\gcc1610_x64_dll), con el escalado que tenga la PC:
   python -u tests/test_i1_integration.py
   python -u tests/test_t1_touch_profile.py
   python -u tests/test_t2_touch_targets.py
   python -u tests/test_f2b_subnav.py
   python -u tests/test_fe08_fe09_visual_integration.py
   python -u tests/test_f2_audit_log.py
   python -u tests/test_f7_ingredients_panel.py
   python -u tests/test_f1_roles_panel.py
   python -u tests/capture_visual_qa.py
No ejecutes test_i2_integration.py (pendiente conocido del dialogo de impresion en el escritorio real), pero si adaptala. Antes de cada prueba GUI comprueba con tasklist que no haya ningun app_desktop.exe corriendo. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual. No instales nada. No compiles: este bloque no toca el programa.

Entrega corta y literal: archivos; DPI con que corriste; por cada prueba, los umbrales y tolerancias que tocaste con su valor a 100 y a 125; lineas de aserciones cambiadas con su antes y su despues; conclusion del punto 5 con la medida y la captura; comandos con codigo de salida; fallos intermedios y como los resolviste; hora de inicio y de fin, numero de ejecuciones de cada prueba; limites de lo verificado. No escribas que algo funciona si no lo ejecutaste.
```

---

## B8 — Claude Code Terminal (`backend/b8`, desde `main`, con plan previo)

Arranque seguro (respaldo antes de migrar y comprobación de integridad), límite de intentos de inicio de sesión y revisión de autorización en todos los servicios. Ninguno depende de decisiones abiertas de la sección 9. Corre en paralelo con D1 y D2: único archivo compartido posible, `src/frontend/main_app.cpp` (B8 solo conecta el arranque; D1 no debería necesitar tocarlo). El bloque del instalador pasa a llamarse **B9**.

```text
Bloque B8 de backend. Si esta es una sesion nueva, lee primero AGENTS.md completo y docs/coordinacion-agentes.md. Trabaja SOLO en <worktree del backend>, rama backend/b8 (ya creada desde main; incluye B7: paquete, carpeta de datos y --self-check). No hagas commits. No compiles ni corras pruebas antes de editar: la linea base esta verificada (CTest 7/7 en Release; PackageTest omitida en Debug).

Contexto: la aplicacion ya se puede empaquetar y pronto se instalara en la caja. Antes de eso hacen falta tres protecciones que AGENTS.md exige o que el plan de backend tiene pendientes, ninguna depende de decisiones de negocio abiertas.

El otro agente esta trabajando en main sobre el escalado de pantalla: edita todo src/frontend/ y luego tests/*.py. No toques nada de eso, con UNA excepcion: src/frontend/main_app.cpp, solo para conectar el punto 1 en el arranque, con el cambio minimo (una llamada y su mensaje de error). Nada de tamanos, ventanas ni otros archivos de src/frontend/.

Archivos permitidos: include/app/ (contratos solo con cambios aditivos), src/domain/, src/storage/, src/services/, migrations/ (solo archivos nuevos, a partir de 014), tests/test_*.cpp, tests/CMakeLists.txt, CMakeLists.txt (solo bibliotecas y pruebas) y main_app.cpp como se indica arriba. No toques migraciones 001 a 013, src/crypto/ ni tests/*.py.

Presenta primero un plan para revision y espera aprobacion antes de escribir codigo. El plan debe resolver los puntos marcados como DECIDIR, con una recomendacion en cada uno.

1. Arranque seguro de una base existente. AGENTS.md: "Crear respaldo consistente antes de migrar datos existentes". Hoy MigrationRunner aplica las migraciones pendientes sin respaldo previo y nadie comprueba la integridad de la base al arrancar. Al instalar una version nueva en la caja, una migracion fallida a medias seria perdida de ventas.
   - Si la base ya tiene datos y hay migraciones pendientes: antes de aplicar la primera, respaldo consistente con el mecanismo de instantanea que ya existe (B1), verificado. Si el respaldo falla, no se migra y la aplicacion no arranca, con un mensaje en lenguaje llano que diga donde esta la base y que hacer. Una base nueva (primer inicio) no se respalda.
   - Si una migracion falla, la base queda como estaba antes de esa migracion (cada una en su transaccion: compruebalo, no lo supongas) y el mensaje indica que existe el respaldo previo y donde.
   - Comprobacion de integridad al arrancar. DECIDIR: PRAGMA quick_check o integrity_check, midiendo cuanto tarda cada uno en esta PC con una base de prueba de tamano realista (por ejemplo 50 000 ventas) y estimando el costo en un i5 de segunda generacion con disco duro. Si la base esta danada, no se abre para vender: mensaje llano con la ruta y la indicacion de restaurar un respaldo.
   - DECIDIR: donde se guarda el respaldo previo a migrar y como se distingue de los demas (tipo propio en el registro de respaldos de B6). Ese respaldo no debe borrarlo la limpieza de B6 mientras sea el unico previo a la version actual.
   - Toda la logica en storage o services, en una funcion sin wxWidgets que se pueda probar en CTest; main_app.cpp solo la llama y muestra el mensaje.
   - Cuidado con --self-check (B7): usa su propia base temporal vacia y no debe crear respaldos.

2. Limite de intentos de inicio de sesion. Hoy se puede probar contrasenas sin limite.
   - Tras N intentos fallidos seguidos para un mismo usuario, ese usuario queda bloqueado M minutos; el mensaje dice en lenguaje llano cuanto falta, sin revelar si el usuario existe (mismo mensaje y mismo tiempo de respuesta aproximado para usuario inexistente).
   - Un inicio correcto reinicia la cuenta de fallos. Persistido en SQLite: cerrar y abrir la aplicacion no lo reinicia. Usa la hora UTC que ya existe (B4) para no depender de cambios de hora local.
   - Evento de auditoria al bloquear (sin la contrasena intentada, nunca).
   - DECIDIR y recomendar N y M. DECIDIR que pasa con la ultima cuenta Dueno activa: no puede quedar la cafeteria sin nadie que entre; propone como evitarlo sin crear una puerta trasera ni contrasena maestra.
   - El primer inicio (alta del Dueno) no cambia.

3. Revision de autorizacion en todos los servicios reales. AGENTS.md: comprobar permisos dentro de cada servicio aunque se invoque sin la pantalla normal. El plan de backend dice desde 2026-09 que catalogo, inventario, carrito y venta omiten controles cuando no hay sesion; puede estar desactualizado.
   - Primero haz y entrega una tabla con cada metodo publico de cada Sqlite*Service: que permiso exige, si exige sesion, y si cumple. No supongas: leelo.
   - Corrige lo que no cumpla. No cambies que permiso exige un metodo que ya lo exige: si crees que es el permiso equivocado, reportalo en vez de cambiarlo.
   - Prueba CTest que recorre todos esos metodos sin sesion y con un rol sin el permiso: todos rechazan y nada se escribe en la base (comprobado por SQL). Si un metodo debe ser publico sin sesion (por ejemplo has_owner o login), lista cuales y por que.

Reglas: migraciones nuevas versionadas, una sola vez, sin efecto al repetir el runner; dinero y cantidades sin float ni double; no relajar pruebas existentes; contratos solo aditivos; no cambiar textos ni comportamiento visibles salvo los mensajes nuevos.

Pruebas CTest nuevas, con TEST_CHECK:
- arranque: base con datos y migracion pendiente -> respaldo creado y verificado antes de migrar; respaldo imposible -> no migra; migracion que falla a medias (una migracion de prueba rota en una carpeta temporal) -> base intacta y respaldo disponible; base danada (bytes alterados en una copia) -> se detecta y no se abre; base nueva -> sin respaldo; segundo arranque sin pendientes -> sin respaldo nuevo;
- intentos: bloqueo tras N, persiste tras cerrar y reabrir, se libera tras M (con reloj inyectable, sin esperar minutos reales), usuario inexistente indistinguible, auditoria sin contrasena, caso de la ultima cuenta Dueno;
- autorizacion: el recorrido del punto 3.
Las pruebas no dejan archivos en el directorio desde el que se ejecutan.

Verificacion. Antepon al PATH <toolchain>\mingw64\bin y <toolchain>\wxwidgets\lib\gcc1610_x64_dll:
   cmake --build build_be_debug --parallel 2
   ctest --test-dir build_be_debug --output-on-failure
   cmake --build build_be_release --parallel 2
   ctest --test-dir build_be_release --output-on-failure
No ejecutes pruebas GUI. No instales nada. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual.

Entrega: archivos; comandos con codigo de salida; decisiones tomadas; la tabla del punto 3 con lo que corregiste; tiempos medidos de quick_check e integrity_check; contratos nuevos con una linea de uso; numero de TEST_CHECK nuevos; mensajes nuevos que vera el usuario, textuales; limites de lo verificado. No escribas que algo funciona si no lo ejecutaste.
```

---

## B8 — respuesta al plan de Claude Code Terminal (2026-11-14)

Plan aprobado con ajustes. Decisiones del usuario: 5 intentos y 5 minutos de bloqueo; el Dueño sigue la misma regla, sin excepción; el aviso de cuenta desactivada solo con la contraseña correcta.

```text
Plan de B8 aprobado con ajustes. Decisiones del usuario:
- N = 5 intentos y M = 5 minutos.
- La ultima cuenta Dueno sigue la misma regla, sin excepcion ni puerta trasera.
- Cuenta inactiva: el aviso de cuenta desactivada solo aparece con la contrasena correcta; si no, el mensaje generico.
Tambien aprobados: quick_check en cada arranque e integrity_check solo con migraciones pendientes; la ruta y el tipo PRE_MIGRATION del respaldo previo; registrarlo despues de migrar; exigir sesion en los metodos de lectura listados conservando sus permisos.

Ajustes:
a. Migracion fallida: con varias migraciones pendientes, las anteriores a la que falla ya quedaron aplicadas, asi que la base NO queda "como estaba". Que el mensaje y la entrega lo digan con exactitud: version en la que quedo la base, que la aplicacion no arranca, y donde esta el respaldo previo, que es el estado anterior completo. Pruebalo con dos migraciones pendientes donde falla la segunda.
b. Migracion 014: reconstruir backup_files es delicado. Conserva todas las filas, indices, restricciones y claves foraneas que tenga hoy, y que la prueba lo compruebe sobre una base con filas de B6 (LOCAL y PRE_RESTORE con su tamano y hash), incluido que la limpieza de B6 sigue funcionando igual despues.
c. Si el respaldo previo se crea pero la migracion falla, el archivo queda sin registrar y B6 nunca lo ve. Esta bien, pero el mensaje al usuario debe dar su ruta exacta.
d. Espacio en disco: antes de respaldar comprueba que hay espacio libre suficiente (al menos el tamano de la base mas un margen); si no, no migra y lo dice en lenguaje llano. Prueba con un callback de espacio inyectable, sin llenar el disco.
e. Mensajes al usuario: lenguaje llano, sin nombres de pragmas, tablas ni codigos; con la ruta y lo que debe hacer (llamar a quien instalo el sistema o restaurar un respaldo). Entrega cada mensaje textual.
f. Tiempos: mide quick_check e integrity_check con 50 000 ventas y tambien con 250 000, y el tiempo total de arranque con y sin migraciones pendientes. La estimacion para un i5 de segunda generacion con disco duro va marcada como estimacion.
g. Autorizacion: la tabla de la entrega incluye tambien cuentas y respaldos completos, no solo los que cambies. Antes de exigir sesion en get_current_shift, get_station_config y las lecturas de catalogo e inventario, revisa tambien rutas que no sean el arranque: cierre de sesion, cambio de usuario y cualquier llamada desde un destructor o al cerrar la ventana; si alguna llamaria sin sesion, reportalo antes de cambiarlo. El orquestador correra toda la bateria GUI al integrar.
h. Los metodos auxiliares publicos en C++ que no estan en el contrato (get_or_create_cart_id, touch_cart, generate_next_folio, find_sale_by_idempotency_key): si se pueden llamar desde fuera del servicio, exigen sesion o pasan a privados; si pasan a privados y alguna prueba los usa, dilo antes de tocar la prueba.
i. Intentos: la poda de filas antiguas y el tope no pueden borrar la fila de un usuario que esta bloqueado ahora. Usuario normalizado igual que login (si login distingue mayusculas, la clave tambien; dilo).
j. main_app.cpp: solo la llamada y el mensaje. El otro agente esta editando src/frontend/; no toques ninguna otra linea de ese archivo.

Adelante con el codigo. Entrega: archivos; comandos con codigo de salida; decisiones tomadas; la tabla completa de autorizacion con lo que corregiste; tiempos medidos; mensajes nuevos textuales; contratos nuevos con una linea de uso; numero de TEST_CHECK nuevos; limites de lo verificado. No hagas commits.
```

