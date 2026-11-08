# Reglas de trabajo — Cafetería ITCH II

## Fuentes de verdad y alcance

- Leer `specs.md`, `frontend/plan.md` y `backend/plan.md` antes de implementar. Mantenerlos coherentes cuando cambie una decisión técnica o un flujo.
- La aplicación es **de escritorio nativa**: C++17, wxWidgets y SQLite embebido en un solo proceso. No introducir navegador, Electron, servidor HTTP local, MySQL o PostgreSQL para la operación diaria.
- La base SQLite local es la única fuente de datos operativa. El servidor externo es opcional y solo almacena respaldos; una falla de red nunca debe bloquear ventas.
- No asumir el sistema operativo de la PC de caja. El entorno de desarrollo actual puede ser distinto; comprobar plataforma, arquitectura y periféricos de destino antes de fijar empaquetado o rutas.

## Límites entre capas

- La interfaz usa únicamente contratos/servicios de `include/app/contracts/`. No escribe SQL ni calcula precios, existencias o permisos definitivos. El dominio y almacenamiento no dependen de wxWidgets.
- Los servicios simulados son solo para desarrollar y probar la interfaz. No conectarlos al ejecutable distribuible ni presentar sus respuestas como datos persistidos. El arranque real debe abrir SQLite, aplicar migraciones y usar servicios persistentes.
- Mantener exactamente cuatro secciones en la sidebar: Punto de Venta, Inventario, Caja y Reportes, BackOffice. Las subsecciones van en la barra horizontal central bajo el encabezado y encima del contenido. “Servidor de respaldos” pertenece a BackOffice y solo es visible para la identidad Desarrollador.

## Integridad, dinero y unidades

- Guardar dinero en centavos enteros y cantidades físicas en unidades base enteras. Validar desbordamientos y entradas inválidas. No usar `float` o `double` para importes, saldos, costos ni conversiones de inventario.
- Un costo por unidad base puede ser menor de un centavo: **no dividir centavos entre miligramos o mililitros y guardar el cociente redondeado en centavos enteros**. Conservar costo total y cantidad originales, y calcular valores proporcionales con una representación racional o escala fija suficiente; redondear solo al obtener un importe monetario final según la regla acordada. Ejemplo obligatorio: 1 kg de limón por $50 conserva un costo no nulo por mg y produce $0.05 por g.
- Una venta confirmada debe guardar venta, pago declarado, consumo de inventario, movimiento de caja y auditoría en una transacción. Un reintento o doble clic no puede duplicarla. Una falla de impresión no revierte la venta.
- Las migraciones deben ser versionadas, repetibles sin duplicación y ejecutadas desde una ruta confiable del paquete. Crear respaldo consistente antes de migrar datos existentes. No copiar una base SQLite activa como respaldo sin usar su mecanismo de instantánea.

## Identidad y permisos

- En el primer inicio exigir alta de una cuenta Dueño con contraseña elegida por el usuario. No crear cuentas, contraseñas o secretos predeterminados. Nunca permitir desactivar o quitar el rol a la última cuenta Dueño activa.
- Comprobar permisos dentro de cada servicio, incluso cuando la acción se invoque sin la pantalla normal. Ocultar un botón no sustituye una autorización.
- La configuración técnica del servidor de respaldos requiere identidad Desarrollador separada y auditada. Un rol configurable por el Dueño no puede otorgarse ese acceso. No incrustar credenciales ni una contraseña maestra en código, pruebas o documentación.

## Verificación y entrega

- Compilar y ejecutar CTest antes de declarar listo un bloque C++. Si faltan CMake, compilador o wxWidgets, reportar el bloqueo exacto y marcar la compilación como **no verificada**. La prueba Python del esquema valida SQL, no el ejecutable C++ ni la interfaz.
- Las pruebas deben ejecutarse en Debug y Release. No usar `assert` como única verificación: puede desaparecer con `NDEBUG`. CTest debe localizar migraciones mediante ruta absoluta configurada o copia controlada al directorio de pruebas, no según el directorio desde donde se invoque.
- Probar la puerta I1 con el ejecutable real: ventana nativa, SQLite local, alta del Dueño, cierre y reapertura con datos persistidos. Los mocks no satisfacen I1.
- En cada entrega indicar archivos modificados, comandos ejecutados, resultados de compilación/pruebas y límites de lo verificado. No afirmar que un flujo funciona solo porque existe código o porque pasó una prueba de otro lenguaje.

## Memoria operativa (compartida por todos los agentes)

Datos ya verificados. Úsalos en lugar de explorar el entorno. Mantener breve: cada línea se carga en cada sesión de cada agente.

- **Toolchain (Windows):** anteponer al `PATH` `D:\Temp\winlibs_msvcrt\mingw64\bin;D:\Temp\wxwidgets\lib\gcc1610_x64_dll`. GCC 16.1 (MinGW), CMake, Ninja y wxWidgets 3.3.3 (`wxWidgets_ROOT_DIR=D:/Temp/wxwidgets`, `wxWidgets_LIB_DIR=D:/Temp/wxwidgets/lib/gcc1610_x64_dll`, `wxWidgets_CONFIGURATION=mswu`). No buscar otros compiladores.
- **Copias de trabajo (git):** `main` en `D:\Cafetería_ITCH` es del frontend (AntiGravity); el backend (Claude Code Terminal) trabaja en el worktree `D:\Cafeteria_ITCH_backend`, rama `backend/<bloque>`. Los agentes no hacen commits. Propiedad de archivos y protocolo en `coordinacion-agentes.md`.
- **Compilación:** frontend usa `build_release` y `build_debug`; backend usa `build_be_release` y `build_be_debug`. Ya están configurados: basta `cmake --build <dir> --parallel 2` y `ctest --test-dir <dir> --output-on-failure`.
- **No compilar ni correr pruebas antes de editar:** la línea base del commit actual ya está verificada por el orquestador.
- **Pruebas:** CTest = ContractsTest, StorageTest, BackupTest, TicketPrintingTest. GUI (solo el agente de frontend, nunca dos a la vez): `tests/test_i1_integration.py`, `tests/test_i2_integration.py`, `tests/test_fe08_fe09_visual_integration.py`, `tests/test_f1_roles_panel.py`; capturas con `tests/capture_visual_qa.py` en `qa_visual/screenshots/`.
- **Medir, no estimar:** alturas, anchos y recortes se comprueban con `GetWindowRect`, `GetTextExtent` o `LVM_GETCOLUMNWIDTH`, no a ojo. Prohibido relajar un umbral de prueba para que pase.
- **Hora local:** las fechas se guardan como `YYYY-MM-DD HH:MM:SS` en hora local.
- **Ganchos de prueba:** `CAFETERIA_SIMULATE_PRINT_ERROR` solo existe si se compila con `-DCAFETERIA_TEST_HOOKS=ON`; los builds habituales lo tienen apagado.
