# Dirección visual — Cafetería ITCH II

Estado: especificado el 2026-09-30; aplicación visual pendiente. Referencia: `../gemini-svg(1).svg`. Seguimiento: FE-09 en `plan.md`, junto con FE-08 para touch. No sustituye las correcciones de integridad de I2.

## Estilo elegido

Composición inspirada en Cupertino: superficies claras, jerarquía sencilla, tarjetas redondeadas, bordes discretos y controles segmentados. Identidad rojo/amarillo del SVG, tipografía Roboto y símbolos Material. Implementación C++17/wxWidgets; no implica introducir Flutter, navegador o un framework distinto. El entorno de desarrollo optimizado no cambia la PC de destino i5 de segunda generación y 4–6 GB de RAM estimados.

## Paleta compartida

| Token | Valor | Uso |
| --- | --- | --- |
| background | #F1F5F9 | Fondo general del SVG |
| surface | #FFFFFF | Tarjetas, formularios y cuenta |
| surface_subtle | #F8FAFC | Cabeceras de tabla y grupos |
| border | #CBD5E1 | Separadores/bordes de superficie; no único indicador de foco |
| text | #1F2937 | Contenido principal |
| text_secondary | #475569 | Etiquetas secundarias |
| brand | #B91C1C | Acción de marca y selección |
| brand_deep | #991B1B | Sidebar y hover de marca |
| brand_soft | #FEF2F2 | Fondo de selección contextual |
| accent | #FACC15 | Acento puntual/indicador; texto #78350F |
| success | #166534 | Cobrar/estado positivo; texto blanco en botón |
| error | #B91C1C | Error/destructivo, siempre con texto |
| focus | #1D4ED8 | Contorno de foco visible |

Comprobar contraste real de texto y controles, incluidos estados deshabilitados. El color no es la única señal de selección, error ni estado de caja. Evitar amarillo como texto sobre blanco o totales naranjas de bajo contraste.

## Geometría, texto e iconos

