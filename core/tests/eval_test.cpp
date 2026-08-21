#include "eval.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <deque>
#include <set>
#include <string>
#include <vector>

#include "ast.hpp"
#include "box.hpp"
#include "compile.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "parser.hpp"
#include "store.hpp"
#include "aggregate.hpp"

namespace {

using CS::Ast;
using CS::Store;
using CS::Diagnostic;
using CS::Value;

/// Разбирает и вычисляет; требует успеха обоих шагов.
Value evaluate(CS::Execution &exec, std::string_view text) {
    /// Every tree this helper compiles stays alive for the whole test binary.
    ///
    /// The Value this helper returns may point at a string-literal box owned
    /// by the tree that produced it, and the box is released when that tree
    /// is destroyed or reset. A single shared tree would therefore release
    /// one test's literals as soon as the next test compiled into it. A
    /// deque rather than a vector: growing a vector moves its elements, and
    /// an Ast being evaluated must not move under the call.
    static thread_local std::deque<Ast> trees;
    Ast &ast = trees.emplace_back();
    Diagnostic diag;
    const std::uint32_t errors =
        CS::compileExpression(text.data(),
                              static_cast<std::uint32_t>(text.size()), ast,
                              exec.store(), &diag, 1);
    if (errors != 0) {
        // Дерево не прошло check и не помечено проверенным: звать
        // evalExpression на нём — либо assert в отладочной сборке, либо
        // хождение по непроверенному дереву прямо в буфер аргументов в
        // релизной (core/src/eval.cpp, kMaxFixedArgs). Ранний выход не даёт
        // страховке из builtin.cpp отключить саму себя первой.
        ADD_FAILURE() << diag.message;
        return Value::null();
    }
    Value out = Value::null();
    EXPECT_TRUE(CS::evalExpression(ast, text, exec, &out, diag))
        << diag.message;
    return out;
}

/// Разбирает успешно, вычисляет с отказом; возвращает диагностику вычисления.
Diagnostic evalError(CS::Execution &exec, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors =
        CS::compileExpression(text.data(),
                              static_cast<std::uint32_t>(text.size()), ast,
                              exec.store(), &diag, 1);
    if (errors != 0) {
        ADD_FAILURE() << diag.message;
        return diag;
    }
    Value out = Value::null();
    EXPECT_FALSE(CS::evalExpression(ast, text, exec, &out, diag));
    return diag;
}

/// Вычисляет и читает результат как строку — самая частая пара в этом файле.
///
/// Returns an owned std::string, not a string_view: a short string's bytes
/// live inside the Value itself, and the Value evaluate() returns is a local
/// that dies at the end of this function, so a view into it would dangle at
/// the caller. Copying out before that local dies keeps the comparison safe.
std::string evalText(CS::Execution &exec, std::string_view text) {
    const Value v = evaluate(exec, text);
    return std::string(CS::stringBytes(v));
}

/// Кладёт глобальную переменную; требует успеха.
void put(Store &store, std::string_view name, std::string_view text) {
    CS::Deferred dead;
    Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(store, dead, name, text, diag)) << diag.message;
}

TEST(EvalLiterals, NumberIsEvaluated) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evaluate(exec, "3").numberValue(), 3.0);
    EXPECT_EQ(evaluate(exec, "0.5").numberValue(), 0.5);
}

TEST(EvalLiterals, BooleanIsEvaluated) {
    Store store;
    CS::Execution exec(store);
    EXPECT_TRUE(evaluate(exec, "true").booleanValue());
    EXPECT_FALSE(evaluate(exec, "false").booleanValue());
}

TEST(EvalLiterals, NullIsEvaluated) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evaluate(exec, "null").kind(), Value::Kind::Null);
}

TEST(EvalLiterals, StringIsEvaluated) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evalText(exec, "'Вася'"), "Вася");
    EXPECT_EQ(evalText(exec, "\"Вася\""), "Вася");
}

TEST(EvalLiterals, StringEscapesAreDecoded) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evalText(exec, "'a\\nb'"), "a\nb");
}

TEST(EvalNames, GlobalIsRead) {
    Store store;
    CS::Execution exec(store);
    put(store, "count", "3");
    EXPECT_EQ(evaluate(exec, "count").numberValue(), 3.0);
}

TEST(EvalNames, GlobalHoldingAggregateIsReadByIdentity) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1, 2]");
    EXPECT_TRUE(evaluate(exec, "items").sameAggregate(store.global("items")));
}

TEST(EvalNames, GlobalHoldingNullIsRead) {
    Store store;
    CS::Execution exec(store);
    put(store, "maybe", "null");
    // Корень со значением null существует и читается как null — это не то же
    // самое, что отсутствующая глобальная переменная.
    EXPECT_EQ(evaluate(exec, "maybe").kind(), Value::Kind::Null);
}

TEST(EvalMember, ExistingKeyIsRead) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася', 'age': 30}");
    EXPECT_EQ(evalText(exec, "user.name"), "Вася");
    EXPECT_EQ(evaluate(exec, "user.age").numberValue(), 30.0);
}

TEST(EvalMember, MissingKeyReadsAsNull) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася'}");
    // docs/semantics.md §6.2: отсутствующий ключ читается как null.
    EXPECT_EQ(evaluate(exec, "user.nickname").kind(), Value::Kind::Null);
}

TEST(EvalMember, ReadingThroughNullGivesNull) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася'}");
    // docs/semantics.md §6.3: путь любой глубины безопасен, а опечатка глубже
    // первого сегмента не диагностируется — это цена правила.
    EXPECT_EQ(evaluate(exec, "user.prfoile.avatar").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(exec, "user.a.b.c.d.e").kind(), Value::Kind::Null);
}

TEST(EvalMember, ReadingKeyOffANonObjectIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "count", "3");
    put(store, "name", "'Вася'");
    put(store, "flag", "true");
    put(store, "items", "[1, 2]");
    // docs/semantics.md §6.4: доступ по ключу определён для Object, чтение у
    // null — правилом §6.3, прочее — ошибка.
    EXPECT_EQ(evalError(exec, "count.x").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "name.x").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "flag.x").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "items.x").code, CS::ErrorCode::Type);
}

TEST(EvalMember, KeyIsTakenLiterallyNotAsAName) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'name': 'ключ'}");
    put(store, "name", "'корень'");
    // docs/semantics.md §6.2: в форме obj.k ключом является имя k буквально, а
    // не значение глобальной переменной, которая случайно называется так же.
    EXPECT_EQ(evalText(exec, "o.name"), "ключ");
}

TEST(EvalMember, OffsetPointsAtTheFailingNode) {
    Store store;
    CS::Execution exec(store);
    put(store, "count", "3");
    // Место ошибки — там, где чинить, а не в начале выражения.
    EXPECT_GT(evalError(exec, "count.a.b").offset, 0u);
}

TEST(EvalIndex, ArrayElementIsRead) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[10, 20, 30]");
    EXPECT_EQ(evaluate(exec, "items[0]").numberValue(), 10.0);
    EXPECT_EQ(evaluate(exec, "items[2]").numberValue(), 30.0);
}

TEST(EvalIndex, ArrayReadBeyondEndGivesNull) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[10]");
    // docs/semantics.md §6.1: чтение за границей штатно — данные неполны.
    EXPECT_EQ(evaluate(exec, "items[1]").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(exec, "items[1000000]").kind(), Value::Kind::Null);
}

TEST(EvalIndex, FractionalAndNegativeIndicesAreErrors) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[10, 20]");
    put(store, "minusOne", "-1");
    put(store, "huge", std::string(400, '9'));
    // Дробный и отрицательный индекс означают намерение, которого в языке нет:
    // приведения к целому тоже нет. Отрицательное значение и бесконечность
    // берутся из данных — унарный минус это оператор, а операторов в части 1
    // нет; четыреста девяток переполняют double и дают inf.
    EXPECT_EQ(evalError(exec, "items[0.5]").code, CS::ErrorCode::Range);
    EXPECT_EQ(evalError(exec, "items[minusOne]").code, CS::ErrorCode::Range);
    EXPECT_EQ(evalError(exec, "items[huge]").code, CS::ErrorCode::Range);
}

TEST(EvalIndex, NonNumberArrayIndexIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[10, 20]");
    // docs/semantics.md §6.1: приведения к Number нет, поэтому items['0']
    // не работает.
    EXPECT_EQ(evalError(exec, "items['0']").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "items[true]").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "items[null]").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, ObjectKeyIsRead) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'name': 'Вася'}");
    EXPECT_EQ(evalText(exec, "o['name']"), "Вася");
    EXPECT_EQ(evaluate(exec, "o['missing']").kind(), Value::Kind::Null);
}

TEST(EvalIndex, ScalarKeysAreCoercedToString) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'0': 'zero', 'true': 'yes', 'null': 'nothing', '1.5': 'half'}");
    // docs/semantics.md §4.1: ключ объекта — одна из трёх позиций, требующих
    // String, и приведение туда одностороннее.
    EXPECT_EQ(evalText(exec, "o[0]"), "zero");
    EXPECT_EQ(evalText(exec, "o[true]"), "yes");
    EXPECT_EQ(evalText(exec, "o[null]"), "nothing");
    EXPECT_EQ(evalText(exec, "o[1.5]"), "half");
}

TEST(EvalIndex, NegativeZeroAndZeroAreDifferentKeys) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'0': 'plus', '-0': 'minus'}");
    put(store, "minusZero", "-0");
    // docs/semantics.md §4.3: -0 == 0 истинно, но ключи разные, потому что
    // представление числа сохраняет знак нуля. Отрицательный ноль приходит из
    // данных по той же причине, что и в тесте выше.
    EXPECT_EQ(evalText(exec, "o[0]"), "plus");
    EXPECT_EQ(evalText(exec, "o[minusZero]"), "minus");
}

