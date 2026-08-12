#include "eval.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <set>
#include <string>

#include "ast.hpp"
#include "compile.hpp"
#include "context.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "parser.hpp"

namespace {

using CS::Ast;
using CS::Context;
using CS::Diagnostic;
using CS::Value;

/// Разбирает и вычисляет; требует успеха обоих шагов.
Value evaluate(Context &ctx, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors = CS::compileExpression(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx, &diag, 1);
    EXPECT_EQ(errors, 0u) << diag.message;
    Value out = Value::null();
    EXPECT_TRUE(CS::evalExpression(ast, ctx, &out, diag)) << diag.message;
    return out;
}

/// Разбирает успешно, вычисляет с отказом; возвращает диагностику вычисления.
Diagnostic evalError(Context &ctx, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors = CS::compileExpression(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx, &diag, 1);
    EXPECT_EQ(errors, 0u) << diag.message;
    Value out = Value::null();
    EXPECT_FALSE(CS::evalExpression(ast, ctx, &out, diag));
    return diag;
}

/// Кладёт корень; требует успеха.
void put(Context &ctx, std::string_view name, std::string_view text) {
    Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(ctx, name, text, diag)) << diag.message;
}

TEST(EvalLiterals, NumberIsEvaluated) {
    Context ctx;
    EXPECT_EQ(evaluate(ctx, "3").numberValue(), 3.0);
    EXPECT_EQ(evaluate(ctx, "0.5").numberValue(), 0.5);
}

TEST(EvalLiterals, BooleanIsEvaluated) {
    Context ctx;
    EXPECT_TRUE(evaluate(ctx, "true").booleanValue());
    EXPECT_FALSE(evaluate(ctx, "false").booleanValue());
}

TEST(EvalLiterals, NullIsEvaluated) {
    Context ctx;
    EXPECT_EQ(evaluate(ctx, "null").kind(), Value::Kind::Null);
}

TEST(EvalLiterals, StringIsEvaluated) {
    Context ctx;
    EXPECT_EQ(ctx.string(evaluate(ctx, "'Вася'")), "Вася");
    EXPECT_EQ(ctx.string(evaluate(ctx, "\"Вася\"")), "Вася");
}

TEST(EvalLiterals, StringEscapesAreDecoded) {
    Context ctx;
    EXPECT_EQ(ctx.string(evaluate(ctx, "'a\\nb'")), "a\nb");
}

TEST(EvalNames, RootIsRead) {
    Context ctx;
    put(ctx, "count", "3");
    EXPECT_EQ(evaluate(ctx, "count").numberValue(), 3.0);
}

TEST(EvalNames, RootHoldingAggregateIsReadByIdentity) {
    Context ctx;
    put(ctx, "items", "[1, 2]");
    EXPECT_TRUE(evaluate(ctx, "items").sameAggregate(ctx.root("items")));
}

TEST(EvalNames, RootHoldingNullIsRead) {
    Context ctx;
    put(ctx, "maybe", "null");
    // Корень со значением null существует и читается как null — это не то же
    // самое, что отсутствующий корень.
    EXPECT_EQ(evaluate(ctx, "maybe").kind(), Value::Kind::Null);
}

TEST(EvalMember, ExistingKeyIsRead) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася', 'age': 30}");
    EXPECT_EQ(ctx.string(evaluate(ctx, "user.name")), "Вася");
    EXPECT_EQ(evaluate(ctx, "user.age").numberValue(), 30.0);
}

TEST(EvalMember, MissingKeyReadsAsNull) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // docs/semantics.md §6.2: отсутствующий ключ читается как null.
    EXPECT_EQ(evaluate(ctx, "user.nickname").kind(), Value::Kind::Null);
}

TEST(EvalMember, ReadingThroughNullGivesNull) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // docs/semantics.md §6.3: путь любой глубины безопасен, а опечатка глубже
    // первого сегмента не диагностируется — это цена правила.
    EXPECT_EQ(evaluate(ctx, "user.prfoile.avatar").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(ctx, "user.a.b.c.d.e").kind(), Value::Kind::Null);
}

TEST(EvalMember, ReadingKeyOffANonObjectIsAnError) {
    Context ctx;
    put(ctx, "count", "3");
    put(ctx, "name", "'Вася'");
    put(ctx, "flag", "true");
    put(ctx, "items", "[1, 2]");
    // docs/semantics.md §6.4: доступ по ключу определён для Object, чтение у
    // null — правилом §6.3, прочее — ошибка.
    EXPECT_EQ(evalError(ctx, "count.x").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "name.x").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "flag.x").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "items.x").code, CS::ErrorCode::Type);
}

TEST(EvalMember, KeyIsTakenLiterallyNotAsAName) {
    Context ctx;
    put(ctx, "o", "{'name': 'ключ'}");
    put(ctx, "name", "'корень'");
    // docs/semantics.md §6.2: в форме obj.k ключом является имя k буквально, а
    // не значение корня, который случайно называется так же.
    EXPECT_EQ(ctx.string(evaluate(ctx, "o.name")), "ключ");
}

TEST(EvalMember, OffsetPointsAtTheFailingNode) {
    Context ctx;
    put(ctx, "count", "3");
    // Место ошибки — там, где чинить, а не в начале выражения.
    EXPECT_GT(evalError(ctx, "count.a.b").offset, 0u);
}

TEST(EvalIndex, ArrayElementIsRead) {
    Context ctx;
    put(ctx, "items", "[10, 20, 30]");
    EXPECT_EQ(evaluate(ctx, "items[0]").numberValue(), 10.0);
    EXPECT_EQ(evaluate(ctx, "items[2]").numberValue(), 30.0);
}

TEST(EvalIndex, ArrayReadBeyondEndGivesNull) {
    Context ctx;
    put(ctx, "items", "[10]");
    // docs/semantics.md §6.1: чтение за границей штатно — данные неполны.
    EXPECT_EQ(evaluate(ctx, "items[1]").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(ctx, "items[1000000]").kind(), Value::Kind::Null);
}

TEST(EvalIndex, FractionalAndNegativeIndicesAreErrors) {
    Context ctx;
    put(ctx, "items", "[10, 20]");
    put(ctx, "minusOne", "-1");
    put(ctx, "huge", std::string(400, '9'));
    // Дробный и отрицательный индекс означают намерение, которого в языке нет:
    // приведения к целому тоже нет. Отрицательное значение и бесконечность
    // берутся из данных — унарный минус это оператор, а операторов в части 1
    // нет; четыреста девяток переполняют double и дают inf.
    EXPECT_EQ(evalError(ctx, "items[0.5]").code, CS::ErrorCode::Range);
    EXPECT_EQ(evalError(ctx, "items[minusOne]").code, CS::ErrorCode::Range);
    EXPECT_EQ(evalError(ctx, "items[huge]").code, CS::ErrorCode::Range);
}

