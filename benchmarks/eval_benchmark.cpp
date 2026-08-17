// База производительности вычислителя. В отличие от разбора, который для
// одного выражения случается однажды, вычисление повторяется на каждой
// перерисовке — поэтому меряется именно оно, с уже разобранным деревом.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>
#include <string_view>

#include "chupascript/chupascript.h"
#include "ast.hpp"
#include "check.hpp"
#include "compile.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "eval.hpp"
#include "expression.hpp"
#include "parser.hpp"
#include "script.hpp"
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
        bool ok = CS::evalExpression(ast, source, store, &out, diag);
        if (!ok) {
            state.SkipWithError("evalExpression failed");
            return;
        }
        benchmark::DoNotOptimize(out);
    }
}

/// Пол измерения: узел, который не читает ни хранилище, ни исходник. Всё, что
/// остаётся, — вызов evalExpression и один переход по виду узла. Прочие строки
/// BM_Eval_* читаются как «столько-то сверх этого пола»; без него нельзя
/// сказать, меряет строка работу или накладные расходы обвязки.
void BM_Eval_Constant(benchmark::State &state) { runEval(state, "42"); }
BENCHMARK(BM_Eval_Constant);

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

/// Второй агрегат — объектный литерал. Стоит рядом с массивом не для симметрии:
/// у него есть то, чего у массива нет, — ключи. Каждый ключ это строковый
/// литерал, который надо получить в виде байт и вставить в объект с
/// поддержанием порядка. Ни одна другая строка этот путь не проходит:
/// BM_Eval_ArrayLiteral обходится без ключей вовсе, а BM_Store_ObjectInsert
/// меряет вставку мимо вычислителя, с ключом, взятым готовым.
///
/// Три ключа, а не десять: столько их в реальном литерале из props, и на такой
/// длине видна вставка, а не двоичный поиск.
void BM_Eval_ObjectLiteral(benchmark::State &state) {
    runEval(state, "{'id': 1, 'name': 'Вася', 'active': true}");
}
BENCHMARK(BM_Eval_ObjectLiteral);

/// Единственный путь, кроме format, где вычисление растит текстовый пул, —
/// склейка строк — в языке отсутствует: + арифметический (core/src/operator.cpp,
/// applyArithmetic), строку он не принимает. Мерить здесь нечего, и это не
/// пробел покрытия, а свойство языка: всякая сборка строки идёт через format,
/// который замерен двумя строками ниже.

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
/// Правый операнд — число, а не строковый литерал: так замер сравним с прежними
/// прогонами. Причина, по которой литерал сюда не годился, снята — он больше не
/// выделяет ничего (BM_Eval_StringLiteral ниже), — но менять состав выражения
/// значило бы порвать ряд.
void BM_Eval_NilCoalesceLong(benchmark::State &state) {
    runEval(state, "user.nickname ?? 0");
}
BENCHMARK(BM_Eval_NilCoalesceLong);

/// Строковый литерал. До укладки на компиляции (docs/backlog.md B51) мерить его
/// было нечем: каждая итерация дописывала байты в пул текста, поштучного
/// освобождения нет, и прогон измерял в том числе рост пула с переездами.
/// Теперь вычисление копирует готовое значение из узла и не трогает хранилище
/// вовсе — ожидание близко к BM_Eval_Constant.
void BM_Eval_StringLiteral(benchmark::State &state) {
    runEval(state, "'привет, мир'");
}
BENCHMARK(BM_Eval_StringLiteral);

/// Литерал с экранированием: раскодирование тоже ушло на компиляцию, вместе с
/// черновиком, который заводился на каждое вычисление.
void BM_Eval_EscapedStringLiteral(benchmark::State &state) {
    runEval(state, "'первая\\nвторая\\tтретья'");
}
BENCHMARK(BM_Eval_EscapedStringLiteral);

// ─── строки, стерегущие цену за узел и цену за имя ───

/// Длинная цепочка сложений: сто узлов Binary над сотней узлов Number, никакой
/// работы с хранилищем вовсе.
///
/// Меряет ровно то, чего не меряет ни одна строка выше: цену одного узла на
/// вычислении — переход по виду, рекурсивный спуск, чтение полей узла. Всякое
/// решение, меняющее раскладку Node или порядок веток в eval, отражается здесь
/// стократно усиленным, тогда как на коротком выражении оно тонет в шуме.
/// Разбор той же формы стережёт BM_Parse_Chain, а вычисление до сих пор не
/// стерёг никто.
void BM_Eval_DeepChain(benchmark::State &state) {
    static const std::string source = [] {
        std::string result = "1";
        for (int i = 0; i < 99; ++i) { result += " + 1"; }
        return result;
    }();
    runEval(state, source);
}
BENCHMARK(BM_Eval_DeepChain);

