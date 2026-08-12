#include "operator.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "context.hpp"

namespace {

using CS::Context;
using CS::Diagnostic;
using CS::TokenKind;
using CS::Value;

/// Смещение, которое тесты передают в операции: любое, лишь бы узнаваемое.
constexpr std::uint32_t kOffset = 7;

Value number(double value) { return Value::number(value); }

/// Применяет бинарную операцию и требует успеха.
Value binary(TokenKind op, Value lhs, Value rhs, const Context &ctx) {
    Diagnostic diag;
    Value out = Value::null();
    EXPECT_TRUE(CS::applyBinary(op, lhs, rhs, ctx, kOffset, &out, diag))
        << diag.message;
    return out;
}

/// Применяет бинарную операцию и требует отказа; возвращает диагностику.
Diagnostic binaryError(TokenKind op, Value lhs, Value rhs, const Context &ctx) {
    Diagnostic diag;
    Value out = Value::null();
    EXPECT_FALSE(CS::applyBinary(op, lhs, rhs, ctx, kOffset, &out, diag));
    return diag;
}

/// Применяет унарную операцию и требует успеха.
Value unary(TokenKind op, Value operand) {
    Diagnostic diag;
    Value out = Value::null();
    EXPECT_TRUE(CS::applyUnary(op, operand, kOffset, &out, diag)) << diag.message;
    return out;
}

/// Применяет унарную операцию и требует отказа.
Diagnostic unaryError(TokenKind op, Value operand) {
    Diagnostic diag;
    Value out = Value::null();
    EXPECT_FALSE(CS::applyUnary(op, operand, kOffset, &out, diag));
    return diag;
}

TEST(OperatorUnary, BangNegatesBoolean) {
    EXPECT_FALSE(unary(TokenKind::Bang, Value::boolean(true)).booleanValue());
    EXPECT_TRUE(unary(TokenKind::Bang, Value::boolean(false)).booleanValue());
}

TEST(OperatorUnary, BangRequiresBoolean) {
    Context ctx;
    EXPECT_EQ(unaryError(TokenKind::Bang, number(1.0)).code, CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Bang, Value::null()).code, CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Bang, ctx.makeString("a")).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Bang, ctx.makeArray()).code,
              CS::ErrorCode::Type);
}

TEST(OperatorUnary, MinusNegatesNumber) {
    EXPECT_EQ(unary(TokenKind::Minus, number(3.0)).numberValue(), -3.0);
    EXPECT_EQ(unary(TokenKind::Minus, number(-3.0)).numberValue(), 3.0);
}

TEST(OperatorUnary, MinusOnNaNGivesNaN) {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    EXPECT_TRUE(std::isnan(unary(TokenKind::Minus, number(nan)).numberValue()));
}

TEST(OperatorUnary, MinusOnZeroGivesNegativeZero) {
    // docs/semantics.md §5.1. Получается само: это обычное отрицание double.
    EXPECT_TRUE(std::signbit(unary(TokenKind::Minus, number(0.0)).numberValue()));
}

TEST(OperatorUnary, MinusRequiresNumber) {
    Context ctx;
    EXPECT_EQ(unaryError(TokenKind::Minus, Value::boolean(true)).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Minus, Value::null()).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Minus, ctx.makeString("1")).code,
              CS::ErrorCode::Type);
}

TEST(OperatorArithmetic, FourOperationsWork) {
    Context ctx;
    EXPECT_EQ(binary(TokenKind::Plus, number(1.0), number(2.0), ctx).numberValue(), 3.0);
    EXPECT_EQ(binary(TokenKind::Minus, number(5.0), number(2.0), ctx).numberValue(), 3.0);
    EXPECT_EQ(binary(TokenKind::Star, number(3.0), number(4.0), ctx).numberValue(), 12.0);
    EXPECT_EQ(binary(TokenKind::Slash, number(9.0), number(2.0), ctx).numberValue(), 4.5);
}

TEST(OperatorArithmetic, RequiresNumbersOnBothSides) {
    Context ctx;
    const Value text = ctx.makeString("1");
    EXPECT_EQ(binaryError(TokenKind::Plus, number(1.0), text, ctx).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Plus, text, number(1.0), ctx).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Plus, number(1.0), Value::null(), ctx).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Plus, number(1.0), Value::boolean(true), ctx).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Star, ctx.makeArray(), number(2.0), ctx).code,
              CS::ErrorCode::Type);
}

TEST(OperatorArithmetic, ThereIsNoStringConcatenation) {
    Context ctx;
    // docs/semantics.md §5.2: строки собирает format, а не плюс. Это место,
    // где привычка из JavaScript обманывает чаще всего.
    EXPECT_EQ(binaryError(TokenKind::Plus, ctx.makeString("a"), ctx.makeString("b"), ctx).code,
              CS::ErrorCode::Type);
}

TEST(OperatorArithmetic, DivisionByZeroFollowsIEEE) {
    Context ctx;
    // docs/semantics.md §5.2: результат определён стандартом, ошибкой не
    // является. Исключение здесь означало бы, что неполные данные роняют экран.
    EXPECT_TRUE(std::isinf(binary(TokenKind::Slash, number(1.0), number(0.0), ctx).numberValue()));
    EXPECT_GT(binary(TokenKind::Slash, number(1.0), number(0.0), ctx).numberValue(), 0.0);
    EXPECT_LT(binary(TokenKind::Slash, number(-1.0), number(0.0), ctx).numberValue(), 0.0);
    EXPECT_TRUE(std::isnan(binary(TokenKind::Slash, number(0.0), number(0.0), ctx).numberValue()));
}

TEST(OperatorArithmetic, ModuloTakesTheSignOfTheDividend) {
    Context ctx;
    // docs/semantics.md §5.2: это std::fmod, а не остаток от целого деления.
    EXPECT_EQ(binary(TokenKind::Percent, number(7.0), number(3.0), ctx).numberValue(), 1.0);
    EXPECT_EQ(binary(TokenKind::Percent, number(-5.0), number(3.0), ctx).numberValue(), -2.0);
    EXPECT_EQ(binary(TokenKind::Percent, number(5.0), number(-3.0), ctx).numberValue(), 2.0);
    EXPECT_EQ(binary(TokenKind::Percent, number(5.5), number(2.0), ctx).numberValue(), 1.5);
}

TEST(OperatorArithmetic, ModuloByZeroGivesNaN) {
    Context ctx;
    EXPECT_TRUE(std::isnan(binary(TokenKind::Percent, number(5.0), number(0.0), ctx).numberValue()));
}

TEST(OperatorDiagnostics, OffsetIsCarriedThrough) {
    Context ctx;
    // Смещение — данные, а не зависимость от дерева: операция получает его
    // параметром и кладёт в диагностику как есть.
    EXPECT_EQ(binaryError(TokenKind::Plus, number(1.0), Value::null(), ctx).offset, kOffset);
    EXPECT_EQ(unaryError(TokenKind::Bang, number(1.0)).offset, kOffset);
}

}  // namespace
