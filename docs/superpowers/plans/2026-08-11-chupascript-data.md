# ChupaScript: данные контекста — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Дать хосту положить в контекст именованные значения: `setVariable(ctx, "state", "{\"count\": 0}")` разбирает текст литерала и кладёт результат под именем.

**Architecture:** Отдельного разборщика данных нет — текст разбирается существующим `parseExpression`, и в парсере не меняется ни строки. Проход «дерево → `Value`» и есть проверка на литеральность: на любом виде узла, которого в данных быть не должно, он останавливается ошибкой. Таблица корней — это отображение имя → значение, то есть объект, поэтому новой структуры под неё не заводится: контекст держит один внутренний объект и работает с ним теми же методами.

**Tech Stack:** C++17, gtest, Google Benchmark, CMake. У самой библиотеки зависимостей нет.

**Спека:** `docs/superpowers/specs/2026-08-11-chupascript-data-design.md` — нормативна для этого плана.
**Грамматика:** `docs/grammar.md` §4.4 (идентификаторы), §4.5 (зарезервированные слова), §5.3 (литералы), §A.
**Семантика:** `docs/semantics.md` §2.1 (типы), §2.3 (ссылочность), §6.2 (ключи).

## Global Constraints

- **C++17.** Стандарт задан в корневом `CMakeLists.txt`, повышать нельзя.
- **У библиотеки нет зависимостей.** gtest и Google Benchmark подтягиваются только для тестов и бенчмарков.
- **Комментарии и документация — по-русски.** Сообщения диагностики — по-английски, как в лексере и парсере.
- **`core/src/parser.hpp` и `core/src/parser.cpp` не меняются ни одной строкой.** Ограничение «только литералы» не выражается грамматикой и не проверяется при разборе.
- **Формат значения — литерал ChupaScript, а не JSON.** Обе формы кавычек равноправны; экспоненты нет; юникодных escape нет — внешний уровень снимает хост.
- **Имя корня обязано быть идентификатором** по `grammar.md` §4.4 и не совпадать с зарезервированным словом (§4.5).
- **Унарный минус над числом принимается** и сворачивается в отрицательное число; `!` и минус над не-числом — нет.
- **Дубликаты ключей: последний выигрывает.** Следствие того, что `objectSet` заменяет значение.
- **Смещение в диагностике считается от начала текста значения,** а не от начала макета.
- **Неудача не оставляет корня:** при провале разбора или материализации имя в контексте не появляется.
- **Сборка тестов:** `cmake -B build && cmake --build build -j`, прогон `ctest --test-dir build --output-on-failure`.
- **Сборка бенчмарков:** только Release — `cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON`.
- **Коммитить явными путями.** `git add -A` не использовать.

---

## Структура файлов

| Файл | Ответственность |
|---|---|
| `core/src/context.hpp` / `.cpp` | добавляются корни: пять методов над внутренним объектом |
| `core/src/data.hpp` | объявление `setVariable` — вся публичная поверхность слоя |
| `core/src/data.cpp` | проверка имени, материализация, раскодирование экранирования |
| `core/tests/data_test.cpp` | разбор значений, отказы, смещения |
| `core/tests/context_test.cpp` | дописывается группа про корни |
| `benchmarks/data_benchmark.cpp` | база производительности слоя |

Существующие интерфейсы, которыми пользуется план:

```cpp
// core/src/parser.hpp — не меняется
bool parseExpression(const char *source, std::uint32_t length, Ast &ast, Diagnostic &diag);

// core/src/ast.hpp — аксессоры
NodeKind kind(NodeId) const; TokenKind op(NodeId) const; std::uint32_t offset(NodeId) const;
std::uint32_t childCount(NodeId) const; NodeId child(NodeId, std::uint32_t) const;
double numberValue(NodeId) const; bool boolValue(NodeId) const;
std::string_view text(NodeId) const; bool hasEscape(NodeId) const; NodeId root() const;

// core/src/lexer.hpp
TokenKind keywordKind(const char *text, std::uint32_t length) noexcept;
class Lexer { Lexer(const char *source, std::uint32_t length); bool next(Token &, Diagnostic &); };

// core/src/token.hpp
struct Token { TokenKind kind; bool hasEscape; std::uint32_t offset, length; double number; };

// core/src/diagnostic.hpp
enum class ErrorCode : std::uint8_t { None, Syntax, Name, Type, Range, Data, Usage, Memory };
struct Diagnostic { ErrorCode code; std::uint32_t offset; const char *message; };
```

---

## Задача 1: Корни в контексте

**Files:**
- Modify: `core/src/context.hpp`, `core/src/context.cpp`
- Modify: `core/tests/context_test.cpp`