/// Общая часть для чувствительности к числу имён: то же выражение, но в
/// хранилище лежит names глобальных переменных, а нужная — последняя.
void runEvalWithGlobals(benchmark::State &state, int names) {
    Store store;
    Diagnostic diag;
    for (int i = 0; i < names; ++i) {
        if (!CS::setVariable(store, "var" + std::to_string(i), "1", diag)) {
            state.SkipWithError("setVariable failed");
            return;
        }
    }
    if (!CS::setVariable(store, "zzz_user", "{'name': 'Вася'}", diag)) {
        state.SkipWithError("setVariable failed");
        return;
    }

    const std::string_view source = "zzz_user.name";
    Ast ast;
    if (CS::compileExpression(source.data(),
                              static_cast<std::uint32_t>(source.size()), ast,
                              store, &diag, 1) != 0) {
        state.SkipWithError("compileExpression failed");
        return;
    }

    for (auto _ : state) {
        Value out = Value::null();
        if (!CS::evalExpression(ast, source, store, &out, diag)) {
            state.SkipWithError("evalExpression failed");
            return;
        }
        benchmark::DoNotOptimize(out);
    }
}

/// Обращение к имени при трёх, тридцати и трёхстах именах в хранилище.
///
/// Эти три строки обязаны совпадать между собой: имя разрешается в номер ячейки
/// на компиляции (core/src/check.cpp), и вычисление лишь индексирует
/// globalValues_. Расхождение между Arg(3) и Arg(300) означает, что поиск по
/// имени вернулся на путь чтения — а по одной строке BM_Eval_ShortPath, где
/// имён всего три, такой возврат неотличим от шума. BM_Data_GlobalLookup меряет
/// поиск по имени намеренно и потому растёт; здесь рост — дефект.
void BM_Eval_NameByGlobalCount(benchmark::State &state) {
    runEvalWithGlobals(state, static_cast<int>(state.range(0)));
}
BENCHMARK(BM_Eval_NameByGlobalCount)->Arg(3)->Arg(30)->Arg(300);

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
        bool ok = CS::runScript(ast, source, store, diag);
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

/// Общая часть для горячего выполнения: хранилище создаётся один раз, в цикле
/// остаётся только runScript.
///
/// Нужна затем, что у runScriptBench выше в измеренное входит создание и
/// наполнение хранилища, а это по базе около полутора тысяч наносекунд против
/// десятков на само выполнение — то есть просадка выполнения вдвое сдвинула бы
/// ту строку на единицы процентов и прошла бы незамеченной. Здесь её видно
/// целиком.
///
/// Цена такой изоляции — требование к скрипту: он обязан быть идемпотентным,
/// иначе вторая итерация работает уже на других данных. Отсюда и состав ниже:
/// присваивание существующему ключу числа, а не push и не строкового литерала.
void runScriptHot(benchmark::State &state, std::string_view source) {
    Store store;
    if (!fill(store)) {
        state.SkipWithError("setVariable failed");
        return;
    }
    Ast ast;
    Diagnostic diag;
    if (CS::compileScript(source.data(),
                          static_cast<std::uint32_t>(source.size()), ast, store,
                          &diag, 1) != 0) {
        state.SkipWithError("compileScript failed");
        return;
    }

    for (auto _ : state) {
        bool ok = CS::runScript(ast, source, store, diag);
        if (!ok) {
            state.SkipWithError("runScript failed");
            return;
        }
        benchmark::DoNotOptimize(ok);
    }
}

/// Одно присваивание в путь из двух сегментов, без создания хранилища.
void BM_Eval_AssignHot(benchmark::State &state) {
    runScriptHot(state, "user.name = 1;");
}
BENCHMARK(BM_Eval_AssignHot);

/// Пять присваиваний подряд, без создания хранилища: цена обхода Script в
/// чистом виде.
void BM_Eval_ScriptHot(benchmark::State &state) {
    runScriptHot(state,
                 "user.a = 1; user.b = 2; user.c = 3; user.d = 4;"
                 " user.e = 5;");
}
BENCHMARK(BM_Eval_ScriptHot);

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

