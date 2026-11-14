# Pasos de empaquetado que se ejecutan EN LA INSTALACION (install(CODE)), no al configurar.
# Se incluye desde CMakeLists.txt con las variables PKG_* ya definidas y PKG_STEP = check | dlls | manifest.
#
#   check    Se niega a empaquetar si no es Release o si CAFETERIA_TEST_HOOKS esta activado.
#   dlls     Copia solo las DLL que no son de Windows que el ejecutable (y las DLL copiadas) importan,
#            de forma recursiva, y falla si falta alguna. La lista no se escribe a mano.
#   manifest Escribe paquete.manifest (version + archivos esperados) que usa --self-check.

if(PKG_STEP STREQUAL "check")
    if(NOT "${PKG_CONFIG_NAME}" STREQUAL "Release")
        message(FATAL_ERROR
            "Empaquetado rechazado: el paquete solo se genera desde Release (configuracion pedida: '${PKG_CONFIG_NAME}'). "
            "Un paquete de Debug llevaria DLL de depuracion y no es entregable.")
    endif()
    if(PKG_TEST_HOOKS)
        message(FATAL_ERROR
            "Empaquetado rechazado: CAFETERIA_TEST_HOOKS esta activado. Reconfigurar con -DCAFETERIA_TEST_HOOKS=OFF.")
    endif()
    return()
endif()

# Importaciones (en minusculas) de un ejecutable o DLL, con el objdump del toolchain.
function(_pkg_imports file out_var)
    execute_process(
        COMMAND "${PKG_OBJDUMP}" -p "${file}"
        OUTPUT_VARIABLE dump
        ERROR_VARIABLE dump_err
        RESULT_VARIABLE dump_rc)
    if(NOT dump_rc EQUAL 0)
        message(FATAL_ERROR "No se pudo leer las importaciones de ${file} con ${PKG_OBJDUMP}: ${dump_err}")
    endif()
    string(REGEX MATCHALL "DLL Name: [^\r\n]+" found "${dump}")
    set(names "")
    foreach(entry IN LISTS found)
        string(REPLACE "DLL Name: " "" name "${entry}")
        string(STRIP "${name}" name)
        string(TOLOWER "${name}" name)
        list(APPEND names "${name}")
    endforeach()
    set(${out_var} "${names}" PARENT_SCOPE)
endfunction()

if(PKG_STEP STREQUAL "dlls")
    file(STRINGS "${PKG_SYSTEM_DLLS_FILE}" system_lines)
    set(system_dlls "")
    foreach(line IN LISTS system_lines)
        string(STRIP "${line}" line)
        if(line AND NOT line MATCHES "^#")
            string(TOLOWER "${line}" line)
            list(APPEND system_dlls "${line}")
        endif()
    endforeach()

    set(queue "${PKG_EXE}")
    set(copied "")
    while(queue)
        list(POP_FRONT queue current)
        _pkg_imports("${current}" imports)
        foreach(dll IN LISTS imports)
            if(dll IN_LIST system_dlls OR dll IN_LIST copied)
                continue()
            endif()
            set(source "")
            foreach(dir IN LISTS PKG_SEARCH_DIRS)
                if(EXISTS "${dir}/${dll}")
                    set(source "${dir}/${dll}")
                    break()
                endif()
            endforeach()
            if(NOT source)
                get_filename_component(who "${current}" NAME)
                message(FATAL_ERROR
                    "Empaquetado rechazado: ${who} importa '${dll}', que no es una DLL de Windows permitida "
                    "(cmake/windows_system_dlls.txt) y no se encontro en: ${PKG_SEARCH_DIRS}")
            endif()
            file(INSTALL "${source}" DESTINATION "${PKG_PREFIX}")
            list(APPEND copied "${dll}")
            list(APPEND queue "${PKG_PREFIX}/${dll}")
        endforeach()
    endwhile()
    message(STATUS "DLL incluidas: ${copied}")
    return()
endif()

if(PKG_STEP STREQUAL "manifest")
    set(lines "# Generado al empaquetar, lo lee --self-check. No editar.")
    list(APPEND lines "version=${PKG_VERSION}")
    file(GLOB_RECURSE assets RELATIVE "${PKG_SOURCE_DIR}" "${PKG_SOURCE_DIR}/assets/*")
    file(GLOB_RECURSE migrations RELATIVE "${PKG_SOURCE_DIR}" "${PKG_SOURCE_DIR}/migrations/*.sql")
    file(GLOB_RECURSE notices RELATIVE "${PKG_SOURCE_DIR}/packaging" "${PKG_SOURCE_DIR}/packaging/licencias/*")
    list(SORT assets)
    list(SORT migrations)
    list(SORT notices)
    foreach(f IN LISTS assets migrations notices)
        list(APPEND lines "${f}")
    endforeach()
    string(REPLACE ";" "\n" text "${lines}")
    file(WRITE "${PKG_PREFIX}/paquete.manifest" "${text}\n")
    return()
endif()

message(FATAL_ERROR "package_runtime.cmake: paso desconocido '${PKG_STEP}'")
