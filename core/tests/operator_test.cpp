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
Value binary(TokenKind op, Value lhs, Value rhs, const Store &store) {
    Diagnostic diag;
    Value out = Value::null();
    EXPECT_TRUE(CS::applyBinary(op, lhs, rhs, store, store, kOffset, &out, diag))
        << diag.message;
    return out;
}

/// Применяет бинарную операцию и требует отказа; возвращает диагностику.
Diagnostic binaryError(TokenKind op, Value lhs, Value rhs, const Store &store) {
    Diagnostic diag;
    Value out = Value::null();
    EXPECT_FALSE(CS::applyBinary(op, lhs, rhs, store, store, kOffset, &out, diag));
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
    EXPECT_EQ(unaryError(TokenKind::Bang, number(1.0)).code, CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Bang, Value::null()).code, CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Bang, store.makeString("a")).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Bang, store.makeArray()).code,
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
    Store store;
    EXPECT_EQ(unaryError(TokenKind::Minus, Value::boolean(true)).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Minus, Value::null()).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(unaryError(TokenKind::Minus, store.makeString("1")).code,
              CS::ErrorCode::Type);
}

TEST(OperatorArithmetic, FourOperationsWork) {
    Store store;
    EXPECT_EQ(binary(TokenKind::Plus, number(1.0), number(2.0), store).numberValue(), 3.0);
    EXPECT_EQ(binary(TokenKind::Minus, number(5.0), number(2.0), store).numberValue(), 3.0);
    EXPECT_EQ(binary(TokenKind::Star, number(3.0), number(4.0), store).numberValue(), 12.0);
    EXPECT_EQ(binary(TokenKind::Slash, number(9.0), number(2.0), store).numberValue(), 4.5);
}

TEST(OperatorArithmetic, RequiresNumbersOnBothSides) {
    Store store;
    const Value text = store.makeString("1");
    EXPECT_EQ(binaryError(TokenKind::Plus, number(1.0), text, store).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Plus, text, number(1.0), store).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Plus, number(1.0), Value::null(), store).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Plus, number(1.0), Value::boolean(true), store).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Star, store.makeArray(), number(2.0), store).code,
              CS::ErrorCode::Type);
}

TEST(OperatorArithmetic, ThereIsNoStringConcatenation) {
    Store store;
    // docs/semantics.md §5.2: строки собирает format, а не плюс. Это место,
    // где привычка из JavaScript обманывает чаще всего.
    EXPECT_EQ(binaryError(TokenKind::Plus, store.makeString("a"), store.makeString("b"), store).code,
              CS::ErrorCode::Type);
}

TEST(OperatorArithmetic, DivisionByZeroFollowsIEEE) {
    Store store;
    // docs/semantics.md §5.2: результат определён стандартом, ошибкой не
    // является. Исключение здесь означало бы, что неполные данные роняют экран.
    EXPECT_TRUE(std::isinf(binary(TokenKind::Slash, number(1.0), number(0.0), store).numberValue()));
    EXPECT_GT(binary(TokenKind::Slash, number(1.0), number(0.0), store).numberValue(), 0.0);
    EXPECT_LT(binary(TokenKind::Slash, number(-1.0), number(0.0), store).numberValue(), 0.0);
    EXPECT_TRUE(std::isnan(binary(TokenKind::Slash, number(0.0), number(0.0), store).numberValue()));
}

TEST(OperatorArithmetic, ModuloTakesTheSignOfTheDividend) {
    Store store;
    // docs/semantics.md §5.2: это std::fmod, а не остаток от целого деления.
    EXPECT_EQ(binary(TokenKind::Percent, number(7.0), number(3.0), store).numberValue(), 1.0);
    EXPECT_EQ(binary(TokenKind::Percent, number(-5.0), number(3.0), store).numberValue(), -2.0);
    EXPECT_EQ(binary(TokenKind::Percent, number(5.0), number(-3.0), store).numberValue(), 2.0);
    EXPECT_EQ(binary(TokenKind::Percent, number(5.5), number(2.0), store).numberValue(), 1.5);
}

TEST(OperatorArithmetic, ModuloByZeroGivesNaN) {
    Store store;
    EXPECT_TRUE(std::isnan(binary(TokenKind::Percent, number(5.0), number(0.0), store).numberValue()));
}

