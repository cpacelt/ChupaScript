# `chupa -repl` — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Интерактивная оболочка языка: набрал выражение — увидел значение, набрал скрипт — увидел, что изменилось.

**Architecture:** Две чистые функции — печать значения и пересчёт колонки — живут отдельными единицами и проверяются тестами; цикл чтения их собирает и проверяется прогоном через канал. Режим строки задаёт человек префиксом `expr:` либо `script:`, поэтому оболочка не угадывает. Контекст один на сессию, роль хоста играет команда `:set`.

**Tech Stack:** C++17, gtest, CMake. Зависимостей не добавляется: строки читаются через `std::getline`.

**Спека:** `docs/superpowers/specs/2026-08-13-chupa-repl-design.md` — нормативна для этого плана.
**Семантика:** `docs/semantics.md` §2.1 типы, §2.3 ссылочность, §3.1 два режима, §4.3 представление чисел, §7.1 состав контекста.
**Грамматика:** `docs/grammar.md` §4.3 строковые литералы (набор escape-последовательностей).

## Global Constraints

- **C++17.** Стандарт задан в корневом `CMakeLists.txt`.
- **Зависимостей не добавляется.** Ни linenoise, ни readline: строки читаются `std::getline`.
- **Комментарии — по-русски. Весь вывод оболочки — по-английски**, включая рамку диагностики: оболочка не спорит с ядром, у которого сообщения английские.
- **`core/` не меняется ни одной строкой.** Ни исходники, ни тесты, ни `core/CMakeLists.txt`. Публичная поверхность ядра остаётся `core/include`.
- **Внутренние заголовки ядра подключаются приватно** — `target_include_directories(<цель> PRIVATE ${CMAKE_SOURCE_DIR}/core/src)`.
- **Код оболочки живёт в пространстве имён `chupa`**, ядро — в `CS`.
- **Колонка считается по символам UTF-8, а не по байтам.**
- **Печатается путь, а не всё виденное:** агрегат под двумя именами циклом не является.
- **Сборка:** `cmake -B build && cmake --build build -j`, прогон `ctest --test-dir build --output-on-failure`.
- **Коммитить явными путями.** `git add -A` не использовать.

---

## Структура файлов

| Файл | Ответственность |
|---|---|
| `cli/printer.hpp` / `.cpp` | печать значения литералами языка, с обрывом цикла |
| `cli/report.hpp` / `.cpp` | пересчёт смещения в колонку и строка со стрелкой |
| `cli/main.cpp` | разбор аргументов, цикл, команды, сборка вывода |
| `cli/tests/printer_test.cpp` | тесты печатника |
| `cli/tests/report_test.cpp` | тесты колонки |
| `cli/CMakeLists.txt` | цель `chupa` и внутренняя библиотека под тесты |
| `cli/tests/CMakeLists.txt` | цель тестов оболочки |

Печатник и пересчёт колонки — чистые функции, проверяемые тестами; цикл читает
поток и пишет в поток, тестами не покрывается и проверяется прогоном через
канал. Ради этой границы они и разведены по файлам.

Существующие интерфейсы, на которые опирается оболочка:

```cpp
// core/src/value.hpp
enum class Value::Kind : std::uint8_t { Null, Boolean, Number, String, Object, Array };
Kind kind() const noexcept;
bool booleanValue() const noexcept;    // предусловие: Kind::Boolean
double numberValue() const noexcept;   // предусловие: Kind::Number
bool sameAggregate(Value other) const noexcept;   // у скаляров всегда false

// core/src/context.hpp
std::string_view string(Value) const noexcept;
std::uint32_t arrayCount(Value) const noexcept;
Value arrayAt(Value, std::uint32_t) const noexcept;
std::uint32_t objectCount(Value) const noexcept;
std::string_view objectKeyAt(Value, std::uint32_t) const noexcept;
Value objectValueAt(Value, std::uint32_t) const noexcept;
std::uint32_t rootCount() const noexcept;
std::string_view rootNameAt(std::uint32_t) const noexcept;
Value root(std::string_view) const noexcept;

// core/src/data.hpp
bool setVariable(Context &, std::string_view name, std::string_view text, Diagnostic &);

// core/src/compile.hpp — возвращают число ошибок, 0 это успех
std::uint32_t compileExpression(const char *source, std::uint32_t length, Ast &,
                                const Context &, Diagnostic *out, std::uint32_t capacity);
std::uint32_t compileScript(const char *source, std::uint32_t length, Ast &,
                            const Context &, Diagnostic *out, std::uint32_t capacity);

// core/src/eval.hpp
bool evalExpression(const Ast &, Context &, Value *out, Diagnostic &);
bool runScript(const Ast &, Context &, Diagnostic &);

// core/src/text.hpp
inline constexpr std::size_t kNumberBufferSize;
std::string_view formatNumber(double, char *buffer, std::size_t size);

// core/src/diagnostic.hpp
struct Diagnostic { ErrorCode code; std::uint32_t offset; const char *message; };
```

`Context` копированию не подлежит — конструктор копирования удалён. Поэтому в
цикле он держится в `std::optional<CS::Context>`, и `:reset` делает `emplace()`.

---

## Задача 1: Пересчёт колонки и стрелка

**Files:**
- Create: `cli/report.hpp`, `cli/report.cpp`, `cli/tests/report_test.cpp`, `cli/tests/CMakeLists.txt`
- Modify: `cli/CMakeLists.txt`

