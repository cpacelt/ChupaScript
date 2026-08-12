// База производительности вычислителя. В отличие от разбора, который для
// одного выражения случается однажды, вычисление повторяется на каждой
// перерисовке — поэтому меряется именно оно, с уже разобранным деревом.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string_view>

#include "chupascript/chupascript.h"
#include "ast.hpp"
#include "context.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "eval.hpp"
#include "parser.hpp"
#include "text.hpp"

namespace {

using CS::Ast;
using CS::Context;
using CS::Diagnostic;
using CS::Value;

/// Наполняет контекст данными, на которых меряются пути.
bool fill(Context &ctx) {
    Diagnostic diag;
    return CS::setVariable(ctx, "user",
                           "{'name': 'Вася', 'profile': {'city': {'code': "
                           "{'zip': 101000}}}}",
                           diag) &&
           CS::setVariable(ctx, "items", "[10, 20, 30]", diag) &&
           CS::setVariable(ctx, "map", "{'0': 'zero', '1': 'one'}", diag);
}

/// Общая часть: наполнить контекст, разобрать выражение, мерить вычисление.
void runEval(benchmark::State &state, std::string_view source) {
    Context ctx;
    if (!fill(ctx)) {
        state.SkipWithError("setVariable failed");
        return;
    }

    Ast ast;
    Diagnostic diag;
    // Срез строкового литерала: данные статические, дерево хранит их срезами.
    if (!CS::parseExpression(source.data(),
                             static_cast<std::uint32_t>(source.size()), ast,
                             diag)) {
        state.SkipWithError("parseExpression failed");
        return;
    }

    for (auto _ : state) {
        Value out = Value::null();
        bool ok = CS::evalExpression(ast, ctx, &out, diag);
        if (!ok) {
            state.SkipWithError("evalExpression failed");
            return;
        }
        benchmark::DoNotOptimize(out);
    }
}

/// Самое частое выражение в props — один сегмент от корня.
void BM_Eval_ShortPath(benchmark::State &state) { runEval(state, "user.name"); }
BENCHMARK(BM_Eval_ShortPath);

/// Пять сегментов: цена спуска по дереву объектов.
void BM_Eval_DeepPath(benchmark::State &state) {
    runEval(state, "user.profile.city.code.zip");
}
BENCHMARK(BM_Eval_DeepPath);

/// Числовой индекс массива — без приведения.
void BM_Eval_ArrayIndex(benchmark::State &state) { runEval(state, "items[1]"); }
BENCHMARK(BM_Eval_ArrayIndex);

/// Числовой ключ объекта — с приведением к строке в горячем месте.
void BM_Eval_CoercedKey(benchmark::State &state) { runEval(state, "map[1]"); }
BENCHMARK(BM_Eval_CoercedKey);

/// Построение агрегата: десять элементов, точное выделение.
/// Контекст создан снаружи цикла и не освобождает элементы поштучно, поэтому
/// куча растёт от итерации к итерации, а строка шумнее прочих (см. B24).
void BM_Eval_ArrayLiteral(benchmark::State &state) {
    runEval(state, "[1, 2, 3, 4, 5, 6, 7, 8, 9, 10]");
}
BENCHMARK(BM_Eval_ArrayLiteral);

/// Представление числа отдельно от всего прочего: у него больше всего краёв.
void BM_Eval_FormatNumber(benchmark::State &state) {
    for (auto _ : state) {
        char buffer[CS::kNumberBufferSize];
        std::string_view text = CS::formatNumber(0.1 + 0.2, buffer, sizeof buffer);
        benchmark::DoNotOptimize(text);
    }
}
BENCHMARK(BM_Eval_FormatNumber);

}  // namespace

static void BM_Version(benchmark::State &state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(chupascript_version());
    }
}
BENCHMARK(BM_Version);
