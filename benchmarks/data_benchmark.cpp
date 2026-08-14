// База производительности слоя данных: разбор текста литерала и укладка
// значений в хранилище. Измеряется путь, который выполняется один раз на
// переменную при сборке экрана.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

#include "data.hpp"
#include "diagnostic.hpp"
#include "store.hpp"

namespace {

using CS::Store;
using CS::Diagnostic;
using CS::Value;

/// Плоский объект из десяти полей — типичная переменная экрана.
void BM_Data_FlatObject(benchmark::State &state) {
    const std::string text =
        "{'id': 1, 'name': 'Вася', 'age': 30, 'active': true, 'score': 4.5,"
        " 'city': 'Москва', 'tag': null, 'rank': -2, 'level': 7, 'code': 'A1'}";

    for (auto _ : state) {
        Store store;
        Diagnostic diag;
        bool ok = CS::setVariable(store, "user", text, diag);
        if (!ok) { state.SkipWithError("setVariable failed"); return; }
        benchmark::DoNotOptimize(ok);
    }
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(text.size()));
}
BENCHMARK(BM_Data_FlatObject);

/// Массив из ста чисел — типичный список.
void BM_Data_NumberArray(benchmark::State &state) {
    std::string text = "[";
    for (int i = 0; i < 100; ++i) {
        if (i > 0) { text += ", "; }
        text += std::to_string(i);
    }
    text += "]";

    for (auto _ : state) {
        Store store;
        Diagnostic diag;
        bool ok = CS::setVariable(store, "items", text, diag);
        if (!ok) { state.SkipWithError("setVariable failed"); return; }
        benchmark::DoNotOptimize(ok);
    }
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(text.size()));
}
BENCHMARK(BM_Data_NumberArray);

/// Строка без экранирования против строки с ним: цена временного буфера.
void BM_Data_PlainString(benchmark::State &state) {
    const std::string text = "'" + std::string(200, 'x') + "'";
    for (auto _ : state) {
        Store store;
        Diagnostic diag;
        bool ok = CS::setVariable(store, "s", text, diag);
        if (!ok) { state.SkipWithError("setVariable failed"); return; }
        benchmark::DoNotOptimize(ok);
    }
}
BENCHMARK(BM_Data_PlainString);

void BM_Data_EscapedString(benchmark::State &state) {
    std::string body;
    for (int i = 0; i < 100; ++i) { body += "x\\n"; }
    const std::string text = "'" + body + "'";

    for (auto _ : state) {
        Store store;
        Diagnostic diag;
        bool ok = CS::setVariable(store, "s", text, diag);
        if (!ok) { state.SkipWithError("setVariable failed"); return; }
        benchmark::DoNotOptimize(ok);
    }
}
BENCHMARK(BM_Data_EscapedString);

/// Поиск глобальной переменной при разном числе имён.
void BM_Data_GlobalLookup(benchmark::State &state) {
    const int names = static_cast<int>(state.range(0));
    Store store;
    Diagnostic diag;
    for (int i = 0; i < names; ++i) {
        const std::string name = "var" + std::to_string(i);
        if (!CS::setVariable(store, name, "1", diag)) {
            state.SkipWithError("setVariable failed");
            return;
        }
    }
    const std::string last = "var" + std::to_string(names - 1);

    for (auto _ : state) {
        Value found = store.global(last);
        benchmark::DoNotOptimize(found);
    }
}
BENCHMARK(BM_Data_GlobalLookup)->Arg(3)->Arg(10)->Arg(30);

}  // namespace
