# ChupaScript: вычислитель, часть 3b — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Завершить вычислитель: тринадцать встроенных функций и статический проход, отвергающий негодную программу до исполнения.

**Architecture:** Таблица билтинов — общий хребет: из неё читают и диспетчер вызова, и все проверки. Статический проход обязателен, поэтому вычислитель дереву доверяет и арность не перепроверяет; проверенность держится отметкой на `Ast`. Проход — плоский цикл по узлам, а не рекурсия: пропускать ему нечего.

**Tech Stack:** C++17, gtest, Google Benchmark, CMake. У самой библиотеки зависимостей нет.

**Спека:** `docs/superpowers/specs/2026-08-12-chupascript-builtins-design.md` — нормативна для этого плана.
**Семантика:** `docs/semantics.md` §2.2 (Void), §4 (приведения), §6 (доступ), §8 (тринадцать функций), §9 (ошибки).
**Грамматика:** `docs/grammar.md` §6 (статические проверки).

## Global Constraints

- **C++17.** Стандарт задан в корневом `CMakeLists.txt`.
- **Комментарии — по-русски.** Сообщения диагностики — по-английски.
- **`core/src/parser.*`, `core/src/lexer.*` и `core/src/operator.*` не меняются ни одной строкой.**
- **Проход обязателен.** `check` при нуле ошибок ставит отметку на `Ast`; `evalExpression` и `runScript` требуют её утверждением. Вычислитель арность и возвращает-ли-значение **не перепроверяет** — правило записано один раз, в проходе.
- **Проход не останавливается на первой ошибке.** Возвращает число найденных, в буфер кладёт не больше `capacity`.
- **Проходу нужны имена, а не значения:** из `Context` читается только `hasRoot`.
- **Проход — плоский цикл** `for (NodeId n = 1; n <= root; ++n)`, не рекурсия: узлы лежат в пост-обходе, дети раньше родителей.
- **`Void` не становится значением.** Для `push` и `pop` `*out` не трогается.
- **Сборка тестов:** `cmake --build build -j`, прогон `ctest --test-dir build --output-on-failure`. Перед сборкой `touch` изменённого `.cpp`, иначе предупреждения спрячутся за отсутствием пересборки.
- **Сборка бенчмарков:** только Release — каталог `build-rel` уже сконфигурирован.
- **Коммитить явными путями.** `git add -A` не использовать.

---

## Структура файлов

| Файл | Ответственность |
|---|---|
| `core/src/builtin.hpp` / `.cpp` | новые: таблица, поиск по имени, реализации двенадцати, разбор шаблона `format` |
| `core/src/check.hpp` / `.cpp` | новые: статический проход |
| `core/src/compile.hpp` / `.cpp` | новые: фасад «разбор плюс проверки» |
| `core/src/ast.hpp` / `.cpp` | отметка «проверено» |
| `core/src/context.hpp` / `.cpp` | сборка строки по частям |
| `core/src/eval.hpp` / `.cpp` | ветка `Call`, цикл `format`, утверждения про отметку |
| `core/tests/*` | дописываются |

`builtin.*` относится к `check.*` и `eval.cpp` так же, как `operator.*` из части 2 относится к обходу: функции от значений, отделённые от управления. **`builtin.*` не знает про `Ast`** — поэтому цикл `format`, которому нужно вычислять аргументы по мере надобности, живёт в `eval.cpp`, а в `builtin.*` вынесен только разбор шаблона.

Существующие интерфейсы:

```cpp
// core/src/ast.hpp
NodeKind kind(NodeId) const noexcept;      // Call — дети: аргументы; текст — имя
std::uint32_t childCount(NodeId) const noexcept;
NodeId child(NodeId, std::uint32_t) const noexcept;
std::string_view text(NodeId) const noexcept;
bool hasEscape(NodeId) const noexcept;     // у String
std::uint32_t offset(NodeId) const noexcept;
NodeId root() const noexcept;
void reset(const char *source);

// core/src/context.hpp
Value makeArray(std::uint32_t capacity = 0);
Value makeString(std::string_view bytes);
std::string_view string(Value) const noexcept;
std::uint32_t arrayCount(Value) const noexcept;
Value arrayAt(Value, std::uint32_t) const noexcept;
void arrayPush(Value, Value);
bool arrayPop(Value, Value *out) noexcept;      // out допускает nullptr
std::uint32_t objectCount(Value) const noexcept;
bool objectHas(Value, std::string_view) const noexcept;
std::string_view objectKeyAt(Value, std::uint32_t) const noexcept;
bool hasRoot(std::string_view) const noexcept;

// core/src/parser.hpp — не меняется
bool parseExpression(const char *source, std::uint32_t length, Ast &, Diagnostic &);
bool parseProgram(const char *source, std::uint32_t length, Ast &, Diagnostic &);

// core/src/text.hpp
inline constexpr std::size_t kNumberBufferSize;
std::string_view formatNumber(double, char *buffer, std::size_t size);

// core/src/eval.cpp, внутренние — уже есть
bool eval(const Ast &, NodeId, Context &, Value *, Diagnostic &);
bool fail(const Ast &, NodeId, ErrorCode, const char *, Diagnostic &);
bool coerceToString(const Ast &, NodeId, Context &, Value, char *numberBuffer,
                    std::string_view *, Diagnostic &);
```

---

## Задача 1: Таблица билтинов

**Files:**
- Create: `core/src/builtin.hpp`, `core/src/builtin.cpp`, `core/tests/builtin_test.cpp`
- Modify: `core/CMakeLists.txt`

**Interfaces:**
- Consumes: ничего.
- Produces: `enum class CS::Builtin`, `CS::kVariadic`, `struct CS::BuiltinInfo`, `bool CS::findBuiltin(std::string_view, Builtin *)`, `const BuiltinInfo &CS::builtinInfo(Builtin)`, `std::uint32_t CS::countPlaceholders(std::string_view)`.

- [ ] **Шаг 1: Написать тесты**

Создать `core/tests/builtin_test.cpp`:

```cpp
#include "builtin.hpp"

#include <gtest/gtest.h>

namespace {

using CS::Builtin;

TEST(BuiltinTable, FindsEveryName) {
    // Тринадцать функций docs/semantics.md §8.
    const std::pair<const char *, Builtin> all[] = {
        {"count", Builtin::Count},   {"keys", Builtin::Keys},
        {"has", Builtin::Has},       {"last", Builtin::Last},
        {"push", Builtin::Push},     {"pop", Builtin::Pop},
        {"str", Builtin::Str},       {"typeof", Builtin::Typeof},
        {"format", Builtin::Format}, {"min", Builtin::Min},
        {"max", Builtin::Max},       {"abs", Builtin::Abs},
        {"round", Builtin::Round},
    };
    for (const auto &pair : all) {
        Builtin id = Builtin::Count;
        EXPECT_TRUE(CS::findBuiltin(pair.first, &id)) << pair.first;
        EXPECT_EQ(id, pair.second) << pair.first;
    }
}

TEST(BuiltinTable, RejectsUnknownNames) {
    Builtin id = Builtin::Count;
    EXPECT_FALSE(CS::findBuiltin("cnt", &id));
    EXPECT_FALSE(CS::findBuiltin("", &id));
    EXPECT_FALSE(CS::findBuiltin("Count", &id));  // регистр значим
    EXPECT_FALSE(CS::findBuiltin("counts", &id));
}

TEST(BuiltinTable, ArityMatchesTheSpecification) {
    // docs/semantics.md §8: один аргумент.
    for (Builtin id : {Builtin::Count, Builtin::Keys, Builtin::Last,
                       Builtin::Pop, Builtin::Str, Builtin::Typeof,
                       Builtin::Abs, Builtin::Round}) {
        EXPECT_EQ(CS::builtinInfo(id).minArgs, 1);
        EXPECT_EQ(CS::builtinInfo(id).maxArgs, 1);
    }
    // Два аргумента.
    for (Builtin id : {Builtin::Has, Builtin::Push, Builtin::Min,
                       Builtin::Max}) {
        EXPECT_EQ(CS::builtinInfo(id).minArgs, 2);
        EXPECT_EQ(CS::builtinInfo(id).maxArgs, 2);
    }
    // format — от одного шаблона и сколько угодно аргументов (§8.9).
    EXPECT_EQ(CS::builtinInfo(Builtin::Format).minArgs, 1);
    EXPECT_EQ(CS::builtinInfo(Builtin::Format).maxArgs, CS::kVariadic);
}

TEST(BuiltinTable, OnlyPushAndPopAreVoid) {
    // Команды отделены от запросов (§8): либо меняет данные и не возвращает
    // значения, либо возвращает и не меняет.
    EXPECT_FALSE(CS::builtinInfo(Builtin::Push).returnsValue);
    EXPECT_FALSE(CS::builtinInfo(Builtin::Pop).returnsValue);
    for (Builtin id : {Builtin::Count, Builtin::Keys, Builtin::Has,
                       Builtin::Last, Builtin::Str, Builtin::Typeof,
                       Builtin::Format, Builtin::Min, Builtin::Max,
                       Builtin::Abs, Builtin::Round}) {
        EXPECT_TRUE(CS::builtinInfo(id).returnsValue);
    }
}

TEST(BuiltinTable, IsSortedByName) {
    // Поиск двоичный, поэтому порядок таблицы — инвариант, а не оформление.
    std::string_view previous;
    // Typeof — последний по алфавиту, значит и последний в enum.
    for (int i = 0; i <= static_cast<int>(Builtin::Typeof); ++i) {
        const std::string_view name =
            CS::builtinInfo(static_cast<Builtin>(i)).name;
        EXPECT_LT(previous, name) << i;
        previous = name;
    }
}

TEST(PlaceholderCount, CountsAndRespectsEscaping) {
    // docs/semantics.md §8.9: плейсхолдер ${}, последовательность $${} даёт
    // литеральное ${} и плейсхолдером не является.
    EXPECT_EQ(CS::countPlaceholders(""), 0u);
    EXPECT_EQ(CS::countPlaceholders("без подстановок"), 0u);
    EXPECT_EQ(CS::countPlaceholders("${}"), 1u);
    EXPECT_EQ(CS::countPlaceholders("Привет, ${}!"), 1u);
    EXPECT_EQ(CS::countPlaceholders("${} из ${}"), 2u);
    EXPECT_EQ(CS::countPlaceholders("цена $${}"), 0u);
    EXPECT_EQ(CS::countPlaceholders("$${} и ${}"), 1u);
    // Одинокие символы плейсхолдера не образуют: $ без {} и { без $.
    EXPECT_EQ(CS::countPlaceholders("$"), 0u);
    EXPECT_EQ(CS::countPlaceholders("${"), 0u);
    EXPECT_EQ(CS::countPlaceholders("{}"), 0u);
    EXPECT_EQ(CS::countPlaceholders("$$"), 0u);
}

}  // namespace
```