TEST(EvalIndex, AggregateKeyIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'a': 1}");
    put(store, "items", "[1]");
    // Агрегат не приводится никуда (docs/semantics.md §4).
    EXPECT_EQ(evalError(exec, "o[items]").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, ReadingThroughNullGivesNull) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася'}");
    EXPECT_EQ(evaluate(exec, "user.missing[0]").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(exec, "user.missing['k']").kind(), Value::Kind::Null);
}

TEST(EvalIndex, SubscriptIsEvaluatedEvenWhenTheBaseIsNull) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася'}");
    // docs/semantics.md §3.3: порядок зафиксирован, короткое замыкание есть
    // только у логических, ?? и тернарного. Ошибка в индексе обязана всплыть,
    // а не быть съеденной null-базой. Побочных эффектов в выражениях нет, так
    // что наблюдать порядок можно только через ошибку.
    //
    // Ошибка вынесена через 1 + 'a' (Type), а не через неизвестное имя: с
    // приходом статического прохода (core/src/check.hpp) неизвестное имя
    // отсеивается ещё до вычисления, независимо от того, какая ветка дерева
    // его на самом деле достигает, и уже не годится в качестве пробы «дошли
    // ли мы сюда вычислением».
    EXPECT_EQ(evalError(exec, "user.missing[1 + 'a']").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, BaseIsEvaluatedBeforeTheSubscript) {
    Store store;
    CS::Execution exec(store);
    // docs/semantics.md §3.3 фиксирует порядок именно ради определённости
    // диагностики, когда ошибочны оба операнда. Обе ошибки — Type через
    // 1 + 'a' (по причине из SubscriptIsEvaluatedEvenWhenTheBaseIsNull выше),
    // и левая обязана выиграть.
    const Diagnostic diag = evalError(exec, "(1 + 'a')[2 + 'b']");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_LT(diag.offset, 10u);
}

TEST(EvalIndex, IndexingANonAggregateIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "count", "3");
    put(store, "name", "'Вася'");
    put(store, "flag", "true");
    // docs/semantics.md §6.4: 'abc'[0] — ошибка, строка не индексируется.
    EXPECT_EQ(evalError(exec, "count[0]").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "name[0]").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "flag[0]").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, ChainedAccessWorks) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'items': [{'id': 1}, {'id': 2}]}");
    EXPECT_EQ(evaluate(exec, "state.items[1].id").numberValue(), 2.0);
}

TEST(EvalAggregates, ArrayLiteralKeepsOrder) {
    Store store;
    CS::Execution exec(store);
    const Value a = evaluate(exec, "[1, 2, 3]");
    ASSERT_EQ(CS::arrayCount(a), 3u);
    EXPECT_EQ(CS::arrayAt(a, 0).numberValue(), 1.0);
    EXPECT_EQ(CS::arrayAt(a, 2).numberValue(), 3.0);
}

TEST(EvalAggregates, ObjectLiteralStoresPairs) {
    Store store;
    CS::Execution exec(store);
    const Value o = evaluate(exec, "{'a': 1, 'b': 2}");
    ASSERT_EQ(CS::objectCount(o), 2u);
    EXPECT_EQ(CS::objectGet(o, "a").numberValue(), 1.0);
    EXPECT_EQ(CS::objectGet(o, "b").numberValue(), 2.0);
}

TEST(EvalAggregates, EmptyLiterals) {
    Store store;
    CS::Execution exec(store);
    const Value a = evaluate(exec, "[]");
    const Value o = evaluate(exec, "{}");
    EXPECT_EQ(CS::arrayCount(a), 0u);
    EXPECT_EQ(CS::objectCount(o), 0u);
}

TEST(EvalAggregates, ElementsAreArbitraryExpressions) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася'}");
    put(store, "items", "[7]");
    // Вот чем агрегат в выражении отличается от агрегата в данных: элемент —
    // выражение, а не литерал.
    const Value a = evaluate(exec, "[user.name, items[0], user.missing]");
    // Массив временный, а первый его элемент пришёл из данных хоста коробкой
    // и копией не стал.
    ASSERT_EQ(CS::arrayCount(a), 3u);
    const Value first = CS::arrayAt(a, 0);
    EXPECT_EQ(CS::stringBytes(first), "Вася");
    EXPECT_EQ(CS::arrayAt(a, 1).numberValue(), 7.0);
    EXPECT_EQ(CS::arrayAt(a, 2).kind(), Value::Kind::Null);
}

TEST(EvalAggregates, ObjectValuesAreExpressionsAndKeysAreLiterals) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася'}");
    const Value o = evaluate(exec, "{'who': user.name}");
    const Value who = CS::objectGet(o, "who");
    EXPECT_EQ(CS::stringBytes(who), "Вася");
}

TEST(EvalAggregates, ErrorInsideAnElementStopsAtTheFirstFailure) {
    Store store;
    CS::Execution exec(store);
    // Два сбойных элемента: диагностика обязана указать на первый, иначе
    // «первая ошибка выигрывает» держится на честном слове. В частях 2 и 3 это
    // правило станет несущим для && и ??.
    //
    // Обе ошибки — Type через 1 + 'a', не неизвестное имя: статический проход
    // (core/src/check.hpp) отсеял бы оба элемента разом, ещё до вычисления, и
    // «первый выигрывает» стало бы непроверяемым.
    const Diagnostic diag = evalError(exec, "[1 + 'a', 2 + 'b']");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_LT(diag.offset, 10u);
}

TEST(EvalDepth, ChainAtTheParserLimitEvaluatesWithoutOverflow) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася'}");
    // Вычислитель спускается по дереву рекурсивно и тратит кадр на звено.
    // Собственного предела у него нет — его даёт парсер, но только потому, что
    // звено цепочки стоит той же единицы, что и вложенность. До этой правки
    // цепочка длины не имела, разбиралась целиком и роняла процесс.
    //
    // 509 — измеренный максимум для цепочки '.' в выражении
    // (docs/grammar.md Приложение C.1). Чтение идёт через null по §6.3, то есть
    // все звенья действительно проходятся.
    std::string source = "user";
    for (int i = 0; i < 509; ++i) {
        source += ".b";
    }
    EXPECT_EQ(evaluate(exec, source).kind(), Value::Kind::Null);

    // На единицу длиннее до вычислителя уже не доходит: отказ на разборе.
    Ast ast;
    Diagnostic diag;
    const std::string tooLong = source + ".b";
    EXPECT_FALSE(CS::parseExpression(
        tooLong.data(), static_cast<std::uint32_t>(tooLong.size()), ast, diag));
    EXPECT_STREQ(diag.message, "expression nesting too deep");
}

TEST(EvalDepth, OperatorChainAtTheParserLimitEvaluatesWithoutOverflow) {
    Store store;
    CS::Execution exec(store);
    // Тот самый тест, ради которого левоассоциативные правила начали тратить
    // бюджет. Цепочка '1 + 1 + …' разбирается циклом, но даёт левоглубокое
    // дерево Binary, по которому вычислитель спускается рекурсивно. Пока
    // бюджета она не тратила, «1» плюс 50 000 раз «+ 1» разбиралось успешно и
    // роняло процесс по SIGSEGV.
    //
    // 509 — измеренный максимум для цепочки одного левоассоциативного уровня
    // в выражении (docs/grammar.md Приложение C.1).
    std::string source = "1";
    for (int i = 0; i < 509; ++i) {
        source += " + 1";
    }
    EXPECT_EQ(evaluate(exec, source).numberValue(), 510.0);

    // На единицу длиннее до вычислителя уже не доходит: отказ на разборе.
    Ast ast;
    Diagnostic diag;
    const std::string tooLong = source + " + 1";
    EXPECT_FALSE(CS::parseExpression(
        tooLong.data(), static_cast<std::uint32_t>(tooLong.size()), ast, diag));
    EXPECT_STREQ(diag.message, "expression nesting too deep");
}

TEST(EvalAggregates, EachEvaluationCreatesANewAggregate) {
    Store store;
    CS::Execution exec(store);
    Ast ast;
    Diagnostic diag;
    const std::string_view text = "[1, 2]";
    // Дерево используется дважды подряд, поэтому проверка идёт через фасад
    // компиляции напрямую, а не через evaluate(): тому нужен свежий Ast на
    // каждый вызов, а здесь как раз важно одно и то же дерево.
    ASSERT_EQ(CS::compileExpression(text.data(),
                                    static_cast<std::uint32_t>(text.size()),
                                    ast, store, &diag, 1),
              0u)
        << diag.message;

    Value first = Value::null();
    Value second = Value::null();
    ASSERT_TRUE(CS::evalExpression(ast, text, exec, &first, diag));
    ASSERT_TRUE(CS::evalExpression(ast, text, exec, &second, diag));

    // docs/semantics.md §2.3: литерал создаёт новый агрегат при каждом
    // вычислении. Без этого теста правило держится на честном слове.
    EXPECT_FALSE(first.sameAggregate(second));
    EXPECT_EQ(CS::arrayCount(first), 2u);
    EXPECT_EQ(CS::arrayCount(second), 2u);
}

TEST(EvalOperators, UnaryWorksThroughTheWalk) {
    Store store;
    CS::Execution exec(store);
    EXPECT_FALSE(evaluate(exec, "!true").booleanValue());
    EXPECT_EQ(evaluate(exec, "-3").numberValue(), -3.0);
}

TEST(EvalOperators, ArithmeticRespectsPrecedence) {
    Store store;
    CS::Execution exec(store);
    // Приоритет — дело грамматики; вычислитель лишь обходит построенное дерево.
    EXPECT_EQ(evaluate(exec, "1 + 2 * 3").numberValue(), 7.0);
    EXPECT_EQ(evaluate(exec, "(1 + 2) * 3").numberValue(), 9.0);
}

