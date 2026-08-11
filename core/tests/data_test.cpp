#include "data.hpp"

#include <cmath>
#include <gtest/gtest.h>
#include <string>

#include "context.hpp"
#include "diagnostic.hpp"

namespace {

using CS::Context;
using CS::Diagnostic;
using CS::ErrorCode;
using CS::Value;

/// Кладёт значение и требует успеха; возвращает то, что легло.
Value put(Context &ctx, std::string_view name, std::string_view text) {
    Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(ctx, name, text, diag)) << diag.message;
    return ctx.root(name);
}

TEST(DataScalars, NumberIsStored) {
    Context ctx;
    EXPECT_EQ(put(ctx, "count", "3").numberValue(), 3.0);
    EXPECT_EQ(put(ctx, "ratio", "0.5").numberValue(), 0.5);
}

TEST(DataScalars, BooleanIsStored) {
    Context ctx;
    EXPECT_TRUE(put(ctx, "on", "true").booleanValue());
    EXPECT_FALSE(put(ctx, "off", "false").booleanValue());
}

TEST(DataScalars, NullIsStored) {
    Context ctx;
    EXPECT_EQ(put(ctx, "nothing", "null").kind(), Value::Kind::Null);
    EXPECT_TRUE(ctx.hasRoot("nothing"));
}

TEST(DataNames, IdentifierIsAccepted) {
    Context ctx;
    Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(ctx, "user_2", "1", diag));
    EXPECT_TRUE(CS::setVariable(ctx, "_private", "1", diag));
}

TEST(DataNames, NonIdentifierIsRejected) {
    Context ctx;
    Diagnostic diag;
    // Корень, который программа не может написать, бесполезен.
    EXPECT_FALSE(CS::setVariable(ctx, "content-type", "1", diag));
    EXPECT_EQ(diag.code, ErrorCode::Name);
    EXPECT_FALSE(CS::setVariable(ctx, "2fa", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, "", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, " state", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, "state ", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, "имя", "1", diag));
    EXPECT_EQ(ctx.rootCount(), 0u);
}

TEST(DataNames, ReservedWordIsRejected) {
    Context ctx;
    Diagnostic diag;
    // docs/grammar.md §4.5: ключевое слово идентификатором не является.
    EXPECT_FALSE(CS::setVariable(ctx, "null", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, "true", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, "while", "1", diag));
    EXPECT_EQ(ctx.rootCount(), 0u);
}

TEST(DataFailure, SyntaxErrorLeavesNoRoot) {
    Context ctx;
    Diagnostic diag;
    EXPECT_FALSE(CS::setVariable(ctx, "broken", "3 3", diag));
    EXPECT_EQ(diag.code, ErrorCode::Syntax);
    EXPECT_FALSE(ctx.hasRoot("broken"));
    EXPECT_EQ(ctx.rootCount(), 0u);
}

TEST(DataStrings, BothQuoteFormsAreAccepted) {
    Context ctx;
    // docs/grammar.md §A: обе формы равноправны и дают одинаковые значения.
    EXPECT_EQ(ctx.string(put(ctx, "a", "'Вася'")), "Вася");
    EXPECT_EQ(ctx.string(put(ctx, "b", "\"Вася\"")), "Вася");
}

TEST(DataStrings, EmptyStringIsAccepted) {
    Context ctx;
    const Value v = put(ctx, "empty", "''");
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_TRUE(ctx.string(v).empty());
}

TEST(DataStrings, BytesArriveAlreadyUnescapedByTheHost) {
    Context ctx;
    // Внешний JSON снимает хост, до нас доезжают настоящие байты UTF-8.
    // Шесть кириллических букв — двенадцать байт.
    EXPECT_EQ(ctx.string(put(ctx, "greet", "'привет'")).size(), 12u);
}

TEST(DataMinus, NegativeNumberIsAccepted) {
    Context ctx;
    // Знака в NumericLiteral нет, -3 приезжает узлом Unary над Number.
    EXPECT_EQ(put(ctx, "below", "-3").numberValue(), -3.0);
    EXPECT_EQ(put(ctx, "half", "-0.5").numberValue(), -0.5);
}

TEST(DataMinus, NegativeZeroKeepsItsSign) {
    Context ctx;
    // docs/semantics.md §2.1 включает отрицательный ноль в модель значений.
    EXPECT_TRUE(std::signbit(put(ctx, "zero", "-0").numberValue()));
}