TEST(EvalIndex, NonNumberArrayIndexIsAnError) {
    Context ctx;
    put(ctx, "items", "[10, 20]");
    // docs/semantics.md §6.1: приведения к Number нет, поэтому items['0']
    // не работает.
    EXPECT_EQ(evalError(ctx, "items['0']").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "items[true]").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "items[null]").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, ObjectKeyIsRead) {
    Context ctx;
    put(ctx, "o", "{'name': 'Вася'}");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o['name']")), "Вася");
    EXPECT_EQ(evaluate(ctx, "o['missing']").kind(), Value::Kind::Null);
}

TEST(EvalIndex, ScalarKeysAreCoercedToString) {
    Context ctx;
    put(ctx, "o", "{'0': 'zero', 'true': 'yes', 'null': 'nothing', '1.5': 'half'}");
    // docs/semantics.md §4.1: ключ объекта — одна из трёх позиций, требующих
    // String, и приведение туда одностороннее.
    EXPECT_EQ(ctx.string(evaluate(ctx, "o[0]")), "zero");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o[true]")), "yes");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o[null]")), "nothing");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o[1.5]")), "half");
}

TEST(EvalIndex, NegativeZeroAndZeroAreDifferentKeys) {
    Context ctx;
    put(ctx, "o", "{'0': 'plus', '-0': 'minus'}");
    put(ctx, "minusZero", "-0");
    // docs/semantics.md §4.3: -0 == 0 истинно, но ключи разные, потому что
    // представление числа сохраняет знак нуля. Отрицательный ноль приходит из
    // данных по той же причине, что и в тесте выше.
    EXPECT_EQ(ctx.string(evaluate(ctx, "o[0]")), "plus");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o[minusZero]")), "minus");
}

TEST(EvalIndex, AggregateKeyIsAnError) {
    Context ctx;
    put(ctx, "o", "{'a': 1}");
    put(ctx, "items", "[1]");
    // Агрегат не приводится никуда (docs/semantics.md §4).
    EXPECT_EQ(evalError(ctx, "o[items]").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, ReadingThroughNullGivesNull) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    EXPECT_EQ(evaluate(ctx, "user.missing[0]").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(ctx, "user.missing['k']").kind(), Value::Kind::Null);
}

TEST(EvalIndex, SubscriptIsEvaluatedEvenWhenTheBaseIsNull) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
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
    EXPECT_EQ(evalError(ctx, "user.missing[1 + 'a']").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, BaseIsEvaluatedBeforeTheSubscript) {
    Context ctx;
    // docs/semantics.md §3.3 фиксирует порядок именно ради определённости
    // диагностики, когда ошибочны оба операнда. Обе ошибки — Type через
    // 1 + 'a' (по причине из SubscriptIsEvaluatedEvenWhenTheBaseIsNull выше),
    // и левая обязана выиграть.
    const Diagnostic diag = evalError(ctx, "(1 + 'a')[2 + 'b']");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_LT(diag.offset, 10u);
}

TEST(EvalIndex, IndexingANonAggregateIsAnError) {
    Context ctx;
    put(ctx, "count", "3");
    put(ctx, "name", "'Вася'");
    put(ctx, "flag", "true");
    // docs/semantics.md §6.4: 'abc'[0] — ошибка, строка не индексируется.
    EXPECT_EQ(evalError(ctx, "count[0]").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "name[0]").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "flag[0]").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, ChainedAccessWorks) {
    Context ctx;
    put(ctx, "state", "{'items': [{'id': 1}, {'id': 2}]}");
    EXPECT_EQ(evaluate(ctx, "state.items[1].id").numberValue(), 2.0);
}

TEST(EvalAggregates, ArrayLiteralKeepsOrder) {
    Context ctx;
    const Value a = evaluate(ctx, "[1, 2, 3]");
    ASSERT_EQ(ctx.arrayCount(a), 3u);
    EXPECT_EQ(ctx.arrayAt(a, 0).numberValue(), 1.0);
    EXPECT_EQ(ctx.arrayAt(a, 2).numberValue(), 3.0);
}

TEST(EvalAggregates, ObjectLiteralStoresPairs) {
    Context ctx;
    const Value o = evaluate(ctx, "{'a': 1, 'b': 2}");
    ASSERT_EQ(ctx.objectCount(o), 2u);
    EXPECT_EQ(ctx.objectGet(o, "a").numberValue(), 1.0);
    EXPECT_EQ(ctx.objectGet(o, "b").numberValue(), 2.0);
}

TEST(EvalAggregates, EmptyLiterals) {
    Context ctx;
    EXPECT_EQ(ctx.arrayCount(evaluate(ctx, "[]")), 0u);
    EXPECT_EQ(ctx.objectCount(evaluate(ctx, "{}")), 0u);
}

TEST(EvalAggregates, ElementsAreArbitraryExpressions) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    put(ctx, "items", "[7]");
    // Вот чем агрегат в выражении отличается от агрегата в данных: элемент —
    // выражение, а не литерал.
    const Value a = evaluate(ctx, "[user.name, items[0], user.missing]");
    ASSERT_EQ(ctx.arrayCount(a), 3u);
    EXPECT_EQ(ctx.string(ctx.arrayAt(a, 0)), "Вася");
    EXPECT_EQ(ctx.arrayAt(a, 1).numberValue(), 7.0);
    EXPECT_EQ(ctx.arrayAt(a, 2).kind(), Value::Kind::Null);
}

TEST(EvalAggregates, ObjectValuesAreExpressionsAndKeysAreLiterals) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    const Value o = evaluate(ctx, "{'who': user.name}");
    EXPECT_EQ(ctx.string(ctx.objectGet(o, "who")), "Вася");
}

TEST(EvalAggregates, ErrorInsideAnElementStopsAtTheFirstFailure) {
    Context ctx;
    // Два сбойных элемента: диагностика обязана указать на первый, иначе
    // «первая ошибка выигрывает» держится на честном слове. В частях 2 и 3 это
    // правило станет несущим для && и ??.
    //
    // Обе ошибки — Type через 1 + 'a', не неизвестное имя: статический проход
    // (core/src/check.hpp) отсеял бы оба элемента разом, ещё до вычисления, и
    // «первый выигрывает» стало бы непроверяемым.
    const Diagnostic diag = evalError(ctx, "[1 + 'a', 2 + 'b']");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_LT(diag.offset, 10u);
}

