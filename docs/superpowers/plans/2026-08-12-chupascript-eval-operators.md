# ChupaScript: вычислитель, часть 2 — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Вычислять всю главу 5 семантики: унарные операторы, арифметику, сравнения порядка, равенство, логические, `??` и тернарный.

**Architecture:** Глава 5 делится надвое по существу. §5.2, §5.3 и §5.4 — чистые функции двух значений, они уезжают в `core/src/operator.*`. §5.5, §5.6 и §5.7 — управление: их суть в том, чтобы правый операнд или невыбранную ветвь **не вычислять**, поэтому они остаются в обходе. Разделение окупается в части 3, где составное присваивание определено как `x = x + e` и потребует ровно «применить операцию к двум значениям».

**Tech Stack:** C++17, gtest, Google Benchmark, CMake. У самой библиотеки зависимостей нет.

**Спека:** `docs/superpowers/specs/2026-08-12-chupascript-eval-operators-design.md` — нормативна для этого плана.
**Семантика:** `docs/semantics.md` §3.3 (порядок вычисления), глава 5 целиком.

## Global Constraints

- **C++17.** Стандарт задан в корневом `CMakeLists.txt`.
- **У библиотеки нет зависимостей.**
- **Комментарии — по-русски.** Сообщения диагностики — по-английски.
- **`core/src/parser.*`, `core/src/lexer.*`, `core/src/ast.*` не меняются ни одной строкой.**
- **Арифметика и сравнения порядка принимают только `Number`,** оба операнда. Конкатенации строк через `+` нет.
- **Ни один из четырёх операторов порядка не выводится отрицанием соседа:** при `NaN` все четыре дают `false`, поэтому `!(1 < NaN)` истинно, а `1 >= NaN` ложно.
- **Правила равенства применяются в порядке перечисления:** `null` проверяется до несовпадения типов, поэтому `null == 5` даёт `false`, а `1 == '1'` — ошибку.
- **`!=` реализуется отрицанием `==`,** а не отдельной таблицей.
- **Короткое замыкание не проверяет тип невычисленного операнда:** `false && 5` даёт `false`, `true && 5` — ошибку.
- **`??` перехватывает только `null` и ошибку не гасит.**
- **Единственный класс ошибок части — `Type`.** Смещение — от узла операции; первая ошибка выигрывает; при отказе `*out` не трогается.
- **Сборка тестов:** `cmake -B build && cmake --build build -j`, прогон `ctest --test-dir build --output-on-failure`.
- **Сборка бенчмарков:** только Release — `cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON`.
- **Коммитить явными путями.** `git add -A` не использовать.

---

## Структура файлов

| Файл | Ответственность |
|---|---|
| `core/src/operator.hpp` | объявления `applyBinary` и `applyUnary` — вся поверхность единицы |
| `core/src/operator.cpp` | арифметика, порядок, равенство над значениями |
| `core/src/eval.cpp` | правится: ветки `Unary`, `Binary`, `Conditional`; короткое замыкание |
| `core/tests/operator_test.cpp` | таблицы типов и значений напрямую, без разбора текста |
| `core/tests/eval_test.cpp` | дописывается: подключение к обходу, короткое замыкание, тернарный |
| `benchmarks/eval_benchmark.cpp` | дописывается |

Существующие интерфейсы:

```cpp
// core/src/value.hpp
enum class Value::Kind : std::uint8_t { Null, Boolean, Number, String, Object, Array };
static Value null() / boolean(bool) / number(double);
Kind kind() const noexcept; bool booleanValue() const; double numberValue() const;
bool sameAggregate(Value) const noexcept;

// core/src/context.hpp
std::string_view string(Value) const noexcept;   // предусловие: kind() == String

// core/src/token.hpp — TokenKind
Plus, Minus, Star, Slash, Percent, Bang,
Equal, NotEqual, Less, Greater, LessEqual, GreaterEqual,
AndAnd, OrOr, QuestionQuestion

// core/src/ast.hpp
NodeKind kind(NodeId) const noexcept;   // среди прочих Unary, Binary, Conditional
TokenKind op(NodeId) const noexcept;    // операция у Unary, Binary, Assign
std::uint32_t offset(NodeId) const noexcept;
NodeId child(NodeId, std::uint32_t) const noexcept;

// core/src/eval.cpp, внутренние — уже есть
bool eval(const Ast &, NodeId, Context &, Value *, Diagnostic &);
bool fail(const Ast &, NodeId, ErrorCode, const char *, Diagnostic &);

// core/src/diagnostic.hpp
enum class ErrorCode : std::uint8_t { None, Syntax, Name, Type, Range, Data, Usage, Memory };
struct Diagnostic { ErrorCode code; std::uint32_t offset; const char *message; };
```

Узлы: у `Unary` один ребёнок — операнд; у `Binary` два — левый и правый; у `Conditional` три — условие, ветвь-да, ветвь-нет.

---

## Задача 1: `operator.*` — унарные и арифметика

**Files:**
- Create: `core/src/operator.hpp`, `core/src/operator.cpp`, `core/tests/operator_test.cpp`
- Modify: `core/CMakeLists.txt:1-11`, `core/tests/CMakeLists.txt:1-11`

**Interfaces:**
- Consumes: `Value`, `Context`, `TokenKind`, `Diagnostic`.
- Produces: `bool CS::applyBinary(TokenKind op, Value lhs, Value rhs, const Context &ctx, std::uint32_t offset, Value *out, Diagnostic &diag)` и `bool CS::applyUnary(TokenKind op, Value operand, std::uint32_t offset, Value *out, Diagnostic &diag)`. В `operator.cpp` появляются внутренние `failType`, `bothNumbers` и `applyArithmetic`, которые расширяют задачи 2 и 3.

- [ ] **Шаг 1: Написать тесты**

Создать `core/tests/operator_test.cpp`:

```cpp
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
```

- [ ] **Шаг 2: Зарегистрировать в сборке**

`core/CMakeLists.txt`, список исходников:

```cmake
add_library(chupascript STATIC
    src/ast.cpp
    src/context.cpp
    src/data.cpp
    src/eval.cpp
    src/lexer.cpp
    src/operator.cpp
    src/parser.cpp
    src/text.cpp
    src/version.cpp
)
```

`core/tests/CMakeLists.txt`, список исходников:

```cmake
add_executable(chupascript_tests
    ast_test.cpp
    context_test.cpp
    data_test.cpp
    eval_test.cpp
    lexer_test.cpp
    operator_test.cpp
    parser_test.cpp
    smoke_test.cpp
    text_test.cpp
    value_test.cpp
)
```

- [ ] **Шаг 3: Убедиться, что не собирается**

Run: `cmake -B build && cmake --build build -j`
Expected: ошибка — `operator.hpp` не существует.

- [ ] **Шаг 4: Написать `core/src/operator.hpp`**

```cpp
#pragma once
#include <cstdint>

#include "context.hpp"
#include "diagnostic.hpp"
#include "token.hpp"
#include "value.hpp"

namespace CS {

/// Применяет бинарную операцию без короткого замыкания.
///
/// op — одна из: Plus, Minus, Star, Slash, Percent, Less, Greater, LessEqual,
/// GreaterEqual, Equal, NotEqual. Логические, ?? и тернарный сюда не попадают:
/// они решают, вычислять ли операнд, а здесь оба уже вычислены.
///
/// ctx нужен единственной операции — сравнению строк на равенство. offset
/// попадает в diag при отказе: это данные, а не зависимость от дерева. При
/// отказе *out не трогается.
bool applyBinary(TokenKind op, Value lhs, Value rhs, const Context &ctx,
                 std::uint32_t offset, Value *out, Diagnostic &diag);

/// Применяет унарную операцию: Bang над Boolean, Minus над Number.
///
/// Контекст не нужен: ни одна из двух операций не заглядывает в хранилище.
/// При отказе *out не трогается.
bool applyUnary(TokenKind op, Value operand, std::uint32_t offset, Value *out,
                Diagnostic &diag);

}  // namespace CS
```

- [ ] **Шаг 5: Написать `core/src/operator.cpp`**

```cpp
#include "operator.hpp"

#include <cassert>
#include <cmath>

namespace CS {
namespace {

/// Записывает отказ по несовпадению типа операнда.
///
/// Единственный класс ошибок этой единицы: всё, что может пойти не так в
/// главе 5, — это неподходящий тип.
bool failType(std::uint32_t offset, const char *message, Diagnostic &diag) {
    diag = Diagnostic{ErrorCode::Type, offset, message};
    return false;
}

/// Оба ли операнда числа. Арифметика и сравнения порядка требуют этого
/// (docs/semantics.md §5.2, §5.3); приведения к числу в языке нет.
bool bothNumbers(Value lhs, Value rhs) {
    return lhs.kind() == Value::Kind::Number && rhs.kind() == Value::Kind::Number;
}

/// Арифметика (docs/semantics.md §5.2).
bool applyArithmetic(TokenKind op, Value lhs, Value rhs, std::uint32_t offset,
                     Value *out, Diagnostic &diag) {
    // Только Number, оба операнда. Конкатенации строк через + нет: строки
    // собирает format.
    if (!bothNumbers(lhs, rhs)) {
        return failType(offset, "arithmetic requires numbers", diag);
    }

    const double a = lhs.numberValue();
    const double b = rhs.numberValue();
    double result = 0.0;
    switch (op) {
        case TokenKind::Plus: result = a + b; break;
        case TokenKind::Minus: result = a - b; break;
        case TokenKind::Star: result = a * b; break;
        // Деление на ноль даёт бесконечность по IEEE 754 и ошибкой не является.
        case TokenKind::Slash: result = a / b; break;
        // Знак делимого: это std::fmod, а не остаток от целого деления.
        case TokenKind::Percent: result = std::fmod(a, b); break;
        default: assert(false && "не арифметическая операция"); break;
    }

    *out = Value::number(result);
    return true;
}

}  // namespace

bool applyUnary(TokenKind op, Value operand, std::uint32_t offset, Value *out,
                Diagnostic &diag) {
    switch (op) {
        case TokenKind::Bang:
            if (operand.kind() != Value::Kind::Boolean) {
                return failType(offset, "! requires a boolean", diag);
            }
            *out = Value::boolean(!operand.booleanValue());
            return true;

        case TokenKind::Minus:
            if (operand.kind() != Value::Kind::Number) {
                return failType(offset, "unary minus requires a number", diag);
            }
            // Над NaN даёт NaN, над нулём — отрицательный ноль: и то и другое
            // получается само, потому что это обычное отрицание double.
            *out = Value::number(-operand.numberValue());
            return true;

        default:
            assert(false && "applyUnary принимает только Bang и Minus");
            return failType(offset, "unsupported unary operator", diag);
    }
}

bool applyBinary(TokenKind op, Value lhs, Value rhs, const Context &ctx,
                 std::uint32_t offset, Value *out, Diagnostic &diag) {
    (void)ctx;  // понадобится равенству строк
    switch (op) {
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
            return applyArithmetic(op, lhs, rhs, offset, out, diag);

        default:
            assert(false && "applyBinary принимает только операции без короткого замыкания");
            return failType(offset, "unsupported binary operator", diag);
    }
}

}  // namespace CS
```

- [ ] **Шаг 6: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "OperatorUnary|OperatorArithmetic|OperatorDiagnostics"`
Expected: 13 тестов PASS.

- [ ] **Шаг 7: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 325 тестов PASS (312 было + 13).

- [ ] **Шаг 8: Коммит**

```bash
git add core/src/operator.hpp core/src/operator.cpp core/tests/operator_test.cpp \
        core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "Apply unary operators and arithmetic to values"
