#include "printer.hpp"

#include <gtest/gtest.h>

#include <string>
#include <string_view>
#include <vector>

#include "data.hpp"
#include "diagnostic.hpp"
#include "store.hpp"

namespace {

using CS::Store;
using CS::Diagnostic;
using CS::Value;

/// Кладёт переменную и возвращает её значение.
Value put(Store &store, std::string_view name, std::string_view text) {
    Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(store, name, text, diag)) << diag.message;
    return store.global(name);
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
    Store &store = ctx.store();
    // Кавычки обязательны: при строгой типизации отличить 1 от '1' глазами —
    // половина отладки выражения.
    EXPECT_EQ(chupa::printValue(ctx, put(store, "s", "'привет'")), "'привет'");
    EXPECT_EQ(chupa::printValue(ctx, put(store, "e", "''")), "''");
}

TEST(PrintValue, StringsAreEscapedBackToSource) {
    CS::Context ctx;
    Store &store = ctx.store();
    // Напечатанное обязано быть тем, что можно набрать обратно: набор
    // escape-последовательностей из docs/grammar.md §4.7.
    EXPECT_EQ(chupa::printValue(ctx, put(store, "q", "'it\\'s'")), "'it\\'s'");
    EXPECT_EQ(chupa::printValue(ctx, put(store, "b", "'a\\\\b'")), "'a\\\\b'");
    EXPECT_EQ(chupa::printValue(ctx, put(store, "n", "'a\\nb'")), "'a\\nb'");
    EXPECT_EQ(chupa::printValue(ctx, put(store, "t", "'a\\tb'")), "'a\\tb'");
}

TEST(PrintValue, EmptyAggregates) {
    CS::Context ctx;
    Store &store = ctx.store();
    EXPECT_EQ(chupa::printValue(ctx, put(store, "a", "[]")), "[]");
    EXPECT_EQ(chupa::printValue(ctx, put(store, "o", "{}")), "{}");
}

TEST(PrintValue, ArraysAndObjects) {
    CS::Context ctx;
    Store &store = ctx.store();
    EXPECT_EQ(chupa::printValue(ctx, put(store, "a", "[1, 2, 3]")), "[1, 2, 3]");
    EXPECT_EQ(chupa::printValue(ctx, put(store, "o", "{'a': 1}")), "{'a': 1}");
}

TEST(PrintValue, NestedAggregates) {
    CS::Context ctx;
    Store &store = ctx.store();
    const Value v = put(store, "v", "{'items': [1, {'k': null}], 'ok': true}");
    // Ключи объекта хранятся отсортированными, поэтому порядок предсказуем.
    EXPECT_EQ(chupa::printValue(ctx, v),
              "{'items': [1, {'k': null}], 'ok': true}");
}

TEST(PrintValue, SelfReferencingObjectTerminates) {
    CS::Context ctx;
    Store &store = ctx.store();
    const Value o = put(store, "o", "{'n': 1}");
    store.objectSet(o, "self", o);
    // docs/semantics.md §2.3 объявляет такую программу корректной; печатник
    // обязан завершиться, а не зациклиться.
    EXPECT_EQ(chupa::printValue(ctx, o), "{'n': 1, 'self': {...}}");
}

TEST(PrintValue, SelfReferencingArrayTerminates) {
    CS::Context ctx;
    Store &store = ctx.store();
    const Value a = put(store, "a", "[1]");
    store.arrayPush(a, a);
    EXPECT_EQ(chupa::printValue(ctx, a), "[1, [...]]");
}

TEST(PrintValue, SharedAggregateIsPrintedInFullTwice) {
    CS::Context ctx;
    Store &store = ctx.store();
    const Value shared = put(store, "shared", "[1, 2]");
    const Value holder = put(store, "holder", "{}");
    store.objectSet(holder, "a", shared);
    store.objectSet(holder, "b", shared);
    // Один агрегат под двумя ключами — не цикл. Отслеживается путь печати, а
    // не всё виденное, поэтому оба вхождения печатаются целиком.
    EXPECT_EQ(chupa::printValue(ctx, holder), "{'a': [1, 2], 'b': [1, 2]}");
}

TEST(PrintValue, DeepNonCyclicTreeIsTruncated) {
    CS::Context ctx;
    Store &store = ctx.store();
    // Не цикл: каждый массив содержит следующий ровно один раз, ни один
    // агрегат не встречается на собственном пути печати. Путь его не
    // поймает — глубина ловится отдельным счётчиком (cli/printer.cpp,
    // kMaxPrintDepth). Без него печать такого дерева переполнила бы стек.
    constexpr int kCount = 200;
    std::vector<Value> arrays;
    arrays.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        arrays.push_back(put(store, "r" + std::to_string(i), "[]"));
    }
    for (int i = 0; i + 1 < kCount; ++i) {
        store.arrayPush(arrays[static_cast<std::size_t>(i)],
                      arrays[static_cast<std::size_t>(i + 1)]);
    }
    const std::string printed = chupa::printValue(ctx, arrays[0]);
    // Печать обязана завершиться (не упасть по стеку) и оборваться меткой.
    EXPECT_NE(printed.find("[...]"), std::string::npos);
}

TEST(PrintValue, MutualCycleTerminates) {
    CS::Context ctx;
    Store &store = ctx.store();
    const Value a = put(store, "a", "{}");
    const Value b = put(store, "b", "{}");
    store.objectSet(a, "b", b);
    store.objectSet(b, "a", a);
    // Цикл длиной два: путь ловит и его.
    EXPECT_EQ(chupa::printValue(ctx, a), "{'b': {'a': {...}}}");
}

}  // namespace