**Interfaces:**
- Consumes: `CS::Diagnostic` из `core/src/diagnostic.hpp`.
- Produces: `std::uint32_t chupa::columnOf(std::string_view, std::uint32_t)`, `std::string chupa::caretLine(std::uint32_t column, std::uint32_t indent)`, `void chupa::reportDiagnostic(std::ostream &, std::string_view source, std::uint32_t indent, const CS::Diagnostic &)`.

- [ ] **Шаг 1: Написать тесты**

Создать `cli/tests/report_test.cpp`:

```cpp
#include "report.hpp"

#include <gtest/gtest.h>

namespace {

TEST(ColumnOf, CountsAsciiBytesAsColumns) {
    EXPECT_EQ(chupa::columnOf("abcdef", 0), 0u);
    EXPECT_EQ(chupa::columnOf("abcdef", 3), 3u);
    EXPECT_EQ(chupa::columnOf("abcdef", 6), 6u);
}

TEST(ColumnOf, CountsCharactersNotBytes) {
    // «имя» — шесть байт и три символа. Смещение шесть байт указывает на
    // точку, то есть на четвёртый символ.
    const std::string_view source = "имя.поле";
    EXPECT_EQ(chupa::columnOf(source, 6), 3u);
    // Смещение на «п» — семь байт от начала, четвёртый символ позади точки.
    EXPECT_EQ(chupa::columnOf(source, 7), 4u);
}

TEST(ColumnOf, CountsFourByteCharacters) {
    // Эмодзи занимает четыре байта и один символ.
    EXPECT_EQ(chupa::columnOf("😀x", 4), 1u);
    EXPECT_EQ(chupa::columnOf("😀x", 5), 2u);
}

TEST(ColumnOf, ClampsOffsetPastTheEnd) {
    // Диагностика вправе указывать на конец строки; за него — не вправе, но
    // оболочка не должна выходить за буфер, если это случится.
    EXPECT_EQ(chupa::columnOf("abc", 3), 3u);
    EXPECT_EQ(chupa::columnOf("abc", 99), 3u);
    EXPECT_EQ(chupa::columnOf("", 0), 0u);
    EXPECT_EQ(chupa::columnOf("", 5), 0u);
}

TEST(ColumnOf, OffsetInsideACharacterRoundsDown) {
    // Смещение внутрь многобайтового символа диагностика не порождает, но
    // округление вниз лучше выхода за границу.
    EXPECT_EQ(chupa::columnOf("имя", 1), 0u);
    EXPECT_EQ(chupa::columnOf("имя", 3), 1u);
}

TEST(CaretLine, PutsTheCaretUnderTheColumn) {
    EXPECT_EQ(chupa::caretLine(0, 0), "^");
    EXPECT_EQ(chupa::caretLine(3, 0), "   ^");
    // Отступ — ширина того, что оболочка напечатала до исходника: приглашение
    // «> » плюс префикс режима.
    EXPECT_EQ(chupa::caretLine(0, 8), "        ^");
    EXPECT_EQ(chupa::caretLine(2, 8), "          ^");
}

TEST(ReportDiagnostic, PrintsCaretThenMessage) {
    std::ostringstream out;
    const CS::Diagnostic diag{CS::ErrorCode::Name, 0, "unknown name"};
    chupa::reportDiagnostic(out, "usre.name", 8, diag);
    EXPECT_EQ(out.str(), "        ^\nerror: unknown name\n");
}

TEST(ReportDiagnostic, PlacesTheCaretByCharactersNotBytes) {
    std::ostringstream out;
    // В «'привет' + 1» оператор стоит на 15-м байте: кавычка, шесть кириллических
    // букв по два байта, кавычка, пробел. А символов до него девять.
    const CS::Diagnostic diag{CS::ErrorCode::Type, 15,
                              "arithmetic requires numbers"};
    chupa::reportDiagnostic(out, "'привет' + 1", 0, diag);
    EXPECT_EQ(out.str(), "         ^\nerror: arithmetic requires numbers\n");
}

}  // namespace
```

Добавь в начало файла `#include <sstream>` и `#include "diagnostic.hpp"`.

- [ ] **Шаг 2: Убедиться, что не собирается**

Run: `cmake -B build && cmake --build build -j`
Expected: ошибка — `cli/report.hpp` не существует.

- [ ] **Шаг 3: Написать `cli/report.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

namespace chupa {

/// Номер колонки для смещения, считая символы UTF-8, а не байты.
///
/// Diagnostic отдаёт смещение в байтах; поставленная по нему стрелка уехала бы
/// вправо на любой строке с кириллицей. Считаются начальные байты символов:
/// продолжающие байты (10xxxxxx) не считаются.
///
/// Смещение за концом строки прижимается к её длине, смещение внутрь символа
/// округляется вниз: диагностика такого не порождает, но выход за буфер хуже
/// приблизительной стрелки.
///
/// Считаются кодовые точки, а не ширина на экране: широкие знаки и
/// комбинирующие метки сдвинут стрелку. Для языка, где исходник это выражение
/// в props, такой точности довольно.
std::uint32_t columnOf(std::string_view source, std::uint32_t offset) noexcept;

/// Строка со стрелкой под указанной колонкой.
///
/// indent — ширина того, что оболочка напечатала до самого исходника:
/// приглашение плюс префикс режима.
std::string caretLine(std::uint32_t column, std::uint32_t indent);

/// Печатает диагностику: строку со стрелкой, затем сообщение.
///
/// Принимает поток, а не пишет в std::cout, — чтобы проверяться тестом.
void reportDiagnostic(std::ostream &out, std::string_view source,
                      std::uint32_t indent, const CS::Diagnostic &diag);

}  // namespace chupa
```