TEST(EvalDepth, ChainAtTheParserLimitEvaluatesWithoutOverflow) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
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
    EXPECT_EQ(evaluate(ctx, source).kind(), Value::Kind::Null);

    // На единицу длиннее до вычислителя уже не доходит: отказ на разборе.
    Ast ast;
    Diagnostic diag;
    const std::string tooLong = source + ".b";
    EXPECT_FALSE(CS::parseExpression(
        tooLong.data(), static_cast<std::uint32_t>(tooLong.size()), ast, diag));
    EXPECT_STREQ(diag.message, "expression nesting too deep");
}

TEST(EvalDepth, OperatorChainAtTheParserLimitEvaluatesWithoutOverflow) {
    Context ctx;
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
    EXPECT_EQ(evaluate(ctx, source).numberValue(), 510.0);

    // На единицу длиннее до вычислителя уже не доходит: отказ на разборе.
    Ast ast;
    Diagnostic diag;
    const std::string tooLong = source + " + 1";
    EXPECT_FALSE(CS::parseExpression(
        tooLong.data(), static_cast<std::uint32_t>(tooLong.size()), ast, diag));
    EXPECT_STREQ(diag.message, "expression nesting too deep");
}

TEST(EvalAggregates, EachEvaluationCreatesANewAggregate) {
    Context ctx;
    Ast ast;
    Diagnostic diag;
    const std::string_view text = "[1, 2]";
    // Дерево используется дважды подряд, поэтому проверка идёт через фасад
    // компиляции напрямую, а не через evaluate(): тому нужен свежий Ast на
    // каждый вызов, а здесь как раз важно одно и то же дерево.
    ASSERT_EQ(CS::compileExpression(text.data(),
                                    static_cast<std::uint32_t>(text.size()),
                                    ast, ctx, &diag, 1),
              0u)
        << diag.message;

    Value first = Value::null();
    Value second = Value::null();
    ASSERT_TRUE(CS::evalExpression(ast, ctx, &first, diag));
    ASSERT_TRUE(CS::evalExpression(ast, ctx, &second, diag));

    // docs/semantics.md §2.3: литерал создаёт новый агрегат при каждом
    // вычислении. Без этого теста правило держится на честном слове.
    EXPECT_FALSE(first.sameAggregate(second));
    EXPECT_EQ(ctx.arrayCount(first), 2u);
    EXPECT_EQ(ctx.arrayCount(second), 2u);
}

TEST(EvalOperators, UnaryWorksThroughTheWalk) {
    Context ctx;
    EXPECT_FALSE(evaluate(ctx, "!true").booleanValue());
    EXPECT_EQ(evaluate(ctx, "-3").numberValue(), -3.0);
}

TEST(EvalOperators, ArithmeticRespectsPrecedence) {
    Context ctx;
    // Приоритет — дело грамматики; вычислитель лишь обходит построенное дерево.
    EXPECT_EQ(evaluate(ctx, "1 + 2 * 3").numberValue(), 7.0);
    EXPECT_EQ(evaluate(ctx, "(1 + 2) * 3").numberValue(), 9.0);
}

TEST(EvalOperators, ComparisonWorksThroughTheWalk) {
    Context ctx;
    EXPECT_TRUE(evaluate(ctx, "1 < 2").booleanValue());
    EXPECT_FALSE(evaluate(ctx, "1 > 2").booleanValue());
}

TEST(EvalOperators, EqualityWorksThroughTheWalk) {
    Context ctx;
    EXPECT_TRUE(evaluate(ctx, "1 == 1").booleanValue());
    EXPECT_TRUE(evaluate(ctx, "1 != 2").booleanValue());
    EXPECT_TRUE(evaluate(ctx, "null == null").booleanValue());
}

TEST(EvalOperators, OperandsComeFromTheContext) {
    Context ctx;
    put(ctx, "state", "{'count': 41}");
    EXPECT_EQ(evaluate(ctx, "state.count + 1").numberValue(), 42.0);
}

TEST(EvalOperators, ErrorInTheLeftOperandStopsEvaluation) {
    Context ctx;
    put(ctx, "items", "[1]");
    // Ошибка — Range через items[-1], не неизвестное имя: статический проход
    // (core/src/check.hpp) отсеял бы неизвестное имя ещё до вычисления, и
    // пробой «дошли ли мы сюда вычислением» оно уже быть не может. Range —
    // код, которого сам '+' не порождает, поэтому от штатного результата
    // отличим по коду, а не только по факту отказа.
    EXPECT_EQ(evalError(ctx, "items[-1] + 1").code, CS::ErrorCode::Range);
}

TEST(EvalOperators, ErrorInTheRightOperandStopsEvaluation) {
    Context ctx;
    put(ctx, "items", "[1]");
    EXPECT_EQ(evalError(ctx, "1 + items[-1]").code, CS::ErrorCode::Range);
}

TEST(EvalOperators, LeftOperandIsEvaluatedBeforeTheRight) {
    Context ctx;
    // docs/semantics.md §3.3 фиксирует порядок именно ради определённости
    // диагностики, когда ошибочны оба операнда. С одним ошибочным операндом
    // порядок ненаблюдаем, и перестановка прошла бы незамеченной.
    const Diagnostic diag = evalError(ctx, "(1 + 'a') + (2 + 'b')");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_LT(diag.offset, 10u);
}

TEST(EvalOperators, AggregateEqualityIsByIdentityThroughTheWalk) {
    Context ctx;
    put(ctx, "items", "[1, 2]");
    // Литерал создаёт новый агрегат при каждом вычислении, поэтому сравнение
    // с ним ложно даже при совпадающем содержимом.
    EXPECT_TRUE(evaluate(ctx, "items == items").booleanValue());
    EXPECT_FALSE(evaluate(ctx, "items == [1, 2]").booleanValue());
}

TEST(EvalShortCircuit, AndDoesNotEvaluateTheRightOperand) {
    Context ctx;
    // Побочных эффектов в выражениях нет, поэтому невычисление наблюдается
    // единственным способом: ошибка справа не всплывает.
    //
    // Проба — Type через 1 + 'a', не неизвестное имя: статический проход
    // (core/src/check.hpp) отсеял бы неизвестное имя ещё до вычисления,
    // независимо от того, короткое замыкание его достигает или нет, и такой
    // пробой служить больше не может.
    EXPECT_FALSE(evaluate(ctx, "false && (1 + 'a')").booleanValue());
}

TEST(EvalShortCircuit, AndEvaluatesTheRightOperandWhenNeeded) {
    Context ctx;
    put(ctx, "items", "[1]");
    EXPECT_FALSE(evaluate(ctx, "true && false").booleanValue());
    EXPECT_TRUE(evaluate(ctx, "true && true").booleanValue());
    // Range через items[-1]: код, которого && сам не порождает.
    EXPECT_EQ(evalError(ctx, "true && items[-1]").code, CS::ErrorCode::Range);
}

