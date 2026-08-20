#include "operator.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>

#include "store.hpp"
#include "aggregate.hpp"

namespace {

using CS::Store;
using CS::Diagnostic;
using CS::TokenKind;
using CS::Value;

/// Смещение, которое тесты передают в операции: любое, лишь бы узнаваемое.
constexpr std::uint32_t kOffset = 7;

Value number(double value) { return Value::number(value); }

/// Применяет бинарную операцию и требует успеха.
Value binary(TokenKind op, Value lhs, Value rhs) {
    Diagnostic diag;
    Value out = Value::null();
    EXPECT_TRUE(CS::applyBinary(op, lhs, rhs, kOffset, &out, diag))
        << diag.message;
    return out;
}

/// Применяет бинарную операцию и требует отказа; возвращает диагностику.
Diagnostic binaryError(TokenKind op, Value lhs, Value rhs) {
    Diagnostic diag;
    Value out = Value::null();
    EXPECT_FALSE(CS::applyBinary(op, lhs, rhs, kOffset, &out, diag));
    // operator.hpp обещает, что при отказе *out не трогается. Обещание
    // проверяется здесь, потому что на него опирается вычислитель: ?? и
    // тернарный передают out вызывающего внутрь.
    EXPECT_EQ(out.kind(), Value::Kind::Null);
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
    // То же обещание, что и у applyBinary.
    EXPECT_EQ(out.kind(), Value::Kind::Null);
    return diag;
}

TEST(OperatorUnary, BangNegatesBoolean) {
    EXPECT_FALSE(unary(TokenKind::Bang, Value::boolean(true)).booleanValue());
    EXPECT_TRUE(unary(TokenKind::Bang, Value::boolean(false)).booleanValue());
}

TEST(OperatorUnary, BangRequiresBoolean) {
    Store store;
    CS::Deferred dead;
    EXPECT_EQ(unaryError(TokenKind::Bang, number(1.0)).code, CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Bang, Value::null()).code, CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Bang, CS::materialize("a", dead)).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Bang, CS::makeArray(0, store.clock(), dead)).code,
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
    CS::Deferred dead;
    EXPECT_EQ(unaryError(TokenKind::Minus, Value::boolean(true)).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Minus, Value::null()).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Minus, CS::materialize("1", dead)).code,
              CS::ErrorCode::Type);
}

TEST(OperatorArithmetic, FourOperationsWork) {
    EXPECT_EQ(binary(TokenKind::Plus, number(1.0), number(2.0)).numberValue(), 3.0);
    EXPECT_EQ(binary(TokenKind::Minus, number(5.0), number(2.0)).numberValue(), 3.0);
    EXPECT_EQ(binary(TokenKind::Star, number(3.0), number(4.0)).numberValue(), 12.0);
    EXPECT_EQ(binary(TokenKind::Slash, number(9.0), number(2.0)).numberValue(), 4.5);
}

TEST(OperatorArithmetic, RequiresNumbersOnBothSides) {
    Store store;
    CS::Deferred dead;
    const Value text = CS::materialize("1", dead);
    EXPECT_EQ(binaryError(TokenKind::Plus, number(1.0), text).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Plus, text, number(1.0)).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Plus, number(1.0), Value::null()).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Plus, number(1.0), Value::boolean(true)).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Star, CS::makeArray(0, store.clock(), dead), number(2.0)).code,
              CS::ErrorCode::Type);
}

TEST(OperatorArithmetic, ThereIsNoStringConcatenation) {
    CS::Deferred dead;
    // docs/semantics.md §5.2: строки собирает format, а не плюс. Это место,
    // где привычка из JavaScript обманывает чаще всего.
    EXPECT_EQ(binaryError(TokenKind::Plus, CS::materialize("a", dead), CS::materialize("b", dead)).code,
              CS::ErrorCode::Type);
}

TEST(OperatorArithmetic, DivisionByZeroFollowsIEEE) {
    // docs/semantics.md §5.2: результат определён стандартом, ошибкой не
    // является. Исключение здесь означало бы, что неполные данные роняют экран.
    EXPECT_TRUE(std::isinf(binary(TokenKind::Slash, number(1.0), number(0.0)).numberValue()));
    EXPECT_GT(binary(TokenKind::Slash, number(1.0), number(0.0)).numberValue(), 0.0);
    EXPECT_LT(binary(TokenKind::Slash, number(-1.0), number(0.0)).numberValue(), 0.0);
    EXPECT_TRUE(std::isnan(binary(TokenKind::Slash, number(0.0), number(0.0)).numberValue()));
}

TEST(OperatorArithmetic, ModuloTakesTheSignOfTheDividend) {
    // docs/semantics.md §5.2: это std::fmod, а не остаток от целого деления.
    EXPECT_EQ(binary(TokenKind::Percent, number(7.0), number(3.0)).numberValue(), 1.0);
    EXPECT_EQ(binary(TokenKind::Percent, number(-5.0), number(3.0)).numberValue(), -2.0);
    EXPECT_EQ(binary(TokenKind::Percent, number(5.0), number(-3.0)).numberValue(), 2.0);
    EXPECT_EQ(binary(TokenKind::Percent, number(5.5), number(2.0)).numberValue(), 1.5);
}