- [ ] **Шаг 2: Убедиться, что не собирается**

Run: `cmake --build build -j`
Expected: ошибка — `builtin.hpp` не существует.

- [ ] **Шаг 3: Написать `core/src/builtin.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string_view>

namespace CS {

/// Встроенные функции языка (docs/semantics.md §8).
///
/// Порядок совпадает с алфавитным порядком имён: таблица ищется двоично.
enum class Builtin : std::uint8_t {
    Abs, Count, Format, Has, Keys, Last, Max, Min, Pop, Push, Round, Str, Typeof
};

/// Верхняя граница числа аргументов отсутствует. Только у format (§8.9).
inline constexpr std::uint8_t kVariadic = 255;

/// Что проходу и вычислителю нужно знать о функции, не вызывая её.
struct BuiltinInfo {
    std::string_view name;
    std::uint8_t minArgs;
    std::uint8_t maxArgs;   ///< kVariadic — без верхней границы
    bool returnsValue;      ///< false — Void (§2.2): результат использовать нельзя
};

/// Находит функцию по имени. false — такой функции нет.
bool findBuiltin(std::string_view name, Builtin *out) noexcept;

const BuiltinInfo &builtinInfo(Builtin id) noexcept;

/// Сколько плейсхолдеров ${} в шаблоне; $${} даёт литеральное ${} и не считается
/// (docs/semantics.md §8.9).
///
/// Одна функция на два потребителя: статический проход сверяет ею число
/// аргументов при литеральном шаблоне, вычислитель ею же разбирает шаблон при
/// сборке строки. Правило записано один раз.
std::uint32_t countPlaceholders(std::string_view fmt) noexcept;

}  // namespace CS
```

- [ ] **Шаг 4: Написать `core/src/builtin.cpp`**

```cpp
#include "builtin.hpp"

#include <algorithm>
#include <cassert>

namespace CS {
namespace {

/// Отсортирована по имени: findBuiltin ищет двоично, как findKey в контексте.
/// Порядок обязан совпадать с порядком в enum Builtin — на этом стоит индексация
/// в builtinInfo, и тест BuiltinTable.IsSortedByName стережёт инвариант.
constexpr BuiltinInfo kTable[] = {
    {"abs", 1, 1, true},        {"count", 1, 1, true},
    {"format", 1, kVariadic, true}, {"has", 2, 2, true},
    {"keys", 1, 1, true},       {"last", 1, 1, true},
    {"max", 2, 2, true},        {"min", 2, 2, true},
    {"pop", 1, 1, false},       {"push", 2, 2, false},
    {"round", 1, 1, true},      {"str", 1, 1, true},
    {"typeof", 1, 1, true},
};

constexpr std::size_t kCount = sizeof kTable / sizeof kTable[0];
static_assert(kCount == static_cast<std::size_t>(Builtin::Typeof) + 1,
              "таблица и enum обязаны совпадать по составу");

}  // namespace

bool findBuiltin(std::string_view name, Builtin *out) noexcept {
    const BuiltinInfo *first = kTable;
    const BuiltinInfo *last = kTable + kCount;
    const BuiltinInfo *found = std::lower_bound(
        first, last, name,
        [](const BuiltinInfo &info, std::string_view key) {
            return info.name < key;
        });
    if (found == last || found->name != name) { return false; }
    *out = static_cast<Builtin>(found - first);
    return true;
}

const BuiltinInfo &builtinInfo(Builtin id) noexcept {
    const std::size_t index = static_cast<std::size_t>(id);
    assert(index < kCount);
    return kTable[index];
}

std::uint32_t countPlaceholders(std::string_view fmt) noexcept {
    std::uint32_t count = 0;
    std::size_t i = 0;
    while (i < fmt.size()) {
        // $${} — экранированный плейсхолдер: даёт литеральное ${}, но сам им
        // не является.
        if (fmt.compare(i, 4, "$${}") == 0) {
            i += 4;
            continue;
        }
        if (fmt.compare(i, 3, "${}") == 0) {
            ++count;
            i += 3;
            continue;
        }
        ++i;
    }
    return count;
}

}  // namespace CS
```

Обрати внимание: `std::string_view::compare(pos, len, str)` при нехватке байт до конца сравнивает то, что есть, и даёт ненулевой результат — выхода за границу не происходит.

- [ ] **Шаг 5: Подключить в сборку**

В `core/CMakeLists.txt` добавить `src/builtin.cpp` в список исходников библиотеки и `tests/builtin_test.cpp` — в список тестовых. Найди, как это сделано для `operator.cpp` и `operator_test.cpp`, и повтори тем же способом.

- [ ] **Шаг 6: Собрать и прогнать**

Run: `cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure -R Builtin`
Expected: 6 тестов PASS.

- [ ] **Шаг 7: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 424 из 424 (418 было + 6).

- [ ] **Шаг 8: Коммит**

```bash
git add core/src/builtin.hpp core/src/builtin.cpp core/tests/builtin_test.cpp core/CMakeLists.txt
git commit -m "Add the builtin table"
```

---

## Задача 2: Статический проход, отметка и фасад компиляции

**Files:**
- Create: `core/src/check.hpp`, `core/src/check.cpp`, `core/src/compile.hpp`, `core/src/compile.cpp`, `core/tests/check_test.cpp`
- Modify: `core/src/ast.hpp`, `core/src/ast.cpp`, `core/CMakeLists.txt`

**Interfaces:**
- Consumes: `findBuiltin`, `builtinInfo`, `countPlaceholders`, `kVariadic` из задачи 1.
- Produces: `std::uint32_t CS::check(Ast &, const Context &, Diagnostic *, std::uint32_t)`; `std::uint32_t CS::compileExpression(const char *, std::uint32_t, Ast &, const Context &, Diagnostic *, std::uint32_t)`; `std::uint32_t CS::compileScript(...)` той же формы; `Ast::markChecked()`, `Ast::isChecked()`.

- [ ] **Шаг 1: Написать тесты**

Создать `core/tests/check_test.cpp`:

```cpp
#include "check.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include "ast.hpp"
#include "compile.hpp"
#include "context.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "parser.hpp"

namespace {

using CS::Ast;
using CS::Context;
using CS::Diagnostic;

/// Кладёт переменную с данными.
void put(Context &ctx, std::string_view name, std::string_view text) {
    Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(ctx, name, text, diag)) << diag.message;
}

/// Компилирует выражение и возвращает найденные ошибки.
std::vector<Diagnostic> checkExpr(Context &ctx, std::string_view text,
                                  std::uint32_t capacity = 8) {
    Ast ast;
    std::vector<Diagnostic> found(capacity);
    const std::uint32_t count = CS::compileExpression(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx,
        found.data(), capacity);
    found.resize(std::min<std::uint32_t>(count, capacity));
    return found;
}

/// То же для скрипта.
std::vector<Diagnostic> checkScript(Context &ctx, std::string_view text,
                                    std::uint32_t capacity = 8) {
    Ast ast;
    std::vector<Diagnostic> found(capacity);
    const std::uint32_t count = CS::compileScript(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx,
        found.data(), capacity);
    found.resize(std::min<std::uint32_t>(count, capacity));
    return found;
}

TEST(Check, CleanExpressionPasses) {
    Context ctx;
    put(ctx, "items", "[1, 2, 3]");
    EXPECT_TRUE(checkExpr(ctx, "count(items)").empty());
    EXPECT_TRUE(checkExpr(ctx, "items[0] + 1").empty());
}

TEST(Check, UnknownFunctionIsACompileError) {
    Context ctx;
    put(ctx, "items", "[1]");
    const auto found = checkExpr(ctx, "cnt(items)");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].code, CS::ErrorCode::Name);
}

TEST(Check, WrongArgumentCountIsACompileError) {
    Context ctx;
    put(ctx, "items", "[1]");
    // docs/semantics.md §8: min берёт ровно два, count — ровно один.
    EXPECT_EQ(checkExpr(ctx, "min(1)").size(), 1u);
    EXPECT_EQ(checkExpr(ctx, "min(1, 2, 3)").size(), 1u);
    EXPECT_EQ(checkExpr(ctx, "count()").size(), 1u);
    EXPECT_EQ(checkExpr(ctx, "count(items, 1)").size(), 1u);
    // format вариадичен: и один, и пять аргументов допустимы по числу.
    EXPECT_TRUE(checkExpr(ctx, "format('нет подстановок')").empty());
}

TEST(Check, ValueReturningCallInStatementPositionIsAnError) {
    Context ctx;
    put(ctx, "items", "[1]");
    put(ctx, "state", "{'n': 0}");
    // docs/grammar.md §6.1: результат не используется.
    EXPECT_EQ(checkScript(ctx, "count(items);").size(), 1u);
    // А push значения не возвращает — он в позиции стейтмента на месте.
    EXPECT_TRUE(checkScript(ctx, "push(items, 1);").empty());
    // И использованный результат тоже на месте.
    EXPECT_TRUE(checkScript(ctx, "state.n = count(items);").empty());
}

TEST(Check, UsingTheResultOfAVoidBuiltinIsAnError) {
    Context ctx;
    put(ctx, "items", "[1]");
    put(ctx, "state", "{'n': 0}");
    // docs/grammar.md §6.2.
    EXPECT_EQ(checkScript(ctx, "state.n = push(items, 1);").size(), 1u);
    EXPECT_EQ(checkScript(ctx, "state.n = push(items, 1) + 1;").size(), 1u);
    EXPECT_EQ(checkScript(ctx, "push(items, pop(items));").size(), 1u);
    // Вложенный вызов, возвращающий значение, — не ошибка.
    EXPECT_TRUE(checkScript(ctx, "push(items, count(items));").empty());
}

TEST(Check, FormatPlaceholderMismatchIsCaughtWhenTheTemplateIsLiteral) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // docs/semantics.md §8.9: при литеральном шаблоне несовпадение — ошибка
    // компиляции.
    EXPECT_EQ(checkExpr(ctx, "format('${} и ${}', user.name)").size(), 1u);
    EXPECT_EQ(checkExpr(ctx, "format('нет', user.name)").size(), 1u);
    EXPECT_TRUE(checkExpr(ctx, "format('${}', user.name)").empty());
    // $${} плейсхолдером не является, поэтому аргументов не требует.
    EXPECT_TRUE(checkExpr(ctx, "format('цена $${}')").empty());
}

TEST(Check, FormatWithANonLiteralTemplateIsNotCheckedHere) {
    Context ctx;
    put(ctx, "user", "{'tpl': '${}'}");
    // Шаблон не литерал — сверять нечего, проверка уходит в выполнение.
    EXPECT_TRUE(checkExpr(ctx, "format(user.tpl, 1, 2, 3)").empty());
}

TEST(Check, AssigningToANameIsACompileError) {
    Context ctx;
    put(ctx, "state", "{'n': 0}");
    // Переезд из вычислителя: docs/semantics.md §7.2, закрывает B27.
    ASSERT_EQ(checkScript(ctx, "state = 1;").size(), 1u);
    EXPECT_EQ(checkScript(ctx, "state = 1;")[0].code, CS::ErrorCode::Name);
    EXPECT_TRUE(checkScript(ctx, "state.n = 1;").empty());
}

TEST(Check, UnknownNameIsACompileError) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // Переезд из выполнения в компиляцию: спека §5.5.
    ASSERT_EQ(checkExpr(ctx, "usre.name").size(), 1u);
    EXPECT_EQ(checkExpr(ctx, "usre.name")[0].code, CS::ErrorCode::Name);
    EXPECT_TRUE(checkExpr(ctx, "user.name").empty());
}

TEST(Check, MisspelledKeyIsNotAnErrorAtAnyStage) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // Асимметрия спеки §5.5: имя переменной проверяется, ключ — нет и никогда.
    // Обе половины обязательны, иначе правило вырождается в одностороннее.
    EXPECT_TRUE(checkExpr(ctx, "user.nmae").empty());
}

TEST(Check, NamesAreCheckedAgainstCompositionNotValues) {
    Context ctx;
    // Валидатору достаточно состава имён: значения не нужны.
    ctx.setRoot("user", CS::Value::null());
    EXPECT_TRUE(checkExpr(ctx, "user.profile.city").empty());
    EXPECT_EQ(checkExpr(ctx, "usre.profile").size(), 1u);
}

TEST(Check, AllErrorsAreReportedNotJustTheFirst) {
    Context ctx;
    put(ctx, "items", "[1]");
    // Смысл буфера вместо одного Diagnostic: валидатор показывает всё сразу.
    const auto found = checkScript(ctx, "cnt(items); min(1); usre.a = 1;");
    EXPECT_GE(found.size(), 3u);
}

TEST(Check, CountExceedsCapacityWhenTheBufferIsSmall) {
    Context ctx;
    put(ctx, "items", "[1]");
    Ast ast;
    Diagnostic one;
    const std::string_view text = "cnt(items); min(1); max(2);";
    const std::uint32_t count = CS::compileScript(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx, &one, 1);
    // Нашлось больше, чем поместилось: вызывающий об этом узнаёт.
    EXPECT_GT(count, 1u);
    EXPECT_EQ(one.code, CS::ErrorCode::Name);
}

TEST(Check, SyntaxErrorGivesExactlyOne) {
    Context ctx;
    // Парсер останавливается на первой; проверки до негодного дерева не идут.
    const auto found = checkExpr(ctx, "1 +");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].code, CS::ErrorCode::Syntax);
}

TEST(Check, CleanTreeIsMarkedAndFaultyIsNot) {
    Context ctx;
    put(ctx, "items", "[1]");
    Ast clean;
    Diagnostic buffer[4];
    const std::string_view good = "count(items)";
    EXPECT_EQ(CS::compileExpression(good.data(),
                                    static_cast<std::uint32_t>(good.size()),
                                    clean, ctx, buffer, 4),
              0u);
    EXPECT_TRUE(clean.isChecked());

    Ast faulty;
    const std::string_view bad = "cnt(items)";
    EXPECT_GT(CS::compileExpression(bad.data(),
                                    static_cast<std::uint32_t>(bad.size()),
                                    faulty, ctx, buffer, 4),
              0u);
    EXPECT_FALSE(faulty.isChecked());
}

TEST(Check, ResetClearsTheMark) {
    Context ctx;
    put(ctx, "items", "[1]");
    Ast ast;
    Diagnostic buffer[4];
    const std::string_view good = "count(items)";
    CS::compileExpression(good.data(), static_cast<std::uint32_t>(good.size()),
                          ast, ctx, buffer, 4);
    ASSERT_TRUE(ast.isChecked());
    // Повторный разбор выбрасывает дерево — отметка обязана уйти с ним.
    const std::string_view other = "1";
    ASSERT_TRUE(CS::parseExpression(
        other.data(), static_cast<std::uint32_t>(other.size()), ast, buffer[0]));
    EXPECT_FALSE(ast.isChecked());
}

}  // namespace
```

