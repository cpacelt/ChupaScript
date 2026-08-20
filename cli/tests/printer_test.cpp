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
    // Агрегат собран напрямую через Store, а не через put(): все мутации
    // ниже обязаны идти по одной настоящей ленте (store.clock()), а не по
    // второй, независимо заведённой, — иначе эпоха коробки способна
    // понизиться относительно уже выданных номеров (epoch.hpp: «номер,
    // выданный позже, всегда больше выданного раньше»).
    CS::Deferred dead;
    CS::Store store;
    CS::Context ctx;
    const Value o = CS::makeObject(store.keys(), 2, store.clock(), dead);
    CS::objectSet(o, "n", Value::number(1.0), store.clock(), dead);
    CS::objectSet(o, "self", o, store.clock(), dead);
    // docs/semantics.md §2.3 объявляет такую программу корректной; печатник
    // обязан завершиться, а не зациклиться.
    EXPECT_EQ(chupa::printValue(ctx, o), "{'n': 1, 'self': {...}}");
    // Cycle broken through the language's own means (null over the field), so
    // the box does not outlive this test (tools/asan.sh runs under LeakSanitizer).
    CS::objectSet(o, "self", Value::null(), store.clock(), dead);
}

TEST(PrintValue, SelfReferencingArrayTerminates) {
    CS::Deferred dead;
    CS::Store store;
    CS::Context ctx;
    const Value a = CS::makeArray(2, store.clock(), dead);
    CS::arrayPush(a, Value::number(1.0), store.clock());
    CS::arrayPush(a, a, store.clock());
    EXPECT_EQ(chupa::printValue(ctx, a), "[1, [...]]");
    // Cycle broken through the language's own means (pop), so the box does
    // not outlive this test (tools/asan.sh runs under LeakSanitizer).
    Value taken = Value::null();
    ASSERT_TRUE(CS::arrayPop(a, &taken, store.clock(), dead));
}

TEST(PrintValue, SharedAggregateIsPrintedInFullTwice) {
    CS::Deferred dead;
    CS::Store store;
    CS::Context ctx;
    const Value shared = CS::makeArray(2, store.clock(), dead);
    CS::arrayPush(shared, Value::number(1.0), store.clock());
    CS::arrayPush(shared, Value::number(2.0), store.clock());
    const Value holder = CS::makeObject(store.keys(), 2, store.clock(), dead);
    CS::objectSet(holder, "a", shared, store.clock(), dead);
    CS::objectSet(holder, "b", shared, store.clock(), dead);
    // Один агрегат под двумя ключами — не цикл. Отслеживается путь печати, а
    // не всё виденное, поэтому оба вхождения печатаются целиком.
    EXPECT_EQ(chupa::printValue(ctx, holder), "{'a': [1, 2], 'b': [1, 2]}");
}

TEST(PrintValue, DeepNonCyclicTreeIsTruncated) {
    // Не цикл: каждый массив содержит следующий ровно один раз, ни один
    // агрегат не встречается на собственном пути печати. Путь его не
    // поймает — глубина ловится отдельным счётчиком (cli/printer.cpp,
    // kMaxPrintDepth). Без него печать такого дерева переполнила бы стек.
    CS::Deferred dead;
    CS::Store store;
    CS::Context ctx;
    constexpr int kCount = 200;
    std::vector<Value> arrays;
    arrays.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        arrays.push_back(CS::makeArray(1, store.clock(), dead));
    }
    for (int i = 0; i + 1 < kCount; ++i) {
        CS::arrayPush(arrays[static_cast<std::size_t>(i)],
                      arrays[static_cast<std::size_t>(i + 1)], store.clock());
    }
    const std::string printed = chupa::printValue(ctx, arrays[0]);
    // Печать обязана завершиться (не упасть по стеку) и оборваться меткой.
    EXPECT_NE(printed.find("[...]"), std::string::npos);
}

TEST(PrintValue, MutualCycleTerminates) {
    CS::Deferred dead;
    CS::Store store;
    CS::Context ctx;
    const Value a = CS::makeObject(store.keys(), 1, store.clock(), dead);
    const Value b = CS::makeObject(store.keys(), 1, store.clock(), dead);
    CS::objectSet(a, "b", b, store.clock(), dead);
    CS::objectSet(b, "a", a, store.clock(), dead);
    // Цикл длиной два: путь ловит и его.
    EXPECT_EQ(chupa::printValue(ctx, a), "{'b': {'a': {...}}}");
    // Cycle broken through the language's own means (null over the field), so
    // the boxes do not outlive this test (tools/asan.sh runs under LeakSanitizer).
    CS::objectSet(b, "a", Value::null(), store.clock(), dead);
}

}  // namespace