- Espaciado lógico: 4, 8, 12, 16, 24 y 32. Tarjetas: padding 20–24, radio 14–16; controles y botones: radio 8–10; contenedores segmentados: radio 10–12. Separadores de 1 unidad lógica, sin dibujar bordes por cada elemento decorativo.
- Roboto Regular/Medium/Bold empaquetada localmente, con versión y licencia conservadas. Registro privado de fuente según plataforma comprobada, sin exigir instalación al usuario. Fallback a fuente del sistema si la carga falla. No depender de descarga de Google Fonts durante operación.
- Texto de trabajo Estándar: 14–16 unidades lógicas como intención visual, títulos 22–26 y total 32–40. En wxWidgets convertir según DPI; no confundir unidades de layout con puntos tipográficos ni fijar todo con píxeles absolutos. Perfiles Touch según FE-08, manteniendo objetivos 48 como mínimo, preferentemente 56–64 en acciones frecuentes.
- Material Symbols Outlined, estilo uniforme de peso aproximado 400 y base 24. Usar recursos locales de un subconjunto necesario; SVG o variantes PNG para escalas admitidas, cacheadas por tamaño/color/DPI. No cargar una fuente completa de iconos ni usar emoji como reemplazo. Botones importantes llevan icono y etiqueta; símbolos sin texto requieren nombre accesible y ayuda.
- QA humano del 2026-10-02: no duplicar símbolos entre bitmap y etiqueta (icono add + «Agregar al carrito», sin un segundo + textual). Usar padding horizontal inicial de 12–16 unidades lógicas y separación icono/texto de 8, ajustados al control y DPI; mantener el conjunto centrado y evitar iconos pegados al borde. Sidebar mantiene alineación consistente por columnas. El contenido de Cantidad se centra horizontal y verticalmente; las demás entradas altas de una línea conservan alineación vertical y padding coherentes, caret y edición nativos. Los fondos de etiquetas deben integrarse con su contenedor, sin parches decorativos.
- Recursos sugeridos: point_of_sale, inventory_2, account_balance_wallet, manage_accounts; search, add, edit, sticky_note_2, delete, more_horiz, receipt_long, print, history, person, lock. Verificar nombres disponibles al empaquetar; limitar iconos por contexto.
- Fuentes oficiales: [Roboto y licencia OFL](https://github.com/google/fonts/blob/main/ofl/roboto/METADATA.pb), [Material Symbols y licencia Apache 2.0](https://developers.google.com/fonts/docs/material_symbols). Incluir licencias junto con los recursos efectivamente distribuidos.

## Composición

1. Encabezado de superficie clara: nombre de cafetería a la izquierda; turno y cuenta/avatar a la derecha. La etiqueta «Usuario:» muestra el nombre de usuario (`username`) de la cuenta autenticada, no su nombre completo (`display_name`); se actualiza al cambiar de sesión. Estado en badge con texto e icono. Mantener controles de ventana del sistema; no copiar botones decorativos de macOS del SVG.
2. Sidebar burdeos con exactamente Punto de Venta, Inventario, Caja y Reportes, BackOffice. Icono + texto; selección marcada por fondo claro y texto burdeos o marcador amarillo de buen contraste. Ancho adaptable, sin duplicar subsecciones.
3. Subsecciones como selector segmentado en el centro, debajo del nombre y encima de búsqueda/contenido. Activa con contorno/fondo distintivo y texto legible; Más subsecciones cuando falte espacio, accesible por teclado y toque.
4. POS: búsqueda/agregado en tarjeta horizontal; pedido en tabla limpia con alineación monetaria a la derecha, líneas separadas y notas bajo el producto. Sin botones repetidos en cada fila. Seleccionar línea habilita una barra contextual para cantidad/nota/quitar.
5. Cuenta actual arriba a la derecha en tarjeta blanca: artículos, subtotal/descuentos si aplican, total destacado y Cobrar verde oscuro. Botón de marca para Agregar; secundarios neutros. Historial y Cancelar en Más acciones sin ocultar la confirmación destructiva.
6. Caja, cuentas y diálogos comparten tarjetas, títulos, campos con etiquetas superiores y estados vacíos/error. Errores junto al campo; no inflar la pantalla con tarjetas de métricas sin información útil.
7. Touch reorganiza anchuras, acciones y espacio; no se limita a escalar botones. No depende de hover/doble clic. Conserva carrito, foco y entrada al cambiar perfil.

## Implementación y rendimiento

- Centralizar tokens y creación de componentes; no dispersar hexadecimales, fuentes y medidas por todos los paneles.
- Dibujar tarjetas redondeadas ligeras con wxGraphicsContext y buffering, conservando entradas/listas nativas donde convenga. Componentes dibujados deben mantener foco, teclado, estados y nombres accesibles; no reemplazar toda la UI por canvas.
- Usar fondos opacos y bordes antes que blur, transparencia o sombras dinámicas. Sombra estática discreta opcional solo si se mide viable. Sin animaciones continuas, resplandor verde del SVG ni repintado constante.
- Cargar solo iconos/imágenes visibles y cachear de forma acotada. Separar diseño visual de servicios, precios, permisos y SQLite. Toda acción existente conserva su contrato y comprobación.
- Mantener números legibles, formato monetario y encabezados; no aceptar el cambio solo por compilar. No añadir datos de ejemplo a la base operativa.

## Aceptación

- [ ] Comparativa revisable de POS Estándar/Touch y cuentas/cobro con esta paleta.
- [ ] Cuatro secciones y subsecciones en posición acordada; cuenta arriba derecha; ninguna acción funcional desaparece.
- [ ] Roboto y recursos Material cargan sin red, con fallback funcional y licencias.
- [ ] Hover/foco/pulsado/deshabilitado/error distinguibles; teclado, escáner y touch operables.
- [ ] Escalas 100/125/150/200%, ventana estrecha y nombres largos sin solapamientos; validar tamaños físicos después en hardware confirmado.
- [ ] Debug/Release, CTest e I1/I2 no regresan; actualizar automatización si cambian controles sin depender de coordenadas decorativas.
- [ ] Registrar capturas y QA humano en `qa-visual.md`; medir arranque/RAM y respuesta antes/después. La aceptación final de rendimiento requiere la PC objetivo.