TEST(OperatorArithmetic, ModuloByZeroGivesNaN) {
    EXPECT_TRUE(std::isnan(binary(TokenKind::Percent, number(5.0), number(0.0)).numberValue()));
}

TEST(OperatorDiagnostics, OffsetIsCarriedThrough) {
    // Смещение — данные, а не зависимость от дерева: операция получает его
    // параметром и кладёт в диагностику как есть.
    EXPECT_EQ(binaryError(TokenKind::Plus, number(1.0), Value::null()).offset, kOffset);
    EXPECT_EQ(unaryError(TokenKind::Bang, number(1.0)).offset, kOffset);
}

TEST(OperatorOrdering, FourOperatorsWork) {
    EXPECT_TRUE(binary(TokenKind::Less, number(1.0), number(2.0)).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Less, number(2.0), number(1.0)).booleanValue());
    EXPECT_TRUE(binary(TokenKind::Greater, number(2.0), number(1.0)).booleanValue());
    EXPECT_TRUE(binary(TokenKind::LessEqual, number(1.0), number(1.0)).booleanValue());
    EXPECT_TRUE(binary(TokenKind::GreaterEqual, number(1.0), number(1.0)).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Less, number(1.0), number(1.0)).booleanValue());
}

TEST(OperatorOrdering, RequiresNumbers) {
    Store store;
    CS::Deferred dead;
    // docs/semantics.md §5.3: строки, логические значения и агрегаты
    // сравнивать нельзя. Побайтовый порядок строк не соответствует
    // алфавитному, а порядок, зависящий от языка, — это коллация, которой
    // в рантайме нет.
    EXPECT_EQ(binaryError(TokenKind::Less, CS::materialize("a", dead), CS::materialize("b", dead)).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Less, Value::boolean(false), Value::boolean(true)).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Less, Value::null(), number(1.0)).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Greater, CS::makeArray(0, store.clock(), dead), CS::makeArray(0, store.clock(), dead)).code,
              CS::ErrorCode::Type);
    // Смешанная пара: число со строкой. Числа здесь достаточно, чтобы соблазн
    // «привести второй операнд» выглядел естественным, — приведения нет.
    EXPECT_EQ(binaryError(TokenKind::Less, number(1.0), CS::materialize("2", dead)).code,
              CS::ErrorCode::Type);
}

TEST(OperatorOrdering, NaNMakesAllFourFalse) {
    const Value nan = number(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(binary(TokenKind::Less, nan, number(1.0)).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Greater, nan, number(1.0)).booleanValue());
    EXPECT_FALSE(binary(TokenKind::LessEqual, nan, number(1.0)).booleanValue());
    EXPECT_FALSE(binary(TokenKind::GreaterEqual, nan, number(1.0)).booleanValue());
}

TEST(OperatorOrdering, NoOperatorIsDerivedByNegatingAnother) {
    const Value nan = number(std::numeric_limits<double>::quiet_NaN());
    // Это и есть проверка запрета из docs/semantics.md §5.3: !(1 < NaN)
    // истинно, тогда как 1 >= NaN ложно. Если бы >= был написан как
    // отрицание <, здесь получилось бы true.
    EXPECT_FALSE(binary(TokenKind::Less, number(1.0), nan).booleanValue());
    EXPECT_FALSE(binary(TokenKind::GreaterEqual, number(1.0), nan).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Greater, number(1.0), nan).booleanValue());
    EXPECT_FALSE(binary(TokenKind::LessEqual, number(1.0), nan).booleanValue());
}

TEST(OperatorOrdering, NaNComparedWithItselfIsAlsoFalse) {
    const Value nan = number(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(binary(TokenKind::LessEqual, nan, nan).booleanValue());
    EXPECT_FALSE(binary(TokenKind::GreaterEqual, nan, nan).booleanValue());
}

TEST(OperatorOrdering, NegativeZeroComparesEqualToZero) {
    // Знак нуля различает ключи объекта, но не порядок: -0 и 0 равны по IEEE.
    EXPECT_FALSE(binary(TokenKind::Less, number(-0.0), number(0.0)).booleanValue());
    EXPECT_TRUE(binary(TokenKind::LessEqual, number(-0.0), number(0.0)).booleanValue());
}

TEST(OperatorOrdering, InfinitiesOrderAsExpected) {
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_TRUE(binary(TokenKind::Less, number(-inf), number(1.0)).booleanValue());
    EXPECT_TRUE(binary(TokenKind::Less, number(1.0), number(inf)).booleanValue());
}

TEST(OperatorEquality, NullEqualsOnlyNull) {
    CS::Deferred dead;
    // Правило 1 применяется раньше правила 2, поэтому null == 5 даёт false,
    // а не ошибку (docs/semantics.md §5.4).
    EXPECT_TRUE(binary(TokenKind::Equal, Value::null(), Value::null()).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, Value::null(), number(5.0)).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, number(5.0), Value::null()).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, Value::null(), CS::materialize("a", dead)).booleanValue());
}