TEST(EvalShortCircuit, OrDoesNotEvaluateTheRightOperand) {
    Context ctx;
    EXPECT_TRUE(evaluate(ctx, "true || (1 + 'a')").booleanValue());
}

TEST(EvalShortCircuit, OrEvaluatesTheRightOperandWhenNeeded) {
    Context ctx;
    put(ctx, "items", "[1]");
    EXPECT_TRUE(evaluate(ctx, "false || true").booleanValue());
    EXPECT_FALSE(evaluate(ctx, "false || false").booleanValue());
    // Range через items[-1]: код, которого || сам не порождает.
    EXPECT_EQ(evalError(ctx, "false || items[-1]").code, CS::ErrorCode::Range);
}

TEST(EvalShortCircuit, ErrorOnTheLeftIsNotSwallowed) {
    Context ctx;
    put(ctx, "items", "[1]");
    // docs/semantics.md §5.5: ошибка && false — ошибка. Левый операнд обязан
    // быть булевым по грамматике оператора, поэтому пробу берём такую, что
    // сама по себе даёт Range ещё до проверки булевости (items[-1]): код,
    // которого ни +, ни && / || не порождают, и коллизии с их штатной
    // ошибкой типа не возникает.
    EXPECT_EQ(evalError(ctx, "items[-1] && false").code, CS::ErrorCode::Range);
    EXPECT_EQ(evalError(ctx, "items[-1] || true").code, CS::ErrorCode::Range);
}

TEST(EvalShortCircuit, TypeOfTheUnevaluatedOperandIsNotChecked) {
    Context ctx;
    // Самая точная проверка правила: тип правого операнда проверяется тогда и
    // только тогда, когда его пришлось вычислить. Поодиночке ни одна из двух
    // строк ничего не доказывает.
    EXPECT_FALSE(evaluate(ctx, "false && 5").booleanValue());
    EXPECT_EQ(evalError(ctx, "true && 5").code, CS::ErrorCode::Type);
    // Зеркало для ||: у него замыкает истина, а не ложь.
    EXPECT_TRUE(evaluate(ctx, "true || 5").booleanValue());
    EXPECT_EQ(evalError(ctx, "false || 5").code, CS::ErrorCode::Type);
}

TEST(EvalShortCircuit, LogicalOperatorsRequireBooleanOnTheLeft) {
    Context ctx;
    EXPECT_EQ(evalError(ctx, "1 && true").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "'a' || true").code, CS::ErrorCode::Type);
}

TEST(EvalShortCircuit, GuardIdiomProtectsTheRightSide) {
    Context ctx;
    put(ctx, "state", "{'items': []}");
    // Ради этого короткое замыкание и существует. Правая часть без защиты
    // слева даёт ошибку типа: строковый индекс массива запрещён. Настоящая
    // идиома из §5.5 пользуется count(), который придёт с частью 3, — здесь
    // та же форма на доступных средствах.
    //
    // Обе строки обязательны: одна показывает, что справа не пошли, вторая —
    // что там действительно есть на что наткнуться.
    EXPECT_FALSE(evaluate(ctx, "false && state.items['0'] == 1").booleanValue());
    EXPECT_EQ(evalError(ctx, "true && state.items['0'] == 1").code,
              CS::ErrorCode::Type);
}

TEST(EvalNilCoalesce, TakesTheLeftWhenItIsNotNull) {
    Context ctx;
    // Проба справа — Type через 1 + 'a' (см. DoesNotSwallowErrors ниже,
    // где этот приём уже применён): она обязана не всплыть, если левый не
    // null.
    EXPECT_EQ(evaluate(ctx, "1 ?? (1 + 'a')").numberValue(), 1.0);
}

TEST(EvalNilCoalesce, TakesTheRightWhenTheLeftIsNull) {
    Context ctx;
    EXPECT_EQ(evaluate(ctx, "null ?? 2").numberValue(), 2.0);
}

TEST(EvalNilCoalesce, DoesNotSwallowErrors) {
    Context ctx;
    // docs/semantics.md §5.6: ?? перехватывает только null. Соблазнительно
    // принять его за «если что-то пойдёт не так, подставь запасное»; он делает
    // не это.
    EXPECT_EQ(evalError(ctx, "(1 + 'a') ?? 0").code, CS::ErrorCode::Type);
    // И справа тоже: если левый null, правый вычисляется по-настоящему.
    EXPECT_EQ(evalError(ctx, "null ?? (2 + 'b')").code, CS::ErrorCode::Type);
}

TEST(EvalNilCoalesce, OperandTypesNeedNotMatch) {
    Context ctx;
    EXPECT_EQ(ctx.string(evaluate(ctx, "null ?? 'запасное'")), "запасное");
}

TEST(EvalNilCoalesce, ChainsRightAssociatively) {
    Context ctx;
    put(ctx, "user", "{'nickname': null}");
    EXPECT_EQ(ctx.string(evaluate(ctx, "user.nickname ?? user.name ?? 'Гость'")),
              "Гость");
}

TEST(EvalTernary, EvaluatesOnlyTheSelectedBranch) {
    Context ctx;
    // Проба в невыбранной ветке — Type через 1 + 'a', не неизвестное имя: она
    // обязана не всплыть, что доказывает невычисление.
    EXPECT_EQ(evaluate(ctx, "true ? 1 : (1 + 'a')").numberValue(), 1.0);
    EXPECT_EQ(evaluate(ctx, "false ? (1 + 'a') : 2").numberValue(), 2.0);
}

TEST(EvalTernary, ConditionMustBeBoolean) {
    Context ctx;
    EXPECT_EQ(evalError(ctx, "1 ? 1 : 2").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "null ? 1 : 2").code, CS::ErrorCode::Type);
}

TEST(EvalTernary, BranchesNeedNotShareAType) {
    Context ctx;
    EXPECT_EQ(evaluate(ctx, "true ? 1 : 'a'").numberValue(), 1.0);
    EXPECT_EQ(ctx.string(evaluate(ctx, "false ? 1 : 'a'")), "a");
}

/// Разбирает и выполняет скрипт; требует успеха обоих шагов.
void run(Context &ctx, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors = CS::compileScript(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx, &diag, 1);
    ASSERT_EQ(errors, 0u) << diag.message;
    ASSERT_TRUE(CS::runScript(ast, ctx, diag)) << diag.message;
}