TEST(EvalOperators, ComparisonWorksThroughTheWalk) {
    Store store;
    CS::Execution exec(store);
    EXPECT_TRUE(evaluate(exec, "1 < 2").booleanValue());
    EXPECT_FALSE(evaluate(exec, "1 > 2").booleanValue());
}

TEST(EvalOperators, EqualityWorksThroughTheWalk) {
    Store store;
    CS::Execution exec(store);
    EXPECT_TRUE(evaluate(exec, "1 == 1").booleanValue());
    EXPECT_TRUE(evaluate(exec, "1 != 2").booleanValue());
    EXPECT_TRUE(evaluate(exec, "null == null").booleanValue());
}

TEST(EvalOperators, OperandsComeFromTheStore) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'count': 41}");
    EXPECT_EQ(evaluate(exec, "state.count + 1").numberValue(), 42.0);
}

TEST(EvalOperators, ErrorInTheLeftOperandStopsEvaluation) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1]");
    // Ошибка — Range через items[-1], не неизвестное имя: статический проход
    // (core/src/check.hpp) отсеял бы неизвестное имя ещё до вычисления, и
    // пробой «дошли ли мы сюда вычислением» оно уже быть не может. Range —
    // код, которого сам '+' не порождает, поэтому от штатного результата
    // отличим по коду, а не только по факту отказа.
    EXPECT_EQ(evalError(exec, "items[-1] + 1").code, CS::ErrorCode::Range);
}

TEST(EvalOperators, ErrorInTheRightOperandStopsEvaluation) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1]");
    EXPECT_EQ(evalError(exec, "1 + items[-1]").code, CS::ErrorCode::Range);
}

TEST(EvalOperators, LeftOperandIsEvaluatedBeforeTheRight) {
    Store store;
    CS::Execution exec(store);
    // docs/semantics.md §3.3 фиксирует порядок именно ради определённости
    // диагностики, когда ошибочны оба операнда. С одним ошибочным операндом
    // порядок ненаблюдаем, и перестановка прошла бы незамеченной.
    const Diagnostic diag = evalError(exec, "(1 + 'a') + (2 + 'b')");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_LT(diag.offset, 10u);
}

TEST(EvalOperators, AggregateEqualityIsByIdentityThroughTheWalk) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1, 2]");
    // Литерал создаёт новый агрегат при каждом вычислении, поэтому сравнение
    // с ним ложно даже при совпадающем содержимом.
    EXPECT_TRUE(evaluate(exec, "items == items").booleanValue());
    EXPECT_FALSE(evaluate(exec, "items == [1, 2]").booleanValue());
}

TEST(EvalShortCircuit, AndDoesNotEvaluateTheRightOperand) {
    Store store;
    CS::Execution exec(store);
    // Побочных эффектов в выражениях нет, поэтому невычисление наблюдается
    // единственным способом: ошибка справа не всплывает.
    //
    // Проба — Type через 1 + 'a', не неизвестное имя: статический проход
    // (core/src/check.hpp) отсеял бы неизвестное имя ещё до вычисления,
    // независимо от того, короткое замыкание его достигает или нет, и такой
    // пробой служить больше не может.
    EXPECT_FALSE(evaluate(exec, "false && (1 + 'a')").booleanValue());
}

TEST(EvalShortCircuit, AndEvaluatesTheRightOperandWhenNeeded) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1]");
    EXPECT_FALSE(evaluate(exec, "true && false").booleanValue());
    EXPECT_TRUE(evaluate(exec, "true && true").booleanValue());
    // Range через items[-1]: код, которого && сам не порождает.
    EXPECT_EQ(evalError(exec, "true && items[-1]").code, CS::ErrorCode::Range);
}

TEST(EvalShortCircuit, OrDoesNotEvaluateTheRightOperand) {
    Store store;
    CS::Execution exec(store);
    EXPECT_TRUE(evaluate(exec, "true || (1 + 'a')").booleanValue());
}

TEST(EvalShortCircuit, OrEvaluatesTheRightOperandWhenNeeded) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1]");
    EXPECT_TRUE(evaluate(exec, "false || true").booleanValue());
    EXPECT_FALSE(evaluate(exec, "false || false").booleanValue());
    // Range через items[-1]: код, которого || сам не порождает.
    EXPECT_EQ(evalError(exec, "false || items[-1]").code, CS::ErrorCode::Range);
}

TEST(EvalShortCircuit, ErrorOnTheLeftIsNotSwallowed) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1]");
    // docs/semantics.md §5.5: ошибка && false — ошибка. Левый операнд обязан
    // быть булевым по грамматике оператора, поэтому пробу берём такую, что
    // сама по себе даёт Range ещё до проверки булевости (items[-1]): код,
    // которого ни +, ни && / || не порождают, и коллизии с их штатной
    // ошибкой типа не возникает.
    EXPECT_EQ(evalError(exec, "items[-1] && false").code, CS::ErrorCode::Range);
    EXPECT_EQ(evalError(exec, "items[-1] || true").code, CS::ErrorCode::Range);
}

TEST(EvalShortCircuit, TypeOfTheUnevaluatedOperandIsNotChecked) {
    Store store;
    CS::Execution exec(store);
    // Самая точная проверка правила: тип правого операнда проверяется тогда и
    // только тогда, когда его пришлось вычислить. Поодиночке ни одна из двух
    // строк ничего не доказывает.
    EXPECT_FALSE(evaluate(exec, "false && 5").booleanValue());
    EXPECT_EQ(evalError(exec, "true && 5").code, CS::ErrorCode::Type);
    // Зеркало для ||: у него замыкает истина, а не ложь.
    EXPECT_TRUE(evaluate(exec, "true || 5").booleanValue());
    EXPECT_EQ(evalError(exec, "false || 5").code, CS::ErrorCode::Type);
}

TEST(EvalShortCircuit, LogicalOperatorsRequireBooleanOnTheLeft) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evalError(exec, "1 && true").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "'a' || true").code, CS::ErrorCode::Type);
}

TEST(EvalShortCircuit, GuardIdiomProtectsTheRightSide) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'items': []}");
    // Ради этого короткое замыкание и существует. Правая часть без защиты
    // слева даёт ошибку типа: строковый индекс массива запрещён. Настоящая
    // идиома из §5.5 пользуется count(), который придёт с частью 3, — здесь
    // та же форма на доступных средствах.
    //
    // Обе строки обязательны: одна показывает, что справа не пошли, вторая —
    // что там действительно есть на что наткнуться.
    EXPECT_FALSE(evaluate(exec, "false && state.items['0'] == 1").booleanValue());
    EXPECT_EQ(evalError(exec, "true && state.items['0'] == 1").code,
              CS::ErrorCode::Type);
}

TEST(EvalNilCoalesce, TakesTheLeftWhenItIsNotNull) {
    Store store;
    CS::Execution exec(store);
    // Проба справа — Type через 1 + 'a' (см. DoesNotSwallowErrors ниже,
    // где этот приём уже применён): она обязана не всплыть, если левый не
    // null.
    EXPECT_EQ(evaluate(exec, "1 ?? (1 + 'a')").numberValue(), 1.0);
}

TEST(EvalNilCoalesce, TakesTheRightWhenTheLeftIsNull) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evaluate(exec, "null ?? 2").numberValue(), 2.0);
}

TEST(EvalNilCoalesce, DoesNotSwallowErrors) {
    Store store;
    CS::Execution exec(store);
    // docs/semantics.md §5.6: ?? перехватывает только null. Соблазнительно
    // принять его за «если что-то пойдёт не так, подставь запасное»; он делает
    // не это.
    EXPECT_EQ(evalError(exec, "(1 + 'a') ?? 0").code, CS::ErrorCode::Type);
    // И справа тоже: если левый null, правый вычисляется по-настоящему.
    EXPECT_EQ(evalError(exec, "null ?? (2 + 'b')").code, CS::ErrorCode::Type);
}

TEST(EvalNilCoalesce, OperandTypesNeedNotMatch) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evalText(exec, "null ?? 'запасное'"), "запасное");
}

TEST(EvalNilCoalesce, ChainsRightAssociatively) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'nickname': null}");
    EXPECT_EQ(evalText(exec, "user.nickname ?? user.name ?? 'Гость'"),
              "Гость");
}

TEST(EvalTernary, EvaluatesOnlyTheSelectedBranch) {
    Store store;
    CS::Execution exec(store);
    // Проба в невыбранной ветке — Type через 1 + 'a', не неизвестное имя: она
    // обязана не всплыть, что доказывает невычисление.
    EXPECT_EQ(evaluate(exec, "true ? 1 : (1 + 'a')").numberValue(), 1.0);
    EXPECT_EQ(evaluate(exec, "false ? (1 + 'a') : 2").numberValue(), 2.0);
}

TEST(EvalTernary, ConditionMustBeBoolean) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evalError(exec, "1 ? 1 : 2").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "null ? 1 : 2").code, CS::ErrorCode::Type);
}

TEST(EvalTernary, BranchesNeedNotShareAType) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evaluate(exec, "true ? 1 : 'a'").numberValue(), 1.0);
    EXPECT_EQ(evalText(exec, "false ? 1 : 'a'"), "a");
}

/// Разбирает и выполняет скрипт; требует успеха обоих шагов.
void run(CS::Execution &exec, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors =
        CS::compileScript(text.data(), static_cast<std::uint32_t>(text.size()),
                          ast, exec.store(), &diag, 1);
    ASSERT_EQ(errors, 0u) << diag.message;
    ASSERT_TRUE(CS::runScript(ast, text, exec, diag)) << diag.message;
}