Заголовку понадобятся `#include <iosfwd>` и `#include "diagnostic.hpp"`.

- [ ] **Шаг 4: Написать `cli/report.cpp`**

```cpp
#include "report.hpp"

namespace chupa {

std::uint32_t columnOf(std::string_view source, std::uint32_t offset) noexcept {
    const std::size_t limit =
        offset < source.size() ? offset : source.size();
    std::uint32_t column = 0;
    for (std::size_t i = 0; i < limit; ++i) {
        // Продолжающий байт UTF-8 имеет вид 10xxxxxx и своего символа не
        // начинает — значит колонку не увеличивает.
        if ((static_cast<unsigned char>(source[i]) & 0xC0u) != 0x80u) {
            ++column;
        }
    }
    return column;
}

std::string caretLine(std::uint32_t column, std::uint32_t indent) {
    std::string line(static_cast<std::size_t>(indent) + column, ' ');
    line += '^';
    return line;
}

void reportDiagnostic(std::ostream &out, std::string_view source,
                      std::uint32_t indent, const CS::Diagnostic &diag) {
    out << caretLine(columnOf(source, diag.offset), indent) << "\n"
        << "error: " << diag.message << "\n";
}

}  // namespace chupa
```

В `.cpp` понадобится `#include <ostream>` — в заголовке довольно `<iosfwd>`.

- [ ] **Шаг 5: Завести сборку тестов оболочки**

Заменить `cli/CMakeLists.txt` целиком:

```cmake
# Чистые части оболочки — отдельной библиотекой, чтобы их могли собрать и
# исполняемый файл, и тесты.
add_library(chupa_cli_core STATIC
    printer.cpp
    report.cpp
)

target_include_directories(chupa_cli_core
    PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}
    PRIVATE ${CMAKE_SOURCE_DIR}/core/src
)

target_link_libraries(chupa_cli_core PRIVATE
    chupascript
    chupascript_compile_options
)

add_executable(chupa main.cpp)

target_include_directories(chupa PRIVATE ${CMAKE_SOURCE_DIR}/core/src)

target_link_libraries(chupa PRIVATE
    chupa_cli_core
    chupascript
    chupascript_compile_options
)

if(CHUPASCRIPT_BUILD_TESTS)
    add_subdirectory(tests)
endif()
```

Создать `cli/tests/CMakeLists.txt`:

```cmake
add_executable(chupa_cli_tests
    printer_test.cpp
    report_test.cpp
)

# Тесты печатника обращаются к внутреннему слою ядра — как и тесты самого ядра.
target_include_directories(chupa_cli_tests PRIVATE ${CMAKE_SOURCE_DIR}/core/src)

target_link_libraries(chupa_cli_tests PRIVATE
    chupa_cli_core
    chupascript
    chupascript_compile_options
    GTest::gtest_main
)

gtest_discover_tests(chupa_cli_tests)
```

**`printer.cpp` и `printer_test.cpp` появятся задачей 2, поэтому сейчас их в
списках быть не должно.** Выпиши `chupa_cli_core` с одним `report.cpp`, а
`chupa_cli_tests` — с одним `report_test.cpp`; задача 2 добавит вторые строки.

`cli/main.cpp` в этой задаче не меняется вовсе: заготовка печатает версию и ни на
что новое не опирается, поэтому цель `chupa` соберётся из неё как есть.

- [ ] **Шаг 6: Собрать и прогнать**

Run: `cmake -B build && cmake --build build -j && ctest --test-dir build --output-on-failure -R "ColumnOf|CaretLine|ReportDiagnostic"`
Expected: 8 тестов PASS.

- [ ] **Шаг 7: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 491 из 491 (483 ядра плюс 8 новых).

- [ ] **Шаг 8: Коммит**

```bash
git add cli/report.hpp cli/report.cpp cli/tests/report_test.cpp cli/tests/CMakeLists.txt cli/CMakeLists.txt
git commit -m "Turn a byte offset into a caret column"
```

---

## Задача 2: Печать значения

**Files:**
- Create: `cli/printer.hpp`, `cli/printer.cpp`, `cli/tests/printer_test.cpp`
- Modify: `cli/CMakeLists.txt`, `cli/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `CS::Context`, `CS::Value`, `CS::formatNumber`, `CS::kNumberBufferSize`.
- Produces: `std::string chupa::printValue(const CS::Context &, CS::Value)`.

- [ ] **Шаг 1: Написать тесты**

Создать `cli/tests/printer_test.cpp`:

```cpp
#include "printer.hpp"

#include <gtest/gtest.h>

#include <string_view>

#include "context.hpp"
#include "data.hpp"
#include "diagnostic.hpp"

namespace {

using CS::Context;
using CS::Diagnostic;
using CS::Value;

/// Кладёт переменную и возвращает её значение.
Value put(Context &ctx, std::string_view name, std::string_view text) {
    Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(ctx, name, text, diag)) << diag.message;
    return ctx.root(name);
}

TEST(PrintValue, Scalars) {
    Context ctx;
    EXPECT_EQ(chupa::printValue(ctx, Value::null()), "null");
    EXPECT_EQ(chupa::printValue(ctx, Value::boolean(true)), "true");
    EXPECT_EQ(chupa::printValue(ctx, Value::boolean(false)), "false");
    EXPECT_EQ(chupa::printValue(ctx, Value::number(42)), "42");
    EXPECT_EQ(chupa::printValue(ctx, Value::number(0.5)), "0.5");
    EXPECT_EQ(chupa::printValue(ctx, Value::number(-0.0)), "-0");
}