/// Восемь вызовов в одном выражении. Сама функция выбрана самой дешёвой из
/// имеющихся, чтобы в измеренном осталась не её работа, а то, что вычислитель
/// делает вокруг вызова: получить функцию по узлу и разложить аргументы.
/// На одном вызове (BM_Eval_CallCount) эта часть неотличима от шума, здесь она
/// усилена восьмикратно.
void BM_Eval_CallChain(benchmark::State &state) {
    runEval(state,
            "count(items) + count(items) + count(items) + count(items)"
            " + count(items) + count(items) + count(items) + count(items)");
}
BENCHMARK(BM_Eval_CallChain);

/// format с длинным литеральным пробегом. BM_Eval_Format выше меряет шаблон в
/// десяток байт, где разбор шаблона теряется за прочей работой; здесь на два
/// плейсхолдера приходится под две сотни байт литерала — столько же, сколько в
/// реальной подписи из props, — и цена прохода по шаблону становится видна.
/// Отдельная строка нужна затем, что по докладам B56 шаблон разбирается заново
/// на каждом вычислении, и без неё этот разбор нечем ни подтвердить, ни
/// закрыть.
void BM_Eval_FormatLongTemplate(benchmark::State &state) {
    runEval(state,
            "format('Заказ собран и передан в доставку, ожидайте курьера в"
            " течение рабочего дня по указанному адресу; номер отправления"
            " ${}, к оплате при получении ${} рублей', 1, 2)");
}
BENCHMARK(BM_Eval_FormatLongTemplate);

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
        std::uint32_t errors = CS::check(ast, source, store, found, 1);
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

// ════════════════════════════════════════════════════════════════════════
// Цена рефакторинга (docs/superpowers/specs/2026-08-15-chupascript-core-
// entities-design.md, Р2 и Р6): решения из этого рефакторинга добавили
// стоимость ровно в двух местах, и обе формы — «до» и «после» — по-прежнему
// выразимы в одном прогоне, потому что старый низкоуровневый путь
// (compileExpression в голый Ast, string_view прямо из пула store) никуда не
// делся — рефакторинг положил новый путь рядом, не убрав старый.
// ════════════════════════════════════════════════════════════════════════

// ─── Р2: копия исходника при компиляции ───
//
// CS::Expression::compile (core/src/expression.cpp) копирует переданный
// string_view в собственное поле source_ и компилирует уже из копии. До
// рефакторинга компиляция шла CS::compileExpression прямо в голый CS::Ast,
// без выделения под исходник — дерево смотрело в чужой буфер.
//
// Обе стороны должны быть в одинаковых условиях, иначе сравнение врёт — и
// оно действительно соврало в первой версии этих строк. Ast::reset()
// (core/src/ast.cpp) делает nodes_.clear() / children_.clear(): ёмкость
// векторов остаётся, реального выделения со второй итерации нет. Если
// единицу-приёмник (Ast для старого пути, Expression для нового) объявить
// снаружи цикла — обе стороны переиспользуют ёмкость одинаково, и разница
// между ними — это разница именно в работе compile(), а не в том, что одна
// сторона холодная, а другая тёплая. Из-за этой асимметрии первая версия
// этого бенчмарка (единица нового пути создавалась внутри цикла, старого —
// снаружи) давала завышенную и неверную цифру.
//
// Но даже после починки New/Old не измеряет ровно «цену копии». Внутри
// Expression::compile строится в локальную built (пустые векторы на каждый
// вызов) и только при успехе делается *out = std::move(built) — так работает
// контракт «при отказе *out не портится» (core/src/expression.cpp,
// core/src/expression.hpp). У Ast, который переиспускается на месте,
// такой пересборки нет вовсе. Значит New/Old меряет две слитые вместе вещи:
// саму копию исходника и рост векторов built с нуля на каждый вызов. Чтобы
// разложить их, ниже есть третья группа строк — BM_Copy_SourceBytes,
// изолированная цена одной только копии string_view в std::string того же
// размера. New − Old даёт полную цену перехода; New − Old − Copy — то, что
// остаётся на сборку во временную единицу.
//
// Хранилище используется только для проверки состава имён (check.hpp),
// значения роли не играют — строится один раз до цикла и не растёт: сама
// компиляция ничего в store не пишет.

