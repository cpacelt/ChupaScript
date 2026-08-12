# ChupaScript: вычислитель, часть 1 — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Вычислять выражения без операторов и вызовов: литералы, чтение корня, `user.profile.avatar`, `state.items[0]`, `obj[k]` с любым скалярным ключом.

**Architecture:** Рекурсивный обход дерева — `eval(ast, node, ctx, out, diag)` спускается по узлам, промежуточные значения живут в кадрах. Собственного предела глубины нет: её ограничил парсер. Раскодирование экранирования и представление числа переезжают в общий `core/src/text.*`, откуда их берут слой данных, вычислитель и — позже — билтины.

**Tech Stack:** C++17, gtest, Google Benchmark, CMake. У самой библиотеки зависимостей нет.

**Спека:** `docs/superpowers/specs/2026-08-12-chupascript-eval-core-design.md` — нормативна для этого плана.
**Семантика:** `docs/semantics.md` §2.3 (ссылочность), §3.3 (порядок вычисления), §4 (приведение), §6 (доступ к данным), §7.1 (имена), §9.2 (ошибки выполнения).
**Грамматика:** `docs/grammar.md` Приложение A (набор экранирования), Приложение C.1 (предел глубины).

## Global Constraints

- **C++17.** Стандарт задан в корневом `CMakeLists.txt`, повышать нельзя.
- **У библиотеки нет зависимостей.** gtest и Google Benchmark подтягиваются только для тестов и бенчмарков.
- **Комментарии и документация — по-русски.** Сообщения диагностики — по-английски, как в лексере, парсере и слое данных.
- **`core/src/parser.*`, `core/src/lexer.*`, `core/src/ast.*` не меняются ни одной строкой.**
- **Часть 1 не содержит ни одного оператора и ни одного вызова.** Узел, которого она не знает, даёт ошибку `Type` с сообщением `expression form is not supported`.
- **Собственного предела глубины у вычислителя нет:** глубина обхода равна глубине дерева, а её ограничил парсер.
- **`Value` передаётся и возвращается копией.**
- **Чтение за границей массива и чтение отсутствующего ключа дают `null`;** дробный, отрицательный, `NaN` и бесконечный индекс — ошибка `Range`; нечисловой индекс массива и доступ у неподходящего типа — ошибка `Type`; неизвестный корень — ошибка `Name`.
- **Приведение к строке односторонее и только для скаляров.** Агрегат — ошибка `Type`.
- **Смещение в диагностике — от узла, на котором остановились.** Первая ошибка выигрывает.
- **Сборка тестов:** `cmake -B build && cmake --build build -j`, прогон `ctest --test-dir build --output-on-failure`.
- **Сборка бенчмарков:** только Release — `cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON`.
- **Коммитить явными путями.** `git add -A` не использовать.

---

## Структура файлов

| Файл | Ответственность |
|---|---|
| `core/src/text.hpp` / `.cpp` | преобразования текст ↔ значение: раскодирование экранирования, содержимое литерала, представление числа |
| `core/src/eval.hpp` | объявление `evalExpression` — вся публичная поверхность слоя |
| `core/src/eval.cpp` | рекурсивный обход, доступ к данным, приведение к строке |
| `core/tests/text_test.cpp` | представление числа и раскодирование |
| `core/tests/eval_test.cpp` | вычисление |
| `benchmarks/eval_benchmark.cpp` | дописывается; существующий `BM_Version` остаётся |

Существующие интерфейсы, которыми пользуется план:

```cpp
// core/src/ast.hpp
NodeKind kind(NodeId) const noexcept;     // Number, String, Boolean, Null, Array, Object,
                                          // Identifier, Member, Index, Call, Unary, Binary,
                                          // Conditional, Assign, CallStatement, Program, Invalid
std::uint32_t offset(NodeId) const noexcept;
std::uint32_t childCount(NodeId) const noexcept;
NodeId child(NodeId, std::uint32_t) const noexcept;
double numberValue(NodeId) const noexcept;
bool boolValue(NodeId) const noexcept;
std::string_view text(NodeId) const noexcept;
bool hasEscape(NodeId) const noexcept;
NodeId root() const noexcept;             // kNoNode, если разбор не удался

// core/src/context.hpp
Value makeString(std::string_view);
Value makeArray(std::uint32_t capacity = 0);
Value makeObject(std::uint32_t capacity = 0);
std::string_view string(Value) const noexcept;
std::uint32_t arrayCount(Value) const noexcept;
Value arrayAt(Value, std::uint32_t) const noexcept;
Value objectGet(Value, std::string_view) const noexcept;
void arrayPush(Value, Value);
void objectSet(Value, std::string_view, Value);
Value root(std::string_view) const noexcept;
bool hasRoot(std::string_view) const noexcept;

// core/src/value.hpp
enum class Value::Kind : std::uint8_t { Null, Boolean, Number, String, Object, Array };
static Value null() / boolean(bool) / number(double);
Kind kind() const noexcept; bool booleanValue() const; double numberValue() const;
bool sameAggregate(Value) const noexcept;

// core/src/parser.hpp — не меняется
bool parseExpression(const char *source, std::uint32_t length, Ast &ast, Diagnostic &diag);

// core/src/data.hpp
bool setVariable(Context &ctx, std::string_view name, std::string_view text, Diagnostic &diag);

// core/src/diagnostic.hpp
enum class ErrorCode : std::uint8_t { None, Syntax, Name, Type, Range, Data, Usage, Memory };
struct Diagnostic { ErrorCode code; std::uint32_t offset; const char *message; };
```

---

## Задача 1: `text.*` — переезд и представление числа

**Files:**
- Create: `core/src/text.hpp`, `core/src/text.cpp`, `core/tests/text_test.cpp`
- Modify: `core/src/data.cpp` (оттуда уезжают две функции), `core/CMakeLists.txt:1-9`, `core/tests/CMakeLists.txt:1-9`

**Interfaces:**
- Consumes: `Ast::kind`, `Ast::hasEscape`, `Ast::text`.
- Produces: `std::string_view CS::decodeEscapes(std::string_view raw, std::string &scratch)`, `std::string_view CS::literalText(const Ast &ast, NodeId node, std::string &scratch)`, `std::string_view CS::formatNumber(double value, char *buffer, std::size_t size)`, `inline constexpr std::size_t CS::kNumberBufferSize = 48`.

- [ ] **Шаг 1: Написать тесты**

Создать `core/tests/text_test.cpp`:

