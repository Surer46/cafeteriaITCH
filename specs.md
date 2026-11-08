# Especificación de interfaz y funciones — Cafetería ITCH II

## 1. Punto de partida y alcance

Referencia visual: [`gemini-svg(1).svg`](gemini-svg(1).svg), maqueta estática de 1360 × 840 px. El archivo muestra una pantalla de punto de venta con menú lateral, búsqueda por código o nombre, cantidad, tabla de artículos, total, botón de cobro y acciones de caja. No contiene componentes interactivos, persistencia, reglas de negocio ni otras pantallas. Los importes y productos son datos de ejemplo.

Objetivo: implementar una aplicación operativa para ventas, inventario y administración de cafetería, conservando la identidad rojo/amarillo de la maqueta y haciendo que las acciones tengan estados, validaciones, permisos y registros verificables.

**Seguimiento del desarrollo:** esta especificación conserva el comportamiento requerido. La sección 12 distingue lo implementado de lo pendiente en la revisión del 2026-09-29; los checklists ejecutables están en `frontend/plan.md` y `backend/plan.md`, y la evidencia/pendientes concretos en sus respectivos `task.md`. Una pantalla o una prueba de servicio no equivale a aceptar todo el módulo.

**Restricción de hardware:** la caja debe funcionar con una PC que puede tener, como máximo, un Intel Core i5 de segunda generación. Se estima que dispone de **4 a 6 GB de RAM**; es una hipótesis de diseño, no un dato confirmado. Asumir recursos limitados de CPU, memoria y almacenamiento; no exigir GPU dedicada. El sistema operativo queda **sin determinar** hasta verificar el equipo. Registrar RAM real, tipo de disco, sistema operativo y resolución antes de elegir el empaquetado final.

## 2. Lectura de la maqueta

| Zona actual | Contenido visible | Función esperada |
| --- | --- | --- |
| Barra superior | Nombre del sistema y controles decorativos de ventana | Identidad, contexto y sesión; controles de ventana solo si es aplicación de escritorio. |
| Barra lateral izquierda | Inventario, ventas, promociones, reabastecimiento, altas, bajas, existencias de barra, bitácora, reportes/corte | Navegación entre módulos. |
| Pie lateral | “Caja Principal • Abierta”, “Usuario: Cajero 01” | Estado de caja, turno y usuario actual. |
| Panel superior central | Código de barras o nombre, cantidad, Agregar | Encontrar producto y agregarlo a la venta. |
| Tabla central | SKU, descripción, precio unitario, cantidad y subtotal | Cuenta o carrito de la venta activa. |
| Panel derecho | Total, Cobrar ticket, quitar artículo, cancelar orden, corte rápido y catálogo rápido | Resumen y acciones de la venta. |

La maqueta contiene detalles que deben corregirse al implementar: “Chocolaleche” requiere validar el nombre real del catálogo; una cantidad de 116 en una venta necesita confirmación o control de error; el total de ejemplo sí coincide con $30.00 + 116 × $25.00 = $2,930.00, pero los importes deben formatearse como `$2,930.00`. El SVG usa textos y formas sin semántica accesible: se debe construir la interfaz con controles nativos, no usar el SVG entero como pantalla funcional.

## 3. Redistribución propuesta

### Escritorio

```text
┌──────────────────────────────────────────────────────────────────────────────┐
│ Cafetería ITCH II                       Caja Principal · Abierta  Cajero 01 │
├──────────────────┬─────────────────────────────────────────┬─────────────────┤
│ Punto de Venta   │ [Venta] [Catálogo visual]                │ CUENTA ACTUAL   │
│ Inventario       │ Buscar código, nombre o escanear         │ 3 artículos     │
│ Caja y Reportes  │ Cantidad [1]  [Agregar]  [Catálogo]      │ Subtotal        │
│ BackOffice       ├─────────────────────────────────────────┤ Descuento       │
│                  │ SKU  Producto  Precio  Cant.  Subtotal   │ TOTAL           │
│                  │ ...                                     │ [Cobrar]        │
│                  │                                         │ Quitar/Cancelar │
└──────────────────┴─────────────────────────────────────────┴─────────────────┘
```

- La **sidebar contiene exactamente cuatro secciones principales**: **Punto de Venta**, **Inventario**, **Caja y Reportes** y **BackOffice**. No lista subsecciones. Seleccionar una sección cambia la barra horizontal de subsecciones situada **en el área central, debajo del encabezado con el nombre de la cafetería y encima de la búsqueda o contenido del módulo**. La subsección activa se distingue visualmente; si alguna no cabe, la barra permite desplazamiento horizontal accesible, sin añadir elementos a la sidebar.
- **Mapa de subsecciones:** Punto de Venta → Venta, Catálogo visual; Inventario → Existencias, Reabastecimiento, Productos y recetas, Ingredientes, Promociones y combos, Barra; Caja y Reportes → Caja y turnos, Bitácora, Reportes; BackOffice → Usuarios y cuentas, Roles y permisos, Configuración, Respaldos, **Servidor de respaldos**. El catálogo visual comparte el carrito de Venta. En módulos sin búsqueda, la barra de subsecciones permanece bajo el encabezado y encima del contenido correspondiente.
- **BackOffice** reúne alta, edición, desactivación y consulta de cuentas del sistema; roles configurables y sus permisos por acción; configuración operativa; y respaldos. El alta de una cuenta solicita correo electrónico, contraseña elegida para esa cuenta, rol y avatar opcional. **Servidor de respaldos** es una subsección propia de BackOffice visible y editable únicamente para la identidad Desarrollador; contiene destino, credenciales/certificados, retención y prueba de conexión. El Dueño puede activar copias y ver su estado en **Respaldos**, pero no editar esa configuración técnica.
- **Mover la zona de “cuenta” arriba a la derecha**: el panel derecho comienza en la primera fila del área de trabajo y muestra “Cuenta actual”, total y botón **Cobrar** sin desplazamiento. El encabezado global, también arriba a la derecha, muestra **caja, estado de turno y usuario**. Así quedan visibles los dos significados posibles de “cuenta” de la maqueta: la cuenta de la venta y la cuenta/sesión del cajero.
- Colocar **Consultar catálogo** junto al campo de búsqueda, pues ayuda a seleccionar un producto antes de agregarlo. Abrirlo como panel o diálogo con búsqueda y selección.
- Situar **Quitar artículo** como acción contextual en la fila seleccionada, además de un botón accesible junto a la tabla; deshabilitarlo sin selección. Mantener **Cancelar orden** separado y con confirmación.
- Trasladar **Corte de turno rápido** a **Caja y Reportes → Caja y turnos**, accesible desde el indicador de caja del encabezado. Evitar que quede mezclado con acciones de la venta.
- Unificar **Alta productos y recetas** y **Dar de baja un producto** en **Inventario → Productos y recetas**. “Dar de baja” será cambiar el estado del producto a inactivo, con confirmación y registro; no borrar ventas históricas.
- Mostrar solo secciones y subsecciones permitidas para el rol. Un usuario que solo puede vender ve Punto de Venta; la falta de permiso también se valida en los servicios internos.
- En pantallas estrechas, el menú principal pasa a un panel plegable y la barra de subsecciones se desplaza horizontalmente; la cuenta se conserva como tarjeta superior compacta y el botón Cobrar permanece visible. La tabla puede desplazarse horizontalmente sin ocultar la descripción y el total.
- Añadir en el punto de venta un selector **Lista / Catálogo visual**. El catálogo visual muestra categorías y tarjetas con nombre, precio, disponibilidad e imagen opcional, como un menú. Al seleccionar una tarjeta se agrega el artículo al mismo carrito de la venta; la lista y la búsqueda por código comparten ese carrito y sus cálculos. La vista Lista será la predeterminada en la PC de bajos recursos; el Dueño puede activar la vista visual y cada cajero puede elegir cuál usar.

### Jerarquía y estados visuales

- La acción primaria de la pantalla es **Cobrar** en verde. **Agregar** usa el rojo de marca. Las acciones destructivas usan texto/borde rojo y piden confirmación.
- El total debe tener la mayor jerarquía numérica. Mostrar subtotal, descuentos e impuestos únicamente si aplican a la configuración real; no inventar cargos.
- Cada control debe tener estados normal, foco, hover, deshabilitado, cargando y error. Confirmaciones y errores deben explicar el siguiente paso.
- Evitar sombras difusas, brillos, transparencias, animaciones continuas y fondos complejos del SVG en la versión operativa. Conservar colores, jerarquía y separación mediante controles y dibujo nativos sencillos; limitar la cantidad de elementos de tabla renderizados simultáneamente.
- Mantener contraste legible, indicadores que no dependan solo del color, objetivos táctiles de al menos 44 × 44 px y navegación por teclado. Las etiquetas e iconos deben tener nombre accesible.

### Dirección visual Cupertino y Material — 2026-09-30