```

---

## Задача 2: Сравнения порядка

**Files:**
- Modify: `core/src/operator.cpp`, `core/tests/operator_test.cpp`

**Interfaces:**
- Consumes: `failType`, `bothNumbers` из задачи 1.
- Produces: внутренняя `applyOrdering`; ветки `Less`, `Greater`, `LessEqual`, `GreaterEqual` в `applyBinary`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/operator_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(OperatorOrdering, FourOperatorsWork) {
    Context ctx;
    EXPECT_TRUE(binary(TokenKind::Less, number(1.0), number(2.0), ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Less, number(2.0), number(1.0), ctx).booleanValue());
    EXPECT_TRUE(binary(TokenKind::Greater, number(2.0), number(1.0), ctx).booleanValue());
    EXPECT_TRUE(binary(TokenKind::LessEqual, number(1.0), number(1.0), ctx).booleanValue());
    EXPECT_TRUE(binary(TokenKind::GreaterEqual, number(1.0), number(1.0), ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Less, number(1.0), number(1.0), ctx).booleanValue());
}

TEST(OperatorOrdering, RequiresNumbers) {
    Context ctx;
    // docs/semantics.md §5.3: строки, логические значения и агрегаты
    // сравнивать нельзя. Побайтовый порядок строк не соответствует
    // алфавитному, а порядок, зависящий от языка, — это коллация, которой
    // в рантайме нет.
    EXPECT_EQ(binaryError(TokenKind::Less, ctx.makeString("a"), ctx.makeString("b"), ctx).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Less, Value::boolean(false), Value::boolean(true), ctx).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Less, Value::null(), number(1.0), ctx).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Greater, ctx.makeArray(), ctx.makeArray(), ctx).code,
              CS::ErrorCode::Type);
}

TEST(OperatorOrdering, NaNMakesAllFourFalse) {
    Context ctx;
    const Value nan = number(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(binary(TokenKind::Less, nan, number(1.0), ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Greater, nan, number(1.0), ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::LessEqual, nan, number(1.0), ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::GreaterEqual, nan, number(1.0), ctx).booleanValue());
}

TEST(OperatorOrdering, NoOperatorIsDerivedByNegatingAnother) {
    Context ctx;
    const Value nan = number(std::numeric_limits<double>::quiet_NaN());
    // Это и есть проверка запрета из docs/semantics.md §5.3: !(1 < NaN)
    // истинно, тогда как 1 >= NaN ложно. Если бы >= был написан как
    // отрицание <, здесь получилось бы true.
    EXPECT_FALSE(binary(TokenKind::Less, number(1.0), nan, ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::GreaterEqual, number(1.0), nan, ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Greater, number(1.0), nan, ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::LessEqual, number(1.0), nan, ctx).booleanValue());
}

TEST(OperatorOrdering, NaNComparedWithItselfIsAlsoFalse) {
    Context ctx;
    const Value nan = number(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(binary(TokenKind::LessEqual, nan, nan, ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::GreaterEqual, nan, nan, ctx).booleanValue());
}

TEST(OperatorOrdering, NegativeZeroComparesEqualToZero) {
    Context ctx;
    // Знак нуля различает ключи объекта, но не порядок: -0 и 0 равны по IEEE.
    EXPECT_FALSE(binary(TokenKind::Less, number(-0.0), number(0.0), ctx).booleanValue());
    EXPECT_TRUE(binary(TokenKind::LessEqual, number(-0.0), number(0.0), ctx).booleanValue());
}

TEST(OperatorOrdering, InfinitiesOrderAsExpected) {
    Context ctx;
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_TRUE(binary(TokenKind::Less, number(-inf), number(1.0), ctx).booleanValue());
    EXPECT_TRUE(binary(TokenKind::Less, number(1.0), number(inf), ctx).booleanValue());
}
```

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R OperatorOrdering`
Expected: FAIL — операции порядка попадают в `default` с `assert`. В отладочной сборке это прерывание, а не провал утверждения; так и должно быть, значит ветки ещё нет.

- [ ] **Шаг 3: Добавить `applyOrdering` в `core/src/operator.cpp`**

В анонимное пространство имён, после `applyArithmetic`:

```cpp
/// Сравнение порядка (docs/semantics.md §5.3).
///
/// Только Number, оба операнда. Если хотя бы один NaN, все четыре оператора
/// дают false — поэтому **ни один из них не выводится отрицанием соседа**:
/// !(1 < NaN) истинно, тогда как 1 >= NaN ложно. Встроенные сравнения C++
/// ведут себя с NaN ровно так, как требует §5.3, и используются напрямую.
bool applyOrdering(TokenKind op, Value lhs, Value rhs, std::uint32_t offset,
                   Value *out, Diagnostic &diag) {
    if (!bothNumbers(lhs, rhs)) {
        return failType(offset, "ordering requires numbers", diag);
    }

    const double a = lhs.numberValue();
    const double b = rhs.numberValue();
    bool result = false;
    switch (op) {
        case TokenKind::Less: result = a < b; break;
        case TokenKind::Greater: result = a > b; break;
        case TokenKind::LessEqual: result = a <= b; break;
        case TokenKind::GreaterEqual: result = a >= b; break;
        default: assert(false && "не операция порядка"); break;
    }

    *out = Value::boolean(result);
    return true;
}
```

- [ ] **Шаг 4: Подключить в `applyBinary`**

В `switch` перед `default`:

```cpp
        case TokenKind::Less:
        case TokenKind::Greater:
        case TokenKind::LessEqual:
        case TokenKind::GreaterEqual:
            return applyOrdering(op, lhs, rhs, offset, out, diag);
```

- [ ] **Шаг 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R OperatorOrdering`
Expected: 7 тестов PASS.

- [ ] **Шаг 6: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 332 теста PASS.

- [ ] **Шаг 7: Коммит**

```bash
git add core/src/operator.cpp core/tests/operator_test.cpp
git commit -m "Compare numbers by order without deriving operators from each other"
```

---

## Задача 3: Равенство

**Files:**
- Modify: `core/src/operator.cpp`, `core/tests/operator_test.cpp`

