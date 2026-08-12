#include "eval.hpp"

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "ast.hpp"
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
    EXPECT_TRUE(CS::parseExpression(text.data(),
                                    static_cast<std::uint32_t>(text.size()), ast,
                                    diag))
        << diag.message;
    Value out = Value::null();
    EXPECT_TRUE(CS::evalExpression(ast, ctx, &out, diag)) << diag.message;
    return out;
}

/// Разбирает успешно, вычисляет с отказом; возвращает диагностику вычисления.
Diagnostic evalError(Context &ctx, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    EXPECT_TRUE(CS::parseExpression(text.data(),
                                    static_cast<std::uint32_t>(text.size()), ast,
                                    diag))
        << diag.message;
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

TEST(EvalUnsupported, CallsAreNotSupportedYet) {
    Context ctx;
    // Вызовы приходят с частью 3. После них в ветке default останутся только
    // Program, Assign и CallStatement — узлы, которых в дереве от
    // parseExpression быть не может, — и она станет защитной окончательно.
    const Diagnostic diag = evalError(ctx, "count(items)");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_STREQ(diag.message, "expression form is not supported");
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

TEST(EvalNames, UnknownRootIsAnError) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md §4:
    // опечатка в корне ловится, потому что состав корней контексту известен.
    const Diagnostic diag = evalError(ctx, "usre");
    EXPECT_EQ(diag.code, CS::ErrorCode::Name);
    EXPECT_EQ(diag.offset, 0u);
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

TEST(EvalMember, UnknownRootIsAnErrorAtAnyDepth) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // База вычисляется рекурсивно, поэтому опечатка в корне всплывает с любой
    // глубины пути: usre.a.b спускается к usre и упирается в неизвестный
    // корень. Частного случая для первого сегмента не нужно.
    EXPECT_EQ(evalError(ctx, "usre.name").code, CS::ErrorCode::Name);
    EXPECT_EQ(evalError(ctx, "usre.a.b").code, CS::ErrorCode::Name);
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
    EXPECT_EQ(evalError(ctx, "user.missing[usre]").code, CS::ErrorCode::Name);
}

TEST(EvalIndex, BaseIsEvaluatedBeforeTheSubscript) {
    Context ctx;
    // docs/semantics.md §3.3 фиксирует порядок именно ради определённости
    // диагностики, когда ошибочны оба операнда.
    const Diagnostic diag = evalError(ctx, "usre[alsoBad]");
    EXPECT_EQ(diag.code, CS::ErrorCode::Name);
    EXPECT_EQ(diag.offset, 0u);
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
    const Diagnostic diag = evalError(ctx, "[usre, alsoBad]");
    EXPECT_EQ(diag.code, CS::ErrorCode::Name);
    EXPECT_LT(diag.offset, 6u);
}

TEST(EvalDepth, ChainAtTheParserLimitEvaluatesWithoutOverflow) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // Вычислитель спускается по дереву рекурсивно и тратит кадр на звено.
    // Собственного предела у него нет — его даёт парсер, но только потому, что
    // звено цепочки стоит той же единицы, что и вложенность. До этой правки
    // цепочка длины не имела, разбиралась целиком и роняла процесс.
    //
    // 93 — измеренный максимум для цепочки '.' в выражении
    // (docs/grammar.md Приложение C.1). Чтение идёт через null по §6.3, то есть
    // все звенья действительно проходятся.
    std::string source = "user";
    for (int i = 0; i < 93; ++i) {
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

TEST(EvalAggregates, EachEvaluationCreatesANewAggregate) {
    Context ctx;
    Ast ast;
    Diagnostic diag;
    const std::string_view text = "[1, 2]";
    ASSERT_TRUE(CS::parseExpression(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, diag));

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
    EXPECT_EQ(evalError(ctx, "usre + 1").code, CS::ErrorCode::Name);
}

TEST(EvalOperators, ErrorInTheRightOperandStopsEvaluation) {
    Context ctx;
    EXPECT_EQ(evalError(ctx, "1 + usre").code, CS::ErrorCode::Name);
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
    EXPECT_FALSE(evaluate(ctx, "false && usre").booleanValue());
}

TEST(EvalShortCircuit, AndEvaluatesTheRightOperandWhenNeeded) {
    Context ctx;
    EXPECT_FALSE(evaluate(ctx, "true && false").booleanValue());
    EXPECT_TRUE(evaluate(ctx, "true && true").booleanValue());
    EXPECT_EQ(evalError(ctx, "true && usre").code, CS::ErrorCode::Name);
}

TEST(EvalShortCircuit, OrDoesNotEvaluateTheRightOperand) {
    Context ctx;
    EXPECT_TRUE(evaluate(ctx, "true || usre").booleanValue());
}

TEST(EvalShortCircuit, OrEvaluatesTheRightOperandWhenNeeded) {
    Context ctx;
    EXPECT_TRUE(evaluate(ctx, "false || true").booleanValue());
    EXPECT_FALSE(evaluate(ctx, "false || false").booleanValue());
    EXPECT_EQ(evalError(ctx, "false || usre").code, CS::ErrorCode::Name);
}

TEST(EvalShortCircuit, ErrorOnTheLeftIsNotSwallowed) {
    Context ctx;
    // docs/semantics.md §5.5: ошибка && false — ошибка.
    EXPECT_EQ(evalError(ctx, "usre && false").code, CS::ErrorCode::Name);
    EXPECT_EQ(evalError(ctx, "usre || true").code, CS::ErrorCode::Name);
}

TEST(EvalShortCircuit, TypeOfTheUnevaluatedOperandIsNotChecked) {
    Context ctx;
    // Самая точная проверка правила: тип правого операнда проверяется тогда и
    // только тогда, когда его пришлось вычислить. Поодиночке ни одна из двух
    // строк ничего не доказывает.
    EXPECT_FALSE(evaluate(ctx, "false && 5").booleanValue());
    EXPECT_EQ(evalError(ctx, "true && 5").code, CS::ErrorCode::Type);
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
    EXPECT_EQ(evaluate(ctx, "1 ?? usre").numberValue(), 1.0);
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
    EXPECT_EQ(evalError(ctx, "usre ?? 0").code, CS::ErrorCode::Name);
    EXPECT_EQ(evalError(ctx, "(1 + 'a') ?? 0").code, CS::ErrorCode::Type);
    // И справа тоже: если левый null, правый вычисляется по-настоящему.
    EXPECT_EQ(evalError(ctx, "null ?? usre").code, CS::ErrorCode::Name);
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
    EXPECT_EQ(evaluate(ctx, "true ? 1 : usre").numberValue(), 1.0);
    EXPECT_EQ(evaluate(ctx, "false ? usre : 2").numberValue(), 2.0);
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

}  // namespace
