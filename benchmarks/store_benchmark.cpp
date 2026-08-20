// База производительности слоя хранения: цена роста, поиска и обхода.
// Служит основанием для решения по docs/backlog.md B1 — заменять ли пулы
// ареной.
#include <benchmark/benchmark.h>

#include <string>

#include "store.hpp"
#include "aggregate.hpp"

namespace {

using CS::Store;
using CS::Value;

/// Наполнение массива через push: цена удвоений и переездов.
void BM_Store_ArrayPush(benchmark::State &state) {
    CS::Deferred dead;
    const int count = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Store store;
        const Value a = CS::makeArray(0, dead);
        for (int i = 0; i < count; ++i) {
            CS::arrayPush(a, Value::number(static_cast<double>(i)));
        }
        // Локальная переменная, а не временное значение: DoNotOptimize от
        // rvalue не переживает смены версии Google Benchmark.
        std::uint32_t filled = CS::arrayCount(a);
        benchmark::DoNotOptimize(filled);
    }
    state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_Store_ArrayPush)->Arg(1000);

/// То же с заранее известным размером: столько стоило бы наполнение без роста.
void BM_Store_ArrayPushReserved(benchmark::State &state) {
    CS::Deferred dead;
    const int count = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Store store;
        const Value a = CS::makeArray(static_cast<std::uint32_t>(count), dead);
        for (int i = 0; i < count; ++i) {
            CS::arrayPush(a, Value::number(static_cast<double>(i)));
        }
        std::uint32_t filled = CS::arrayCount(a);
        benchmark::DoNotOptimize(filled);
    }
    state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_Store_ArrayPushReserved)->Arg(1000);

/// Обход массива: цена разыменования через индекс.
void BM_Store_ArrayTraverse(benchmark::State &state) {
    Store store;
    CS::Deferred dead;
    const Value a = CS::makeArray(1000, dead);
    for (int i = 0; i < 1000; ++i) {
        CS::arrayPush(a, Value::number(static_cast<double>(i)));
    }

    for (auto _ : state) {
        double sum = 0.0;
        for (std::uint32_t i = 0; i < CS::arrayCount(a); ++i) {
            sum += CS::arrayAt(a, i).numberValue();
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_Store_ArrayTraverse);

/// Объект заданного размера с ключами вида "keyNN".
/// dead принадлежит вызывающему: единственная ссылка на новый объект — ссылка
/// создателя, и слить список здесь значило бы убить его на возврате.
Value makeFilledObject(Store &store, CS::Deferred &dead, int keys) {
    const Value o = CS::makeObject(store.keys(), static_cast<std::uint32_t>(keys), dead);
    for (int i = 0; i < keys; ++i) {
        CS::objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)), dead);
    }
    return o;
}

/// Поиск ключа: проверка утверждения «двоичный поиск дешевле хеша на 3–20».
void BM_Store_ObjectGet(benchmark::State &state) {
    const int keys = static_cast<int>(state.range(0));
    Store store;
    CS::Deferred dead;
    const Value o = makeFilledObject(store, dead, keys);
    const std::string last = "key" + std::to_string(keys - 1);

    for (auto _ : state) {
        Value found = CS::objectGet(o, last);
        benchmark::DoNotOptimize(found);
    }
}
BENCHMARK(BM_Store_ObjectGet)->Arg(3)->Arg(8)->Arg(20);

/// Вставка keys ключей в объект с заранее выделенной ёмкостью — роста нет,
/// сдвиг хвоста при каждой вставке есть; в измеряемое время входит и
/// создание/уничтожение самого Store.
void BM_Store_ObjectInsert(benchmark::State &state) {
    CS::Deferred dead;
    const int keys = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Store store;
        const Value o = makeFilledObject(store, dead, keys);
        std::uint32_t filled = CS::objectCount(o);
        benchmark::DoNotOptimize(filled);
    }
    state.SetItemsProcessed(state.iterations() * keys);
}
BENCHMARK(BM_Store_ObjectInsert)->Arg(3)->Arg(8)->Arg(20);

/// Creating a string: bytes copied into a reference-counted box.
/// 32 bytes stays above Value::kInlineCapacity (task 8), on both sides of
/// that task, so this benchmark's cost is the box path alone and is not
/// expected to move because of it.
void BM_Value_Materialize(benchmark::State &state) {
    const std::string text(32, 'x');
    for (auto _ : state) {
        CS::Deferred dead;
        for (int i = 0; i < 100; ++i) {
            Value made = CS::materialize(text, dead);
            benchmark::DoNotOptimize(made);
        }
    }
    state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK(BM_Value_Materialize);

/// Creating a string short enough to live inside the Value itself (task 8):
/// no box, no reference count, no allocation. Added alongside the existing
/// BM_Value_Materialize rather than replacing it, because that one is a
/// useful fixed point for the box path and predates this task; this one is
/// what checkpoint B's "materialize should get cheaper for short strings"
/// expectation is actually about, and nothing in the suite measured it
/// before now.
void BM_Value_MaterializeInline(benchmark::State &state) {
    const std::string text(10, 'x');
    for (auto _ : state) {
        CS::Deferred dead;
        for (int i = 0; i < 100; ++i) {
            Value made = CS::materialize(text, dead);
            benchmark::DoNotOptimize(made);
        }
    }
    state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK(BM_Value_MaterializeInline);

}  // namespace