```cpp
#include "text.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

namespace {

/// Представление числа в строку — обёртка, скрывающая буфер.
std::string format(double value) {
    char buffer[CS::kNumberBufferSize];
    return std::string(CS::formatNumber(value, buffer, sizeof buffer));
}

TEST(FormatNumber, ExamplesFromTheSpec) {
    // docs/semantics.md §4.3, таблица примеров целиком.
    EXPECT_EQ(format(1.0), "1");
    EXPECT_EQ(format(1.5), "1.5");
    EXPECT_EQ(format(1000000.0), "1000000");
    EXPECT_EQ(format(0.1 + 0.2), "0.30000000000000004");
    EXPECT_EQ(format(1e21), "1e+21");
}

TEST(FormatNumber, SpecialValues) {
    EXPECT_EQ(format(std::numeric_limits<double>::infinity()), "inf");
    EXPECT_EQ(format(-std::numeric_limits<double>::infinity()), "-inf");
    EXPECT_EQ(format(std::numeric_limits<double>::quiet_NaN()), "nan");
}

TEST(FormatNumber, ZeroKeepsItsSign) {
    // Ноль обрабатывается отдельно: общее правило дало бы 0e+00.
    EXPECT_EQ(format(0.0), "0");
    EXPECT_EQ(format(-0.0), "-0");
}

TEST(FormatNumber, UpperThreshold) {
    // Ниже 1e21 — фиксированная запись, начиная с 1e21 — научная.
    EXPECT_EQ(format(1e20), "100000000000000000000");
    EXPECT_EQ(format(1e21), "1e+21");
}

TEST(FormatNumber, LowerThreshold) {
    // 1e-7 ещё фиксированная, 1e-8 уже научная.
    EXPECT_EQ(format(1e-7), "0.0000001");
    EXPECT_EQ(format(1e-8), "1e-08");
}

TEST(FormatNumber, ExponentKeepsTwoDigits) {
    // За порогом отдаём ровно то, что даёт to_chars: расхождение с JavaScript,
    // который написал бы 1e-8, сознательное (спека §7.1).
    EXPECT_EQ(format(1e-8), "1e-08");
    EXPECT_EQ(format(1e300), "1e+300");
}

TEST(FormatNumber, NegativeValues) {
    EXPECT_EQ(format(-1.5), "-1.5");
    EXPECT_EQ(format(-1e21), "-1e+21");
}

TEST(FormatNumber, RoundTripsThroughTheShortestForm) {
    // Кратчайшее представление обязано читаться обратно в то же значение.
    const double values[] = {0.1, 1.0 / 3.0, 1e-5, 12345.6789, 2.2250738585072014e-308};
    for (double value : values) {
        EXPECT_EQ(std::stod(format(value)), value) << format(value);
    }
}

TEST(DecodeEscapes, WithoutEscapesTheTextIsUnchanged) {
    std::string scratch;
    EXPECT_EQ(CS::decodeEscapes("abc", scratch), "abc");
}

TEST(DecodeEscapes, WholeSetIsDecoded) {
    // docs/grammar.md Приложение A: \\ \' \" \n \t и больше ничего.
    std::string scratch;
    EXPECT_EQ(CS::decodeEscapes("a\\nb", scratch), "a\nb");
    EXPECT_EQ(CS::decodeEscapes("a\\tb", scratch), "a\tb");
    EXPECT_EQ(CS::decodeEscapes("a\\\\b", scratch), "a\\b");
    EXPECT_EQ(CS::decodeEscapes("a\\'b", scratch), "a'b");
    EXPECT_EQ(CS::decodeEscapes("a\\\"b", scratch), "a\"b");
}

TEST(DecodeEscapes, ScratchIsReusable) {
    // Один буфер обслуживает несколько вызовов подряд — так им пользуется
    // построение объекта, где ключи раскодируются по очереди.
    std::string scratch;
    EXPECT_EQ(CS::decodeEscapes("\\n", scratch), "\n");
    EXPECT_EQ(CS::decodeEscapes("\\t\\t", scratch), "\t\t");
    EXPECT_EQ(CS::decodeEscapes("", scratch), "");
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
    src/lexer.cpp
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
    lexer_test.cpp
    parser_test.cpp
    smoke_test.cpp
    text_test.cpp
    value_test.cpp
)
```

- [ ] **Шаг 3: Убедиться, что не собирается**

Run: `cmake -B build && cmake --build build -j`
Expected: ошибка — `text.hpp` не существует.

- [ ] **Шаг 4: Написать `core/src/text.hpp`**

```cpp
#pragma once
#include <cstddef>
#include <string>
#include <string_view>

#include "ast.hpp"

namespace CS {

/// Раскодирует экранирование строкового литерала в scratch.
///
/// Набор — из docs/grammar.md Приложение A: \\ \' \" \n \t. Неизвестной
/// последовательности здесь быть не может: её отверг лексер.
///
/// Возвращает срез scratch. Буфер очищается при каждом вызове и потому
/// пригоден для повторного использования.
std::string_view decodeEscapes(std::string_view raw, std::string &scratch);

/// Содержимое строкового литерала: срез исходника, если экранирования нет,
/// иначе раскодированное в scratch.
///
/// Флаг hasEscape избавляет от временного буфера в подавляющем большинстве
/// случаев: экранирование редкость.
///
/// Предусловие: ast.kind(node) == NodeKind::String.
std::string_view literalText(const Ast &ast, NodeId node, std::string &scratch);

/// Достаточный размер буфера под formatNumber.
///
/// Худшая фиксированная запись внутри порога занимает около тридцати символов,
/// худшая научная — двадцать четыре; запас взят, чтобы вопрос не возвращался.
inline constexpr std::size_t kNumberBufferSize = 48;

/// Представление числа по docs/semantics.md §4.3: кратчайшая десятичная запись,
/// читающаяся обратно в то же значение double.
///
/// Порядок правил: nan и бесконечности; ноль со своим знаком; фиксированная
/// запись при 1e-7 <= |x| < 1e21; научная вне порога.
///
/// Предусловие: size >= kNumberBufferSize. Возвращает срез buffer либо
/// статическую строку для особых значений.
std::string_view formatNumber(double value, char *buffer, std::size_t size);

}  // namespace CS
```

- [ ] **Шаг 5: Написать `core/src/text.cpp`**