TEST(PrintValue, StringsAreQuoted) {
    Context ctx;
    // Кавычки обязательны: при строгой типизации отличить 1 от '1' глазами —
    // половина отладки выражения.
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "s", "'привет'")), "'привет'");
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "e", "''")), "''");
}

TEST(PrintValue, StringsAreEscapedBackToSource) {
    Context ctx;
    // Напечатанное обязано быть тем, что можно набрать обратно: набор
    // escape-последовательностей из docs/grammar.md §4.3.
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "q", "'it\\'s'")), "'it\\'s'");
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "b", "'a\\\\b'")), "'a\\\\b'");
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "n", "'a\\nb'")), "'a\\nb'");
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "t", "'a\\tb'")), "'a\\tb'");
}

TEST(PrintValue, EmptyAggregates) {
    Context ctx;
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "a", "[]")), "[]");
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "o", "{}")), "{}");
}

TEST(PrintValue, ArraysAndObjects) {
    Context ctx;
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "a", "[1, 2, 3]")), "[1, 2, 3]");
    EXPECT_EQ(chupa::printValue(ctx, put(ctx, "o", "{'a': 1}")), "{'a': 1}");
}

TEST(PrintValue, NestedAggregates) {
    Context ctx;
    const Value v = put(ctx, "v", "{'items': [1, {'k': null}], 'ok': true}");
    // Ключи объекта хранятся отсортированными, поэтому порядок предсказуем.
    EXPECT_EQ(chupa::printValue(ctx, v),
              "{'items': [1, {'k': null}], 'ok': true}");
}

TEST(PrintValue, SelfReferencingObjectTerminates) {
    Context ctx;
    const Value o = put(ctx, "o", "{'n': 1}");
    ctx.objectSet(o, "self", o);
    // docs/semantics.md §2.3 объявляет такую программу корректной; печатник
    // обязан завершиться, а не зациклиться.
    EXPECT_EQ(chupa::printValue(ctx, o), "{'n': 1, 'self': {...}}");
}

TEST(PrintValue, SelfReferencingArrayTerminates) {
    Context ctx;
    const Value a = put(ctx, "a", "[1]");
    ctx.arrayPush(a, a);
    EXPECT_EQ(chupa::printValue(ctx, a), "[1, [...]]");
}

TEST(PrintValue, SharedAggregateIsPrintedInFullTwice) {
    Context ctx;
    const Value shared = put(ctx, "shared", "[1, 2]");
    const Value holder = put(ctx, "holder", "{}");
    ctx.objectSet(holder, "a", shared);
    ctx.objectSet(holder, "b", shared);
    // Один агрегат под двумя ключами — не цикл. Отслеживается путь печати, а
    // не всё виденное, поэтому оба вхождения печатаются целиком.
    EXPECT_EQ(chupa::printValue(ctx, holder), "{'a': [1, 2], 'b': [1, 2]}");
}

TEST(PrintValue, MutualCycleTerminates) {
    Context ctx;
    const Value a = put(ctx, "a", "{}");
    const Value b = put(ctx, "b", "{}");
    ctx.objectSet(a, "b", b);
    ctx.objectSet(b, "a", a);
    // Цикл длиной два: путь ловит и его.
    EXPECT_EQ(chupa::printValue(ctx, a), "{'b': {'a': {...}}}");
}

}  // namespace
```

- [ ] **Шаг 2: Убедиться, что не собирается**

Run: `cmake --build build -j`
Expected: ошибка — `cli/printer.hpp` не существует.

- [ ] **Шаг 3: Написать `cli/printer.hpp`**

```cpp
#pragma once
#include <string>

#include "context.hpp"
#include "value.hpp"

namespace chupa {

/// Печатает значение литералами языка: [1, 2], {'a': 1}, строки в кавычках.
///
/// Печатает для человека, а не приводит к строке: str агрегаты отвергает
/// (docs/semantics.md §8.7), и это правило языка остаётся нетронутым.
///
/// Агрегат, встреченный на собственном пути печати, заменяется на {...} либо
/// [...]: самоссылка законна (§2.3) и зациклила бы наивный обход.
/// Отслеживается именно путь, а не всё виденное за печать, — один агрегат под
/// двумя именами циклом не является и печатается целиком оба раза.
std::string printValue(const CS::Context &ctx, CS::Value value);

}  // namespace chupa
```

- [ ] **Шаг 4: Написать `cli/printer.cpp`**

```cpp
#include "printer.hpp"

#include <vector>

#include "text.hpp"

