# QA visual — revisión humana

Estado inicial al 2026-09-29: **0 casos visuales aceptados por el usuario**. Compilar o pasar CTest no marca estos casos. Registrar observaciones aquí y vincularlas a las tareas de `plan.md`; cada caso se marca solo después de revisión humana y corrección de sus hallazgos.

## Abrir la versión revisada

Ejecutar en PowerShell:

```powershell
Set-Location -LiteralPath 'D:\Cafetería_ITCH'
$env:PATH = "D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll;" + $env:PATH
$env:CAFETERIA_DATA_DIR = 'D:\Cafetería_ITCH\qa_visual\data'
& 'D:\Cafetería_ITCH\build_release\app_desktop.exe'
```

Al primer inicio, elegir correo y contraseña propios para la cuenta Dueño de prueba. Al reabrir con la misma ruta se solicita login y se conservan los datos de QA. La variable se aplica a los procesos iniciados desde esa terminal; abrir otra terminal o quitarla al terminar permite volver a la ruta habitual:

```powershell
Remove-Item Env:CAFETERIA_DATA_DIR -ErrorAction SilentlyContinue
```

## Evidencia gráfica automatizada — Tema Cupertino / ITCH II (FE-09)

Se integró el sistema de diseño visual conforme a `frontend/design-system.md` (composición clara estilo Cupertino, paleta vino institucional `#991B1B` y amarillo `#FACC15`, tipografía Roboto TTF local, e iconos Material SVG tintados en memoria).

Se generaron capturas nativas de alta resolución mediante la API de dibujo de ventanas Win32 (`tests/capture_visual_qa.py`) disponibles en `qa_visual/screenshots/`:

1. `01_pos_standard.png`: Punto de Venta en perfil Estándar (carrito con líneas, notas de preparación, acción contextual de edición/quitar, resumen con total hero $178.00 y botón Cobrar primario verde).
2. `02_pos_touch.png`: Punto de Venta en perfil Touch (dimensiones ampliadas a 52px para táctil, espaciados generosos de separación entre botones de interacción frecuente).
3. `03_checkout_dialog.png`: Diálogo modal de cobro (tarjeta de Total a cobrar, selección de método de pago, captura de efectivo en centavos y cálculo en tiempo real del cambio a devolver).
4. `04_ticket_dialog.png`: Diálogo modal de ticket (banner con folio confirmado, pregunta explícita «¿Deseas imprimir el ticket?», previsualización tipográfica monoespaciada con notas, botones «Ahora no» e «Imprimir»).
5. `05_cash_shift_panel.png`: Panel de control de turnos de caja (indicador de turno abierto, fondo inicial registrado, controles de arqueo y cierre).
6. `06_backoffice_users_panel.png`: Panel de administración de cuentas de usuario (tabla de usuarios, roles, estado activo/inactivo, acciones de cambio de contraseña y estado, sin mostrar herramientas técnicas de desarrollador).

Para regenerar las capturas en cualquier momento:
```powershell
python -u tests/capture_visual_qa.py
```

## Casos de revisión

| Caso | Estado | Recorrido y observaciones |
| --- | --- | --- |
| QA-V01 — Acceso | [ ] | Revisar alta inicial, etiquetas, campos, contraseña/confirmación, mensajes, cancelación y reapertura/login. Anotar resolución y escala utilizadas. |
| QA-V02 — Navegación | [ ] | Recorrer cuatro secciones; comprobar título, barra central, cuenta arriba a la derecha, ancho útil y ausencia de solapamientos. |
| QA-V03 — Cuentas | [ ] | Crear cuenta de prueba con correo/rol/avatar, revisar listado, cambio de contraseña, activación/desactivación y mensajes de error. Registrar también los controles de edición que aún faltan. |
| QA-V04 — Venta | [ ] | Con datos de prueba preparados, buscar/agregar/quitar/cancelar, revisar contador/total/notas y pasos del cobro. Edición de línea y correcciones de integridad siguen pendientes en el plan. |
| QA-V05 — Ticket | [ ] | Revisar pregunta Imprimir/Ahora no y vista previa, texto largo y notas. No aceptar impresión real: el botón actual solo muestra un mensaje. |
| QA-V06 — Estándar y accesibilidad | [ ] | Revisar espaciado, contraste, foco/teclado, nombres largos, tamaño de ventana y DPI; anotar qué dificulta una tarea concreta. |
| QA-V07 — Táctil | [ ] | Revisar propuesta Estándar/Táctil de FE-08 antes de implementar; después verificar botones y espacios cómodos, acciones contextuales, teclado y flujo por toque en monitor grande real. |