```cpp
#include "text.hpp"

#include <cassert>
#include <charconv>
#include <cmath>
#include <system_error>

namespace CS {

std::string_view decodeEscapes(std::string_view raw, std::string &scratch) {
    scratch.clear();
    scratch.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\') {
            scratch.push_back(raw[i]);
            continue;
        }
        assert(i + 1 < raw.size() && "лексер не пропустил бы висячий слэш");
        ++i;
        switch (raw[i]) {
            case 'n': scratch.push_back('\n'); break;
            case 't': scratch.push_back('\t'); break;
            case '\\': scratch.push_back('\\'); break;
            case '\'': scratch.push_back('\''); break;
            case '"': scratch.push_back('"'); break;
            default: assert(false && "лексер отверг бы такую последовательность");
        }
    }
    return scratch;
}

std::string_view literalText(const Ast &ast, NodeId node, std::string &scratch) {
    assert(ast.kind(node) == NodeKind::String);
    if (!ast.hasEscape(node)) { return ast.text(node); }
    return decodeEscapes(ast.text(node), scratch);
}

std::string_view formatNumber(double value, char *buffer, std::size_t size) {
    assert(size >= kNumberBufferSize);

    if (std::isnan(value)) { return "nan"; }
    if (std::isinf(value)) { return value > 0.0 ? "inf" : "-inf"; }
    // Ноль отдельно: общее правило отправило бы его в научную запись, где он
    // выглядит как 0e+00.
    if (value == 0.0) { return std::signbit(value) ? "-0" : "0"; }

    // Порог выбран так, чтобы 1000000 осталось 1000000, а 1e21 стало 1e+21 —
    // ровно как в таблице примеров docs/semantics.md §4.3.
    const double magnitude = std::fabs(value);
    const std::chars_format format = (magnitude >= 1e-7 && magnitude < 1e21)
                                         ? std::chars_format::fixed
                                         : std::chars_format::scientific;

    const std::to_chars_result result =
        std::to_chars(buffer, buffer + size, value, format);
    assert(result.ec == std::errc() && "kNumberBufferSize оказался мал");
    return std::string_view(buffer,
                            static_cast<std::size_t>(result.ptr - buffer));
}

}  // namespace CS
```

- [ ] **Шаг 6: Убрать переехавшее из `core/src/data.cpp`**

Удалить из анонимного пространства имён определения `decodeEscapes` и `literalText` целиком. Добавить `#include "text.hpp"` к списку включений. Прежний `decodeEscapes` возвращал `void`; вызовов у него не остаётся, а `literalText` зовётся из `materialize` — она продолжает работать без изменений, потому что имя и подпись у новой функции те же.

Проверить, что `#include <string>` в `data.cpp` всё ещё нужен: `std::string scratch` в `materialize` остаётся.

- [ ] **Шаг 7: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "FormatNumber|DecodeEscapes"`
Expected: 11 тестов PASS.

- [ ] **Шаг 8: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 267 тестов PASS (256 было + 11). Тесты слоя данных обязаны пройти без изменений: функция переехала, поведение — нет.

- [ ] **Шаг 9: Коммит**

```bash
git add core/src/text.hpp core/src/text.cpp core/src/data.cpp \
        core/tests/text_test.cpp core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "Move text conversions into their own unit and add number formatting"
```

---

## Задача 2: Скелет вычислителя и скалярные литералы

**Files:**
- Create: `core/src/eval.hpp`, `core/src/eval.cpp`, `core/tests/eval_test.cpp`
- Modify: `core/CMakeLists.txt:1-10`, `core/tests/CMakeLists.txt:1-10`

**Interfaces:**
- Consumes: `literalText` из задачи 1; `parseExpression`; `Context::makeString`.
- Produces: `bool CS::evalExpression(const Ast &ast, Context &ctx, Value *out, Diagnostic &diag)`. В `eval.cpp` появляются внутренние `bool fail(const Ast &, NodeId, ErrorCode, const char *, Diagnostic &)` и `bool eval(const Ast &, NodeId, Context &, Value *, Diagnostic &)`, которые расширяют задачи 3–6.

- [ ] **Шаг 1: Написать тесты**

Создать `core/tests/eval_test.cpp`:

```cpp
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
    parser_test.cpp
    smoke_test.cpp
    text_test.cpp
    value_test.cpp
)
```

- [ ] **Шаг 3: Убедиться, что не собирается**

Run: `cmake -B build && cmake --build build -j`
Expected: ошибка — `eval.hpp` не существует.

- [ ] **Шаг 4: Написать `core/src/eval.hpp`**

```cpp
#pragma once
#include "ast.hpp"
#include "context.hpp"
#include "diagnostic.hpp"
#include "value.hpp"

namespace CS {

/// Вычисляет разобранное выражение и кладёт результат в *out.
///
/// Дерево обязано быть построено parseExpression успешно: у неудачного разбора
/// корень равен kNoNode, и вычислять там нечего. Буфер исходника обязан
/// пережить вычисление — имена и содержимое литералов в дереве это его срезы.
///
/// Значения-агрегаты создаются в ctx и живут по его правилам. При отказе
/// возвращает false и заполняет diag; смещение считается от начала исходника
/// выражения.
bool evalExpression(const Ast &ast, Context &ctx, Value *out, Diagnostic &diag);

}  // namespace CS
```

- [ ] **Шаг 5: Написать `core/src/eval.cpp`**

```cpp
#include "eval.hpp"

#include <cassert>
#include <string>

#include "text.hpp"

namespace CS {
namespace {

/// Записывает отказ с местом узла. Первая ошибка выигрывает: вызывающие
/// возвращают false немедленно и диагностику не переписывают.
bool fail(const Ast &ast, NodeId node, ErrorCode code, const char *message,
          Diagnostic &diag) {
    diag = Diagnostic{code, ast.offset(node), message};
    return false;
}

/// Обход дерева. Рекурсия, а не цикл: короткому замыканию нужен пропуск
/// поддеревьев, а циклу — буфер значений на всё дерево (спека §3). Собственного
/// предела глубины нет — её ограничил парсер.
bool eval(const Ast &ast, NodeId node, Context &ctx, Value *out,
          Diagnostic &diag) {
    switch (ast.kind(node)) {
        case NodeKind::Number:
            *out = Value::number(ast.numberValue(node));
            return true;

        case NodeKind::Boolean:
            *out = Value::boolean(ast.boolValue(node));
            return true;

        case NodeKind::Null:
            *out = Value::null();
            return true;

        case NodeKind::String: {
            std::string scratch;
            *out = ctx.makeString(literalText(ast, node, scratch));
            return true;
        }

        default:
            // Часть 1 не знает операторов и вызовов. С приходом частей 2 и 3
            // ветка сузится до Program, Assign и CallStatement — узлов, которых
            // в дереве от parseExpression быть не может, — и станет защитной.
            return fail(ast, node, ErrorCode::Type,
                        "expression form is not supported", diag);
    }
}

}  // namespace

bool evalExpression(const Ast &ast, Context &ctx, Value *out,
                    Diagnostic &diag) {
    assert(ast.root() != kNoNode && "дерево обязано быть разобрано успешно");
    return eval(ast, ast.root(), ctx, out, diag);
}

}  // namespace CS
```

- [ ] **Шаг 6: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "EvalLiterals|EvalUnsupported"`
Expected: 6 тестов PASS.

