# Prompts del 2026-11-13 — ronda 5 (inicio de Inventario, I3)

Las rutas absolutas de la PC en uso se sustituyen por marcadores (`<copia principal>`, `<worktree del backend>`, `<toolchain>`); ver `docs/entorno.md`. Al enviar un prompt se escriben las rutas reales.

Decisiones del usuario del 2026-11-13: I3 se desarrolla sin esperar el QA humano de I2; sin sobreventa; existencias para toda la cafetería; el costo de cada entrada lo captura un usuario con permiso; costo y margen por platillo visibles solo para quien administra productos, con promedio ponderado; el frontend prueba el modo Orchestrator (Argon) de AntiGravity.

F6 y B5 corren en paralelo: no comparten archivos.

---

## F6 — AntiGravity, modo Orchestrator (Argon) (`main`)

Bloque corto: ajustar la prueba de I2 al folio nuevo de B4 y dejar la batería GUI en verde antes de construir Inventario. Primer bloque en el modo Orchestrator (Argon).

```text
Eres el agente de frontend de este proyecto. Si esta es una sesion nueva, lee primero AGENTS.md completo y docs/coordinacion-agentes.md. No leas nada mas para este bloque.

Bloque F6, pequeno y mecanico. No hace falta plan previo. Trabaja SOLO en <copia principal>, rama main. No hagas commits. No compiles ni corras pruebas antes de editar: main compila y CTest pasa 5/5 en build_release y build_debug.

Contexto: el backend integro el bloque B4. El folio de una venta nueva ya no es V-10001: ahora es V-<codigo de caja>-<5 digitos>, por ejemplo V-C1-00001. Los folios antiguos siguen siendo validos. tests/test_i2_integration.py todavia busca la venta con el texto fijo V-10001 (hacia la linea 980: WHERE folio='V-10001', y su mensaje de error; el comentario hacia la linea 756 tambien lo menciona), asi que esa prueba falla hoy.

Archivo que puedes editar, y ningun otro: tests/test_i2_integration.py
No toques src/, include/, migrations/, CMakeLists.txt, tests/*.cpp, otras pruebas ni documentos. Otro agente esta trabajando en el backend.

Trabajo:
1. Sustituye la busqueda por el texto fijo V-10001 por el folio real de la primera venta, que la prueba ya lee en la variable folio1. Usa consulta parametrizada, no concatenes texto en el SQL. Actualiza el mensaje de error y el comentario.
2. Agrega una asercion nueva: el folio de cada venta nueva de la prueba cumple el formato V-, de 1 a 4 caracteres A-Z o 0-9, guion, y exactamente 5 digitos; y dos ventas distintas tienen folios distintos.
3. No cambies nada mas: ni tiempos de espera, ni otras aserciones, ni umbrales. Prohibido relajar o quitar una asercion para que pase.

Escalado de pantalla: las pruebas GUI solo son validas con Windows al 100 por ciento (96 DPI). Compruebalo antes de ejecutar; si no es 100 por ciento, detente y reportalo.

Verificacion: ejecuta la bateria GUI completa, una prueba a la vez y en este orden. Antes de cada una comprueba con tasklist que no haya ningun app_desktop.exe corriendo; si lo hay, espera 30 s y vuelve a comprobar:
   python -u tests/test_i1_integration.py
   python -u tests/test_i2_integration.py
   python -u tests/test_fe08_fe09_visual_integration.py
   python -u tests/test_f1_roles_panel.py
   python -u tests/test_f2_audit_log.py
   python -u tests/test_f2b_subnav.py
No ejecutes tests/capture_visual_qa.py. Si una prueba falla, repitela UNA vez a solas. Si falla dos veces y no es por tu cambio en test_i2_integration.py, NO la arregles: copia el error textual y continua con la siguiente. No instales nada.

Entrega corta y literal: lineas cambiadas en test_i2_integration.py; el folio real que genero la prueba; por cada prueba, comando, codigo de salida y si paso a la primera, a la segunda o fallo dos veces con su error textual; escalado de pantalla; limites de lo verificado. No escribas que algo funciona si no lo ejecutaste.
```

---

## B5 — Claude Code Terminal (`backend/b5`, desde `main` con B4)

Primer bloque de backend de I3: mermas y ajustes, movimientos, costo y margen por receta, y dos pendientes de B4. Con plan previo.

