#include "eval.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <set>
#include <string>

#include "ast.hpp"
#include "compile.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "parser.hpp"
#include "store.hpp"

namespace {

using CS::Ast;
using CS::Store;
using CS::Diagnostic;
using CS::Value;

/// Разбирает и вычисляет; требует успеха обоих шагов.
Value evaluate(Store &store, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors = CS::compileExpression(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, store, &diag, 1);
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
    EXPECT_TRUE(CS::evalExpression(ast, text, store, &out, diag)) << diag.message;
    return out;
}

/// Разбирает успешно, вычисляет с отказом; возвращает диагностику вычисления.
Diagnostic evalError(Store &store, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors = CS::compileExpression(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, store, &diag, 1);
    if (errors != 0) {
        ADD_FAILURE() << diag.message;
        return diag;
    }
    Value out = Value::null();
    EXPECT_FALSE(CS::evalExpression(ast, text, store, &out, diag));
    return diag;
}

/// Кладёт глобальную переменную; требует успеха.
void put(Store &store, std::string_view name, std::string_view text) {
    Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(store, name, text, diag)) << diag.message;
}

TEST(EvalLiterals, NumberIsEvaluated) {
    Store store;
    EXPECT_EQ(evaluate(store, "3").numberValue(), 3.0);
    EXPECT_EQ(evaluate(store, "0.5").numberValue(), 0.5);
}

TEST(EvalLiterals, BooleanIsEvaluated) {
    Store store;
    EXPECT_TRUE(evaluate(store, "true").booleanValue());
    EXPECT_FALSE(evaluate(store, "false").booleanValue());
}

TEST(EvalLiterals, NullIsEvaluated) {
    Store store;
    EXPECT_EQ(evaluate(store, "null").kind(), Value::Kind::Null);
}

TEST(EvalLiterals, StringIsEvaluated) {
    Store store;
    EXPECT_EQ(store.string(evaluate(store, "'Вася'")), "Вася");
    EXPECT_EQ(store.string(evaluate(store, "\"Вася\"")), "Вася");
}

TEST(EvalLiterals, StringEscapesAreDecoded) {
    Store store;
    EXPECT_EQ(store.string(evaluate(store, "'a\\nb'")), "a\nb");
}

TEST(EvalNames, GlobalIsRead) {
    Store store;
    put(store, "count", "3");
    EXPECT_EQ(evaluate(store, "count").numberValue(), 3.0);
}

TEST(EvalNames, GlobalHoldingAggregateIsReadByIdentity) {
    Store store;
    put(store, "items", "[1, 2]");
    EXPECT_TRUE(evaluate(store, "items").sameAggregate(store.global("items")));
}

TEST(EvalNames, GlobalHoldingNullIsRead) {
    Store store;
    put(store, "maybe", "null");
    // Корень со значением null существует и читается как null — это не то же
    // самое, что отсутствующая глобальная переменная.
    EXPECT_EQ(evaluate(store, "maybe").kind(), Value::Kind::Null);
}

TEST(EvalMember, ExistingKeyIsRead) {
    Store store;
    put(store, "user", "{'name': 'Вася', 'age': 30}");
    EXPECT_EQ(store.string(evaluate(store, "user.name")), "Вася");
    EXPECT_EQ(evaluate(store, "user.age").numberValue(), 30.0);
}

TEST(EvalMember, MissingKeyReadsAsNull) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    // docs/semantics.md §6.2: отсутствующий ключ читается как null.
    EXPECT_EQ(evaluate(store, "user.nickname").kind(), Value::Kind::Null);
}

TEST(EvalMember, ReadingThroughNullGivesNull) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    // docs/semantics.md §6.3: путь любой глубины безопасен, а опечатка глубже
    // первого сегмента не диагностируется — это цена правила.
    EXPECT_EQ(evaluate(store, "user.prfoile.avatar").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(store, "user.a.b.c.d.e").kind(), Value::Kind::Null);
}

TEST(EvalMember, ReadingKeyOffANonObjectIsAnError) {
    Store store;
    put(store, "count", "3");
    put(store, "name", "'Вася'");
    put(store, "flag", "true");
    put(store, "items", "[1, 2]");
    // docs/semantics.md §6.4: доступ по ключу определён для Object, чтение у
    // null — правилом §6.3, прочее — ошибка.
    EXPECT_EQ(evalError(store, "count.x").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "name.x").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "flag.x").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "items.x").code, CS::ErrorCode::Type);
}

TEST(EvalMember, KeyIsTakenLiterallyNotAsAName) {
    Store store;
    put(store, "o", "{'name': 'ключ'}");
    put(store, "name", "'корень'");
    // docs/semantics.md §6.2: в форме obj.k ключом является имя k буквально, а
    // не значение глобальной переменной, которая случайно называется так же.
    EXPECT_EQ(store.string(evaluate(store, "o.name")), "ключ");
}

TEST(EvalMember, OffsetPointsAtTheFailingNode) {
    Store store;
    put(store, "count", "3");
    // Место ошибки — там, где чинить, а не в начале выражения.
    EXPECT_GT(evalError(store, "count.a.b").offset, 0u);
}

TEST(EvalIndex, ArrayElementIsRead) {
    Store store;
    put(store, "items", "[10, 20, 30]");
    EXPECT_EQ(evaluate(store, "items[0]").numberValue(), 10.0);
    EXPECT_EQ(evaluate(store, "items[2]").numberValue(), 30.0);
}

TEST(EvalIndex, ArrayReadBeyondEndGivesNull) {
    Store store;
    put(store, "items", "[10]");
    // docs/semantics.md §6.1: чтение за границей штатно — данные неполны.
    EXPECT_EQ(evaluate(store, "items[1]").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(store, "items[1000000]").kind(), Value::Kind::Null);
}

TEST(EvalIndex, FractionalAndNegativeIndicesAreErrors) {
    Store store;
    put(store, "items", "[10, 20]");
    put(store, "minusOne", "-1");
    put(store, "huge", std::string(400, '9'));
    // Дробный и отрицательный индекс означают намерение, которого в языке нет:
    // приведения к целому тоже нет. Отрицательное значение и бесконечность
    // берутся из данных — унарный минус это оператор, а операторов в части 1
    // нет; четыреста девяток переполняют double и дают inf.
    EXPECT_EQ(evalError(store, "items[0.5]").code, CS::ErrorCode::Range);
    EXPECT_EQ(evalError(store, "items[minusOne]").code, CS::ErrorCode::Range);
    EXPECT_EQ(evalError(store, "items[huge]").code, CS::ErrorCode::Range);
}

TEST(EvalIndex, NonNumberArrayIndexIsAnError) {
    Store store;
    put(store, "items", "[10, 20]");
    // docs/semantics.md §6.1: приведения к Number нет, поэтому items['0']
    // не работает.
    EXPECT_EQ(evalError(store, "items['0']").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "items[true]").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "items[null]").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, ObjectKeyIsRead) {
    Store store;
    put(store, "o", "{'name': 'Вася'}");
    EXPECT_EQ(store.string(evaluate(store, "o['name']")), "Вася");
    EXPECT_EQ(evaluate(store, "o['missing']").kind(), Value::Kind::Null);
}