- [ ] **Шаг 7: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 273 теста PASS.

- [ ] **Шаг 8: Коммит**

```bash
git add core/src/eval.hpp core/src/eval.cpp core/tests/eval_test.cpp \
        core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "Evaluate scalar literals"
```

---

## Задача 3: Имена и корни

**Files:**
- Modify: `core/src/eval.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `eval`, `fail` из задачи 2; `Context::hasRoot`, `Context::root`.
- Produces: ветка `NodeKind::Identifier` в `eval`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
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

```

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalNames`
Expected: FAIL — идентификатор попадает в ветку `default` и даёт `Type` вместо чтения корня.

- [ ] **Шаг 3: Добавить ветку в `core/src/eval.cpp`**

В `switch` перед `default`:

```cpp
        case NodeKind::Identifier: {
            // docs/semantics.md §7.1: объявлений в языке нет, всякий
            // идентификатор — чтение из контекста.
            const std::string_view name = ast.text(node);
            // Неизвестный корень — ошибка, а не null: состав корней контексту
            // известен, состав ключей внутри них — нет. Поэтому опечатка в
            // корне ловится, а опечатка глубже даёт null по §6.3.
            if (!ctx.hasRoot(name)) {
                return fail(ast, node, ErrorCode::Name, "unknown root", diag);
            }
            *out = ctx.root(name);
            return true;
        }
```

Добавить `#include <string_view>` к включениям, если его там ещё нет.

- [ ] **Шаг 4: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalNames`
Expected: 4 теста PASS.

- [ ] **Шаг 5: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 277 тестов PASS.

- [ ] **Шаг 6: Коммит**

```bash
git add core/src/eval.cpp core/tests/eval_test.cpp
git commit -m "Read context roots by name"
```

---

## Задача 4: Доступ по имени поля

**Files:**
- Modify: `core/src/eval.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `eval`, `fail`; `Context::objectGet`.
- Produces: внутренняя `bool readKey(const Ast &, NodeId, Context &, Value base, std::string_view key, Value *, Diagnostic &)` и ветка `NodeKind::Member`. `readKey` понадобится задаче 5 для объектного индекса.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(EvalMember, ExistingKeyIsRead) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася', 'age': 30}");
    EXPECT_EQ(ctx.string(evaluate(ctx, "user.name")), "Вася");
    EXPECT_EQ(evaluate(ctx, "user.age").numberValue(), 30.0);
}

TEST(EvalMember, MissingKeyReadsAsNull) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // docs/semantics.md §6.2: отсутствующий ключ читается как null.
    EXPECT_EQ(evaluate(ctx, "user.nickname").kind(), Value::Kind::Null);
}

TEST(EvalMember, ReadingThroughNullGivesNull) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // docs/semantics.md §6.3: путь любой глубины безопасен, а опечатка глубже
    // первого сегмента не диагностируется — это цена правила.
    EXPECT_EQ(evaluate(ctx, "user.prfoile.avatar").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(ctx, "user.a.b.c.d.e").kind(), Value::Kind::Null);
}

TEST(EvalMember, ReadingKeyOffANonObjectIsAnError) {
    Context ctx;
    put(ctx, "count", "3");
    put(ctx, "name", "'Вася'");
    put(ctx, "flag", "true");
    put(ctx, "items", "[1, 2]");
    // docs/semantics.md §6.4: доступ по ключу определён для Object, чтение у
    // null — правилом §6.3, прочее — ошибка.
    EXPECT_EQ(evalError(ctx, "count.x").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "name.x").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "flag.x").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "items.x").code, CS::ErrorCode::Type);
}

TEST(EvalMember, KeyIsTakenLiterallyNotAsAName) {
    Context ctx;
    put(ctx, "o", "{'name': 'ключ'}");
    put(ctx, "name", "'корень'");
    // docs/semantics.md §6.2: в форме obj.k ключом является имя k буквально, а
    // не значение корня, который случайно называется так же.
    EXPECT_EQ(ctx.string(evaluate(ctx, "o.name")), "ключ");
}

TEST(EvalMember, UnknownRootIsAnErrorAtAnyDepth) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // База вычисляется рекурсивно, поэтому опечатка в корне всплывает с любой
    // глубины пути: usre.a.b спускается к usre и упирается в неизвестный
    // корень. Частного случая для первого сегмента не нужно.
    EXPECT_EQ(evalError(ctx, "usre.name").code, CS::ErrorCode::Name);
    EXPECT_EQ(evalError(ctx, "usre.a.b").code, CS::ErrorCode::Name);
}

TEST(EvalMember, OffsetPointsAtTheFailingNode) {
    Context ctx;
    put(ctx, "count", "3");
    // Место ошибки — там, где чинить, а не в начале выражения.
    EXPECT_GT(evalError(ctx, "count.a.b").offset, 0u);
}
```

Ключа, совпадающего с зарезервированным словом, в этом наборе нет намеренно: `docs/semantics.md` §6.2 прямо говорит, что такие ключи доступны только через `obj['true']`, — форма с точкой требует идентификатора. Проверка скобочной формы живёт в задаче 5.

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalMember`
Expected: FAIL — узел `Member` попадает в `default`.

- [ ] **Шаг 3: Добавить в `core/src/eval.cpp`**

В анонимное пространство имён, перед `eval`, добавить объявление и определение:

```cpp
/// Чтение ключа у базы (docs/semantics.md §6.2, §6.3, §6.4).
///
/// Объект — значение либо null; null — null; прочее — ошибка. Один и тот же
/// разбор обслуживает и obj.k, и obj[k]: отличаются они только тем, откуда
/// берётся ключ.
bool readKey(const Ast &ast, NodeId node, Context &ctx, Value base,
             std::string_view key, Value *out, Diagnostic &diag) {
    switch (base.kind()) {
        case Value::Kind::Object:
            *out = ctx.objectGet(base, key);
            return true;
        case Value::Kind::Null:
            *out = Value::null();
            return true;
        default:
            return fail(ast, node, ErrorCode::Type, "only objects have keys",
                        diag);
    }
}
```

В `switch` функции `eval`, перед `default`:

```cpp
        case NodeKind::Member: {
            Value base = Value::null();
            if (!eval(ast, ast.child(node, 0), ctx, &base, diag)) { return false; }
            // Имя поля берётся из узла буквально, без приведения.
            return readKey(ast, node, ctx, base, ast.text(node), out, diag);
        }
```

`eval` вызывает сам себя, поэтому перед `readKey` нужно предварительное объявление `eval`; поставить его первой строкой анонимного пространства имён:

```cpp
bool eval(const Ast &ast, NodeId node, Context &ctx, Value *out,
          Diagnostic &diag);
```