namespace chupa {
namespace {

/// Строковый литерал языка: одинарные кавычки и пять escape-последовательностей
/// из docs/grammar.md §4.3. Напечатанное обязано набираться обратно.
void appendQuoted(std::string &out, std::string_view text) {
    out += '\'';
    for (const char c : text) {
        switch (c) {
            case '\\': out += "\\\\"; break;
            case '\'': out += "\\'"; break;
            case '\n': out += "\\n"; break;
            case '\t': out += "\\t"; break;
            default: out += c; break;
        }
    }
    out += '\'';
}

/// path — агрегаты, печатаемые прямо сейчас: путь от корня до текущего места.
void append(std::string &out, const CS::Context &ctx, CS::Value value,
            std::vector<CS::Value> &path) {
    switch (value.kind()) {
        case CS::Value::Kind::Null: out += "null"; return;
        case CS::Value::Kind::Boolean:
            out += value.booleanValue() ? "true" : "false";
            return;
        case CS::Value::Kind::Number: {
            char buffer[CS::kNumberBufferSize];
            out += CS::formatNumber(value.numberValue(), buffer, sizeof buffer);
            return;
        }
        case CS::Value::Kind::String:
            appendQuoted(out, ctx.string(value));
            return;
        default: break;
    }

    // Агрегат уже на пути — дальше идти значит зациклиться.
    for (const CS::Value &open : path) {
        if (open.sameAggregate(value)) {
            out += value.kind() == CS::Value::Kind::Array ? "[...]" : "{...}";
            return;
        }
    }
    path.push_back(value);

    if (value.kind() == CS::Value::Kind::Array) {
        out += '[';
        const std::uint32_t count = ctx.arrayCount(value);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (i != 0) { out += ", "; }
            append(out, ctx, ctx.arrayAt(value, i), path);
        }
        out += ']';
    } else {
        out += '{';
        const std::uint32_t count = ctx.objectCount(value);
        for (std::uint32_t i = 0; i < count; ++i) {
            if (i != 0) { out += ", "; }
            appendQuoted(out, ctx.objectKeyAt(value, i));
            out += ": ";
            append(out, ctx, ctx.objectValueAt(value, i), path);
        }
        out += '}';
    }

    path.pop_back();
}

}  // namespace

std::string printValue(const CS::Context &ctx, CS::Value value) {
    std::string out;
    std::vector<CS::Value> path;
    append(out, ctx, value, path);
    return out;
}

}  // namespace chupa
```

- [ ] **Шаг 5: Вернуть печатник в сборку**

В `cli/CMakeLists.txt` вернуть `printer.cpp` в список исходников
`chupa_cli_core`; в `cli/tests/CMakeLists.txt` вернуть `printer_test.cpp`.

- [ ] **Шаг 6: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R PrintValue`
Expected: 10 тестов PASS.

- [ ] **Шаг 7: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 501 из 501.

- [ ] **Шаг 8: Коммит**

```bash
git add cli/printer.hpp cli/printer.cpp cli/tests/printer_test.cpp cli/CMakeLists.txt cli/tests/CMakeLists.txt
git commit -m "Print values as language literals, cutting cycles by path"
```

---

## Задача 3: Каркас оболочки и команды без данных

**Files:**
- Modify: `cli/main.cpp`

**Interfaces:**
- Consumes: `chupa::columnOf`, `chupa::caretLine` из задачи 1.
- Produces: исполняемый файл `chupa`, цикл чтения, команды `:help` и `:quit`, отказ на строке без префикса.

- [ ] **Шаг 1: Переписать `cli/main.cpp`**

```cpp
// Интерактивная оболочка языка. Разбор аргументов, цикл чтения, команды.
//
// Чистые части — печать значения и пересчёт колонки — живут в printer.* и
// report.*: они проверяются тестами, а этот файл читает поток и пишет в поток,
// поэтому проверяется прогоном через канал.
#include <iostream>
#include <optional>
#include <string>
#include <string_view>

#include "chupascript/chupascript.h"
#include "context.hpp"
#include "report.hpp"

namespace {

constexpr std::string_view kExprPrefix = "expr:";
constexpr std::string_view kScriptPrefix = "script:";

/// Что делать после разобранной строки.
enum class After { Continue, Quit };

void printUsage(std::ostream &out) {
    out << "chupa " << chupascript_version() << "\n"
        << "\n"
        << "usage:\n"
        << "  chupa -repl    start the interactive shell\n";
}

void printHelp(std::ostream &out) {
    out << "  expr: <expression>   evaluate an expression\n"
        << "  script: <statements> run a script\n"
        << "  :set <name> = <literal>  put a variable into the context\n"
        << "  :vars                    list the context\n"
        << "  :reset                   start with an empty context\n"
        << "  :help                    this text\n"
        << "  :quit                    leave\n";
}

/// Обрезает пробелы с обоих концов.
std::string_view trim(std::string_view text) {
    std::size_t first = 0;
    while (first < text.size() && (text[first] == ' ' || text[first] == '\t')) {
        ++first;
    }
    std::size_t last = text.size();
    while (last > first && (text[last - 1] == ' ' || text[last - 1] == '\t')) {
        --last;
    }
    return text.substr(first, last - first);
}

/// Выполняет одну строку.
After handleLine(CS::Context &ctx, std::string_view line) {
    (void)ctx;
    const std::string_view text = trim(line);
    if (text.empty()) { return After::Continue; }

    if (text[0] == ':') {
        const std::string_view command = trim(text.substr(1));
        if (command == "quit") { return After::Quit; }
        if (command == "help") {
            printHelp(std::cout);
            return After::Continue;
        }
        std::cout << "error: unknown command\n";
        printHelp(std::cout);
        return After::Continue;
    }

    if (text.substr(0, kExprPrefix.size()) == kExprPrefix ||
        text.substr(0, kScriptPrefix.size()) == kScriptPrefix) {
        std::cout << "error: not implemented yet\n";
        return After::Continue;
    }

    // Режим задаётся человеком: оболочка не угадывает, выражение это или
    // скрипт (docs/superpowers/specs/2026-08-13-chupa-repl-design.md §3).
    std::cout << "error: a line must start with 'expr:', 'script:' or ':'\n";
    return After::Continue;
}

int runRepl() {
    std::optional<CS::Context> ctx;
    ctx.emplace();

    std::cout << "chupa " << chupascript_version() << ", :help for commands\n";

    std::string line;
    while (true) {
        std::cout << "> " << std::flush;
        if (!std::getline(std::cin, line)) {
            // Конец ввода: перевод строки, чтобы приглашение не осталось
            // висеть, и выход.
            std::cout << "\n";
            break;
        }
        if (handleLine(*ctx, line) == After::Quit) { break; }
    }
    return 0;
}

}  // namespace

int main(int argc, char **argv) {
    if (argc == 2 && std::string_view(argv[1]) == "-repl") {
        return runRepl();
    }
    if (argc == 1) {
        printUsage(std::cout);
        return 0;
    }
    printUsage(std::cerr);
    return 2;
}
```