Adoptar composición inspirada en Cupertino con superficies claras, tarjetas y controles redondeados, separadores discretos y subsecciones segmentadas; conservar identidad rojo/amarillo del SVG, Roboto y Material Symbols Outlined. El contrato visual completo de colores, tamaños, estados, recursos locales y QA está en `frontend/design-system.md`. Mantener cuatro secciones, cuenta/cobro arriba a la derecha y perfil táctil reorganizado. Implementación nativa C++17/wxWidgets; el entorno de desarrollo más potente no elimina la restricción del i5/4–6 GB de RAM ni los pendientes de integridad. Este diseño queda especificado, todavía no implementado ni aceptado visualmente.

### Modo táctil para monitores grandes

- Ofrecer dos perfiles de interfaz: **Estándar** y **Táctil**. El modo táctil reorganiza el espacio, la densidad y los formularios; no se limita a escalar botones. Permitir seleccionarlo en **BackOffice → Configuración**, conservar la preferencia del puesto al reiniciar y cambiar de perfil sin perder el carrito. La presencia de una pantalla grande no implica que sea táctil: el perfil se elige explícitamente.
- Mantener las cuatro secciones laterales y las subsecciones en la barra central. Aumentar el área de pulsación y separar las opciones; cuando no quepan, conservar la subsección activa y ofrecer **Más subsecciones** mediante un selector nativo de opciones grandes. No reducirlas a botones pequeños ni duplicarlas en la sidebar. Repartir el ancho entre navegación, catálogo/lista y cuenta en función del área útil y escalado reales.
- Como punto de partida, usar objetivos táctiles de al menos **48 × 48 unidades lógicas**, preferentemente 56–64 para Agregar/Cobrar y controles frecuentes, separación de 8–12 entre acciones y bloques de contenido de 16–24. Texto principal de 16–20 y total con jerarquía mayor, ajustables según legibilidad real. Estas medidas son de diseño y se validan con DPI y tamaño físico del monitor, sin suponer que los píxeles de la captura equivalen al área física de toque.
- En Venta, agrupar el trabajo en **selección del producto**, **pedido actual** y **resumen/cobro**. Mantener Cuenta actual y Cobrar arriba a la derecha. Mostrar principalmente Buscar, Agregar y Cobrar; al seleccionar una línea, presentar un área contextual con cantidad, nota y quitar. Evitar repetir tres o cuatro botones en cada renglón. Cancelar pedido y otras acciones poco frecuentes van en **Más acciones**, con texto claro y confirmación cuando corresponda. No esconder información necesaria para decidir el cobro.
- En el catálogo visual, usar tarjetas amplias con nombre, precio, disponibilidad e imagen opcional y una única acción clara de agregado. Ajustar columnas al espacio útil, paginar o cargar solo lo visible y mantener acceso a la vista Lista. Evitar saturar la pantalla mostrando todas las categorías, tarjetas y acciones administrativas a la vez.
- En cantidades y efectivo, ofrecer captura numérica cómoda; los controles de cantidad respetan las validaciones del servicio. El teclado en pantalla, nativo o integrado ligero según la plataforma confirmada, aparece al enfocar un campo cuando se necesita y no cubre el campo activo, el total ni la confirmación. Conservar teclado físico/escáner. Ninguna acción depende exclusivamente de hover, clic derecho, doble clic o gestos difíciles de descubrir.
- Organizar formularios de Inventario y BackOffice por grupos claros, usando secciones o pasos cuando haya muchos campos. Mostrar acciones sobre una cuenta o producto tras seleccionarlo, en vez de añadir muchos botones por fila. Cada toque debe dar respuesta visible; impedir activaciones repetidas durante operaciones en curso y conservar mensajes de validación junto al campo afectado.
- Antes de implementar, producir una distribución revisable de Venta, cobro y cuentas en ambos perfiles, con pocos controles primarios y jerarquía clara. Verificar después en el monitor táctil real alcance, precisión de pulsación, legibilidad, espacios vacíos útiles y ausencia de solapamientos al abrir teclado o cambiar el escalado. El modo táctil sigue pendiente; los controles actuales no satisfacen este requisito por usar wxWidgets.

## 4. Comportamiento del punto de venta

### Captura de artículos

1. Al abrir **Vender**, enfocar la búsqueda. Admitir entrada manual y lector de código de barras que actúe como teclado.
2. Buscar por SKU/código exacto y por nombre parcial. Mostrar coincidencias con nombre, precio, disponibilidad y estado. No permitir agregar productos inactivos.
3. La cantidad inicia en 1; aceptar enteros positivos para productos por pieza. Si se venden por peso, definir unidad, precisión y reglas antes de habilitar cantidades decimales.
4. Al agregar, calcular precio vigente, descuentos aplicables y subtotal. Si el mismo producto ya existe, incrementar su cantidad según la regla de precio/promoción. Limpiar la búsqueda y devolverle el foco.
5. Permitir modificar cantidad de una línea y quitarla. Recalcular totales inmediatamente. Para existencias insuficientes, mostrar el disponible y bloquear o solicitar autorización según la política definida.
6. La venta en curso se conserva ante navegación accidental o cierre y reapertura de la aplicación; al retomarla, validar de nuevo precios y existencias antes de cobrar.
7. El **carrito de pedido** admite varias líneas y combinaciones, por ejemplo dos platillos y una bebida, con cantidad individual por línea. Muestra el número total de artículos y el total acumulado; agregar un artículo no reemplaza los anteriores. Las tarjetas del catálogo, el escáner y la búsqueda manual alimentan el mismo carrito.
8. Cada línea del carrito permite agregar, editar o quitar una **nota opcional para ese producto** antes de confirmar el cobro, por ejemplo «sin azúcar». La nota se muestra junto a la línea y después debajo de ese producto en el ticket. Si dos unidades del mismo producto requieren notas distintas, deben conservarse como líneas separadas; solo se agrupan líneas con la misma nota y la misma configuración de precio/promoción.

### Cobro

1. **Cobrar** solo se habilita con carrito válido y caja/turno abierto.
2. Mostrar resumen final y registrar el **método de pago declarado** (por ejemplo, efectivo, tarjeta o transferencia). Por ahora el sistema solo guarda ese dato: **no procesa, autoriza ni comprueba pagos electrónicos**. En efectivo, capturar importe recibido y calcular cambio. Definir si se admiten pagos mixtos, descuentos manuales, impuestos y propinas antes de implementarlos.
3. Confirmar cobro una sola vez: guardar venta y pagos, descontar existencias o insumos según receta, registrar movimientos y generar número único de ticket en una operación atómica. Una repetición por doble clic o reintento no debe duplicar la venta.
4. Tras confirmar la venta, mostrar folio y preguntar explícitamente **«¿Deseas imprimir el ticket?»** con acciones **Imprimir** y **Ahora no**. La respuesta no modifica ni repite el cobro. Permitir reimpresión posterior desde el historial de ventas según permiso. Si no hay impresora o falla la impresión, la venta permanece registrada y se informa que puede reimprimirse.
5. Cancelar una **orden sin cobrar** vacía el carrito tras confirmar. Anular o devolver una **venta cobrada** es un flujo distinto con permisos, motivo y movimientos de reversión; nunca se elimina el registro original.

### Ticket de compra y notas por producto

- El ticket usa los datos confirmados de la venta local: nombre de la cafetería, folio, fecha y hora, productos comprados, cantidad, precio unitario y subtotal de cada línea, descuentos/impuestos solo si corresponden, total y método de pago **declarado**. En efectivo, mostrar recibido y cambio. No imprimir credenciales, datos técnicos ni información de pago sensible.
- Imprimir cada nota debajo del producto al que pertenece, con una etiqueta clara como «Nota: sin azúcar». La nota es texto libre breve, opcional, con límite inicial de **250 caracteres**, validación de caracteres de control y representación segura en pantalla e impresora. Una nota no altera por sí sola el precio, la receta ni el descuento; las modificaciones de ingredientes con efecto en inventario requieren una función distinta con reglas explícitas.
- Persistir la nota en el borrador y copiarla como **instantánea inmutable de la línea de venta** al confirmar. Reimprimir reconstruye el ticket desde esa venta guardada, con los mismos productos, importes y notas, aunque cambie después el catálogo. La nota de una venta confirmada no se edita desde el carrito.
- El diseño de impresión se adapta al ancho de papel de la impresora confirmada para la caja; el tipo de conexión, ancho y controlador se validarán en el equipo objetivo. Generar solo las líneas de la venta, sin cargar imágenes del catálogo ni bloquear innecesariamente la interfaz.
- **Perfiles de impresión térmica — decisión del 2026-10-02:** admitir y permitir alternar entre anchos nominales de **55 mm y 80 mm** desde **BackOffice → Configuración → Impresión**, con permiso de configuración operativa. Guardar por puesto la impresora elegida y el perfil activo mediante un contrato/servicio de configuración, para conservarlos al reiniciar. El cambio afecta el formato de próximas impresiones y reimpresiones, sin alterar ventas ni datos históricos del ticket.
- Distinguir ancho nominal del papel y área imprimible real del controlador. Ajustar márgenes, columnas, envoltura, paginación y vista previa al perfil seleccionado y a las métricas reales de la impresora; comprobar compatibilidad del papel configurado antes de imprimir y mostrar un error claro si el área disponible no permite un ticket legible. Verificar ambos perfiles con productos, notas, acentos e importes largos, además de cancelación/error sin alterar la venta. La validación física de 55 mm y 80 mm queda pendiente de I2; no asumir equivalencia de 55 mm con las pruebas existentes de 58 mm.