**Interfaces:**
- Consumes: `makeObject`, `objectGet`, `objectHas`, `objectSet`, `objectCount`, `objectKeyAt` из уже готового `Context`.
- Produces: `Value root(std::string_view) const noexcept`, `bool hasRoot(std::string_view) const noexcept`, `void setRoot(std::string_view, Value)`, `std::uint32_t rootCount() const noexcept`, `std::string_view rootNameAt(std::uint32_t) const noexcept`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/context_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(ContextRoots, FreshContextHasNoRoots) {
    Context ctx;
    EXPECT_EQ(ctx.rootCount(), 0u);
}

TEST(ContextRoots, MissingRootReadsAsNull) {
    Context ctx;
    EXPECT_EQ(ctx.root("state").kind(), Value::Kind::Null);
    EXPECT_FALSE(ctx.hasRoot("state"));
}

TEST(ContextRoots, StoredRootIsFound) {
    Context ctx;
    ctx.setRoot("count", Value::number(3.0));
    EXPECT_TRUE(ctx.hasRoot("count"));
    EXPECT_EQ(ctx.root("count").numberValue(), 3.0);
    EXPECT_EQ(ctx.rootCount(), 1u);
}

TEST(ContextRoots, NullRootIsDistinctFromAbsence) {
    Context ctx;
    ctx.setRoot("maybe", Value::null());
    // Тот же довод, что для ключей объекта (docs/semantics.md §6.2).
    EXPECT_EQ(ctx.root("maybe").kind(), Value::Kind::Null);
    EXPECT_TRUE(ctx.hasRoot("maybe"));
}

TEST(ContextRoots, RepeatedSetReplacesValueWithoutAddingName) {
    Context ctx;
    ctx.setRoot("state", Value::number(1.0));
    ctx.setRoot("state", Value::number(2.0));
    EXPECT_EQ(ctx.rootCount(), 1u);
    EXPECT_EQ(ctx.root("state").numberValue(), 2.0);
}

TEST(ContextRoots, RootHoldsAggregate) {
    Context ctx;
    const Value items = ctx.makeArray();
    ctx.arrayPush(items, Value::number(1.0));
    ctx.setRoot("items", items);

    EXPECT_TRUE(ctx.root("items").sameAggregate(items));
    EXPECT_EQ(ctx.arrayCount(ctx.root("items")), 1u);
}

TEST(ContextRoots, MutationThroughRootIsSeenThroughTheOriginal) {
    Context ctx;
    const Value items = ctx.makeArray();
    ctx.setRoot("items", items);
    for (int i = 0; i < 30; ++i) {
        ctx.arrayPush(ctx.root("items"), Value::number(static_cast<double>(i)));
    }
    // docs/semantics.md §2.3: ссылочность наблюдаема и через корень.
    EXPECT_EQ(ctx.arrayCount(items), 30u);
}

TEST(ContextRoots, EnumerationYieldsEveryName) {
    Context ctx;
    ctx.setRoot("user", Value::null());
    ctx.setRoot("state", Value::null());

    ASSERT_EQ(ctx.rootCount(), 2u);
    std::string seen;
    for (std::uint32_t i = 0; i < ctx.rootCount(); ++i) {
        seen += ctx.rootNameAt(i);
        seen += ' ';
    }
    // Хранение отсортировано, как у любого объекта.
    EXPECT_EQ(seen, "state user ");
}
```

- [ ] **Шаг 2: Поправить тест, который эта задача делает неверным**

Новый контекст теперь держит объект корней, поэтому `bytesUsed()` у него уже не
ноль. Существующий `ContextMetrics.EmptyContextUsesNothing` в
`core/tests/context_test.cpp` это утверждает и обязан быть переписан — не
ослаблен, а переформулирован под то, что стало правдой:

```cpp
TEST(ContextMetrics, FreshContextHoldsOnlyTheRootTable) {
    Context ctx;
    // Единственное, что есть у нового контекста, — пустой объект корней:
    // один заголовок и ни одной пары.
    EXPECT_EQ(ctx.rootCount(), 0u);
    EXPECT_GT(ctx.bytesUsed(), 0u);
    EXPECT_LT(ctx.bytesUsed(), 64u);
}
```

Остальные тесты метрик сравнивают прирост, а не абсолютное значение, и правки не
требуют.

- [ ] **Шаг 3: Убедиться, что не собирается**

Run: `cmake --build build -j`
Expected: ошибка — у `Context` нет `root`, `hasRoot`, `setRoot`, `rootCount`, `rootNameAt`.

- [ ] **Шаг 4: Объявить в `core/src/context.hpp`**

Завести секцию перед секцией «метрики»:

```cpp
    // ─── корни ───
    //
    // Таблица корней — отображение имя → значение, то есть объект. Отдельной
    // структуры под неё нет: контекст держит один внутренний объект и работает
    // с ним теми же методами, что и с любым другим.

    /// Значение корня либо null, если имени нет.
    Value root(std::string_view name) const noexcept;

    /// Есть ли такое имя. Отличает корень со значением null от отсутствующего.
    bool hasRoot(std::string_view name) const noexcept;

    /// Заводит корень либо заменяет значение существующего.
    void setRoot(std::string_view name, Value v);

    std::uint32_t rootCount() const noexcept;

    /// Имя корня по порядковому номеру либо пустой срез за границей.
    std::string_view rootNameAt(std::uint32_t i) const noexcept;