**Interfaces:**
- Consumes: `failType` из задачи 1; `Context::string`, `Value::sameAggregate`.
- Produces: внутренняя `valuesEqual`; ветки `Equal` и `NotEqual` в `applyBinary`. Здесь же перестаёт быть нужным `(void)ctx` из задачи 1.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/operator_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(OperatorEquality, NullEqualsOnlyNull) {
    Context ctx;
    // Правило 1 применяется раньше правила 2, поэтому null == 5 даёт false,
    // а не ошибку (docs/semantics.md §5.4).
    EXPECT_TRUE(binary(TokenKind::Equal, Value::null(), Value::null(), ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, Value::null(), number(5.0), ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, number(5.0), Value::null(), ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, Value::null(), ctx.makeString("a"), ctx).booleanValue());
}

TEST(OperatorEquality, DifferentTypesAreAnError) {
    Context ctx;
    // Правило 2: типы различаются — ошибка, а не false.
    EXPECT_EQ(binaryError(TokenKind::Equal, number(1.0), ctx.makeString("1"), ctx).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Equal, Value::boolean(true), number(1.0), ctx).code,
              CS::ErrorCode::Type);
    EXPECT_EQ(binaryError(TokenKind::Equal, ctx.makeArray(), ctx.makeObject(), ctx).code,
              CS::ErrorCode::Type);
}

TEST(OperatorEquality, NumbersCompareByValue) {
    Context ctx;
    EXPECT_TRUE(binary(TokenKind::Equal, number(1.5), number(1.5), ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, number(1.5), number(2.5), ctx).booleanValue());
}

TEST(OperatorEquality, NaNIsNotEqualToItself) {
    Context ctx;
    const Value nan = number(std::numeric_limits<double>::quiet_NaN());
    EXPECT_FALSE(binary(TokenKind::Equal, nan, nan, ctx).booleanValue());
    EXPECT_TRUE(binary(TokenKind::NotEqual, nan, nan, ctx).booleanValue());
}

TEST(OperatorEquality, NegativeZeroEqualsZero) {
    Context ctx;
    // docs/semantics.md §4.3: -0 == 0 истинно, хотя ключами они различаются.
    EXPECT_TRUE(binary(TokenKind::Equal, number(-0.0), number(0.0), ctx).booleanValue());
}

TEST(OperatorEquality, StringsCompareByBytes) {
    Context ctx;
    const Value a = ctx.makeString("привет");
    const Value b = ctx.makeString("привет");
    const Value c = ctx.makeString("пока");
    // Два разных значения с одинаковым содержимым равны: сравниваются байты,
    // а не хранилище.
    EXPECT_TRUE(binary(TokenKind::Equal, a, b, ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, a, c, ctx).booleanValue());
}

TEST(OperatorEquality, StringsAreNotNormalized) {
    Context ctx;
    // docs/semantics.md §5.4: нормализация юникода не выполняется. Одна и та
    // же буква, записанная как готовый символ и как база с комбинирующим
    // знаком, — разные строки.
    const Value composed = ctx.makeString("\xD0\xB9");                  // й
    const Value decomposed = ctx.makeString("\xD0\xB8\xCC\x86");        // и + бреве
    EXPECT_FALSE(binary(TokenKind::Equal, composed, decomposed, ctx).booleanValue());
}

TEST(OperatorEquality, BooleansCompareByValue) {
    Context ctx;
    EXPECT_TRUE(binary(TokenKind::Equal, Value::boolean(true), Value::boolean(true), ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, Value::boolean(true), Value::boolean(false), ctx).booleanValue());
}

TEST(OperatorEquality, AggregatesCompareByIdentity) {
    Context ctx;
    const Value items = ctx.makeArray();
    ctx.arrayPush(items, number(1.0));
    const Value alias = items;
    const Value other = ctx.makeArray();
    ctx.arrayPush(other, number(1.0));

    // docs/semantics.md §5.4: равны тогда и только тогда, когда это один и тот
    // же объект. Одинаковое содержимое не делает их равными.
    EXPECT_TRUE(binary(TokenKind::Equal, items, alias, ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::Equal, items, other, ctx).booleanValue());
}

TEST(OperatorEquality, NotEqualNegatesEqual) {
    Context ctx;
    EXPECT_FALSE(binary(TokenKind::NotEqual, number(1.0), number(1.0), ctx).booleanValue());
    EXPECT_TRUE(binary(TokenKind::NotEqual, number(1.0), number(2.0), ctx).booleanValue());
    EXPECT_FALSE(binary(TokenKind::NotEqual, Value::null(), Value::null(), ctx).booleanValue());
}

TEST(OperatorEquality, NotEqualPropagatesTheError) {
    Context ctx;
    // docs/semantics.md §5.4: != эквивалентен отрицанию == во всём, включая
    // случай ошибки.
    EXPECT_EQ(binaryError(TokenKind::NotEqual, number(1.0), ctx.makeString("1"), ctx).code,
              CS::ErrorCode::Type);
}
```

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R OperatorEquality`
Expected: FAIL — `Equal` и `NotEqual` попадают в `default` с `assert`.

- [ ] **Шаг 3: Добавить `valuesEqual` в `core/src/operator.cpp`**

В анонимное пространство имён, после `applyOrdering`:

```cpp
/// Равенство (docs/semantics.md §5.4).
///
/// Правила применяются в порядке перечисления, и порядок существенен: null
/// проверяется раньше несовпадения типов, поэтому null == 5 даёт false, а
/// 1 == '1' — ошибку.
bool valuesEqual(Value lhs, Value rhs, const Context &ctx, std::uint32_t offset,
                 bool *out, Diagnostic &diag) {
    // 1. Один из операндов null: равно тогда и только тогда, когда второй тоже.
    if (lhs.kind() == Value::Kind::Null || rhs.kind() == Value::Kind::Null) {
        *out = lhs.kind() == Value::Kind::Null && rhs.kind() == Value::Kind::Null;
        return true;
    }

    // 2. Типы различаются — ошибка.
    if (lhs.kind() != rhs.kind()) {
        return failType(offset, "equality requires operands of the same type", diag);
    }

    switch (lhs.kind()) {
        // 3. NaN не равен ничему, включая себя: это обычное сравнение double.
        case Value::Kind::Number:
            *out = lhs.numberValue() == rhs.numberValue();
            return true;

        // 4. Побайтово, без нормализации юникода.
        case Value::Kind::String:
            *out = ctx.string(lhs) == ctx.string(rhs);
            return true;

        // 5. По значению.
        case Value::Kind::Boolean:
            *out = lhs.booleanValue() == rhs.booleanValue();
            return true;

        // 6. По идентичности. Литерал создаёт новый агрегат при каждом
        // вычислении (§2.3), поэтому state.items == [1, 2] ложно всегда.
        case Value::Kind::Object:
        case Value::Kind::Array:
            *out = lhs.sameAggregate(rhs);
            return true;

        default:
            assert(false && "Null обработан правилом 1");
            return failType(offset, "equality is not defined for this type", diag);
    }
}
```