- [ ] **Шаг 2: Убедиться, что не собирается**

Run: `cmake --build build -j`
Expected: ошибка — `check.hpp` и `compile.hpp` не существуют.

- [ ] **Шаг 3: Добавить отметку в `core/src/ast.hpp`**

В публичную часть класса, после `setRoot`:

```cpp
    /// Помечает дерево прошедшим статические проверки (core/src/check.hpp).
    ///
    /// Ставит её только check при нуле находок; вычислитель требует её
    /// утверждением. Так «забыли проверить» падает на первом же тесте, а в
    /// релизе не стоит ничего.
    void markChecked() noexcept { checked_ = true; }
    [[nodiscard]] bool isChecked() const noexcept { return checked_; }
```

В приватную часть, рядом с прочими полями:

```cpp
    bool checked_ = false;
```

В `core/src/ast.cpp`, в теле `reset`, рядом с очисткой узлов добавить:

```cpp
    // Дерево выброшено — отметка уходит вместе с ним.
    checked_ = false;
```

- [ ] **Шаг 4: Написать `core/src/check.hpp`**

```cpp
#pragma once
#include <cstdint>

#include "ast.hpp"
#include "context.hpp"
#include "diagnostic.hpp"

namespace CS {

/// Проверяет дерево правилами docs/grammar.md §6 — теми, что требуют сведений
/// за пределами грамматики.
///
/// Возвращает, сколько ошибок нашлось; в out кладёт не больше capacity первых.
/// Возвращённое число может превысить capacity: вызывающий узнаёт, что нашлось
/// больше, чем поместилось. Ноль — дерево пригодно к вычислению, и на нём
/// ставится отметка markChecked.
///
/// Из ctx читается **только состав имён** (hasRoot): значения проверкам не
/// нужны, поэтому инструменту валидации довольно контекста, где под каждым
/// объявленным именем лежит null.
///
/// Проход не останавливается на первой ошибке — иначе исправлять пришлось бы по
/// одной.
std::uint32_t check(Ast &ast, const Context &ctx, Diagnostic *out,
                    std::uint32_t capacity);

}  // namespace CS
```

- [ ] **Шаг 5: Написать `core/src/check.cpp`**

```cpp
#include "check.hpp"

#include "builtin.hpp"

namespace CS {
namespace {

/// Состояние одного прохода: копит находки, не останавливаясь.
struct Checker {
    const Ast &ast;
    const Context &ctx;
    Diagnostic *out;
    std::uint32_t capacity;
    std::uint32_t found = 0;

    void report(NodeId node, ErrorCode code, const char *message) {
        if (found < capacity && out != nullptr) {
            out[found] = Diagnostic{code, ast.offset(node), message};
        }
        ++found;
    }

    /// Вызов, чей результат употреблён, обязан возвращать значение (§6.2).
    void requireValue(NodeId call) {
        Builtin id = Builtin::Count;
        if (!findBuiltin(ast.text(call), &id)) { return; }  // уже сообщено
        if (!builtinInfo(id).returnsValue) {
            report(call, ErrorCode::Name, "builtin does not return a value");
        }
    }

    /// Вызов в позиции стейтмента обязан значения не возвращать (§6.1).
    void requireVoid(NodeId call) {
        Builtin id = Builtin::Count;
        if (!findBuiltin(ast.text(call), &id)) { return; }
        if (builtinInfo(id).returnsValue) {
            report(call, ErrorCode::Name, "call result is not used");
        }
    }

    void checkCall(NodeId node) {
        Builtin id = Builtin::Count;
        if (!findBuiltin(ast.text(node), &id)) {
            report(node, ErrorCode::Name, "unknown function");
            return;
        }
        const BuiltinInfo &info = builtinInfo(id);
        const std::uint32_t count = ast.childCount(node);
        if (count < info.minArgs ||
            (info.maxArgs != kVariadic && count > info.maxArgs)) {
            report(node, ErrorCode::Name, "wrong number of arguments");
            return;
        }
        if (id != Builtin::Format) { return; }

        // Шаблон-литерал сверяется здесь; иначе проверка уходит в выполнение
        // (docs/semantics.md §8.9). Литерал с escape-последовательностями
        // пропускается: в дереве лежит недекодированный текст, и ${} могло бы
        // прийти из $ — считать по нему неверно.
        const NodeId tmpl = ast.child(node, 0);
        if (ast.kind(tmpl) != NodeKind::String || ast.hasEscape(tmpl)) { return; }
        if (countPlaceholders(ast.text(tmpl)) != count - 1) {
            report(node, ErrorCode::Name,
                   "format placeholder count does not match arguments");
        }
    }

    void checkNode(NodeId node) {
        switch (ast.kind(node)) {
            case NodeKind::Call:
                checkCall(node);
                break;

            case NodeKind::Identifier:
                // Узлы Identifier — это в точности обращения к именам: имя поля
                // у Member лежит текстом, а не ребёнком.
                if (!ctx.hasRoot(ast.text(node))) {
                    report(node, ErrorCode::Name, "unknown name");
                }
                break;

            case NodeKind::Assign:
                // Целью не может быть само имя (docs/semantics.md §7.2).
                if (ast.kind(ast.child(node, 0)) == NodeKind::Identifier) {
                    report(ast.child(node, 0), ErrorCode::Name,
                           "cannot assign to a variable name");
                }
                break;

            default:
                break;
        }

        // Употреблён ли результат вызова, видно от родителя: Call среди детей
        // означает, что его значение куда-то идёт. Родитель у узла один, а
        // пост-обход гарантирует, что дети идут раньше.
        const std::uint32_t children = ast.childCount(node);
        for (std::uint32_t i = 0; i < children; ++i) {
            const NodeId child = ast.child(node, i);
            if (ast.kind(child) != NodeKind::Call) { continue; }
            if (ast.kind(node) == NodeKind::CallStatement) {
                requireVoid(child);
            } else {
                requireValue(child);
            }
        }
    }
};

}  // namespace

std::uint32_t check(Ast &ast, const Context &ctx, Diagnostic *out,
                    std::uint32_t capacity) {
    const NodeId root = ast.root();
    if (root == kNoNode) { return 0; }

    Checker checker{ast, ctx, out, capacity};
    // Плоский цикл, а не рекурсия: узлы лежат в пост-обходе, дети раньше
    // родителей, и проверкам пропускать нечего — значит предела глубины у
    // прохода нет вовсе.
    for (NodeId node = 1; node <= root; ++node) {
        checker.checkNode(node);
    }
    // У корня родителя нет. Если корень — вызов, его результат и есть значение
    // выражения, то есть употреблён.
    if (ast.kind(root) == NodeKind::Call) { checker.requireValue(root); }

    if (checker.found == 0) { ast.markChecked(); }
    return checker.found;
}

}  // namespace CS
```

- [ ] **Шаг 6: Написать `core/src/compile.hpp`**