```

В приватную секцию, к полям:

```cpp
    /// Объект с корнями. Создаётся в конструкторе, поэтому Value::null()
    /// здесь — только заглушка до его вызова.
    Value roots_ = Value::null();
```

- [ ] **Шаг 5: Написать тела в `core/src/context.cpp`**

Заменить определение конструктора:

```cpp
Context::Context() { roots_ = makeObject(); }
```

Дописать после `objectSet`:

```cpp
Value Context::root(std::string_view name) const noexcept {
    return objectGet(roots_, name);
}

bool Context::hasRoot(std::string_view name) const noexcept {
    return objectHas(roots_, name);
}

void Context::setRoot(std::string_view name, Value v) { objectSet(roots_, name, v); }

std::uint32_t Context::rootCount() const noexcept { return objectCount(roots_); }

std::string_view Context::rootNameAt(std::uint32_t i) const noexcept {
    return objectKeyAt(roots_, i);
}
```

- [ ] **Шаг 6: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R ContextRoots`
Expected: 8 тестов PASS.

- [ ] **Шаг 7: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 211 тестов PASS (203 было + 8).

- [ ] **Шаг 8: Коммит**

```bash
git add core/src/context.hpp core/src/context.cpp core/tests/context_test.cpp
git commit -m "Give the context a table of named roots"
```

---

## Задача 2: `setVariable` и проверка имени

**Files:**
- Create: `core/src/data.hpp`, `core/src/data.cpp`, `core/tests/data_test.cpp`
- Modify: `core/CMakeLists.txt:1-7`, `core/tests/CMakeLists.txt:1-8`

**Interfaces:**
- Consumes: `Context::setRoot` из задачи 1; `parseExpression` из `parser.hpp`; `Lexer` и `Token` из `lexer.hpp` и `token.hpp`.
- Produces: `bool CS::setVariable(Context &ctx, std::string_view name, std::string_view text, Diagnostic &diag)`. В `data.cpp` появляется внутренняя `bool materialize(const Ast &, NodeId, Context &, Value *, Diagnostic &)`, которую расширяют задачи 3–5.

В этой задаче материализуются только `Number`, `Boolean` и `Null`. Строки, минус и агрегаты приходят дальше; до тех пор они отвергаются как всё прочее.

- [ ] **Шаг 1: Написать тесты**

Создать `core/tests/data_test.cpp`:

```cpp
#include "data.hpp"

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
    value_test.cpp
)
```

- [ ] **Шаг 3: Убедиться, что не собирается**

Run: `cmake -B build && cmake --build build -j`
Expected: ошибка — `data.hpp` не существует.

- [ ] **Шаг 4: Написать `core/src/data.hpp`**

```cpp
#pragma once
#include <string_view>

#include "context.hpp"
#include "diagnostic.hpp"

namespace CS {

/// Кладёт в контекст значение под именем name.
///
/// text — литерал ChupaScript целиком: число, строка, true, false, null,
/// массив или объект, произвольно вложенные
/// (docs/superpowers/specs/2026-08-11-chupascript-data-design.md §3).
/// Выражение литералом не является и отвергается: данные не вычисляются.
///
/// name обязано быть идентификатором (docs/grammar.md §4.4) и не совпадать с
/// зарезервированным словом (§4.5) — иначе программа не сможет к нему
/// обратиться.
///
/// При отказе возвращает false, заполняет diag и не заводит корня. Смещение в
/// diag считается от начала text.
bool setVariable(Context &ctx, std::string_view name, std::string_view text,
                 Diagnostic &diag);

}  // namespace CS
```

- [ ] **Шаг 5: Написать `core/src/data.cpp`**