/// Компиляция выражения, новый путь (с копией исходника). Приёмник создаётся
/// внутри цикла: пропс компилируется однажды при парсинге и дальше только
/// вычисляется, так что каждая компиляция в жизни продукта — в свежий
/// объект. Переиспользование приёмника между итерациями измеряло бы сценарий,
/// которого нет.
void BM_Compile_Expr_New(benchmark::State &state, std::string_view source) {
    Store store;
    if (!fill(store)) {
        state.SkipWithError("setVariable failed");
        return;
    }
    Diagnostic diag;
    for (auto _ : state) {
        CS::Expression expr;
        std::uint32_t errors = CS::Expression::compile(source, store, &expr,
                                                        &diag, 1);
        if (errors != 0) {
            state.SkipWithError("Expression::compile failed");
            return;
        }
        benchmark::DoNotOptimize(expr);
    }
}

/// Компиляция выражения, старый путь (без копии, дерево смотрит в буфер
/// вызывающего). Ast — внутри цикла, симметрично New.
void BM_Compile_Expr_Old(benchmark::State &state, std::string_view source) {
    Store store;
    if (!fill(store)) {
        state.SkipWithError("setVariable failed");
        return;
    }
    Diagnostic diag;
    for (auto _ : state) {
        Ast ast;
        std::uint32_t errors = CS::compileExpression(
            source.data(), static_cast<std::uint32_t>(source.size()), ast,
            store, &diag, 1);
        if (errors != 0) {
            state.SkipWithError("compileExpression failed");
            return;
        }
        benchmark::DoNotOptimize(ast);
    }
}

/// Изолированная цена копии: не компиляция вовсе, а ровно то самое
/// присваивание built.source_ = std::string(source), что стоит в
/// Expression::compile — вынесенное отдельно, чтобы иметь порядок величины
/// без примеси сборки дерева. Свежий std::string на каждой итерации, как и в
/// built: переиспользования ёмкости здесь нет ни у New, ни у Old, поэтому
/// сравнение с ними по этой оси честное.
void BM_Copy_SourceBytes(benchmark::State &state, std::string_view source) {
    for (auto _ : state) {
        std::string copy(source);
        benchmark::DoNotOptimize(copy);
    }
}

// Самое частое выражение в props — один сегмент от глобальной переменной.
BENCHMARK_CAPTURE(BM_Compile_Expr_New, ShortPath, "user.name");
BENCHMARK_CAPTURE(BM_Compile_Expr_Old, ShortPath, "user.name");
BENCHMARK_CAPTURE(BM_Copy_SourceBytes, ShortPath, "user.name");

// Реалистичный источник из props: вызов и тернарник вместе.
BENCHMARK_CAPTURE(BM_Compile_Expr_New, CallTernary,
                  "count(items) > 0 ? items[0] : user.name");
BENCHMARK_CAPTURE(BM_Compile_Expr_Old, CallTernary,
                  "count(items) > 0 ? items[0] : user.name");
BENCHMARK_CAPTURE(BM_Copy_SourceBytes, CallTernary,
                  "count(items) > 0 ? items[0] : user.name");

/// Компиляция скрипта, новый путь (с копией исходника). Script — внутри
/// цикла, по той же причине, что и у выражения: свежий приёмник на каждую
/// компиляцию.
void BM_Compile_Script_New(benchmark::State &state, std::string_view source) {
    Store store;
    if (!fill(store)) {
        state.SkipWithError("setVariable failed");
        return;
    }
    Diagnostic diag;
    for (auto _ : state) {
        CS::Script script;
        std::uint32_t errors = CS::Script::compile(source, store, &script,
                                                    &diag, 1);
        if (errors != 0) {
            state.SkipWithError("Script::compile failed");
            return;
        }
        benchmark::DoNotOptimize(script);
    }
}

/// Компиляция скрипта, старый путь (без копии). Ast — внутри цикла,
/// симметрично New.
void BM_Compile_Script_Old(benchmark::State &state, std::string_view source) {
    Store store;
    if (!fill(store)) {
        state.SkipWithError("setVariable failed");
        return;
    }
    Diagnostic diag;
    for (auto _ : state) {
        Ast ast;
        std::uint32_t errors = CS::compileScript(
            source.data(), static_cast<std::uint32_t>(source.size()), ast,
            store, &diag, 1);
        if (errors != 0) {
            state.SkipWithError("compileScript failed");
            return;
        }
        benchmark::DoNotOptimize(ast);
    }
}

// Обработчик из трёх операторов — вызов, присваивание, форматирование.
BENCHMARK_CAPTURE(BM_Compile_Script_New, Handler,
                  "push(items, 1);"
                  "user.badge = count(items);"
                  "user.label = format('${} шт.', count(items));");
BENCHMARK_CAPTURE(BM_Compile_Script_Old, Handler,
                  "push(items, 1);"
                  "user.badge = count(items);"
                  "user.label = format('${} шт.', count(items));");
