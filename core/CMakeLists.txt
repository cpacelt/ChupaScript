add_library(chupascript STATIC
    src/ast.cpp
    src/builtin.cpp
    src/c_api.cpp
    src/callee.cpp
    src/check.cpp
    src/compile.cpp
    src/context.cpp
    src/store.cpp
    src/data.cpp
    src/epoch.cpp
    src/eval.cpp
    src/host.cpp
    src/keytable.cpp
    src/box.cpp
    src/deferred.cpp
    src/expression.cpp
    src/lexer.cpp
    src/operator.cpp
    src/parser.cpp
    src/script.cpp
    src/text.cpp
)

target_include_directories(chupascript PUBLIC include)
target_link_libraries(chupascript
    PRIVATE chupascript_compile_options
    PRIVATE double_conversion
)

if(CHUPASCRIPT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