```cpp
#include "data.hpp"

#include <cassert>
#include <cstdint>

#include "ast.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "token.hpp"

namespace CS {
namespace {

/// Записывает отказ «в данных выражение недопустимо» с местом узла.
bool rejectNode(const Ast &ast, NodeId node, Diagnostic &diag) {
    diag = Diagnostic{ErrorCode::Data, ast.offset(node),
                      "expression is not allowed in data"};
    return false;
}

/// Идентификатор ли это по docs/grammar.md §4.4 и не ключевое ли слово (§4.5).
///
/// Проверка выполняется лексером, а не своей таблицей: так набор ключевых слов
/// и ограничение на ASCII заведомо совпадают с языком и не разъедутся с ним.
bool isRootName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 0xffffffffu) { return false; }

    Lexer lexer(name.data(), static_cast<std::uint32_t>(name.size()));
    Diagnostic ignored;

    Token first;
    if (!lexer.next(first, ignored)) { return false; }
    if (first.kind != TokenKind::Identifier) { return false; }
    // Токен обязан покрывать имя целиком: иначе " state" и "state " прошли бы,
    // а обратиться к такому корню нельзя.
    if (first.offset != 0 || first.length != name.size()) { return false; }

    Token tail;
    if (!lexer.next(tail, ignored)) { return false; }
    return tail.kind == TokenKind::End;
}

bool materialize(const Ast &ast, NodeId node, Context &ctx, Value *out,
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

        default:
            return rejectNode(ast, node, diag);
    }
}

}  // namespace

bool setVariable(Context &ctx, std::string_view name, std::string_view text,
                 Diagnostic &diag) {
    if (!isRootName(name)) {
        diag = Diagnostic{ErrorCode::Name, 0, "root name must be an identifier"};
        return false;
    }

    Ast ast;
    if (!parseExpression(text.data(), static_cast<std::uint32_t>(text.size()), ast,
                         diag)) {
        return false;
    }

    Value value = Value::null();
    if (!materialize(ast, ast.root(), ctx, &value, diag)) { return false; }

    // Корень заводится только после успеха: отказ не оставляет имени.
    ctx.setRoot(name, value);
    return true;
}

}  // namespace CS
```

- [ ] **Шаг 6: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "DataScalars|DataNames|DataFailure"`
Expected: 7 тестов PASS.

- [ ] **Шаг 7: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 218 тестов PASS.

- [ ] **Шаг 8: Коммит**

```bash
git add core/src/data.hpp core/src/data.cpp core/tests/data_test.cpp \
        core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "Set context variables from literal text"
```

---

## Задача 3: Строки без экранирования и унарный минус

**Files:**
- Modify: `core/src/data.cpp`
- Modify: `core/tests/data_test.cpp`

**Interfaces:**
- Consumes: `materialize`, `rejectNode` из задачи 2; `Context::makeString`.
- Produces: расширенный `materialize`, принимающий `NodeKind::String` без экранирования и `NodeKind::Unary` с операцией `TokenKind::Minus` над `NodeKind::Number`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/data_test.cpp` перед закрывающим `}  // namespace`:

```cpp
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
```

Добавить `#include <cmath>` в начало файла — он нужен `std::signbit`.

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "DataStrings|DataMinus"`
Expected: FAIL — `materialize` отвергает `String` и `Unary` как всё прочее.

- [ ] **Шаг 3: Расширить `materialize` в `core/src/data.cpp`**

Добавить в `switch` перед `default`:

```cpp
        case NodeKind::String:
            // Экранирование раскодируется в задаче про escape; пока литерал
            // без него кладётся срезом исходника.
            *out = ctx.makeString(ast.text(node));
            return true;

        case NodeKind::Unary: {
            // Минус над числом — это запись отрицательного значения, а не
            // вычисление: знака в NumericLiteral нет (docs/grammar.md §4.6),
            // и без этой ветки первое же отрицательное поле с бэкенда упёрлось
            // бы в «выражения в данных запрещены».
            if (ast.op(node) != TokenKind::Minus) { return rejectNode(ast, node, diag); }
            const NodeId operand = ast.child(node, 0);
            if (ast.kind(operand) != NodeKind::Number) {
                return rejectNode(ast, node, diag);
            }
            *out = Value::number(-ast.numberValue(operand));
            return true;
        }
```

- [ ] **Шаг 4: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "DataStrings|DataMinus"`
Expected: 6 тестов PASS.

- [ ] **Шаг 5: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 224 теста PASS.

- [ ] **Шаг 6: Коммит**

```bash
git add core/src/data.cpp core/tests/data_test.cpp
git commit -m "Accept string literals and negative numbers in data"
```

---

## Задача 4: Раскодирование экранирования

**Files:**
- Modify: `core/src/data.cpp`
- Modify: `core/tests/data_test.cpp`

**Interfaces:**
- Consumes: `Ast::hasEscape`, `Ast::text`.
- Produces: внутренние `void decodeEscapes(std::string_view raw, std::string &out)` и `std::string_view literalText(const Ast &ast, NodeId node, std::string &scratch)`. Вторую использует задача 5 для ключей объекта.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/data_test.cpp` перед закрывающим `}  // namespace`:

```cpp
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
    // docs/grammar.md §8 и §4.7: юникодных escape в языке нет — внешний
    // уровень снимает хост, и до нас доезжают готовые байты.
    EXPECT_FALSE(CS::setVariable(ctx, "s", "'\\u0041'", diag));
    EXPECT_EQ(diag.code, ErrorCode::Syntax);
}
```