### Atajos sugeridos

`Enter`: elegir/agregar cuando el foco esté en búsqueda; `F2`: enfocar búsqueda; `F4`: abrir cobro; `Esc`: cerrar diálogo. Los atajos son configurables y no deben interferir con campos o tecnologías de asistencia.

## 5. Módulos funcionales

| Módulo | Funciones mínimas | Validaciones y resultado |
| --- | --- | --- |
| Inventario general | Buscar, filtrar, consultar existencias por producto/ubicación y movimientos. | Mostrar unidad, mínimos y fecha de actualización; distinguir disponible, reservado y agotado si aplica. |
| Productos, platillos y bebidas | Crear/editar producto, SKU, código de barras, categoría, unidad, precio, imagen opcional y estado; marcar si es artículo directo o preparado. Para cada platillo/bebida, capturar ingredientes y cantidad por porción con unidad. | SKU/código únicos; receta con al menos un ingrediente y cantidades positivas; baja lógica; cambios de receta y precio con fecha/usuario. Las imágenes inválidas o grandes se rechazan antes de guardar. |
| Ingredientes | Crear/editar ingrediente, unidad base, existencias y umbral de bajo stock por ingrediente. | Solo el dueño configura el umbral; mostrar alerta cuando la existencia llegue o baje de él. Registrar quién cambió el umbral y cuándo. |
| Promociones y combos | Crear reglas con vigencia, productos participantes, límites y prioridad. | Mostrar descuento aplicado en la cuenta; impedir reglas incompatibles o ambiguas. |
| Reabastecer | Registrar entrada de productos/ingredientes, proveedor, cantidad, unidad de compra, **costo total pagado** y costo unitario calculado; referencia y ubicación opcionales. Ejemplo: 1 kg de limón por $50.00 = $0.05 por gramo. | Movimiento con usuario y fecha; costo y cantidad positivos; conservar costo histórico de cada entrada; no modificar existencias directamente sin asiento. |
| Existencias de barra | Ver insumos y preparados disponibles en barra; registrar merma, traslado y ajuste. | Motivo obligatorio en ajustes; límites y permisos para saldos negativos. |
| Bitácora | Consultar ventas, cobros, entradas, ajustes, bajas, anulaciones y cambios sensibles. | Filtros por fecha, usuario, tipo y folio; registro inmutable para auditoría. |
| Reportes | Ventas por periodo, producto y método de pago; inventario, mermas y movimientos; exportación. | Totales reconciliados con ventas y movimientos; zona horaria local. |
| Caja y turnos | Abrir turno con fondo inicial, registrar entradas/salidas, consultar saldo esperado y cerrar con arqueo. | Impedir dos turnos activos incompatibles; diferencia y motivo documentados; corte/reporte imprimible. |
| BackOffice | Crear cuentas con correo, contraseña, rol asignado y avatar opcional; editar/desactivar cuentas, gestionar roles configurables, permisos, configuración operativa y respaldos. | Validar correo único y contraseña; Dueño administra cuentas y roles; la última cuenta Dueño activa se protege; configuración técnica del servidor remoto solo para Desarrollador. |

## 6. Datos y reglas transversales

- Entidades base: **Usuario, Rol, Permiso, Caja, Turno, Producto, Ingrediente, Receta, Componente de receta, Inventario por ubicación, Entrada de compra, Movimiento de inventario, Promoción, Venta, Línea de venta, Pago registrado, Movimiento de caja y Evento de auditoría**.
- En el encabezado, **«Usuario:» muestra el nombre de usuario (`username`) de la cuenta autenticada**, obtenido del servicio de cuentas y actualizado al cambiar de sesión. Esta indicación identifica la cuenta; no usa el nombre completo (`display_name`). El nombre visible conserva su función en gestión de cuentas y en la instantánea histórica del cajero del ticket.
- Guardar importes en centavos enteros y costos proporcionales mediante costo total/cantidad original o representación racional, nunca en coma flotante binaria. Presentar moneda MXN y hora de `America/Mexico_City`; conservar marcas temporales con zona horaria.
- Una línea de venta conserva instantáneas de nombre, precio, cantidad, descuentos, impuestos y nota opcional, para que la historia y la reimpresión no cambien si se edita el catálogo.
- La disponibilidad de un producto preparado se deriva de su receta y existencias de insumos, según política operativa. Los descuentos, impuestos y redondeos deben tener reglas explícitas y pruebas con ejemplos reales.
- Los roles serán **configurables**: el dueño puede crear, editar y desactivar roles, y asignar permisos específicos por acción y módulo (ver, crear, editar, cobrar, anular, ajustar, cortar, gestionar usuarios, roles y respaldos). Los permisos se comprueban en la capa de servicios de la aplicación en cada operación, no solo al habilitar controles de la interfaz.
- Debe existir siempre una cuenta activa con rol **Dueño**, acceso a todas las funciones operativas y administrativas del negocio y registro en la base de datos local. El alta inicial solicita correo electrónico y contraseña elegida por el Dueño, sin credenciales predeterminadas. Impedir eliminar, desactivar o quitar el rol a la última cuenta Dueño activa. Registrar los cambios de usuarios, roles y permisos. La configuración técnica protegida del servidor de respaldos, descrita abajo, es la única excepción a este acceso del Dueño.

### Gestión de cuentas y avatares

- En **BackOffice → Usuarios y cuentas**, el Dueño puede crear una cuenta y asignarle un rol operativo existente. El formulario exige nombre de usuario, nombre visible, correo electrónico y contraseña con confirmación; permite elegir un avatar opcional. La contraseña se almacena únicamente como hash seguro. El rol y los permisos se aplican en los servicios, no solo en la interfaz. El correo no obliga a disponer de internet ni cambia por sí solo el identificador usado para iniciar sesión.
- Validar sintaxis razonable del correo y unicidad sin distinguir mayúsculas/minúsculas; normalizarlo para comparar, sin alterar necesariamente su presentación. Permitir editar el correo y sustituir o quitar el avatar con permisos de gestión de cuentas. Mostrar un avatar predeterminado o iniciales cuando no se haya elegido imagen.
- SQLite admite bytes de imágenes mediante `BLOB`. Guardar el avatar optimizado en una tabla separada `user_avatars`, relacionada uno a uno con `users`, para que las consultas de cuentas no carguen imágenes innecesariamente. Admitir JPEG, PNG o WebP tras verificar el formato real; limitar dimensiones y tamaño del archivo, generar una miniatura (objetivo inicial: hasta 256 × 256 px y 100 KB) y cargarla solo donde sea visible. La copia consistente de SQLite incluye estos avatares.
- La migración `004_user_email_and_avatar.sql` ya agregó `users.email` y `users.avatar_blob`, y los contratos/servicios reciben esos datos. Su implementación sigue **parcial**: asigna correos inventados a las cuentas antiguas, compara correos distinguiendo mayúsculas y guarda el avatar en la fila del usuario. Corregirlo mediante **una nueva migración versionada**, sin modificar migraciones ya aplicadas y con respaldo consistente previo. No inventar un correo de contacto: permitir transitoriamente correo sin completar en cuentas antiguas y solicitar su actualización, conservando su acceso. Separar el avatar en `user_avatars`, migrar sus bytes, crear unicidad para correo normalizado y completar validación/optimización de imágenes. Las altas nuevas exigen un correo válido elegido por el usuario.
- Requerir autenticación, contraseñas almacenadas con hash seguro, bloqueo de sesión por inactividad, trazabilidad de cambios y protección de datos personales. No registrar contraseñas ni datos sensibles de pago en la bitácora.
- La **lógica de negocio y la base de datos operativa deben ejecutarse dentro de la aplicación de escritorio**, sin depender del servidor externo ni requerir la instalación separada de un motor de base de datos. SQLite se incluye con la aplicación; no existe un servidor local adicional. La instalación o copia de la aplicación debe preparar automáticamente archivos, esquema y migraciones, sin pedir al usuario instalar SQLite, otro servicio o dependencias manualmente. La compatibilidad exacta del paquete se decide tras conocer el sistema operativo real.
- La base debe guardarse en una ruta local configurable y escribible, fuera del directorio del ejecutable cuando este sea de solo lectura. Implementar respaldo desde la aplicación a una ubicación elegida en la máquina o unidad externa, con nombre/fecha y verificación de integridad; restauración guiada con advertencia y copia de seguridad previa. Nunca copiar el archivo de la base mientras haya escrituras activas sin usar el mecanismo de respaldo consistente del motor.
- El sistema debe funcionar sin conexión para sus funciones principales. El servidor externo **solo recibe copias de respaldo**; nunca atiende ventas, consultas del catálogo ni escrituras operativas. No hay sincronización bidireccional ni segunda base activa en el alcance actual.