- [ ] **Шаг 4: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalMember`
Expected: 7 тестов PASS.

- [ ] **Шаг 5: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 284 теста PASS.

- [ ] **Шаг 6: Коммит**

```bash
git add core/src/eval.cpp core/tests/eval_test.cpp
git commit -m "Read object keys through the dot form"
```

---

## Задача 5: Индексация и приведение к строке

**Files:**
- Modify: `core/src/eval.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `eval`, `fail`, `readKey` из задач 2–4; `formatNumber` и `kNumberBufferSize` из задачи 1; `Context::arrayCount`, `Context::arrayAt`, `Context::string`.
- Produces: внутренние `bool coerceToString(const Ast &, NodeId, Context &, Value, char *, std::string_view *, Diagnostic &)` и `bool readIndex(const Ast &, NodeId, Context &, Value array, Value subscript, Value *, Diagnostic &)`; ветка `NodeKind::Index`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(EvalIndex, ArrayElementIsRead) {
    Context ctx;
    put(ctx, "items", "[10, 20, 30]");
    EXPECT_EQ(evaluate(ctx, "items[0]").numberValue(), 10.0);
    EXPECT_EQ(evaluate(ctx, "items[2]").numberValue(), 30.0);
}

TEST(EvalIndex, ArrayReadBeyondEndGivesNull) {
    Context ctx;
    put(ctx, "items", "[10]");
    // docs/semantics.md §6.1: чтение за границей штатно — данные неполны.
    EXPECT_EQ(evaluate(ctx, "items[1]").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(ctx, "items[1000000]").kind(), Value::Kind::Null);
}

TEST(EvalIndex, FractionalAndNegativeIndicesAreErrors) {
    Context ctx;
    put(ctx, "items", "[10, 20]");
    put(ctx, "minusOne", "-1");
    put(ctx, "huge", std::string(400, '9'));
    // Дробный и отрицательный индекс означают намерение, которого в языке нет:
    // приведения к целому тоже нет. Отрицательное значение и бесконечность
    // берутся из данных — унарный минус это оператор, а операторов в части 1
    // нет; четыреста девяток переполняют double и дают inf. Без последней
    // строки проверка !isfinite в readIndex не покрыта ничем.
    EXPECT_EQ(evalError(ctx, "items[0.5]").code, CS::ErrorCode::Range);
    EXPECT_EQ(evalError(ctx, "items[minusOne]").code, CS::ErrorCode::Range);
    EXPECT_EQ(evalError(ctx, "items[huge]").code, CS::ErrorCode::Range);
}

TEST(EvalIndex, NonNumberArrayIndexIsAnError) {
    Context ctx;
    put(ctx, "items", "[10, 20]");
    // docs/semantics.md §6.1: приведения к Number нет, поэтому items['0']
    // не работает.
    EXPECT_EQ(evalError(ctx, "items['0']").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "items[true]").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "items[null]").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, ObjectKeyIsRead) {
    Context ctx;
    put(ctx, "o", "{'name': 'Вася'}");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o['name']")), "Вася");
    EXPECT_EQ(evaluate(ctx, "o['missing']").kind(), Value::Kind::Null);
}

TEST(EvalIndex, ScalarKeysAreCoercedToString) {
    Context ctx;
    put(ctx, "o", "{'0': 'zero', 'true': 'yes', 'null': 'nothing', '1.5': 'half'}");
    // docs/semantics.md §4.1: ключ объекта — одна из трёх позиций, требующих
    // String, и приведение туда одностороннее.
    EXPECT_EQ(ctx.string(evaluate(ctx, "o[0]")), "zero");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o[true]")), "yes");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o[null]")), "nothing");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o[1.5]")), "half");
}

TEST(EvalIndex, NegativeZeroAndZeroAreDifferentKeys) {
    Context ctx;
    put(ctx, "o", "{'0': 'plus', '-0': 'minus'}");
    put(ctx, "minusZero", "-0");
    // docs/semantics.md §4.3: -0 == 0 истинно, но ключи разные, потому что
    // представление числа сохраняет знак нуля. Отрицательный ноль приходит из
    // данных по той же причине, что и в тесте выше.
    EXPECT_EQ(ctx.string(evaluate(ctx, "o[0]")), "plus");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o[minusZero]")), "minus");
}

TEST(EvalIndex, AggregateKeyIsAnError) {
    Context ctx;
    put(ctx, "o", "{'a': 1}");
    put(ctx, "items", "[1]");
    // Агрегат не приводится никуда (docs/semantics.md §4).
    EXPECT_EQ(evalError(ctx, "o[items]").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, ReadingThroughNullGivesNull) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    EXPECT_EQ(evaluate(ctx, "user.missing[0]").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(ctx, "user.missing['k']").kind(), Value::Kind::Null);
}

TEST(EvalIndex, SubscriptIsEvaluatedEvenWhenTheBaseIsNull) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // docs/semantics.md §3.3: порядок зафиксирован, короткое замыкание есть
    // только у логических, ?? и тернарного. Ошибка в индексе обязана всплыть,
    // а не быть съеденной null-базой. Побочных эффектов в выражениях нет, так
    // что наблюдать порядок можно только через ошибку.
    EXPECT_EQ(evalError(ctx, "user.missing[usre]").code, CS::ErrorCode::Name);
}

TEST(EvalIndex, IndexingANonAggregateIsAnError) {
    Context ctx;
    put(ctx, "count", "3");
    put(ctx, "name", "'Вася'");
    put(ctx, "flag", "true");
    // docs/semantics.md §6.4: 'abc'[0] — ошибка, строка не индексируется.
    EXPECT_EQ(evalError(ctx, "count[0]").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "name[0]").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "flag[0]").code, CS::ErrorCode::Type);
}

TEST(EvalIndex, ChainedAccessWorks) {
    Context ctx;
    put(ctx, "state", "{'items': [{'id': 1}, {'id': 2}]}");
    EXPECT_EQ(evaluate(ctx, "state.items[1].id").numberValue(), 2.0);
}
```

Отрицательные значения в двух тестах выше приходят из данных, а не из выражения, и это не обходной приём: унарный минус — оператор (`docs/semantics.md` §5.1), а часть 1 операторов не знает. Проверяемые же свойства принадлежат индексации и приведению, а не минусу, поэтому проверять их надо здесь. Слой данных умеет отрицательные литералы, потому что там минус — запись значения, а не вычисление.

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalIndex`
Expected: FAIL — узел `Index` попадает в `default`.

- [ ] **Шаг 3: Добавить приведение и чтение элемента в `core/src/eval.cpp`**

В анонимное пространство имён, после `readKey`:

```cpp
/// Приведение скаляра к строке (docs/semantics.md §4).
///
/// Возвращает срез: у строки — её собственные байты в контексте, у числа —
/// буфер вызывающего, у остальных — статическая строка. В контекст ничего не
/// кладётся: строка нужна на время одного поиска ключа, и класть её в пул
/// значило бы копить мусор на каждом чтении.
///
/// numberBuffer обязан быть размером не меньше kNumberBufferSize.
bool coerceToString(const Ast &ast, NodeId node, Context &ctx, Value value,
                    char *numberBuffer, std::string_view *out,
                    Diagnostic &diag) {
    switch (value.kind()) {
        case Value::Kind::String:
            *out = ctx.string(value);
            return true;
        case Value::Kind::Boolean:
            *out = value.booleanValue() ? "true" : "false";
            return true;
        case Value::Kind::Null:
            *out = "null";
            return true;
        case Value::Kind::Number:
            *out = formatNumber(value.numberValue(), numberBuffer,
                                kNumberBufferSize);
            return true;
        default:
            return fail(ast, node, ErrorCode::Type,
                        "aggregates cannot be converted to string", diag);
    }
}

/// Чтение элемента массива (docs/semantics.md §6.1).
bool readIndex(const Ast &ast, NodeId node, Context &ctx, Value array,
               Value subscript, Value *out, Diagnostic &diag) {
    if (subscript.kind() != Value::Kind::Number) {
        return fail(ast, node, ErrorCode::Type, "array index must be a number",
                    diag);
    }

    const double index = subscript.numberValue();
    // Дробный и отрицательный индекс — ошибка автора, а не неполнота данных:
    // приведения к целому в языке нет.
    if (!std::isfinite(index) || index < 0.0 || index != std::floor(index)) {
        return fail(ast, node, ErrorCode::Range,
                    "array index must be a non-negative integer", diag);
    }

    // За границей — штатное чтение. Сравнение в double, потому что индекс
    // может превышать всё, что влезает в uint32.
    if (index >= static_cast<double>(ctx.arrayCount(array))) {
        *out = Value::null();
        return true;
    }
    *out = ctx.arrayAt(array, static_cast<std::uint32_t>(index));
    return true;
}
```

Добавить `#include <cmath>` и `#include <cstdint>` к включениям.

- [ ] **Шаг 4: Добавить ветку индексации**

В `switch` функции `eval`, перед `default`:

```cpp
        case NodeKind::Index: {
            Value base = Value::null();
            if (!eval(ast, ast.child(node, 0), ctx, &base, diag)) { return false; }
            // Индекс вычисляется даже при базе null: порядок зафиксирован
            // (docs/semantics.md §3.3), а короткого замыкания у индексации нет.
            Value subscript = Value::null();
            if (!eval(ast, ast.child(node, 1), ctx, &subscript, diag)) {
                return false;
            }

            switch (base.kind()) {
                case Value::Kind::Array:
                    return readIndex(ast, node, ctx, base, subscript, out, diag);
                case Value::Kind::Object: {
                    char buffer[kNumberBufferSize];
                    std::string_view key;
                    if (!coerceToString(ast, node, ctx, subscript, buffer, &key,
                                        diag)) {
                        return false;
                    }
                    return readKey(ast, node, ctx, base, key, out, diag);
                }
                case Value::Kind::Null:
                    *out = Value::null();
                    return true;
                default:
                    return fail(ast, node, ErrorCode::Type,
                                "only arrays and objects can be indexed", diag);
            }
        }
```

- [ ] **Шаг 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalIndex`
Expected: 12 тестов PASS.

- [ ] **Шаг 6: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 296 тестов PASS.

- [ ] **Шаг 7: Прогнать под санитайзерами**

```bash
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

Expected: 296 PASS, ни одного отчёта. Приведение возвращает срез буфера, живущего на стеке вызывающего, — именно то место, где ошибка времени жизни была бы незаметна в обычной сборке.

- [ ] **Шаг 8: Коммит**

```bash
git add core/src/eval.cpp core/tests/eval_test.cpp
git commit -m "Index arrays and objects, coercing scalar keys to string"
```

---

## Задача 6: Агрегатные литералы

**Files:**
- Modify: `core/src/eval.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `eval`; `literalText` из задачи 1; `Context::makeArray`, `arrayPush`, `makeObject`, `objectSet`.
- Produces: ветки `NodeKind::Array` и `NodeKind::Object` в `eval`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(EvalAggregates, ArrayLiteralKeepsOrder) {
    Context ctx;
    const Value a = evaluate(ctx, "[1, 2, 3]");
    ASSERT_EQ(ctx.arrayCount(a), 3u);
    EXPECT_EQ(ctx.arrayAt(a, 0).numberValue(), 1.0);
    EXPECT_EQ(ctx.arrayAt(a, 2).numberValue(), 3.0);
}

TEST(EvalAggregates, ObjectLiteralStoresPairs) {
    Context ctx;
    const Value o = evaluate(ctx, "{'a': 1, 'b': 2}");
    ASSERT_EQ(ctx.objectCount(o), 2u);
    EXPECT_EQ(ctx.objectGet(o, "a").numberValue(), 1.0);
    EXPECT_EQ(ctx.objectGet(o, "b").numberValue(), 2.0);
}

TEST(EvalAggregates, EmptyLiterals) {
    Context ctx;
    EXPECT_EQ(ctx.arrayCount(evaluate(ctx, "[]")), 0u);
    EXPECT_EQ(ctx.objectCount(evaluate(ctx, "{}")), 0u);
}

TEST(EvalAggregates, ElementsAreArbitraryExpressions) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    put(ctx, "items", "[7]");
    // Вот чем агрегат в выражении отличается от агрегата в данных: элемент —
    // выражение, а не литерал.
    const Value a = evaluate(ctx, "[user.name, items[0], user.missing]");
    ASSERT_EQ(ctx.arrayCount(a), 3u);
    EXPECT_EQ(ctx.string(ctx.arrayAt(a, 0)), "Вася");
    EXPECT_EQ(ctx.arrayAt(a, 1).numberValue(), 7.0);
    EXPECT_EQ(ctx.arrayAt(a, 2).kind(), Value::Kind::Null);
}

TEST(EvalAggregates, ObjectValuesAreExpressionsAndKeysAreLiterals) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    const Value o = evaluate(ctx, "{'who': user.name}");
    EXPECT_EQ(ctx.string(ctx.objectGet(o, "who")), "Вася");
}

TEST(EvalAggregates, ErrorInsideAnElementStopsEvaluation) {
    Context ctx;
    EXPECT_EQ(evalError(ctx, "[1, usre, 3]").code, CS::ErrorCode::Name);
}

TEST(EvalAggregates, EachEvaluationCreatesANewAggregate) {
    Context ctx;
    Ast ast;
    Diagnostic diag;
    const std::string_view text = "[1, 2]";
    ASSERT_TRUE(CS::parseExpression(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, diag));

    Value first = Value::null();
    Value second = Value::null();
    ASSERT_TRUE(CS::evalExpression(ast, ctx, &first, diag));
    ASSERT_TRUE(CS::evalExpression(ast, ctx, &second, diag));

    // docs/semantics.md §2.3: литерал создаёт новый агрегат при каждом
    // вычислении. Без этого теста правило держится на честном слове.
    EXPECT_FALSE(first.sameAggregate(second));
    EXPECT_EQ(ctx.arrayCount(first), 2u);
    EXPECT_EQ(ctx.arrayCount(second), 2u);
}
```

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalAggregates`
Expected: FAIL — узлы `Array` и `Object` попадают в `default`.

- [ ] **Шаг 3: Добавить ветки в `core/src/eval.cpp`**

В `switch` функции `eval`, перед `default`:

```cpp
        case NodeKind::Array: {
            const std::uint32_t count = ast.childCount(node);
            // Размер известен заранее — точное выделение, без переездов.
            const Value array = ctx.makeArray(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                Value element = Value::null();
                if (!eval(ast, ast.child(node, i), ctx, &element, diag)) {
                    return false;
                }
                ctx.arrayPush(array, element);
            }
            *out = array;
            return true;
        }

        case NodeKind::Object: {
            // Дети чередуются: ключ, значение. Ключ — строковый литерал по
            // грамматике, приведение §4 к нему не применяется.
            const std::uint32_t count = ast.childCount(node);
            const Value object = ctx.makeObject(count / 2);
            std::string scratch;
            for (std::uint32_t i = 0; i + 1 < count; i += 2) {
                Value value = Value::null();
                if (!eval(ast, ast.child(node, i + 1), ctx, &value, diag)) {
                    return false;
                }
                ctx.objectSet(object,
                              literalText(ast, ast.child(node, i), scratch),
                              value);
            }
            *out = object;
            return true;
        }