Константы ширины приглашения здесь нет намеренно: она нужна только там, где
печатается стрелка, и появится задачей 4. Неиспользуемая `constexpr` в анонимном
пространстве имён даёт предупреждение `-Wunused-const-variable`, а сборка
`build-werror` считает предупреждения ошибками.

- [ ] **Шаг 2: Собрать**

Run: `cmake --build build -j`
Expected: сборка чистая, появился `build/cli/chupa`.

- [ ] **Шаг 3: Проверить прогоном через канал**

```bash
./build/cli/chupa
echo "код возврата: $?"
./build/cli/chupa -bogus; echo "код возврата: $?"
printf ':help\n:quit\n' | ./build/cli/chupa -repl
printf 'user.name\n:quit\n' | ./build/cli/chupa -repl
printf ':nope\n:quit\n' | ./build/cli/chupa -repl
printf '' | ./build/cli/chupa -repl; echo "код возврата: $?"
```

Ожидаемо: без аргументов usage и код 0; при неизвестном флаге usage в поток
ошибок и код 2; `:help` печатает команды; строка без префикса даёт сообщение про
`expr:` и `script:`; неизвестная команда даёт ошибку и список; пустой ввод
завершает работу с кодом 0.

Приведи вывод всех шести прогонов в отчёте.

- [ ] **Шаг 4: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 501 из 501 — число не меняется, тестов эта задача не добавляет.

- [ ] **Шаг 5: Коммит**

```bash
git add cli/main.cpp
git commit -m "Add the chupa binary and its read loop"
```

---

## Задача 4: Вычисление кода и печать диагностики

**Files:**
- Modify: `cli/main.cpp`

**Interfaces:**
- Consumes: `chupa::columnOf`, `chupa::caretLine`, `chupa::printValue`; `CS::compileExpression`, `CS::compileScript`, `CS::evalExpression`, `CS::runScript`.
- Produces: работающие `expr:` и `script:`.

- [ ] **Шаг 1: Дописать включения и разбор режима**

В `cli/main.cpp` добавить включения:

```cpp
#include "ast.hpp"
#include "compile.hpp"
#include "diagnostic.hpp"
#include "eval.hpp"
#include "printer.hpp"
#include "value.hpp"
```

В анонимное пространство имён:

```cpp
/// Ширина приглашения: на неё сдвинута всякая колонка, потому что исходник
/// человек набирал после него.
constexpr std::uint32_t kPromptWidth = 2;

/// Сколько ошибок статического прохода помещается в один отчёт.
///
/// Проход возвращает, сколько нашлось всего, поэтому о непоместившихся есть что
/// сказать. Строка в оболочке короткая, и восьми хватает с запасом.
constexpr std::uint32_t kMaxReported = 8;

/// Компилирует и выполняет строку кода.
///
/// source — то, что осталось после префикса режима; indent — ширина всего, что
/// напечатано до него.
void runCode(CS::Context &ctx, std::string_view source, std::uint32_t indent,
             bool asScript) {
    CS::Ast ast;
    CS::Diagnostic found[kMaxReported];
    const std::uint32_t length = static_cast<std::uint32_t>(source.size());
    const std::uint32_t errors =
        asScript ? CS::compileScript(source.data(), length, ast, ctx, found,
                                     kMaxReported)
                 : CS::compileExpression(source.data(), length, ast, ctx, found,
                                         kMaxReported);

    if (errors != 0) {
        // Печатаются все, а не первая: проход отдаёт их массивом именно затем.
        const std::uint32_t shown =
            errors < kMaxReported ? errors : kMaxReported;
        for (std::uint32_t i = 0; i < shown; ++i) {
            chupa::reportDiagnostic(std::cout, source, indent, found[i]);
        }
        if (errors > shown) {
            std::cout << "error: " << (errors - shown) << " more not shown\n";
        }
        return;
    }

    CS::Diagnostic diag;
    if (asScript) {
        // Скрипт при успехе молчит: значения у него нет, а результат виден
        // через :vars.
        if (!CS::runScript(ast, ctx, diag)) {
            chupa::reportDiagnostic(std::cout, source, indent, diag);
        }
        return;
    }

    CS::Value out = CS::Value::null();
    if (!CS::evalExpression(ast, ctx, &out, diag)) {
        chupa::reportDiagnostic(std::cout, source, indent, diag);
        return;
    }
    std::cout << chupa::printValue(ctx, out) << "\n";
}
```

- [ ] **Шаг 2: Подключить в `handleLine`**

Заменить ветку, печатающую `not implemented yet`, на:

```cpp
    const bool isExpr = text.substr(0, kExprPrefix.size()) == kExprPrefix;
    const bool isScript = text.substr(0, kScriptPrefix.size()) == kScriptPrefix;
    if (isExpr || isScript) {
        const std::size_t prefixSize =
            isExpr ? kExprPrefix.size() : kScriptPrefix.size();
        const std::string_view rest = text.substr(prefixSize);
        const std::string_view source = trim(rest);

        // Отступ считается по исходной строке, а не по обрезанной: человек
        // видит на экране приглашение, префикс и те пробелы, что набрал сам.
        const std::size_t sourceStart =
            static_cast<std::size_t>(source.data() - line.data());
        const std::uint32_t indent =
            kPromptWidth + chupa::columnOf(line, static_cast<std::uint32_t>(
                                                     sourceStart));

        runCode(ctx, source, indent, isScript);
        return true;
    }
```

**Осторожно с обрезкой.** `trim(line)` в начале `handleLine` уже сдвинул начало,
поэтому `source.data() - line.data()` считается от **исходной** строки, которую
видел человек, — именно она и напечатана на экране. Убедись, что `line` в
`handleLine` это полная строка, а не результат `trim`, и что `text` — отдельная
переменная.

- [ ] **Шаг 3: Собрать и проверить прогоном**

```bash
cmake --build build -j
printf 'expr: 1 + 1\nexpr: 2 * 3 - 1\n:quit\n' | ./build/cli/chupa -repl
printf "expr: 'привет'\nexpr: [1, 2, 3]\nexpr: {'a': 1}\n:quit\n" | ./build/cli/chupa -repl
printf 'expr: usre.name\n:quit\n' | ./build/cli/chupa -repl
printf 'expr: 1 +\n:quit\n' | ./build/cli/chupa -repl
printf 'expr: cnt(1) + min(2)\n:quit\n' | ./build/cli/chupa -repl
```

Ожидаемо: арифметика считается; строка печатается в кавычках, агрегаты
литералами; `usre.name` даёт стрелку под `usre` и `error: unknown name`;
`1 +` даёт синтаксическую ошибку со стрелкой; последняя строка даёт **две**
ошибки — неизвестная функция и неверное число аргументов.

**Проверь стрелку на кириллице:**

```bash
printf "expr: 'привет' + 1\n:quit\n" | ./build/cli/chupa -repl
```

Стрелка обязана встать под `+`, а не правее: если она уехала, `columnOf` считает
байты вместо символов.

Приведи вывод всех прогонов в отчёте.

- [ ] **Шаг 4: Проверить, что скрипт молчит и меняет данные**

```bash
printf 'script: ;\nexpr: 1\n:quit\n' | ./build/cli/chupa -repl
printf 'script: push(items, 1);\n:quit\n' | ./build/cli/chupa -repl
```

Ожидаемо: пустой скрипт ничего не печатает; второй даёт `error: unknown name`,
потому что `items` в контексте нет — данные появятся задачей 5.

- [ ] **Шаг 5: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 501 из 501.

- [ ] **Шаг 6: Коммит**

```bash
git add cli/main.cpp
git commit -m "Evaluate expressions and scripts, pointing at the failure"
```

---

## Задача 5: Данные — `:set`, `:vars`, `:reset`

**Files:**
- Modify: `cli/main.cpp`

**Interfaces:**
- Consumes: `CS::setVariable`; `CS::Context::rootCount`, `rootNameAt`, `root`; `chupa::printValue`.
- Produces: полный набор команд оболочки.

- [ ] **Шаг 1: Дописать включение и команды**

В `cli/main.cpp` добавить `#include "data.hpp"`. В анонимное пространство имён:

```cpp
/// Кладёт переменную: та же дверь, которой пользуется хост.
///
/// setVariable принимает только литерал — данные не вычисляются
/// (docs/superpowers/specs/2026-08-11-chupascript-data-design.md §3). В
/// оболочке это ограничение встречается первым, поэтому отказ поясняется.
void runSet(CS::Context &ctx, std::string_view argument) {
    const std::size_t equals = argument.find('=');
    if (equals == std::string_view::npos) {
        std::cout << "error: usage is :set <name> = <literal>\n";
        return;
    }
    const std::string_view name = trim(argument.substr(0, equals));
    const std::string_view text = trim(argument.substr(equals + 1));
    if (name.empty() || text.empty()) {
        std::cout << "error: usage is :set <name> = <literal>\n";
        return;
    }

    CS::Diagnostic diag;
    if (CS::setVariable(ctx, name, text, diag)) { return; }

    std::cout << "error: " << diag.message << "\n";
    if (diag.code == CS::ErrorCode::Data) {
        std::cout << "note: data is set from a literal, not an expression\n";
    }
}

/// Печатает состав контекста: имя и значение.
void runVars(const CS::Context &ctx) {
    const std::uint32_t count = ctx.rootCount();
    if (count == 0) {
        std::cout << "the context is empty\n";
        return;
    }
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::string_view name = ctx.rootNameAt(i);
        std::cout << name << " = "
                  << chupa::printValue(ctx, ctx.root(name)) << "\n";
    }
}
```

- [ ] **Шаг 2: Подключить в разбор команд**

Ветку разбора команд в `handleLine` заменить на:

```cpp
    if (text[0] == ':') {
        const std::string_view body = trim(text.substr(1));
        const std::size_t space = body.find_first_of(" \t");
        const std::string_view name =
            space == std::string_view::npos ? body : body.substr(0, space);
        const std::string_view argument =
            space == std::string_view::npos ? std::string_view()
                                            : trim(body.substr(space));

        if (name == "quit") { return false; }
        if (name == "help") {
            printHelp(std::cout);
            return true;
        }
        if (name == "vars") {
            runVars(ctx);
            return true;
        }
        if (name == "set") {
            runSet(ctx, argument);
            return true;
        }
        if (name == "reset") {
            // Контекст копит мусор — освобождения по одному нет
            // (docs/backlog.md B1). :reset единственный способ начать чисто,
            // не выходя из оболочки.
            return true;   // сам сброс делает вызывающий, см. шаг 3
        }
        std::cout << "error: unknown command\n";
        printHelp(std::cout);
        return true;
    }
```

