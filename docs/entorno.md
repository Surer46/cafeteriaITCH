# Entorno de desarrollo — preparar cualquier PC

Guía para una persona o un agente que llega al proyecto en una PC nueva. Los archivos versionados **no contienen rutas absolutas de ninguna PC**: todo se expresa relativo a la copia de trabajo. Si algo de esta guía no coincide con lo que encuentras, corrige la guía en el mismo cambio.

## Disposición de carpetas

Una carpeta contenedora cualquiera (el nombre y la unidad no importan) con tres hermanas:

```text
<contenedora>/
  cafeteriaITCH/            copia principal, rama main (frontend)
  Cafeteria_ITCH_backend/   git worktree, rama backend/<bloque> (backend)
  toolchain/                herramientas; no se versiona
    mingw64/                compilador (bin/, lib/, ...)
    wxwidgets/              include/ y lib/gcc1610_x64_dll/
  notas-pc.md               notas de esta PC; no se versiona
```

Desde cualquiera de las dos copias de trabajo, el toolchain está en `..\toolchain`. Si en una PC debe vivir en otro sitio, define `CAFETERIA_TOOLCHAIN` con su ruta absoluta y úsala en lugar de `..\toolchain`; no escribas esa ruta en ningún archivo versionado.

## Qué se necesita

| Herramienta | Versión verificada | Origen |
|---|---|---|
| GCC (MinGW-w64, x86_64, posix, seh, msvcrt) | 16.1.0 | WinLibs: `github.com/brechtsanders/winlibs_mingw`, release `16.1.0posix-14.0.0-msvcrt-r4`, archivo `winlibs-x86_64-posix-seh-gcc-16.1.0-mingw-w64msvcrt-14.0.0-r4.zip`. Incluye CMake y Ninja. |
| wxWidgets (binarios para ese GCC) | 3.3.3 | `github.com/wxWidgets/wxWidgets`, release `v3.3.3`: `wxMSW-3.3.3_gcc1610_x64_Dev.7z` y `wxWidgets-3.3.3-headers.7z`. |
| Python | 3.13 | Solo para las pruebas GUI (`tests/*.py`) y las capturas. |
| Git | cualquiera reciente | — |

SQLite viene en `third_party/sqlite/`. El compilador y los binarios de wxWidgets deben corresponderse (`gcc1610` = GCC 16.1.0, runtime msvcrt). Con otro compilador hay que compilar wxWidgets desde fuentes y actualizar esta tabla.

No se modifica el `PATH` del sistema ni ninguna configuración global: cada terminal antepone el toolchain a su propio `PATH`.

## Pasos

1. **Comprobar qué hay.** Si existe `..\toolchain\mingw64\bin\g++.exe` y `..\toolchain\wxwidgets\lib\gcc1610_x64_dll`, salta al paso 3.
2. **Instalar.** Las descargas requieren la aprobación del usuario (indica archivo, origen y tamaño). Descomprime el zip de WinLibs en `..\toolchain` (crea `mingw64/`) y los dos archivos de wxWidgets en `..\toolchain\wxwidgets` (crean `include/` y `lib/gcc1610_x64_dll/`). `tar -xf` de Windows abre `.zip` y `.7z`.
3. **Preparar la terminal** (PowerShell, desde la raíz de una copia de trabajo):

   ```powershell
   $tc = if ($env:CAFETERIA_TOOLCHAIN) { $env:CAFETERIA_TOOLCHAIN } else { (Resolve-Path ..\toolchain).Path }
   $env:PATH = "$tc\mingw64\bin;$tc\wxwidgets\lib\gcc1610_x64_dll;" + $env:PATH
   ```

4. **Crear el worktree del backend** (una vez, desde la copia principal): `git fetch origin` y `git worktree add ..\Cafeteria_ITCH_backend backend/<bloque>`.
5. **Configurar las carpetas de compilación** (una vez por copia). En la copia principal, `build_release` y `build_debug`; en el worktree del backend, `build_be_release` y `build_be_debug`:

   ```powershell
   $wx = ("$tc\wxwidgets" -replace '\\','/')
   cmake -B build_release -G Ninja -DCMAKE_BUILD_TYPE=Release "-DwxWidgets_ROOT_DIR=$wx" "-DwxWidgets_LIB_DIR=$wx/lib/gcc1610_x64_dll" -DwxWidgets_CONFIGURATION=mswu
   cmake -B build_debug   -G Ninja -DCMAKE_BUILD_TYPE=Debug   "-DwxWidgets_ROOT_DIR=$wx" "-DwxWidgets_LIB_DIR=$wx/lib/gcc1610_x64_dll" -DwxWidgets_CONFIGURATION=mswu
   ```

   CMake copia las DLL del compilador y de wxWidgets a cada carpeta de compilación, así que el ejecutable y las pruebas corren sin depender del `PATH`.
6. **Verificar la línea base** antes de aceptar trabajo nuevo:

   ```powershell
   cmake --build build_release --parallel 2
   ctest --test-dir build_release --output-on-failure
   ```

   y lo mismo en Debug. Si falla en una PC nueva sin haber cambiado código, es un problema del entorno: repórtalo, no adaptes el código ni relajes pruebas.

## Datos de prueba manual

Usar una carpeta fuera del repositorio o ignorada por git, por ejemplo `.\build_release\app_desktop.exe --data-dir="..\Cafeteria_QA_Manual"`.

## Reglas para mantener esto portable

- Ninguna ruta absoluta (`C:\...`, `D:\...`, `/home/...`) en archivos versionados nuevos: usar rutas relativas a la copia de trabajo o `..\toolchain`.
- Los prompts que el orquestador entrega para pegar sí pueden llevar rutas absolutas de la PC en uso: no se reutilizan tal cual en otra PC.
- Auditorías y prompts con fecha son registros históricos: las rutas que citan son de la PC donde se hicieron y no se corrigen.
- Lo propio de una PC (rutas reales, qué está instalado, qué se verificó ahí, sesiones de agentes) se anota en `..\notas-pc.md`, fuera del repositorio. Nunca se sube a GitHub.
- Las conversaciones de los agentes no viajan con el repositorio. El estado está en `docs/ORQUESTADOR.md`; las reglas, en `AGENTS.md`.