### Respaldos opcionales en servidor externo

- El Dueño puede **activar o desactivar** el respaldo remoto, iniciar una copia manual y ver fecha, resultado y último error de las copias. Si está activado, la aplicación envía copias en segundo plano según una frecuencia definida; una falla de red o del servidor se informa y se reintenta sin bloquear ventas ni afectar la base local.
- La pantalla de configuración técnica del destino remoto dentro de la aplicación —dirección del servidor, credenciales, certificados, política de retención y prueba de conexión— queda **reservada al Desarrollador**. No se concede mediante roles configurables por el Dueño ni se habilita por ocultar un botón: la capa de servicios exige una identidad técnica independiente y auditada. No incluir cuentas ni contraseñas de desarrollador predeterminadas o incrustadas en el código. El mecanismo de alta y recuperación de esa identidad se definirá antes de implementarlo.
- Crear una instantánea consistente de la base antes de subirla; verificar integridad y resultado de la transferencia. Usar conexión cifrada, proteger credenciales en el equipo y cifrar las copias almacenadas en el servidor. Definir retención y eliminación para no llenar el servidor.
- La restauración desde una copia remota descarga y verifica el respaldo, crea una copia de seguridad del estado local actual y restaura **en la base local** mediante una acción autorizada. Nunca convertir el servidor de respaldos en el origen de datos de la operación diaria.

### Ingredientes, unidades, costos y alertas

- Cada ingrediente tiene **unidad base de inventario**: masa (mg, g o kg), volumen (ml o L) o piezas. Convertir automáticamente dentro de la misma dimensión (1 kg = 1,000 g; 1 g = 1,000 mg; 1 L = 1,000 ml). No convertir masa a volumen sin una densidad definida ni piezas a masa/volumen sin equivalencia específica. Usar cantidades decimales exactas y precisión definida por ingrediente.
- Una receta guarda ingrediente, cantidad y unidad **por porción**. La venta de varias porciones multiplica el consumo; el descuento de inventario ocurre al confirmar la venta y queda registrado como movimiento. Cambios posteriores de receta no alteran consumos de ventas anteriores.
- En cada entrada registrar cantidad y unidad comprada, costo total y costo unitario normalizado a la unidad base. Ejemplo: **1 kg de limón por $50.00 = $50.00/kg = $0.05/g**. Mostrar costo estimado por porción sumando cantidades de receta × costo unitario vigente. El método de valuación cuando existan compras a precios distintos queda pendiente de definir.
- El **Dueño** fija un umbral de bajo stock por ingrediente en su unidad base; el sistema alerta al llegar a `existencia ≤ umbral`, incluso si la cantidad es cero. Mostrar alertas en inventario y un indicador global; actualizar tras entradas, ventas, mermas y ajustes. Definir si el umbral es global o por ubicación cuando se confirme el manejo de almacén/barra. Evitar avisos repetidos de un mismo estado sin cambio relevante.

### Rendimiento para la PC objetivo

- Usar la arquitectura concreta de la sección 11: aplicación de escritorio nativa en C++ con wxWidgets y SQLite embebido. Evitar navegador, servidor HTTP local, procesos residentes, librerías visuales pesadas y servicios innecesarios.
- Usar búsquedas indexadas por SKU/código y nombre; paginar resultados, bitácora y reportes. No cargar todo el catálogo ni el historial al abrir la venta.
- Mantener la base local como única fuente de datos operativa; usar transacciones y ejecutar los respaldos locales o remotos fuera del hilo de interacción.
- Generar reportes extensos e impresión fuera del ciclo de respuesta de la interfaz; mostrar progreso y permitir continuar operando cuando sea seguro.
- Optimizar primero para el equipo real: medir tiempo de arranque, respuesta al escaneo, agregar artículo, abrir cobro y confirmar venta con el catálogo y volumen de historial esperados. Probar también con disco mecánico si ese es el hardware instalado.
- Objetivos iniciales, sujetos a validación en la PC: venta lista en ≤ 5 s tras abrir la aplicación; respuesta visible a búsqueda/escaneo y agregado en ≤ 300 ms; apertura de cobro en ≤ 500 ms; confirmación de venta local en ≤ 2 s, sin contar impresión ni red externa. Medir percentil 95 en condiciones normales y corregir si no se cumplen.

## 7. Pantallas y estados a diseñar

Además de la venta mostrada en el SVG, diseñar vistas para todos los módulos de la sección 5 y estos estados: carga, vacío, sin resultados, error de almacenamiento local, validación de formulario, acceso denegado, turno cerrado, producto agotado, venta confirmada e impresión fallida. Incluir diálogos de selección de producto, registro del método de pago, cancelación de orden, anulación de venta, ajustes de inventario, edición de receta, alertas de ingredientes, administración de usuarios/roles, respaldo/restauración y corte de caja. Como el pago electrónico solo se registra, no existe un estado de “pago rechazado” por una pasarela dentro de este alcance.

## 8. Plan de ejecución

La división detallada y ejecutable del trabajo está en [`frontend/plan.md`](frontend/plan.md) y [`backend/plan.md`](backend/plan.md). Ambos planes comparten contratos C++ y puertas de integración; “backend” designa la capa interna de la aplicación nativa, no un servidor local.

1. **Definición operativa:** confirmar reglas pendientes de la sección 9 con responsables de caja e inventario; inventariar RAM, disco, sistema operativo y periféricos de la PC objetivo; elegir plataforma, impresora y lector con una prueba de rendimiento en ese equipo.
2. **Diseño:** crear componentes reutilizables y prototipos de escritorio y pantalla estrecha; validar la nueva posición de la cuenta y recorridos de venta con cajeros.
3. **Base técnica:** empaquetar aplicación nativa y base embebida locales; definir esquema, autenticación, cuenta Dueño inicial, permisos configurables, migraciones, bitácora, respaldo/restauración local y destino remoto opcional con configuración técnica exclusiva del Desarrollador, sin instalaciones adicionales en la caja.
4. **Núcleo de venta:** catálogo, búsqueda/escaneo, carrito, cálculo, caja abierta, registro del método de pago, cobro transaccional y ticket.
5. **Operación y administración:** ingredientes y conversiones, recetas, entradas con costos, umbrales y alertas, inventario, barra, promociones, anulaciones, bitácora, cortes y reportes.
6. **Verificación y puesta en marcha:** probar flujos completos y fallos, respaldo/restauración local y remota con datos de prueba, operación sin red, restricciones de configuración técnica, roles y bloqueo de última cuenta Dueño, cálculos de unidades/costos, importación del catálogo inicial, capacitación, piloto con conciliación diaria y despliegue.

## 9. Decisiones pendientes para cerrar la especificación

1. La interfaz será una **aplicación de escritorio nativa**. Verificar sistema operativo, arquitectura y resolución mínima de la PC para compilar y probar el paquete correspondiente; no se presupone ninguno.
2. ¿Qué hardware de lector de código, impresora de tickets y cajón de dinero se usará? ¿Se requiere facturación? La terminal bancaria no se integrará en esta etapa; únicamente se registra el método declarado.
3. ¿Qué métodos de pago se deben poder registrar? ¿Se permiten pagos mixtos, impuestos, descuentos manuales y devoluciones? ¿Cómo se redondea? Ninguno de estos métodos implica procesamiento electrónico en esta etapa.
4. ¿Se permite vender sin existencia o autorizar sobreventa? ¿Cómo se calculan mermas, recetas, combos y productos por peso?
5. ¿Qué permisos iniciales y roles adicionales al Dueño se necesitan para empezar? El Dueño podrá configurarlos después. ¿Quién puede cambiar precios, anular ventas, ajustar inventario y cerrar caja?
6. ¿Hay datos existentes para importar y formatos obligatorios de ticket, reportes y corte?
7. ¿Cuál es la configuración exacta de la PC de caja (RAM real —estimada en 4 a 6 GB—, HDD/SSD, sistema operativo y resolución) y cuántos productos y ventas históricas se esperan?
8. ¿Cómo se valúan ingredientes comprados a precios distintos: promedio ponderado, primeras entradas/primeras salidas u otro método? ¿Se requiere mostrar costo y margen estimados por platillo/bebida?
9. ¿El bajo stock se calcula por ingrediente en toda la cafetería o por ubicación (almacén/barra)? ¿Debe alertar solo en pantalla o también generar un reporte imprimible?
10. ¿Dónde se guardarán los respaldos locales y con qué frecuencia se harán? ¿Quién, además del Dueño, puede restaurarlos? Para el servidor remoto opcional, definir dirección, capacidad, frecuencia, retención y responsable de administrarlo.
11. ¿Cómo se provisionará y recuperará la identidad técnica del Desarrollador para modificar la configuración remota sin introducir una contraseña maestra fija o una puerta trasera?

## 10. Criterios de aceptación principales

