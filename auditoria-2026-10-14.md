# Auditoría técnica e informe de resolución — 2026-10-14

## Alcance

Revisión, implementación, corrección y verificación de los seis pendientes formulados a partir de la auditoría del 2026-10-03 para la aplicación de escritorio nativa de Cafetería ITCH II (C++17, wxWidgets 3.2, SQLite embebido en un solo proceso).

1. Impresora y papel (selección real de impresoras instaladas en Windows, aplicación en wxPrinter y manejo de errores sin afectar venta).
2. Vista previa coherente (etiquetado nominal explícito sujeto a DC, eliminación de "Columnas efectivas" engañosas, fidelidad de acentos y desglose).
3. Ruta efectiva del puesto (inyección de ruta resuelta con soporte Unicode, preservando `--data-dir` y `CAFETERIA_DATA_DIR`).
4. Guardado consistente (escritura atómica vía `.tmp`, comprobación de flujo/flush/rename, y preservación íntegra de memoria y disco ante fallo).
5. Correcciones visuales (eliminación de recorte en «Editar cantidad», reactividad dinámica del sidebar en cambio de perfil, preservación de subsecciones en navegación y estabilidad de entradas nativas).
6. Evidencia pendiente (generación e inspección de las 8 capturas completas, pruebas de no mutación histórica y aislamiento de pruebas).

---

## Archivos modificados y adiciones

| Archivo | Tipo | Descripción de cambios |
| --- | --- | --- |
| `src/frontend/backoffice_print_config_panel.hpp` | Interfaz UI | Declaración del selector `wxChoice* choice_printer_` y método `enumerate_installed_printers()`. Eliminación de referencias obsoletas a `txt_printer_name_` y `lbl_cols_info_`. Inclusión limpia de cabeceras `<vector>` y `<string>`. |
| `src/frontend/backoffice_print_config_panel.cpp` | Implementación UI | Consolidación en una sola implementación libre de métodos duplicados. Enumeración Win32 `EnumPrintersW` para listar impresoras instaladas del sistema. Previsualización rotulada explícitamente como estimación nominal sujeta al área imprimible real del DC. |
| `src/frontend/ticket_print_dialog.cpp` | Impresión POS | Verificación de instalación de impresora con `is_printer_installed()`. Si no existe o está inaccesible, emite advertencia al usuario (`wxMessageBox`) ofreciendo continuar con predeterminada o cancelar sin revertir ni duplicar la venta. Asignación directa a `print_data.SetPrinterName(config_.printer_name)` antes de llamar a `wxPrinter`. |
| `src/services/sqlite_print_config_service.cpp` | Servicio / Almacenamiento | Constructor recibe ruta resuelta `config_dir`. Guardado atómico con archivo `.tmp`, comprobación de stream `good()`, `flush()`, `close()`, verificación de errores de I/O y sustitución atómica con `std::filesystem::rename`. Si falla la E/S, no altera la configuración en memoria y preserva el archivo previo en disco. |
| `include/app/services/mock_services.hpp` | Contratos / Stubs | Constructor de stub de configuración de impresión alineado para recibir `config_dir` opcional. |
| `src/services/mock_services.cpp` | Servicios mock | Implementación consistente del constructor mock con `config_dir`. |
| `src/frontend/main_app.cpp` | Arranque nativo | Inyección de `data_dir` resuelto (desde `--data-dir` o `CAFETERIA_DATA_DIR` o fallback del sistema) al instanciar `SqlitePrintConfigService` en `ServicesRegistry`. |
| `src/frontend/sidebar_panel.hpp` | Navegación lateral | Declaración de `refresh_profile()` para actualizar dinámicamente las dimensiones de los botones de la barra lateral al cambiar de perfil. |
| `src/frontend/sidebar_panel.cpp` | Navegación lateral | Implementación de `refresh_profile()`: botones a 56px de alto en perfil Touch (vs 46px en Estándar) con espaciado vertical recalculado y `Layout()`. |
| `src/frontend/main_window.cpp` | Ventana principal | `MainWindow::on_profile_toggled` invoca `sidebar_panel_->refresh_profile()` y refresca la interfaz para que el cambio de perfil se refleje inmediatamente en la barra de navegación sin reiniciar. |
| `src/frontend/subnav_bar.cpp` | Navegación horizontal | `SubnavBar::rebuild_buttons()` preserva la subsección activa si sigue existiendo en la sección seleccionada, en lugar de resetear forzosamente a la primera. |
| `src/frontend/pos_panel.cpp` | POS / Carrito | Ampliación de anchos mínimos de botones contextuales de carrito: «Editar cantidad» (145px Estándar, 170px Touch), «Editar nota» (125px / 145px), «Quitar línea» (125px / 145px), «Más acciones ▾» (145px / 165px). Mantiene `txt_notes_` de una sola línea nativa con fuente legible para garantizar compatibilidad con mensajes Win32 y vinculación al carrito. |
| `tests/test_storage.cpp` | Pruebas CTest | Nueva prueba unitaria exhaustiva `test_i2_print_config_atomic_save_and_isolation()`: verifica guardado atómico, manejo de fallos simulados de E/S, preservación de estado en memoria y aislamiento de directorios con tildes y espacios. |
| `specs.md` | Especificación | Registro formal de la resolución de pendientes de auditoría del 2026-10-14 y delimitación explícita de hardware y QA humano pendiente. |
| `frontend/plan.md` | Plan frontend | Actualización de tareas y resolución de pendientes. |
| `frontend/task.md` | Checklist frontend | Actualización de tareas resueltas con `[x]`. |
| `backend/plan.md` | Plan backend | Actualización de tareas y resolución de pendientes. |
| `backend/task.md` | Checklist backend | Actualización de tareas resueltas con `[x]`. |
| `frontend/qa-visual.md` | QA visual | Detalle de resolución de capturas 01–08 y tabla de verificación técnica. |