Добавить `#include <string>` в начало файла, если его там ещё нет.

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R DataEscapes`
Expected: FAIL у первых пяти — экранирование сейчас кладётся в значение как есть, `'a\nb'` даёт четыре байта вместо трёх. Шестой уже проходит: его отвергает лексер.

- [ ] **Шаг 3: Написать раскодирование в `core/src/data.cpp`**

Добавить `#include <string>` к включениям и в анонимное пространство имён, перед `materialize`:

```cpp
/// Раскодирует экранирование строкового литерала.
///
/// Набор — из docs/grammar.md §A: \\ \' \" \n \t. Неизвестной
/// последовательности здесь быть не может: её отверг лексер.
void decodeEscapes(std::string_view raw, std::string &out) {
    out.clear();
    out.reserve(raw.size());
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] != '\\') {
            out.push_back(raw[i]);
            continue;
        }
        assert(i + 1 < raw.size() && "лексер не пропустил бы висячий слэш");
        ++i;
        switch (raw[i]) {
            case 'n': out.push_back('\n'); break;
            case 't': out.push_back('\t'); break;
            case '\\': out.push_back('\\'); break;
            case '\'': out.push_back('\''); break;
            case '"': out.push_back('"'); break;
            default: assert(false && "лексер отверг бы такую последовательность");
        }
    }
}

/// Содержимое строкового литерала: срез исходника, если экранирования нет,
/// иначе раскодированное в scratch.
///
/// Флаг hasEscape избавляет от временного буфера в подавляющем большинстве
/// случаев: экранирование в данных редкость.
std::string_view literalText(const Ast &ast, NodeId node, std::string &scratch) {
    assert(ast.kind(node) == NodeKind::String);
    if (!ast.hasEscape(node)) { return ast.text(node); }
    decodeEscapes(ast.text(node), scratch);
    return scratch;
}
```

- [ ] **Шаг 4: Использовать его в `materialize`**

Заменить ветку `case NodeKind::String:`:

```cpp
        case NodeKind::String: {
            std::string scratch;
            *out = ctx.makeString(literalText(ast, node, scratch));
            return true;
        }
```

- [ ] **Шаг 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R DataEscapes`
Expected: 6 тестов PASS.

- [ ] **Шаг 6: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 230 тестов PASS.

- [ ] **Шаг 7: Коммит**

```bash
git add core/src/data.cpp core/tests/data_test.cpp
git commit -m "Decode string escapes when materializing data"
```

---

## Задача 5: Агрегаты

**Files:**
- Modify: `core/src/data.cpp`
- Modify: `core/tests/data_test.cpp`

**Interfaces:**
- Consumes: `literalText` из задачи 4; `Context::makeArray`, `arrayPush`, `makeObject`, `objectSet`.
- Produces: расширенный `materialize`, принимающий `NodeKind::Array` и `NodeKind::Object`.

Дети узла `Object` чередуются: ключ, значение, ключ, значение — поэтому `childCount` у объекта из `n` пар равен `2n`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/data_test.cpp` перед закрывающим `}  // namespace`:

```cpp
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
```

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R DataAggregates`
Expected: FAIL — `materialize` отвергает `Array` и `Object`.

- [ ] **Шаг 3: Расширить `materialize` в `core/src/data.cpp`**

Добавить в `switch` перед `default`:

```cpp
        case NodeKind::Array: {
            const std::uint32_t count = ast.childCount(node);
            // Размер известен заранее, поэтому ёмкость выделяется точно и
            // построение не оставляет мусора.
            const Value array = ctx.makeArray(count);
            for (std::uint32_t i = 0; i < count; ++i) {
                Value element = Value::null();
                if (!materialize(ast, ast.child(node, i), ctx, &element, diag)) {
                    return false;
                }
                ctx.arrayPush(array, element);
            }
            *out = array;
            return true;
        }

        case NodeKind::Object: {
            // Дети чередуются: ключ, значение, ключ, значение.
            const std::uint32_t count = ast.childCount(node);
            const Value object = ctx.makeObject(count / 2);
            std::string scratch;
            for (std::uint32_t i = 0; i + 1 < count; i += 2) {
                Value value = Value::null();
                if (!materialize(ast, ast.child(node, i + 1), ctx, &value, diag)) {
                    return false;
                }
                // Повторный ключ заменяет значение: последний выигрывает.
                ctx.objectSet(object, literalText(ast, ast.child(node, i), scratch),
                              value);
            }
            *out = object;
            return true;
        }
```

Рекурсия здесь ограничена парсером: вложенность агрегатов упирается в 31 уровень (`docs/grammar.md` Приложение C.1), поэтому собственного предела глубины не нужно.

- [ ] **Шаг 4: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R DataAggregates`
Expected: 9 тестов PASS.

- [ ] **Шаг 5: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 239 тестов PASS.

- [ ] **Шаг 6: Прогнать под санитайзерами**