```

- [ ] **Шаг 4: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalAggregates`
Expected: 7 тестов PASS.

- [ ] **Шаг 5: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 303 теста PASS.

- [ ] **Шаг 6: Прогнать под санитайзерами и с `-Werror`**

```bash
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
cmake --build build-werror -j && ctest --test-dir build-werror --output-on-failure
```

Expected: 303 PASS в обеих, ни одного отчёта санитайзера, ни одного предупреждения.

- [ ] **Шаг 7: Коммит**

```bash
git add core/src/eval.cpp core/tests/eval_test.cpp
git commit -m "Evaluate aggregate literals over arbitrary expressions"
```

---

## Задача 7: Бенчмарки

**Files:**
- Modify: `benchmarks/eval_benchmark.cpp`, `benchmarks/baseline.json`

**Interfaces:**
- Consumes: `evalExpression`, `parseExpression`, `setVariable`, `formatNumber`.
- Produces: базу для сравнения при частях 2 и 3.

- [ ] **Шаг 1: Дописать бенчмарки**

Заменить содержимое `benchmarks/eval_benchmark.cpp` на приведённое ниже. `BM_Version` остаётся: он записан в `benchmarks/baseline.json`, а `tools/bench-compare.py` считает исчезновение строки из базы деградацией и возвращает единицу.

```cpp
// База производительности вычислителя. В отличие от разбора, который для
// одного выражения случается однажды, вычисление повторяется на каждой
// перерисовке — поэтому меряется именно оно, с уже разобранным деревом.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string_view>

#include "chupascript/chupascript.h"
#include "ast.hpp"
#include "context.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "eval.hpp"
#include "parser.hpp"
#include "text.hpp"

namespace {

using CS::Ast;
using CS::Context;
using CS::Diagnostic;
using CS::Value;

/// Наполняет контекст данными, на которых меряются пути.
bool fill(Context &ctx) {
    Diagnostic diag;
    return CS::setVariable(ctx, "user",
                           "{'name': 'Вася', 'profile': {'city': {'code': "
                           "{'zip': 101000}}}}",
                           diag) &&
           CS::setVariable(ctx, "items", "[10, 20, 30]", diag) &&
           CS::setVariable(ctx, "map", "{'0': 'zero', '1': 'one'}", diag);
}

/// Общая часть: наполнить контекст, разобрать выражение, мерить вычисление.
void runEval(benchmark::State &state, std::string_view source) {
    Context ctx;
    if (!fill(ctx)) {
        state.SkipWithError("setVariable failed");
        return;
    }

    Ast ast;
    Diagnostic diag;
    // Срез строкового литерала: данные статические, дерево хранит их срезами.
    if (!CS::parseExpression(source.data(),
                             static_cast<std::uint32_t>(source.size()), ast,
                             diag)) {
        state.SkipWithError("parseExpression failed");
        return;
    }

    for (auto _ : state) {
        Value out = Value::null();
        bool ok = CS::evalExpression(ast, ctx, &out, diag);
        if (!ok) {
            state.SkipWithError("evalExpression failed");
            return;
        }
        benchmark::DoNotOptimize(out);
    }
}

/// Самое частое выражение в props — один сегмент от корня.
void BM_Eval_ShortPath(benchmark::State &state) { runEval(state, "user.name"); }
BENCHMARK(BM_Eval_ShortPath);

/// Пять сегментов: цена спуска по дереву объектов.
void BM_Eval_DeepPath(benchmark::State &state) {
    runEval(state, "user.profile.city.code.zip");
}
BENCHMARK(BM_Eval_DeepPath);

/// Числовой индекс массива — без приведения.
void BM_Eval_ArrayIndex(benchmark::State &state) { runEval(state, "items[1]"); }
BENCHMARK(BM_Eval_ArrayIndex);

/// Числовой ключ объекта — с приведением к строке в горячем месте.
void BM_Eval_CoercedKey(benchmark::State &state) { runEval(state, "map[1]"); }
BENCHMARK(BM_Eval_CoercedKey);

/// Построение агрегата: десять элементов, точное выделение.
void BM_Eval_ArrayLiteral(benchmark::State &state) {
    runEval(state, "[1, 2, 3, 4, 5, 6, 7, 8, 9, 10]");
}
BENCHMARK(BM_Eval_ArrayLiteral);

/// Представление числа отдельно от всего прочего: у него больше всего краёв.
void BM_Eval_FormatNumber(benchmark::State &state) {
    for (auto _ : state) {
        char buffer[CS::kNumberBufferSize];
        std::string_view text = CS::formatNumber(0.1 + 0.2, buffer, sizeof buffer);
        benchmark::DoNotOptimize(text);
    }
}
BENCHMARK(BM_Eval_FormatNumber);

}  // namespace

static void BM_Version(benchmark::State &state) {
    for (auto _ : state) {
        benchmark::DoNotOptimize(chupascript_version());
    }
}
BENCHMARK(BM_Version);
```