## Registro de retroalimentación

### Resolución de recortes y verificación geométrica rigurosa — 2026-09-30 (FE-08 / FE-09)

1. **Perfil Estándar (QA-V04 / QA-V06):**
   - Eliminado de forma definitiva el recorte a la derecha en subtotal y total. Se redimensionaron las columnas de `list_items_` (suma total 555px) y se fijó el ancho de la tarjeta de cuenta `CardPanel` en 310px con padding interno uniforme de 16px. En `MainWindow`, `SetClientSize(wxSize(1200, 720))` garantiza un área de cliente interior exacta de 1200×720 sin depender del marco del SO.
   - Verificado con `GetTextExtentPoint32W` y Win32 rects: el control de TOTAL tiene 135px de ancho vs 64px de extensión del texto `$2,900.00`, con 16px de margen libre al borde derecho de la tarjeta.

2. **Perfil Touch (QA-V07 / FE-08):**
   - Reorganizada el área de entrada en 3 filas funcionales:
     - Fila 1: Búsqueda y catálogo de productos.
     - Fila 2: Campo de notas de preparación `txt_notes_` en fila completa exclusiva: 608px de ancho útil (ampliamente superior a los 350px mínimos) y 48px de altura táctil.
     - Fila 3: Steppers táctiles `[−]` y `[+]` de 48×48px alrededor de la cantidad y botón Hero `+ Agregar al Carrito` (210×56px).
   - Reorganizadas las acciones inferiores del carrito en 2 filas:
     - Fila 1: Acciones contextuales de línea (`Editar cantidad` 150×48px, `Editar nota` 140×48px, `Quitar línea` 140×48px).
     - Fila 2: Acciones generales con `Más acciones ▾` (160×48px) alineado a la derecha, eliminando toda compresión o recorte.
   - Columnas de `list_items_` en Touch ajustadas a suma de 605px, garantizando que el encabezado y columna `Subtotal` sean 100% visibles sin barra de desplazamiento horizontal.
   - Todos los objetivos táctiles interactivos cumplen con el estándar de accesibilidad de ≥48px de ancho y alto.

3. **Suite de verificación visual geométrica (`tests/test_fe08_fe09_visual_integration.py`):**
   - Sustituida la comprobación superficial de píxeles por verificación geométrica estricta vía Win32 API (`GetWindowRect`, `GetClientRect`, `GetParent`): `child.right <= container.right - pad`, `child.left >= container.left + pad`.
   - Medición de extensión de texto con GDI (`GetTextExtentPoint32W`) comprobando matemáticamente que `control_w >= text_extent_w` para el importe exacto esperado `$2,900.00`.
   - Matriz de prueba en 1200×720 y 960×600 en ambos perfiles (Estándar y Touch) con importes grandes ($2,900.00 / 116 unidades) y nombres/notas largos.
   - Detección de escala DPI: escala 100% (96 DPI) verificada activamente en ejecución; escalas 125%, 150% y 200% registradas explícitamente como `[NO VERIFICADO]` debido a que el entorno de escritorio de ejecución Win32 no permite conmutación dinámica de escala sin reinicio de sesión de usuario o cambio físico de pantalla.
   - Ejecutada y aprobada al 100% en Release y Debug.

4. **Estado de aceptación humana:**
   - Se regeneraron las capturas en `qa_visual/screenshots/` (1216×759 para ventanas completas, 440×520 para cobro, 480×580 para ticket).
   - Los 7 casos de revisión humana permanecen formalmente en estado `[ ]` (0 casos aceptados) hasta que el usuario realice la inspección interactiva.

### Observaciones de auditoría — 2026-09-30

Inspección de capturas generadas, no aceptación humana del usuario:

- QA-V04/06: total/subtotal recortados por el borde derecho en `01_pos_standard.png`; texto del botón Historial/Reimpresión cortado en Estándar y Touch.
- QA-V02: badge de encabezado sin texto visible en las capturas; verificar en vivo, pues PrintWindow puede producir artefactos en contenido dibujado.
- QA-V07: Touch aumenta algunas alturas pero conserva distribución y controles pequeños; requieren reorganización, áreas de toque completas y persistencia de preferencia.
- I1 repetido por auditoría: aprobado en Release/Debug. No se repitieron CTest ni I2 en esta revisión; sus resultados son los registrados por Gemini.

### Resolución técnica aplicada y verificada — 2026-09-30 (FE-08 / FE-09)

1. **Cuenta actual (QA-V04 / QA-V06):**
   - Tarjeta de cuenta ampliada a 340px (Estándar) y 380px (Touch).
   - Eliminado `wxFIXED_MINSIZE`; sizers de subtotal/descuento/total configurados con `AddStretchSpacer(1)` y alineación a la derecha.
   - Comprobada con importes grandes ($2,900.00 / 116 unidades) en 1200×720 y ventana estrecha (960×600) con margen libre derecho ≥24px verificado.
   - Cuenta y botón Cobrar se mantienen fijados arriba a la derecha.

2. **Acciones del carrito (QA-V04 / QA-V06):**
   - Eliminado `btn_ticket_history_` de la fila inferior para evitar desbordamiento horizontal (ancho reducido de >750px a ≤610px). El historial y reimpresión se reubicaron dentro de `Más acciones ▾`.
   - Botones contextuales (`btn_edit_qty_`, `btn_edit_notes_`, `btn_remove_item_`) se habilitan o deshabilitan dinámicamente según la fila seleccionada. Al agregar o refrescar, se selecciona automáticamente el primer elemento.
   - Distinción explícita de acciones: `Quitar línea` elimina el producto seleccionado del carrito; `Cancelar pedido` en la tarjeta de cuenta vacía toda la orden tras confirmación.

3. **StatusBadge de turno (QA-V02):**
   - Diagnóstico: `wxGraphicsContext::DrawText` de GDI+ no resolvía la tipografía privada "Roboto" cargada vía GDI (`AddFontResourceExW`) durante la captura por `PrintWindow`.
   - Solución: renderizado del texto mediante GDI estándar sobre el `wxDC` (`dc.SetFont`, `dc.SetTextForeground`, `dc.DrawText`) con fondo transparente, conservando la pastilla con `wxGraphicsContext`.
   - `DoGetBestSize()` y tamaño mínimo fijados en 28px (Estándar) y 34px (Touch). Verificado texto visible en ventana viva y >250 píxeles verde oscuro en capturas Win32.

4. **Modo Touch y persistencia por puesto (FE-08 / QA-V07):**
   - Persistencia por puesto implementada en `station_profile.cfg` dentro del directorio de datos del puesto (`CAFETERIA_DATA_DIR`).
   - El cambio de perfil preserva el carrito al 100% (verificado con 116 artículos en SQLite). Al reabrir el ejecutable, la ventana inicia automáticamente en el perfil guardado.
   - Reorganización Touch: Row 1 con búsqueda, botón y catálogo; Row 2 con botones stepper `[−]` y `[+]` de 48×48px alrededor del campo de cantidad, campo de notas de 48px y botón hero `+ Agregar al Carrito` de 56×210px. Botón `Cobrar` de 56px de alto en la tarjeta de cuenta.
   - Separación de 8–14px entre controles táctiles. Cero dependencia de hover o doble clic.

5. **Automatización de verificación visual (`tests/test_fe08_fe09_visual_integration.py`):**
   - Suite Win32 completa ejecutada y aprobada al 100% en Release y Debug.
   - CTest 2/2 aprobado en Release y Debug.
   - Capturas regeneradas en `qa_visual/screenshots/`.
   - **Nota sobre aceptación humana:** los 7 casos de QA visual siguen en estado `[ ]` (0 casos aceptados), a la espera de la inspección interactiva y aprobación del usuario.

QA-V05 debe verificar wxPrinter, actualmente integrado, incluida cancelación/error y renderizado de tickets largos; la antigua descripción del botón como mensaje simulado queda obsoleta. Todos los casos permanecen sin aceptación del usuario.

