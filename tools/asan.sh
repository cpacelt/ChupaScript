#!/usr/bin/env bash
# Сборка и прогон тестов под санитайзером адресов.
#
# Компилятор берётся не системный, и это обязательно. Рантайм ASan из Xcode 17
# (clang-1700, /Applications/Xcode.app/.../clang/17) виснет на macOS 26 ещё до
# main, во взаимной блокировке собственной инициализации:
#
#   __malloc_init → wrap_malloc_default_zone (ASan)
#     → AsanInitInternal → InitializeShadowMemory → MemoryRangeIsAvailable
#       → get_dyld_hdr → dyld_shared_cache_iterate_text_swift (Dyld)
#         → _Block_copy → malloc → __sanitizer_mz_malloc (ASan)
#           → AsanInitFromRtl → StaticSpinMutex::LockSlow → навсегда
#
# То есть ASan спрашивает у dyld карту памяти, в macOS 26 этот вопрос идёт
# через блочный API, который сам выделяет память, выделение попадает в
# перехватчик ASan, и тот повторно входит в инициализацию с уже взятым замком.
# Рантайм из LLVM 22 (Homebrew) это умеет и отрабатывает нормально.
#
# Если Homebrew LLVM не установлен: brew install llvm
set -euo pipefail

LLVM_PREFIX="${LLVM_PREFIX:-/opt/homebrew/opt/llvm}"
BUILD_DIR="${BUILD_DIR:-build-asan}"

if [[ ! -x "${LLVM_PREFIX}/bin/clang++" ]]; then
    echo "нет ${LLVM_PREFIX}/bin/clang++ — поставьте: brew install llvm" >&2
    exit 1
fi

cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="${LLVM_PREFIX}/bin/clang" \
    -DCMAKE_CXX_COMPILER="${LLVM_PREFIX}/bin/clang++" \
    -DCMAKE_C_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
    -DCMAKE_CXX_FLAGS="-fsanitize=address -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address" > /dev/null

cmake --build "${BUILD_DIR}" -j8

# detect_leaks по умолчанию на macOS выключен — включаем явно: утечка узла со
# счётчиком ссылок иначе не видна ничем.
export ASAN_OPTIONS="detect_leaks=1:${ASAN_OPTIONS:-}"

status=0
"./${BUILD_DIR}/core/tests/chupascript_tests" "$@" || status=$?
"./${BUILD_DIR}/cli/tests/chupa_cli_tests" || status=$?

# The cycle program must FAIL here: a reference-counted cycle is unreachable
# memory, which is exactly what the leak detector reports. A non-zero exit
# alone is not enough: cycle_leak_main.cpp also returns non-zero on three
# unrelated setup failures (setVariableText, compileScript, ctx.run), and a
# regression that broke the cycle itself would exit the same way. Requiring
# the LeakSanitizer marker in the captured output tells the two cases apart.
cycle_output=$("./${BUILD_DIR}/core/tests/chupascript_cycle_leak" 2>&1) && cycle_status=0 || cycle_status=$?
if [[ ${cycle_status} -eq 0 ]] || ! grep -q "ERROR: LeakSanitizer: detected memory leaks" <<< "${cycle_output}"; then
    echo "cycle leak went unreported — the leak detector is not doing its job" >&2
    echo "${cycle_output}" >&2
    status=1
fi

exit "${status}"