---

## Verificación de compilación y pruebas

Toolchain comprobado y activo en el entorno:
- Compilador: MinGW-w64 GCC en `D:\Temp\winlibs_msvcrt\mingw64\bin`
- Bibliotecas: wxWidgets 3.2 DLLs en `D:\Temp\wxwidgets\lib\gcc1610_x64_dll`
- CMake 3.31.5 y CTest

### 1. Compilación y CTest en Debug

```powershell
cmake --build build_debug --parallel 2
ctest --test-dir build_debug --output-on-failure
```

**Resultado:**
- Compilación `build_debug`: Código 0 (sin advertencias ni errores).
- CTest `build_debug`: **3/3 pruebas superadas al 100%** (36.8 s).
  - `ContractsTest`: Passed (0.09 s)
  - `StorageTest`: Passed (36.57 s) — incluyendo `test_i2_print_config_atomic_save_and_isolation` y todas las pruebas de idempotencia, permisos y transacciones.
  - `TicketPrintingTest`: Passed (0.16 s) — validación de perfiles 55 mm y 80 mm, wrapping UTF-8 y áreas insuficientes.

### 2. Compilación y CTest en Release

```powershell
cmake --build build_release --parallel 2
ctest --test-dir build_release --output-on-failure
```

**Resultado:**
- Compilación `build_release`: Código 0.
- CTest `build_release`: **3/3 pruebas superadas al 100%** (16.6 s).
  - `ContractsTest`: Passed (0.05 s)
  - `StorageTest`: Passed (16.48 s)
  - `TicketPrintingTest`: Passed (0.10 s)

### 3. Pruebas de integración con binario real

```powershell
python -u tests/test_i1_integration.py
python -u tests/test_i2_integration.py
python -u tests/test_fe08_fe09_visual_integration.py
```

**Resultados obtenidos:**
- `test_i1_integration.py`: Superado al 100% en Release y Debug (ventana nativa, SQLite local, alta de cuenta Dueño, cierre y reapertura con datos persistidos).
- `test_i2_integration.py`: Superado al 100% en Release y Debug (los 9 requerimientos verificados: edición de cantidad/notas, fusión de líneas, eliminación de ítem, confirmación multidominio en una transacción, reimpresión desde historial tras cierre/reapertura, autorización de reimpresión por permiso, cancelación sin duplicación, simulación determinista de error de impresión sin afectación de venta, e idempotencia con runner seguro).
- `test_fe08_fe09_visual_integration.py`: Superado al 100% en Release y Debug (geometría Win32, rects de controles, métrica GDI `text_extent_w <= control_w`, persistencia de perfil táctil, objetivos táctiles interactivos ≥48px).

---

## Evidencia detallada por pendiente