```bash
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

Expected: 239 PASS, ни одного отчёта. Ключ объекта передаётся в `objectSet` срезом, который при отсутствии экранирования смотрит в буфер исходника, а при наличии — в `scratch`; обе ветки стоит проверить под ASan.

- [ ] **Шаг 7: Коммит**

```bash
git add core/src/data.cpp core/tests/data_test.cpp
git commit -m "Materialize arrays and objects from literal text"
```

---

## Задача 6: Отказы и смещения

**Files:**
- Modify: `core/tests/data_test.cpp` (только тесты)

**Interfaces:**
- Consumes: весь `setVariable` из задач 2–5. Нового кода не пишется; если тест не проходит, правится `core/src/data.cpp`.
- Produces: ничего.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/data_test.cpp` перед закрывающим `}  // namespace`:

```cpp
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
    // начало текста и не на соседний элемент: иначе хост не покажет, где
    // именно чинить. В "[1, user.name, 3]" выражение занимает байты с 4 по 12,
    // а последний элемент стоит на 15 — верхняя граница отсекает его.
    const Diagnostic diag = reject(ctx, "[1, user.name, 3]");
    EXPECT_EQ(diag.code, ErrorCode::Data);
    EXPECT_GE(diag.offset, 4u);
    EXPECT_LT(diag.offset, 13u);
}

TEST(DataRejects, TrailingBytesAreRejected) {
    Context ctx;
    // Текст обязан быть значением целиком.
    EXPECT_EQ(reject(ctx, "1 2").code, ErrorCode::Syntax);
    EXPECT_EQ(reject(ctx, "[1] 2").code, ErrorCode::Syntax);
}

TEST(DataRejects, IndexingALiteralIsNotData) {
    Context ctx;
    // [1] [2] разбирается: это массив, проиндексированный двойкой. Индексация —
    // постфиксная операция над любым Primary, включая литерал агрегата, поэтому
    // синтаксической ошибки здесь нет. Это выражение, а не запись значения.
    EXPECT_EQ(reject(ctx, "[1] [2]").code, ErrorCode::Data);
}

TEST(DataRejects, ExponentIsNotANumber) {
    Context ctx;
    // docs/grammar.md §8 и §4.6: экспоненты в языке нет, 1e3 — два токена.
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
```

- [ ] **Шаг 2: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R DataRejects`
Expected: 11 тестов PASS. Если какой-то падает — правится `core/src/data.cpp`, тест не трогается: он выражает требование спеки §6. Если тест требует невозможного — остановись и спроси, а не подгоняй под него реализацию.

- [ ] **Шаг 3: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 250 тестов PASS.

- [ ] **Шаг 4: Прогнать под санитайзерами и с `-Werror`**

```bash
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
cmake --build build-werror -j && ctest --test-dir build-werror --output-on-failure
```

Expected: 250 PASS в обеих, ни одного отчёта санитайзера, ни одного предупреждения.

- [ ] **Шаг 5: Коммит**

```bash
git add core/tests/data_test.cpp
git commit -m "Cover every rejection path of the data layer"
```

---

## Задача 7: Бенчмарки

**Files:**
- Create: `benchmarks/data_benchmark.cpp`
- Modify: `benchmarks/CMakeLists.txt:3-8`
- Modify: `benchmarks/baseline.json`

**Interfaces:**
- Consumes: `setVariable`, `Context`.
- Produces: базу для сравнения при будущих правках слоя.

- [ ] **Шаг 1: Написать бенчмарки**

Создать `benchmarks/data_benchmark.cpp`:

```cpp
// База производительности слоя данных: разбор текста литерала и укладка
// значений в контекст. Измеряется путь, который выполняется один раз на
// переменную при сборке экрана.
#include <benchmark/benchmark.h>

#include <string>

#include "context.hpp"
#include "data.hpp"
#include "diagnostic.hpp"

namespace {

using CS::Context;
using CS::Diagnostic;
using CS::Value;

/// Плоский объект из десяти полей — типичная переменная экрана.
void BM_Data_FlatObject(benchmark::State &state) {
    const std::string text =
        "{'id': 1, 'name': 'Вася', 'age': 30, 'active': true, 'score': 4.5,"
        " 'city': 'Москва', 'tag': null, 'rank': -2, 'level': 7, 'code': 'A1'}";

    for (auto _ : state) {
        Context ctx;
        Diagnostic diag;
        bool ok = CS::setVariable(ctx, "user", text, diag);
        if (!ok) { state.SkipWithError("setVariable failed"); return; }
        benchmark::DoNotOptimize(ok);
    }
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(text.size()));
}
BENCHMARK(BM_Data_FlatObject);

