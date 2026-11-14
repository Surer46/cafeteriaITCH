cmake_minimum_required(VERSION 3.16)
project(CafeteriaITCH VERSION 0.1.0 LANGUAGES C CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opción para ganchos de prueba en app_desktop (OFF por defecto)
option(CAFETERIA_TEST_HOOKS "Habilita ganchos de prueba en app_desktop para simulación de errores" OFF)

# Rutas globales de inclusión
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# 1. Biblioteca de SQLite embebido
add_library(sqlite3 STATIC
    third_party/sqlite/sqlite3.c
    third_party/sqlite/sqlite3.h
)
target_include_directories(sqlite3 PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
)
target_compile_definitions(sqlite3 PRIVATE
    SQLITE_ENABLE_FTS5
    SQLITE_ENABLE_JSON1
    SQLITE_ENABLE_RTREE
    SQLITE_THREADSAFE=1
)
if(NOT MSVC)
    target_compile_options(sqlite3 PRIVATE -w)
endif()

# 2. Biblioteca de Criptografía y Hash de contraseñas (PBKDF2-HMAC-SHA256)
add_library(app_crypto STATIC
    src/crypto/password_hasher.cpp
    src/crypto/random_bytes.cpp
    include/app/crypto/password_hasher.hpp
    include/app/crypto/random_bytes.hpp
)
target_include_directories(app_crypto PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 3. Biblioteca de Contratos compartidos (include/app/contracts/)
add_library(app_contracts STATIC
    src/contracts/money.cpp
    include/app/contracts/money.hpp
    include/app/contracts/result.hpp
    include/app/contracts/dto.hpp
    include/app/contracts/account_service.hpp
    include/app/contracts/catalog_service.hpp
    include/app/contracts/cart_service.hpp
    include/app/contracts/sale_service.hpp
    include/app/contracts/inventory_service.hpp
    include/app/contracts/shift_service.hpp
    include/app/contracts/report_service.hpp
    include/app/contracts/backup_service.hpp
    include/app/contracts/print_config_service.hpp
)
target_include_directories(app_contracts PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 4. Biblioteca de Dominio (include/app/domain/, src/domain/)
add_library(app_domain STATIC
    src/domain/unit_converter.cpp
    src/domain/clock.cpp
    src/domain/stock_valuation.cpp
    include/app/domain/unit_converter.hpp
    include/app/domain/clock.hpp
    include/app/domain/stock_valuation.hpp
)
target_include_directories(app_domain PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(app_domain PUBLIC
    app_contracts
)

# 5. Biblioteca de Almacenamiento SQLite (include/app/storage/, src/storage/)
add_library(app_storage STATIC
    src/storage/sqlite_database.cpp
    src/storage/migration_runner.cpp
    include/app/storage/sqlite_database.hpp
    include/app/storage/migration_runner.hpp
)
target_include_directories(app_storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite
)
target_link_libraries(app_storage PUBLIC
    app_contracts
    sqlite3
)

# 6. Biblioteca de Servicios (include/app/services/, src/services/)
add_library(app_services STATIC
    src/services/identifiers.cpp
    src/services/stock_ledger.cpp
    src/services/operating_context.cpp
    src/services/mock_services.cpp
    src/services/sqlite_account_service.cpp
    src/services/sqlite_backup_service.cpp
    src/services/sqlite_report_service.cpp
    src/services/sqlite_catalog_service.cpp
    src/services/sqlite_inventory_service.cpp
    src/services/sqlite_shift_service.cpp
    src/services/sqlite_cart_service.cpp
    src/services/sqlite_sale_service.cpp
    src/services/sqlite_print_config_service.cpp
    src/services/data_dir_policy.cpp
    src/services/package_self_check.cpp
    include/app/services/data_dir_policy.hpp
    include/app/services/package_self_check.hpp
    include/app/services/identifiers.hpp
    include/app/services/stock_ledger.hpp
    include/app/services/operating_context.hpp
    include/app/services/mock_services.hpp
    include/app/services/sqlite_account_service.hpp
    include/app/services/sqlite_backup_service.hpp
    include/app/services/sqlite_report_service.hpp
    include/app/services/sqlite_catalog_service.hpp
    include/app/services/sqlite_inventory_service.hpp
    include/app/services/sqlite_shift_service.hpp
    include/app/services/sqlite_cart_service.hpp
    include/app/services/sqlite_sale_service.hpp
    include/app/services/sqlite_print_config_service.hpp
)
target_include_directories(app_services PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(app_services PUBLIC
    app_contracts
    app_domain
    app_storage
    app_crypto
)

# 7. Aplicación de Escritorio wxWidgets (src/frontend/)
set(wxWidgets_ROOT_DIR "" CACHE PATH "Ruta raíz del SDK wxWidgets (o definir variable de entorno WXWIN, WXWIDGETS_ROOT o WX_DIR)")
if(NOT wxWidgets_ROOT_DIR)
    if(DEFINED ENV{WXWIN})
        set(wxWidgets_ROOT_DIR "$ENV{WXWIN}")
    elseif(DEFINED ENV{WXWIDGETS_ROOT})
        set(wxWidgets_ROOT_DIR "$ENV{WXWIDGETS_ROOT}")
    elseif(DEFINED ENV{WX_DIR})
        set(wxWidgets_ROOT_DIR "$ENV{WX_DIR}")
    endif()
endif()
if(wxWidgets_ROOT_DIR AND NOT wxWidgets_LIB_DIR)
    file(GLOB WX_CANDIDATE_LIB_DIRS
        "${wxWidgets_ROOT_DIR}/lib/gcc*_x64_dll"
        "${wxWidgets_ROOT_DIR}/lib/gcc*_dll"
        "${wxWidgets_ROOT_DIR}/lib/vc*_x64_dll"
        "${wxWidgets_ROOT_DIR}/lib"
    )
    if(WX_CANDIDATE_LIB_DIRS)
        list(GET WX_CANDIDATE_LIB_DIRS 0 FIRST_LIB_DIR)
        set(wxWidgets_LIB_DIR "${FIRST_LIB_DIR}" CACHE PATH "Ruta a bibliotecas compiladas de wxWidgets")
    endif()
endif()

find_package(wxWidgets COMPONENTS core base OPTIONAL_COMPONENTS net)
if(wxWidgets_FOUND)
    include(${wxWidgets_USE_FILE})
    add_executable(app_desktop WIN32
        src/frontend/main_app.cpp
        src/frontend/main_window.cpp
        src/frontend/main_window.hpp
        src/frontend/header_panel.cpp
        src/frontend/header_panel.hpp
        src/frontend/sidebar_panel.cpp
        src/frontend/sidebar_panel.hpp
        src/frontend/subnav_bar.cpp
        src/frontend/subnav_bar.hpp
        src/frontend/initial_owner_dialog.cpp
        src/frontend/initial_owner_dialog.hpp
        src/frontend/login_dialog.cpp
        src/frontend/login_dialog.hpp
        src/frontend/backoffice_users_panel.cpp
        src/frontend/backoffice_users_panel.hpp
        src/frontend/ui_helpers.cpp
        src/frontend/ui_helpers.hpp
        src/frontend/toggle_switch.cpp
        src/frontend/toggle_switch.hpp
        src/frontend/backoffice_roles_panel.cpp
        src/frontend/backoffice_roles_panel.hpp
        src/frontend/backoffice_print_config_panel.cpp
        src/frontend/backoffice_print_config_panel.hpp
        src/frontend/checkout_dialog.cpp
        src/frontend/checkout_dialog.hpp
        src/frontend/ticket_layout.cpp
        src/frontend/ticket_layout.hpp
        src/frontend/ticket_print_dialog.cpp
        src/frontend/ticket_print_dialog.hpp
        src/frontend/ticket_history_dialog.cpp
        src/frontend/ticket_history_dialog.hpp
        src/frontend/pos_panel.cpp
        src/frontend/pos_panel.hpp
        src/frontend/cash_shift_panel.cpp
        src/frontend/cash_shift_panel.hpp
        src/frontend/audit_log_panel.cpp
        src/frontend/audit_log_panel.hpp
        src/frontend/inventory_ingredients_panel.cpp
        src/frontend/inventory_ingredients_panel.hpp
        src/frontend/theme.cpp
        src/frontend/theme.hpp
        src/frontend/app.rc
    )
    target_link_libraries(app_desktop PRIVATE
        app_services
        app_contracts
        app_domain
        app_storage
        app_crypto
        ${wxWidgets_LIBRARIES}
    )
    # Version: fuente unica project(VERSION). Llega al recurso de version y a la autocomprobacion.
    configure_file(${CMAKE_CURRENT_SOURCE_DIR}/cmake/version.rc.in
                   ${CMAKE_BINARY_DIR}/generated/cafeteria_version.rc @ONLY)
    target_include_directories(app_desktop PRIVATE ${CMAKE_BINARY_DIR}/generated)
    target_compile_definitions(app_desktop PRIVATE CAFETERIA_VERSION="${PROJECT_VERSION}")
    set_property(TARGET app_desktop APPEND PROPERTY LINK_DEPENDS ${CMAKE_BINARY_DIR}/generated/cafeteria_version.rc)
    set_source_files_properties(src/frontend/app.rc PROPERTIES
        OBJECT_DEPENDS ${CMAKE_BINARY_DIR}/generated/cafeteria_version.rc)

    if(CAFETERIA_TEST_HOOKS)
        target_compile_definitions(app_desktop PRIVATE CAFETERIA_TEST_HOOKS)
        message(STATUS "CAFETERIA_TEST_HOOKS activado para app_desktop.")
    endif()
    if(MINGW)
        target_link_options(app_desktop PRIVATE "-specs=${CMAKE_CURRENT_SOURCE_DIR}/cmake/no-default-manifest.specs")
    endif()
    # Copiar recursos (fuentes e iconos) al directorio de compilación
    add_custom_target(copy_app_assets ALL
        COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/assets
            ${CMAKE_BINARY_DIR}/assets
        COMMENT "Copiando recursos (fuentes e iconos) a ${CMAKE_BINARY_DIR}/assets"
    )
    add_dependencies(app_desktop copy_app_assets)
    message(STATUS "wxWidgets encontrado. Objetivo app_desktop habilitado.")
else()
    message(WARNING "wxWidgets no fue encontrado. app_desktop desactivado, pero bibliotecas y pruebas siguen listas.")
endif()

# Copia automática de DLLs de compilador y wxWidgets para ejecución y pruebas reproducibles
if(WIN32)
    if(CMAKE_CXX_COMPILER)
        get_filename_component(COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        file(GLOB COMPILER_RUNTIME_DLLS
            "${COMPILER_BIN_DIR}/libgcc_s_*.dll"
            "${COMPILER_BIN_DIR}/libstdc++-*.dll"
            "${COMPILER_BIN_DIR}/libwinpthread-*.dll"
        )
        if(COMPILER_RUNTIME_DLLS)
            file(COPY ${COMPILER_RUNTIME_DLLS} DESTINATION "${CMAKE_BINARY_DIR}")
            file(COPY ${COMPILER_RUNTIME_DLLS} DESTINATION "${CMAKE_BINARY_DIR}/tests")
        endif()
    endif()

    if(wxWidgets_LIB_DIR)
        file(GLOB WX_RUNTIME_DLLS "${wxWidgets_LIB_DIR}/*.dll")
        if(WX_RUNTIME_DLLS)
            file(COPY ${WX_RUNTIME_DLLS} DESTINATION "${CMAKE_BINARY_DIR}")
        endif()
    endif()
endif()

# 9. Paquete (install + CPack). Solo Release; el ZIP es la copia portable (lleva portable.flag).
#    El instalador de packaging/cafeteria.iss usa solo el componente "app" (sin portable.flag).
if(TARGET app_desktop AND WIN32)
    set(PKG_SYSTEM_DLLS_FILE ${CMAKE_CURRENT_SOURCE_DIR}/cmake/windows_system_dlls.txt)
    get_filename_component(PKG_COMPILER_BIN "${CMAKE_CXX_COMPILER}" DIRECTORY)
    set(PKG_SEARCH_DIRS "${PKG_COMPILER_BIN}")
    if(wxWidgets_LIB_DIR)
        list(APPEND PKG_SEARCH_DIRS "${wxWidgets_LIB_DIR}")
    endif()
    set(PKG_TEST_HOOKS_VALUE OFF)
    if(CAFETERIA_TEST_HOOKS)
        set(PKG_TEST_HOOKS_VALUE ON)
    endif()

    # Variables comunes para cada paso (se evaluan al instalar).
    set(PKG_COMMON_CODE "
        set(PKG_CONFIG_NAME \"\${CMAKE_INSTALL_CONFIG_NAME}\")
        set(PKG_TEST_HOOKS ${PKG_TEST_HOOKS_VALUE})
        set(PKG_PREFIX \"\$ENV{DESTDIR}\${CMAKE_INSTALL_PREFIX}\")
        set(PKG_OBJDUMP \"${CMAKE_OBJDUMP}\")
        set(PKG_SEARCH_DIRS \"${PKG_SEARCH_DIRS}\")
        set(PKG_SYSTEM_DLLS_FILE \"${PKG_SYSTEM_DLLS_FILE}\")
        set(PKG_SOURCE_DIR \"${CMAKE_CURRENT_SOURCE_DIR}\")
        set(PKG_VERSION \"${PROJECT_VERSION}\")
        set(PKG_EXE \"\${PKG_PREFIX}/CafeteriaITCH.exe\")
    ")

    install(CODE "${PKG_COMMON_CODE}
        set(PKG_STEP check)
        include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_runtime.cmake\")
    " COMPONENT app)
    # Solo al instalar el ejecutable se renombra; en el arbol de compilacion sigue siendo app_desktop.exe.
    install(PROGRAMS $<TARGET_FILE:app_desktop> DESTINATION . RENAME CafeteriaITCH.exe COMPONENT app)
    install(CODE "${PKG_COMMON_CODE}
        set(PKG_STEP dlls)
        include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_runtime.cmake\")
    " COMPONENT app)
    install(DIRECTORY assets/ DESTINATION assets COMPONENT app)
    install(DIRECTORY migrations/ DESTINATION migrations COMPONENT app FILES_MATCHING PATTERN "*.sql")
    install(DIRECTORY packaging/licencias/ DESTINATION licencias COMPONENT app)
    install(CODE "${PKG_COMMON_CODE}
        set(PKG_STEP manifest)
        include(\"${CMAKE_CURRENT_SOURCE_DIR}/cmake/package_runtime.cmake\")
    " COMPONENT app)

    # Marcador de copia portable: solo en el ZIP, nunca en el instalador.
    file(WRITE ${CMAKE_BINARY_DIR}/generated/portable.flag
        "Copia portable: los datos se guardan en la carpeta data junto al ejecutable. No borrar este archivo.
")
    install(FILES ${CMAKE_BINARY_DIR}/generated/portable.flag DESTINATION . COMPONENT portable_marker)

    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(PKG_ARCH win64)
    else()
        set(PKG_ARCH win32)
    endif()
    set(CPACK_GENERATOR ZIP)
    set(CPACK_PACKAGE_NAME CafeteriaITCH)
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_FILE_NAME "CafeteriaITCH-${PROJECT_VERSION}-portable-${PKG_ARCH}")
    set(CPACK_ARCHIVE_COMPONENT_INSTALL OFF)
    set(CPACK_PACKAGE_DIRECTORY ${CMAKE_BINARY_DIR})
    include(CPack)
endif()

# 8. Pruebas unitarias y de integración (CTest)
enable_testing()
add_subdirectory(tests)
