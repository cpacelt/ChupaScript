# Чистые части оболочки — отдельной библиотекой, чтобы их могли собрать и
# исполняемый файл, и тесты.
add_library(chupa_cli_core STATIC
    report.cpp
)

target_include_directories(chupa_cli_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/core/src
)

target_link_libraries(chupa_cli_core PRIVATE
    chupascript
    chupascript_compile_options
)

add_executable(chupa main.cpp)

target_include_directories(chupa PRIVATE ${CMAKE_SOURCE_DIR}/core/src)

target_link_libraries(chupa PRIVATE
    chupa_cli_core
    chupascript
    chupascript_compile_options
)

if(CHUPASCRIPT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