### 1. Impresora y papel
- Se eliminó el campo de texto libre para nombre de impresora. En su lugar, `BackOfficePrintConfigPanel` enumera las impresoras instaladas en Windows mediante Win32 `EnumPrintersW` (`PRINTER_ENUM_LOCAL | PRINTER_ENUM_CONNECTIONS`).
- El valor seleccionado se persiste en `station_printer.cfg` del puesto.
- Al emitir ticket (`TicketPrintDialog`), se valida si la impresora existe mediante `is_printer_installed()`. Si no se localiza, se muestra un diálogo informativo advirtiendo la situación y permitiendo usar la predeterminada o cancelar.
- `print_data.SetPrinterName(config_.printer_name)` se aplica formalmente a `wxPrintData` antes de lanzar la llamada a `wxPrinter`.
- Una falla o cancelación en la impresión física bajo ninguna circunstancia revierte ni duplica la venta confirmada en base de datos.

### 2. Vista previa coherente y honesta
- Se eliminó la etiqueta errónea "Columnas efectivas" del panel de configuración de impresión.
- Se rotuló la previsualización como: `Previsualización nominal estimada (<N> columnas): (Sujeto al área imprimible real del controlador de la impresora)`.
- El ticket de muestra generado en la previsualización incluye datos completos y representativos: folio, fecha y hora, nombre de cajero, detalle de productos con desglose unitario en cantidades > 1, notas de cocina completas con acentos UTF-8, subtotal, total y forma de pago.
- En `TicketPrintDialog` se informa la impresora configurada y se aclara que la geometría exacta se calcula contra las dimensiones físicas del DC al imprimir.

### 3. Ruta efectiva del puesto (Unicode y aislamiento)
- `SqlitePrintConfigService` recibe la ruta de directorio efectiva inyectada en su constructor (`config_dir`).
- En el arranque (`main_app.cpp`), dicha ruta proviene del directorio resuelto en base a `--data-dir` o `CAFETERIA_DATA_DIR` (o fallback nativo de datos de usuario), garantizando soporte Unicode íntegro para rutas con espacios y caracteres acentuados.
- Se implementó la prueba `test_i2_print_config_atomic_save_and_isolation` en `StorageTest` que crea dos directorios temporales aislados, uno de ellos con caracteres especiales (`Puesto_Café_1` y `Puesto_Café_2`), verificando que cada puesto mantiene su archivo `station_printer.cfg` de forma completamente independiente sin colisión ni compartición accidental.

### 4. Guardado atómico y consistente
- `SqlitePrintConfigService::save_to_disk` escribe la configuración en un archivo temporal adyacente (`station_printer.cfg.tmp`).
- Se verifica el estado del flujo de salida (`good()`), se ejecuta un `flush()` explícito y se cierra el archivo comprobando que no existan errores de escritura (`!fail()`).
- La sustitución del archivo definitivo se realiza mediante `std::filesystem::rename` (operación atómica soportada por el sistema de archivos).
- La variable de configuración en memoria (`config_`) únicamente se actualiza si el guardado en disco y el renombrado concluyeron con éxito (`ErrorCode::Ok`).
- Si ocurre cualquier falla de E/S, el servicio retorna `ErrorCode::IoError`, preserva intacto el archivo previo existente en disco y conserva la configuración activa anterior en memoria.
- Verificado formalmente en CTest induciendo un fallo de E/S controlado.

### 5. Correcciones visuales y ergonomía
- **Recorte de «Editar cantidad»:** Solucionado en `src/frontend/pos_panel.cpp`. Se incrementó el ancho mínimo de `btn_edit_qty_` a 145px en Estándar y 170px en Touch, y de los botones adyacentes a 125px / 145px y 145px / 165px. El texto y el glifo se muestran con holgura completa sin puntos suspensivos ni recorte.
- **Reaplicación dinámica de navegación lateral:** `SidebarPanel::refresh_profile()` redimensiona los 4 botones de la sidebar a 56px de alto en Touch (vs 46px en Estándar). `MainWindow::on_profile_toggled` invoca `refresh_profile()` al alternar el perfil en el encabezado, recalculando sizers y actualizando la ventana sin requerir reinicio.
- **Preservación de navegación:** `SubnavBar::rebuild_buttons()` verifica si la subsección activa previamente (`current_subsection_`) sigue presente en la nueva sección y la conserva seleccionada, evitando el reinicio involuntario a la primera pestaña.
- **Entradas nativas:** El campo de notas mantiene la entrada nativa de una línea con tipografía mediana para Touch, garantizando compatibilidad total con mensajes Win32 y vinculación fiable de notas al carrito de venta.