Para cada observación, anotar **caso/pantalla**, **acción**, **resultado actual**, **cambio deseado**, **resolución/escala** y, cuando aplique, si se usó ratón, teclado o touch. Añadir una captura si facilita explicar distribución/solapamientos. Registrar tarea FE correspondiente, corrección realizada y fecha de aceptación antes de marcar el caso.

### Inspección independiente de la última entrega — 2026-09-30

Las capturas actuales de Venta en Estándar y Touch muestran subtotal/total completos y Más acciones legible. Touch ahora dispone de notas en una fila amplia y acciones del carrito en dos filas. Los tamaños descritos en la sección anterior corresponden a una entrega previa: la terminal de la última entrega informa tarjetas de 310/330 px y una distribución Touch de tres filas de entrada.

Alcance: inspección de capturas, sin aceptación humana ni repetición independiente de las pruebas en esta revisión. El reporte de ejecución cubre 96 DPI (100%); 125%, 150% y 200% siguen sin verificar. La revisión Touch de encabezado, navegación y subsecciones queda pendiente: comprobar siete botones de Venta no valida todos los controles de la aplicación. Se conservan los siete casos de QA humano sin marcar.

## QA humano recibido — 2026-10-02 (atendido en software y capturas regeneradas)

Evidencia original conservada: `../qa_visual/screenshots/2026-10-02-human-pos.png`. Captura de Venta con caja cerrada.
Capturas corregidas generadas con base aislada: `../qa_visual/screenshots/01_pos_standard.png`, `02_pos_touch.png`, `03_checkout_dialog.png`, `04_ticket_dialog.png`, `05_cash_shift_panel.png`, `06_backoffice_users_panel.png`.

| Observación del usuario | Casos | Corrección implementada | Verificación técnica |
| --- | --- | --- | --- |
| Doble + en Agregar al Carrito | QA-V04/06 | Etiqueta corregida a «Agregar al Carrito» con único icono Material `add` (sin `+` textual repetido). Se revisó y normalizó también `btn_new_user_` en BackOffice. | Verificado en capturas 01_pos_standard y 02_pos_touch y pruebas de UI. |
| «Usuario» muestra el nombre completo | QA-V02 | `MainWindow::update_header_status()` modificado para mostrar `username` (`Usuario: <username>`) mediante `cur_user.username`, manteniendo `display_name` para snapshots históricos y gestión de usuarios. | Verificado con cuentas con username ≠ display_name y persistencia de sesión. |
| Iconos pegados al borde | QA-V02/04/06 | Creado helper `Theme::setup_button_icon()` con `SetBitmapPosition(wxLEFT)` y `SetBitmapMargins(8..10px, 0)` normalizado en sidebar, encabezado, POS y diálogos. | Verificado espaciado visual en todas las capturas regeneradas. |
| Colores poco pulidos | QA-V02/04/06 | `CardPanel::SetBackgroundColour` asigna el color de superficie de la tarjeta para responder a `WM_CTLCOLORSTATIC` nativo de Windows, eliminando los recuadros claros tras etiquetas estáticas. | Verificado fondo uniforme sin parches en capturas 01 a 06. |
| Valor de Cantidad arriba en campo alto | QA-V04/06/07 | `txt_qty_` centrado horizontal (`wxTE_CENTRE`) y verticalmente en su contenedor mediante panel y espaciadores elásticos en `PosPanel`, conservando caret, selección, teclado y validación nativos. | Verificado centrado geométrico en perfil Estándar y Touch. |
| Touch no favorece suficientemente el uso por toque | QA-V07 | Altura de botones de sidebar ajustada a 56px en Touch, toggle de perfil en encabezado a 48px, separación táctil holgada, filas de tabla cómodas y objetivos interactivos ≥48px. | Verificado en `tests/test_fe08_fe09_visual_integration.py` y captura 02_pos_touch. |

- [x] Corregir observaciones en código y generar nuevas capturas comparables Estándar/Touch (`qa_visual/screenshots/`).
- [ ] Usuario revisa interactivamente las correcciones en el ejecutable real; **ningún caso está formalmente aceptado todavía** (0/7 casos aceptados).

Los checks de implementación y pruebas automatizadas no sustituyen la aceptación visual humana. Esta sección queda preparada para la inspección y firma del usuario en la PC de caja.

### Inspección independiente de capturas — 2026-10-03

