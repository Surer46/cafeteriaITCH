# Cafetería ITCH II — Base Integrable (C++17 + wxWidgets + SQLite Embebido)

Este repositorio contiene la arquitectura inicial y primera base integrable de la aplicación de escritorio para la **Cafetería ITCH II**, cubriendo las tareas **FE-00**, **BE-00** y el inicio de **BE-01** según lo definido en [`specs.md`](specs.md), [`frontend/plan.md`](frontend/plan.md) y [`backend/plan.md`](backend/plan.md).

---

## 1. Estructura y Separación Arquitectónica en CMake

El proyecto está organizado en bibliotecas desacopladas bajo un solo proceso nativo:

- [`app_contracts`](include/app/contracts/): Contratos de intercambio C++17 independientes de GUI y SQL. Define:
  - `Money`: manejo monetario en centavos enteros (`int64_t`), formateo con comas (`$2,930.00`), parsing seguro y operadores aritméticos sin coma flotante.
  - `Result<T>` y `OperationResult`: resultados tipados con catálogo de `ErrorCode` (`Success`, `InvalidInput`, `Forbidden`, `NotFound`, `Conflict`, `InsufficientStock`, `StorageFailure`, `BackupFailure`, `ShiftClosed`, `InvalidState`).
  - DTOs inmutables para producto, carrito, usuario, roles, turnos, alertas de inventario y respaldos.
  - Fachadas abstractas puras: `IAccountService`, `ICatalogService`, `ICartService`, `ISaleService`, `IInventoryService`, `IShiftService`, `IReportService`, `IBackupService`.
- [`app_domain`](include/app/domain/): Lógica de dominio pura:
  - `UnitConverter`: dimensiones (`MASS`, `VOLUME`, `COUNT`), conversiones a unidades base enteras (`mg`, `ml`, `pz`), validación estricta de compatibilidad dimensional y cálculo de costo unitario por unidad base.
- [`sqlite3`](third_party/sqlite/): Motor SQLite embebido (amalgamación C oficial v3.45.3) enlazado estáticamente sin necesidad de instalar servidores de base de datos.
- [`app_storage`](include/app/storage/): Capa de persistencia local SQLite:
  - `SqliteDatabase`: envoltorio RAII de `sqlite3*`, sentencias preparadas `SqliteStatement`, transacciones seguras (`BEGIN IMMEDIATE`, `COMMIT`, `ROLLBACK`) y pragmas obligatorios (`foreign_keys = ON`, `busy_timeout = 5000`, `journal_mode = WAL`).
  - `MigrationRunner`: gestor de migraciones versionadas con tabla de control `schema_migrations`.
  - [`migrations/001_initial_schema.sql`](migrations/001_initial_schema.sql): esquema relacional completo con 21 tablas, índices y restricciones, **sin credenciales de usuario ni contraseñas predeterminadas**.
- [`app_services`](include/app/services/): Implementaciones de servicios y mocks desacoplados para pruebas y trabajo paralelo del frontend.
- [`app_desktop`](src/frontend/): Aplicación nativa wxWidgets:
  - Encabezado con identidad de la cafetería ("Cafetería ITCH II") y estado de caja/usuario en la esquina superior derecha.
  - Sidebar con **exactamente cuatro secciones**: **Punto de Venta**, **Inventario**, **Caja y Reportes**, **BackOffice** (sin subsecciones en la barra lateral).
  - Barra de subsecciones horizontal en el área superior central, bajo el título de la cafetería y sobre el área de contenido.
  - Panel "CUENTA ACTUAL" en la esquina superior derecha del módulo de venta con botón "Cobrar" verde destacado.
- [`tests`](tests/): Pruebas de contratos (`test_contracts.cpp`), almacenamiento/persistencia (`test_storage.cpp`) ejecutables con CTest, y verificación técnica (`verify_schema.py`).

---

## 2. Requisitos y Dependencias del Sistema

- **Estándar:** C++17
- **Compilador:** GCC 9+, Clang 10+ o MSVC 2019/2022
- **Sistema de construcción:** CMake 3.16 o superior
- **Biblioteca gráfica:** wxWidgets 3.2+
- **Almacenamiento:** SQLite embebido (incluido en `third_party/sqlite/`)

---

## 3. Instrucciones de Compilación y Pruebas

### Configuración del SDK de wxWidgets
El SDK de wxWidgets es totalmente configurable y no depende de rutas fijas del sistema:
- **Opción A (Línea de comandos de CMake):**
  ```powershell
  cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release -DwxWidgets_ROOT_DIR="C:/ruta/a/wxwidgets"
  ```
- **Opción B (Variables de entorno):**
  Defina `WXWIN` o `WXWIDGETS_ROOT` apuntando al directorio raíz del SDK de wxWidgets:
  ```powershell
  $env:WXWIN = "C:\ruta\a\wxwidgets"
  cmake -B build -G "Ninja" -DCMAKE_BUILD_TYPE=Release
  ```
- **Opción C (Linux / Paquete de sistema):**
  En sistemas Linux, instale `libwxgtk3.2-dev` o equivalente; CMake lo detecta automáticamente mediante `wx-config`.

### Ejecución de Pruebas con CTest
El sistema copia automáticamente las DLLs de runtime (`libgcc`, `libstdc++`, `libwinpthread` y wxWidgets) y configura las propiedades del entorno de prueba, por lo que CTest es completamente reproducible y no requiere preparar manualmente el `PATH`:
```powershell
# Debug
ctest --test-dir build_debug --output-on-failure

# Release
ctest --test-dir build_release --output-on-failure
```

### Configuración de Ganchos de Prueba (`CAFETERIA_TEST_HOOKS`)
Por defecto, la opción está desactivada (`OFF`) para que el ejecutable de producción no contenga ganchos de simulación de errores de hardware. Para activar los ganchos durante pruebas de integración automatizadas:
```powershell
cmake -B build_debug -DCAFETERIA_TEST_HOOKS=ON
cmake --build build_debug
```

### Opciones de Ejecución de la Aplicación de Escritorio
- **Directorio de datos aislado/portable:**
  Es posible especificar una ruta de datos independiente (útil para pruebas o instalaciones portables) mediante el argumento `--data-dir`:
  ```powershell
  ./app_desktop.exe --data-dir="D:/MisDatos/cafeteria"
  ```
  O mediante la variable de entorno:
  ```powershell
  $env:CAFETERIA_DATA_DIR = "D:\MisDatos\cafeteria"
  ./app_desktop.exe
  ```

---

## 4. Estado de Verificación y Puerta I1

- **Entorno verificado:**
  - Compilador: GCC 16.1.0 MinGW-W64 MSVCRT x86_64
  - CMake 3.30+ con generador Ninja
  - wxWidgets 3.3.3 DLL x64
- **Puerta de Integración I1 (Aceptada):**
  - **Pruebas automáticas (CTest):** `ContractsTest` y `StorageTest` 100% aprobadas en Debug y Release.
  - **Ejecución integrada con ejecutable nativo:** Verificado el ciclo de vida completo en `build_debug/app_desktop.exe` y `build_release/app_desktop.exe` (alta inicial de Dueño obligatoria, cancelación, persistencia en SQLite, login subsecuente, rechazo de contraseñas incorrectas y ocultamiento de vistas técnicas no autorizadas).