/// Разбирает успешно, выполняет с отказом; возвращает диагностику выполнения.
Diagnostic runError(Context &ctx, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors = CS::compileScript(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx, &diag, 1);
    EXPECT_EQ(errors, 0u) << diag.message;
    EXPECT_FALSE(CS::runScript(ast, ctx, diag));
    return diag;
}

TEST(EvalAssign, ExistingKeyIsReplaced) {
    Context ctx;
    put(ctx, "state", "{'count': 1}");
    run(ctx, "state.count = 42;");
    EXPECT_EQ(evaluate(ctx, "state.count").numberValue(), 42.0);
}

TEST(EvalAssign, MissingKeyIsCreated) {
    Context ctx;
    put(ctx, "state", "{}");
    // docs/semantics.md §6.2: запись создаёт ключ, если его нет.
    run(ctx, "state.fresh = 'значение';");
    EXPECT_EQ(ctx.string(evaluate(ctx, "state.fresh")), "значение");
}

TEST(EvalAssign, ValueMayBeAnyExpression) {
    Context ctx;
    put(ctx, "state", "{'a': 2, 'b': 3}");
    run(ctx, "state.sum = state.a * state.b + 1;");
    EXPECT_EQ(evaluate(ctx, "state.sum").numberValue(), 7.0);
}

TEST(EvalAssign, DeepPathIsWritable) {
    Context ctx;
    put(ctx, "user", "{'profile': {'city': {}}}");
    run(ctx, "user.profile.city.name = 'Москва';");
    EXPECT_EQ(ctx.string(evaluate(ctx, "user.profile.city.name")), "Москва");
}

TEST(EvalAssign, WritingIntoNullIsAnError) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // docs/semantics.md §7.2: мягкость §6.3 распространяется только на чтение.
    // Обе половины обязательны: без второй правило вырождается.
    EXPECT_EQ(evaluate(ctx, "user.profile.name").kind(), Value::Kind::Null);
    EXPECT_EQ(runError(ctx, "user.profile.name = 'Вася';").code,
              CS::ErrorCode::Type);
}

TEST(EvalAssign, WritingAKeyOffANonObjectIsAnError) {
    Context ctx;
    put(ctx, "count", "3");
    put(ctx, "items", "[1]");
    EXPECT_EQ(runError(ctx, "count.x = 1;").code, CS::ErrorCode::Type);
    EXPECT_EQ(runError(ctx, "items.x = 1;").code, CS::ErrorCode::Type);
}

// AssigningToANameIsAnError и UnknownNameIsAnError переехали в
// core/tests/check_test.cpp (Check.AssigningToANameIsACompileError,
// Check.UnknownNameInAssignmentTargetIsACompileError): "state = 1;" и
// "usre.a = 1;" отсеиваются статическим проходом ещё до вычисления, и
// runError() до них больше не доходит.

TEST(EvalAssign, ErrorInTheValueLeavesTheTargetUntouched) {
    Context ctx;
    put(ctx, "state", "{'a': 1}");
    // Ошибка — Type через 1 + 'a', не неизвестное имя: с приходом
    // статического прохода последнее ловится ещё на компиляции, а здесь
    // важно именно поведение вычислителя при рантайм-ошибке справа.
    EXPECT_EQ(runError(ctx, "state.a = 1 + 'a';").code, CS::ErrorCode::Type);
    EXPECT_EQ(evaluate(ctx, "state.a").numberValue(), 1.0);
}

TEST(EvalAssign, TargetCheckLosesToValueErrorWhenBothFail) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    put(ctx, "items", "[1, 2, 3]");
    put(ctx, "minusOne", "-1");
    // docs/semantics.md §7.2: цель проверяется после вычисления правой части.
    // Запись в null сама по себе даёт Type (WritingIntoNullIsAnError), но
    // здесь неисправна и правая часть тоже — чтение по отрицательному индексу
    // даёт Range (FractionalAndNegativeIndicesAreErrors), и она вычисляется
    // раньше — побеждает Range.
    EXPECT_EQ(runError(ctx, "user.profile.name = items[minusOne];").code,
              CS::ErrorCode::Range);
}

TEST(EvalScript, EmptyScriptSucceeds) {
    Context ctx;
    run(ctx, "");
    run(ctx, ";;;");
}

TEST(EvalAssignIndex, ArrayElementIsReplaced) {
    Context ctx;
    put(ctx, "items", "[10, 20, 30]");
    run(ctx, "items[1] = 99;");
    EXPECT_EQ(evaluate(ctx, "items[1]").numberValue(), 99.0);
    EXPECT_EQ(evaluate(ctx, "items[0]").numberValue(), 10.0);
}

TEST(EvalAssignIndex, WritingBeyondTheEndIsAnError) {
    Context ctx;
    put(ctx, "items", "[10]");
    // docs/semantics.md §6.1: чтение за границей штатно, запись за границу —
    // намерение создать элемент, для чего существует push. Обе половины
    // обязательны.
    EXPECT_EQ(evaluate(ctx, "items[1]").kind(), Value::Kind::Null);
    EXPECT_EQ(runError(ctx, "items[1] = 1;").code, CS::ErrorCode::Range);
    EXPECT_EQ(runError(ctx, "items[1000000] = 1;").code, CS::ErrorCode::Range);
    // 2^32: приведение к uint32_t усекло бы индекс в ноль, попав в границы.
    EXPECT_EQ(runError(ctx, "items[4294967296] = 1;").code, CS::ErrorCode::Range);
}

TEST(EvalAssignIndex, FractionalAndNegativeIndicesAreErrors) {
    Context ctx;
    put(ctx, "items", "[10, 20]");
    put(ctx, "minusOne", "-1");
    EXPECT_EQ(runError(ctx, "items[0.5] = 1;").code, CS::ErrorCode::Range);
    EXPECT_EQ(runError(ctx, "items[minusOne] = 1;").code, CS::ErrorCode::Range);
}

TEST(EvalAssignIndex, NonNumberArrayIndexIsAnError) {
    Context ctx;
    put(ctx, "items", "[10, 20]");
    EXPECT_EQ(runError(ctx, "items['0'] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, ObjectKeyIsWritten) {
    Context ctx;
    put(ctx, "o", "{'a': 1}");
    run(ctx, "o['a'] = 2;");
    run(ctx, "o['fresh'] = 3;");
    EXPECT_EQ(evaluate(ctx, "o.a").numberValue(), 2.0);
    EXPECT_EQ(evaluate(ctx, "o.fresh").numberValue(), 3.0);
}

TEST(EvalAssignIndex, ScalarKeysAreCoercedToString) {
    Context ctx;
    put(ctx, "o", "{}");
    // docs/semantics.md §4.1: ключ объекта — одна из трёх позиций, требующих
    // String; правила приведения те же, что при чтении.
    run(ctx, "o[0] = 'ноль';");
    run(ctx, "o[true] = 'да';");
    run(ctx, "o[null] = 'ничего';");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o['0']")), "ноль");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o['true']")), "да");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o['null']")), "ничего");
}

