// База производительности вычислителя. В отличие от разбора, который для
// одного выражения случается однажды, вычисление повторяется на каждой
// перерисовке — поэтому меряется именно оно, с уже разобранным деревом.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string_view>

#include "chupascript/chupascript.h"
#include "ast.hpp"
#include "check.hpp"
#include "compile.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "eval.hpp"
#include "parser.hpp"
#include "store.hpp"
#include "text.hpp"

namespace {

using CS::Ast;
using CS::Store;
using CS::Diagnostic;
using CS::Value;

/// Наполняет хранилище данными, на которых меряются пути.
bool fill(Store &store) {
    Diagnostic diag;
    return CS::setVariable(store, "user",
                           "{'name': 'Вася', 'profile': {'city': {'code': "
                           "{'zip': 101000}}}}",
                           diag) &&
           CS::setVariable(store, "items", "[10, 20, 30]", diag) &&
           CS::setVariable(store, "map", "{'0': 'zero', '1': 'one'}", diag);
}

/// Общая часть: наполнить хранилище, разобрать выражение, мерить вычисление.
void runEval(benchmark::State &state, std::string_view source) {
    Store store;
    if (!fill(store)) {
        state.SkipWithError("setVariable failed");
        return;
    }

    Ast ast;
    Diagnostic diag;
    // Срез строкового литерала: данные статические, дерево хранит их срезами.
    // compileExpression вместо parseExpression: evalExpression утверждением
    // требует отметку прохода, а разбор без проверки её не ставит.
    if (CS::compileExpression(source.data(),
                              static_cast<std::uint32_t>(source.size()), ast,
                              store, &diag, 1) != 0) {
        state.SkipWithError("compileExpression failed");
        return;
    }

    for (auto _ : state) {
        Value out = Value::null();
        bool ok = CS::evalExpression(ast, store, &out, diag);
        if (!ok) {
            state.SkipWithError("evalExpression failed");
            return;
        }
        benchmark::DoNotOptimize(out);
    }
}

/// Самое частое выражение в props — один сегмент от глобальной переменной.
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
/// Хранилище создано снаружи цикла и не освобождает элементы поштучно, поэтому
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

/// Арифметика поверх глубокого пути. В измеренной стоимости доминирует не
/// арифметика, а сам путь `user.profile.city.code.zip`: 82.1 нс здесь против
/// 56.1 нс у `BM_Eval_DeepPath`, который меряет только его, — около 68%
/// времени строки уходит на поиск по пути. Читать эту строку имеет смысл в
/// сравнении с `BM_Eval_DeepPath`, а не саму по себе.
void BM_Eval_Arithmetic(benchmark::State &state) {
    runEval(state, "user.profile.city.code.zip * 2 + 1 - 3");
}
BENCHMARK(BM_Eval_Arithmetic);

/// Цепочка сравнений, соединённая && — типичная защита в props.
void BM_Eval_LogicalChain(benchmark::State &state) {
    runEval(state, "1 < 2 && 2 < 3 && 3 < 4");
}
BENCHMARK(BM_Eval_LogicalChain);

/// ?? по короткому пути: слева не null, правый операнд не вычисляется.
void BM_Eval_NilCoalesceShort(benchmark::State &state) {
    runEval(state, "user.name ?? 'Гость'");
}
BENCHMARK(BM_Eval_NilCoalesceShort);

/// ?? по длинному пути: слева null, правый вычисляется. Разница с коротким —
/// то, что видно на экране: ?? самый частый оператор в props.
///
/// Правый операнд — число, а не строковый литерал, именно чтобы разница мерила
/// заявленное. Строковый литерал зовёт store.makeString и дописывает в пул текста
/// хранилища, а поштучного освобождения нет: пул рос бы на каждой итерации весь
/// прогон, с переездами внутри измеряемого цикла (та же беда, что у
/// BM_Eval_ArrayLiteral, см. B24). Короткий путь не выделяет ничего, и разница
/// оказалась бы ценой вычисления правого операнда плюс неограниченным
/// выделением, которого короткому пути платить не приходится.
void BM_Eval_NilCoalesceLong(benchmark::State &state) {
    runEval(state, "user.nickname ?? 0");
}
BENCHMARK(BM_Eval_NilCoalesceLong);

/// Общая часть для скриптов: наполнить хранилище, разобрать, мерить выполнение.
///
/// Хранилище создаётся заново на каждой итерации: скрипт меняет данные, и без
/// пересоздания вторая итерация работала бы уже на изменённых. Цена создания
/// входит в измерение — читать эти строки имеет смысл в сравнении друг с
/// другом, а не с BM_Eval_* для выражений.
void runScriptBench(benchmark::State &state, std::string_view source) {
    Ast ast;
    Diagnostic diag;
    // Хранилище для проверки имён нужен до цикла: runScript требует отметку
    // прохода, а проходу довольно состава имён — значения роли не играют.
    Store checkStore;
    if (!fill(checkStore)) {
        state.SkipWithError("setVariable failed");
        return;
    }
    if (CS::compileScript(source.data(),
                          static_cast<std::uint32_t>(source.size()), ast,
                          checkStore, &diag, 1) != 0) {
        state.SkipWithError("compileScript failed");
        return;
    }

    for (auto _ : state) {
        Store store;
        if (!fill(store)) {
            state.SkipWithError("setVariable failed");
            return;
        }
        bool ok = CS::runScript(ast, store, diag);
        if (!ok) {
            state.SkipWithError("runScript failed");
            return;
        }
        benchmark::DoNotOptimize(ok);
    }
}

/// Присваивание в путь из двух сегментов — самая частая форма в обработчике.
void BM_Eval_Assign(benchmark::State &state) {
    runScriptBench(state, "user.name = 'Петя';");
}
BENCHMARK(BM_Eval_Assign);

/// Составное присваивание туда же: чтение, операция, запись.
void BM_Eval_CompoundAssign(benchmark::State &state) {
    runScriptBench(state, "user.profile.city.code.zip += 1;");
}
BENCHMARK(BM_Eval_CompoundAssign);

/// Скрипт из пяти присваиваний — цена обхода Script.
void BM_Eval_Script(benchmark::State &state) {
    runScriptBench(state,
                   "user.a = 1; user.b = 2; user.c = 3; user.d = 4;"
                   " user.e = 5;");
}
BENCHMARK(BM_Eval_Script);

/// Дешёвый билтин: один аргумент, ничего не выделяет.
void BM_Eval_CallCount(benchmark::State &state) { runEval(state, "count(items)"); }
BENCHMARK(BM_Eval_CallCount);

/// Выделяющий билтин: создаёт массив на каждый вызов.
void BM_Eval_CallKeys(benchmark::State &state) { runEval(state, "keys(map)"); }
BENCHMARK(BM_Eval_CallKeys);

/// Сборка строки: единственный билтин, растящий текстовый пул.
void BM_Eval_Format(benchmark::State &state) {
    runEval(state, "format('${} из ${}', 1, 2)");
}
BENCHMARK(BM_Eval_Format);

/// Вызов внутри выражения, какие и бывают в props.
void BM_Eval_CallInProps(benchmark::State &state) {
    runEval(state, "count(items) > 0 ? items[0] : 0");
}
BENCHMARK(BM_Eval_CallInProps);

/// Общая часть для прохода: разобрать один раз, мерить только проверки.
void runCheck(benchmark::State &state, std::string_view source, bool script) {
    Store store;
    if (!fill(store)) {
        state.SkipWithError("setVariable failed");
        return;
    }
    Ast ast;
    Diagnostic diag;
    const bool parsed =
        script ? CS::parseScript(source.data(),
                                   static_cast<std::uint32_t>(source.size()),
                                   ast, diag)
                : CS::parseExpression(source.data(),
                                      static_cast<std::uint32_t>(source.size()),
                                      ast, diag);
    if (!parsed) {
        state.SkipWithError("parse failed");
        return;
    }
    for (auto _ : state) {
        Diagnostic found[1];
        std::uint32_t errors = CS::check(ast, store, found, 1);
        benchmark::DoNotOptimize(errors);
    }
}

/// Проход по дереву props. Сравнивать эту строку надо с BM_Parse_Props,
/// делённым на сто: решение делать проход обязательным стоит на том, что он
/// заметно дешевле разбора.
void BM_Check_Props(benchmark::State &state) {
    runCheck(state,
             "user.profile.city.code.zip > 0"
             " ? format('${}', user.name)"
             " : 'нет'",
             false);
}
BENCHMARK(BM_Check_Props);

/// Проход по дереву обработчика.
void BM_Check_Handler(benchmark::State &state) {
    runCheck(state,
             "push(items, 1);"
             "user.badge = count(items);"
             "user.label = format('${} шт.', count(items));",
             true);
}
BENCHMARK(BM_Check_Handler);

}  // namespace

static void BM_Version(benchmark::State &state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(chupa_version());
    }
}
BENCHMARK(BM_Version);