- [ ] **Шаг 4: Подключить в `applyBinary`**

В `switch` перед `default`:

```cpp
        case TokenKind::Equal:
        case TokenKind::NotEqual: {
            bool equal = false;
            if (!valuesEqual(lhs, rhs, ctx, offset, &equal, diag)) { return false; }
            // != реализуется отрицанием ==, а не отдельной таблицей: тогда
            // разойтись они не могут по построению. Отказ уже вернулся выше,
            // поэтому «включая случай ошибки» выполняется само.
            *out = Value::boolean(op == TokenKind::Equal ? equal : !equal);
            return true;
        }
```

Убрать строку `(void)ctx;` из начала `applyBinary`: теперь контекст используется.

- [ ] **Шаг 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R OperatorEquality`
Expected: 11 тестов PASS.

- [ ] **Шаг 6: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 343 теста PASS.

- [ ] **Шаг 7: Коммит**

```bash
git add core/src/operator.cpp core/tests/operator_test.cpp
git commit -m "Compare values for equality by the six ordered rules"
```

---

## Задача 4: Подключение к обходу

**Files:**
- Modify: `core/src/eval.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `applyBinary`, `applyUnary` из задач 1–3; `eval`, `fail` из части 1.
- Produces: ветки `NodeKind::Unary` и `NodeKind::Binary` в `eval`. Ветка `Binary` временно отвергает `&&`, `||` и `??` — их вносит задача 5.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(EvalOperators, UnaryWorksThroughTheWalk) {
    Context ctx;
    EXPECT_FALSE(evaluate(ctx, "!true").booleanValue());
    EXPECT_EQ(evaluate(ctx, "-3").numberValue(), -3.0);
}

TEST(EvalOperators, ArithmeticRespectsPrecedence) {
    Context ctx;
    // Приоритет — дело грамматики; вычислитель лишь обходит построенное дерево.
    EXPECT_EQ(evaluate(ctx, "1 + 2 * 3").numberValue(), 7.0);
    EXPECT_EQ(evaluate(ctx, "(1 + 2) * 3").numberValue(), 9.0);
}

TEST(EvalOperators, ComparisonWorksThroughTheWalk) {
    Context ctx;
    EXPECT_TRUE(evaluate(ctx, "1 < 2").booleanValue());
    EXPECT_FALSE(evaluate(ctx, "1 > 2").booleanValue());
}

TEST(EvalOperators, EqualityWorksThroughTheWalk) {
    Context ctx;
    EXPECT_TRUE(evaluate(ctx, "1 == 1").booleanValue());
    EXPECT_TRUE(evaluate(ctx, "1 != 2").booleanValue());
    EXPECT_TRUE(evaluate(ctx, "null == null").booleanValue());
}

TEST(EvalOperators, OperandsComeFromTheContext) {
    Context ctx;
    put(ctx, "state", "{'count': 41}");
    EXPECT_EQ(evaluate(ctx, "state.count + 1").numberValue(), 42.0);
}

TEST(EvalOperators, ErrorInTheLeftOperandStopsEvaluation) {
    Context ctx;
    EXPECT_EQ(evalError(ctx, "usre + 1").code, CS::ErrorCode::Name);
}

TEST(EvalOperators, ErrorInTheRightOperandStopsEvaluation) {
    Context ctx;
    EXPECT_EQ(evalError(ctx, "1 + usre").code, CS::ErrorCode::Name);
}

TEST(EvalOperators, AggregateEqualityIsByIdentityThroughTheWalk) {
    Context ctx;
    put(ctx, "items", "[1, 2]");
    // Литерал создаёт новый агрегат при каждом вычислении, поэтому сравнение
    // с ним ложно даже при совпадающем содержимом.
    EXPECT_TRUE(evaluate(ctx, "items == items").booleanValue());
    EXPECT_FALSE(evaluate(ctx, "items == [1, 2]").booleanValue());
}
```

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalOperators`
Expected: FAIL — узлы `Unary` и `Binary` попадают в `default` и дают «expression form is not supported».

- [ ] **Шаг 3: Добавить ветки в `core/src/eval.cpp`**

Добавить `#include "operator.hpp"` к включениям. В `switch` функции `eval`, перед `default`:

```cpp
        case NodeKind::Unary: {
            Value operand = Value::null();
            if (!eval(ast, ast.child(node, 0), ctx, &operand, diag)) { return false; }
            return applyUnary(ast.op(node), operand, ast.offset(node), out, diag);
        }

        case NodeKind::Binary: {
            const TokenKind op = ast.op(node);
            // Короткое замыкание решает, вычислять ли правый операнд, поэтому
            // в applyBinary не попадает. Его ветки приходят следующей задачей.
            if (op == TokenKind::AndAnd || op == TokenKind::OrOr ||
                op == TokenKind::QuestionQuestion) {
                return fail(ast, node, ErrorCode::Type,
                            "expression form is not supported", diag);
            }

            // Слева направо: порядок зафиксирован (docs/semantics.md §3.3).
            Value lhs = Value::null();
            if (!eval(ast, ast.child(node, 0), ctx, &lhs, diag)) { return false; }
            Value rhs = Value::null();
            if (!eval(ast, ast.child(node, 1), ctx, &rhs, diag)) { return false; }
            return applyBinary(op, lhs, rhs, ctx, ast.offset(node), out, diag);
        }
```