BENCHMARK_CAPTURE(BM_Copy_SourceBytes, Handler,
                  "push(items, 1);"
                  "user.badge = count(items);"
                  "user.label = format('${} шт.', count(items));");

// ─── Р6: цена границы C на строковом результате ───
//
// Пара мерит одно и то же вычисление двумя путями: через C API и напрямую
// через ядро (CS::Expression::eval плюс Store::string). Раньше между ними
// стояло владение: chupa_eval_string_borrowed заводил ChupaString — обёртку над
// std::string, — копировал туда результат, и хост обязан был позвать
// chupa_string_destroy. Пара malloc/free плюс копия байт на каждое чтение.
//
// Владения больше нет: C API отдаёт срез в текстовый пул store, тот же самый
// string_view, что берёт нижняя половина пары. Поэтому отношение сторон
// должно стоять около единицы, и разъезд — признак того, что на пути снова
// завелось выделение. Ради этого пара и оставлена.
//
// Значение читается из поля, установленного один раз до цикла
// (setVariable / chupa_context_set_string) — сама компиляция и вычисление
// не пишут в текстовый пул store (никаких строковых литералов и никакого
// makeString в горячем пути), поэтому пул не растёт от итерации к итерации,
// в отличие от BM_Eval_ArrayLiteral и BM_Eval_NilCoalesceLong (см. B24).
//
// Две длины строки остались от прежнего замера, где короткая (<=22 байта)
// укладывалась в SSO std::string, а длинная требовала настоящего malloc под
// байты. Теперь не копируется ни та, ни другая, и разница между ними должна
// пропасть — это второй способ увидеть то же самое.

// 9 байт — заведомо короче порога SSO (22 байта у libc++ std::string).
constexpr std::string_view kShortStringValue = "avatar_ok";
// 54 байта — заведомо длиннее порога SSO, реалистичный URL из props.
constexpr std::string_view kLongStringValue =
    "https://cdn.example.com/avatars/abcdef1234567890.png";

/// Вычисление со строковым результатом через C API.
void BM_Eval_String_New(benchmark::State &state, std::string_view value) {
    ChupaContext *ctx = chupa_context_create();
    if (ctx == nullptr) {
        state.SkipWithError("chupa_context_create failed");
        return;
    }
    chupa_context_set_string(ctx, "s", 1, value.data(), value.size());
    ChupaExpression *expr = chupa_compile_expression(ctx, "s", 1);
    if (expr == nullptr) {
        state.SkipWithError("chupa_compile_expression failed");
        chupa_context_destroy(ctx);
        return;
    }

    for (auto _ : state) {
        const char *bytes = nullptr;
        size_t len = 0;
        const ChupaStatus status =
            chupa_eval_string_borrowed(ctx, expr, &bytes, &len);
        if (status != CHUPA_OK) {
            state.SkipWithError("chupa_eval_string_borrowed failed");
            break;
        }
        benchmark::DoNotOptimize(bytes);
        benchmark::DoNotOptimize(len);
    }

    chupa_expression_destroy(expr);
    chupa_context_destroy(ctx);
}

/// То же вычисление напрямую через ядро, мимо границы C.
void BM_Eval_String_Old(benchmark::State &state, std::string_view value) {
    Store store;
    Diagnostic diag;
    // setVariable понимает синтаксис языка, а не JSON: строка в одинарных
    // кавычках — обычный строковый литерал.
    const std::string literal = "'" + std::string(value) + "'";
    if (!CS::setVariable(store, "s", literal, diag)) {
        state.SkipWithError("setVariable failed");
        return;
    }

    CS::Expression expr;
    if (CS::Expression::compile("s", store, &expr, &diag, 1) != 0) {
        state.SkipWithError("Expression::compile failed");
        return;
    }

    for (auto _ : state) {
        Value out = Value::null();
        if (!expr.eval(store, &out, diag)) {
            state.SkipWithError("eval failed");
            return;
        }
        std::string_view text = store.string(out);
        benchmark::DoNotOptimize(text);
    }
}

BENCHMARK_CAPTURE(BM_Eval_String_New, Short, kShortStringValue);
BENCHMARK_CAPTURE(BM_Eval_String_Old, Short, kShortStringValue);
BENCHMARK_CAPTURE(BM_Eval_String_New, Long, kLongStringValue);
BENCHMARK_CAPTURE(BM_Eval_String_Old, Long, kLongStringValue);

}  // namespace

static void BM_Version(benchmark::State &state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(chupa_version());
    }
}
BENCHMARK(BM_Version);