- [ ] **Шаг 3: Сделать `:reset` пересозданием контекста**

`handleLine` получает `CS::Context &` и пересоздать его не может: копирование и
присваивание у `Context` удалены. Поэтому сброс делает цикл.

Измени возвращаемое значение `handleLine` на перечисление:

```cpp
/// Что делать после строки.
enum class After { Continue, Reset, Quit };
```

`handleLine` возвращает `After`; ветка `quit` даёт `After::Quit`, ветка `reset`
даёт `After::Reset`, всё прочее — `After::Continue`. В `runRepl`:

```cpp
        const After after = handleLine(*ctx, line);
        if (after == After::Quit) { break; }
        if (after == After::Reset) {
            ctx.emplace();
            std::cout << "the context is empty\n";
        }
```

`ctx.emplace()` уничтожает прежний контекст и строит новый на его месте — именно
поэтому он и держится в `std::optional`.

- [ ] **Шаг 4: Собрать и проверить прогоном**

```bash
cmake --build build -j
printf ":set user = {'name': 'Вася'}\nexpr: user.name\n:quit\n" | ./build/cli/chupa -repl
printf ":set items = [1, 2]\nscript: push(items, 3);\nexpr: count(items)\n:quit\n" | ./build/cli/chupa -repl
printf ":vars\n:set a = 1\n:vars\n:quit\n" | ./build/cli/chupa -repl
printf ":set n = 1 + 1\n:quit\n" | ./build/cli/chupa -repl
printf ":set = 1\n:set a\n:quit\n" | ./build/cli/chupa -repl
printf ":set if = 1\n:quit\n" | ./build/cli/chupa -repl
printf ":set a = 1\n:reset\n:vars\n:quit\n" | ./build/cli/chupa -repl
```

Ожидаемо: переменная кладётся и читается; скрипт меняет массив и `count` это
видит; `:vars` на пустом контексте говорит, что он пуст, а после `:set` печатает
имя и значение; `:set n = 1 + 1` отказывает и **поясняет про литерал**; кривые
формы команды дают подсказку; `:set if = 1` отказывает, потому что имя
зарезервировано; после `:reset` контекст пуст.

Приведи вывод всех прогонов в отчёте.

- [ ] **Шаг 5: Прогнать весь набор в трёх сборках**

```bash
ctest --test-dir build --output-on-failure
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
cmake --build build-werror -j && ctest --test-dir build-werror --output-on-failure
```

Expected: 501 из 501 в каждой, ни одного предупреждения, ни одного отчёта
санитайзеров.

**Отдельно прогони оболочку под ASan**, потому что печатник рекурсивен и работает
со срезами чужих пулов:

```bash
printf ":set o = {'a': [1, {'b': 'привет'}]}\nexpr: o\n:vars\n:quit\n" | ./build-asan/cli/chupa -repl
```

Ожидаемо: значение печатается, санитайзер молчит.

- [ ] **Шаг 6: Коммит**

```bash
git add cli/main.cpp
git commit -m "Add :set, :vars and :reset"
```

---

## Задача 6: Документы

**Files:**
- Modify: `docs/backlog.md`
- Create: `cli/README.md`

**Interfaces:**
- Consumes: решения задач 1–5.
- Produces: описание оболочки и отметки в бэклоге.

- [ ] **Шаг 1: Написать `cli/README.md`**

Короткое описание: зачем оболочка, как запускается, полный список команд, два
режима с примерами, и **два ограничения, о которые спотыкаются первыми**: данные
ставятся только литералом (`:set n = 1 + 1` не работает), и контекст копит
память, поэтому в долгой сессии есть `:reset`.

Пиши по-английски: файл описывает инструмент, весь вывод которого английский.

- [ ] **Шаг 2: Отметить в `docs/backlog.md`**

Пункт **B30** получает строку о том, что оболочка делает ограничение «данные
ставятся литералом» ощутимым первым: в `:set` оно встречается раньше, чем
где-либо ещё.

Пункт **B1** получает строку о том, что у оболочки накопление мусора обходится
командой `:reset`, но не лечится.

Формулируй как свойство, а не как историю правок.

- [ ] **Шаг 3: Проверить**

```bash
grep -c "^### B" docs/backlog.md
ctest --test-dir build --output-on-failure
```

Expected: 33 — число пунктов не меняется; 501 из 501.

- [ ] **Шаг 4: Коммит**

```bash
git add cli/README.md docs/backlog.md
git commit -m "Describe the shell and its two rough edges"
```

---

## Итог

| | |
|---|---|
| Задач | 6 |
| Новых файлов | 7 |
| Тестов добавлено | 18 |
| Тестов всего | 501 |
| Строк изменено в `core/` | 0 |

Работа закончена, когда: `chupa -repl` запускается; `expr:` и `script:` работают;
стрелка встаёт под ошибкой на кириллице; `:set`, `:vars`, `:reset`, `:help` и
`:quit` делают заявленное; печатник не зацикливается на самоссылке и печатает
разделяемый агрегат целиком; `ctest` даёт 501 из 501 в обычной сборке, под
ASan+UBSan и с `-Werror`.