```cpp
#pragma once
#include <cstdint>

#include "ast.hpp"
#include "context.hpp"
#include "diagnostic.hpp"

namespace CS {

/// Разбирает выражение и проверяет его: одна дверь вместо двух шагов.
///
/// Возвращает число найденных ошибок; 0 — успех, и дерево помечено пригодным к
/// вычислению. Ошибка разбора даёт ровно единицу: парсер останавливается на
/// первой. Ошибок проверки может быть сколько угодно, и в out попадает не
/// больше capacity первых.
///
/// Буфер source обязан пережить дерево: имена и литералы хранятся срезами.
std::uint32_t compileExpression(const char *source, std::uint32_t length,
                                Ast &ast, const Context &ctx, Diagnostic *out,
                                std::uint32_t capacity);

/// То же для скрипта (docs/semantics.md §3.1).
std::uint32_t compileScript(const char *source, std::uint32_t length, Ast &ast,
                            const Context &ctx, Diagnostic *out,
                            std::uint32_t capacity);

}  // namespace CS
```

- [ ] **Шаг 7: Написать `core/src/compile.cpp`**

```cpp
#include "compile.hpp"

#include "check.hpp"
#include "parser.hpp"

namespace CS {
namespace {

std::uint32_t reportParseFailure(const Diagnostic &diag, Diagnostic *out,
                                 std::uint32_t capacity) {
    if (capacity > 0 && out != nullptr) { out[0] = diag; }
    return 1;
}

}  // namespace

std::uint32_t compileExpression(const char *source, std::uint32_t length,
                                Ast &ast, const Context &ctx, Diagnostic *out,
                                std::uint32_t capacity) {
    Diagnostic diag;
    if (!parseExpression(source, length, ast, diag)) {
        return reportParseFailure(diag, out, capacity);
    }
    return check(ast, ctx, out, capacity);
}

std::uint32_t compileScript(const char *source, std::uint32_t length, Ast &ast,
                            const Context &ctx, Diagnostic *out,
                            std::uint32_t capacity) {
    Diagnostic diag;
    if (!parseProgram(source, length, ast, diag)) {
        return reportParseFailure(diag, out, capacity);
    }
    return check(ast, ctx, out, capacity);
}

}  // namespace CS
```

- [ ] **Шаг 8: Подключить в сборку**

В `core/CMakeLists.txt` добавить `src/check.cpp` и `src/compile.cpp` в исходники библиотеки, `tests/check_test.cpp` — в тестовые.

- [ ] **Шаг 9: Собрать и прогнать**

Run: `cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure -R Check`
Expected: 16 тестов PASS.

- [ ] **Шаг 10: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 440 из 440 (424 + 16).

- [ ] **Шаг 11: Коммит**

```bash
git add core/src/check.hpp core/src/check.cpp core/src/compile.hpp core/src/compile.cpp core/src/ast.hpp core/src/ast.cpp core/tests/check_test.cpp core/CMakeLists.txt
git commit -m "Add the static check pass behind a compile facade"
```

---

## Задача 3: Ветка вызова и четыре функции над агрегатами

**Files:**
- Modify: `core/src/builtin.hpp`, `core/src/builtin.cpp`, `core/src/eval.hpp`, `core/src/eval.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `Builtin`, `builtinInfo`, `findBuiltin` из задачи 1; `check`, `compileExpression`, `compileScript`, `Ast::isChecked` из задачи 2.
- Produces: `bool CS::applyBuiltin(Builtin, Context &, const Value *args, std::uint32_t count, std::uint32_t offset, Value *out, Diagnostic &)` — с реализациями `count`, `keys`, `has`, `last`; ветка `NodeKind::Call` в обходе; `CallStatement` в `execute`.

- [ ] **Шаг 1: Перевести существующие тесты на фасад**

`evalExpression` и `runScript` начнут требовать отметку, поэтому четыре вспомогательные функции в `core/tests/eval_test.cpp` обязаны ходить через фасад. Правится только они — все существующие тесты пользуются ими.

Замени тела `evaluate`, `evalError`, `run` и `runError` так, чтобы вместо `parseExpression`/`parseProgram` вызывался `compileExpression`/`compileScript` с буфером на один `Diagnostic`, а ноль возвращённых ошибок считался успехом разбора и проверки. Добавь `#include "compile.hpp"`.

Например, `evaluate` принимает вид:

```cpp
Value evaluate(Context &ctx, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors = CS::compileExpression(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx, &diag, 1);
    EXPECT_EQ(errors, 0u) << diag.message;
    Value out = Value::null();
    EXPECT_TRUE(CS::evalExpression(ast, ctx, &out, diag)) << diag.message;
    return out;
}
```

Остальные три:

```cpp
Diagnostic evalError(Context &ctx, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors = CS::compileExpression(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx, &diag, 1);
    EXPECT_EQ(errors, 0u) << diag.message;
    Value out = Value::null();
    EXPECT_FALSE(CS::evalExpression(ast, ctx, &out, diag));
    return diag;
}

void run(Context &ctx, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors = CS::compileScript(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx, &diag, 1);
    ASSERT_EQ(errors, 0u) << diag.message;
    ASSERT_TRUE(CS::runScript(ast, ctx, diag)) << diag.message;
}

Diagnostic runError(Context &ctx, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    const std::uint32_t errors = CS::compileScript(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx, &diag, 1);
    EXPECT_EQ(errors, 0u) << diag.message;
    EXPECT_FALSE(CS::runScript(ast, ctx, diag));
    return diag;
}
```

**Часть существующих тестов после этого обязана сломаться, и это правильно:** те, что ждали ошибки выполнения от `usre.a` и от `state = 1;`, теперь получают ошибку компиляции. Перенеси их в `core/tests/check_test.cpp`, если такой случай там ещё не покрыт, иначе удали как дубликат. Список сломавшихся даст `ctest`; в отчёте перечисли, что с каждым сделал.

