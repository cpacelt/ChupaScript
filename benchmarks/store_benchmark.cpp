// База производительности слоя хранения: цена роста, поиска и обхода.
// Служит основанием для решения по docs/backlog.md B1 — заменять ли пулы
// ареной.
#include <benchmark/benchmark.h>

#include <string>

#include "context.hpp"

namespace {

using CS::Context;
using CS::Value;

/// Наполнение массива через push: цена удвоений и переездов.
void BM_Store_ArrayPush(benchmark::State &state) {
    const int count = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Context ctx;
        const Value a = ctx.makeArray();
        for (int i = 0; i < count; ++i) {
            ctx.arrayPush(a, Value::number(static_cast<double>(i)));
        }
        // Локальная переменная, а не временное значение: DoNotOptimize от
        // rvalue не переживает смены версии Google Benchmark.
        std::uint32_t filled = ctx.arrayCount(a);
        benchmark::DoNotOptimize(filled);
    }
    state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_Store_ArrayPush)->Arg(1000);

/// То же с заранее известным размером: столько стоило бы наполнение без роста.
void BM_Store_ArrayPushReserved(benchmark::State &state) {
    const int count = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Context ctx;
        const Value a = ctx.makeArray(static_cast<std::uint32_t>(count));
        for (int i = 0; i < count; ++i) {
            ctx.arrayPush(a, Value::number(static_cast<double>(i)));
        }
        std::uint32_t filled = ctx.arrayCount(a);
        benchmark::DoNotOptimize(filled);
    }
    state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_Store_ArrayPushReserved)->Arg(1000);

/// Обход массива: цена разыменования через индекс.
void BM_Store_ArrayTraverse(benchmark::State &state) {
    Context ctx;
    const Value a = ctx.makeArray(1000);
    for (int i = 0; i < 1000; ++i) {
        ctx.arrayPush(a, Value::number(static_cast<double>(i)));
    }

    for (auto _ : state) {
        double sum = 0.0;
        for (std::uint32_t i = 0; i < ctx.arrayCount(a); ++i) {
            sum += ctx.arrayAt(a, i).numberValue();
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_Store_ArrayTraverse);

/// Объект заданного размера с ключами вида "keyNN".
Value makeFilledObject(Context &ctx, int keys) {
    const Value o = ctx.makeObject(static_cast<std::uint32_t>(keys));
    for (int i = 0; i < keys; ++i) {
        ctx.objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)));
    }
    return o;
}

/// Поиск ключа: проверка утверждения «двоичный поиск дешевле хеша на 3–20».
void BM_Store_ObjectGet(benchmark::State &state) {
    const int keys = static_cast<int>(state.range(0));
    Context ctx;
    const Value o = makeFilledObject(ctx, keys);
    const std::string last = "key" + std::to_string(keys - 1);

    for (auto _ : state) {
        Value found = ctx.objectGet(o, last);
        benchmark::DoNotOptimize(found);
    }
}
BENCHMARK(BM_Store_ObjectGet)->Arg(3)->Arg(8)->Arg(20);

/// Вставка keys ключей в объект с заранее выделенной ёмкостью — роста нет,
/// сдвиг хвоста при каждой вставке есть; в измеряемое время входит и
/// создание/уничтожение самого Context.
void BM_Store_ObjectInsert(benchmark::State &state) {
    const int keys = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Context ctx;
        const Value o = makeFilledObject(ctx, keys);
        std::uint32_t filled = ctx.objectCount(o);
        benchmark::DoNotOptimize(filled);
    }
    state.SetItemsProcessed(state.iterations() * keys);
}
BENCHMARK(BM_Store_ObjectInsert)->Arg(3)->Arg(8)->Arg(20);

/// Создание строки: копия в пул текста.
void BM_Store_MakeString(benchmark::State &state) {
    const std::string text(32, 'x');
    for (auto _ : state) {
        Context ctx;
        for (int i = 0; i < 100; ++i) {
            Value made = ctx.makeString(text);
            benchmark::DoNotOptimize(made);
        }
    }
    state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK(BM_Store_MakeString);

}  // namespace