```text
Bloque B5 de backend, primero de Inventario (I3). Si esta es una sesion nueva, lee primero AGENTS.md completo y docs/coordinacion-agentes.md. Trabaja SOLO en <worktree del backend>, rama backend/b5 (ya creada desde main, incluye B4 y la migracion 010). No hagas commits. No compiles ni corras pruebas antes de editar: la linea base esta verificada (CTest 5/5 en build_be_release y build_be_debug).

Decisiones de negocio ya tomadas por el usuario (docs/specs.md, seccion 9):
- Sin sobreventa: sin existencias suficientes la venta se bloquea (ya lo hace el servicio de ventas). Ninguna operacion puede dejar existencias negativas.
- Existencias y avisos de poco inventario por ingrediente para toda la cafeteria. No hay almacen ni barra: no implementes ubicaciones ni traslados.
- El costo de cada entrada lo captura un usuario con permiso (register_purchase_entry ya guarda costo total y unidades base exactos).
- Costo y margen estimados por platillo o bebida: promedio ponderado, visibles solo para quien tiene inventory.manage_products.
- Combos, promociones y productos por peso NO estan decididos: quedan fuera de este bloque.

Estado actual que debes conocer:
- IInventoryService ya tiene alta, edicion, listado y consulta de ingredientes, get_stock_alerts, update_low_stock_threshold y register_purchase_entry. ICatalogService ya tiene productos, imagenes y recetas (get_recipe, set_recipe). RecipeDto ya trae estimated_cost_per_portion: revisa como se calcula hoy.
- stock_movements tiene id, ingredient_id, movement_type (PURCHASE, SALE, ADJUSTMENT, WASTE, TRANSFER), quantity_base_units, reason, created_at y created_at_utc. No guarda quien hizo el movimiento.
- Existe el permiso inventory.adjust (Registrar mermas y ajustes) y no lo usa ningun servicio.

Archivos permitidos: include/app/ (contratos incluidos), src/domain/, src/storage/, src/services/, migrations/ (solo archivos nuevos, a partir de 011), tests/test_*.cpp y CMakeLists.txt (solo bibliotecas y pruebas; no toques la lista de fuentes de app_desktop). No toques src/frontend/, tests/*.py, src/crypto/ ni migraciones 001 a 010.

Contratos: solo cambios aditivos. No cambies firmas ni campos existentes de include/app/contracts/: el frontend compila contra ellos y el otro agente esta trabajando en main. Puedes agregar metodos y DTO nuevos; el mock debe implementarlos de forma coherente. Si necesitas cambiar algo existente, detente y reportalo.

Presenta primero un plan para revision y espera aprobacion antes de escribir codigo. El plan debe resolver los puntos marcados como DECIDIR.

1. Mermas y ajustes. Operacion nueva para registrar una merma (WASTE) o un ajuste (ADJUSTMENT, que puede sumar o restar) sobre un ingrediente, con motivo obligatorio. Requiere inventory.adjust, comprobado dentro del servicio. En una sola transaccion: movimiento, existencia actualizada y evento de auditoria. Rechaza cantidades cero, desbordamientos y cualquier resultado negativo. DECIDIR: forma del contrato (una operacion con tipo o dos operaciones) y si la cantidad se captura en unidad base o en la unidad de compra con conversion.

2. Movimientos de existencias. Consulta nueva que lista movimientos con filtros por ingrediente, tipo y rango de fechas, con limite, mas recientes primero; requiere inventory.view. DECIDIR: agregar a stock_movements quien hizo el movimiento (migracion 011, NULL en filas anteriores) y si las ventas y compras ya existentes deben empezar a guardarlo.

3. Costo y margen.
   - Costo promedio ponderado por ingrediente a partir de las entradas de compra. Regla obligatoria de AGENTS.md: no dividir centavos entre unidades base y guardar el cociente redondeado; conserva costo total y cantidad, calcula con representacion racional o escala fija suficiente y redondea solo al obtener un importe final. Ejemplo obligatorio en las pruebas: 1 kg de limon por 50 pesos da 0.05 pesos por gramo y un costo no nulo por mg.
   - DECIDIR: promedio sobre todas las entradas registradas, o promedio movil sobre las existencias (se recalcula en cada compra con lo que queda en existencia). Explica la diferencia con un ejemplo numerico de dos compras a precios distintos con consumo intermedio, y recomienda una. El orquestador confirmara con el usuario.
   - Costo estimado de la receta por porcion y margen del producto (precio menos costo, y porcentaje). Un ingrediente sin entradas tiene costo DESCONOCIDO, no cero: la receta lo indica y no presenta un margen falso.
   - Estos datos solo se devuelven a quien tiene inventory.manage_products. DECIDIR: como se exponen (consulta nueva o campos nuevos) sin romper get_recipe actual ni mostrar costos a un cajero.
   - Sin float ni double en ningun importe, costo o conversion.

4. Pendientes de B4 (incluyelos en el plan):
   - generate_next_folio usa substr(folio, 1, n) = ?, que recorre toda la tabla sales en cada venta. Cambialo a una busqueda por rango que use el indice unico de folio, sin cambiar el formato ni el resultado. Las pruebas de folio existentes deben seguir pasando sin tocarlas.
   - create_verified_snapshot (sqlite_backup_service.cpp) no borra <destino>.tmp-wal y <destino>.tmp-shm en la ruta de exito. Corrigelo y agrega una prueba: tras un respaldo exitoso no queda ningun archivo .tmp junto al destino.
   - Fuera de alcance: el carrito y el reintento idempotente fijos a reg-01.

Reglas: migraciones nuevas versionadas, aplicadas una sola vez por el runner y sin efecto al ejecutarlo de nuevo; permisos comprobados dentro de cada servicio; no relajar pruebas existentes.

Pruebas CTest nuevas, con TEST_CHECK y no solo assert:
- merma y ajuste: existencia y movimiento correctos; motivo vacio rechazado; sin permiso rechazado; resultado negativo rechazado sin cambiar nada; auditoria registrada; fallo a mitad de transaccion no deja datos parciales;
- movimientos: filtros, orden y limite; una venta y una compra aparecen con su tipo;
- costo: ejemplo del limon; dos compras a precios distintos; ingrediente sin entradas da costo desconocido; receta con varios ingredientes; margen; un usuario sin inventory.manage_products no recibe costos;
- base con migraciones 001 a 010 y datos, migrada a la version nueva: datos intactos y segundo runner sin cambios;
- respaldo exitoso sin archivos .tmp residuales.
Las pruebas no deben dejar archivos en el directorio desde el que se ejecutan.

Verificacion. Antepon al PATH <toolchain>\mingw64\bin y <toolchain>\wxwidgets\lib\gcc1610_x64_dll:
   cmake --build build_be_debug --parallel 2
   ctest --test-dir build_be_debug --output-on-failure
   cmake --build build_be_release --parallel 2
   ctest --test-dir build_be_release --output-on-failure
No ejecutes pruebas GUI. No instales nada. Si un comando falla dos veces con el mismo error sin cambiar codigo, detente y reporta el error textual.

Entrega: archivos; comandos con codigo de salida; decisiones tomadas; metodos y DTO nuevos de los contratos con una linea de uso cada uno (el frontend los usara despues); numero de TEST_CHECK nuevos; limites de lo verificado.
```