TEST(EvalAssignIndex, AggregateKeyIsAnError) {
    Context ctx;
    put(ctx, "o", "{}");
    put(ctx, "items", "[1]");
    EXPECT_EQ(runError(ctx, "o[items] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, WritingIntoNullIsAnError) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    EXPECT_EQ(evaluate(ctx, "user.missing[0]").kind(), Value::Kind::Null);
    EXPECT_EQ(runError(ctx, "user.missing[0] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, WritingIntoANonAggregateIsAnError) {
    Context ctx;
    put(ctx, "count", "3");
    put(ctx, "name", "'Вася'");
    EXPECT_EQ(runError(ctx, "count[0] = 1;").code, CS::ErrorCode::Type);
    EXPECT_EQ(runError(ctx, "name[0] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, ChainedTargetWorks) {
    Context ctx;
    put(ctx, "state", "{'rows': [{'cells': [1, 2]}]}");
    run(ctx, "state.rows[0].cells[1] = 99;");
    EXPECT_EQ(evaluate(ctx, "state.rows[0].cells[1]").numberValue(), 99.0);
}

TEST(EvalAssignIndex, SubscriptMayBeAnExpression) {
    Context ctx;
    put(ctx, "items", "[10, 20, 30]");
    put(ctx, "i", "1");
    run(ctx, "items[i + 1] = 99;");
    EXPECT_EQ(evaluate(ctx, "items[2]").numberValue(), 99.0);
}

TEST(EvalCompound, FourOperatorsWorkOnAKey) {
    Context ctx;
    put(ctx, "s", "{'n': 10}");
    run(ctx, "s.n += 5;");
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 15.0);
    run(ctx, "s.n -= 3;");
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 12.0);
    run(ctx, "s.n *= 2;");
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 24.0);
    run(ctx, "s.n /= 4;");
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 6.0);
}

TEST(EvalCompound, WorksOnAnArrayElement) {
    Context ctx;
    put(ctx, "items", "[1, 2, 3]");
    run(ctx, "items[1] += 10;");
    EXPECT_EQ(evaluate(ctx, "items[1]").numberValue(), 12.0);
}

TEST(EvalCompound, WorksOnAnObjectKeyByIndex) {
    Context ctx;
    put(ctx, "o", "{'a': 1}");
    run(ctx, "o['a'] += 1;");
    EXPECT_EQ(evaluate(ctx, "o.a").numberValue(), 2.0);
}

TEST(EvalCompound, TypeMismatchIsAnError) {
    Context ctx;
    put(ctx, "s", "{'text': 'а'}");
    // Операция берётся из applyBinary, поэтому правила типов те же, что у
    // обычного оператора: конкатенации строк через + нет.
    EXPECT_EQ(runError(ctx, "s.text += 'б';").code, CS::ErrorCode::Type);
}

TEST(EvalCompound, MissingKeyReadsAsNullAndThenFails) {
    Context ctx;
    put(ctx, "s", "{}");
    // Чтение отсутствующего ключа даёт null (§6.2), а null + 1 — ошибка типа.
    EXPECT_EQ(runError(ctx, "s.missing += 1;").code, CS::ErrorCode::Type);
}

TEST(EvalCompound, BeyondTheEndGivesTypeNotRange) {
    Context ctx;
    put(ctx, "items", "[1, 2]");
    // docs/semantics.md §7.3: x += e есть x = x + e. Сначала читается items[5],
    // что штатно даёт null, затем вычисляется null + 1 — ошибка типа. До
    // проверки границы записи дело не доходит, поэтому Type, а не Range.
    // Простое присваивание туда же даёт Range — обе строки обязательны.
    EXPECT_EQ(runError(ctx, "items[5] += 1;").code, CS::ErrorCode::Type);
    EXPECT_EQ(runError(ctx, "items[5] = 1;").code, CS::ErrorCode::Range);
}

TEST(EvalCompound, ErrorLeavesTheTargetUntouched) {
    Context ctx;
    put(ctx, "s", "{'n': 10}");
    EXPECT_EQ(runError(ctx, "s.n += 'а';").code, CS::ErrorCode::Type);
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 10.0);
}

TEST(EvalCompound, TargetCheckLosesToValueErrorWhenBothFail) {
    Context ctx;
    put(ctx, "items", "[1, 2, 3]");
    // §7.3 говорит про результат ('x += e' есть 'x = x + e'), а не про
    // порядок проверок: цель проверяется после вычисления правой части
    // (docs/semantics.md §7.2). Индекс -1 сам по себе дал бы Range
    // (FractionalAndNegativeIndicesAreErrors), но правая часть неисправна —
    // Type через 1 + 'a' — и она побеждает первой.
    EXPECT_EQ(runError(ctx, "items[-1] += 1 + 'a';").code, CS::ErrorCode::Type);
}

TEST(EvalCompound, DivisionByZeroFollowsIEEE) {
    Context ctx;
    put(ctx, "s", "{'n': 1}");
    // Деление на ноль даёт бесконечность, а не ошибку (§5.2).
    run(ctx, "s.n /= 0;");
    EXPECT_TRUE(std::isinf(evaluate(ctx, "s.n").numberValue()));
}

TEST(EvalCompound, DeepTargetWorks) {
    Context ctx;
    put(ctx, "state", "{'rows': [{'n': 1}]}");
    put(ctx, "i", "0");
    // Однократность вычисления цели (docs/grammar.md §6.4) в этом языке
    // ненаблюдаема: выражения чисты, поэтому повторное вычисление дало бы тот
    // же результат. Тест проверяет лишь, что сложная цель вообще работает;
    // само требование держится устройством кода, а не этой проверкой.
    run(ctx, "state.rows[i].n += 41;");
    EXPECT_EQ(evaluate(ctx, "state.rows[0].n").numberValue(), 42.0);
}

TEST(EvalScriptBehaviour, StatementsApplyInOrder) {
    Context ctx;
    put(ctx, "s", "{'n': 0}");
    // Каждый стейтмент читает результат предыдущего, поэтому 13 получается
    // только если применились все три и именно в этом порядке: пропуск первого
    // даёт 3, второго — 4, третьего — 10, перестановка — иное число.
    run(ctx, "s.n = s.n + 1; s.n = s.n * 10; s.n = s.n + 3;");
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 13.0);
}