/// Разбирает успешно, выполняет с отказом; возвращает диагностику выполнения.
Diagnostic runError(CS::Execution &exec, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors =
        CS::compileScript(text.data(), static_cast<std::uint32_t>(text.size()),
                          ast, exec.store(), &diag, 1);
    if (errors != 0) {
        ADD_FAILURE() << diag.message;
        return diag;
    }
    EXPECT_FALSE(CS::runScript(ast, text, exec, diag));
    return diag;
}

TEST(EvalAssign, ExistingKeyIsReplaced) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'count': 1}");
    run(exec, "state.count = 42;");
    EXPECT_EQ(evaluate(exec, "state.count").numberValue(), 42.0);
}

TEST(EvalAssign, MissingKeyIsCreated) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{}");
    // docs/semantics.md §6.2: запись создаёт ключ, если его нет.
    run(exec, "state.fresh = 'значение';");
    EXPECT_EQ(evalText(exec, "state.fresh"), "значение");
}

TEST(EvalAssign, ValueMayBeAnyExpression) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'a': 2, 'b': 3}");
    run(exec, "state.sum = state.a * state.b + 1;");
    EXPECT_EQ(evaluate(exec, "state.sum").numberValue(), 7.0);
}

TEST(EvalAssign, DeepPathIsWritable) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'profile': {'city': {}}}");
    run(exec, "user.profile.city.name = 'Москва';");
    EXPECT_EQ(evalText(exec, "user.profile.city.name"), "Москва");
}

TEST(EvalAssign, WritingIntoNullIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася'}");
    // docs/semantics.md §7.2: мягкость §6.3 распространяется только на чтение.
    // Обе половины обязательны: без второй правило вырождается.
    EXPECT_EQ(evaluate(exec, "user.profile.name").kind(), Value::Kind::Null);
    EXPECT_EQ(runError(exec, "user.profile.name = 'Вася';").code,
              CS::ErrorCode::Type);
}

TEST(EvalAssign, WritingAKeyOffANonObjectIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "count", "3");
    put(store, "items", "[1]");
    EXPECT_EQ(runError(exec, "count.x = 1;").code, CS::ErrorCode::Type);
    EXPECT_EQ(runError(exec, "items.x = 1;").code, CS::ErrorCode::Type);
}

// AssigningToANameIsAnError и UnknownNameIsAnError переехали в
// core/tests/check_test.cpp (Check.AssigningToANameIsACompileError,
// Check.UnknownNameInAssignmentTargetIsACompileError): "state = 1;" и
// "usre.a = 1;" отсеиваются статическим проходом ещё до вычисления, и
// runError() до них больше не доходит.

TEST(EvalAssign, ErrorInTheValueLeavesTheTargetUntouched) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'a': 1}");
    // Ошибка — Type через 1 + 'a', не неизвестное имя: с приходом
    // статического прохода последнее ловится ещё на компиляции, а здесь
    // важно именно поведение вычислителя при рантайм-ошибке справа.
    EXPECT_EQ(runError(exec, "state.a = 1 + 'a';").code, CS::ErrorCode::Type);
    EXPECT_EQ(evaluate(exec, "state.a").numberValue(), 1.0);
}

TEST(EvalAssign, TargetCheckLosesToValueErrorWhenBothFail) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася'}");
    put(store, "items", "[1, 2, 3]");
    put(store, "minusOne", "-1");
    // docs/semantics.md §7.2: цель проверяется после вычисления правой части.
    // Запись в null сама по себе даёт Type (WritingIntoNullIsAnError), но
    // здесь неисправна и правая часть тоже — чтение по отрицательному индексу
    // даёт Range (FractionalAndNegativeIndicesAreErrors), и она вычисляется
    // раньше — побеждает Range.
    EXPECT_EQ(runError(exec, "user.profile.name = items[minusOne];").code,
              CS::ErrorCode::Range);
}

TEST(EvalScript, EmptyScriptSucceeds) {
    Store store;
    CS::Execution exec(store);
    run(exec, "");
    run(exec, ";;;");
}

TEST(EvalAssignIndex, ArrayElementIsReplaced) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[10, 20, 30]");
    run(exec, "items[1] = 99;");
    EXPECT_EQ(evaluate(exec, "items[1]").numberValue(), 99.0);
    EXPECT_EQ(evaluate(exec, "items[0]").numberValue(), 10.0);
}

TEST(EvalAssignIndex, WritingBeyondTheEndIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[10]");
    // docs/semantics.md §6.1: чтение за границей штатно, запись за границу —
    // намерение создать элемент, для чего существует push. Обе половины
    // обязательны.
    EXPECT_EQ(evaluate(exec, "items[1]").kind(), Value::Kind::Null);
    EXPECT_EQ(runError(exec, "items[1] = 1;").code, CS::ErrorCode::Range);
    EXPECT_EQ(runError(exec, "items[1000000] = 1;").code, CS::ErrorCode::Range);
    // 2^32: приведение к uint32_t усекло бы индекс в ноль, попав в границы.
    EXPECT_EQ(runError(exec, "items[4294967296] = 1;").code, CS::ErrorCode::Range);
}

TEST(EvalAssignIndex, FractionalAndNegativeIndicesAreErrors) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[10, 20]");
    put(store, "minusOne", "-1");
    EXPECT_EQ(runError(exec, "items[0.5] = 1;").code, CS::ErrorCode::Range);
    EXPECT_EQ(runError(exec, "items[minusOne] = 1;").code, CS::ErrorCode::Range);
}

TEST(EvalAssignIndex, NonNumberArrayIndexIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[10, 20]");
    EXPECT_EQ(runError(exec, "items['0'] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, ObjectKeyIsWritten) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'a': 1}");
    run(exec, "o['a'] = 2;");
    run(exec, "o['fresh'] = 3;");
    EXPECT_EQ(evaluate(exec, "o.a").numberValue(), 2.0);
    EXPECT_EQ(evaluate(exec, "o.fresh").numberValue(), 3.0);
}

TEST(EvalAssignIndex, ScalarKeysAreCoercedToString) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{}");
    // docs/semantics.md §4.1: ключ объекта — одна из трёх позиций, требующих
    // String; правила приведения те же, что при чтении.
    run(exec, "o[0] = 'ноль';");
    run(exec, "o[true] = 'да';");
    run(exec, "o[null] = 'ничего';");
    EXPECT_EQ(evalText(exec, "o['0']"), "ноль");
    EXPECT_EQ(evalText(exec, "o['true']"), "да");
    EXPECT_EQ(evalText(exec, "o['null']"), "ничего");
}

TEST(EvalAssignIndex, AggregateKeyIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{}");
    put(store, "items", "[1]");
    EXPECT_EQ(runError(exec, "o[items] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, WritingIntoNullIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася'}");
    EXPECT_EQ(evaluate(exec, "user.missing[0]").kind(), Value::Kind::Null);
    EXPECT_EQ(runError(exec, "user.missing[0] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, WritingIntoANonAggregateIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "count", "3");
    put(store, "name", "'Вася'");
    EXPECT_EQ(runError(exec, "count[0] = 1;").code, CS::ErrorCode::Type);
    EXPECT_EQ(runError(exec, "name[0] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, ChainedTargetWorks) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'rows': [{'cells': [1, 2]}]}");
    run(exec, "state.rows[0].cells[1] = 99;");
    EXPECT_EQ(evaluate(exec, "state.rows[0].cells[1]").numberValue(), 99.0);
}

TEST(EvalAssignIndex, SubscriptMayBeAnExpression) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[10, 20, 30]");
    put(store, "i", "1");
    run(exec, "items[i + 1] = 99;");
    EXPECT_EQ(evaluate(exec, "items[2]").numberValue(), 99.0);
}

TEST(EvalAssignToName, WritesAScalar) {
    Store store;
    CS::Execution exec(store);
    CS::Deferred dead;
    store.setGlobal("liked", Value::boolean(false), dead);

    run(exec, "liked = true;");

    EXPECT_TRUE(evaluate(exec, "liked").booleanValue());
}

TEST(EvalAssignToName, CompoundAssignmentReadsTheCell) {
    Store store;
    CS::Execution exec(store);
    CS::Deferred dead;
    store.setGlobal("n", Value::number(1), dead);

    // x op= e есть x = x op e, и читается x из ячейки.
    run(exec, "n += 2; n *= 3;");

    EXPECT_EQ(evaluate(exec, "n").numberValue(), 9.0);
}

TEST(EvalAssignToName, BumpsTheCellEpoch) {
    Store store;
    CS::Execution exec(store);
    CS::Deferred dead;
    store.setGlobal("n", Value::number(1), dead);
    const CS::GlobalSlot slot = store.globalSlot("n");
    const CS::Epoch before = store.epochAt(slot);

    run(exec, "n = 2;");

    // Без подъёма отслеживание зависимостей записи не увидит, и выражение,
    // читающее n, останется на прежнем значении навсегда.
    EXPECT_GT(store.epochAt(slot), before);
}

TEST(EvalAssignToName, ReplacingTheBindingLeavesTheAliasAlone) {
    Store store;
    CS::Execution exec(store);
    CS::Deferred dead;
    // Алиас создаёт хост, и создать его можно только отсюда: через C-границу
    // одну коробку под двумя именами не передать — все четыре сеттера строят
    // значение с нуля (спека §2.2).
    const Value shared = CS::makeObject(store.keys(), 1, store.clock(), dead);
    store.setGlobal("a", shared, dead);
    store.setGlobal("b", shared, dead);

    // Содержимое общее: изменение через одно имя видно через второе (§2.3).
    run(exec, "a.k = 1;");
    EXPECT_EQ(evaluate(exec, "b.k").numberValue(), 1.0);

    // Привязка — не содержимое: b продолжает смотреть на прежнюю коробку.
    run(exec, "a = 5;");
    EXPECT_EQ(evaluate(exec, "a").numberValue(), 5.0);
    EXPECT_EQ(evaluate(exec, "b.k").numberValue(), 1.0);
}

#ifndef NDEBUG
TEST(EvalAssignToName,
     ReplacingTheBindingWithAnAggregateReleasesTheDisplacedBox) {
    const std::size_t empty = CS::detail::liveBoxCount();
    {
        Store store;
        CS::Execution exec(store);
        CS::Deferred dead;
        // Тот же алиас, что и выше, но привязка меняется на агрегат, а не на
        // скаляр: это единственный путь, которым evalTracked заходит в
        // retain-a-box ветку setGlobalAt, а не только в retain-a-scalar.
        const Value shared = CS::makeObject(store.keys(), 1, store.clock(), dead);
        store.setGlobal("a", shared, dead);
        store.setGlobal("b", shared, dead);
        run(exec, "a.k = 1;");

        run(exec, "a = {'k': 2};");

        // Привязка a ушла на новую коробку; b по-прежнему смотрит на старую
        // — алиас не следует за присваиванием имени (§2.3).
        EXPECT_EQ(evaluate(exec, "b.k").numberValue(), 1.0);
        EXPECT_EQ(evaluate(exec, "a.k").numberValue(), 2.0);
    }
    // Хранилище умерло вместе с обеими коробками — вытесненная не потекла.
    EXPECT_EQ(CS::detail::liveBoxCount(), empty);
}
#endif

TEST(EvalCompound, FourOperatorsWorkOnAKey) {
    Store store;
    CS::Execution exec(store);
    put(store, "s", "{'n': 10}");
    run(exec, "s.n += 5;");
    EXPECT_EQ(evaluate(exec, "s.n").numberValue(), 15.0);
    run(exec, "s.n -= 3;");
    EXPECT_EQ(evaluate(exec, "s.n").numberValue(), 12.0);
    run(exec, "s.n *= 2;");
    EXPECT_EQ(evaluate(exec, "s.n").numberValue(), 24.0);
    run(exec, "s.n /= 4;");
    EXPECT_EQ(evaluate(exec, "s.n").numberValue(), 6.0);
}

TEST(EvalCompound, WorksOnAnArrayElement) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1, 2, 3]");
    run(exec, "items[1] += 10;");
    EXPECT_EQ(evaluate(exec, "items[1]").numberValue(), 12.0);
}

TEST(EvalCompound, WorksOnAnObjectKeyByIndex) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'a': 1}");
    run(exec, "o['a'] += 1;");
    EXPECT_EQ(evaluate(exec, "o.a").numberValue(), 2.0);
}