---

## B5 — respuesta al plan de Claude Code Terminal (2026-11-13)

El usuario eligió el **promedio móvil sobre las existencias** (método B), no el promedio de todas las entradas que recomendaba el plan. El resto del plan se aprueba con ajustes.

```text
Plan de B5 aprobado con cambios. Respuestas: (1) el usuario eligio el metodo B, promedio movil sobre lo que queda en existencia, NO el A. (2) Si: get_recipe devuelve estimated_cost_per_portion en 0 a quien no tenga inventory.manage_products.

Puedes empezar ya con las secciones 1 (mermas y ajustes), 2 (movimientos) y 4 (pendientes de B4) tal como las planteaste, con los ajustes de abajo. Para la seccion 3 envia antes un anexo corto del plan con el diseno del metodo B y espera aprobacion; no escribas codigo de costos hasta entonces.

Requisitos del metodo B para tu anexo:
- La valuacion de las existencias de cada ingrediente se guarda como entero en una escala fija suficiente (por ejemplo micro-centavos), nunca como costo unitario redondeado, y sin float ni double.
- Compra: la valuacion sube por el costo total de la entrada. Consumo (venta, merma, ajuste negativo): baja de forma proporcional a la cantidad que sale, de modo que el costo promedio no cambia por consumir. Cuando la existencia llega a 0 la valuacion queda exactamente en 0: sin residuos por redondeo.
- Toda actualizacion de la valuacion va en la MISMA transaccion que el cambio de existencias, y por una sola funcion compartida que usen ventas, compras, mermas y ajustes.
- DECIDIR en el anexo: (a) ajuste positivo (aparece mercancia): a que costo entra; (b) que costo muestra una receta cuando un ingrediente tiene existencia 0 pero tuvo compras (ultimo promedio conocido o desconocido); (c) relleno de datos existentes en la migracion 011 y como se documenta que es aproximado; (d) que pasa con la valuacion si un ingrediente nunca tuvo compras y recibe un ajuste positivo.
- Sigue valiendo: costo desconocido nunca es 0; receta con algun ingrediente desconocido no da margen; consultas nuevas con Forbidden sin inventory.manage_products.
- Pruebas obligatorias ademas de las que listaste: el ejemplo 1000 g por 50 pesos, consumo de 800 g, compra de 1000 g por 100 pesos da 11000 centavos entre 1200 g (9.17 centavos por gramo al redondear solo al final); el ejemplo del limon; una merma no cambia el costo promedio; muchas salidas pequenas hasta existencia 0 dejan valuacion exactamente 0; una venta que falla y se revierte deja la valuacion intacta; base con datos migrada a 011.

Ajustes al resto del plan:
1. Agrega una consulta aditiva list_purchase_entries (filtros por ingrediente y fechas, limite, mas recientes primero) que devuelva las entradas de compra con su costo y quien las registro. La pantalla de Reabastecimiento la necesita. Requiere inventory.restock o inventory.manage_products.
2. El limite de 200 del motivo se cuenta en caracteres, no en bytes UTF-8.
3. Trigger de existencias negativas: aprobado como respaldo. El servicio debe seguir rechazando antes con un error claro; el trigger no sustituye esa validacion.
4. Respaldo: la verificacion de integridad del respaldo se hace DESPUES de pasar el temporal a journal_mode=DELETE, sobre el archivo final. Incluye en la prueba que el respaldo se restaura.
5. Folio por rango: aprobado como lo planteaste.
6. Contratos: solo aditivos, como propusiste. En la entrega lista cada metodo y DTO nuevo con una linea de uso, porque el frontend construira las pantallas con ellos.
```