TEST(EvalIndex, ScalarKeysAreCoercedToString) {
    Store store;
    put(store, "o", "{'0': 'zero', 'true': 'yes', 'null': 'nothing', '1.5': 'half'}");
    // docs/semantics.md §4.1: ключ объекта — одна из трёх позиций, требующих
    // String, и приведение туда одностороннее.
    EXPECT_EQ(store.string(evaluate(store, "o[0]")), "zero");
    EXPECT_EQ(store.string(evaluate(store, "o[true]")), "yes");
    EXPECT_EQ(store.string(evaluate(store, "o[null]")), "nothing");
    EXPECT_EQ(store.string(evaluate(store, "o[1.5]")), "half");
}

TEST(EvalIndex, NegativeZeroAndZeroAreDifferentKeys) {
    Store store;
    put(store, "o", "{'0': 'plus', '-0': 'minus'}");
    put(store, "minusZero", "-0");
    // docs/semantics.md §4.3: -0 == 0 истинно, но ключи разные, потому что
    // представление числа сохраняет знак нуля. Отрицательный ноль приходит из
    // данных по той же причине, что и в тесте выше.
    EXPECT_EQ(store.string(evaluate(store, "o[0]")), "plus");
    EXPECT_EQ(store.string(evaluate(store, "o[minusZero]")), "minus");
}

TEST(EvalIndex, AggregateKeyIsAnError) {
    Store store;
    put(store, "o", "{'a': 1}");
    put(store, "items", "[1]");
    // Агрегат не приводится никуда (docs/semantics.md §4).
    EXPECT_EQ(evalError(store, "o[items]").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, ReadingThroughNullGivesNull) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    EXPECT_EQ(evaluate(store, "user.missing[0]").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(store, "user.missing['k']").kind(), Value::Kind::Null);
}

TEST(EvalIndex, SubscriptIsEvaluatedEvenWhenTheBaseIsNull) {
    Store store;
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
    EXPECT_EQ(evalError(store, "user.missing[1 + 'a']").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, BaseIsEvaluatedBeforeTheSubscript) {
    Store store;
    // docs/semantics.md §3.3 фиксирует порядок именно ради определённости
    // диагностики, когда ошибочны оба операнда. Обе ошибки — Type через
    // 1 + 'a' (по причине из SubscriptIsEvaluatedEvenWhenTheBaseIsNull выше),
    // и левая обязана выиграть.
    const Diagnostic diag = evalError(store, "(1 + 'a')[2 + 'b']");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_LT(diag.offset, 10u);
}

TEST(EvalIndex, IndexingANonAggregateIsAnError) {
    Store store;
    put(store, "count", "3");
    put(store, "name", "'Вася'");
    put(store, "flag", "true");
    // docs/semantics.md §6.4: 'abc'[0] — ошибка, строка не индексируется.
    EXPECT_EQ(evalError(store, "count[0]").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "name[0]").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "flag[0]").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, ChainedAccessWorks) {
    Store store;
    put(store, "state", "{'items': [{'id': 1}, {'id': 2}]}");
    EXPECT_EQ(evaluate(store, "state.items[1].id").numberValue(), 2.0);
}

TEST(EvalAggregates, ArrayLiteralKeepsOrder) {
    Store store;
    const Value a = evaluate(store, "[1, 2, 3]");
    ASSERT_EQ(store.arrayCount(a), 3u);
    EXPECT_EQ(store.arrayAt(a, 0).numberValue(), 1.0);
    EXPECT_EQ(store.arrayAt(a, 2).numberValue(), 3.0);
}

TEST(EvalAggregates, ObjectLiteralStoresPairs) {
    Store store;
    const Value o = evaluate(store, "{'a': 1, 'b': 2}");
    ASSERT_EQ(store.objectCount(o), 2u);
    EXPECT_EQ(store.objectGet(o, "a").numberValue(), 1.0);
    EXPECT_EQ(store.objectGet(o, "b").numberValue(), 2.0);
}

TEST(EvalAggregates, EmptyLiterals) {
    Store store;
    EXPECT_EQ(store.arrayCount(evaluate(store, "[]")), 0u);
    EXPECT_EQ(store.objectCount(evaluate(store, "{}")), 0u);
}

TEST(EvalAggregates, ElementsAreArbitraryExpressions) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    put(store, "items", "[7]");
    // Вот чем агрегат в выражении отличается от агрегата в данных: элемент —
    // выражение, а не литерал.
    const Value a = evaluate(store, "[user.name, items[0], user.missing]");
    ASSERT_EQ(store.arrayCount(a), 3u);
    EXPECT_EQ(store.string(store.arrayAt(a, 0)), "Вася");
    EXPECT_EQ(store.arrayAt(a, 1).numberValue(), 7.0);
    EXPECT_EQ(store.arrayAt(a, 2).kind(), Value::Kind::Null);
}

TEST(EvalAggregates, ObjectValuesAreExpressionsAndKeysAreLiterals) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    const Value o = evaluate(store, "{'who': user.name}");
    EXPECT_EQ(store.string(store.objectGet(o, "who")), "Вася");
}

TEST(EvalAggregates, ErrorInsideAnElementStopsAtTheFirstFailure) {
    Store store;
    // Два сбойных элемента: диагностика обязана указать на первый, иначе
    // «первая ошибка выигрывает» держится на честном слове. В частях 2 и 3 это
    // правило станет несущим для && и ??.
    //
    // Обе ошибки — Type через 1 + 'a', не неизвестное имя: статический проход
    // (core/src/check.hpp) отсеял бы оба элемента разом, ещё до вычисления, и
    // «первый выигрывает» стало бы непроверяемым.
    const Diagnostic diag = evalError(store, "[1 + 'a', 2 + 'b']");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_LT(diag.offset, 10u);
}

TEST(EvalDepth, ChainAtTheParserLimitEvaluatesWithoutOverflow) {
    Store store;
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
    EXPECT_EQ(evaluate(store, source).kind(), Value::Kind::Null);

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
    EXPECT_EQ(evaluate(store, source).numberValue(), 510.0);

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
    ASSERT_TRUE(CS::evalExpression(ast, text, store, &first, diag));
    ASSERT_TRUE(CS::evalExpression(ast, text, store, &second, diag));

    // docs/semantics.md §2.3: литерал создаёт новый агрегат при каждом
    // вычислении. Без этого теста правило держится на честном слове.
    EXPECT_FALSE(first.sameAggregate(second));
    EXPECT_EQ(store.arrayCount(first), 2u);
    EXPECT_EQ(store.arrayCount(second), 2u);
}

TEST(EvalOperators, UnaryWorksThroughTheWalk) {
    Store store;
    EXPECT_FALSE(evaluate(store, "!true").booleanValue());
    EXPECT_EQ(evaluate(store, "-3").numberValue(), -3.0);
}

TEST(EvalOperators, ArithmeticRespectsPrecedence) {
    Store store;
    // Приоритет — дело грамматики; вычислитель лишь обходит построенное дерево.
    EXPECT_EQ(evaluate(store, "1 + 2 * 3").numberValue(), 7.0);
    EXPECT_EQ(evaluate(store, "(1 + 2) * 3").numberValue(), 9.0);
}

TEST(EvalOperators, ComparisonWorksThroughTheWalk) {
    Store store;
    EXPECT_TRUE(evaluate(store, "1 < 2").booleanValue());
    EXPECT_FALSE(evaluate(store, "1 > 2").booleanValue());
}

TEST(EvalOperators, EqualityWorksThroughTheWalk) {
    Store store;
    EXPECT_TRUE(evaluate(store, "1 == 1").booleanValue());
    EXPECT_TRUE(evaluate(store, "1 != 2").booleanValue());
    EXPECT_TRUE(evaluate(store, "null == null").booleanValue());
}