TEST(DataMinus, MinusOverNonNumberIsRejected) {
    Context ctx;
    Diagnostic diag;
    EXPECT_FALSE(CS::setVariable(ctx, "bad", "-'abc'", diag));
    EXPECT_EQ(diag.code, ErrorCode::Data);
    EXPECT_FALSE(CS::setVariable(ctx, "worse", "!true", diag));
    EXPECT_EQ(diag.code, ErrorCode::Data);
    EXPECT_EQ(ctx.rootCount(), 0u);
}

TEST(DataEscapes, NewlineIsDecoded) {
    Context ctx;
    const std::string_view text = ctx.string(put(ctx, "s", "'a\\nb'"));
    ASSERT_EQ(text.size(), 3u);
    EXPECT_EQ(text[1], '\n');
}

TEST(DataEscapes, TabIsDecoded) {
    Context ctx;
    const std::string_view text = ctx.string(put(ctx, "s", "'a\\tb'"));
    ASSERT_EQ(text.size(), 3u);
    EXPECT_EQ(text[1], '\t');
}

TEST(DataEscapes, BackslashIsDecoded) {
    Context ctx;
    const std::string_view text = ctx.string(put(ctx, "s", "'a\\\\b'"));
    ASSERT_EQ(text.size(), 3u);
    EXPECT_EQ(text[1], '\\');
}

TEST(DataEscapes, BothQuotesAreDecoded) {
    Context ctx;
    EXPECT_EQ(ctx.string(put(ctx, "a", "'a\\'b'")), "a'b");
    EXPECT_EQ(ctx.string(put(ctx, "b", "\"a\\\"b\"")), "a\"b");
}

TEST(DataEscapes, EscapeAtBothEndsIsDecoded) {
    Context ctx;
    EXPECT_EQ(ctx.string(put(ctx, "s", "'\\n\\t'")), "\n\t");
}

TEST(DataEscapes, UnicodeEscapeIsRejectedByTheLexer) {
    Context ctx;
    Diagnostic diag;
    // docs/grammar.md §11: юникодных escape в языке нет — внешний уровень
    // снимает хост, и до нас доезжают готовые байты.
    EXPECT_FALSE(CS::setVariable(ctx, "s", "'\\u0041'", diag));
    EXPECT_EQ(diag.code, ErrorCode::Syntax);
}

TEST(DataAggregates, ArrayKeepsOrder) {
    Context ctx;
    const Value a = put(ctx, "items", "[1, 2, 3]");
    ASSERT_EQ(ctx.arrayCount(a), 3u);
    EXPECT_EQ(ctx.arrayAt(a, 0).numberValue(), 1.0);
    EXPECT_EQ(ctx.arrayAt(a, 2).numberValue(), 3.0);
}

TEST(DataAggregates, EmptyArrayAndObject) {
    Context ctx;
    EXPECT_EQ(ctx.arrayCount(put(ctx, "a", "[]")), 0u);
    EXPECT_EQ(ctx.objectCount(put(ctx, "o", "{}")), 0u);
}

TEST(DataAggregates, ObjectStoresKeys) {
    Context ctx;
    const Value o = put(ctx, "user", "{\"name\": \"Вася\", \"age\": 30}");
    ASSERT_EQ(ctx.objectCount(o), 2u);
    EXPECT_EQ(ctx.string(ctx.objectGet(o, "name")), "Вася");
    EXPECT_EQ(ctx.objectGet(o, "age").numberValue(), 30.0);
}

TEST(DataAggregates, KeyWithEscapeIsDecoded) {
    Context ctx;
    const Value o = put(ctx, "o", "{'a\\nb': 1}");
    EXPECT_TRUE(ctx.objectHas(o, "a\nb"));
}

TEST(DataAggregates, LastDuplicateKeyWins) {
    Context ctx;
    // Бэкенд отсутствия дубликатов не гарантирует; поведение определено.
    const Value o = put(ctx, "o", "{'k': 1, 'k': 2}");
    EXPECT_EQ(ctx.objectCount(o), 1u);
    EXPECT_EQ(ctx.objectGet(o, "k").numberValue(), 2.0);
}

TEST(DataAggregates, NestingWorks) {
    Context ctx;
    const Value o = put(ctx, "state", "{'items': [{'id': 1}, {'id': 2}]}");
    const Value items = ctx.objectGet(o, "items");
    ASSERT_EQ(ctx.arrayCount(items), 2u);
    EXPECT_EQ(ctx.objectGet(ctx.arrayAt(items, 1), "id").numberValue(), 2.0);
}