---

## F6b — AntiGravity, modo Orchestrator (Argon) (`main`)

Diagnóstico y arreglo de `tests/test_i2_integration.py`: el orquestador la vio fallar 5 de 5 en el paso del diálogo de impresión, con la impresora predeterminada en la virtual de OneNote y también en Microsoft Print to PDF, mientras F6 la reportó aprobada.

```text
Bloque F6b: diagnostico y arreglo de una prueba. Trabaja SOLO en <copia principal>, rama main. No hagas commits. No compiles: no se toca codigo C++.

Hecho comprobado por el orquestador: python -u tests/test_i2_integration.py falla 5 de 5 en esta PC (Windows 11, escalado 100 por ciento), desde dos consolas distintas y con dos impresoras predeterminadas distintas (una virtual de OneNote y Microsoft Print to PDF). Siempre igual, en el requerimiento 4 y 7, antes de las lineas que cambiaste en F6:
   -> Dialogo del sistema detectado y cancelado con IDCANCEL (HWND: None)
   TimeoutError: [TIMEOUT 6.0s] Esperando ventana con titulo 'Impresion Cancelada' ... Ventanas visibles del proceso: ['Venta Registrada - Cafeteria ITCH II', 'Cafeteria ITCH II']
Durante la ejecucion existe un proceso PrintDialog.exe de Windows. Hipotesis: Windows 11 abre el dialogo de impresion en ese proceso aparte, y cancel_system_print_dialog_by_pid solo busca una ventana de clase #32770 dentro del proceso de la aplicacion. Ademas la prueba imprime detectado y cancelado aunque el identificador sea None. Tu informe de F6 dio esta prueba por aprobada: eso no coincide con lo anterior.

Archivo que puedes editar, y ningun otro: tests/test_i2_integration.py
No toques src/, include/, otras pruebas ni documentos. No cambies la configuracion de Windows ni la impresora predeterminada. No instales nada.

Paso 1, diagnostico SIN editar nada. Comprueba el escalado (100 por ciento) y que no haya app_desktop.exe corriendo. Ejecuta la prueba una vez guardando la salida completa en un archivo fuera del repositorio y reporta textualmente: el codigo de salida, la linea del dialogo del sistema y las ultimas 15 lineas. Si esta vez pasa, dilo y explica que fue distinto; no supongas.

Paso 2, confirma la causa con datos: mientras el dialogo de impresion esta abierto, enumera las ventanas de nivel superior visibles con su clase, titulo y nombre del proceso dueno, y reporta cual es el dialogo de impresion y a que proceso pertenece. No adivines.

Paso 3, arreglo en la prueba:
- cancel_system_print_dialog_by_pid debe encontrar el dialogo de impresion tanto si es una ventana #32770 del proceso de la aplicacion como si lo aloja otro proceso de Windows (segun lo que hallaste en el paso 2), identificandolo de forma que no pueda cerrar por error una ventana ajena a la prueba;
- lo cancela sin imprimir nada y espera a que desaparezca;
- despues de llamarla, la prueba debe fallar con un mensaje claro si no se encontro ningun dialogo. Prohibido imprimir detectado si el identificador es None;
- revisa si el mismo patron (seguir adelante con None) existe en los otros pasos de impresion de esta prueba y corrigelo igual;
- no cambies otros tiempos de espera, aserciones ni umbrales. Prohibido relajar o quitar una asercion. Si el aviso Impresion Cancelada de la aplicacion no aparece aun cancelando bien el dialogo, NO adaptes la prueba: detente y reportalo, porque seria un defecto de la aplicacion.

Paso 4, verificacion: ejecuta python -u tests/test_i2_integration.py tres veces seguidas, a solas, y reporta el codigo de salida de cada una. Al terminar comprueba que no quede ningun app_desktop.exe ni dialogo de impresion abierto.

Entrega literal: salida textual del paso 1; tabla de ventanas del paso 2; lineas cambiadas; codigos de salida de las tres ejecuciones; folio real generado; limites. Explica ademas por que tu informe de F6 dio esta prueba por aprobada. No escribas que algo funciona si no lo ejecutaste.
```