TEST(EvalScriptBehaviour, LaterStatementsSeeEarlierWrites) {
    Context ctx;
    put(ctx, "s", "{'a': 1}");
    run(ctx, "s.b = s.a + 1; s.c = s.b + 1;");
    EXPECT_EQ(evaluate(ctx, "s.c").numberValue(), 3.0);
}

TEST(EvalScriptBehaviour, ErrorStopsTheScriptAndKeepsWhatWasDone) {
    Context ctx;
    put(ctx, "s", "{'a': 0, 'b': 0, 'c': 0}");
    // docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md: откатывать
    // нечего, предыдущих состояний хранилище не держит. Обработчик, упавший на
    // третьем присваивании из пяти, оставит первые два применёнными.
    //
    // Ошибка — Type через 1 + 'a', не неизвестное имя: статический проход
    // отсеял бы весь скрипт ещё до первого стейтмента, и «первые два
    // применились» стало бы неверно уже по другой причине.
    const Diagnostic diag =
        runError(ctx, "s.a = 1; s.b = 2; s.x = 1 + 'a'; s.c = 3;");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_EQ(evaluate(ctx, "s.a").numberValue(), 1.0);
    EXPECT_EQ(evaluate(ctx, "s.b").numberValue(), 2.0);
    EXPECT_EQ(evaluate(ctx, "s.c").numberValue(), 0.0);
    EXPECT_FALSE(ctx.objectHas(ctx.root("s"), "x"));
}

TEST(EvalScriptBehaviour, MutationIsVisibleThroughAnotherName) {
    Context ctx;
    // Хост кладёт один агрегат под двумя именами: значения — хендлы, поэтому
    // это тот же массив (docs/semantics.md §2.3).
    put(ctx, "state", "{'items': [1, 2]}");
    const Value items = ctx.objectGet(ctx.root("state"), "items");
    ctx.setRoot("shortcut", items);

    run(ctx, "state.items[0] = 99;");
    EXPECT_EQ(evaluate(ctx, "shortcut[0]").numberValue(), 99.0);
}

TEST(EvalScriptBehaviour, AssignmentCreatesAnAliasJustLikeTheHostDoes) {
    Context ctx;
    // §2.3, первое предложение: «присваивание... копии не создаёт». Здесь, в
    // отличие от MutationIsVisibleThroughAnotherName выше, второе имя для
    // массива ставит не хост, а само присваивание скрипта.
    put(ctx, "state", "{'a': [1, 2], 'b': null}");
    run(ctx, "state.b = state.a; state.a[0] = 9;");
    EXPECT_EQ(evaluate(ctx, "state.b[0]").numberValue(), 9.0);
}

TEST(EvalScriptBehaviour, SelfReferenceIsAValidProgram) {
    Context ctx;
    // §2.3: 'obj[\'self\'] = obj;' — корректная программа, ссылочность
    // допускает циклы. Запись не обходит значение и потому не зацикливается;
    // повторное чтение через self подтверждает, что цикл остался невредимым.
    put(ctx, "obj", "{}");
    // 'self' — зарезервированное слово (docs/grammar.md §4.5), поэтому чтение
    // назад идёт через '[]', как и запись; через '.' этот ключ недостижим.
    run(ctx, "obj['self'] = obj;");
    EXPECT_TRUE(evaluate(ctx, "obj['self'] == obj").booleanValue());
    EXPECT_TRUE(evaluate(ctx, "obj['self']['self']['self'] == obj").booleanValue());
}

TEST(EvalScriptBehaviour, EmptyStatementsAreSkipped) {
    Context ctx;
    put(ctx, "s", "{'n': 0}");
    run(ctx, ";; s.n = 1 ;;");
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 1.0);
}

TEST(EvalScriptBehaviour, DeepPathInsideAScript) {
    Context ctx;
    put(ctx, "state", "{'rows': [{'cells': [0]}, {'cells': [0]}]}");
    run(ctx, "state.rows[0].cells[0] = 1; state.rows[1].cells[0] = 2;");
    EXPECT_EQ(evaluate(ctx, "state.rows[0].cells[0]").numberValue(), 1.0);
    EXPECT_EQ(evaluate(ctx, "state.rows[1].cells[0]").numberValue(), 2.0);
}

