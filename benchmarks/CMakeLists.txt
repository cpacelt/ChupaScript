# Benchmarks link the core lib and reach into src/ for isolated-stage timing
# (internal headers are not part of the public API).
add_executable(chupascript_benchmarks
    data_benchmark.cpp
    eval_benchmark.cpp
    host_benchmark.cpp
    lexer_benchmark.cpp
    parser_benchmark.cpp
    store_benchmark.cpp
)

target_include_directories(chupascript_benchmarks PRIVATE
    ${CMAKE_SOURCE_DIR}/core/src
)

target_link_libraries(chupascript_benchmarks PRIVATE
    chupascript
    chupascript_compile_options
    benchmark::benchmark_main
)