TEST(EvalCompound, TypeMismatchIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "s", "{'text': 'а'}");
    // Операция берётся из applyBinary, поэтому правила типов те же, что у
    // обычного оператора: конкатенации строк через + нет.
    EXPECT_EQ(runError(exec, "s.text += 'б';").code, CS::ErrorCode::Type);
}

TEST(EvalCompound, MissingKeyReadsAsNullAndThenFails) {
    Store store;
    CS::Execution exec(store);
    put(store, "s", "{}");
    // Чтение отсутствующего ключа даёт null (§6.2), а null + 1 — ошибка типа.
    EXPECT_EQ(runError(exec, "s.missing += 1;").code, CS::ErrorCode::Type);
}

TEST(EvalCompound, BeyondTheEndGivesTypeNotRange) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1, 2]");
    // docs/semantics.md §7.3: x += e есть x = x + e. Сначала читается items[5],
    // что штатно даёт null, затем вычисляется null + 1 — ошибка типа. До
    // проверки границы записи дело не доходит, поэтому Type, а не Range.
    // Простое присваивание туда же даёт Range — обе строки обязательны.
    EXPECT_EQ(runError(exec, "items[5] += 1;").code, CS::ErrorCode::Type);
    EXPECT_EQ(runError(exec, "items[5] = 1;").code, CS::ErrorCode::Range);
}

TEST(EvalCompound, ErrorLeavesTheTargetUntouched) {
    Store store;
    CS::Execution exec(store);
    put(store, "s", "{'n': 10}");
    EXPECT_EQ(runError(exec, "s.n += 'а';").code, CS::ErrorCode::Type);
    EXPECT_EQ(evaluate(exec, "s.n").numberValue(), 10.0);
}

TEST(EvalCompound, TargetCheckLosesToValueErrorWhenBothFail) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1, 2, 3]");
    // §7.3 говорит про результат ('x += e' есть 'x = x + e'), а не про
    // порядок проверок: цель проверяется после вычисления правой части
    // (docs/semantics.md §7.2). Индекс -1 сам по себе дал бы Range
    // (FractionalAndNegativeIndicesAreErrors), но правая часть неисправна —
    // Type через 1 + 'a' — и она побеждает первой.
    EXPECT_EQ(runError(exec, "items[-1] += 1 + 'a';").code, CS::ErrorCode::Type);
}

TEST(EvalCompound, DivisionByZeroFollowsIEEE) {
    Store store;
    CS::Execution exec(store);
    put(store, "s", "{'n': 1}");
    // Деление на ноль даёт бесконечность, а не ошибку (§5.2).
    run(exec, "s.n /= 0;");
    EXPECT_TRUE(std::isinf(evaluate(exec, "s.n").numberValue()));
}

TEST(EvalCompound, DeepTargetWorks) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'rows': [{'n': 1}]}");
    put(store, "i", "0");
    // Однократность вычисления цели (docs/grammar.md §6.4) в этом языке
    // ненаблюдаема: выражения чисты, поэтому повторное вычисление дало бы тот
    // же результат. Тест проверяет лишь, что сложная цель вообще работает;
    // само требование держится устройством кода, а не этой проверкой.
    run(exec, "state.rows[i].n += 41;");
    EXPECT_EQ(evaluate(exec, "state.rows[0].n").numberValue(), 42.0);
}

TEST(EvalScriptBehaviour, StatementsApplyInOrder) {
    Store store;
    CS::Execution exec(store);
    put(store, "s", "{'n': 0}");
    // Каждый стейтмент читает результат предыдущего, поэтому 13 получается
    // только если применились все три и именно в этом порядке: пропуск первого
    // даёт 3, второго — 4, третьего — 10, перестановка — иное число.
    run(exec, "s.n = s.n + 1; s.n = s.n * 10; s.n = s.n + 3;");
    EXPECT_EQ(evaluate(exec, "s.n").numberValue(), 13.0);
}

TEST(EvalScriptBehaviour, LaterStatementsSeeEarlierWrites) {
    Store store;
    CS::Execution exec(store);
    put(store, "s", "{'a': 1}");
    run(exec, "s.b = s.a + 1; s.c = s.b + 1;");
    EXPECT_EQ(evaluate(exec, "s.c").numberValue(), 3.0);
}

TEST(EvalScriptBehaviour, ErrorStopsTheScriptAndKeepsWhatWasDone) {
    Store store;
    CS::Execution exec(store);
    put(store, "s", "{'a': 0, 'b': 0, 'c': 0}");
    // docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md: откатывать
    // нечего, предыдущих состояний хранилище не держит. Обработчик, упавший на
    // третьем присваивании из пяти, оставит первые два применёнными.
    //
    // Ошибка — Type через 1 + 'a', не неизвестное имя: статический проход
    // отсеял бы весь скрипт ещё до первого стейтмента, и «первые два
    // применились» стало бы неверно уже по другой причине.
    const Diagnostic diag =
        runError(exec, "s.a = 1; s.b = 2; s.x = 1 + 'a'; s.c = 3;");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_EQ(evaluate(exec, "s.a").numberValue(), 1.0);
    EXPECT_EQ(evaluate(exec, "s.b").numberValue(), 2.0);
    EXPECT_EQ(evaluate(exec, "s.c").numberValue(), 0.0);
    EXPECT_FALSE(CS::objectHas(store.global("s"), "x"));
}

TEST(EvalScriptBehaviour, MutationIsVisibleThroughAnotherName) {
    Store store;
    CS::Execution exec(store);
    // Хост кладёт один агрегат под двумя именами: значения — хендлы, поэтому
    // это тот же массив (docs/semantics.md §2.3).
    put(store, "state", "{'items': [1, 2]}");
    const Value items = CS::objectGet(store.global("state"), "items");
    store.setGlobal("shortcut", items, exec.deferred());

    run(exec, "state.items[0] = 99;");
    EXPECT_EQ(evaluate(exec, "shortcut[0]").numberValue(), 99.0);
}

TEST(EvalScriptBehaviour, AssignmentCreatesAnAliasJustLikeTheHostDoes) {
    Store store;
    CS::Execution exec(store);
    // §2.3, первое предложение: «присваивание... копии не создаёт». Здесь, в
    // отличие от MutationIsVisibleThroughAnotherName выше, второе имя для
    // массива ставит не хост, а само присваивание скрипта.
    put(store, "state", "{'a': [1, 2], 'b': null}");
    run(exec, "state.b = state.a; state.a[0] = 9;");
    EXPECT_EQ(evaluate(exec, "state.b[0]").numberValue(), 9.0);
}

TEST(EvalScriptBehaviour, SelfReferenceIsAValidScript) {
    Store store;
    CS::Execution exec(store);
    // §2.3: 'obj[\'self\'] = obj;' — корректная программа, ссылочность
    // допускает циклы. Запись не обходит значение и потому не зацикливается;
    // повторное чтение через self подтверждает, что цикл остался невредимым.
    put(store, "obj", "{}");
    // 'self' — зарезервированное слово (docs/grammar.md §4.5), поэтому чтение
    // назад идёт через '[]', как и запись; через '.' этот ключ недостижим.
    run(exec, "obj['self'] = obj;");
    EXPECT_TRUE(evaluate(exec, "obj['self'] == obj").booleanValue());
    EXPECT_TRUE(evaluate(exec, "obj['self']['self']['self'] == obj").booleanValue());
    // Break the cycle through the language itself, so the box does not
    // outlive this test (tools/asan.sh runs under LeakSanitizer).
    run(exec, "obj['self'] = null;");
}