TEST(EvalCall, CountOfEachKind) {
    Context ctx;
    put(ctx, "items", "[10, 20, 30]");
    put(ctx, "o", "{'a': 1, 'b': 2}");
    put(ctx, "empty", "[]");
    EXPECT_EQ(evaluate(ctx, "count(items)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(ctx, "count(o)").numberValue(), 2.0);
    EXPECT_EQ(evaluate(ctx, "count(empty)").numberValue(), 0.0);
}

TEST(EvalCall, CountOfStringCountsBytesNotCharacters) {
    Context ctx;
    // docs/semantics.md §8.1 явно: байты, а не символы.
    EXPECT_EQ(evaluate(ctx, "count('привет')").numberValue(), 12.0);
    EXPECT_EQ(evaluate(ctx, "count('😀')").numberValue(), 4.0);
    EXPECT_EQ(evaluate(ctx, "count('abc')").numberValue(), 3.0);
    EXPECT_EQ(evaluate(ctx, "count('')").numberValue(), 0.0);
}

TEST(EvalCall, CountRejectsScalarsOtherThanString) {
    Context ctx;
    EXPECT_EQ(evalError(ctx, "count(1)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "count(true)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "count(null)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, KeysReturnsEveryKey) {
    Context ctx;
    put(ctx, "o", "{'b': 1, 'a': 2, 'c': 3}");
    const Value keys = evaluate(ctx, "keys(o)");
    ASSERT_EQ(keys.kind(), Value::Kind::Array);
    ASSERT_EQ(ctx.arrayCount(keys), 3u);
    // Порядок docs/semantics.md §8.2 не определяет, поэтому тест собирает
    // множество, а не список: опираться на порядок значило бы обещать его.
    std::set<std::string> got;
    for (std::uint32_t i = 0; i < 3; ++i) {
        got.insert(std::string(ctx.string(ctx.arrayAt(keys, i))));
    }
    EXPECT_EQ(got, (std::set<std::string>{"a", "b", "c"}));
}

TEST(EvalCall, KeysOfEmptyObjectIsEmptyArray) {
    Context ctx;
    put(ctx, "o", "{}");
    EXPECT_EQ(ctx.arrayCount(evaluate(ctx, "keys(o)")), 0u);
}

TEST(EvalCall, KeysRejectsNonObjects) {
    Context ctx;
    put(ctx, "items", "[1]");
    EXPECT_EQ(evalError(ctx, "keys(items)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "keys('a')").code, CS::ErrorCode::Type);
}

TEST(EvalCall, HasDistinguishesAbsentFromNull) {
    Context ctx;
    put(ctx, "o", "{'present': 1, 'empty': null}");
    // docs/semantics.md §8.3: единственный способ их различить. Обе половины
    // обязательны, иначе правило вырождается.
    EXPECT_TRUE(evaluate(ctx, "has(o, 'present')").booleanValue());
    EXPECT_TRUE(evaluate(ctx, "has(o, 'empty')").booleanValue());
    EXPECT_FALSE(evaluate(ctx, "has(o, 'missing')").booleanValue());
    EXPECT_EQ(evaluate(ctx, "o.empty").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(ctx, "o.missing").kind(), Value::Kind::Null);
}

TEST(EvalCall, HasCoercesTheKey) {
    Context ctx;
    put(ctx, "o", "{'0': 'ноль', 'true': 'да'}");
    EXPECT_TRUE(evaluate(ctx, "has(o, 0)").booleanValue());
    EXPECT_TRUE(evaluate(ctx, "has(o, true)").booleanValue());
    EXPECT_EQ(evalError(ctx, "has(o, [1])").code, CS::ErrorCode::Type);
}

TEST(EvalCall, LastOfArrayAndOfEmpty) {
    Context ctx;
    put(ctx, "items", "[10, 20, 30]");
    put(ctx, "empty", "[]");
    EXPECT_EQ(evaluate(ctx, "last(items)").numberValue(), 30.0);
    // docs/semantics.md §8.4: на пустом — null, тогда как items[count-1] дал бы
    // ошибку. Обе половины в одном тесте.
    EXPECT_EQ(evaluate(ctx, "last(empty)").kind(), Value::Kind::Null);
    EXPECT_EQ(evalError(ctx, "empty[count(empty) - 1]").code,
              CS::ErrorCode::Range);
}

TEST(EvalCall, LastRejectsNonArrays) {
    Context ctx;
    put(ctx, "o", "{}");
    EXPECT_EQ(evalError(ctx, "last(o)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, NestedCallsWork) {
    Context ctx;
    put(ctx, "o", "{'a': 1, 'b': 2}");
    EXPECT_EQ(evaluate(ctx, "count(keys(o))").numberValue(), 2.0);
}

TEST(EvalCall, ArgumentsAreEvaluatedLeftToRight) {
    Context ctx;
    put(ctx, "items", "[1]");
    // Оба аргумента негодны, но по-разному: код ошибки называет, который из
    // них вычислялся первым.
    EXPECT_EQ(evalError(ctx, "has(items[-1], 2 + 'b')").code,
              CS::ErrorCode::Range);
}

TEST(EvalCall, VariadicFormatDoesNotOverflowTheArgumentBuffer) {
    Context ctx;
    // format проходит проверки с любым числом аргументов, а буфер ветки Call
    // рассчитан на два: аргументы обязаны не попадать в него вовсе.
    EXPECT_EQ(evalError(ctx, "format('${}${}${}${}${}', 1, 2, 3, 4, 5)").code,
              CS::ErrorCode::Type);
}

TEST(EvalCall, PushAppends) {
    Context ctx;
    put(ctx, "items", "[1, 2]");
    run(ctx, "push(items, 3);");
    EXPECT_EQ(evaluate(ctx, "count(items)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(ctx, "items[2]").numberValue(), 3.0);
}

TEST(EvalCall, PushIsTheOnlyWayToGrowAnArray) {
    Context ctx;
    put(ctx, "items", "[1]");
    // docs/semantics.md §6.1: запись за границу — ошибка, расширяет только push.
    EXPECT_EQ(runError(ctx, "items[1] = 2;").code, CS::ErrorCode::Range);
    run(ctx, "push(items, 2);");
    EXPECT_EQ(evaluate(ctx, "items[1]").numberValue(), 2.0);
}

TEST(EvalCall, PushRejectsNonArrays) {
    Context ctx;
    put(ctx, "o", "{}");
    EXPECT_EQ(runError(ctx, "push(o, 1);").code, CS::ErrorCode::Type);
}

TEST(EvalCall, PopRemovesAndDoesNothingOnEmpty) {
    Context ctx;
    put(ctx, "items", "[1, 2]");
    put(ctx, "empty", "[]");
    run(ctx, "pop(items);");
    EXPECT_EQ(evaluate(ctx, "count(items)").numberValue(), 1.0);
    // docs/semantics.md §8.6: на пустом ничего не делает и не отказывает.
    run(ctx, "pop(empty);");
    EXPECT_EQ(evaluate(ctx, "count(empty)").numberValue(), 0.0);
}

TEST(EvalCall, TakingTheRemovedElementNeedsTwoSteps) {
    Context ctx;
    put(ctx, "state", "{'items': [1, 2, 3], 'taken': null}");
    // docs/semantics.md §8.6 показывает ровно эту пару: pop значения не
    // возвращает, читают его через last.
    run(ctx, "state.taken = last(state.items); pop(state.items);");
    EXPECT_EQ(evaluate(ctx, "state.taken").numberValue(), 3.0);
    EXPECT_EQ(evaluate(ctx, "count(state.items)").numberValue(), 2.0);
}

TEST(EvalCall, PushAndPopAreVisibleThroughAnAlias) {
    Context ctx;
    put(ctx, "state", "{'items': [1]}");
    const Value items = ctx.objectGet(ctx.root("state"), "items");
    ctx.setRoot("shortcut", items);
    // docs/semantics.md §2.3: мутация через один путь видна через второй.
    run(ctx, "push(state.items, 2);");
    EXPECT_EQ(evaluate(ctx, "count(shortcut)").numberValue(), 2.0);
}

TEST(EvalCall, StrConvertsScalars) {
    Context ctx;
    // docs/semantics.md §8.7 — правила §4.2 и §4.3.
    EXPECT_EQ(ctx.string(evaluate(ctx, "str(1)")), "1");
    EXPECT_EQ(ctx.string(evaluate(ctx, "str(0.5)")), "0.5");
    EXPECT_EQ(ctx.string(evaluate(ctx, "str(true)")), "true");
    EXPECT_EQ(ctx.string(evaluate(ctx, "str(false)")), "false");
    EXPECT_EQ(ctx.string(evaluate(ctx, "str(null)")), "null");
    EXPECT_EQ(ctx.string(evaluate(ctx, "str('уже строка')")), "уже строка");
}

TEST(EvalCall, StrRejectsAggregates) {
    Context ctx;
    put(ctx, "items", "[1]");
    put(ctx, "o", "{}");
    EXPECT_EQ(evalError(ctx, "str(items)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "str(o)").code, CS::ErrorCode::Type);
}

}  // namespace