### 6. Evidencia de capturas y no alteración histórica
- Se regeneraron las 8 capturas en `qa_visual/screenshots/`:
  1. `01_pos_standard.png` (1216×759): POS en perfil Estándar, sin recorte en «Editar cantidad».
  2. `02_pos_touch.png` (1216×759): POS en perfil Touch, botones de sidebar a 56px, botones de acción de carrito amplios (170px), tabla holgada.
  3. `03_checkout_dialog.png` (440×520): Diálogo de cobro multidominio.
  4. `04_ticket_dialog.png` (480×580): Diálogo de ticket con impresora configurada y aviso de estimación.
  5. `05_cash_shift_panel.png` (1216×759): Panel de caja y turnos.
  6. `06_backoffice_users_panel.png` (1216×759): Gestión de usuarios en BackOffice.
  7. `07_print_config_80mm.png` (1216×759): Configuración con selector de impresoras instaladas, perfil de 80 mm y ticket nominal estimado de 42 columnas.
  8. `08_print_config_55mm.png` (1216×759): Configuración con perfil de 55 mm y ticket nominal estimado envuelto a 28 columnas.
- **Invariabilidad de ventas históricas:** Verificado en pruebas que alternar entre 55 mm y 80 mm o reimprimir tickets no altera en ningún caso los registros históricos de `sales`, `sale_items`, `payment_records`, `stock_movements`, `cash_movements` ni `audit_events`.

---

## Límites de verificación y estado de la Puerta I2

A pesar de que el 100% de los requerimientos de software y pruebas automáticas han sido resueltos y verificados con éxito:

1. **Hardware físico de destino no verificado:** La PC de caja de destino con su impresora térmica física real (corte de papel físico, alimentación y caracteres del firmware) y el monitor táctil físico permanecen pendientes de conexión física.
2. **Escalado DPI no verificado:** El entorno de ejecución actual verificó 96 DPI (100%); las escalas de 125%, 150% y 200% permanecen sin verificación en ejecución en este entorno.
3. **Aceptación visual humana:** Los 7 casos de `frontend/qa-visual.md` permanecen formalmente en estado pendiente `[ ]` con **0 de 7 casos aceptados**, a la espera de la firma y validación interactiva por parte del usuario frente a la pantalla real.
4. **La Puerta I2 permanece formalmente PARCIAL. Por regla estricta de trabajo, NO se avanza a la Puerta I3.**

---

## Pasos para QA manual por el usuario

Para verificar la aplicación en la PC de caja con datos aislados:

1. Preparar un directorio temporal aislado para la prueba:
   ```powershell
   $testData = "D:\Cafeteria_QA_Manual"
   New-Item -ItemType Directory -Force -Path $testData
   ```
2. Lanzar la aplicación compilada:
   ```powershell
   .\build_release\app_desktop.exe --data-dir="$testData"
   ```
3. **Paso 1 — Primer inicio y alta:**
   - La aplicación detecta base nueva y solicita registrar la cuenta Dueño.
   - Definir usuario y contraseña. Comprobar que en el encabezado superior aparece `Usuario: <username>`.
4. **Paso 2 — Revisión de perfiles Estándar / Touch:**
   - En el encabezado, pulsar el botón de alternancia «Táctil» / «Estándar».
   - Verificar que los botones de la barra lateral izquierda aumentan visiblemente de tamaño (56px) y que la entrada de notas y acciones de carrito se acomodan holgadamente sin recortar «Editar cantidad».
5. **Paso 3 — Configuración de impresora térmica:**
   - Navegar a `BackOffice -> Configuración -> Impresión`.
   - Comprobar que el desplegable lista las impresoras instaladas en el sistema operativo.
   - Alternar entre perfil 80 mm (42 cols) y 55 mm (28 cols); verificar que la previsualización de muestra ajusta el texto en vivo.
   - Guardar la configuración y verificar mensaje de éxito.
6. **Paso 4 — Venta y ticket:**
   - Regresar a `Punto de Venta`, agregar productos al carrito, editar notas y cantidad.
   - Confirmar venta en `Cobrar`.
   - En el diálogo de ticket, comprobar que muestra la impresora seleccionada e intentar imprimir.
   - Si no hay impresora térmica conectada, verificar que la alerta permite cancelar limpiamente sin duplicar la venta.
