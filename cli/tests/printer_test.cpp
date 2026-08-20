#include "printer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "context.hpp"
#include "diagnostic.hpp"
#include "aggregate.hpp"

namespace {

using CS::Diagnostic;
using CS::Value;

/// Кладёт переменную и возвращает её значение.
Value put(CS::Context &ctx, std::string_view name, std::string_view text) {
    Diagnostic diag;
    EXPECT_TRUE(ctx.setVariableText(name, text, diag)) << diag.message;
    return ctx.store().global(name);
}

TEST(PrintValue, Scalars) {
    CS::Context ctx;
    EXPECT_EQ(chupa::printValue(ctx, Value::null()), "null");
    EXPECT_EQ(chupa::printValue(ctx, Value::boolean(true)), "true");
    EXPECT_EQ(chupa::printValue(ctx, Value::boolean(false)), "false");
    EXPECT_EQ(chupa::printValue(ctx, Value::number(42)), "42");
    EXPECT_EQ(chupa::printValue(ctx, Value::number(0.5)), "0.5");
    EXPECT_EQ(chupa::printValue(ctx, Value::number(-0.0)), "-0");
}

TEST(PrintValue, StringsAreQuoted) {
    CS::Context ctx;
    // Кавычки обязательны: при строгой типизации отличить 1 от '1' глазами —
    // половина отладки выражения.
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "s", "'привет'")), "'привет'");
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "e", "''")), "''");
}

TEST(PrintValue, StringsAreEscapedBackToSource) {
    CS::Context ctx;
    // Напечатанное обязано быть тем, что можно набрать обратно: набор
    // escape-последовательностей из docs/grammar.md §4.7.
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "q", "'it\\'s'")), "'it\\'s'");
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "b", "'a\\\\b'")), "'a\\\\b'");
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "n", "'a\\nb'")), "'a\\nb'");
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "t", "'a\\tb'")), "'a\\tb'");
}

TEST(PrintValue, EmptyAggregates) {
    CS::Context ctx;
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "a", "[]")), "[]");
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "o", "{}")), "{}");
}

TEST(PrintValue, ArraysAndObjects) {
    CS::Context ctx;
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "a", "[1, 2, 3]")), "[1, 2, 3]");
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "o", "{'a': 1}")), "{'a': 1}");
}

TEST(PrintValue, NestedAggregates) {
    CS::Context ctx;
    const Value v = put(ctx, "v", "{'items': [1, {'k': null}], 'ok': true}");
    // Ключи объекта хранятся отсортированными, поэтому порядок предсказуем.
    EXPECT_EQ(chupa::printValue(ctx, v),
              "{'items': [1, {'k': null}], 'ok': true}");
}

TEST(PrintValue, SelfReferencingObjectTerminates) {
    CS::Deferred dead;
    CS::Context ctx;
    const Value o = put(ctx, "o", "{'n': 1}");
    CS::objectSet(o, "self", o, dead);
    // docs/semantics.md §2.3 объявляет такую программу корректной; печатник
    // обязан завершиться, а не зациклиться.
    EXPECT_EQ(chupa::printValue(ctx, o), "{'n': 1, 'self': {...}}");
    // Cycle broken through the language's own means (null over the field), so
    // the box does not outlive this test (tools/asan.sh runs under LeakSanitizer).
    CS::objectSet(o, "self", Value::null(), dead);
}

TEST(PrintValue, SelfReferencingArrayTerminates) {
    CS::Deferred dead;
    CS::Context ctx;
    const Value a = put(ctx, "a", "[1]");
    CS::arrayPush(a, a);
    EXPECT_EQ(chupa::printValue(ctx, a), "[1, [...]]");
    // Cycle broken through the language's own means (pop), so the box does
    // not outlive this test (tools/asan.sh runs under LeakSanitizer).
    Value taken = Value::null();
    ASSERT_TRUE(CS::arrayPop(a, &taken, dead));
}

TEST(PrintValue, SharedAggregateIsPrintedInFullTwice) {
    CS::Deferred dead;
    CS::Context ctx;
    const Value shared = put(ctx, "shared", "[1, 2]");
    const Value holder = put(ctx, "holder", "{}");
    CS::objectSet(holder, "a", shared, dead);
    CS::objectSet(holder, "b", shared, dead);
    // Один агрегат под двумя ключами — не цикл. Отслеживается путь печати, а
    // не всё виденное, поэтому оба вхождения печатаются целиком.
    EXPECT_EQ(chupa::printValue(ctx, holder), "{'a': [1, 2], 'b': [1, 2]}");
}

TEST(PrintValue, DeepNonCyclicTreeIsTruncated) {
    CS::Context ctx;
    // Не цикл: каждый массив содержит следующий ровно один раз, ни один
    // агрегат не встречается на собственном пути печати. Путь его не
    // поймает — глубина ловится отдельным счётчиком (cli/printer.cpp,
    // kMaxPrintDepth). Без него печать такого дерева переполнила бы стек.
    constexpr int kCount = 200;
    std::vector<Value> arrays;
    arrays.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        arrays.push_back(put(ctx, "r" + std::to_string(i), "[]"));
    }
    for (int i = 0; i + 1 < kCount; ++i) {
        CS::arrayPush(arrays[static_cast<std::size_t>(i)],
                      arrays[static_cast<std::size_t>(i + 1)]);
    }
    const std::string printed = chupa::printValue(ctx, arrays[0]);
    // Печать обязана завершиться (не упасть по стеку) и оборваться меткой.
    EXPECT_NE(printed.find("[...]"), std::string::npos);
}

TEST(PrintValue, MutualCycleTerminates) {
    CS::Deferred dead;
    CS::Context ctx;
    const Value a = put(ctx, "a", "{}");
    const Value b = put(ctx, "b", "{}");
    CS::objectSet(a, "b", b, dead);
    CS::objectSet(b, "a", a, dead);
    // Цикл длиной два: путь ловит и его.
    EXPECT_EQ(chupa::printValue(ctx, a), "{'b': {'a': {...}}}");
    // Cycle broken through the language's own means (null over the field), so
    // the boxes do not outlive this test (tools/asan.sh runs under LeakSanitizer).
    CS::objectSet(b, "a", Value::null(), dead);
}

}  // namespace