- La cuenta de la venta, su total y **Cobrar** aparecen arriba a la derecha en escritorio; caja/turno y usuario aparecen en el extremo derecho del encabezado. La interfaz sigue operable en pantalla estrecha.
- El perfil Táctil reorganiza Venta, cobro y formularios para un monitor grande: controles amplios y separados, acciones contextuales y secundarias agrupadas, sin solapamientos ni exceso de botones por fila. Se puede seleccionar producto, editar cantidad/nota, cobrar y elegir impresión mediante un solo toque por activación, sin depender de ratón; el teclado en pantalla no oculta la captura ni las acciones necesarias. Cambiar perfil conserva el pedido.
- La sidebar muestra solo **Punto de Venta, Inventario, Caja y Reportes, BackOffice** según permisos. Cada sección muestra sus subsecciones en una barra central debajo del nombre de la cafetería y encima de la búsqueda o contenido; ninguna subsección aparece en la sidebar.
- **BackOffice → Servidor de respaldos** solo aparece para la identidad Desarrollador; el Dueño ve **BackOffice → Respaldos** para activar, ejecutar y supervisar copias, sin acceso a destino ni credenciales.
- Un cajero puede encontrar un producto, agregarlo, cambiar cantidad, quitar una línea, cobrar y obtener ticket usando teclado o ratón; los totales coinciden con los renglones y las reglas configuradas.
- Tras cobrar, la aplicación pregunta si se desea imprimir; elegir «Ahora no» conserva la venta y permite reimprimirla después. El ticket impreso contiene productos, cantidades, precios, subtotales y total confirmados. Una nota añadida a un producto aparece bajo su propia línea y permanece igual en una reimpresión; dos notas distintas del mismo producto no se mezclan.
- En el catálogo visual opcional, un cajero puede agregar desde tarjetas con o sin imagen **dos platillos y una bebida**; los tres quedan en el mismo carrito, el total se acumula correctamente y la venta produce un solo ticket. La vista Lista mantiene toda la funcionalidad cuando se desactivan las imágenes.
- Cada venta guarda el método de pago seleccionado para consulta y reportes, sin conexión a una pasarela ni validación de transferencias/tarjetas.
- El Dueño puede crear un platillo o bebida con ingredientes y cantidades en unidades compatibles; una venta descuenta las cantidades convertidas correctamente y conserva el movimiento histórico.
- El sistema impide operar sin la cuenta Dueño inicial; esa cuenta tiene acceso a todo el negocio y no puede quedar desactivada o sin rol si es la última cuenta Dueño activa. Los roles creados por el Dueño restringen tanto pantallas como operaciones de la capa de servicios. Solo la identidad técnica del Desarrollador puede modificar la configuración del servidor remoto.
- El Dueño puede crear una cuenta con correo, contraseña y rol; un correo duplicado con distinta capitalización se rechaza. Puede añadir, cambiar y quitar su avatar; la cuenta y la imagen sobreviven al cierre y reapertura, y las cuentas anteriores se conservan tras la migración.
- Una entrada de 1 kg de limón por $50.00 conserva cantidad y costo originales, calcula el costo unitario normalizado y actualiza existencias. El umbral de bajo stock configurado por el Dueño dispara una alerta al alcanzarse o cruzarse.
- La aplicación arranca y guarda datos localmente sin instalar un motor de base de datos ni otros componentes separados. Un respaldo creado desde la aplicación puede verificarse y restaurarse en una instalación compatible.
- Con el respaldo remoto desactivado o el servidor caído, ventas e inventario siguen funcionando. Al activarlo, una copia consistente llega al servidor y se puede verificar; el Dueño ve su estado, pero no puede editar el destino ni las credenciales técnicas.
- Ninguna venta confirmada se duplica por doble clic/reintento; venta, pago, inventario, caja y bitácora quedan consistentes tras un error.
- Una orden sin cobrar puede cancelarse; una venta cobrada solo puede anularse mediante flujo autorizado y auditable.
- El corte de turno concilia ventas y movimientos de caja, muestra diferencias y conserva quién lo realizó.
- Los módulos muestran estados vacíos y errores comprensibles; permisos se aplican también en servicios, no solo ocultando botones.
- En la PC objetivo, la aplicación cumple las metas de rendimiento de la sección 6 con un catálogo y un historial representativos.

## 11. Arquitectura técnica elegida

### Lenguaje, interfaz y distribución

- **Lenguaje: C++17**, compilado para el sistema operativo y arquitectura reales de la PC. Se elige por su bajo costo de ejecución y control de memoria, pero el rendimiento se aceptará solo tras medirlo en la PC objetivo. Usar bibliotecas fijadas por versión y controles de calidad para reducir errores de memoria y concurrencia.
- **Interfaz: wxWidgets**, con ventana, menús, formularios, tablas y diálogos de escritorio que usan controles nativos donde la plataforma lo permite. Construir las vistas mediante layouts adaptables, no como una imagen SVG ni como una página web. Usar componentes de tabla virtual o paginada y cargar miniaturas solo para tarjetas visibles.
- **Arquitectura en un proceso:** la ventana llama a una capa de servicios C++ para ventas, inventario, roles, caja y respaldos; esa capa abre SQLite directamente. Separar interfaz, reglas de negocio y repositorios para que los permisos y las transacciones no dependan de un botón. Las tareas de respaldo, reportes e imágenes van a trabajadores en segundo plano y actualizan la ventana por el mecanismo seguro de eventos de wxWidgets; nunca manipulan controles desde el hilo trabajador.
- **Distribución:** paquete por plataforma validada con ejecutable y bibliotecas necesarias incluidas. Al primer inicio crea directorio de datos, ejecuta migraciones y guía el alta del Dueño. El usuario no instala compilador, intérprete, navegador ni servidor de bases de datos. Una actualización crea respaldo previo, migra el esquema y conserva datos. El sistema operativo sigue sin determinarse hasta revisar la PC real.
- **Seguridad local:** inicio de sesión dentro de la aplicación, contraseña con hash seguro y sesión en memoria con bloqueo por inactividad. La capa de servicios comprueba permisos en cada acción sensible, incluso si se invoca desde otro diálogo o atajo. Limitar intentos de acceso y registrar auditoría. La configuración técnica de respaldo exige identidad de Desarrollador independiente; nunca se desbloquea con un control oculto. Proteger el archivo de datos y credenciales con permisos del sistema operativo disponible.

### Base de datos elegida

- **SQLite embebido** en archivo local, enlazado con la aplicación mediante su API C/C++ e incluido en el paquete final. MySQL y PostgreSQL requieren un servidor separado y administración adicional; no aportan una ventaja necesaria a una sola caja con datos locales y respaldos remotos.
- Activar claves foráneas, transacciones para operaciones de venta y movimientos, `busy_timeout` y modo WAL cuando el sistema de archivos local lo soporte. Limitar conexiones y escrituras concurrentes; no colocar la base activa en una carpeta de red. Indexar SKU/código, nombre normalizado, fecha de venta, folio, ingrediente y estado de alerta.
- Guardar dinero en centavos enteros. Guardar cantidades en una escala decimal definida por unidad (por ejemplo, miligramos para masa y mililitros para volumen cuando alcance la precisión requerida), evitando `float` para inventario y costos. Aplicar redondeo monetario solo en puntos definidos y conservar costo total original de compra.
- El esquema mínimo incluye `users` (con correo normalizado para unicidad), `user_avatars`, `roles`, `permissions`, `role_permissions`, `products`, `product_images`, `ingredients`, `recipes`, `recipe_items`, `stock_locations`, `stock_movements`, `purchase_entries`, `promotions`, `carts`, `cart_items`, `sales`, `sale_items`, `payment_records`, `cash_registers`, `shifts`, `cash_movements`, `backup_jobs` y `audit_events`. Migraciones versionadas y restricciones `NOT NULL`, `UNIQUE`, `CHECK` y claves foráneas deben proteger invariantes, además de las validaciones en C++.
- Guardar el carrito activo como **borrador local** ligado a caja y usuario para recuperarlo tras cerrar la aplicación. Al confirmar, revalidar precios, recetas, permisos, turno y existencias y convertir el borrador en venta dentro de una transacción. Usar una clave de idempotencia persistida para evitar dobles cobros. Definir caducidad y limpieza de borradores abandonados sin alterar ventas cerradas.

### Catálogo con imágenes y carrito