TEST(OperatorDiagnostics, OffsetIsCarriedThrough) {
    Store store;
    // Смещение — данные, а не зависимость от дерева: операция получает его
    // параметром и кладёт в диагностику как есть.
    EXPECT_EQ(binaryError(TokenKind::Plus, number(1.0), Value::null(), store).offset, kOffset);
    EXPECT_EQ(unaryError(TokenKind::Bang, number(1.0)).offset, kOffset);
}

TEST(OperatorOrdering, FourOperatorsWork) {
    Store store;
    EXPECT_TRUE(binary(TokenKind::Less, number(1.0), number(2.0), store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Less, number(2.0), number(1.0), store).booleanValue());
    EXPECT_TRUE(binary(TokenKind::Greater, number(2.0), number(1.0), store).booleanValue());
    EXPECT_TRUE(binary(TokenKind::LessEqual, number(1.0), number(1.0), store).booleanValue());
    EXPECT_TRUE(binary(TokenKind::GreaterEqual, number(1.0), number(1.0), store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Less, number(1.0), number(1.0), store).booleanValue());
}

TEST(OperatorOrdering, RequiresNumbers) {
    Store store;
    // docs/semantics.md §5.3: строки, логические значения и агрегаты
    // сравнивать нельзя. Побайтовый порядок строк не соответствует
    // алфавитному, а порядок, зависящий от языка, — это коллация, которой
    // в рантайме нет.
    EXPECT_EQ(binaryError(TokenKind::Less, store.makeString("a"), store.makeString("b"), store).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Less, Value::boolean(false), Value::boolean(true), store).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Less, Value::null(), number(1.0), store).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Greater, store.makeArray(), store.makeArray(), store).code,
              CS::ErrorCode::Type);
    // Смешанная пара: число со строкой. Числа здесь достаточно, чтобы соблазн
    // «привести второй операнд» выглядел естественным, — приведения нет.
    EXPECT_EQ(binaryError(TokenKind::Less, number(1.0), store.makeString("2"), store).code,
              CS::ErrorCode::Type);
}

TEST(OperatorOrdering, NaNMakesAllFourFalse) {
    Store store;
    const Value nan = number(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(binary(TokenKind::Less, nan, number(1.0), store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Greater, nan, number(1.0), store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::LessEqual, nan, number(1.0), store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::GreaterEqual, nan, number(1.0), store).booleanValue());
}

TEST(OperatorOrdering, NoOperatorIsDerivedByNegatingAnother) {
    Store store;
    const Value nan = number(std::numeric_limits<double>::quiet_NaN());
    // Это и есть проверка запрета из docs/semantics.md §5.3: !(1 < NaN)
    // истинно, тогда как 1 >= NaN ложно. Если бы >= был написан как
    // отрицание <, здесь получилось бы true.
    EXPECT_FALSE(binary(TokenKind::Less, number(1.0), nan, store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::GreaterEqual, number(1.0), nan, store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Greater, number(1.0), nan, store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::LessEqual, number(1.0), nan, store).booleanValue());
}

TEST(OperatorOrdering, NaNComparedWithItselfIsAlsoFalse) {
    Store store;
    const Value nan = number(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(binary(TokenKind::LessEqual, nan, nan, store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::GreaterEqual, nan, nan, store).booleanValue());
}

TEST(OperatorOrdering, NegativeZeroComparesEqualToZero) {
    Store store;
    // Знак нуля различает ключи объекта, но не порядок: -0 и 0 равны по IEEE.
    EXPECT_FALSE(binary(TokenKind::Less, number(-0.0), number(0.0), store).booleanValue());
    EXPECT_TRUE(binary(TokenKind::LessEqual, number(-0.0), number(0.0), store).booleanValue());
}

TEST(OperatorOrdering, InfinitiesOrderAsExpected) {
    Store store;
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_TRUE(binary(TokenKind::Less, number(-inf), number(1.0), store).booleanValue());
    EXPECT_TRUE(binary(TokenKind::Less, number(1.0), number(inf), store).booleanValue());
}

TEST(OperatorEquality, NullEqualsOnlyNull) {
    Store store;
    // Правило 1 применяется раньше правила 2, поэтому null == 5 даёт false,
    // а не ошибку (docs/semantics.md §5.4).
    EXPECT_TRUE(binary(TokenKind::Equal, Value::null(), Value::null(), store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, Value::null(), number(5.0), store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, number(5.0), Value::null(), store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, Value::null(), store.makeString("a"), store).booleanValue());
}

TEST(OperatorEquality, DifferentTypesAreAnError) {
    Store store;
    // Правило 2: типы различаются — ошибка, а не false.
    EXPECT_EQ(binaryError(TokenKind::Equal, number(1.0), store.makeString("1"), store).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Equal, Value::boolean(true), number(1.0), store).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Equal, store.makeArray(), store.makeObject(), store).code,
              CS::ErrorCode::Type);
}

