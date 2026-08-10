cmake_minimum_required(VERSION 3.20)

project(ChupaScript
    VERSION 0.1.0
    DESCRIPTION "ChupaScript language core"
    LANGUAGES C CXX
)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR
        "In-source builds are not allowed. "
        "Create a separate build directory: `cmake -B build`.")
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Emit compile_commands.json for clangd / IDE integration.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Enable tests by default when we're the top-level project; consumers
# embedding ChupaScript as a subdirectory won't build our tests.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(CHUPASCRIPT_BUILD_TESTS "Build ChupaScript tests" ON)
else()
    option(CHUPASCRIPT_BUILD_TESTS "Build ChupaScript tests" OFF)
endif()

# Benchmarks are opt-in — they pull Google Benchmark and want an optimized
# build; keep them off the default path. Run with:
#   cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON
option(CHUPASCRIPT_BUILD_BENCHMARKS "Build ChupaScript benchmarks" OFF)

# ---------------------------------------------------------------------------
# Compile options (applied ONLY to our targets, not to third-party deps).
#
# Usage: target_link_libraries(<target> PRIVATE chupascript_compile_options)
# ---------------------------------------------------------------------------
add_library(chupascript_compile_options INTERFACE)

option(CHUPASCRIPT_WERROR             "Treat warnings as errors"              OFF)
option(CHUPASCRIPT_SANITIZE_ADDRESS   "Build with AddressSanitizer + LeakSan" OFF)
option(CHUPASCRIPT_SANITIZE_UNDEFINED "Build with UndefinedBehaviorSanitizer" OFF)
option(CHUPASCRIPT_SANITIZE_THREAD    "Build with ThreadSanitizer"            OFF)

if(CHUPASCRIPT_SANITIZE_ADDRESS AND CHUPASCRIPT_SANITIZE_THREAD)
    message(FATAL_ERROR "CHUPASCRIPT_SANITIZE_ADDRESS and CHUPASCRIPT_SANITIZE_THREAD are mutually exclusive.")
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(chupascript_compile_options INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Wdouble-promotion
        -Wformat=2
    )
    if(CHUPASCRIPT_WERROR)
        target_compile_options(chupascript_compile_options INTERFACE -Werror)
    endif()

    set(_cs_sanitizers "")
    if(CHUPASCRIPT_SANITIZE_ADDRESS)
        list(APPEND _cs_sanitizers "address")
    endif()
    if(CHUPASCRIPT_SANITIZE_UNDEFINED)
        list(APPEND _cs_sanitizers "undefined")
    endif()
    if(CHUPASCRIPT_SANITIZE_THREAD)
        list(APPEND _cs_sanitizers "thread")
    endif()

    if(_cs_sanitizers)
        list(JOIN _cs_sanitizers "," _cs_sanitize_arg)
        target_compile_options(chupascript_compile_options INTERFACE
            -fsanitize=${_cs_sanitize_arg}
            -fno-omit-frame-pointer
            -g
        )
        target_link_options(chupascript_compile_options INTERFACE
            -fsanitize=${_cs_sanitize_arg}
        )
    endif()
endif()

# ---------------------------------------------------------------------------
# Third-party dependencies (pulled only when tests / benchmarks need them).
# ---------------------------------------------------------------------------
if(CHUPASCRIPT_BUILD_TESTS)
    enable_testing()

    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG        v1.15.2
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googletest)

    # Silence third-party warnings by marking GoogleTest headers as SYSTEM,
    # so our strict flags don't fire on them.
    foreach(_gt_tgt gtest gtest_main gmock gmock_main)
        if(TARGET ${_gt_tgt})
            get_target_property(_gt_inc ${_gt_tgt} INTERFACE_INCLUDE_DIRECTORIES)
            if(_gt_inc)
                set_target_properties(${_gt_tgt} PROPERTIES
                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_gt_inc}")
            endif()
        endif()
    endforeach()

    include(GoogleTest)
endif()

if(CHUPASCRIPT_BUILD_BENCHMARKS)
    include(FetchContent)
    set(BENCHMARK_ENABLE_TESTING     OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
    set(BENCHMARK_ENABLE_INSTALL     OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
        googlebenchmark
        GIT_REPOSITORY https://github.com/google/benchmark.git
        GIT_TAG        v1.9.1
        GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(googlebenchmark)

    # Mark benchmark headers SYSTEM so our strict flags don't fire on them.
    foreach(_bm_tgt benchmark benchmark_main)
        if(TARGET ${_bm_tgt})
            get_target_property(_bm_inc ${_bm_tgt} INTERFACE_INCLUDE_DIRECTORIES)
            if(_bm_inc)
                set_target_properties(${_bm_tgt} PROPERTIES
                    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES "${_bm_inc}")
            endif()
        endif()
    endforeach()
endif()

add_subdirectory(core)

# Interactive REPL (top-level project only).
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    option(CHUPASCRIPT_BUILD_CLI "Build the ChupaScript REPL" ON)
else()
    option(CHUPASCRIPT_BUILD_CLI "Build the ChupaScript REPL" OFF)
endif()

if(CHUPASCRIPT_BUILD_CLI)
    add_subdirectory(cli)
endif()

if(CHUPASCRIPT_BUILD_BENCHMARKS)
    add_subdirectory(benchmarks)
endif()
