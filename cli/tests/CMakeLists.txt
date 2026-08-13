add_executable(chupa_cli_tests
    report_test.cpp
)

# Тесты печатника обращаются к внутреннему слою ядра — как и тесты самого ядра.
target_include_directories(chupa_cli_tests PRIVATE ${CMAKE_SOURCE_DIR}/core/src)

target_link_libraries(chupa_cli_tests PRIVATE
    chupa_cli_core
    chupascript
    chupascript_compile_options
    GTest::gtest_main
)

gtest_discover_tests(chupa_cli_tests)
