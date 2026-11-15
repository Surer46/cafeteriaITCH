# Produce el instalador de Windows: instala el componente "app" (sin portable.flag) en una carpeta de trabajo
# dentro de la compilacion y compila packaging/cafeteria.iss con ISCC.
# Uso (lo llama el objetivo "installer" de CMake):
#   cmake -DCMAKE_COMMAND_PATH=... -DBUILD_DIR=... -DISCC=... -DISS=... -DAPP_VERSION=... -P cmake/build_installer.cmake
# Variables: BUILD_DIR (compilacion Release), ISCC, ISS, APP_VERSION, y opcionales STAGE_DIR, OUT_DIR.
# Falla si ISCC emite advertencias.

foreach(var BUILD_DIR ISCC ISS APP_VERSION)
    if(NOT DEFINED ${var} OR "${${var}}" STREQUAL "")
        message(FATAL_ERROR "build_installer.cmake: falta ${var}")
    endif()
endforeach()
if(NOT EXISTS "${ISCC}")
    message(FATAL_ERROR
        "No se encontro ISCC.exe en: ${ISCC}\n"
        "Inno Setup va en <toolchain>\innosetup (ver docs/entorno.md; el toolchain es ..\toolchain o CAFETERIA_TOOLCHAIN).")
endif()
if(NOT DEFINED STAGE_DIR OR "${STAGE_DIR}" STREQUAL "")
    set(STAGE_DIR "${BUILD_DIR}/installer_stage/CafeteriaITCH")
endif()
if(NOT DEFINED OUT_DIR OR "${OUT_DIR}" STREQUAL "")
    set(OUT_DIR "${BUILD_DIR}/installer_out")
endif()

file(REMOVE_RECURSE "${STAGE_DIR}")
execute_process(
    COMMAND "${CMAKE_COMMAND}" --install "${BUILD_DIR}" --config Release --component app --prefix "${STAGE_DIR}"
    RESULT_VARIABLE install_rc OUTPUT_VARIABLE install_out ERROR_VARIABLE install_err)
if(NOT install_rc EQUAL 0)
    message(FATAL_ERROR "cmake --install fallo (${install_rc}):\n${install_out}\n${install_err}")
endif()

file(MAKE_DIRECTORY "${OUT_DIR}")
execute_process(
    COMMAND "${ISCC}" "/DAppVersion=${APP_VERSION}" "/DSourceDir=${STAGE_DIR}" "/O${OUT_DIR}" "${ISS}"
    RESULT_VARIABLE iscc_rc OUTPUT_VARIABLE iscc_out ERROR_VARIABLE iscc_err)
message(STATUS "${iscc_out}")
if(NOT iscc_rc EQUAL 0)
    message(FATAL_ERROR "ISCC fallo (${iscc_rc}):\n${iscc_out}\n${iscc_err}")
endif()
string(TOLOWER "${iscc_out}${iscc_err}" iscc_all)
if(iscc_all MATCHES "warning")
    message(FATAL_ERROR "ISCC emitio advertencias; el instalador no se acepta con advertencias:\n${iscc_out}\n${iscc_err}")
endif()
set(installer "${OUT_DIR}/CafeteriaITCH-${APP_VERSION}-instalador-win64.exe")
if(NOT EXISTS "${installer}")
    message(FATAL_ERROR "ISCC termino bien pero no existe ${installer}")
endif()
file(SIZE "${installer}" installer_size)
message(STATUS "Instalador: ${installer} (${installer_size} bytes)")
