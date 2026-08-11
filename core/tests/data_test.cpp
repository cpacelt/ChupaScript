#include "data.hpp"

#include <cmath>
#include <gtest/gtest.h>

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

}  // namespace