- Cada producto puede tener **una imagen opcional**. Un usuario con permiso para editar productos carga JPEG, PNG o WebP; la capa de servicios valida formato real y dimensiones, reduce a un tamaño máximo configurable (objetivo inicial: 512 × 512 px y 100 KB por imagen) y genera miniatura. Guardar la versión optimizada en `product_images` dentro de SQLite para que una copia consistente incluya catálogo e imágenes. No guardar fotografías originales de alta resolución.
- El catálogo visual muestra categorías, tarjetas y búsqueda incremental. Cargar primero nombres y precios; decodificar miniaturas solo para tarjetas visibles y mantener una caché de memoria de tamaño limitado dentro de la aplicación. Si una imagen falta o falla, mostrar marcador de posición sin impedir vender. La vista Lista continúa disponible y es la predeterminada para rendimiento.
- El carrito se muestra junto al catálogo o en el panel superior derecho según espacio. Una línea contiene producto, cantidad, precio aplicado, subtotal y nota opcional; el total y número de artículos se recalculan al agregar, editar o quitar. Productos idénticos se agrupan si tienen la misma configuración de precio/promoción **y la misma nota**; diferencias de precio, variantes o notas forman líneas separadas. Una compra como **2 platillos + 1 bebida** se cobra en una sola venta y un solo ticket.
- No cargar todas las imágenes ni todas las tarjetas al abrir el punto de venta. Medir memoria del proceso de escritorio; si la vista visual supera los límites medidos, reducir miniaturas, tamaño de página o desactivarla en la configuración del Dueño.

### Servicios y operaciones

- Separar servicios internos por dominio: sesión/usuarios/roles, catálogo/ingredientes/recetas, carrito/ventas/pagos registrados, inventario/compras, caja/reportes y respaldos. Los servicios validan toda acción de escritura; la ventana no decide por sí sola el total definitivo ni las existencias.
- Confirmar venta en una transacción que asigna folio, guarda líneas con sus notas y método de pago declarado, genera movimientos de inventario y caja, actualiza el turno y registra auditoría. Si cualquier paso falla, revertir todo y mantener el borrador. La pregunta de impresión y la impresión ocurren después de confirmar y permiten reintento sin duplicar la venta.
- Las alertas de bajo stock se calculan tras cada movimiento y se consultan mediante índice; no ejecutar un recorrido completo del inventario en cada pantalla. Reportes grandes se generan por lotes y con límites de fecha/paginación.
- Respaldos: producir instantánea SQLite consistente, comprobar integridad, empaquetar metadatos de versión y cifrar antes de guardar localmente o enviar al servidor opcional mediante HTTPS. El servidor solo recibe y conserva archivos; la aplicación local registra estado, reintentos y retención. Restaurar únicamente tras validar versión e integridad y crear respaldo previo de la base vigente.

### Verificación técnica mínima

- Medir en la PC real arranque, RAM del proceso de escritorio, búsqueda, carga del catálogo con y sin imágenes, agregado de 20 artículos, cobro, reportes y copia remota. Usar catálogo e historial de tamaño representativo y el disco real.
- Probar cierre inesperado durante venta, pérdida de corriente simulada, doble clic en Cobrar, base ocupada, disco lleno, imagen corrupta, falta de red y caída del servidor de copias. Verificar que ventas confirmadas, movimientos y respaldos sigan consistentes.
- Como metas iniciales adicionales: la vista Lista debe poder operar con imágenes desactivadas; el catálogo visual debe presentar la primera página sin cargar imágenes fuera de pantalla; 20 líneas de carrito deben editarse sin pausas perceptibles. Definir un límite de RAM medido en la PC antes de aceptar la versión final.

### Fundamento de la elección

