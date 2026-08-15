add_library(chupascript STATIC
    src/ast.cpp
    src/builtin.cpp
    src/c_api.cpp
    src/check.cpp
    src/compile.cpp
    src/store.cpp
    src/data.cpp
    src/eval.cpp
    src/expression.cpp
    src/lexer.cpp
    src/operator.cpp
    src/parser.cpp
    src/script.cpp
    src/text.cpp
)

target_include_directories(chupascript PUBLIC include)
target_link_libraries(chupascript PRIVATE chupascript_compile_options)

if(CHUPASCRIPT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