/// Массив из ста чисел — типичный список.
void BM_Data_NumberArray(benchmark::State &state) {
    std::string text = "[";
    for (int i = 0; i < 100; ++i) {
        if (i > 0) { text += ", "; }
        text += std::to_string(i);
    }
    text += "]";

    for (auto _ : state) {
        Context ctx;
        Diagnostic diag;
        bool ok = CS::setVariable(ctx, "items", text, diag);
        if (!ok) { state.SkipWithError("setVariable failed"); return; }
        benchmark::DoNotOptimize(ok);
    }
    state.SetBytesProcessed(state.iterations() *
                            static_cast<std::int64_t>(text.size()));
}
BENCHMARK(BM_Data_NumberArray);

/// Строка без экранирования против строки с ним: цена временного буфера.
void BM_Data_PlainString(benchmark::State &state) {
    const std::string text = "'" + std::string(200, 'x') + "'";
    for (auto _ : state) {
        Context ctx;
        Diagnostic diag;
        bool ok = CS::setVariable(ctx, "s", text, diag);
        if (!ok) { state.SkipWithError("setVariable failed"); return; }
        benchmark::DoNotOptimize(ok);
    }
}
BENCHMARK(BM_Data_PlainString);

void BM_Data_EscapedString(benchmark::State &state) {
    std::string body;
    for (int i = 0; i < 100; ++i) { body += "x\\n"; }
    const std::string text = "'" + body + "'";

    for (auto _ : state) {
        Context ctx;
        Diagnostic diag;
        bool ok = CS::setVariable(ctx, "s", text, diag);
        if (!ok) { state.SkipWithError("setVariable failed"); return; }
        benchmark::DoNotOptimize(ok);
    }
}
BENCHMARK(BM_Data_EscapedString);

/// Поиск корня при разном числе имён.
void BM_Data_RootLookup(benchmark::State &state) {
    const int names = static_cast<int>(state.range(0));
    Context ctx;
    Diagnostic diag;
    for (int i = 0; i < names; ++i) {
        const std::string name = "var" + std::to_string(i);
        if (!CS::setVariable(ctx, name, "1", diag)) {
            state.SkipWithError("setVariable failed");
            return;
        }
    }
    const std::string last = "var" + std::to_string(names - 1);

    for (auto _ : state) {
        Value found = ctx.root(last);
        benchmark::DoNotOptimize(found);
    }
}
BENCHMARK(BM_Data_RootLookup)->Arg(3)->Arg(10)->Arg(30);

}  // namespace
```

- [ ] **Шаг 2: Зарегистрировать в сборке**

`benchmarks/CMakeLists.txt`, список исходников:

```cmake
add_executable(chupascript_benchmarks
    data_benchmark.cpp
    eval_benchmark.cpp
    lexer_benchmark.cpp
    parser_benchmark.cpp
    store_benchmark.cpp
)
```

- [ ] **Шаг 3: Собрать в Release и прогнать**

```bash
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks --benchmark_filter=Data
```

Expected: семь строк — четыре одиночных бенчмарка и три от `BM_Data_RootLookup`, — ни одной с `SkipWithError`.

Посмотреть глазами на два соотношения: `BM_Data_EscapedString` обязан быть дороже `BM_Data_PlainString` — это и есть цена временного буфера; `BM_Data_RootLookup` от 3 к 30 именам обязан расти полого, а не линейно. Если хоть одно не выполняется, не записывать базу молча, а сообщить в отчёте.

- [ ] **Шаг 4: Проверить, что путь парсинга не задет**

```bash
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_out=/tmp/data-current.json --benchmark_out_format=json
python3 tools/bench-compare.py benchmarks/baseline.json /tmp/data-current.json
```

Expected: код возврата 0. Слой данных не трогает ни парсер, ни хранилище, поэтому строки `BM_Parse_*`, `BM_Lex_*` и `BM_Store_*` обязаны остаться на прежнем уровне. Деградация в них означала бы, что что-то поехало в общем коде, и её надо разобрать до записи базы.

Машина при замере обязана быть незанятой: параллельная сборка искажает числа на десятки процентов.

- [ ] **Шаг 5: Записать базу**

```bash
cp /tmp/data-current.json benchmarks/baseline.json
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
for prefix in ('BM_Lex_', 'BM_Parse_', 'BM_Store_', 'BM_Data_'):
    assert any(n.startswith(prefix) for n in names), prefix
print('база содержит все четыре семейства, поле машины:', d['context']['chupascript_machine'])
"
```

Expected: проверка печатает подтверждение и не падает.

- [ ] **Шаг 6: Коммит**

```bash
git add benchmarks/data_benchmark.cpp benchmarks/CMakeLists.txt benchmarks/baseline.json
git commit -m "Record the data layer performance baseline"
```

---

## Задача 8: Документы

**Files:**
- Modify: `docs/backlog.md` (пункты B3, B9, новый пункт)
- Modify: `core/src/diagnostic.hpp:16`

**Interfaces:**
- Consumes: решения задач 1–7.
- Produces: согласованные документы.

- [ ] **Шаг 1: Закрыть B3 в `docs/backlog.md`**

Заменить тело пункта «B3. Форма `set_data`: откуда берутся корневые имена» целиком на:

```markdown
**Где:** `core/src/data.hpp`
**Статус:** закрыт

