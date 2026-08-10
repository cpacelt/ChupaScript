// Placeholder benchmark: proves Google Benchmark is wired end to end.
#include <benchmark/benchmark.h>

#include "chupascript/chupascript.h"

static void BM_Version(benchmark::State &state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(chupascript_version());
    }
}
BENCHMARK(BM_Version);