TEST(EvalScriptBehaviour, EmptyStatementsAreSkipped) {
    Store store;
    CS::Execution exec(store);
    put(store, "s", "{'n': 0}");
    run(exec, ";; s.n = 1 ;;");
    EXPECT_EQ(evaluate(exec, "s.n").numberValue(), 1.0);
}

TEST(EvalScriptBehaviour, DeepPathInsideAScript) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'rows': [{'cells': [0]}, {'cells': [0]}]}");
    run(exec, "state.rows[0].cells[0] = 1; state.rows[1].cells[0] = 2;");
    EXPECT_EQ(evaluate(exec, "state.rows[0].cells[0]").numberValue(), 1.0);
    EXPECT_EQ(evaluate(exec, "state.rows[1].cells[0]").numberValue(), 2.0);
}

TEST(EvalCall, CountOfEachKind) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[10, 20, 30]");
    put(store, "o", "{'a': 1, 'b': 2}");
    put(store, "empty", "[]");
    EXPECT_EQ(evaluate(exec, "count(items)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(exec, "count(o)").numberValue(), 2.0);
    EXPECT_EQ(evaluate(exec, "count(empty)").numberValue(), 0.0);
}

TEST(EvalCall, CountOfStringCountsBytesNotCharacters) {
    Store store;
    CS::Execution exec(store);
    // docs/semantics.md §8.1 явно: байты, а не символы.
    EXPECT_EQ(evaluate(exec, "count('привет')").numberValue(), 12.0);
    EXPECT_EQ(evaluate(exec, "count('😀')").numberValue(), 4.0);
    EXPECT_EQ(evaluate(exec, "count('abc')").numberValue(), 3.0);
    EXPECT_EQ(evaluate(exec, "count('')").numberValue(), 0.0);
}

TEST(EvalCall, CountRejectsScalarsOtherThanString) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evalError(exec, "count(1)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "count(true)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "count(null)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, KeysReturnsEveryKey) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'b': 1, 'a': 2, 'c': 3}");
    const Value keys = evaluate(exec, "keys(o)");
    ASSERT_EQ(keys.kind(), Value::Kind::Array);
    ASSERT_EQ(CS::arrayCount(keys), 3u);
    // Порядок docs/semantics.md §8.2 не определяет, поэтому тест собирает
    // множество, а не список: опираться на порядок значило бы обещать его.
    std::set<std::string> got;
    for (std::uint32_t i = 0; i < 3; ++i) {
        const Value key = CS::arrayAt(keys, i);
        got.insert(std::string(CS::stringBytes(key)));
    }
    EXPECT_EQ(got, (std::set<std::string>{"a", "b", "c"}));
}

TEST(EvalCall, KeysOfEmptyObjectIsEmptyArray) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{}");
    const Value keys = evaluate(exec, "keys(o)");
    EXPECT_EQ(CS::arrayCount(keys), 0u);
}

TEST(EvalCall, KeysRejectsNonObjects) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1]");
    EXPECT_EQ(evalError(exec, "keys(items)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "keys('a')").code, CS::ErrorCode::Type);
}

TEST(EvalCall, HasDistinguishesAbsentFromNull) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'present': 1, 'empty': null}");
    // docs/semantics.md §8.3: единственный способ их различить. Обе половины
    // обязательны, иначе правило вырождается.
    EXPECT_TRUE(evaluate(exec, "has(o, 'present')").booleanValue());
    EXPECT_TRUE(evaluate(exec, "has(o, 'empty')").booleanValue());
    EXPECT_FALSE(evaluate(exec, "has(o, 'missing')").booleanValue());
    EXPECT_EQ(evaluate(exec, "o.empty").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(exec, "o.missing").kind(), Value::Kind::Null);
}

TEST(EvalCall, HasCoercesTheKey) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'0': 'ноль', 'true': 'да'}");
    EXPECT_TRUE(evaluate(exec, "has(o, 0)").booleanValue());
    EXPECT_TRUE(evaluate(exec, "has(o, true)").booleanValue());
    EXPECT_EQ(evalError(exec, "has(o, [1])").code, CS::ErrorCode::Type);
}

TEST(EvalCall, LastOfArrayAndOfEmpty) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[10, 20, 30]");
    put(store, "empty", "[]");
    EXPECT_EQ(evaluate(exec, "last(items)").numberValue(), 30.0);
    // docs/semantics.md §8.4: на пустом — null, тогда как items[count-1] дал бы
    // ошибку. Обе половины в одном тесте.
    EXPECT_EQ(evaluate(exec, "last(empty)").kind(), Value::Kind::Null);
    EXPECT_EQ(evalError(exec, "empty[count(empty) - 1]").code,
              CS::ErrorCode::Range);
}

TEST(EvalCall, LastRejectsNonArrays) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{}");
    EXPECT_EQ(evalError(exec, "last(o)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, NestedCallsWork) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'a': 1, 'b': 2}");
    EXPECT_EQ(evaluate(exec, "count(keys(o))").numberValue(), 2.0);
}

TEST(EvalCall, ArgumentsAreEvaluatedLeftToRight) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1]");
    // Оба аргумента негодны, но по-разному: код ошибки называет, который из
    // них вычислялся первым.
    EXPECT_EQ(evalError(exec, "has(items[-1], 2 + 'b')").code,
              CS::ErrorCode::Range);
}

TEST(EvalCall, VariadicFormatDoesNotOverflowTheArgumentBuffer) {
    Store store;
    CS::Execution exec(store);
    // Буфер аргументов в ветке Call рассчитан на два: format обязан идти мимо
    // него. Пять аргументов затёрли бы стек, попади они туда.
    EXPECT_EQ(evalText(exec, "format('${}${}${}${}${}', 1, 2, 3, 4, 5)"),
              "12345");
}

TEST(EvalCall, PushAppends) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1, 2]");
    run(exec, "push(items, 3);");
    EXPECT_EQ(evaluate(exec, "count(items)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(exec, "items[2]").numberValue(), 3.0);
}

TEST(EvalCall, PushIsTheOnlyWayToGrowAnArray) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1]");
    // docs/semantics.md §6.1: запись за границу — ошибка, расширяет только push.
    EXPECT_EQ(runError(exec, "items[1] = 2;").code, CS::ErrorCode::Range);
    run(exec, "push(items, 2);");
    EXPECT_EQ(evaluate(exec, "items[1]").numberValue(), 2.0);
}

TEST(EvalCall, PushRejectsNonArrays) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{}");
    EXPECT_EQ(runError(exec, "push(o, 1);").code, CS::ErrorCode::Type);
}

TEST(EvalCall, PopRemovesAndDoesNothingOnEmpty) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1, 2]");
    put(store, "empty", "[]");
    run(exec, "pop(items);");
    EXPECT_EQ(evaluate(exec, "count(items)").numberValue(), 1.0);
    // docs/semantics.md §8.6: на пустом ничего не делает и не отказывает.
    run(exec, "pop(empty);");
    EXPECT_EQ(evaluate(exec, "count(empty)").numberValue(), 0.0);
}

TEST(EvalCall, TakingTheRemovedElementNeedsTwoSteps) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'items': [1, 2, 3], 'taken': null}");
    // docs/semantics.md §8.6 показывает ровно эту пару: pop значения не
    // возвращает, читают его через last.
    run(exec, "state.taken = last(state.items); pop(state.items);");
    EXPECT_EQ(evaluate(exec, "state.taken").numberValue(), 3.0);
    EXPECT_EQ(evaluate(exec, "count(state.items)").numberValue(), 2.0);
}

TEST(EvalCall, PushAndPopAreVisibleThroughAnAlias) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'items': [1]}");
    const Value items = CS::objectGet(store.global("state"), "items");
    store.setGlobal("shortcut", items, exec.deferred());
    // docs/semantics.md §2.3: мутация через один путь видна через второй.
    run(exec, "push(state.items, 2);");
    EXPECT_EQ(evaluate(exec, "count(shortcut)").numberValue(), 2.0);
}

TEST(EvalCall, StrConvertsScalars) {
    Store store;
    CS::Execution exec(store);
    // docs/semantics.md §8.7 — правила §4.2 и §4.3.
    EXPECT_EQ(evalText(exec, "str(1)"), "1");
    EXPECT_EQ(evalText(exec, "str(0.5)"), "0.5");
    EXPECT_EQ(evalText(exec, "str(true)"), "true");
    EXPECT_EQ(evalText(exec, "str(false)"), "false");
    EXPECT_EQ(evalText(exec, "str(null)"), "null");
    EXPECT_EQ(evalText(exec, "str('уже строка')"), "уже строка");
}

TEST(EvalCall, StrRejectsAggregates) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1]");
    put(store, "o", "{}");
    EXPECT_EQ(evalError(exec, "str(items)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "str(o)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, MinAndMaxTakeExactlyTwo) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evaluate(exec, "min(1, 2)").numberValue(), 1.0);
    EXPECT_EQ(evaluate(exec, "max(1, 2)").numberValue(), 2.0);
    EXPECT_EQ(evaluate(exec, "min(-1, -2)").numberValue(), -2.0);
    // Для трёх и более — вложение (§8.9).
    EXPECT_EQ(evaluate(exec, "min(3, min(1, 2))").numberValue(), 1.0);
}