Проверка на `AndAnd`, `OrOr` и `QuestionQuestion` — временные леса: без неё эти операции дошли бы до `applyBinary` и упёрлись в его `assert`, то есть отладочная сборка прерывалась бы на `true && false`. Задача 5 заменяет этот блок настоящей обработкой. Тестов на леса не пишем: они уйдут вместе с ними.

- [ ] **Шаг 4: Поправить тест, который эта задача делает неверным**

Часть 1 оставила в `core/tests/eval_test.cpp` тест `EvalUnsupported.OperatorsAreNotSupportedYet`, проверявший на `1 + 1`, что операторы не поддержаны. С этой задачи `1 + 1` даёт двойку, и тест обязан быть переформулирован под то, что стало правдой, — ветка `default` никуда не делась, она сузилась до вызовов:

```cpp
TEST(EvalUnsupported, CallsAreNotSupportedYet) {
    Context ctx;
    // Вызовы приходят с частью 3. После них в ветке default останутся только
    // Program, Assign и CallStatement — узлы, которых в дереве от
    // parseExpression быть не может, — и она станет защитной окончательно.
    const Diagnostic diag = evalError(ctx, "count(items)");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_STREQ(diag.message, "expression form is not supported");
}
```

Счёт тестов от этого не меняется: один переписан, а не добавлен.

- [ ] **Шаг 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalOperators`
Expected: 8 тестов PASS.

- [ ] **Шаг 6: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 351 тест PASS.

- [ ] **Шаг 7: Коммит**

```bash
git add core/src/eval.cpp core/tests/eval_test.cpp
git commit -m "Wire unary and binary operators into the walk"
```

---

## Задача 5: Короткое замыкание и тернарный

**Files:**
- Modify: `core/src/eval.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `eval`, `fail`; ветка `Binary` из задачи 4.
- Produces: обработка `AndAnd`, `OrOr`, `QuestionQuestion` и ветка `NodeKind::Conditional`. Временные леса из задачи 4 удаляются.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(EvalShortCircuit, AndDoesNotEvaluateTheRightOperand) {
    Context ctx;
    // Побочных эффектов в выражениях нет, поэтому невычисление наблюдается
    // единственным способом: ошибка справа не всплывает.
    EXPECT_FALSE(evaluate(ctx, "false && usre").booleanValue());
}

TEST(EvalShortCircuit, AndEvaluatesTheRightOperandWhenNeeded) {
    Context ctx;
    EXPECT_FALSE(evaluate(ctx, "true && false").booleanValue());
    EXPECT_TRUE(evaluate(ctx, "true && true").booleanValue());
    EXPECT_EQ(evalError(ctx, "true && usre").code, CS::ErrorCode::Name);
}

TEST(EvalShortCircuit, OrDoesNotEvaluateTheRightOperand) {
    Context ctx;
    EXPECT_TRUE(evaluate(ctx, "true || usre").booleanValue());
}

TEST(EvalShortCircuit, OrEvaluatesTheRightOperandWhenNeeded) {
    Context ctx;
    EXPECT_TRUE(evaluate(ctx, "false || true").booleanValue());
    EXPECT_FALSE(evaluate(ctx, "false || false").booleanValue());
    EXPECT_EQ(evalError(ctx, "false || usre").code, CS::ErrorCode::Name);
}

TEST(EvalShortCircuit, ErrorOnTheLeftIsNotSwallowed) {
    Context ctx;
    // docs/semantics.md §5.5: ошибка && false — ошибка.
    EXPECT_EQ(evalError(ctx, "usre && false").code, CS::ErrorCode::Name);
    EXPECT_EQ(evalError(ctx, "usre || true").code, CS::ErrorCode::Name);
}

TEST(EvalShortCircuit, TypeOfTheUnevaluatedOperandIsNotChecked) {
    Context ctx;
    // Самая точная проверка правила: тип правого операнда проверяется тогда и
    // только тогда, когда его пришлось вычислить. Поодиночке ни одна из двух
    // строк ничего не доказывает.
    EXPECT_FALSE(evaluate(ctx, "false && 5").booleanValue());
    EXPECT_EQ(evalError(ctx, "true && 5").code, CS::ErrorCode::Type);
}

TEST(EvalShortCircuit, LogicalOperatorsRequireBooleanOnTheLeft) {
    Context ctx;
    EXPECT_EQ(evalError(ctx, "1 && true").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "'a' || true").code, CS::ErrorCode::Type);
}

TEST(EvalShortCircuit, GuardIdiomProtectsTheRightSide) {
    Context ctx;
    put(ctx, "state", "{'items': []}");
    // Ради этого короткое замыкание и существует. Правая часть без защиты
    // слева даёт ошибку типа: строковый индекс массива запрещён. Настоящая
    // идиома из §5.5 пользуется count(), который придёт с частью 3, — здесь
    // та же форма на доступных средствах.
    //
    // Обе строки обязательны: одна показывает, что справа не пошли, вторая —
    // что там действительно есть на что наткнуться.
    EXPECT_FALSE(evaluate(ctx, "false && state.items['0'] == 1").booleanValue());
    EXPECT_EQ(evalError(ctx, "true && state.items['0'] == 1").code,
              CS::ErrorCode::Type);
}

TEST(EvalNilCoalesce, TakesTheLeftWhenItIsNotNull) {
    Context ctx;
    EXPECT_EQ(evaluate(ctx, "1 ?? usre").numberValue(), 1.0);
}

TEST(EvalNilCoalesce, TakesTheRightWhenTheLeftIsNull) {
    Context ctx;
    EXPECT_EQ(evaluate(ctx, "null ?? 2").numberValue(), 2.0);
}