TEST(OperatorEquality, NumbersCompareByValue) {
    Store store;
    EXPECT_TRUE(binary(TokenKind::Equal, number(1.5), number(1.5), store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, number(1.5), number(2.5), store).booleanValue());
}

TEST(OperatorEquality, NaNIsNotEqualToItself) {
    Store store;
    const Value nan = number(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(binary(TokenKind::Equal, nan, nan, store).booleanValue());
    EXPECT_TRUE(binary(TokenKind::NotEqual, nan, nan, store).booleanValue());
}

TEST(OperatorEquality, NegativeZeroEqualsZero) {
    Store store;
    // docs/semantics.md §4.3: -0 == 0 истинно, хотя ключами они различаются.
    EXPECT_TRUE(binary(TokenKind::Equal, number(-0.0), number(0.0), store).booleanValue());
}

TEST(OperatorEquality, StringsCompareByBytes) {
    Store store;
    const Value a = store.makeString("привет");
    const Value b = store.makeString("привет");
    const Value c = store.makeString("пока");
    // Два разных значения с одинаковым содержимым равны: сравниваются байты,
    // а не хранилище.
    EXPECT_TRUE(binary(TokenKind::Equal, a, b, store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, a, c, store).booleanValue());
}

TEST(OperatorEquality, StringsAreNotNormalized) {
    Store store;
    // docs/semantics.md §5.4: нормализация юникода не выполняется. Одна и та
    // же буква, записанная как готовый символ и как база с комбинирующим
    // знаком, — разные строки.
    const Value composed = store.makeString("\xD0\xB9");                  // й
    const Value decomposed = store.makeString("\xD0\xB8\xCC\x86");        // и + бреве
    EXPECT_FALSE(binary(TokenKind::Equal, composed, decomposed, store).booleanValue());
}

TEST(OperatorEquality, BooleansCompareByValue) {
    Store store;
    EXPECT_TRUE(binary(TokenKind::Equal, Value::boolean(true), Value::boolean(true), store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, Value::boolean(true), Value::boolean(false), store).booleanValue());
}

TEST(OperatorEquality, AggregatesCompareByIdentity) {
    Store store;
    const Value items = store.makeArray();
    CS::arrayPush(items, number(1.0));
    const Value alias = items;
    const Value other = store.makeArray();
    CS::arrayPush(other, number(1.0));

    // docs/semantics.md §5.4: равны тогда и только тогда, когда это один и тот
    // же объект. Одинаковое содержимое не делает их равными.
    EXPECT_TRUE(binary(TokenKind::Equal, items, alias, store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, items, other, store).booleanValue());

    // Объекты идут по той же ветке switch, что и массивы, но проверены до сих
    // пор были только массивы.
    const Value box = store.makeObject();
    CS::objectSet(box, "k", number(1.0), store.deferred());
    const Value boxAlias = box;
    const Value otherBox = store.makeObject();
    CS::objectSet(otherBox, "k", number(1.0), store.deferred());
    EXPECT_TRUE(binary(TokenKind::Equal, box, boxAlias, store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, box, otherBox, store).booleanValue());
}

TEST(OperatorEquality, NotEqualNegatesEqual) {
    Store store;
    EXPECT_FALSE(binary(TokenKind::NotEqual, number(1.0), number(1.0), store).booleanValue());
    EXPECT_TRUE(binary(TokenKind::NotEqual, number(1.0), number(2.0), store).booleanValue());
    EXPECT_FALSE(binary(TokenKind::NotEqual, Value::null(), Value::null(), store).booleanValue());
}

TEST(OperatorEquality, NotEqualPropagatesTheError) {
    Store store;
    // docs/semantics.md §5.4: != эквивалентен отрицанию == во всём, включая
    // случай ошибки.
    EXPECT_EQ(binaryError(TokenKind::NotEqual, number(1.0), store.makeString("1"), store).code,
              CS::ErrorCode::Type);
}

}  // namespace