La documentación oficial de [wxWidgets](https://wxwidgets.org/) describe su interfaz C++ multiplataforma basada en controles nativos. La documentación de [SQLite](https://www.sqlite.org/whentouse.html) lo presenta para almacenamiento local de aplicaciones y explica su diferencia frente a bases cliente-servidor; su [API C/C++](https://www.sqlite.org/c3ref/intro.html) permite integrarlo directamente y su [API de respaldo](https://www.sqlite.org/backup.html) permite copias consistentes mientras la base está en uso. Estas fuentes respaldan la viabilidad técnica; el rendimiento concreto queda sujeto a la medición indicada arriba.

## 12. Estado actual y próxima integración — revisión del 2026-09-29

La aplicación usa servicios SQLite de cuentas, catálogo, ingredientes/compras, turnos, carrito y ventas desde `ServiceRegistry::create_sqlite_registry`. Reportes todavía usa `MockReportService`; ese mock debe retirarse del ejecutable distribuible. El servicio de respaldos solo cubre la instantánea local básica; el destino remoto, restauración y configuración persistente siguen pendientes. Estas observaciones describen el código inspeccionado mientras AntiGravity continúa trabajando.

| Área | Avance presente | Trabajo para cumplir esta especificación |
| --- | --- | --- |
| Acceso y cuentas | Alta inicial con correo y contraseña, roles configurables, gestión de cuentas por servicios, auditoría; vista de cuentas con alta, cambio de contraseña, estado y consulta de avatar. | Correo normalizado único, correos anteriores sin inventar datos, tabla separada de avatar y validación real de imágenes; edición de correo/rol/avatar en UI, confirmación de contraseña en alta de empleados, bloqueo de sesión e identidad técnica. |
| Catálogo y recetas | CRUD persistente de productos, ingredientes y recetas; búsqueda paginada; BLOB de imagen con comprobación de cabecera/peso. | Vistas de inventario y editor de recetas, decodificación/dimensiones/miniaturas reales, stock de productos directos y versiones/consumos históricos verificables. |
| Compras e inventario | Entrada básica con costo/cantidad originales, conversión entera, movimiento de entrada, consulta de umbrales y alertas. | Proveedor/referencia/ubicación/autor del movimiento, merma/traslado/ajuste, restricción de umbrales al Dueño y valuación acordada. El costo estimado actual usa la última compra; aún no es una política contable aprobada. |
| Carrito y cobro | Borrador SQLite, aislamiento por usuario/caja, notas por línea, captura monetaria con validación estricta en centavos (`Money::try_parse`), edición de cantidad y notas en cuadrícula con fusión automática, confirmación atómica en transacción con revalidación de turno/precios/existencias bajo receta vigente, deducción en `stock_movements`, registro en `cash_movements` (SALE), vaciado de carrito y auditoría. Idempotencia contextual vinculada a usuario y caja con recuperación tras cierre de turno. | Consumo de productos directos sin receta, interfaz para anulación de venta cobrada. |
| Caja y ticket | Apertura explícita de turno desde UI con fondo inicial elegido por el usuario (migración 006 con `cash_movements`), estado en vivo en encabezado, cierre con arqueo; pregunta Imprimir/Ahora no y vista previa del ticket recuperable por folio con nombre de cajero y notas; diálogo de historial de tickets y reimpresión (`TicketHistoryDialog`); subsistema de impresión nativo wxWidgets (`wxPrinter`/`wxPrintout`) con `TicketPrintLayout` (único sistema de coordenadas, reserva estricta de pie de página, columnas dinámicas $\ge 20$ y sin recortes `...`) y manejo de cancelación/error sin revertir venta. | Impresión física real en hardware de tickets de destino (comprobar salida y periférico real una vez conectada la PC de caja), conciliar diferencias de arqueo y reportes de corte. |
| Navegación y operación final | Cuatro secciones y barra de subsecciones; Venta, Usuarios y cuentas, y Caja y turnos tienen paneles específicos. | Las demás vistas siguen como pantallas preparatorias; filtros de permisos en subsecciones restantes, catálogo visual, reportes, respaldos completos, accesibilidad y medición en la PC real. |
| Modo táctil | Perfil Estándar/Touch persistido por puesto (`station_profile.cfg` en `CAFETERIA_DATA_DIR`); reorganización táctil de Venta con steppers 48×48px, botón de agregado 56×210px, separación de 8–14px, contextuales dinámicos, Cuenta actual sin recortes para importes grandes ($2,900.00 / 116 unidades) y conservación total del carrito al alternar; verificado con suite Win32 nativa en Release y Debug (FE-08/FE-09). | Catálogo visual táctil por tarjetas, captura numérica/teclado en pantalla según plataforma y validación física en monitor grande táctil real. |

### Orden del siguiente trabajo con seguimiento

1. **Integridad de software de I2 (BE-01/02/04/05/06 y FE-02/03) [SOFTWARE PARCIAL — ver auditoria-2026-10-18.md]:**
   - Fusión transaccional y segura en carrito: agrupación condicionada estrictamente a la coincidencia simultánea de producto, notas y precio unitario (`unit_price_cents`); ejecución dentro de `TransactionScope` (`begin_transaction` / `commit_transaction` / `rollback_transaction`) con comprobación estricta de prepare, bind, step y `carts.updated_at` (`touch_cart`); validación previa de desbordamientos aritméticos en 64 bits en subtotales de línea y totales acumulados antes de persistir, preservando el borrador intacto ante cualquier error. Probado con fallos inducidos vía triggers SQLite en `DELETE` durante fusión y en `updated_at`.
   - Instantánea inmutable del cajero en tickets: migración `007_sale_cashier_snapshot.sql` (`cashier_name TEXT` en `sales`); al confirmar la venta se guarda `display_name`; cambios posteriores de cuenta no alteran tickets emitidos ni el historial. Para ventas previas a la migración 007, se muestra explícitamente `'Nombre histórico no disponible (' || s.user_id || ')'` o `'Nombre histórico no disponible'` sin depender de `users.display_name`.
   - Autorización estricta de reimpresión: `get_ticket` y `get_recent_tickets` exigen `pos.reprint` en la capa de servicios; la venta recién confirmada permanece accesible para su vendedor autorizado (`pos.sell`). Probado con roles sin `pos.reprint` en CTest.
   - Formato y aritmética monetaria: `Money::try_parse` valida la estricta agrupación de miles (comas de 3 dígitos exactos, rechazo estricto de `1,5` o comas ambiguas); manejo seguro de `INT64_MIN` y aritmética de 64 bits en `Money::to_string` sin undefined behavior.
   - Impresión íntegra y paginada: `TicketPrintLayout` (`src/frontend/ticket_layout.hpp`/`.cpp`) unifica el sistema de coordenadas (`MapScreenSizeToPage` y `GetLogicalPageRect`), mide métrica de fuentes en DC, reserva espacio estricto para el pie de página (`usable_h - footer_reserved_h`) demostrando ausencia de solapamiento, precalcula la paginación para mantener coherentes `GetPageInfo()`, `HasPage()` y `OnPrintPage()`, y valida áreas de 58 mm, 80 mm y altura reducida. `TicketDto::to_formatted_text()` adapta dinámicamente las columnas ($\ge 20$), envuelve limpiamente texto por palabras para cafetería, cajero, productos y notas sin puntos suspensivos `...`, y soporta cantidades e importes grandes válidos en modo multilínea (`utf8_column_width <= max_cols`). Mensajes de diálogo distinguen envío al spooler vs comprobación de salida en papel físico, asegurando que cancelaciones o fallas del spooler no anulan ni duplican la venta.
   - Captura monetaria estricta (`Money::try_parse`): rechaza entradas inválidas, letras, >2 decimales, negativos no permitidos y desbordamientos numéricos; en UI (`CheckoutDialog`, `CashShiftPanel`), una entrada inválida invalida el importe previo e inhabilita Confirmar/Abrir.
   - Recetas vigentes: disponibilidad y venta consultan únicamente la versión activa (`ORDER BY version DESC LIMIT 1`); versiones anteriores se conservan en la base para trazabilidad histórica; comprobado que dos ventas sucesivas descuentan recetas v1 y v2 correspondientes.
   - Idempotencia contextual: la clave está ligada a `user_id` y `register_id`; usuarios ajenos reciben `ErrorCode::Forbidden`; el reintento autorizado recupera la venta original incluso tras cerrar el turno, sin duplicar cobro ni inventario; la clave UI se invalida tras cualquier cambio en el carrito.
   - Edición de carrito: contratos y UI (`PosPanel`) permiten editar cantidad y notas de líneas existentes en cuadrícula; modificar una nota para que coincida con otra línea existente del mismo producto las fusiona en una sola línea; cantidad 0 vacía la línea.
    - Historial y reimpresión: contrato y servicio `get_recent_tickets(limit)` implementados; diálogo `TicketHistoryDialog` para búsqueda y recuperación por folio; integración de impresión nativa `wxPrinter` / `wxPrintout` (`TicketPrintout`) con manejo de cancelación y error sin anular venta.
    - Integración gráfica I2 completa (9 requerimientos en `tests/test_i2_integration.py` con ejecutable real `app_desktop.exe` en Release y Debug):
      1. Edición interactiva de cantidad y notas en el carrito vía `wxTextEntryDialog`.
      2. Fusión compatible de líneas al igualar notas en UI y SQLite (línea consolidada a $180.00 y segunda fila eliminada).
      3. Eliminación de línea (`Quitar línea`) y verificación del estado restante exacto ($200.00).
      4. Cobro íntegro con registro atómico en SQLite: venta V-10001, pago, inventario según receta, movimiento de caja `SALE` y auditoría `SALE_CONFIRMED`.
      5. Cierre, reapertura y recuperación íntegra del ticket histórico desde `TicketHistoryDialog`.
      6. Autorización estricta: bloqueo por `ErrorCode::Forbidden` a usuario sin `pos.reprint` al consultar historial, y acceso garantizado del cajero vendedor a su ticket recién confirmado.
      7. Cancelación del diálogo nativo del sistema (`#32770`, `IDCANCEL`) con preservación íntegra de la venta confirmada una sola vez.
      8. Error de impresión reproducible mediante variable de entorno `CAFETERIA_SIMULATE_PRINT_ERROR=1` (`TicketPrintout::OnBeginDocument` retorna `false`) manejado limpiamente con `Aviso del Subsistema de Impresión` sin corromper ni revertir la venta.
      9. Idempotencia transaccional estricta sin duplicación de ventas, pagos, movimientos de caja ni deducciones de stock.
      Sustitución integral de llamadas Win32 bloqueantes por `SendMessageTimeoutW` (`SMTO_ABORTIFHUNG`) y `PostMessageW`, con timeouts explícitos y limpieza garantizada de procesos en `finally`.
2. **Validación pendiente de I2:** hardware físico de destino (impresora térmica real y monitor táctil físico en la PC de caja) y aceptación visual humana por el usuario (7 casos en `frontend/qa-visual.md`). La puerta I2 queda formalmente **parcial** por hardware y aceptación humana; todos los requerimientos de software de la Puerta I2 han sido completamente resueltos y verificados.
3. **Completar cuentas e imágenes (BE-02/03 y FE-06):** migración correctiva de correo/avatar, imágenes válidas y optimizadas, edición de cuentas y roles desde BackOffice.
4. **Continuar I3–I7:** vistas y flujos de inventario, caja, permisos, reportes, restauración y perfil táctil (FE-08). Diseñar este perfil antes de cerrar los layouts; validar I7 en un monitor grande real. Resolver antes las decisiones de negocio de la sección 9 que afecten cada flujo; el equipo/periféricos y el mecanismo de identidad Desarrollador siguen pendientes.

Los planes mantienen `[x]` para avances concretos y `[ ]` para lo parcial. Las puertas I2–I7 permanecen abiertas hasta que existan pruebas completas de los flujos con el ejecutable real.
Verificación de integridad y funcionalidad I2 (2026-09-30): CTest completado al 100% en Debug y Release (3/3 targets: `ContractsTest`, `StorageTest` con 11 suites exhaustivas, y `TicketPrintingTest` con 4 suites de renderizado y áreas térmicas). `tests/test_i1_integration.py`, `tests/test_i2_integration.py` (9 requerimientos gráficos nativos con timeouts) y `tests/test_fe08_fe09_visual_integration.py` ejecutados al 100% en Debug y Release con bases SQLite aisladas y binarios reales. La puerta I2 permanece formalmente parcial hasta validar la impresora térmica física real en la PC de caja, monitor táctil físico y aceptación visual humana.

Verificación visual y de perfil táctil FE-08 / FE-09 (2026-09-30): `tests/test_fe08_fe09_visual_integration.py` ejecutado al 100% en Debug y Release con ejecutables nativos reales, comprobando: persistencia de perfil por puesto en `station_profile.cfg`, preservación de 116 artículos en carrito al cambiar de perfil, renderizado nítido de texto en `StatusBadge`, verificación geométrica estricta vía Win32 rects y métrica GDI (`control_w >= text_extent_w`) para importes grandes íntegros ($2,900.00 / 116 unidades, margen libre 16px en Estándar y 18px en Touch), reorganización de entrada Touch a 3 filas con campo de notas amplio (608px de ancho útil, 48px de alto), acciones de carrito en 2 filas con `Más acciones ▾` (160×48px) completamente legible sin recorte, encabezado `Subtotal` de tabla visible sin desplazamiento, objetivos táctiles interactivos ≥48px, adaptabilidad a 1200×720 y 960×600 en ambos perfiles, y reporte explícito de DPI (100% verificado activamente; 125/150/200% registrados como no verificados por restricciones del entorno Win32).

El QA visual humano comienza con **0 casos aceptados** en `frontend/qa-visual.md`. Ese archivo contiene comando de lanzamiento con base aislada y recorridos de acceso, navegación, cuentas, venta/ticket y perfil táctil. Cada aceptación debe reflejarse en FE-07/08 y la puerta correspondiente después de atender la retroalimentación.


### Estado y verificación de software I2 — 2026-10-02

Los pendientes de software de la puerta I2 y los tres bloques solicitados fueron completamente implementados, corregidos y verificados con pruebas automatizadas en Debug y Release:

- [x] Corrección de corte de caracteres UTF-8 en `split_long_word_utf8` al envolver palabras largas, comprobando preservación de caracteres multibyte.
- [x] Tipado exacto `PDWORD_PTR` (`ULONG_PTR = ctypes.c_size_t`) en `SendMessageTimeoutW` y propagación estricta de errores/timeouts en helpers Win32 de prueba.
- [x] Propagación estricta de resultados de bind en `SqliteCartService` y rollback transaccional íntegro con preservación del borrador, verificado con simulación de fallo.
- [x] Reintento real de confirmación en ejecutable nativo (`ISaleService::confirm_sale`) comparando todas las tablas operacionales fila por fila con idéntica concordancia (cero duplicación); validación integral de todos los campos del ticket recuperado en UI.
- [x] Validación de áreas imprimibles insuficientes en `TicketLayout::calculate_layout` (<20 columnas, <1 línea, o márgenes que agoten el espacio útil) con error explícito y cancelación limpia sin dibujar fuera; medición de pie de página.
- [x] Eliminación de supresión de aserciones (`wxDisableAsserts`) y corrección de la causa raíz de rango de páginas en `TicketPrintout::GetPageInfo` y `TicketPrintDialog`.
- [x] **Bloque 1 — Aislamiento del mecanismo de prueba de idempotencia:** Retiro absoluto de argumentos `--test-idempotency-key`, `--test-user`, `--test-pass` y fallback `admin/admin` de `app_desktop.exe`. Creado binario de prueba dedicado `tests/test_idempotency_runner.exe` que consume credenciales exclusivamente por `stdin` (sin exposición en `argv` ni logs) y comprueba invariabilidad fila por fila en las 8 tablas operacionales (`sales`, `sale_items`, `payment_records`, `ingredients`, `stock_movements`, `cash_movements`, `audit_events`, `cart_items`), excluyendo eventos de login.
- [x] **Bloque 2 — Corrección de QA visual humano del 2026-10-02:**
  - Botón «Agregar al Carrito» muestra un único icono Material `add` y etiqueta limpia sin `+` textual repetido; normalizado en botones de alta de usuario.
  - Padding interior, separación icono/texto (`SetBitmapMargins`) y alineación vertical estandarizados en sidebar (56px en Touch), encabezado y botones de diálogo vía `Theme::setup_button_icon`.
  - Eliminados parches claros tras etiquetas estáticas mediante `CardPanel::SetBackgroundColour` igualando el fondo de tarjeta para responder a `WM_CTLCOLORSTATIC`.
  - Entrada de «Cantidad» centrada horizontal y verticalmente en su contenedor (`wxTE_CENTRE` con espaciadores elásticos verticales), preservando caret, teclado y accesibilidad nativa.
  - Ergonomía Touch pulida: filas y navegación holgadas (56px en sidebar, 48px en toggle de encabezado), espaciado táctil confortable.
  - En el encabezado «Usuario:» se muestra `username` de la cuenta activa en vez de `display_name`, conservando este último para snapshots de ticket y gestión de cuentas.
- [x] **Bloque 3 — Perfiles de impresión térmica 55 mm y 80 mm:**
  - Contrato `IPrintConfigService` y servicio persistente `SqlitePrintConfigService` guardando la configuración por puesto en `station_printer.cfg`.
  - Validación estricta de anchos soportados: 55 mm (28 cols nominales) y 80 mm (42 cols nominales), rechazando perfiles inválidos o no soportados (ej. 58 mm).
  - Exigencia de permiso de autorización `config.manage`.
  - Panel de administración en `BackOffice -> Configuración -> Impresión` con selector, nombre de impresora y previsualización en vivo.
  - `TicketPrintLayout` distingue ancho nominal de perfil del área imprimible real del DC (`usable_w / char_w_`), abortando con error limpio si el área es insuficiente (<20 cols).
  - Formato de ticket `TicketDto::to_formatted_text()` incluye desglose de precio unitario en líneas con cantidad > 1 (`4 x $45.00`).
  - Verificado que cambiar de perfil o reimprimir no modifica datos históricos. Probado 55 mm explícitamente en CTest.

Límites de verificación:
- **Hardware de destino:** La impresora térmica física y el monitor táctil físico de la PC de caja quedan pendientes.
- **DPI dinámico:** Escalas de 125%, 150% y 200% permanecen sin verificación en ejecución en el entorno Win32 actual.
- **QA humano:** Los 7 casos de `frontend/qa-visual.md` continúan pendientes de aceptación interactiva por el usuario (0 casos aceptados). Capturas regeneradas en `qa_visual/screenshots/` para su revisión.
- **La puerta I2 permanece formalmente PARCIAL. No se avanza a la Puerta I3.**

### Auditoría posterior a los tres bloques — 2026-10-03

Avances inspeccionados: runner aislado, username, único icono de agregado, fondos más limpios, Cantidad centrada y servicio/panel de perfiles térmicos. No equivalen a aceptación visual o impresión física.

- [ ] Aplicar impresora elegida y comprobar papel/controlador 55/80; unificar vista previa con geometría real.
- [ ] Inyectar ruta efectiva Unicode del puesto a configuración de impresión, incluido --data-dir.
- [ ] Guardar configuración sin cambios parciales ni éxito ante error de escritura; actualizar memoria después del éxito.
- [ ] Resolver recorte de Editar cantidad, actualización dinámica de Touch en navegación y centrado de otras entradas altas.
- [ ] Capturas de configuración/previa 55/80 y recorridos GUI de cambio/reinicio/reimpresión sin alterar historia.

Ver `auditoria-2026-10-03.md` en la raíz. I2 sigue parcial; no avanzar a I3.

### Resolución de pendientes de auditoría e integridad de software — 2026-10-14

Los cinco pendientes de software de la auditoría 2026-10-03 fueron implementados, verificados y cubiertos por pruebas automatizadas en Debug y Release:

- [x] **Impresora y papel aplicados:** Enlace con impresoras instaladas en el sistema mediante enumeración Win32 (`EnumPrintersW`) en `BackOffice -> Configuración -> Impresión`. `TicketPrintDialog` aplica explícitamente `print_data.SetPrinterName(config_.printer_name)` antes de abrir la impresión; si el dispositivo configurado no está disponible, presenta un diálogo de advertencia claro sin revertir ni duplicar la venta confirmada.
- [x] **Vista previa coherente y honesta:** Identificada explícitamente en UI como ejemplo nominal sujeto al área imprimible real del controlador (`usable_w / char_w_`). Eliminada la etiqueta confusa "Columnas efectivas" del panel; se muestra la previsualización formateada conservando productos, cantidades, precios unitarios, notas y acentos íntegros.
- [x] **Ruta efectiva del puesto con soporte Unicode:** `SqlitePrintConfigService` recibe la ruta de directorio inyectada resuelta en el arranque desde `main_app.cpp` / `services_registry.cpp`, respetando `--data-dir` y `CAFETERIA_DATA_DIR`. Aislado por puesto y verificado con rutas conteniendo espacios y caracteres acentuados en `StorageTest`.
- [x] **Guardado atómico y consistente:** `SqlitePrintConfigService::save_to_disk` escribe a archivo temporal `.tmp`, verifica `good()`, fuerza `flush()`, cierra y comprueba errores de flujo antes de realizar sustitución atómica vía `std::filesystem::rename`. En caso de fallo de E/S, conserva intacto el archivo previo en disco y no altera la configuración activa en memoria. Verificado en `StorageTest` (`test_i2_print_config_atomic_save_and_isolation`).
- [x] **Correcciones visuales y ergonomía:** Eliminado el recorte de "Editar cantidad" ampliando los anchos mínimos en `pos_panel.cpp` (145px Estándar, 170px Touch). `SidebarPanel::refresh_profile()` actualiza dinámicamente las alturas de botones (56px en Touch, 46px en Estándar) al alternar el perfil sin reiniciar. `SubnavBar` conserva la subsección activa durante la reconstrucción. Entrada de notas Touch con fuente legible y manejo nativo confiable.
- [x] **Evidencia completa de capturas y no alteración histórica:** Generadas las 8 capturas en `qa_visual/screenshots/` (incluyendo `07_print_config_80mm.png` y `08_print_config_55mm.png`). Comprobado mediante pruebas de regresión que alternar perfiles térmicos o reimprimir tickets no altera en ningún caso los registros históricos de ventas, pagos, movimientos ni inventario.

Límites de verificación:
- **Hardware de destino:** La impresora térmica física real y el monitor táctil físico en la PC de caja continúan pendientes.
- **DPI no verificado:** 125%, 150% y 200% permanecen sin verificación en ejecución en el entorno Win32 actual (100% verificado).
- **QA visual humano:** Los 7 casos de `frontend/qa-visual.md` continúan pendientes de aceptación interactiva formal por el usuario (0 casos aceptados).
- **La puerta I2 permanece formalmente PARCIAL. No se avanza a la Puerta I3.**

### Auditoría independiente 2026-10-18

Ver `auditoria-2026-10-18.md`. Build desde cero y CTest 3/3 en Debug/Release confirmados; I1/I2/FE-08 GUI no repetidos. Pendientes de software: tamaño de papel 55/80 no aplicado, `MockReportService` en el registro real, gancho `CAFETERIA_SIMULATE_PRINT_ERROR` en el binario distribuible, centinela de impresora ficticio, prueba atómica insuficiente, recortes visuales y captura 08 inválida. I2 sigue **parcial en software**, hardware y aceptación humana. No avanzar a I3.

Actualización 2026-10-19: prompts A, B y C auditados (ver `auditoria-2026-10-18.md`). Los pendientes de software de la auditoría del 2026-10-18 quedan cerrados; I2 sigue parcial por hardware térmico/táctil, DPI 125/150/200 y aceptación humana (0/7). No avanzar a I3 hasta el QA humano.
