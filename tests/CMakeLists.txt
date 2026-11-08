cmake_minimum_required(VERSION 3.16)

# Copiar migraciones al directorio de binarios de pruebas para ejecución aislada
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../migrations" DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/../migrations" DESTINATION "${CMAKE_BINARY_DIR}")

add_custom_target(copy_migrations ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../migrations"
        "${CMAKE_CURRENT_BINARY_DIR}/migrations"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_CURRENT_SOURCE_DIR}/../migrations"
        "${CMAKE_BINARY_DIR}/migrations"
)

# Test de contratos, dominio y hashing
add_executable(test_contracts
    test_contracts.cpp
)
target_link_libraries(test_contracts PRIVATE
    app_contracts
    app_domain
    app_crypto
)
add_test(NAME ContractsTest COMMAND test_contracts)

# Test de almacenamiento SQLite, persistencia y seguridad
add_executable(test_storage
    test_storage.cpp
)
target_link_libraries(test_storage PRIVATE
    app_storage
    app_services
    app_contracts
    app_crypto
    sqlite3
)
add_dependencies(test_storage copy_migrations)
target_compile_definitions(test_storage PRIVATE
    MIGRATIONS_DIR_PATH="${CMAKE_CURRENT_BINARY_DIR}/migrations"
    SOURCE_MIGRATIONS_DIR="${CMAKE_SOURCE_DIR}/migrations"
    MIGRATIONS_FILE_PATH="${CMAKE_CURRENT_BINARY_DIR}/migrations/001_initial_schema.sql"
    SOURCE_MIGRATIONS_PATH="${CMAKE_SOURCE_DIR}/migrations/001_initial_schema.sql"
)
add_test(NAME StorageTest COMMAND test_storage)

# Test B1/I5: respaldo local seguro y restauración
add_executable(test_backup
    test_backup.cpp
)
target_link_libraries(test_backup PRIVATE
    app_storage
    app_services
    app_contracts
    app_crypto
    sqlite3
)
add_dependencies(test_backup copy_migrations)
target_compile_definitions(test_backup PRIVATE
    MIGRATIONS_DIR_PATH="${CMAKE_CURRENT_BINARY_DIR}/migrations"
    SOURCE_MIGRATIONS_DIR="${CMAKE_SOURCE_DIR}/migrations"
)
add_test(NAME BackupTest COMMAND test_backup)

# Test B2: servicio de reportes (resumen diario)
add_executable(test_reports
    test_reports.cpp
)
target_link_libraries(test_reports PRIVATE
    app_storage
    app_services
    app_contracts
    app_crypto
    sqlite3
)
add_dependencies(test_reports copy_migrations)
target_compile_definitions(test_reports PRIVATE
    MIGRATIONS_DIR_PATH="${CMAKE_CURRENT_BINARY_DIR}/migrations"
    SOURCE_MIGRATIONS_DIR="${CMAKE_SOURCE_DIR}/migrations"
)
add_test(NAME ReportsTest COMMAND test_reports)

# Ejecutable aislado de integración para reintento real de idempotencia a nivel de servicio
add_executable(test_idempotency_runner
    test_idempotency_runner.cpp
)
target_link_libraries(test_idempotency_runner PRIVATE
    app_storage
    app_services
    app_contracts
    app_crypto
    sqlite3
)
add_dependencies(test_idempotency_runner copy_migrations)

# Test de diseño, paginación y renderizado de tickets con wxWidgets
if(wxWidgets_FOUND)
    add_executable(test_ticket_printing
        test_ticket_printing.cpp
        ../src/frontend/ticket_layout.cpp
        ../src/frontend/app.rc
    )
    target_link_libraries(test_ticket_printing PRIVATE
        app_contracts
        ${wxWidgets_LIBRARIES}
    )
    if(MINGW)
        target_link_options(test_ticket_printing PRIVATE "-specs=${CMAKE_SOURCE_DIR}/cmake/no-default-manifest.specs")
    endif()
    add_test(NAME TicketPrintingTest COMMAND test_ticket_printing)
endif()

# Configurar entorno de CTest para reproducibilidad total en Windows sin preparar PATH manualmente
if(WIN32)
    if(CMAKE_CXX_COMPILER)
        get_filename_component(COMPILER_BIN_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
    endif()
    if(wxWidgets_LIB_DIR)
        file(GLOB WX_TEST_DLLS "${wxWidgets_LIB_DIR}/*.dll")
        if(WX_TEST_DLLS)
            file(COPY ${WX_TEST_DLLS} DESTINATION "${CMAKE_CURRENT_BINARY_DIR}")
        endif()
    endif()
    set_tests_properties(ContractsTest StorageTest BackupTest ReportsTest PROPERTIES
        ENVIRONMENT "PATH=${CMAKE_CURRENT_BINARY_DIR};${CMAKE_BINARY_DIR};${wxWidgets_LIB_DIR};${COMPILER_BIN_DIR};$ENV{PATH}"
    )
    if(wxWidgets_FOUND)
        set_tests_properties(TicketPrintingTest PROPERTIES
            ENVIRONMENT "PATH=${CMAKE_CURRENT_BINARY_DIR};${CMAKE_BINARY_DIR};${wxWidgets_LIB_DIR};${COMPILER_BIN_DIR};$ENV{PATH}"
        )
    endif()
endif()

