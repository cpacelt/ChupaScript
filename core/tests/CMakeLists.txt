add_executable(chupascript_tests
    ast_test.cpp
    context_test.cpp
    data_test.cpp
    lexer_test.cpp
    parser_test.cpp
    smoke_test.cpp
    text_test.cpp
    value_test.cpp
)

# Tests reach into the internal layer as well as the public header.
target_include_directories(chupascript_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)

target_link_libraries(chupascript_tests PRIVATE
    chupascript
    chupascript_compile_options
    GTest::gtest_main
)

gtest_discover_tests(chupascript_tests)