---

## B5 — respuesta al anexo de costos (2026-11-13)

Anexo del promedio móvil aprobado, con las decisiones (a) a (d) tal como las propuso Claude. La entrega parcial (secciones 1, 2 y 4) se audita junto con la sección 3 al final del bloque.

```text
Anexo de la seccion 3 aprobado, incluidas las decisiones (a), (b), (c) y (d) tal como las propones. Adelante con el codigo de costos. Ajustes:

1. estimated_cost_per_portion de get_recipe queda con su calculo antiguo solo por compatibilidad con las pruebas que fijan su valor. Marca el campo como obsoleto en un comentario del DTO e indica que las pantallas nuevas deben usar get_recipe_costing. No lo elimines ni cambies su calculo.

2. Pruebas adicionales a las de tu lista:
   - invariantes tras una secuencia larga y mezclada de compras, ventas, mermas y ajustes: la valuacion nunca es negativa; con existencia 0 la valuacion es exactamente 0; y el promedio vigente queda siempre entre el costo unitario minimo y maximo de las compras que lo forman;
   - el relleno de la migracion 012 con numeros concretos: un ingrediente con dos compras a precios distintos y existencia parcial, uno sin compras y uno con existencia 0;
   - desbordamiento: una compra o una valuacion que no cabe se rechaza con error y no cambia nada;
   - la regla (d) con existencia previa sin costo y la compra de costo 0.

3. La funcion compartida de existencias y valuacion es la UNICA que escribe current_stock. Comprueba con una busqueda que no queda ningun otro UPDATE de current_stock en src/ y reporta el resultado de esa busqueda.

4. El valor de las existencias y los costos solo salen por las consultas que exigen inventory.manage_products. Verifica que list_ingredients, get_ingredient, get_stock_alerts y list_stock_movements no los exponen.

5. Entrega final de B5 completa (secciones 1 a 4): archivos, comandos con codigo de salida, contratos nuevos con una linea de uso, numero de TEST_CHECK nuevos y limites. No hagas commits.
```
