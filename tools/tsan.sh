#!/usr/bin/env bash
# Build and run the test suites under the thread sanitizer.
#
# The compiler is deliberately not the system one, for the same reason
# tools/asan.sh gives: the sanitizer runtime shipped with Xcode 17 deadlocks
# inside its own initialisation on macOS 26, before main is reached. The LLVM
# 22 runtime from Homebrew works.
#
# What this catches: the engine's threading contract (chupascript.h) allows two
# Contexts to run on two threads, and the live-box counter is shared by all of
# them.
set -euo pipefail

LLVM_PREFIX="${LLVM_PREFIX:-/opt/homebrew/opt/llvm}"
BUILD_DIR="${BUILD_DIR:-build-tsan}"

if [[ ! -x "${LLVM_PREFIX}/bin/clang++" ]]; then
    echo "no ${LLVM_PREFIX}/bin/clang++ — install with: brew install llvm" >&2
    exit 1
fi

cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="${LLVM_PREFIX}/bin/clang" \
    -DCMAKE_CXX_COMPILER="${LLVM_PREFIX}/bin/clang++" \
    -DCMAKE_C_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" \
    -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" > /dev/null

cmake --build "${BUILD_DIR}" -j8

export TSAN_OPTIONS="halt_on_error=1:${TSAN_OPTIONS:-}"

"./${BUILD_DIR}/core/tests/chupascript_tests" "$@"
