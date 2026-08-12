add_library(chupascript STATIC
    src/ast.cpp
    src/context.cpp
    src/data.cpp
    src/lexer.cpp
    src/parser.cpp
    src/text.cpp
    src/version.cpp
)

target_include_directories(chupascript PUBLIC include)
target_link_libraries(chupascript PRIVATE chupascript_compile_options)

if(CHUPASCRIPT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