TEST(EvalOperators, OperandsComeFromTheStore) {
    Store store;
    put(store, "state", "{'count': 41}");
    EXPECT_EQ(evaluate(store, "state.count + 1").numberValue(), 42.0);
}

TEST(EvalOperators, ErrorInTheLeftOperandStopsEvaluation) {
    Store store;
    put(store, "items", "[1]");
    // Ошибка — Range через items[-1], не неизвестное имя: статический проход
    // (core/src/check.hpp) отсеял бы неизвестное имя ещё до вычисления, и
    // пробой «дошли ли мы сюда вычислением» оно уже быть не может. Range —
    // код, которого сам '+' не порождает, поэтому от штатного результата
    // отличим по коду, а не только по факту отказа.
    EXPECT_EQ(evalError(store, "items[-1] + 1").code, CS::ErrorCode::Range);
}

TEST(EvalOperators, ErrorInTheRightOperandStopsEvaluation) {
    Store store;
    put(store, "items", "[1]");
    EXPECT_EQ(evalError(store, "1 + items[-1]").code, CS::ErrorCode::Range);
}

TEST(EvalOperators, LeftOperandIsEvaluatedBeforeTheRight) {
    Store store;
    // docs/semantics.md §3.3 фиксирует порядок именно ради определённости
    // диагностики, когда ошибочны оба операнда. С одним ошибочным операндом
    // порядок ненаблюдаем, и перестановка прошла бы незамеченной.
    const Diagnostic diag = evalError(store, "(1 + 'a') + (2 + 'b')");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_LT(diag.offset, 10u);
}

TEST(EvalOperators, AggregateEqualityIsByIdentityThroughTheWalk) {
    Store store;
    put(store, "items", "[1, 2]");
    // Литерал создаёт новый агрегат при каждом вычислении, поэтому сравнение
    // с ним ложно даже при совпадающем содержимом.
    EXPECT_TRUE(evaluate(store, "items == items").booleanValue());
    EXPECT_FALSE(evaluate(store, "items == [1, 2]").booleanValue());
}

TEST(EvalShortCircuit, AndDoesNotEvaluateTheRightOperand) {
    Store store;
    // Побочных эффектов в выражениях нет, поэтому невычисление наблюдается
    // единственным способом: ошибка справа не всплывает.
    //
    // Проба — Type через 1 + 'a', не неизвестное имя: статический проход
    // (core/src/check.hpp) отсеял бы неизвестное имя ещё до вычисления,
    // независимо от того, короткое замыкание его достигает или нет, и такой
    // пробой служить больше не может.
    EXPECT_FALSE(evaluate(store, "false && (1 + 'a')").booleanValue());
}

TEST(EvalShortCircuit, AndEvaluatesTheRightOperandWhenNeeded) {
    Store store;
    put(store, "items", "[1]");
    EXPECT_FALSE(evaluate(store, "true && false").booleanValue());
    EXPECT_TRUE(evaluate(store, "true && true").booleanValue());
    // Range через items[-1]: код, которого && сам не порождает.
    EXPECT_EQ(evalError(store, "true && items[-1]").code, CS::ErrorCode::Range);
}

TEST(EvalShortCircuit, OrDoesNotEvaluateTheRightOperand) {
    Store store;
    EXPECT_TRUE(evaluate(store, "true || (1 + 'a')").booleanValue());
}

TEST(EvalShortCircuit, OrEvaluatesTheRightOperandWhenNeeded) {
    Store store;
    put(store, "items", "[1]");
    EXPECT_TRUE(evaluate(store, "false || true").booleanValue());
    EXPECT_FALSE(evaluate(store, "false || false").booleanValue());
    // Range через items[-1]: код, которого || сам не порождает.
    EXPECT_EQ(evalError(store, "false || items[-1]").code, CS::ErrorCode::Range);
}

TEST(EvalShortCircuit, ErrorOnTheLeftIsNotSwallowed) {
    Store store;
    put(store, "items", "[1]");
    // docs/semantics.md §5.5: ошибка && false — ошибка. Левый операнд обязан
    // быть булевым по грамматике оператора, поэтому пробу берём такую, что
    // сама по себе даёт Range ещё до проверки булевости (items[-1]): код,
    // которого ни +, ни && / || не порождают, и коллизии с их штатной
    // ошибкой типа не возникает.
    EXPECT_EQ(evalError(store, "items[-1] && false").code, CS::ErrorCode::Range);
    EXPECT_EQ(evalError(store, "items[-1] || true").code, CS::ErrorCode::Range);
}

TEST(EvalShortCircuit, TypeOfTheUnevaluatedOperandIsNotChecked) {
    Store store;
    // Самая точная проверка правила: тип правого операнда проверяется тогда и
    // только тогда, когда его пришлось вычислить. Поодиночке ни одна из двух
    // строк ничего не доказывает.
    EXPECT_FALSE(evaluate(store, "false && 5").booleanValue());
    EXPECT_EQ(evalError(store, "true && 5").code, CS::ErrorCode::Type);
    // Зеркало для ||: у него замыкает истина, а не ложь.
    EXPECT_TRUE(evaluate(store, "true || 5").booleanValue());
    EXPECT_EQ(evalError(store, "false || 5").code, CS::ErrorCode::Type);
}

TEST(EvalShortCircuit, LogicalOperatorsRequireBooleanOnTheLeft) {
    Store store;
    EXPECT_EQ(evalError(store, "1 && true").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "'a' || true").code, CS::ErrorCode::Type);
}

TEST(EvalShortCircuit, GuardIdiomProtectsTheRightSide) {
    Store store;
    put(store, "state", "{'items': []}");
    // Ради этого короткое замыкание и существует. Правая часть без защиты
    // слева даёт ошибку типа: строковый индекс массива запрещён. Настоящая
    // идиома из §5.5 пользуется count(), который придёт с частью 3, — здесь
    // та же форма на доступных средствах.
    //
    // Обе строки обязательны: одна показывает, что справа не пошли, вторая —
    // что там действительно есть на что наткнуться.
    EXPECT_FALSE(evaluate(store, "false && state.items['0'] == 1").booleanValue());
    EXPECT_EQ(evalError(store, "true && state.items['0'] == 1").code,
              CS::ErrorCode::Type);
}

TEST(EvalNilCoalesce, TakesTheLeftWhenItIsNotNull) {
    Store store;
    // Проба справа — Type через 1 + 'a' (см. DoesNotSwallowErrors ниже,
    // где этот приём уже применён): она обязана не всплыть, если левый не
    // null.
    EXPECT_EQ(evaluate(store, "1 ?? (1 + 'a')").numberValue(), 1.0);
}

TEST(EvalNilCoalesce, TakesTheRightWhenTheLeftIsNull) {
    Store store;
    EXPECT_EQ(evaluate(store, "null ?? 2").numberValue(), 2.0);
}

TEST(EvalNilCoalesce, DoesNotSwallowErrors) {
    Store store;
    // docs/semantics.md §5.6: ?? перехватывает только null. Соблазнительно
    // принять его за «если что-то пойдёт не так, подставь запасное»; он делает
    // не это.
    EXPECT_EQ(evalError(store, "(1 + 'a') ?? 0").code, CS::ErrorCode::Type);
    // И справа тоже: если левый null, правый вычисляется по-настоящему.
    EXPECT_EQ(evalError(store, "null ?? (2 + 'b')").code, CS::ErrorCode::Type);
}