TEST(EvalCall, MinAndMaxRejectNonNumbers) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evalError(exec, "min('a', 1)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "max(1, true)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "min(null, 1)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, MinAndMaxPropagateNaN) {
    Store store;
    CS::Execution exec(store);
    // 0 / 0 даёт NaN как значение (§5.2). Обе позиции обязательны: fmin и fmax
    // вернули бы число в каждой из них.
    EXPECT_TRUE(std::isnan(evaluate(exec, "min(0 / 0, 5)").numberValue()));
    EXPECT_TRUE(std::isnan(evaluate(exec, "min(5, 0 / 0)").numberValue()));
    EXPECT_TRUE(std::isnan(evaluate(exec, "max(0 / 0, 5)").numberValue()));
    EXPECT_TRUE(std::isnan(evaluate(exec, "max(5, 0 / 0)").numberValue()));
    // А бесконечность по-прежнему проходит насквозь, не превращаясь в NaN.
    EXPECT_EQ(evaluate(exec, "min(1, 1 / 0)").numberValue(), 1.0);
}

TEST(EvalCall, AbsIsTheModulus) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evaluate(exec, "abs(3)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(exec, "abs(-3)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(exec, "abs(0)").numberValue(), 0.0);
    EXPECT_EQ(evalError(exec, "abs('a')").code, CS::ErrorCode::Type);
}

TEST(EvalCall, RoundGoesAwayFromZero) {
    Store store;
    CS::Execution exec(store);
    // docs/semantics.md §8.10 перечисляет ровно эти случаи: от нуля, а не к
    // чётному. round(2.5) даёт 3, чего половинное-к-чётному не дало бы.
    EXPECT_EQ(evaluate(exec, "round(0.5)").numberValue(), 1.0);
    EXPECT_EQ(evaluate(exec, "round(1.5)").numberValue(), 2.0);
    EXPECT_EQ(evaluate(exec, "round(2.5)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(exec, "round(-0.5)").numberValue(), -1.0);
    EXPECT_EQ(evaluate(exec, "round(1.4)").numberValue(), 1.0);
    EXPECT_EQ(evalError(exec, "round(true)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, ArithmeticBuiltinsFollowIEEEOnSpecialValues) {
    Store store;
    CS::Execution exec(store);
    // Экспонента в числах не входит в грамматику (docs/grammar.md §4.6:
    // `1e3` — не число), поэтому "1e400" не разбирается. Бесконечность из
    // данных получают тем же путём, что и core/tests/data_test.cpp
    // (DataScalars.VeryLongIntegerBecomesInfinity): 400-значный литерал
    // переполняет double и лексер округляет его до IEEE-бесконечности.
    put(store, "inf", std::string(400, '9'));
    // Бесконечность — значение, а не ошибка (§5.2), и функции её пропускают.
    EXPECT_TRUE(std::isinf(evaluate(exec, "abs(inf)").numberValue()));
    EXPECT_TRUE(std::isinf(evaluate(exec, "max(1, inf)").numberValue()));
    EXPECT_EQ(evaluate(exec, "min(1, inf)").numberValue(), 1.0);
}

TEST(EvalFormat, SubstitutesLeftToRight) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася'}");
    put(store, "cart", "{'taken': 2, 'total': 5}");
    EXPECT_EQ(evalText(exec, "format('Привет, ${}!', user.name)"),
              "Привет, Вася!");
    EXPECT_EQ(evalText(exec,
                                  "format('${} из ${}', cart.taken, cart.total)"),
              "2 из 5");
}

TEST(EvalFormat, EscapedPlaceholderIsLiteral) {
    Store store;
    CS::Execution exec(store);
    // docs/semantics.md §8.8: $${} даёт литеральное ${} и аргумента не требует.
    EXPECT_EQ(evalText(exec, "format('цена $${}')"), "цена ${}");
    EXPECT_EQ(evalText(exec, "format('$${} и ${}', 1)"), "${} и 1");
}

TEST(EvalFormat, DollarWithoutBracesIsOrdinaryText) {
    Store store;
    CS::Execution exec(store);
    // Маркером делает не '$', а всё "${}" целиком: одиночный доллар — обычный
    // байт шаблона, где бы он ни стоял.
    EXPECT_EQ(evalText(exec, "format('a$b${}', 1)"), "a$b1");
    EXPECT_EQ(evalText(exec, "format('цена: 100$')"), "цена: 100$");
    EXPECT_EQ(evalText(exec, "format('$')"), "$");
    EXPECT_EQ(evalText(exec, "format('${')"), "${");
    EXPECT_EQ(evalText(exec, "format('$}')"), "$}");
    EXPECT_EQ(evalText(exec, "format('{}')"), "{}");
}

TEST(EvalFormat, MarkerIsFoundAfterARunOfDollars) {
    Store store;
    CS::Execution exec(store);
    // Цепочка долларов перед маркером — случай, на котором ошибается всякий
    // разбор, ищущий маркер по первому символу и не отступающий назад.
    // '$$$${}' — это литеральные '$$', затем экранирование '$${}'.
    EXPECT_EQ(evalText(exec, "format('$$$${}')"), "$$${}");
    // Ещё один доллар впереди — ещё один литеральный.
    EXPECT_EQ(evalText(exec, "format('$$$$${}')"), "$$$${}");
    // Тот же разбег, но следом настоящий плейсхолдер.
    EXPECT_EQ(evalText(exec, "format('$$$${}${}', 1)"), "$$${}1");
}

TEST(EvalFormat, NoPlaceholdersGivesTheTemplate) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evalText(exec, "format('без подстановок')"),
              "без подстановок");
    EXPECT_EQ(evalText(exec, "format('')"), "");
}

TEST(EvalFormat, ArgumentsAreCoercedByChapterFour) {
    Store store;
    CS::Execution exec(store);
    EXPECT_EQ(evalText(exec, "format('${}', 0.5)"), "0.5");
    EXPECT_EQ(evalText(exec, "format('${}', true)"), "true");
    EXPECT_EQ(evalText(exec, "format('${}', null)"), "null");
}

TEST(EvalFormat, AggregateArgumentIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1]");
    EXPECT_EQ(evalError(exec, "format('${}', items)").code, CS::ErrorCode::Type);
}

TEST(EvalFormat, NonStringTemplateIsAnError) {
    Store store;
    CS::Execution exec(store);
    put(store, "n", "1");
    EXPECT_EQ(evalError(exec, "format(n, 1)").code, CS::ErrorCode::Type);
}

TEST(EvalFormat, MismatchWithANonLiteralTemplateIsARuntimeError) {
    Store store;
    CS::Execution exec(store);
    put(store, "tpl", "{'two': '${} и ${}', 'none': 'нет'}");
    // Шаблон не литерал, значит проход сверить не мог — ловится здесь (§8.8).
    EXPECT_EQ(evalError(exec, "format(tpl.two, 1)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(exec, "format(tpl.none, 1)").code, CS::ErrorCode::Type);
    // А совпадающее число проходит.
    EXPECT_EQ(evalText(exec, "format(tpl.two, 1, 2)"), "1 и 2");
}

TEST(EvalFormat, ResultIsAnOrdinaryString) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'label': ''}");
    run(exec, "state.label = format('${} шт.', 3);");
    EXPECT_EQ(evalText(exec, "state.label"), "3 шт.");
    // count у строки считает байты, а не символы (§8.1): пять символов
    // «3 шт.» дают семь байт, потому что ш и т по два.
    EXPECT_EQ(evaluate(exec, "count(state.label)").numberValue(), 7.0);
}

TEST(EvalFormat, LongResultCrossesPoolGrowth) {
    Store store;
    CS::Execution exec(store);
    put(store, "user", "{'name': 'Вася'}");
    // Результат заведомо длиннее начальной ёмкости пула: сборка обязана
    // пережить его переезд.
    const Value built = evaluate(
        exec,
        "format('${}${}${}${}${}${}${}${}${}${}', user.name, user.name,"
        " user.name, user.name, user.name, user.name, user.name, user.name,"
        " user.name, user.name)");
    std::string expected;
    for (int i = 0; i < 10; ++i) { expected += "Вася"; }
    EXPECT_EQ(CS::stringBytes(built), expected);
}

TEST(EvalFormat, ArgumentThatWritesToThePoolDoesNotLeakIntoTheResult) {
    Store store;
    CS::Execution exec(store);
    // Строковый литерал в аргументе сам кладёт строку в пул — между началом
    // и концом сборки. В результат это попадать не должно.
    EXPECT_EQ(evalText(exec, "format('Привет, ${}!', 'мир')"),
              "Привет, мир!");
    // Вложенная сборка не поглощается внешней.
    EXPECT_EQ(evalText(exec, "format('${}', format('${}', 1))"), "1");
    // str тоже пишет в пул. (keys сюда не годится в пример: она возвращает
    // массив, а массив — агрегат, format отверг бы его по типу раньше, чем
    // дело дошло бы до утечки байтов.)
    EXPECT_EQ(evalText(exec, "format('${}', str(2))"), "2");
}

TEST(EvalFormat, ResultSurvivesIntoTheData) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'label': ''}");
    run(exec, "state.label = format('${} шт.', 'десять');");
    EXPECT_EQ(evalText(exec, "state.label"), "десять шт.");
}

TEST(EvalFormat, NestedAssemblyKeepsTheOuterPrefix) {
    Store store;
    CS::Execution exec(store);
    // Вложенный format стоит не первым куском шаблона, поэтому к моменту его
    // начала во внешней сборке уже накоплен префикс. Внутренняя обязана снять
    // за собой ровно свой хвост и не тронуть его: это и есть свойство стека,
    // ради которого сборка живёт в отдельном буфере.
    EXPECT_EQ(evalText(exec, "format('a${}b', format('c${}d', 1))"),
              "ac1db");
    // Три уровня, размотка изнутри наружу:
    //   format('5${}6', 7)                       = "5" + "7"     + "6" = "576"
    //   format('3${}4', "576")                    = "3" + "576"  + "4" = "35764"
    //   format('1${}2', "35764")                  = "1" + "35764" + "2" = "1357642"
    EXPECT_EQ(
        evalText(exec, "format('1${}2', format('3${}4', format('5${}6', 7)))"),
        "1357642");
}