TEST(OperatorEquality, DifferentTypesAreAnError) {
    CS::Deferred dead;
    Store store;
    // Правило 2: типы различаются — ошибка, а не false.
    EXPECT_EQ(binaryError(TokenKind::Equal, number(1.0), CS::materialize("1", dead)).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Equal, Value::boolean(true), number(1.0)).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Equal, CS::makeArray(0, store.clock(), dead), CS::makeObject(store.keys(), 0, store.clock(), dead)).code,
              CS::ErrorCode::Type);
}

TEST(OperatorEquality, NumbersCompareByValue) {
    EXPECT_TRUE(binary(TokenKind::Equal, number(1.5), number(1.5)).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, number(1.5), number(2.5)).booleanValue());
}

TEST(OperatorEquality, NaNIsNotEqualToItself) {
    const Value nan = number(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(binary(TokenKind::Equal, nan, nan).booleanValue());
    EXPECT_TRUE(binary(TokenKind::NotEqual, nan, nan).booleanValue());
}

TEST(OperatorEquality, NegativeZeroEqualsZero) {
    // docs/semantics.md §4.3: -0 == 0 истинно, хотя ключами они различаются.
    EXPECT_TRUE(binary(TokenKind::Equal, number(-0.0), number(0.0)).booleanValue());
}

TEST(OperatorEquality, StringsCompareByBytes) {
    CS::Deferred dead;
    const Value a = CS::materialize("привет", dead);
    const Value b = CS::materialize("привет", dead);
    const Value c = CS::materialize("пока", dead);
    // Два разных значения с одинаковым содержимым равны: сравниваются байты,
    // а не хранилище.
    EXPECT_TRUE(binary(TokenKind::Equal, a, b).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, a, c).booleanValue());
}

TEST(OperatorEquality, StringsAreNotNormalized) {
    CS::Deferred dead;
    // docs/semantics.md §5.4: нормализация юникода не выполняется. Одна и та
    // же буква, записанная как готовый символ и как база с комбинирующим
    // знаком, — разные строки.
    const Value composed = CS::materialize("\xD0\xB9", dead);                  // й
    const Value decomposed = CS::materialize("\xD0\xB8\xCC\x86", dead);        // и + бреве
    EXPECT_FALSE(binary(TokenKind::Equal, composed, decomposed).booleanValue());
}

TEST(OperatorEquality, BooleansCompareByValue) {
    EXPECT_TRUE(binary(TokenKind::Equal, Value::boolean(true), Value::boolean(true)).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, Value::boolean(true), Value::boolean(false)).booleanValue());
}

TEST(OperatorEquality, AggregatesCompareByIdentity) {
    CS::Deferred dead;
    Store store;
    const Value items = CS::makeArray(0, store.clock(), dead);
    CS::arrayPush(items, number(1.0), store.clock());
    const Value alias = items;
    const Value other = CS::makeArray(0, store.clock(), dead);
    CS::arrayPush(other, number(1.0), store.clock());

    // docs/semantics.md §5.4: равны тогда и только тогда, когда это один и тот
    // же объект. Одинаковое содержимое не делает их равными.
    EXPECT_TRUE(binary(TokenKind::Equal, items, alias).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, items, other).booleanValue());

    // Объекты идут по той же ветке switch, что и массивы, но проверены до сих
    // пор были только массивы.
    const Value box = CS::makeObject(store.keys(), 0, store.clock(), dead);
    CS::objectSet(box, "k", number(1.0), store.clock(), dead);
    const Value boxAlias = box;
    const Value otherBox = CS::makeObject(store.keys(), 0, store.clock(), dead);
    CS::objectSet(otherBox, "k", number(1.0), store.clock(), dead);
    EXPECT_TRUE(binary(TokenKind::Equal, box, boxAlias).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, box, otherBox).booleanValue());
}

TEST(OperatorEquality, NotEqualNegatesEqual) {
    EXPECT_FALSE(binary(TokenKind::NotEqual, number(1.0), number(1.0)).booleanValue());
    EXPECT_TRUE(binary(TokenKind::NotEqual, number(1.0), number(2.0)).booleanValue());
    EXPECT_FALSE(binary(TokenKind::NotEqual, Value::null(), Value::null()).booleanValue());
}

TEST(OperatorEquality, NotEqualPropagatesTheError) {
    CS::Deferred dead;
    // docs/semantics.md §5.4: != эквивалентен отрицанию == во всём, включая
    // случай ошибки.
    EXPECT_EQ(binaryError(TokenKind::NotEqual, number(1.0), CS::materialize("1", dead)).code,
              CS::ErrorCode::Type);
}

}  // namespace