- [ ] **Шаг 2: Собрать в Release и прогнать**

```bash
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks --benchmark_filter=Eval
```

Expected: шесть строк `BM_Eval_*`, ни одной с `SkipWithError`.

Посмотреть глазами на два соотношения: `BM_Eval_DeepPath` обязан быть дороже `BM_Eval_ShortPath` — это цена спуска; `BM_Eval_CoercedKey` обязан быть дороже `BM_Eval_ArrayIndex` — это цена приведения. Если хоть одно не выполняется, не записывать базу молча, а сообщить в отчёте: значит бенчмарк меряет не то, что думает.

- [ ] **Шаг 3: Проверить, что прежние семейства не деградировали**

```bash
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_out=/tmp/eval-current.json --benchmark_out_format=json
python3 tools/bench-compare.py benchmarks/baseline.json /tmp/eval-current.json
```

Этот этап трогает `core/src/data.cpp` — из него уезжают две функции, — поэтому `BM_Data_*` под подозрением, а `BM_Lex_*`, `BM_Parse_*` и `BM_Store_*` меняться не должны.

Порог различимости на этой машине около восьми процентов (`docs/backlog.md` B24), так что сдвиг меньше этого — шум. Если увидишь деградацию выше порога, разберись, прежде чем записывать базу, и напиши в отчёте, что нашёл.

Машина при замере обязана быть незанятой: параллельная сборка искажает числа на десятки процентов.

- [ ] **Шаг 4: Записать базу**

```bash
cp /tmp/eval-current.json benchmarks/baseline.json
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
for prefix in ('BM_Lex_', 'BM_Parse_', 'BM_Store_', 'BM_Data_', 'BM_Eval_'):
    assert any(n.startswith(prefix) for n in names), prefix
assert any(n.startswith('BM_Version') for n in names), 'BM_Version'
print('база содержит все семейства, поле машины:', d['context']['chupascript_machine'])
"
```

Expected: проверка печатает подтверждение и не падает.

- [ ] **Шаг 5: Коммит**

```bash
git add benchmarks/eval_benchmark.cpp benchmarks/baseline.json
git commit -m "Record the evaluator performance baseline"
```

---

## Задача 8: Документы

**Files:**
- Modify: `docs/semantics.md` (§4.3), `docs/backlog.md` (новый пункт)

**Interfaces:**
- Consumes: решения задач 1–7.
- Produces: согласованные документы.

- [ ] **Шаг 1: Поправить §4.3 `docs/semantics.md`**

Заменить предложение «Кратчайшее десятичное представление, которое читается обратно в то же самое значение double. Соответствует `std::to_chars` без указания формата.» на:

```markdown
Кратчайшее десятичное представление, которое читается обратно в то же самое
значение double. Правила применяются в порядке перечисления:

1. `NaN` даёт `'nan'`, бесконечности — `'inf'` и `'-inf'`.
2. Ноль даёт `'0'` либо `'-0'` по знаку.
3. При `1e-7 ≤ |x| < 1e21` запись фиксированная.
4. Вне этого диапазона — научная, с двузначным показателем: `1e-8` даёт
   `'1e-08'`.

Порог отделяет числа, которые человек читает как числа, от тех, где научная
запись короче и понятнее. Он же объясняет, почему `1000000` остаётся
`'1000000'`, а `1e21` становится `'1e+21'`.
```

Таблица примеров ниже остаётся без изменений: правила выведены так, чтобы ей удовлетворять.

- [ ] **Шаг 2: Добавить пункт в `docs/backlog.md`**

В раздел «Дерево разбора», после B10:

```markdown
### B25. Обход дерева циклом вместо рекурсии

**Где:** `core/src/eval.cpp`, `core/src/ast.hpp`
**Статус:** отложено; наблюдение записано, чтобы не выводить его заново

Узлы дерева лежат в порядке пост-обхода: парсер строит узел только после его
детей, поэтому индекс ребёнка всегда меньше индекса родителя, а поддерево
занимает непрерывный отрезок индексов, заканчивающийся самим узлом. Прямой цикл
`for (NodeId n = 1; n <= root; ++n)` — корректный обход, и он снял бы рекурсию
вместе с наследованным пределом глубины.

Мешают две вещи. Короткое замыкание требует **не** вычислять часть дерева
(`docs/semantics.md` §5.5, §5.6, §5.7), а цикл вычисляет всё подряд; пропуск
отрезка требует, чтобы узел знал, где начинается его поддерево, — сейчас он
этого не хранит, и понадобится ещё один `uint32` (см. B6). Кроме того, циклу
нужен буфер промежуточных значений на всё дерево, шестнадцать байт на узел,
тогда как рекурсии хватает глубины.

**Признак, что пора:** `BM_Eval_*` показывают, что вычисление упирается в
накладные расходы вызовов, а не в чтение данных.
```

- [ ] **Шаг 3: Проверить ссылки и счёт**

Run: `grep -c "^### B" docs/backlog.md`
Expected: 25.

Run: `grep -n "TODO(B" core/src/*.hpp core/src/*.cpp`
Expected: каждый упомянутый номер имеет заголовок `### B<N>.` в backlog.

- [ ] **Шаг 4: Прогнать весь набор**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 303 теста PASS — правки документов сборку не меняют, но убедиться стоит.

- [ ] **Шаг 5: Коммит**

```bash
git add docs/semantics.md docs/backlog.md
git commit -m "Record the number formatting rule and the loop traversal option"
```

---

## Итог

| | |
|---|---|
| Задач | 8 |
| Новых файлов | 5 (`text.hpp`, `text.cpp`, `eval.hpp`, `eval.cpp`, плюс два теста — `text_test.cpp`, `eval_test.cpp`) |
| Изменённых | `data.cpp`, `eval_benchmark.cpp`, два CMakeLists, `semantics.md`, `backlog.md` |
| Тестов добавлено | 47 |
| Тестов всего | 303 |
| Бенчмарков добавлено | 6 |
| Строк изменено в парсере, лексере и дереве | 0 |

Часть 1 закончена, когда: `ctest` даёт 303 из 303 в обычной сборке, под ASan+UBSan и с `-Werror`; `benchmarks/baseline.json` содержит строки `BM_Eval_*`, а прежние семейства не деградировали сверх порога различимости; `docs/semantics.md` §4.3 описывает то, что реализовано.

Следующая часть — операторы: вся глава 5 семантики, от унарных до тернарного, с коротким замыканием у `&&`, `||`, `??` и выбором ветви у `? :`.