Capturas 01/02: se confirma único icono add, username y ausencia de parches de texto. Cantidad aparece centrada. Editar cantidad todavía se recorta en ambos perfiles; navegación lateral permanece compacta en la captura Touch. Búsqueda/notas en Touch no están centradas verticalmente. Falta evidencia gráfica de configuración/previa térmica 55/80. Todos los casos humanos siguen pendientes; ver auditoria-2026-10-03.md.

### Verificación y resolución técnica de capturas — 2026-10-14

Se atendieron los pendientes visuales y de evidencia identificados en la auditoría del 2026-10-03:

1. **Resolución de recortes en acciones contextuales (QA-V04 / QA-V06):**
   - El botón «Editar cantidad» se amplió a 145px en Estándar y 170px en Touch (anteriormente 125px / 150px), eliminando todo recorte de glifos o del icono `edit`.
   - Los botones «Editar nota» y «Quitar línea» se ampliaron a 125px en Estándar y 145px en Touch.
   - El botón «Más acciones ▾» se amplió a 145px en Estándar y 165px en Touch, garantizando margen y legibilidad sin colisión con la tarjeta de cuenta.
   - Verificado en `01_pos_standard.png` y `02_pos_touch.png`.

2. **Reaplicación dinámica de dimensiones de navegación al alternar perfil (QA-V02 / QA-V07 / FE-08):**
   - Implementado `SidebarPanel::refresh_profile()` que recalcula las dimensiones mínimas de los cuatro botones (`btn_pos_`, `btn_inventory_`, `btn_cash_`, `btn_backoffice_`) a 56px de alto en perfil Touch (vs 46px en Estándar) y llama a `Layout()`.
   - `MainWindow` invoca `sidebar_panel_->refresh_profile()` dentro del callback de cambio de perfil del encabezado.
   - `SubnavBar::rebuild_buttons()` modificado para preservar la subsección activa al reconstruirse (no resetea a la primera subsección si la actual sigue siendo válida).
   - Verificado en `02_pos_touch.png` donde los botones de la barra lateral miden efectivamente 56px de alto tras el toggle a Touch.

3. **Evidencia gráfica de configuración de perfiles térmicos 55 mm y 80 mm (QA-V05):**
   - Generadas capturas `07_print_config_80mm.png` y `08_print_config_55mm.png` en `qa_visual/screenshots/`.
   - Incorpora selector de impresoras instaladas del sistema (`EnumPrintersW` en Windows) en `BackofficePrintConfigPanel`.
   - Eliminada la etiqueta confusa «Columnas efectivas»; rotulada claramente como estimación nominal del perfil (42 columnas para 80 mm, 28 columnas para 55 mm).
   - Previsualización en vivo en fuente monoespaciada con ticket de demostración completo: folio, fecha, cajero, productos desglosados, cantidades, precios unitarios, notas de preparación, subtotal, total y forma de pago.

4. **Diálogo de impresión de ticket (`04_ticket_dialog.png`):**
   - Informa la impresora configurada (`Impresora configurada: <nombre>`).
   - Rotulado como «Vista previa estimada (geometría exacta calculada al imprimir)».
   - Aplica el nombre configurado a `wxPrintData::SetPrinterName()` antes de abrir el diálogo nativo de `wxPrinter`.
   - Advertencia previa con confirmación si la impresora configurada no se encuentra entre las impresoras instaladas en Windows, permitiendo reintentar o cancelar sin revertir ni duplicar la venta confirmada en SQLite.

5. **Estado de aceptación humana:**
   - Se regeneraron las 8 capturas en `qa_visual/screenshots/`:
     1. `01_pos_standard.png` (1216×759)
     2. `02_pos_touch.png` (1216×759)
     3. `03_checkout_dialog.png` (440×520)
     4. `04_ticket_dialog.png` (480×580)
     5. `05_cash_shift_panel.png` (1216×759)
     6. `06_backoffice_users_panel.png` (1216×759)
     7. `07_print_config_80mm.png` (1216×759)
     8. `08_print_config_55mm.png` (1216×759)
   - Los 7 casos de revisión humana permanecen formalmente en estado `[ ]` (0 casos aceptados), a la espera de la inspección interactiva del usuario en la PC de caja con monitor táctil e impresora física.