TEST(EvalNilCoalesce, DoesNotSwallowErrors) {
    Context ctx;
    // docs/semantics.md §5.6: ?? перехватывает только null. Соблазнительно
    // принять его за «если что-то пойдёт не так, подставь запасное»; он делает
    // не это.
    EXPECT_EQ(evalError(ctx, "usre ?? 0").code, CS::ErrorCode::Name);
    EXPECT_EQ(evalError(ctx, "(1 + 'a') ?? 0").code, CS::ErrorCode::Type);
    // И справа тоже: если левый null, правый вычисляется по-настоящему.
    EXPECT_EQ(evalError(ctx, "null ?? usre").code, CS::ErrorCode::Name);
}

TEST(EvalNilCoalesce, OperandTypesNeedNotMatch) {
    Context ctx;
    EXPECT_EQ(ctx.string(evaluate(ctx, "null ?? 'запасное'")), "запасное");
}

TEST(EvalNilCoalesce, ChainsRightAssociatively) {
    Context ctx;
    put(ctx, "user", "{'nickname': null}");
    EXPECT_EQ(ctx.string(evaluate(ctx, "user.nickname ?? user.name ?? 'Гость'")),
              "Гость");
}

TEST(EvalTernary, EvaluatesOnlyTheSelectedBranch) {
    Context ctx;
    EXPECT_EQ(evaluate(ctx, "true ? 1 : usre").numberValue(), 1.0);
    EXPECT_EQ(evaluate(ctx, "false ? usre : 2").numberValue(), 2.0);
}

TEST(EvalTernary, ConditionMustBeBoolean) {
    Context ctx;
    EXPECT_EQ(evalError(ctx, "1 ? 1 : 2").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "null ? 1 : 2").code, CS::ErrorCode::Type);
}

TEST(EvalTernary, BranchesNeedNotShareAType) {
    Context ctx;
    EXPECT_EQ(evaluate(ctx, "true ? 1 : 'a'").numberValue(), 1.0);
    EXPECT_EQ(ctx.string(evaluate(ctx, "false ? 1 : 'a'")), "a");
}
```

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "EvalShortCircuit|EvalNilCoalesce|EvalTernary"`
Expected: FAIL — логические и `??` упираются в леса из задачи 4, тернарный попадает в `default`.

- [ ] **Шаг 3: Заменить леса настоящей обработкой в `core/src/eval.cpp`**

В ветке `case NodeKind::Binary` заменить блок с временной проверкой на:

```cpp
            if (op == TokenKind::AndAnd || op == TokenKind::OrOr) {
                Value lhs = Value::null();
                if (!eval(ast, ast.child(node, 0), ctx, &lhs, diag)) { return false; }
                if (lhs.kind() != Value::Kind::Boolean) {
                    return fail(ast, node, ErrorCode::Type,
                                "logical operators require booleans", diag);
                }

                // Левый определил результат — правый не вычисляется, а значит и
                // не проверяется: проверять нечего. Поэтому false && 5 даёт
                // false, а true && 5 — ошибку.
                const bool left = lhs.booleanValue();
                if ((op == TokenKind::AndAnd && !left) ||
                    (op == TokenKind::OrOr && left)) {
                    *out = Value::boolean(left);
                    return true;
                }

                Value rhs = Value::null();
                if (!eval(ast, ast.child(node, 1), ctx, &rhs, diag)) { return false; }
                if (rhs.kind() != Value::Kind::Boolean) {
                    return fail(ast, node, ErrorCode::Type,
                                "logical operators require booleans", diag);
                }
                *out = Value::boolean(rhs.booleanValue());
                return true;
            }

            if (op == TokenKind::QuestionQuestion) {
                Value lhs = Value::null();
                if (!eval(ast, ast.child(node, 0), ctx, &lhs, diag)) { return false; }
                // Перехватывает только null: ошибка слева уже вернулась выше и
                // не гасится.
                if (lhs.kind() != Value::Kind::Null) {
                    *out = lhs;
                    return true;
                }
                return eval(ast, ast.child(node, 1), ctx, out, diag);
            }
```

- [ ] **Шаг 4: Добавить ветку тернарного**

В `switch` функции `eval`, перед `default`:

```cpp
        case NodeKind::Conditional: {
            Value condition = Value::null();
            if (!eval(ast, ast.child(node, 0), ctx, &condition, diag)) { return false; }
            if (condition.kind() != Value::Kind::Boolean) {
                return fail(ast, node, ErrorCode::Type,
                            "ternary condition must be a boolean", diag);
            }
            // Вычисляется только выбранная ветвь (docs/semantics.md §5.7).
            const NodeId branch = ast.child(node, condition.booleanValue() ? 1 : 2);
            return eval(ast, branch, ctx, out, diag);
        }
```

- [ ] **Шаг 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "EvalShortCircuit|EvalNilCoalesce|EvalTernary"`
Expected: 16 тестов PASS.

- [ ] **Шаг 6: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 367 тестов PASS.

- [ ] **Шаг 7: Прогнать под санитайзерами и с `-Werror`**

```bash
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
cmake --build build-werror -j && ctest --test-dir build-werror --output-on-failure
```

Expected: 367 PASS в обеих, ни одного отчёта санитайзера, ни одного предупреждения.

- [ ] **Шаг 8: Коммит**

```bash
git add core/src/eval.cpp core/tests/eval_test.cpp
git commit -m "Short-circuit logical operators and select a ternary branch"
```

---

## Задача 6: Бенчмарки

**Files:**
- Modify: `benchmarks/eval_benchmark.cpp`, `benchmarks/baseline.json`

**Interfaces:**
- Consumes: `evalExpression`; помощник `runEval`, уже есть в файле.
- Produces: базу для сравнения при части 3.

- [ ] **Шаг 1: Дописать бенчмарки**

В `benchmarks/eval_benchmark.cpp`, в анонимное пространство имён, перед `}  // namespace`:

```cpp
/// Арифметика: четыре операции над числами из контекста.
void BM_Eval_Arithmetic(benchmark::State &state) {
    runEval(state, "user.profile.city.code.zip * 2 + 1 - 3");
}
BENCHMARK(BM_Eval_Arithmetic);

/// Цепочка сравнений, соединённая && — типичная защита в props.
void BM_Eval_LogicalChain(benchmark::State &state) {
    runEval(state, "1 < 2 && 2 < 3 && 3 < 4");
}
BENCHMARK(BM_Eval_LogicalChain);

/// ?? по короткому пути: слева не null, правый операнд не вычисляется.
void BM_Eval_NilCoalesceShort(benchmark::State &state) {
    runEval(state, "user.name ?? 'Гость'");
}
BENCHMARK(BM_Eval_NilCoalesceShort);

/// ?? по длинному пути: слева null, правый вычисляется. Разница с коротким —
/// то, что видно на экране: ?? самый частый оператор в props.
void BM_Eval_NilCoalesceLong(benchmark::State &state) {
    runEval(state, "user.nickname ?? 'Гость'");
}
BENCHMARK(BM_Eval_NilCoalesceLong);
```

`user.nickname` отсутствует в данных, которыми `fill` наполняет контекст, поэтому читается как `null` и правый операнд вычисляется. `user.name` там есть.

- [ ] **Шаг 2: Собрать в Release и прогнать**

```bash
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks --benchmark_filter=Eval
```

Expected: десять строк `BM_Eval_*` — шесть прежних и четыре новых, — ни одной с `SkipWithError`.

Посмотреть глазами: `BM_Eval_NilCoalesceLong` обязан быть дороже `BM_Eval_NilCoalesceShort`, иначе короткое замыкание не работает или бенчмарк меряет не то. Если соотношение не выполняется, сообщи в отчёте и не записывай базу.

- [ ] **Шаг 3: Проверить, что прежние семейства не деградировали**

```bash
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_out=/tmp/ops-current.json --benchmark_out_format=json
python3 tools/bench-compare.py benchmarks/baseline.json /tmp/ops-current.json
```

Эта часть трогает `core/src/eval.cpp`, поэтому прежние строки `BM_Eval_*` под подозрением — в ветку `Binary` добавились проверки. `BM_Lex_*`, `BM_Parse_*`, `BM_Store_*` и `BM_Data_*` меняться не должны.

Порог различимости на этой машине около восьми процентов (`docs/backlog.md` B24). Выше порога — разберись до записи базы и напиши в отчёте, что нашёл. Машина при замере обязана быть незанятой.

- [ ] **Шаг 4: Записать базу**

```bash
cp /tmp/ops-current.json benchmarks/baseline.json
python3 - <<'PY'
import json, platform
path = "benchmarks/baseline.json"
with open(path, encoding="utf-8") as handle:
    data = json.load(handle)
data["context"]["chupascript_machine"] = f"{platform.machine()} {platform.platform()}"
with open(path, "w", encoding="utf-8") as handle:
    json.dump(data, handle, indent=2, ensure_ascii=False)
    handle.write("\n")
PY
python3 -c "
import json
d = json.load(open('benchmarks/baseline.json'))
names = {b['name'].split('/')[0] for b in d['benchmarks']}
for prefix in ('BM_Lex_', 'BM_Parse_', 'BM_Store_', 'BM_Data_', 'BM_Eval_', 'BM_Version'):
    assert any(n.startswith(prefix) for n in names), prefix
print('база содержит все семейства, поле машины:', d['context']['chupascript_machine'])
"
```

Expected: проверка печатает подтверждение и не падает.

- [ ] **Шаг 5: Коммит**

```bash
git add benchmarks/eval_benchmark.cpp benchmarks/baseline.json
git commit -m "Record the operator performance baseline"
```

---

## Задача 7: Документы

**Files:**
- Modify: `docs/backlog.md` (пункт B15)

**Interfaces:**
- Consumes: решения задач 1–6.
- Produces: закрытый B15.

- [ ] **Шаг 1: Закрыть B15 в `docs/backlog.md`**

Заменить тело пункта «B15. `strictEqual` и `looseEqual` в `core/src/value.hpp`» целиком на:

```markdown
**Где:** `core/src/operator.cpp`
**Статус:** закрыт

Равенство в языке одно и определено в `docs/semantics.md` §5.4 шестью правилами,
применяемыми по порядку. Реализовано `valuesEqual` в `core/src/operator.cpp`;
`!=` получается отрицанием, а не второй таблицей, поэтому разойтись они не
могут.

Двух функций, `strictEqual` и `looseEqual`, не существует и не будет: они были
черновиком по образцу языка с двумя равенствами, а у нас его одно.
```

Заголовок пункта при этом переименовать в «B15. Равенство значений», чтобы он не обещал двух функций.

- [ ] **Шаг 2: Проверить ссылки и счёт**

Run: `grep -c "^### B" docs/backlog.md`
Expected: 25 — число пунктов не меняется, B15 закрыт, а не удалён.

Run: `grep -n "strictEqual\|looseEqual" core/ docs/ -r`
Expected: совпадений нет нигде, кроме тела B15, где они названы как несуществующие.

- [ ] **Шаг 3: Прогнать весь набор**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 367 тестов PASS.

- [ ] **Шаг 4: Коммит**

```bash
git add docs/backlog.md
git commit -m "Close B15: the language has one equality"
```

---

## Итог

| | |
|---|---|
| Задач | 7 |
| Новых файлов | 3 (`operator.hpp`, `operator.cpp`, `operator_test.cpp`) |
| Изменённых | `eval.cpp`, `eval_test.cpp`, `eval_benchmark.cpp`, два CMakeLists, `baseline.json`, `backlog.md` |
| Тестов добавлено | 55 |
| Тестов всего | 367 |
| Бенчмарков добавлено | 4 |
| Строк изменено в парсере, лексере и дереве | 0 |

Часть 2 закончена, когда: `ctest` даёт 367 из 367 в обычной сборке, под ASan+UBSan и с `-Werror`; `benchmarks/baseline.json` содержит `BM_Eval_Arithmetic`, `BM_Eval_LogicalChain` и обе строки `??`; B15 закрыт.

Следующая часть — стейтменты и встроенные: присваивание и составное присваивание (§7), режим скрипта, тринадцать функций (§8) и статические проверки арности вместе с `Void` (B11).