TEST(EvalNilCoalesce, OperandTypesNeedNotMatch) {
    Store store;
    EXPECT_EQ(store.string(evaluate(store, "null ?? 'запасное'")), "запасное");
}

TEST(EvalNilCoalesce, ChainsRightAssociatively) {
    Store store;
    put(store, "user", "{'nickname': null}");
    EXPECT_EQ(store.string(evaluate(store, "user.nickname ?? user.name ?? 'Гость'")),
              "Гость");
}

TEST(EvalTernary, EvaluatesOnlyTheSelectedBranch) {
    Store store;
    // Проба в невыбранной ветке — Type через 1 + 'a', не неизвестное имя: она
    // обязана не всплыть, что доказывает невычисление.
    EXPECT_EQ(evaluate(store, "true ? 1 : (1 + 'a')").numberValue(), 1.0);
    EXPECT_EQ(evaluate(store, "false ? (1 + 'a') : 2").numberValue(), 2.0);
}

TEST(EvalTernary, ConditionMustBeBoolean) {
    Store store;
    EXPECT_EQ(evalError(store, "1 ? 1 : 2").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "null ? 1 : 2").code, CS::ErrorCode::Type);
}

TEST(EvalTernary, BranchesNeedNotShareAType) {
    Store store;
    EXPECT_EQ(evaluate(store, "true ? 1 : 'a'").numberValue(), 1.0);
    EXPECT_EQ(store.string(evaluate(store, "false ? 1 : 'a'")), "a");
}

/// Разбирает и выполняет скрипт; требует успеха обоих шагов.
void run(Store &store, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors = CS::compileScript(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, store, &diag, 1);
    ASSERT_EQ(errors, 0u) << diag.message;
    ASSERT_TRUE(CS::runScript(ast, text, store, diag)) << diag.message;
}

/// Разбирает успешно, выполняет с отказом; возвращает диагностику выполнения.
Diagnostic runError(Store &store, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors = CS::compileScript(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, store, &diag, 1);
    if (errors != 0) {
        ADD_FAILURE() << diag.message;
        return diag;
    }
    EXPECT_FALSE(CS::runScript(ast, text, store, diag));
    return diag;
}

TEST(EvalAssign, ExistingKeyIsReplaced) {
    Store store;
    put(store, "state", "{'count': 1}");
    run(store, "state.count = 42;");
    EXPECT_EQ(evaluate(store, "state.count").numberValue(), 42.0);
}

TEST(EvalAssign, MissingKeyIsCreated) {
    Store store;
    put(store, "state", "{}");
    // docs/semantics.md §6.2: запись создаёт ключ, если его нет.
    run(store, "state.fresh = 'значение';");
    EXPECT_EQ(store.string(evaluate(store, "state.fresh")), "значение");
}

TEST(EvalAssign, ValueMayBeAnyExpression) {
    Store store;
    put(store, "state", "{'a': 2, 'b': 3}");
    run(store, "state.sum = state.a * state.b + 1;");
    EXPECT_EQ(evaluate(store, "state.sum").numberValue(), 7.0);
}

TEST(EvalAssign, DeepPathIsWritable) {
    Store store;
    put(store, "user", "{'profile': {'city': {}}}");
    run(store, "user.profile.city.name = 'Москва';");
    EXPECT_EQ(store.string(evaluate(store, "user.profile.city.name")), "Москва");
}

TEST(EvalAssign, WritingIntoNullIsAnError) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    // docs/semantics.md §7.2: мягкость §6.3 распространяется только на чтение.
    // Обе половины обязательны: без второй правило вырождается.
    EXPECT_EQ(evaluate(store, "user.profile.name").kind(), Value::Kind::Null);
    EXPECT_EQ(runError(store, "user.profile.name = 'Вася';").code,
              CS::ErrorCode::Type);
}

TEST(EvalAssign, WritingAKeyOffANonObjectIsAnError) {
    Store store;
    put(store, "count", "3");
    put(store, "items", "[1]");
    EXPECT_EQ(runError(store, "count.x = 1;").code, CS::ErrorCode::Type);
    EXPECT_EQ(runError(store, "items.x = 1;").code, CS::ErrorCode::Type);
}

// AssigningToANameIsAnError и UnknownNameIsAnError переехали в
// core/tests/check_test.cpp (Check.AssigningToANameIsACompileError,
// Check.UnknownNameInAssignmentTargetIsACompileError): "state = 1;" и
// "usre.a = 1;" отсеиваются статическим проходом ещё до вычисления, и
// runError() до них больше не доходит.

TEST(EvalAssign, ErrorInTheValueLeavesTheTargetUntouched) {
    Store store;
    put(store, "state", "{'a': 1}");
    // Ошибка — Type через 1 + 'a', не неизвестное имя: с приходом
    // статического прохода последнее ловится ещё на компиляции, а здесь
    // важно именно поведение вычислителя при рантайм-ошибке справа.
    EXPECT_EQ(runError(store, "state.a = 1 + 'a';").code, CS::ErrorCode::Type);
    EXPECT_EQ(evaluate(store, "state.a").numberValue(), 1.0);
}

TEST(EvalAssign, TargetCheckLosesToValueErrorWhenBothFail) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    put(store, "items", "[1, 2, 3]");
    put(store, "minusOne", "-1");
    // docs/semantics.md §7.2: цель проверяется после вычисления правой части.
    // Запись в null сама по себе даёт Type (WritingIntoNullIsAnError), но
    // здесь неисправна и правая часть тоже — чтение по отрицательному индексу
    // даёт Range (FractionalAndNegativeIndicesAreErrors), и она вычисляется
    // раньше — побеждает Range.
    EXPECT_EQ(runError(store, "user.profile.name = items[minusOne];").code,
              CS::ErrorCode::Range);
}

TEST(EvalScript, EmptyScriptSucceeds) {
    Store store;
    run(store, "");
    run(store, ";;;");
}

TEST(EvalAssignIndex, ArrayElementIsReplaced) {
    Store store;
    put(store, "items", "[10, 20, 30]");
    run(store, "items[1] = 99;");
    EXPECT_EQ(evaluate(store, "items[1]").numberValue(), 99.0);
    EXPECT_EQ(evaluate(store, "items[0]").numberValue(), 10.0);
}

TEST(EvalAssignIndex, WritingBeyondTheEndIsAnError) {
    Store store;
    put(store, "items", "[10]");
    // docs/semantics.md §6.1: чтение за границей штатно, запись за границу —
    // намерение создать элемент, для чего существует push. Обе половины
    // обязательны.
    EXPECT_EQ(evaluate(store, "items[1]").kind(), Value::Kind::Null);
    EXPECT_EQ(runError(store, "items[1] = 1;").code, CS::ErrorCode::Range);
    EXPECT_EQ(runError(store, "items[1000000] = 1;").code, CS::ErrorCode::Range);
    // 2^32: приведение к uint32_t усекло бы индекс в ноль, попав в границы.
    EXPECT_EQ(runError(store, "items[4294967296] = 1;").code, CS::ErrorCode::Range);
}

TEST(EvalAssignIndex, FractionalAndNegativeIndicesAreErrors) {
    Store store;
    put(store, "items", "[10, 20]");
    put(store, "minusOne", "-1");
    EXPECT_EQ(runError(store, "items[0.5] = 1;").code, CS::ErrorCode::Range);
    EXPECT_EQ(runError(store, "items[minusOne] = 1;").code, CS::ErrorCode::Range);
}