TEST(EvalDeps, AConstantDependsOnNothing) {
    Store store;
    CS::Execution exec(store);
    evaluate(exec, "42");
    EXPECT_EQ(exec.deps().count(), 0u);
    EXPECT_FALSE(exec.deps().overflowed());
}

TEST(EvalDeps, ABareScalarDependsOnItsCell) {
    Store store;
    CS::Execution exec(store);
    put(store, "button_enabled", "true");
    evaluate(exec, "button_enabled");

    ASSERT_EQ(exec.deps().count(), 1u);
    EXPECT_EQ(exec.deps().at(0).epoch,
              store.epochAddressAt(store.globalSlot("button_enabled")));
    EXPECT_EQ(exec.deps().at(0).owner.kind(), Value::Kind::Null)
        << "у ячейки владельца нет: её эпоха живёт столько же, сколько "
           "хранилище";
}

TEST(EvalDeps, APathRecordsCellAndEveryBoxOnTheWay) {
    Store store;
    CS::Execution exec(store);
    put(store, "users", "[{'name': 'Вася'}, {'name': 'Петя'}]");
    evaluate(exec, "users[0].name");

    ASSERT_EQ(exec.deps().count(), 3u);
    EXPECT_EQ(exec.deps().at(0).epoch,
              store.epochAddressAt(store.globalSlot("users")));
    EXPECT_EQ(exec.deps().at(1).owner.kind(), Value::Kind::Array);
    EXPECT_EQ(exec.deps().at(2).owner.kind(), Value::Kind::Object);
}

TEST(EvalDeps, ADeepPathOverflows) {
    Store store;
    CS::Execution exec(store);
    put(store, "u", "{'a': {'b': {'c': {'d': {'e': 1}}}}}");
    evaluate(exec, "u.a.b.c.d.e");

    EXPECT_TRUE(exec.deps().overflowed());
}

TEST(EvalDeps, TheSetIsRebuiltOnEveryEvaluation) {
    // Набор верен до следующего вычисления этого выражения — у выражения с
    // путями он меняется от вычисления к вычислению (спека §2.6).
    Store store;
    CS::Execution exec(store);
    put(store, "a", "1");
    evaluate(exec, "a");
    evaluate(exec, "42");

    EXPECT_EQ(exec.deps().count(), 0u);
}

TEST(EvalDeps, TheSameExpressionRebuildsADifferentNonEmptySet) {
    // Свидетельство выше — переход «1 зависимость → 0», и то на ДВУХ РАЗНЫХ
    // выражениях: оно не отличает «набор перестроен» от «набор дописывается,
    // а второе выражение просто ничего не добавило». Здесь выражение ОДНО и
    // то же, набор непустой в обоих прогонах, а состав между прогонами
    // меняется — ровно то свойство, ради которого §2.6 требует считать набор
    // годным только до следующего вычисления.
    Store store;
    CS::Execution exec(store);
    put(store, "flag", "true");
    put(store, "left", "{'v': 1}");
    put(store, "right", "{'v': 2}");

    // Тернарник вычисляет только выбранную ветвь (docs/semantics.md §5.7),
    // поэтому в наборе всегда ровно одна из двух коробок.
    evaluate(exec, "flag ? left.v : right.v");
    ASSERT_EQ(exec.deps().count(), 3u);
    const CS::Epoch *chosen = exec.deps().at(2).epoch;
    EXPECT_EQ(chosen, CS::epochAddressOf(store.global("left")));

    put(store, "flag", "false");
    evaluate(exec, "flag ? left.v : right.v");
    ASSERT_EQ(exec.deps().count(), 3u)
        << "набор второго прогона обязан быть непустым, а не просто короче";
    EXPECT_EQ(exec.deps().at(2).epoch,
              CS::epochAddressOf(store.global("right")))
        << "набор перестроен под новый путь, а не дописан к старому";
    for (std::uint32_t i = 0; i < exec.deps().count(); ++i) {
        EXPECT_NE(exec.deps().at(i).epoch, chosen)
            << "коробка невыбранной ветви осталась в наборе от прошлого раза";
    }
}

TEST(EvalDeps, ACallRecordsItsCalleeArgumentBoxes) {
    // Состав набора для вызова: у count(items) их две — ячейка имени и сама
    // коробка, содержимое которой билтин прочитал. Без второй набор не
    // отличает push(items, x) от отсутствия изменений (C1).
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1, 2, 3]");
    evaluate(exec, "count(items)");

    ASSERT_EQ(exec.deps().count(), 2u);
    EXPECT_EQ(exec.deps().at(0).epoch,
              store.epochAddressAt(store.globalSlot("items")));
    EXPECT_EQ(exec.deps().at(1).epoch,
              CS::epochAddressOf(store.global("items")));
    EXPECT_EQ(exec.deps().at(1).owner.kind(), Value::Kind::Array)
        << "за коробку читателю надо держаться ретейном (§2.7)";
}

/// Снимок читателя: адреса эпох набора и их сумма (спека §2.4).
///
/// Копируется НЕМЕДЛЕННО после вычисления и живёт своей жизнью: следующий
/// шаг теста — скрипт, а runScript набор не сбрасывает и пишет в него мусор
/// за все стейтменты (execution.hpp). Читатель на границе кадра устроен так
/// же: он уносит адреса к себе, а не подглядывает в движок.
struct EpochSnapshot {
    std::vector<const CS::Epoch *> epochs;
    std::uint64_t sum = 0;
};

EpochSnapshot captureEpochs(const CS::Execution &exec) {
    EpochSnapshot snapshot;
    for (std::uint32_t i = 0; i < exec.deps().count(); ++i) {
        const CS::Epoch *address = exec.deps().at(i).epoch;
        snapshot.epochs.push_back(address);
        snapshot.sum += *address;
    }
    return snapshot;
}

std::uint64_t resum(const EpochSnapshot &snapshot) {
    std::uint64_t total = 0;
    for (const CS::Epoch *address : snapshot.epochs) { total += *address; }
    return total;
}

/// Прогон одного случая «изменение обязано быть замечено».
///
/// Вычисляет выражение, снимает сумму эпох, выполняет скрипт, меняющий
/// СОДЕРЖИМОЕ агрегата, и требует, чтобы сумма разошлась. Совпадение здесь —
/// ложное попадание: читатель отдал бы устаревшее значение, а схема обещает
/// никогда этого не делать (спека §2.3).
void expectSetSeesMutation(CS::Execution &exec, std::string_view expression,
                           std::string_view mutation) {
    evaluate(exec, expression);
    const EpochSnapshot snapshot = captureEpochs(exec);
    run(exec, mutation);
    EXPECT_NE(resum(snapshot), snapshot.sum)
        << "ложное попадание: " << expression << " против " << mutation;
}

TEST(EvalDeps, CountSeesTheContentsItRead) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1, 2, 3]");
    expectSetSeesMutation(exec, "count(items)", "push(items, 9);");
}

TEST(EvalDeps, KeysSeesTheContentsItRead) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'a': 1}");
    expectSetSeesMutation(exec, "count(keys(o))", "o.b = 2;");
}

TEST(EvalDeps, HasSeesTheContentsItRead) {
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'a': 1}");
    expectSetSeesMutation(exec, "has(o, 'b')", "o.b = 2;");
}

TEST(EvalDeps, LastSeesTheContentsItRead) {
    Store store;
    CS::Execution exec(store);
    put(store, "items", "[1, 2, 3]");
    expectSetSeesMutation(exec, "last(items)", "push(items, 9);");
}

TEST(EvalDeps, ANestedArgumentAggregateIsRecordedToo) {
    // Вложенный случай: спуск записывает ячейку state и коробку state, а
    // аргументом связывается третья коробка — сам массив. Три зависимости,
    // потолок четыре (спека §2.3).
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'items': [1, 2, 3]}");
    evaluate(exec, "count(state.items)");
    EXPECT_EQ(exec.deps().count(), 3u);

    expectSetSeesMutation(exec, "count(state.items)", "push(state.items, 9);");
}

TEST(EvalDeps, AnObjectArgumentIsRecordedToo) {
    Store store;
    CS::Execution exec(store);
    put(store, "state", "{'o': {'a': 1}}");
    expectSetSeesMutation(exec, "count(state.o)", "state.o.b = 2;");
}

TEST(EvalDeps, IndexingIntoACallResultStillRecordsTheSourceBox) {
    // Коробка, которую вернул вызов, рождается заново на каждом вычислении, и
    // её эпоха после рождения не двигается уже никогда: спуск через неё
    // записывает адрес, вечно совпадающий сам с собой. Держать набор на одном
    // таком адресе нельзя — в нём обязан стоять и источник содержимого,
    // коробка o, которую прочитал keys.
    Store store;
    CS::Execution exec(store);
    put(store, "o", "{'a': 1}");
    evaluate(exec, "keys(o)[0]");

    const CS::Epoch *source = CS::epochAddressOf(store.global("o"));
    bool recorded = false;
    for (std::uint32_t i = 0; i < exec.deps().count(); ++i) {
        if (exec.deps().at(i).epoch == source) { recorded = true; }
    }
    EXPECT_TRUE(recorded)
        << "в наборе только свежерождённая коробка результата — её эпоха "
           "никогда не двинется, и набор застыл бы навсегда";
}

}  // namespace
