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

TEST(EvalUnsupported, OperatorsAreNotSupportedYet) {
    Context ctx;
    // Часть 1 не знает операторов и вызовов; парсер их принимает, вычислитель
    // отвергает узнаваемым сообщением.
    const Diagnostic diag = evalError(ctx, "1 + 1");
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

}  // namespace