TEST(EvalAssignIndex, NonNumberArrayIndexIsAnError) {
    Store store;
    put(store, "items", "[10, 20]");
    EXPECT_EQ(runError(store, "items['0'] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, ObjectKeyIsWritten) {
    Store store;
    put(store, "o", "{'a': 1}");
    run(store, "o['a'] = 2;");
    run(store, "o['fresh'] = 3;");
    EXPECT_EQ(evaluate(store, "o.a").numberValue(), 2.0);
    EXPECT_EQ(evaluate(store, "o.fresh").numberValue(), 3.0);
}

TEST(EvalAssignIndex, ScalarKeysAreCoercedToString) {
    Store store;
    put(store, "o", "{}");
    // docs/semantics.md §4.1: ключ объекта — одна из трёх позиций, требующих
    // String; правила приведения те же, что при чтении.
    run(store, "o[0] = 'ноль';");
    run(store, "o[true] = 'да';");
    run(store, "o[null] = 'ничего';");
    EXPECT_EQ(store.string(evaluate(store, "o['0']")), "ноль");
    EXPECT_EQ(store.string(evaluate(store, "o['true']")), "да");
    EXPECT_EQ(store.string(evaluate(store, "o['null']")), "ничего");
}

TEST(EvalAssignIndex, AggregateKeyIsAnError) {
    Store store;
    put(store, "o", "{}");
    put(store, "items", "[1]");
    EXPECT_EQ(runError(store, "o[items] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, WritingIntoNullIsAnError) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    EXPECT_EQ(evaluate(store, "user.missing[0]").kind(), Value::Kind::Null);
    EXPECT_EQ(runError(store, "user.missing[0] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, WritingIntoANonAggregateIsAnError) {
    Store store;
    put(store, "count", "3");
    put(store, "name", "'Вася'");
    EXPECT_EQ(runError(store, "count[0] = 1;").code, CS::ErrorCode::Type);
    EXPECT_EQ(runError(store, "name[0] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, ChainedTargetWorks) {
    Store store;
    put(store, "state", "{'rows': [{'cells': [1, 2]}]}");
    run(store, "state.rows[0].cells[1] = 99;");
    EXPECT_EQ(evaluate(store, "state.rows[0].cells[1]").numberValue(), 99.0);
}

TEST(EvalAssignIndex, SubscriptMayBeAnExpression) {
    Store store;
    put(store, "items", "[10, 20, 30]");
    put(store, "i", "1");
    run(store, "items[i + 1] = 99;");
    EXPECT_EQ(evaluate(store, "items[2]").numberValue(), 99.0);
}

TEST(EvalCompound, FourOperatorsWorkOnAKey) {
    Store store;
    put(store, "s", "{'n': 10}");
    run(store, "s.n += 5;");
    EXPECT_EQ(evaluate(store, "s.n").numberValue(), 15.0);
    run(store, "s.n -= 3;");
    EXPECT_EQ(evaluate(store, "s.n").numberValue(), 12.0);
    run(store, "s.n *= 2;");
    EXPECT_EQ(evaluate(store, "s.n").numberValue(), 24.0);
    run(store, "s.n /= 4;");
    EXPECT_EQ(evaluate(store, "s.n").numberValue(), 6.0);
}

TEST(EvalCompound, WorksOnAnArrayElement) {
    Store store;
    put(store, "items", "[1, 2, 3]");
    run(store, "items[1] += 10;");
    EXPECT_EQ(evaluate(store, "items[1]").numberValue(), 12.0);
}

TEST(EvalCompound, WorksOnAnObjectKeyByIndex) {
    Store store;
    put(store, "o", "{'a': 1}");
    run(store, "o['a'] += 1;");
    EXPECT_EQ(evaluate(store, "o.a").numberValue(), 2.0);
}

TEST(EvalCompound, TypeMismatchIsAnError) {
    Store store;
    put(store, "s", "{'text': 'а'}");
    // Операция берётся из applyBinary, поэтому правила типов те же, что у
    // обычного оператора: конкатенации строк через + нет.
    EXPECT_EQ(runError(store, "s.text += 'б';").code, CS::ErrorCode::Type);
}

TEST(EvalCompound, MissingKeyReadsAsNullAndThenFails) {
    Store store;
    put(store, "s", "{}");
    // Чтение отсутствующего ключа даёт null (§6.2), а null + 1 — ошибка типа.
    EXPECT_EQ(runError(store, "s.missing += 1;").code, CS::ErrorCode::Type);
}

TEST(EvalCompound, BeyondTheEndGivesTypeNotRange) {
    Store store;
    put(store, "items", "[1, 2]");
    // docs/semantics.md §7.3: x += e есть x = x + e. Сначала читается items[5],
    // что штатно даёт null, затем вычисляется null + 1 — ошибка типа. До
    // проверки границы записи дело не доходит, поэтому Type, а не Range.
    // Простое присваивание туда же даёт Range — обе строки обязательны.
    EXPECT_EQ(runError(store, "items[5] += 1;").code, CS::ErrorCode::Type);
    EXPECT_EQ(runError(store, "items[5] = 1;").code, CS::ErrorCode::Range);
}

TEST(EvalCompound, ErrorLeavesTheTargetUntouched) {
    Store store;
    put(store, "s", "{'n': 10}");
    EXPECT_EQ(runError(store, "s.n += 'а';").code, CS::ErrorCode::Type);
    EXPECT_EQ(evaluate(store, "s.n").numberValue(), 10.0);
}

TEST(EvalCompound, TargetCheckLosesToValueErrorWhenBothFail) {
    Store store;
    put(store, "items", "[1, 2, 3]");
    // §7.3 говорит про результат ('x += e' есть 'x = x + e'), а не про
    // порядок проверок: цель проверяется после вычисления правой части
    // (docs/semantics.md §7.2). Индекс -1 сам по себе дал бы Range
    // (FractionalAndNegativeIndicesAreErrors), но правая часть неисправна —
    // Type через 1 + 'a' — и она побеждает первой.
    EXPECT_EQ(runError(store, "items[-1] += 1 + 'a';").code, CS::ErrorCode::Type);
}

TEST(EvalCompound, DivisionByZeroFollowsIEEE) {
    Store store;
    put(store, "s", "{'n': 1}");
    // Деление на ноль даёт бесконечность, а не ошибку (§5.2).
    run(store, "s.n /= 0;");
    EXPECT_TRUE(std::isinf(evaluate(store, "s.n").numberValue()));
}

TEST(EvalCompound, DeepTargetWorks) {
    Store store;
    put(store, "state", "{'rows': [{'n': 1}]}");
    put(store, "i", "0");
    // Однократность вычисления цели (docs/grammar.md §6.4) в этом языке
    // ненаблюдаема: выражения чисты, поэтому повторное вычисление дало бы тот
    // же результат. Тест проверяет лишь, что сложная цель вообще работает;
    // само требование держится устройством кода, а не этой проверкой.
    run(store, "state.rows[i].n += 41;");
    EXPECT_EQ(evaluate(store, "state.rows[0].n").numberValue(), 42.0);
}

TEST(EvalScriptBehaviour, StatementsApplyInOrder) {
    Store store;
    put(store, "s", "{'n': 0}");
    // Каждый стейтмент читает результат предыдущего, поэтому 13 получается
    // только если применились все три и именно в этом порядке: пропуск первого
    // даёт 3, второго — 4, третьего — 10, перестановка — иное число.
    run(store, "s.n = s.n + 1; s.n = s.n * 10; s.n = s.n + 3;");
    EXPECT_EQ(evaluate(store, "s.n").numberValue(), 13.0);
}

TEST(EvalScriptBehaviour, LaterStatementsSeeEarlierWrites) {
    Store store;
    put(store, "s", "{'a': 1}");
    run(store, "s.b = s.a + 1; s.c = s.b + 1;");
    EXPECT_EQ(evaluate(store, "s.c").numberValue(), 3.0);
}

TEST(EvalScriptBehaviour, ErrorStopsTheScriptAndKeepsWhatWasDone) {
    Store store;
    put(store, "s", "{'a': 0, 'b': 0, 'c': 0}");
    // docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md: откатывать
    // нечего, предыдущих состояний хранилище не держит. Обработчик, упавший на
    // третьем присваивании из пяти, оставит первые два применёнными.
    //
    // Ошибка — Type через 1 + 'a', не неизвестное имя: статический проход
    // отсеял бы весь скрипт ещё до первого стейтмента, и «первые два
    // применились» стало бы неверно уже по другой причине.
    const Diagnostic diag =
        runError(store, "s.a = 1; s.b = 2; s.x = 1 + 'a'; s.c = 3;");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_EQ(evaluate(store, "s.a").numberValue(), 1.0);
    EXPECT_EQ(evaluate(store, "s.b").numberValue(), 2.0);
    EXPECT_EQ(evaluate(store, "s.c").numberValue(), 0.0);
    EXPECT_FALSE(store.objectHas(store.global("s"), "x"));
}

TEST(EvalScriptBehaviour, MutationIsVisibleThroughAnotherName) {
    Store store;
    // Хост кладёт один агрегат под двумя именами: значения — хендлы, поэтому
    // это тот же массив (docs/semantics.md §2.3).
    put(store, "state", "{'items': [1, 2]}");
    const Value items = store.objectGet(store.global("state"), "items");
    store.setGlobal("shortcut", items);

    run(store, "state.items[0] = 99;");
    EXPECT_EQ(evaluate(store, "shortcut[0]").numberValue(), 99.0);
}

TEST(EvalScriptBehaviour, AssignmentCreatesAnAliasJustLikeTheHostDoes) {
    Store store;
    // §2.3, первое предложение: «присваивание... копии не создаёт». Здесь, в
    // отличие от MutationIsVisibleThroughAnotherName выше, второе имя для
    // массива ставит не хост, а само присваивание скрипта.
    put(store, "state", "{'a': [1, 2], 'b': null}");
    run(store, "state.b = state.a; state.a[0] = 9;");
    EXPECT_EQ(evaluate(store, "state.b[0]").numberValue(), 9.0);
}

TEST(EvalScriptBehaviour, SelfReferenceIsAValidScript) {
    Store store;
    // §2.3: 'obj[\'self\'] = obj;' — корректная программа, ссылочность
    // допускает циклы. Запись не обходит значение и потому не зацикливается;
    // повторное чтение через self подтверждает, что цикл остался невредимым.
    put(store, "obj", "{}");
    // 'self' — зарезервированное слово (docs/grammar.md §4.5), поэтому чтение
    // назад идёт через '[]', как и запись; через '.' этот ключ недостижим.
    run(store, "obj['self'] = obj;");
    EXPECT_TRUE(evaluate(store, "obj['self'] == obj").booleanValue());
    EXPECT_TRUE(evaluate(store, "obj['self']['self']['self'] == obj").booleanValue());
}

TEST(EvalScriptBehaviour, EmptyStatementsAreSkipped) {
    Store store;
    put(store, "s", "{'n': 0}");
    run(store, ";; s.n = 1 ;;");
    EXPECT_EQ(evaluate(store, "s.n").numberValue(), 1.0);
}

TEST(EvalScriptBehaviour, DeepPathInsideAScript) {
    Store store;
    put(store, "state", "{'rows': [{'cells': [0]}, {'cells': [0]}]}");
    run(store, "state.rows[0].cells[0] = 1; state.rows[1].cells[0] = 2;");
    EXPECT_EQ(evaluate(store, "state.rows[0].cells[0]").numberValue(), 1.0);
    EXPECT_EQ(evaluate(store, "state.rows[1].cells[0]").numberValue(), 2.0);
}

TEST(EvalCall, CountOfEachKind) {
    Store store;
    put(store, "items", "[10, 20, 30]");
    put(store, "o", "{'a': 1, 'b': 2}");
    put(store, "empty", "[]");
    EXPECT_EQ(evaluate(store, "count(items)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(store, "count(o)").numberValue(), 2.0);
    EXPECT_EQ(evaluate(store, "count(empty)").numberValue(), 0.0);
}

TEST(EvalCall, CountOfStringCountsBytesNotCharacters) {
    Store store;
    // docs/semantics.md §8.1 явно: байты, а не символы.
    EXPECT_EQ(evaluate(store, "count('привет')").numberValue(), 12.0);
    EXPECT_EQ(evaluate(store, "count('😀')").numberValue(), 4.0);
    EXPECT_EQ(evaluate(store, "count('abc')").numberValue(), 3.0);
    EXPECT_EQ(evaluate(store, "count('')").numberValue(), 0.0);
}

TEST(EvalCall, CountRejectsScalarsOtherThanString) {
    Store store;
    EXPECT_EQ(evalError(store, "count(1)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "count(true)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "count(null)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, KeysReturnsEveryKey) {
    Store store;
    put(store, "o", "{'b': 1, 'a': 2, 'c': 3}");
    const Value keys = evaluate(store, "keys(o)");
    ASSERT_EQ(keys.kind(), Value::Kind::Array);
    ASSERT_EQ(store.arrayCount(keys), 3u);
    // Порядок docs/semantics.md §8.2 не определяет, поэтому тест собирает
    // множество, а не список: опираться на порядок значило бы обещать его.
    std::set<std::string> got;
    for (std::uint32_t i = 0; i < 3; ++i) {
        got.insert(std::string(store.string(store.arrayAt(keys, i))));
    }
    EXPECT_EQ(got, (std::set<std::string>{"a", "b", "c"}));
}

TEST(EvalCall, KeysOfEmptyObjectIsEmptyArray) {
    Store store;
    put(store, "o", "{}");
    EXPECT_EQ(store.arrayCount(evaluate(store, "keys(o)")), 0u);
}

TEST(EvalCall, KeysRejectsNonObjects) {
    Store store;
    put(store, "items", "[1]");
    EXPECT_EQ(evalError(store, "keys(items)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "keys('a')").code, CS::ErrorCode::Type);
}

TEST(EvalCall, HasDistinguishesAbsentFromNull) {
    Store store;
    put(store, "o", "{'present': 1, 'empty': null}");
    // docs/semantics.md §8.3: единственный способ их различить. Обе половины
    // обязательны, иначе правило вырождается.
    EXPECT_TRUE(evaluate(store, "has(o, 'present')").booleanValue());
    EXPECT_TRUE(evaluate(store, "has(o, 'empty')").booleanValue());
    EXPECT_FALSE(evaluate(store, "has(o, 'missing')").booleanValue());
    EXPECT_EQ(evaluate(store, "o.empty").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(store, "o.missing").kind(), Value::Kind::Null);
}

TEST(EvalCall, HasCoercesTheKey) {
    Store store;
    put(store, "o", "{'0': 'ноль', 'true': 'да'}");
    EXPECT_TRUE(evaluate(store, "has(o, 0)").booleanValue());
    EXPECT_TRUE(evaluate(store, "has(o, true)").booleanValue());
    EXPECT_EQ(evalError(store, "has(o, [1])").code, CS::ErrorCode::Type);
}

TEST(EvalCall, LastOfArrayAndOfEmpty) {
    Store store;
    put(store, "items", "[10, 20, 30]");
    put(store, "empty", "[]");
    EXPECT_EQ(evaluate(store, "last(items)").numberValue(), 30.0);
    // docs/semantics.md §8.4: на пустом — null, тогда как items[count-1] дал бы
    // ошибку. Обе половины в одном тесте.
    EXPECT_EQ(evaluate(store, "last(empty)").kind(), Value::Kind::Null);
    EXPECT_EQ(evalError(store, "empty[count(empty) - 1]").code,
              CS::ErrorCode::Range);
}

TEST(EvalCall, LastRejectsNonArrays) {
    Store store;
    put(store, "o", "{}");
    EXPECT_EQ(evalError(store, "last(o)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, NestedCallsWork) {
    Store store;
    put(store, "o", "{'a': 1, 'b': 2}");
    EXPECT_EQ(evaluate(store, "count(keys(o))").numberValue(), 2.0);
}

TEST(EvalCall, ArgumentsAreEvaluatedLeftToRight) {
    Store store;
    put(store, "items", "[1]");
    // Оба аргумента негодны, но по-разному: код ошибки называет, который из
    // них вычислялся первым.
    EXPECT_EQ(evalError(store, "has(items[-1], 2 + 'b')").code,
              CS::ErrorCode::Range);
}

TEST(EvalCall, VariadicFormatDoesNotOverflowTheArgumentBuffer) {
    Store store;
    // Буфер аргументов в ветке Call рассчитан на два: format обязан идти мимо
    // него. Пять аргументов затёрли бы стек, попади они туда.
    EXPECT_EQ(store.string(evaluate(store, "format('${}${}${}${}${}', 1, 2, 3, 4, 5)")),
              "12345");
}

TEST(EvalCall, PushAppends) {
    Store store;
    put(store, "items", "[1, 2]");
    run(store, "push(items, 3);");
    EXPECT_EQ(evaluate(store, "count(items)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(store, "items[2]").numberValue(), 3.0);
}

TEST(EvalCall, PushIsTheOnlyWayToGrowAnArray) {
    Store store;
    put(store, "items", "[1]");
    // docs/semantics.md §6.1: запись за границу — ошибка, расширяет только push.
    EXPECT_EQ(runError(store, "items[1] = 2;").code, CS::ErrorCode::Range);
    run(store, "push(items, 2);");
    EXPECT_EQ(evaluate(store, "items[1]").numberValue(), 2.0);
}

TEST(EvalCall, PushRejectsNonArrays) {
    Store store;
    put(store, "o", "{}");
    EXPECT_EQ(runError(store, "push(o, 1);").code, CS::ErrorCode::Type);
}

TEST(EvalCall, PopRemovesAndDoesNothingOnEmpty) {
    Store store;
    put(store, "items", "[1, 2]");
    put(store, "empty", "[]");
    run(store, "pop(items);");
    EXPECT_EQ(evaluate(store, "count(items)").numberValue(), 1.0);
    // docs/semantics.md §8.6: на пустом ничего не делает и не отказывает.
    run(store, "pop(empty);");
    EXPECT_EQ(evaluate(store, "count(empty)").numberValue(), 0.0);
}

TEST(EvalCall, TakingTheRemovedElementNeedsTwoSteps) {
    Store store;
    put(store, "state", "{'items': [1, 2, 3], 'taken': null}");
    // docs/semantics.md §8.6 показывает ровно эту пару: pop значения не
    // возвращает, читают его через last.
    run(store, "state.taken = last(state.items); pop(state.items);");
    EXPECT_EQ(evaluate(store, "state.taken").numberValue(), 3.0);
    EXPECT_EQ(evaluate(store, "count(state.items)").numberValue(), 2.0);
}

TEST(EvalCall, PushAndPopAreVisibleThroughAnAlias) {
    Store store;
    put(store, "state", "{'items': [1]}");
    const Value items = store.objectGet(store.global("state"), "items");
    store.setGlobal("shortcut", items);
    // docs/semantics.md §2.3: мутация через один путь видна через второй.
    run(store, "push(state.items, 2);");
    EXPECT_EQ(evaluate(store, "count(shortcut)").numberValue(), 2.0);
}

TEST(EvalCall, StrConvertsScalars) {
    Store store;
    // docs/semantics.md §8.7 — правила §4.2 и §4.3.
    EXPECT_EQ(store.string(evaluate(store, "str(1)")), "1");
    EXPECT_EQ(store.string(evaluate(store, "str(0.5)")), "0.5");
    EXPECT_EQ(store.string(evaluate(store, "str(true)")), "true");
    EXPECT_EQ(store.string(evaluate(store, "str(false)")), "false");
    EXPECT_EQ(store.string(evaluate(store, "str(null)")), "null");
    EXPECT_EQ(store.string(evaluate(store, "str('уже строка')")), "уже строка");
}

TEST(EvalCall, StrRejectsAggregates) {
    Store store;
    put(store, "items", "[1]");
    put(store, "o", "{}");
    EXPECT_EQ(evalError(store, "str(items)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "str(o)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, MinAndMaxTakeExactlyTwo) {
    Store store;
    EXPECT_EQ(evaluate(store, "min(1, 2)").numberValue(), 1.0);
    EXPECT_EQ(evaluate(store, "max(1, 2)").numberValue(), 2.0);
    EXPECT_EQ(evaluate(store, "min(-1, -2)").numberValue(), -2.0);
    // Для трёх и более — вложение (§8.9).
    EXPECT_EQ(evaluate(store, "min(3, min(1, 2))").numberValue(), 1.0);
}

TEST(EvalCall, MinAndMaxRejectNonNumbers) {
    Store store;
    EXPECT_EQ(evalError(store, "min('a', 1)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "max(1, true)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "min(null, 1)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, MinAndMaxPropagateNaN) {
    Store store;
    // 0 / 0 даёт NaN как значение (§5.2). Обе позиции обязательны: fmin и fmax
    // вернули бы число в каждой из них.
    EXPECT_TRUE(std::isnan(evaluate(store, "min(0 / 0, 5)").numberValue()));
    EXPECT_TRUE(std::isnan(evaluate(store, "min(5, 0 / 0)").numberValue()));
    EXPECT_TRUE(std::isnan(evaluate(store, "max(0 / 0, 5)").numberValue()));
    EXPECT_TRUE(std::isnan(evaluate(store, "max(5, 0 / 0)").numberValue()));
    // А бесконечность по-прежнему проходит насквозь, не превращаясь в NaN.
    EXPECT_EQ(evaluate(store, "min(1, 1 / 0)").numberValue(), 1.0);
}

TEST(EvalCall, AbsIsTheModulus) {
    Store store;
    EXPECT_EQ(evaluate(store, "abs(3)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(store, "abs(-3)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(store, "abs(0)").numberValue(), 0.0);
    EXPECT_EQ(evalError(store, "abs('a')").code, CS::ErrorCode::Type);
}

TEST(EvalCall, RoundGoesAwayFromZero) {
    Store store;
    // docs/semantics.md §8.10 перечисляет ровно эти случаи: от нуля, а не к
    // чётному. round(2.5) даёт 3, чего половинное-к-чётному не дало бы.
    EXPECT_EQ(evaluate(store, "round(0.5)").numberValue(), 1.0);
    EXPECT_EQ(evaluate(store, "round(1.5)").numberValue(), 2.0);
    EXPECT_EQ(evaluate(store, "round(2.5)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(store, "round(-0.5)").numberValue(), -1.0);
    EXPECT_EQ(evaluate(store, "round(1.4)").numberValue(), 1.0);
    EXPECT_EQ(evalError(store, "round(true)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, ArithmeticBuiltinsFollowIEEEOnSpecialValues) {
    Store store;
    // Экспонента в числах не входит в грамматику (docs/grammar.md §4.6:
    // `1e3` — не число), поэтому "1e400" не разбирается. Бесконечность из
    // данных получают тем же путём, что и core/tests/data_test.cpp
    // (DataScalars.VeryLongIntegerBecomesInfinity): 400-значный литерал
    // переполняет double и лексер округляет его до IEEE-бесконечности.
    put(store, "inf", std::string(400, '9'));
    // Бесконечность — значение, а не ошибка (§5.2), и функции её пропускают.
    EXPECT_TRUE(std::isinf(evaluate(store, "abs(inf)").numberValue()));
    EXPECT_TRUE(std::isinf(evaluate(store, "max(1, inf)").numberValue()));
    EXPECT_EQ(evaluate(store, "min(1, inf)").numberValue(), 1.0);
}

TEST(EvalFormat, SubstitutesLeftToRight) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    put(store, "cart", "{'taken': 2, 'total': 5}");
    EXPECT_EQ(store.string(evaluate(store, "format('Привет, ${}!', user.name)")),
              "Привет, Вася!");
    EXPECT_EQ(store.string(evaluate(store,
                                  "format('${} из ${}', cart.taken, cart.total)")),
              "2 из 5");
}

TEST(EvalFormat, EscapedPlaceholderIsLiteral) {
    Store store;
    // docs/semantics.md §8.8: $${} даёт литеральное ${} и аргумента не требует.
    EXPECT_EQ(store.string(evaluate(store, "format('цена $${}')")), "цена ${}");
    EXPECT_EQ(store.string(evaluate(store, "format('$${} и ${}', 1)")), "${} и 1");
}

TEST(EvalFormat, NoPlaceholdersGivesTheTemplate) {
    Store store;
    EXPECT_EQ(store.string(evaluate(store, "format('без подстановок')")),
              "без подстановок");
    EXPECT_EQ(store.string(evaluate(store, "format('')")), "");
}

TEST(EvalFormat, ArgumentsAreCoercedByChapterFour) {
    Store store;
    EXPECT_EQ(store.string(evaluate(store, "format('${}', 0.5)")), "0.5");
    EXPECT_EQ(store.string(evaluate(store, "format('${}', true)")), "true");
    EXPECT_EQ(store.string(evaluate(store, "format('${}', null)")), "null");
}

TEST(EvalFormat, AggregateArgumentIsAnError) {
    Store store;
    put(store, "items", "[1]");
    EXPECT_EQ(evalError(store, "format('${}', items)").code, CS::ErrorCode::Type);
}

TEST(EvalFormat, NonStringTemplateIsAnError) {
    Store store;
    put(store, "n", "1");
    EXPECT_EQ(evalError(store, "format(n, 1)").code, CS::ErrorCode::Type);
}

TEST(EvalFormat, MismatchWithANonLiteralTemplateIsARuntimeError) {
    Store store;
    put(store, "tpl", "{'two': '${} и ${}', 'none': 'нет'}");
    // Шаблон не литерал, значит проход сверить не мог — ловится здесь (§8.8).
    EXPECT_EQ(evalError(store, "format(tpl.two, 1)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(store, "format(tpl.none, 1)").code, CS::ErrorCode::Type);
    // А совпадающее число проходит.
    EXPECT_EQ(store.string(evaluate(store, "format(tpl.two, 1, 2)")), "1 и 2");
}

TEST(EvalFormat, ResultIsAnOrdinaryString) {
    Store store;
    put(store, "state", "{'label': ''}");
    run(store, "state.label = format('${} шт.', 3);");
    EXPECT_EQ(store.string(evaluate(store, "state.label")), "3 шт.");
    // count у строки считает байты, а не символы (§8.1): пять символов
    // «3 шт.» дают семь байт, потому что ш и т по два.
    EXPECT_EQ(evaluate(store, "count(state.label)").numberValue(), 7.0);
}

TEST(EvalFormat, LongResultCrossesPoolGrowth) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    // Результат заведомо длиннее начальной ёмкости пула: сборка обязана
    // пережить его переезд.
    const Value built = evaluate(
        store,
        "format('${}${}${}${}${}${}${}${}${}${}', user.name, user.name,"
        " user.name, user.name, user.name, user.name, user.name, user.name,"
        " user.name, user.name)");
    std::string expected;
    for (int i = 0; i < 10; ++i) { expected += "Вася"; }
    EXPECT_EQ(store.string(built), expected);
}

TEST(EvalFormat, ArgumentThatWritesToThePoolDoesNotLeakIntoTheResult) {
    Store store;
    // Строковый литерал в аргументе сам кладёт строку в пул — между началом
    // и концом сборки. В результат это попадать не должно.
    EXPECT_EQ(store.string(evaluate(store, "format('Привет, ${}!', 'мир')")),
              "Привет, мир!");
    // Вложенная сборка не поглощается внешней.
    EXPECT_EQ(store.string(evaluate(store, "format('${}', format('${}', 1))")), "1");
    // str тоже пишет в пул. (keys сюда не годится в пример: она возвращает
    // массив, а массив — агрегат, format отверг бы его по типу раньше, чем
    // дело дошло бы до утечки байтов.)
    EXPECT_EQ(store.string(evaluate(store, "format('${}', str(2))")), "2");
}

TEST(EvalFormat, ResultSurvivesIntoTheData) {
    Store store;
    put(store, "state", "{'label': ''}");
    run(store, "state.label = format('${} шт.', 'десять');");
    EXPECT_EQ(store.string(evaluate(store, "state.label")), "десять шт.");
}

TEST(EvalFormat, NestedAssemblyKeepsTheOuterPrefix) {
    Store store;
    // Вложенный format стоит не первым куском шаблона, поэтому к моменту его
    // начала во внешней сборке уже накоплен префикс. Внутренняя обязана снять
    // за собой ровно свой хвост и не тронуть его: это и есть свойство стека,
    // ради которого сборка живёт в отдельном буфере.
    EXPECT_EQ(store.string(evaluate(store, "format('a${}b', format('c${}d', 1))")),
              "ac1db");
    // Три уровня, размотка изнутри наружу:
    //   format('5${}6', 7)                       = "5" + "7"     + "6" = "576"
    //   format('3${}4', "576")                    = "3" + "576"  + "4" = "35764"
    //   format('1${}2', "35764")                  = "1" + "35764" + "2" = "1357642"
    EXPECT_EQ(store.string(evaluate(
                  store, "format('1${}2', format('3${}4', format('5${}6', 7)))")),
              "1357642");
}

}  // namespace