Имя корня даёт хост, по вызову на корень:

```cpp
setVariable(ctx, "state", "{'count': 0}", diag);
```

Вариант, при котором корни берутся из ключей объекта верхнего уровня, оставлял
недостижимыми ключи, не являющиеся идентификаторами: к полю `content-type` или
`@meta` не подобраться никаким написанием. Пришлось бы отдельно решать, отвергать
такие данные или молча их пропускать; отвергать плохо, потому что бэкенд добавит
служебное поле и экран перестанет открываться из-за данных, которые никому не
мешают. При именовании со стороны хоста этой развилки нет.

Решение и его следствия описаны в
`docs/superpowers/specs/2026-08-11-chupascript-data-design.md` §2.
```

- [ ] **Шаг 2: Сузить B9 в `docs/backlog.md`**

В конец тела пункта «B9. Раскодирование экранирования в строковых литералах» дописать:

```markdown
Для данных вопрос закрыт: `core/src/data.cpp` раскодирует набор `\\ \' \" \n \t`
при материализации, а флаг `hasEscape` избавляет от временного буфера, когда
экранирования нет. Открытым остаётся то же для строковых литералов внутри
выражений — закрывается вместе с вычислителем, той же функцией.
```

- [ ] **Шаг 3: Добавить новый пункт в `docs/backlog.md`**

В раздел «Граница с хостом», после B5:

```markdown
### B23. Новый корень после первой компиляции

**Где:** слой компиляции, `core/src/data.cpp`
**Статус:** отложено до появления компилятора

Спека C API §4 выводит из порядка «данные поставлены → компиляция» то, что
обращение к несуществующему корню — ошибка компиляции: компилятор видит состав
имён, поэтому `usre.name` отвергается при разборе макета, а не при первом показе
экрана.

Повторный `setVariable` по существующему имени этому не мешает: состав имён не
меняется, значит проверки остаются верными. А вот введение **нового** имени
после компиляции делает их неверными и должно отвергаться.

Механизма для этого в слое данных нет: заморозка требует знать, что компиляция
состоялась, а компилятора на этом слое не существует. `Context::hasRoot` — всё,
что проверке нужно.

**Признак, что пора:** появление компиляции макета, то есть слоя, который зовёт
`parseExpression` от имени хоста.
```

- [ ] **Шаг 4: Поправить комментарий кода ошибки**

В `core/src/diagnostic.hpp` строка про `Data` говорит «некорректный JSON во входных данных». JSON рантайм не видит: хост разбирает его сам и передаёт имя и текст литерала. Заменить строку на:

```cpp
    Data,    ///< значение переменной не является литералом
```

- [ ] **Шаг 5: Проверить, что ссылки живые**

Run: `grep -c "^### B" docs/backlog.md`
Expected: 23.

Run: `grep -n "TODO(B" core/src/*.hpp core/src/*.cpp`
Expected: каждый упомянутый номер имеет заголовок `### B<N>.` в backlog.

- [ ] **Шаг 6: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 250 тестов PASS — правка комментария сборку не меняет, но убедиться стоит.

- [ ] **Шаг 7: Коммит**

```bash
git add docs/backlog.md core/src/diagnostic.hpp
git commit -m "Close B3 and B9 for data, record the freeze rule"
```

---

## Итог

| | |
|---|---|
| Задач | 8 |
| Новых файлов | 4 (`data.hpp`, `data.cpp`, `data_test.cpp`, `data_benchmark.cpp`) |
| Изменённых | `context.hpp`, `context.cpp`, `context_test.cpp`, `diagnostic.hpp`, два CMakeLists, backlog |
| Тестов добавлено | 47 |
| Тестов всего | 250 |
| Бенчмарков добавлено | 7 строк из 5 функций |
| Строк изменено в парсере | 0 |

Слой закончен, когда: `ctest` даёт 250 из 250 в обычной сборке, под ASan+UBSan и с `-Werror`; `benchmarks/baseline.json` содержит строки `BM_Data_*`, а прежние `BM_Lex_*` и `BM_Parse_*` не деградировали. Три строки `BM_Store_*` сдвинулись, и это принятая цена двух ранее отревьюенных решений: `021acf1` (перечитывание заголовка по индексу вместо удержания ссылки через рост пула, ради того чтобы замена пулов ареной осталась подменой приватных полей; около 0.28 нс на операцию) и `e960581` (конструктор `Context` строит объект корней, за что платит всякий бенчмарк, создающий контекст внутри измеряемого цикла). `docs/backlog.md` закрывает B3 и сужает B9.

Следующий этап — вычислитель: главы 3–6 `docs/semantics.md`, вход — дерево от парсера и данные из контекста, выход — `Value`.