TEST(DataAggregates, NegativeNumbersInsideAggregates) {
    Context ctx;
    const Value a = put(ctx, "a", "[-1, -2.5]");
    EXPECT_EQ(ctx.arrayAt(a, 0).numberValue(), -1.0);
    EXPECT_EQ(ctx.arrayAt(a, 1).numberValue(), -2.5);
}

TEST(DataAggregates, ExactCapacityLeavesNoGarbage) {
    Context ctx;
    std::string text = "[";
    for (int i = 0; i < 100; ++i) {
        if (i > 0) { text += ", "; }
        text += std::to_string(i);
    }
    text += "]";

    const std::size_t before = ctx.bytesUsed();
    const Value a = put(ctx, "hundred", text);
    ASSERT_EQ(ctx.arrayCount(a), 100u);
    // Прирост — сто слотов плюс заголовок массива, пара корня и байты его
    // имени; запас до ста десяти слотов это покрывает. При удвоении вместо
    // точного размера ушло бы сто двадцать восемь слотов, и порог не прошёл бы:
    // размер известен заранее, поэтому переездов при построении нет.
    EXPECT_LT(ctx.bytesUsed() - before, 110u * sizeof(Value));
}

TEST(DataAggregates, ExpressionInsideAggregateIsRejected) {
    Context ctx;
    Diagnostic diag;
    EXPECT_FALSE(CS::setVariable(ctx, "a", "[1, count(x), 3]", diag));
    EXPECT_EQ(diag.code, ErrorCode::Data);
    EXPECT_FALSE(ctx.hasRoot("a"));
}

/// Требует отказа и возвращает диагностику.
Diagnostic reject(Context &ctx, std::string_view text) {
    Diagnostic diag;
    EXPECT_FALSE(CS::setVariable(ctx, "v", text, diag));
    return diag;
}

TEST(DataRejects, IdentifierIsNotData) {
    Context ctx;
    EXPECT_EQ(reject(ctx, "user").code, ErrorCode::Data);
}

TEST(DataRejects, CallIsNotData) {
    Context ctx;
    EXPECT_EQ(reject(ctx, "count(items)").code, ErrorCode::Data);
}

TEST(DataRejects, BinaryIsNotData) {
    Context ctx;
    EXPECT_EQ(reject(ctx, "1 + 1").code, ErrorCode::Data);
}

TEST(DataRejects, MemberIsNotData) {
    Context ctx;
    EXPECT_EQ(reject(ctx, "user.name").code, ErrorCode::Data);
}

TEST(DataRejects, IndexIsNotData) {
    Context ctx;
    EXPECT_EQ(reject(ctx, "items[0]").code, ErrorCode::Data);
}

TEST(DataRejects, ConditionalIsNotData) {
    Context ctx;
    EXPECT_EQ(reject(ctx, "a ? 1 : 2").code, ErrorCode::Data);
}

TEST(DataRejects, OffsetPointsAtTheOffendingNode) {
    Context ctx;
    // Ошибка внутри массива обязана указывать на место выражения, а не на
    // начало текста: иначе хост не покажет, где именно чинить.
    const Diagnostic diag = reject(ctx, "[1, user.name, 3]");
    EXPECT_EQ(diag.code, ErrorCode::Data);
    EXPECT_GT(diag.offset, 3u);
}

TEST(DataRejects, TrailingBytesAreRejected) {
    Context ctx;
    // Текст обязан быть значением целиком.
    EXPECT_EQ(reject(ctx, "1 2").code, ErrorCode::Syntax);
    EXPECT_EQ(reject(ctx, "[1] [2]").code, ErrorCode::Syntax);
}

TEST(DataRejects, ExponentIsNotANumber) {
    Context ctx;
    // docs/grammar.md §11: экспоненты в языке нет, 1e3 — два токена.
    // Единственное расхождение с JSON, переживающее границу.
    EXPECT_EQ(reject(ctx, "1e3").code, ErrorCode::Syntax);
}

TEST(DataRejects, FailedSetLeavesPreviousValueIntact) {
    Context ctx;
    Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(ctx, "v", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, "v", "user.name", diag));
    // Отказ не трогает того, что уже лежало.
    EXPECT_EQ(ctx.root("v").numberValue(), 1.0);
    EXPECT_EQ(ctx.rootCount(), 1u);
}

}  // namespace
