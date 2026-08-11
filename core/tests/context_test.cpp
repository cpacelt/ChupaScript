#include "context.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using CS::Context;
using CS::Value;

TEST(ContextString, RoundTripsBytes) {
    Context ctx;
    const Value v = ctx.makeString("привет");
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_EQ(ctx.string(v), "привет");
}

TEST(ContextString, EmptyStringIsEmptyView) {
    Context ctx;
    const Value v = ctx.makeString("");
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_TRUE(ctx.string(v).empty());
}

TEST(ContextString, LengthIsCountedInBytes) {
    Context ctx;
    // Шесть кириллических букв — двенадцать байт (semantics.md §2.1).
    EXPECT_EQ(ctx.string(ctx.makeString("привет")).size(), 12u);
}

TEST(ContextString, KeepsEmbeddedNulByte) {
    Context ctx;
    const std::string bytes("a\0b", 3);
    const Value v = ctx.makeString(bytes);
    EXPECT_EQ(ctx.string(v).size(), 3u);
    EXPECT_EQ(ctx.string(v)[1], '\0');
}

TEST(ContextString, EqualStringsAreStoredTwice) {
    Context ctx;
    const Value a = ctx.makeString("одинаково");
    const Value b = ctx.makeString("одинаково");
    EXPECT_EQ(ctx.string(a), ctx.string(b));
    // Дедупликации нет: второй экземпляр занял место (спека §6).
    EXPECT_NE(ctx.string(a).data(), ctx.string(b).data());
}

TEST(ContextString, AcceptsSliceOfItsOwnTextPool) {
    Context ctx;
    // Копирование строки, которая уже лежит в пуле: источник может переехать
    // прямо во время копирования, и наивный insert здесь был бы UB.
    Value seed = ctx.makeString("исходная строка");
    for (int i = 0; i < 64; ++i) {
        seed = ctx.makeString(ctx.string(seed));
    }
    EXPECT_EQ(ctx.string(seed), "исходная строка");
}

TEST(ContextMetrics, EmptyContextUsesNothing) {
    Context ctx;
    EXPECT_EQ(ctx.bytesUsed(), 0u);
}

TEST(ContextMetrics, StringAddsItsBytes) {
    Context ctx;
    const std::size_t before = ctx.bytesUsed();
    ctx.makeString("12345");
    EXPECT_EQ(ctx.bytesUsed(), before + 5u);
}

TEST(ContextArray, EmptyArrayHasNoElements) {
    Context ctx;
    const Value a = ctx.makeArray();
    EXPECT_EQ(a.kind(), Value::Kind::Array);
    EXPECT_EQ(ctx.arrayCount(a), 0u);
}

TEST(ContextArray, CapacityDoesNotCreateElements) {
    Context ctx;
    EXPECT_EQ(ctx.arrayCount(ctx.makeArray(16)), 0u);
}

TEST(ContextArray, ReadBeyondEndGivesNull) {
    Context ctx;
    const Value a = ctx.makeArray();
    // semantics.md §6.1: чтение за границей — штатная ситуация.
    EXPECT_EQ(ctx.arrayAt(a, 0).kind(), Value::Kind::Null);
    EXPECT_EQ(ctx.arrayAt(a, 1000).kind(), Value::Kind::Null);
}

TEST(ContextArray, WriteBeyondEndIsRefused) {
    Context ctx;
    const Value a = ctx.makeArray(8);
    // semantics.md §7.2: запись за границу — ошибка, ёмкость её не оправдывает.
    EXPECT_FALSE(ctx.arraySet(a, 0, Value::number(1.0)));
}

TEST(ContextArray, TwoEmptyArraysAreDistinct) {
    Context ctx;
    EXPECT_FALSE(ctx.makeArray().sameAggregate(ctx.makeArray()));
}

TEST(ContextArray, CopyOfValueIsTheSameArray) {
    Context ctx;
    const Value a = ctx.makeArray();
    const Value b = a;
    EXPECT_TRUE(a.sameAggregate(b));
}

}  // namespace