- [ ] **Шаг 2: Написать тесты вызова**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(EvalCall, CountOfEachKind) {
    Context ctx;
    put(ctx, "items", "[10, 20, 30]");
    put(ctx, "o", "{'a': 1, 'b': 2}");
    put(ctx, "empty", "[]");
    EXPECT_EQ(evaluate(ctx, "count(items)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(ctx, "count(o)").numberValue(), 2.0);
    EXPECT_EQ(evaluate(ctx, "count(empty)").numberValue(), 0.0);
}

TEST(EvalCall, CountOfStringCountsBytesNotCharacters) {
    Context ctx;
    // docs/semantics.md §8.1 явно: байты, а не символы.
    EXPECT_EQ(evaluate(ctx, "count('привет')").numberValue(), 12.0);
    EXPECT_EQ(evaluate(ctx, "count('😀')").numberValue(), 4.0);
    EXPECT_EQ(evaluate(ctx, "count('abc')").numberValue(), 3.0);
    EXPECT_EQ(evaluate(ctx, "count('')").numberValue(), 0.0);
}

TEST(EvalCall, CountRejectsScalarsOtherThanString) {
    Context ctx;
    EXPECT_EQ(evalError(ctx, "count(1)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "count(true)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "count(null)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, KeysReturnsEveryKey) {
    Context ctx;
    put(ctx, "o", "{'b': 1, 'a': 2, 'c': 3}");
    const Value keys = evaluate(ctx, "keys(o)");
    ASSERT_EQ(keys.kind(), Value::Kind::Array);
    ASSERT_EQ(ctx.arrayCount(keys), 3u);
    // Порядок docs/semantics.md §8.2 не определяет, поэтому тест собирает
    // множество, а не список: опираться на порядок значило бы обещать его.
    std::set<std::string> got;
    for (std::uint32_t i = 0; i < 3; ++i) {
        got.insert(std::string(ctx.string(ctx.arrayAt(keys, i))));
    }
    EXPECT_EQ(got, (std::set<std::string>{"a", "b", "c"}));
}

TEST(EvalCall, KeysOfEmptyObjectIsEmptyArray) {
    Context ctx;
    put(ctx, "o", "{}");
    EXPECT_EQ(ctx.arrayCount(evaluate(ctx, "keys(o)")), 0u);
}

TEST(EvalCall, KeysRejectsNonObjects) {
    Context ctx;
    put(ctx, "items", "[1]");
    EXPECT_EQ(evalError(ctx, "keys(items)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "keys('a')").code, CS::ErrorCode::Type);
}

TEST(EvalCall, HasDistinguishesAbsentFromNull) {
    Context ctx;
    put(ctx, "o", "{'present': 1, 'empty': null}");
    // docs/semantics.md §8.3: единственный способ их различить. Обе половины
    // обязательны, иначе правило вырождается.
    EXPECT_TRUE(evaluate(ctx, "has(o, 'present')").booleanValue());
    EXPECT_TRUE(evaluate(ctx, "has(o, 'empty')").booleanValue());
    EXPECT_FALSE(evaluate(ctx, "has(o, 'missing')").booleanValue());
    EXPECT_EQ(evaluate(ctx, "o.empty").kind(), Value::Kind::Null);
    EXPECT_EQ(evaluate(ctx, "o.missing").kind(), Value::Kind::Null);
}

TEST(EvalCall, HasCoercesTheKey) {
    Context ctx;
    put(ctx, "o", "{'0': 'ноль', 'true': 'да'}");
    EXPECT_TRUE(evaluate(ctx, "has(o, 0)").booleanValue());
    EXPECT_TRUE(evaluate(ctx, "has(o, true)").booleanValue());
    EXPECT_EQ(evalError(ctx, "has(o, [1])").code, CS::ErrorCode::Type);
}

TEST(EvalCall, LastOfArrayAndOfEmpty) {
    Context ctx;
    put(ctx, "items", "[10, 20, 30]");
    put(ctx, "empty", "[]");
    EXPECT_EQ(evaluate(ctx, "last(items)").numberValue(), 30.0);
    // docs/semantics.md §8.4: на пустом — null, тогда как items[count-1] дал бы
    // ошибку. Обе половины в одном тесте.
    EXPECT_EQ(evaluate(ctx, "last(empty)").kind(), Value::Kind::Null);
    EXPECT_EQ(evalError(ctx, "empty[count(empty) - 1]").code,
              CS::ErrorCode::Range);
}

TEST(EvalCall, LastRejectsNonArrays) {
    Context ctx;
    put(ctx, "o", "{}");
    EXPECT_EQ(evalError(ctx, "last(o)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, NestedCallsWork) {
    Context ctx;
    put(ctx, "o", "{'a': 1, 'b': 2}");
    EXPECT_EQ(evaluate(ctx, "count(keys(o))").numberValue(), 2.0);
}

TEST(EvalCall, ArgumentsAreEvaluatedLeftToRight) {
    Context ctx;
    put(ctx, "o", "{'k': 1}");
    put(ctx, "items", "[1]");
    // Первый аргумент негоден, второй тоже — побеждает первая ошибка.
    EXPECT_EQ(evalError(ctx, "has(items, [1])").code, CS::ErrorCode::Type);
}
```

Добавь `#include <set>` и `#include <string>` в блок стандартных заголовков `eval_test.cpp`, если их там нет.

- [ ] **Шаг 3: Убедиться, что тесты падают**

Run: `touch core/src/eval.cpp && cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalCall`
Expected: FAIL — узел `Call` попадает в защитную ветку `default`.

- [ ] **Шаг 4: Объявить `applyBuiltin` в `core/src/builtin.hpp`**

Добавить включения `"context.hpp"`, `"diagnostic.hpp"`, `"value.hpp"` и объявление:

```cpp
/// Применяет функцию к уже вычисленным аргументам. Все, кроме format: он
/// вариадичен и вычисляет аргументы по мере надобности, поэтому его цикл живёт
/// в вычислителе (core/src/eval.cpp).
///
/// Число аргументов и то, что функция возвращает значение, гарантированы
/// статическим проходом (core/src/check.hpp): здесь они не перепроверяются.
/// Для Void-функций *out не трогается — Void не становится значением (§2.2).
///
/// offset — смещение узла вызова, для диагностики.
bool applyBuiltin(Builtin id, Context &ctx, const Value *args,
                  std::uint32_t count, std::uint32_t offset, Value *out,
                  Diagnostic &diag);
```

- [ ] **Шаг 5: Реализовать четыре функции в `core/src/builtin.cpp`**

Добавить включения `<cmath>`, `"text.hpp"`. В анонимное пространство имён:

```cpp
bool failType(std::uint32_t offset, const char *message, Diagnostic &diag) {
    diag = Diagnostic{ErrorCode::Type, offset, message};
    return false;
}

/// Приводит скаляр к строке ключа по docs/semantics.md §4; агрегат — ошибка.
bool keyOf(const Context &ctx, Value v, char *buffer, std::string_view *out,
           std::uint32_t offset, Diagnostic &diag) {
    switch (v.kind()) {
        case Value::Kind::String: *out = ctx.string(v); return true;
        case Value::Kind::Number:
            *out = formatNumber(v.numberValue(), buffer, kNumberBufferSize);
            return true;
        case Value::Kind::Boolean:
            *out = v.booleanValue() ? "true" : "false";
            return true;
        case Value::Kind::Null: *out = "null"; return true;
        default:
            return failType(offset, "aggregate cannot be used as a key", diag);
    }
}
```

И в `applyBuiltin` — `switch` по `id` с четырьмя ветками:

```cpp
bool applyBuiltin(Builtin id, Context &ctx, const Value *args,
                  std::uint32_t count, std::uint32_t offset, Value *out,
                  Diagnostic &diag) {
    (void)count;  // арность гарантирована проходом
    switch (id) {
        case Builtin::Count:
            // Array, Object либо String (§8.1); у строки — байты, не символы.
            switch (args[0].kind()) {
                case Value::Kind::Array:
                    *out = Value::number(ctx.arrayCount(args[0]));
                    return true;
                case Value::Kind::Object:
                    *out = Value::number(ctx.objectCount(args[0]));
                    return true;
                case Value::Kind::String:
                    *out = Value::number(
                        static_cast<double>(ctx.string(args[0]).size()));
                    return true;
                default:
                    return failType(offset,
                                    "count expects an array, object or string",
                                    diag);
            }

        case Builtin::Keys: {
            if (args[0].kind() != Value::Kind::Object) {
                return failType(offset, "keys expects an object", diag);
            }
            const std::uint32_t size = ctx.objectCount(args[0]);
            // Точное выделение: длина известна заранее.
            Value result = ctx.makeArray(size);
            for (std::uint32_t i = 0; i < size; ++i) {
                // Порядок наружу не обещан (§8.2); мы отдаём тот, в котором
                // ключи лежат, и обещанием это не становится.
                ctx.arrayPush(result, ctx.makeString(ctx.objectKeyAt(args[0], i)));
            }
            *out = result;
            return true;
        }

        case Builtin::Has: {
            if (args[0].kind() != Value::Kind::Object) {
                return failType(offset, "has expects an object", diag);
            }
            char buffer[kNumberBufferSize];
            std::string_view key;
            if (!keyOf(ctx, args[1], buffer, &key, offset, diag)) { return false; }
            *out = Value::boolean(ctx.objectHas(args[0], key));
            return true;
        }

        case Builtin::Last: {
            if (args[0].kind() != Value::Kind::Array) {
                return failType(offset, "last expects an array", diag);
            }
            const std::uint32_t size = ctx.arrayCount(args[0]);
            // На пустом — null (§8.4): через индексацию это невыразимо.
            *out = size == 0 ? Value::null() : ctx.arrayAt(args[0], size - 1);
            return true;
        }

        default:
            // Остальные функции приходят следующими задачами.
            return failType(offset, "builtin is not implemented yet", diag);
    }
}
```

**Важно про `keys`:** `ctx.objectKeyAt` отдаёт срез пула текста, а `ctx.makeString` в тот же пул дописывает и вправе его переселить. Срез обязан браться заново на каждой итерации — как и написано выше, — а не сохраняться до цикла. `Context::appendText` алиас собственного пула распознаёт, но полагаться на это, держа срез через мутацию, всё равно нельзя.

- [ ] **Шаг 6: Написать ветку `Call` в `core/src/eval.cpp`**

Добавить `#include "builtin.hpp"`. В `switch` функции `eval`, перед `default`:

```cpp
        case NodeKind::Call: {
            Builtin id = Builtin::Count;
            const bool known = findBuiltin(ast.text(node), &id);
            // Неизвестное имя отсеял статический проход, а дереву мы доверяем
            // (спека §5.3): здесь это утверждение, а не диагностика.
            assert(known && "дерево обязано пройти check");
            (void)known;

            // Арность гарантирована проходом, поэтому буфер по самой широкой
            // невариадической функции — двум аргументам.
            Value args[2] = {Value::null(), Value::null()};
            const std::uint32_t count = ast.childCount(node);
            for (std::uint32_t i = 0; i < count; ++i) {
                if (!eval(ast, ast.child(node, i), ctx, &args[i], diag)) {
                    return false;
                }
            }
            return applyBuiltin(id, ctx, args, count, ast.offset(node), out, diag);
        }
```

Убрать из комментария к ветке `default` упоминание, что `NodeKind::Call` придёт в части 3b: он пришёл.

- [ ] **Шаг 7: Выполнять `CallStatement` в `execute`**

В `switch` функции `execute`, перед `default`:

```cpp
        case NodeKind::CallStatement: {
            // Вызов в позиции стейтмента возвращает Void, поэтому результат
            // читать нечего и незачем (docs/semantics.md §2.2).
            Value discarded = Value::null();
            return eval(ast, ast.child(node, 0), ctx, &discarded, diag);
        }
```

- [ ] **Шаг 8: Потребовать отметку в `core/src/eval.cpp`**

В `evalExpression` и в `runScript`, рядом с существующими утверждениями:

```cpp
    assert(ast.isChecked() && "дерево обязано пройти check перед вычислением");
```

В `core/src/eval.hpp` дописать в оба комментария строку о том, что дерево обязано быть скомпилировано `compileExpression` либо `compileScript` — то есть разобрано **и** проверено.

- [ ] **Шаг 9: Убрать переехавшую проверку из `assign`**

Ветка `case NodeKind::Identifier` в функции `assign` вместе с комментарием и `TODO(B27)` удаляется: правило переехало в статический проход. Дерево, дошедшее до вычислителя, эту форму содержать не может, и защитная ветка `default` покрывает её наравне с прочими невозможными.

- [ ] **Шаг 10: Собрать и прогнать**

Run: `touch core/src/eval.cpp && cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: все зелёные. Число тестов — 440 плюс 12 новых, минус перенесённые на шаге 1; назови итог в отчёте.

- [ ] **Шаг 11: Коммит**

```bash
git add core/src/builtin.hpp core/src/builtin.cpp core/src/eval.hpp core/src/eval.cpp core/tests/eval_test.cpp core/tests/check_test.cpp
git commit -m "Call builtins from the walk and add the aggregate queries"
```

---

## Задача 4: Команды и приведения — push, pop, str, typeof

**Files:**
- Modify: `core/src/builtin.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `applyBuiltin` из задачи 3.
- Produces: ветки `Push`, `Pop`, `Str`, `Typeof` в `applyBuiltin`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(EvalCall, PushAppends) {
    Context ctx;
    put(ctx, "items", "[1, 2]");
    run(ctx, "push(items, 3);");
    EXPECT_EQ(evaluate(ctx, "count(items)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(ctx, "items[2]").numberValue(), 3.0);
}

TEST(EvalCall, PushIsTheOnlyWayToGrowAnArray) {
    Context ctx;
    put(ctx, "items", "[1]");
    // docs/semantics.md §6.1: запись за границу — ошибка, расширяет только push.
    EXPECT_EQ(runError(ctx, "items[1] = 2;").code, CS::ErrorCode::Range);
    run(ctx, "push(items, 2);");
    EXPECT_EQ(evaluate(ctx, "items[1]").numberValue(), 2.0);
}

TEST(EvalCall, PushRejectsNonArrays) {
    Context ctx;
    put(ctx, "o", "{}");
    EXPECT_EQ(runError(ctx, "push(o, 1);").code, CS::ErrorCode::Type);
}

TEST(EvalCall, PopRemovesAndDoesNothingOnEmpty) {
    Context ctx;
    put(ctx, "items", "[1, 2]");
    put(ctx, "empty", "[]");
    run(ctx, "pop(items);");
    EXPECT_EQ(evaluate(ctx, "count(items)").numberValue(), 1.0);
    // docs/semantics.md §8.6: на пустом ничего не делает и не отказывает.
    run(ctx, "pop(empty);");
    EXPECT_EQ(evaluate(ctx, "count(empty)").numberValue(), 0.0);
}

TEST(EvalCall, TakingTheRemovedElementNeedsTwoSteps) {
    Context ctx;
    put(ctx, "state", "{'items': [1, 2, 3], 'taken': null}");
    // docs/semantics.md §8.6 показывает ровно эту пару: pop значения не
    // возвращает, читают его через last.
    run(ctx, "state.taken = last(state.items); pop(state.items);");
    EXPECT_EQ(evaluate(ctx, "state.taken").numberValue(), 3.0);
    EXPECT_EQ(evaluate(ctx, "count(state.items)").numberValue(), 2.0);
}

TEST(EvalCall, PushAndPopAreVisibleThroughAnAlias) {
    Context ctx;
    put(ctx, "state", "{'items': [1]}");
    const Value items = ctx.objectGet(ctx.root("state"), "items");
    ctx.setRoot("shortcut", items);
    // docs/semantics.md §2.3: мутация через один путь видна через второй.
    run(ctx, "push(state.items, 2);");
    EXPECT_EQ(evaluate(ctx, "count(shortcut)").numberValue(), 2.0);
}

TEST(EvalCall, StrConvertsScalars) {
    Context ctx;
    // docs/semantics.md §8.7 — правила §4.2 и §4.3.
    EXPECT_EQ(ctx.string(evaluate(ctx, "str(1)")), "1");
    EXPECT_EQ(ctx.string(evaluate(ctx, "str(0.5)")), "0.5");
    EXPECT_EQ(ctx.string(evaluate(ctx, "str(true)")), "true");
    EXPECT_EQ(ctx.string(evaluate(ctx, "str(false)")), "false");
    EXPECT_EQ(ctx.string(evaluate(ctx, "str(null)")), "null");
    EXPECT_EQ(ctx.string(evaluate(ctx, "str('уже строка')")), "уже строка");
}

TEST(EvalCall, StrRejectsAggregates) {
    Context ctx;
    put(ctx, "items", "[1]");
    put(ctx, "o", "{}");
    EXPECT_EQ(evalError(ctx, "str(items)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "str(o)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, TypeofNamesEveryType) {
    Context ctx;
    put(ctx, "items", "[1]");
    put(ctx, "o", "{}");
    // docs/semantics.md §8.8: строчными буквами.
    EXPECT_EQ(ctx.string(evaluate(ctx, "typeof(null)")), "null");
    EXPECT_EQ(ctx.string(evaluate(ctx, "typeof(true)")), "boolean");
    EXPECT_EQ(ctx.string(evaluate(ctx, "typeof(1)")), "number");
    EXPECT_EQ(ctx.string(evaluate(ctx, "typeof('a')")), "string");
    EXPECT_EQ(ctx.string(evaluate(ctx, "typeof(o)")), "object");
    EXPECT_EQ(ctx.string(evaluate(ctx, "typeof(items)")), "array");
}

TEST(EvalCall, TypeofAcceptsAggregatesUnlikeStr) {
    Context ctx;
    put(ctx, "items", "[1]");
    // typeof существует именно затем, чтобы защититься от непостоянных типов
    // в данных (§8.8), поэтому агрегат ему не ошибка — в отличие от str.
    EXPECT_EQ(ctx.string(evaluate(ctx, "typeof(items)")), "array");
    EXPECT_EQ(evalError(ctx, "str(items)").code, CS::ErrorCode::Type);
}
```

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `touch core/src/builtin.cpp && cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalCall`
Expected: FAIL с сообщением `builtin is not implemented yet`.

- [ ] **Шаг 3: Реализовать четыре ветки в `core/src/builtin.cpp`**

В `switch` функции `applyBuiltin`, перед `default`:

```cpp
        case Builtin::Push:
            if (args[0].kind() != Value::Kind::Array) {
                return failType(offset, "push expects an array", diag);
            }
            // Void: *out не трогается (§2.2).
            ctx.arrayPush(args[0], args[1]);
            return true;

        case Builtin::Pop:
            if (args[0].kind() != Value::Kind::Array) {
                return failType(offset, "pop expects an array", diag);
            }
            // На пустом ничего не делает и не отказывает (§8.6). Снятое
            // значение никуда не идёт: pop его не возвращает.
            ctx.arrayPop(args[0], nullptr);
            return true;

        case Builtin::Str: {
            if (args[0].kind() == Value::Kind::String) {
                *out = args[0];
                return true;
            }
            char buffer[kNumberBufferSize];
            std::string_view text;
            if (!keyOf(ctx, args[0], buffer, &text, offset, diag)) {
                // keyOf отказывает ровно на агрегатах — тех же, что запрещает
                // §8.7. Сообщение уточняем под str.
                diag.message = "str expects a scalar";
                return false;
            }
            *out = ctx.makeString(text);
            return true;
        }

        case Builtin::Typeof: {
            // Имена типов строчными (§8.8). Литералы статические, поэтому
            // makeString копирует их в пул один раз за вызов.
            const char *name = "null";
            switch (args[0].kind()) {
                case Value::Kind::Null: name = "null"; break;
                case Value::Kind::Boolean: name = "boolean"; break;
                case Value::Kind::Number: name = "number"; break;
                case Value::Kind::String: name = "string"; break;
                case Value::Kind::Object: name = "object"; break;
                case Value::Kind::Array: name = "array"; break;
            }
            *out = ctx.makeString(name);
            return true;
        }
```

- [ ] **Шаг 4: Собрать и прогнать**

Run: `touch core/src/builtin.cpp && cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalCall`
Expected: все PASS.

- [ ] **Шаг 5: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: прежнее число плюс 10; назови итог в отчёте.

- [ ] **Шаг 6: Коммит**

```bash
git add core/src/builtin.cpp core/tests/eval_test.cpp
git commit -m "Add push, pop, str and typeof"
```

---

## Задача 5: Арифметические — min, max, abs, round

**Files:**
- Modify: `core/src/builtin.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `applyBuiltin`, `failType` из задач 3–4.
- Produces: ветки `Min`, `Max`, `Abs`, `Round`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(EvalCall, MinAndMaxTakeExactlyTwo) {
    Context ctx;
    EXPECT_EQ(evaluate(ctx, "min(1, 2)").numberValue(), 1.0);
    EXPECT_EQ(evaluate(ctx, "max(1, 2)").numberValue(), 2.0);
    EXPECT_EQ(evaluate(ctx, "min(-1, -2)").numberValue(), -2.0);
    // Для трёх и более — вложение (§8.10).
    EXPECT_EQ(evaluate(ctx, "min(3, min(1, 2))").numberValue(), 1.0);
}

TEST(EvalCall, MinAndMaxRejectNonNumbers) {
    Context ctx;
    EXPECT_EQ(evalError(ctx, "min('a', 1)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "max(1, true)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "min(null, 1)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, AbsIsTheModulus) {
    Context ctx;
    EXPECT_EQ(evaluate(ctx, "abs(3)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(ctx, "abs(-3)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(ctx, "abs(0)").numberValue(), 0.0);
    EXPECT_EQ(evalError(ctx, "abs('a')").code, CS::ErrorCode::Type);
}

TEST(EvalCall, RoundGoesAwayFromZero) {
    Context ctx;
    // docs/semantics.md §8.11 перечисляет ровно эти случаи: от нуля, а не к
    // чётному. round(2.5) даёт 3, чего половинное-к-чётному не дало бы.
    EXPECT_EQ(evaluate(ctx, "round(0.5)").numberValue(), 1.0);
    EXPECT_EQ(evaluate(ctx, "round(1.5)").numberValue(), 2.0);
    EXPECT_EQ(evaluate(ctx, "round(2.5)").numberValue(), 3.0);
    EXPECT_EQ(evaluate(ctx, "round(-0.5)").numberValue(), -1.0);
    EXPECT_EQ(evaluate(ctx, "round(1.4)").numberValue(), 1.0);
    EXPECT_EQ(evalError(ctx, "round(true)").code, CS::ErrorCode::Type);
}

TEST(EvalCall, ArithmeticBuiltinsFollowIEEEOnSpecialValues) {
    Context ctx;
    put(ctx, "inf", "1e400");
    // Бесконечность — значение, а не ошибка (§5.2), и функции её пропускают.
    EXPECT_TRUE(std::isinf(evaluate(ctx, "abs(inf)").numberValue()));
    EXPECT_TRUE(std::isinf(evaluate(ctx, "max(1, inf)").numberValue()));
    EXPECT_EQ(evaluate(ctx, "min(1, inf)").numberValue(), 1.0);
}
```

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `touch core/src/builtin.cpp && cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalCall`
Expected: FAIL с сообщением `builtin is not implemented yet`.

- [ ] **Шаг 3: Реализовать четыре ветки в `core/src/builtin.cpp`**

В анонимное пространство имён:

```cpp
/// Все четыре арифметические функции требуют Number и отказывают одинаково.
bool numbersOnly(const Value *args, std::uint32_t count, const char *what,
                 std::uint32_t offset, Diagnostic &diag) {
    for (std::uint32_t i = 0; i < count; ++i) {
        if (args[i].kind() != Value::Kind::Number) {
            return failType(offset, what, diag);
        }
    }
    return true;
}
```

В `switch` функции `applyBuiltin`, перед `default`:

```cpp
        case Builtin::Min:
            if (!numbersOnly(args, 2, "min expects numbers", offset, diag)) {
                return false;
            }
            *out = Value::number(
                std::fmin(args[0].numberValue(), args[1].numberValue()));
            return true;

        case Builtin::Max:
            if (!numbersOnly(args, 2, "max expects numbers", offset, diag)) {
                return false;
            }
            *out = Value::number(
                std::fmax(args[0].numberValue(), args[1].numberValue()));
            return true;

        case Builtin::Abs:
            if (!numbersOnly(args, 1, "abs expects a number", offset, diag)) {
                return false;
            }
            *out = Value::number(std::fabs(args[0].numberValue()));
            return true;

        case Builtin::Round:
            if (!numbersOnly(args, 1, "round expects a number", offset, diag)) {
                return false;
            }
            // От нуля, а не к чётному (§8.11): ровно то, что делает std::round.
            *out = Value::number(std::round(args[0].numberValue()));
            return true;
```

- [ ] **Шаг 4: Собрать и прогнать**

Run: `touch core/src/builtin.cpp && cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalCall`
Expected: все PASS.

- [ ] **Шаг 5: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: прежнее число плюс 5.

- [ ] **Шаг 6: Коммит**

```bash
git add core/src/builtin.cpp core/tests/eval_test.cpp
git commit -m "Add min, max, abs and round"
```

---

## Задача 6: `format`

**Files:**
- Modify: `core/src/context.hpp`, `core/src/context.cpp`, `core/src/eval.cpp`, `core/tests/eval_test.cpp`, `core/tests/context_test.cpp`

**Interfaces:**
- Consumes: `countPlaceholders` из задачи 1; `coerceToString` из `eval.cpp`; ветка `Call` из задачи 3.
- Produces: `Context::beginString`, `appendToString`, `endString`, `abortString`; цикл `format` в `eval.cpp`.

- [ ] **Шаг 1: Написать тесты сборки строки**

Дописать в `core/tests/context_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(ContextStringBuilder, AssemblesFromParts) {
    Context ctx;
    const std::uint32_t mark = ctx.beginString();
    ctx.appendToString("Привет");
    ctx.appendToString(", ");
    ctx.appendToString("мир");
    const Value built = ctx.endString(mark);
    EXPECT_EQ(ctx.string(built), "Привет, мир");
}

TEST(ContextStringBuilder, EmptyBuildGivesEmptyString) {
    Context ctx;
    const std::uint32_t mark = ctx.beginString();
    const Value built = ctx.endString(mark);
    EXPECT_EQ(ctx.string(built), "");
}

TEST(ContextStringBuilder, AbortLeavesNothingBehind) {
    Context ctx;
    const Value before = ctx.makeString("уже в пуле");
    const std::uint32_t used = ctx.bytesUsed();

    const std::uint32_t mark = ctx.beginString();
    ctx.appendToString("это будет выброшено");
    ctx.abortString(mark);

    // Пул усечён к метке, а прежняя строка цела.
    EXPECT_EQ(ctx.bytesUsed(), used);
    EXPECT_EQ(ctx.string(before), "уже в пуле");
}

TEST(ContextStringBuilder, SurvivesPoolGrowth) {
    Context ctx;
    // Кусков заведомо больше, чем влезет без переезда пула: сборка обязана
    // держаться на смещениях, а не на указателях.
    const std::uint32_t mark = ctx.beginString();
    std::string expected;
    for (int i = 0; i < 500; ++i) {
        ctx.appendToString("кусок");
        expected += "кусок";
    }
    EXPECT_EQ(ctx.string(ctx.endString(mark)), expected);
}
```

- [ ] **Шаг 2: Написать тесты `format`**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(EvalFormat, SubstitutesLeftToRight) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    put(ctx, "cart", "{'taken': 2, 'total': 5}");
    EXPECT_EQ(ctx.string(evaluate(ctx, "format('Привет, ${}!', user.name)")),
              "Привет, Вася!");
    EXPECT_EQ(ctx.string(evaluate(ctx,
                                  "format('${} из ${}', cart.taken, cart.total)")),
              "2 из 5");
}

TEST(EvalFormat, EscapedPlaceholderIsLiteral) {
    Context ctx;
    // docs/semantics.md §8.9: $${} даёт литеральное ${} и аргумента не требует.
    EXPECT_EQ(ctx.string(evaluate(ctx, "format('цена $${}')")), "цена ${}");
    EXPECT_EQ(ctx.string(evaluate(ctx, "format('$${} и ${}', 1)")), "${} и 1");
}

TEST(EvalFormat, NoPlaceholdersGivesTheTemplate) {
    Context ctx;
    EXPECT_EQ(ctx.string(evaluate(ctx, "format('без подстановок')")),
              "без подстановок");
    EXPECT_EQ(ctx.string(evaluate(ctx, "format('')")), "");
}

TEST(EvalFormat, ArgumentsAreCoercedByChapterFour) {
    Context ctx;
    EXPECT_EQ(ctx.string(evaluate(ctx, "format('${}', 0.5)")), "0.5");
    EXPECT_EQ(ctx.string(evaluate(ctx, "format('${}', true)")), "true");
    EXPECT_EQ(ctx.string(evaluate(ctx, "format('${}', null)")), "null");
}

TEST(EvalFormat, AggregateArgumentIsAnError) {
    Context ctx;
    put(ctx, "items", "[1]");
    EXPECT_EQ(evalError(ctx, "format('${}', items)").code, CS::ErrorCode::Type);
}

TEST(EvalFormat, NonStringTemplateIsAnError) {
    Context ctx;
    put(ctx, "n", "1");
    EXPECT_EQ(evalError(ctx, "format(n, 1)").code, CS::ErrorCode::Type);
}

TEST(EvalFormat, MismatchWithANonLiteralTemplateIsARuntimeError) {
    Context ctx;
    put(ctx, "tpl", "{'two': '${} и ${}', 'none': 'нет'}");
    // Шаблон не литерал, значит проход сверить не мог — ловится здесь (§8.9).
    EXPECT_EQ(evalError(ctx, "format(tpl.two, 1)").code, CS::ErrorCode::Type);
    EXPECT_EQ(evalError(ctx, "format(tpl.none, 1)").code, CS::ErrorCode::Type);
    // А совпадающее число проходит.
    EXPECT_EQ(ctx.string(evaluate(ctx, "format(tpl.two, 1, 2)")), "1 и 2");
}

TEST(EvalFormat, ResultIsAnOrdinaryString) {
    Context ctx;
    put(ctx, "state", "{'label': ''}");
    run(ctx, "state.label = format('${} шт.', 3);");
    EXPECT_EQ(ctx.string(evaluate(ctx, "state.label")), "3 шт.");
    EXPECT_EQ(evaluate(ctx, "count(state.label)").numberValue(), 9.0);
}

TEST(EvalFormat, LongResultCrossesPoolGrowth) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // Результат заведомо длиннее начальной ёмкости пула: сборка обязана
    // пережить его переезд.
    const Value built = evaluate(
        ctx,
        "format('${}${}${}${}${}${}${}${}${}${}', user.name, user.name,"
        " user.name, user.name, user.name, user.name, user.name, user.name,"
        " user.name, user.name)");
    std::string expected;
    for (int i = 0; i < 10; ++i) { expected += "Вася"; }
    EXPECT_EQ(ctx.string(built), expected);
}
```

- [ ] **Шаг 3: Убедиться, что тесты падают**

Run: `touch core/src/eval.cpp && cmake --build build -j`
Expected: ошибка сборки — у `Context` нет `beginString`.

- [ ] **Шаг 4: Добавить сборку строки в `core/src/context.hpp`**

В публичную часть, после `makeString`:

```cpp
    // ─── сборка строки по частям ───
    //
    // Нужна format (docs/semantics.md §8.9): длина результата заранее
    // неизвестна, а требовать её вторым проходом значило бы представлять числа
    // дважды. Метка — смещение в пуле, а не указатель, поэтому переезд пула
    // сборке безразличен.

    /// Начинает сборку. Возвращает метку для endString либо abortString.
    std::uint32_t beginString() noexcept;

    /// Дописывает кусок к собираемой строке.
    void appendToString(std::string_view bytes);

    /// Завершает сборку: строка — всё, что дописано после метки.
    Value endString(std::uint32_t mark) noexcept;

    /// Отменяет сборку, усекая пул к метке. Корректно лишь пока между beginString
    /// и этим вызовом в пул не писал никто другой.
    void abortString(std::uint32_t mark) noexcept;
```

- [ ] **Шаг 5: Реализовать в `core/src/context.cpp`**

Рядом с `makeString`:

```cpp
std::uint32_t Context::beginString() noexcept {
    return static_cast<std::uint32_t>(text_.size());
}

void Context::appendToString(std::string_view bytes) {
    // appendText уже умеет копировать срез собственного пула: аргумент format
    // вполне может им быть.
    appendText(bytes);
}

Value Context::endString(std::uint32_t mark) noexcept {
    const std::uint32_t size = static_cast<std::uint32_t>(text_.size()) - mark;
    return Value::string(mark, size);
}

void Context::abortString(std::uint32_t mark) noexcept {
    text_.resize(mark);
}
```

- [ ] **Шаг 6: Написать цикл `format` в `core/src/eval.cpp`**

`evalFormat` зовёт `eval`, а определяется раньше него, поэтому перед ней нужно предварительное объявление:

```cpp
bool eval(const Ast &ast, NodeId node, Context &ctx, Value *out,
          Diagnostic &diag);
```

Затем, в том же анонимном пространстве имён:

```cpp
/// Собирает строку по шаблону (docs/semantics.md §8.9).
///
/// Живёт здесь, а не в builtin.cpp, потому что format вариадичен: буфер под
/// заранее вычисленные аргументы потребовал бы верхней границы их числа,
/// которой §8.9 не устанавливает. Шаблон потребляет аргументы строго слева
/// направо, по одному на плейсхолдер, поэтому лениво выходит и проще, и без
/// придуманного предела.
bool evalFormat(const Ast &ast, NodeId node, Context &ctx, Value *out,
                Diagnostic &diag) {
    const std::uint32_t argCount = ast.childCount(node);

    Value tmpl = Value::null();
    if (!eval(ast, ast.child(node, 0), ctx, &tmpl, diag)) { return false; }
    if (tmpl.kind() != Value::Kind::String) {
        return fail(ast, node, ErrorCode::Type,
                    "format expects a string template", diag);
    }

    const std::uint32_t mark = ctx.beginString();
    std::uint32_t next = 1;      // следующий аргумент
    std::size_t i = 0;           // позиция в шаблоне
    std::size_t runStart = 0;    // начало неподставляемого куска

    // Срез шаблона берётся заново после каждого дописывания: пул вправе
    // переехать, и прежний срез повис бы. Смещение и длина при этом остаются
    // верными, потому что содержимое пула переезд сохраняет.
    while (i < ctx.string(tmpl).size()) {
        const std::string_view text = ctx.string(tmpl);
        const bool escaped = text.compare(i, 4, "$${}") == 0;
        const bool placeholder = !escaped && text.compare(i, 3, "${}") == 0;
        if (!escaped && !placeholder) {
            ++i;
            continue;
        }

        ctx.appendToString(text.substr(runStart, i - runStart));
        if (escaped) {
            ctx.appendToString("${}");
            i += 4;
            runStart = i;
            continue;
        }

        if (next >= argCount) {
            ctx.abortString(mark);
            return fail(ast, node, ErrorCode::Type,
                        "format placeholder count does not match arguments",
                        diag);
        }
        Value argument = Value::null();
        if (!eval(ast, ast.child(node, next), ctx, &argument, diag)) {
            ctx.abortString(mark);
            return false;
        }
        ++next;

        char buffer[kNumberBufferSize];
        std::string_view piece;
        if (!coerceToString(ast, node, ctx, argument, buffer, &piece, diag)) {
            ctx.abortString(mark);
            return false;
        }
        ctx.appendToString(piece);
        i += 3;
        runStart = i;
    }

    ctx.appendToString(ctx.string(tmpl).substr(runStart));

    if (next != argCount) {
        ctx.abortString(mark);
        return fail(ast, node, ErrorCode::Type,
                    "format placeholder count does not match arguments", diag);
    }
    *out = ctx.endString(mark);
    return true;
}
```

- [ ] **Шаг 7: Подключить в ветке `Call`**

В начале ветки `case NodeKind::Call`, сразу после разрешения имени:

```cpp
            // format вычисляет аргументы по мере надобности и идёт своим путём.
            if (id == Builtin::Format) { return evalFormat(ast, node, ctx, out, diag); }
```

- [ ] **Шаг 8: Собрать и прогнать**

Run: `touch core/src/eval.cpp core/src/context.cpp && cmake --build build -j && ctest --test-dir build --output-on-failure -R "EvalFormat|ContextStringBuilder"`
Expected: 13 тестов PASS.

- [ ] **Шаг 9: Прогнать весь набор под санитайзерами и с `-Werror`**

```bash
ctest --test-dir build --output-on-failure
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
cmake --build build-werror -j && ctest --test-dir build-werror --output-on-failure
```

Expected: одинаковое число во всех трёх, все зелёные, ни одного предупреждения, ни одного отчёта санитайзеров. Сборка строки — самое вероятное место висячего среза во всей части, поэтому ASan здесь несёт основную нагрузку.

- [ ] **Шаг 10: Коммит**

```bash
git add core/src/context.hpp core/src/context.cpp core/src/eval.cpp core/tests/eval_test.cpp core/tests/context_test.cpp
git commit -m "Add format and string assembly in the text pool"
```

---

## Задача 7: Бенчмарки

**Files:**
- Modify: `benchmarks/eval_benchmark.cpp`, `benchmarks/baseline.json`

**Interfaces:**
- Consumes: всё из задач 1–6.
- Produces: базу для сравнения при следующих этапах.

- [ ] **Шаг 1: Дописать бенчмарки**

В `benchmarks/eval_benchmark.cpp` добавить `#include "check.hpp"` и `#include "compile.hpp"`. Существующая `runEval` разбирает выражение через `parseExpression` — замени этот вызов на `compileExpression` с буфером на один `Diagnostic`, иначе вычисление упрётся в утверждение про отметку. То же для `runScriptBench`.

Затем в анонимное пространство имён, перед `}  // namespace`:

```cpp
/// Дешёвый билтин: один аргумент, ничего не выделяет.
void BM_Eval_CallCount(benchmark::State &state) { runEval(state, "count(items)"); }
BENCHMARK(BM_Eval_CallCount);

/// Выделяющий билтин: создаёт массив на каждый вызов.
void BM_Eval_CallKeys(benchmark::State &state) { runEval(state, "keys(map)"); }
BENCHMARK(BM_Eval_CallKeys);

/// Сборка строки: единственный билтин, растящий текстовый пул.
void BM_Eval_Format(benchmark::State &state) {
    runEval(state, "format('${} из ${}', 1, 2)");
}
BENCHMARK(BM_Eval_Format);

/// Вызов внутри выражения, какие и бывают в props.
void BM_Eval_CallInProps(benchmark::State &state) {
    runEval(state, "count(items) > 0 ? items[0] : 0");
}
BENCHMARK(BM_Eval_CallInProps);

/// Общая часть для прохода: разобрать один раз, мерить только проверки.
void runCheck(benchmark::State &state, std::string_view source, bool program) {
    Context ctx;
    if (!fill(ctx)) {
        state.SkipWithError("setVariable failed");
        return;
    }
    Ast ast;
    Diagnostic diag;
    const bool parsed =
        program ? CS::parseProgram(source.data(),
                                   static_cast<std::uint32_t>(source.size()),
                                   ast, diag)
                : CS::parseExpression(source.data(),
                                      static_cast<std::uint32_t>(source.size()),
                                      ast, diag);
    if (!parsed) {
        state.SkipWithError("parse failed");
        return;
    }
    for (auto _ : state) {
        Diagnostic found[1];
        std::uint32_t errors = CS::check(ast, ctx, found, 1);
        benchmark::DoNotOptimize(errors);
    }
}

/// Проход по дереву props. Сравнивать эту строку надо с BM_Parse_Props,
/// делённым на сто: решение делать проход обязательным стоит на том, что он
/// заметно дешевле разбора.
void BM_Check_Props(benchmark::State &state) {
    runCheck(state,
             "user.profile.city.code.zip > 0"
             " ? format('${}', user.name)"
             " : 'нет'",
             false);
}
BENCHMARK(BM_Check_Props);

/// Проход по дереву обработчика.
void BM_Check_Handler(benchmark::State &state) {
    runCheck(state,
             "push(items, 1);"
             "user.badge = count(items);"
             "user.label = format('${} шт.', count(items));",
             true);
}
BENCHMARK(BM_Check_Handler);
```

- [ ] **Шаг 2: Собрать в Release и прогнать**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks --benchmark_filter="Eval|Check"
```

Expected: прежние строки плюс шесть новых, ни одной с `SkipWithError`.

Посмотреть глазами: `BM_Eval_CallKeys` обязан быть дороже `BM_Eval_CallCount` — он выделяет массив, тот не выделяет ничего. Если нет, сообщи и базу не записывай.

- [ ] **Шаг 3: Сравнить с прежней базой**

```bash
uptime
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_out=/tmp/builtins-current.json --benchmark_out_format=json
python3 tools/bench-compare.py benchmarks/baseline.json /tmp/builtins-current.json
uptime
```

Порог различимости около восьми процентов (`docs/backlog.md` B24). Этап трогает `eval.cpp` и `context.cpp`, поэтому `BM_Eval_*` и `BM_Store_*` под подозрением.

**B24 описывает и то, как отличить устаревшую базу от настоящей регрессии:** собрать код базового коммита и померить его сегодня. Если сравнение выйдет за порог — сделай это прежде, чем что-то заключать, и приведи обе таблицы. Базу при превышении не переписывай без разбора.

Машина при замере обязана быть незанятой; приведи обе строки `uptime`.

- [ ] **Шаг 4: Ответить на вопрос, ради которого мерили проход**

В отчёте приведи отношение: `BM_Check_Props` против `BM_Parse_Props`, делённого на 100 (тот бенчмарк разбирает сто копий выражения). Спека §5.3 обосновывает обязательность прохода тем, что он заметно дешевле разбора. **Если это не подтвердится — это находка, сообщи её отдельно и не прячь в таблицу.**

- [ ] **Шаг 5: Записать базу**

```bash
cp /tmp/builtins-current.json benchmarks/baseline.json
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
for prefix in ('BM_Lex_', 'BM_Parse_', 'BM_Store_', 'BM_Data_', 'BM_Eval_', 'BM_Check_', 'BM_Version'):
    assert any(n.startswith(prefix) for n in names), prefix
print('база содержит все семейства, поле машины:', d['context']['chupascript_machine'])
"
```

Expected: проверка печатает подтверждение и не падает.

- [ ] **Шаг 6: Коммит**

```bash
git add benchmarks/eval_benchmark.cpp benchmarks/baseline.json
git commit -m "Record the builtin and check-pass performance baseline"
```

---

## Задача 8: Документы

**Files:**
- Modify: `docs/semantics.md`, `docs/backlog.md`

**Interfaces:**
- Consumes: решения задач 1–7.
- Produces: согласованные документы.

- [ ] **Шаг 1: Переписать таблицы главы 9 `docs/semantics.md`**

Обе таблицы §9.1 и §9.2 получают колонки **«Код»** и **«Кто диагностирует»**. Источник — один из четырёх: лексер, парсер, проход, вычислитель. Коды — из `core/src/diagnostic.hpp`: `Syntax`, `Name`, `Type`, `Range`, `Data`, `Usage`, `Memory`.

Строки §9.1 после переписывания:

| Условие | Код | Кто |
|---|---|---|
| Лексические (`docs/grammar.md` §4.9) | `Syntax` | лексер |
| Синтаксические (`docs/grammar.md` §5.5) | `Syntax` | парсер |
| Вызов неизвестной функции | `Name` | проход |
| Неверное число аргументов билтина | `Name` | проход |
| Вызов билтина, возвращающего значение, в позиции стейтмента | `Name` | проход |
| Использование результата `Void`-билтина | `Name` | проход |
| Несовпадение числа плейсхолдеров `format` при литеральном шаблоне | `Name` | проход |
| Цель присваивания — само имя | `Name` | проход |
| Обращение к необъявленному имени | `Name` | проход |

Две последние строки **переезжают из §9.2** — там их больше нет.

В §9.2 остаются пятнадцать строк минус две переехавшие. **У всех тринадцати
источник один — вычислитель**, а коды такие: `Range` у трёх строк про индекс
массива — дробный либо отрицательный индекс и запись за границу; у всех
остальных `Type`. Строка про `format` при нелитеральном шаблоне остаётся здесь
же с кодом `Type`.

Проверь каждую строку по коду, а не по этому плану: коды бери из вызовов `fail`
в `core/src/eval.cpp`, `core/src/operator.cpp` и `core/src/builtin.cpp`.
Расхождение плана с кодом — находка, сообщи её отдельно.

- [ ] **Шаг 2: Дописать в §9 абзац про стадии**

Перед таблицей §9.1:

```markdown
Ошибки разделены по стадии, на которой обнаруживаются. Ошибка компиляции
означает, что макет не применяется целиком: пришедший с сервера макет отвергнут,
хост остаётся на прежнем. Ошибка выполнения прерывает вычисление посреди работы,
и сделанное к этому моменту остаётся сделанным.

Отсюда правило, по которому условие попадает в ту или иную таблицу: **всё, что
устанавливается по одному дереву и составу имён, устанавливается до исполнения.**
```

- [ ] **Шаг 3: Уточнить §7.2**

В абзаце про то, что целью не может быть само имя, заменить упоминание отказа при вычислении на отказ при компиляции, сославшись на `docs/grammar.md` §6.

- [ ] **Шаг 4: Закрыть вопрос в главе 10**

Из списка частных вопросов главы 10 убрать пункт «Обращение к имени, отсутствующему в контексте, — ошибка выполнения либо ошибка компиляции» и вместо него записать ответ: это ошибка компиляции, и она верна при условии, что состав имён известен на момент компиляции. Условие записать свойством, а не запретом динамических имён: всякая будущая возможность языка обязана его сохранять.

- [ ] **Шаг 5: Обновить бэклог**

`B11` и `B27` закрываются: проход существует, проверки в нём. Формат закрытия возьми у уже закрытых пунктов файла.

`B23` дописывается: он перестал быть желательным. Правильность проверки имён держится на порядке «имена поставлены, затем компиляция», а обеспечить его нечем — `setVariable` зовётся когда угодно.

`B30` дописывается инвариантом: состав имён обязан оставаться известным на момент компиляции, иначе проверка имён становится невозможной. Объявления с литеральным именем инвариант сохраняют, вычисляемое имя ломает.

`TODO(B27)` в `core/src/eval.cpp` уже удалён вместе с переехавшей веткой (задача 3); убедись, что упоминаний не осталось: `grep -rn "B27" core/`.

- [ ] **Шаг 6: Проверить**

```bash
grep -c "^### B" docs/backlog.md
grep -rn "B27" core/ ; echo "(пусто — хорошо)"
cmake --build build -j && ctest --test-dir build --output-on-failure
```

Expected: число пунктов не изменилось (31); упоминаний B27 в коде нет; все тесты зелёные.

- [ ] **Шаг 7: Коммит**

```bash
git add docs/semantics.md docs/backlog.md
git commit -m "Say which stage and which component reports each error"
```

---

## Итог

| | |
|---|---|
| Задач | 8 |
| Новых файлов | 6 (`builtin.*`, `check.*`, `compile.*`) плюс два тестовых |
| Тестов добавлено | 62 |
| Бенчмарков добавлено | 6 |
| Строк изменено в парсере, лексере и операторах | 0 |

Часть 3b закончена, когда: `ctest` зелёный в обычной сборке, под ASan+UBSan и с `-Werror`; `benchmarks/baseline.json` содержит `BM_Check_*`; глава 9 семантики называет для каждой ошибки код и источник; B11 и B27 закрыты.

После неё вычислитель готов целиком, и следующий шаг — C API и обёртка на Swift.
