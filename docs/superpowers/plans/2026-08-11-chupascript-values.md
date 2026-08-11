# ChupaScript: слой значений и хранилища — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Построить слой, который хранит значения ChupaScript: `Value` в 16 байт, агрегаты с наблюдаемой ссылочностью и фасад `Context`, через который к ним обращаются.

**Architecture:** Всё хранилище адресуется 32-битными индексами в пять пулов внутри `Context`; указателей внутрь хранилища значения не содержат, поэтому пулы вправе переезжать при росте. Агрегат разделён на заголовок и данные: заголовок стоит на месте, данные переезжают — так `push` не расщепляет алиасы. Раскладка повторяет `core/src/ast.hpp`: вектор записей плюс боковой пул диапазонов.

**Tech Stack:** C++17, gtest, Google Benchmark, CMake. У самой библиотеки зависимостей нет.

**Спека:** `docs/superpowers/specs/2026-08-11-chupascript-values-design.md` — нормативна для этого плана.
**Семантика языка:** `docs/semantics.md` §2 (модель значений), §6.1 и §6.2 (чтение за границей), §7.2 (запись).

## Global Constraints

- **C++17.** Стандарт задан в корневом `CMakeLists.txt`, повышать нельзя.
- **У библиотеки нет зависимостей.** gtest и Google Benchmark подтягиваются только для тестов и бенчмарков.
- **Комментарии и документация — по-русски.** Так написаны `ast.hpp`, `parser.cpp` и все документы проекта.
- **`sizeof(Value) == 16`,** проверяется `static_assert` в заголовке и тестом.
- **`Value` тривиально копируем** — диапазоны в пулах копируются целиком; проверяется `static_assert`.
- **Никаких указателей внутрь хранилища в значениях.** Только `std::uint32_t` индексы.
- **`detail::ArrayRep`, `detail::ObjectRep`, `detail::Entry` определены только в `core/src/context.cpp`.** В заголовке — объявления.
- **Деструктор `Context` объявляется в `context.hpp` и определяется в `context.cpp`.** Иначе `std::vector` неполного типа инстанцируется раньше, чем тип известен.
- **`Value` передаётся и возвращается копией,** а не ссылкой: ссылка внутрь `pool_` повиснет при росте.
- **Чтение за границей даёт `Value::null()`; запись за границу массива даёт `false`.** Это `semantics.md` §6.1, §6.2, §7.2.
- **Несовпадение вида — `assert`, а не ветка.** Вид обязан проверить вызывающий: без этого он не выдаст диагностику §6.4.
- **Поштучного освобождения нет.** Мусор копится до смерти контекста.
- **Сборка тестов:** `cmake -B build && cmake --build build -j`, прогон `ctest --test-dir build --output-on-failure`.
- **Сборка бенчмарков:** только Release — `cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON`.
- **Коммитить явными путями.** `git add -A` не использовать.

---

## Структура файлов

| Файл | Ответственность |
|---|---|
| `core/src/value.hpp` | `Value`: раскладка, скалярные фабрики и аксессоры, идентичность. Заголовочный, без `.cpp` |
| `core/src/context.hpp` | Объявление фасада `Context` и приватных пулов |
| `core/src/context.cpp` | `ArrayRep`, `ObjectRep`, `Entry` и все тела методов |
| `core/tests/value_test.cpp` | Раскладка и скаляры |
| `core/tests/context_test.cpp` | Строки, массивы, объекты, мутации, идентичность |
| `benchmarks/store_benchmark.cpp` | База производительности хранилища |

Существующий `core/src/value.hpp` — черновик прошлой эпохи: в объединении заглушки `std::size_t *`, поля длины нет, поэтому строку он не представляет вовсе. Задача 1 переписывает его целиком.

---

## Задача 1: `Value`

**Files:**
- Modify: `core/src/value.hpp` (переписывается целиком)
- Create: `core/tests/value_test.cpp`
- Modify: `core/tests/CMakeLists.txt:1-6`

**Interfaces:**
- Consumes: ничего.
- Produces: `CS::Value` с публичными `Value::null()`, `Value::boolean(bool)`, `Value::number(double)`, `kind()`, `booleanValue()`, `numberValue()`, `sameAggregate(Value)`; приватными `Value::string(std::uint32_t offset, std::uint32_t length)`, `Value::array(std::uint32_t index)`, `Value::object(std::uint32_t index)`, `index()`, `length()` — доступны только `class Context`. Перечисление `CS::Value::Kind` со значениями `Null`, `Boolean`, `Number`, `String`, `Object`, `Array`.

- [ ] **Шаг 1: Написать тест раскладки и скаляров**

Создать `core/tests/value_test.cpp`:

```cpp
#include "value.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <type_traits>

namespace {

using CS::Value;

TEST(ValueLayout, SizeIsSixteenBytes) {
    EXPECT_EQ(sizeof(Value), 16u);
}

TEST(ValueLayout, IsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<Value>);
}

TEST(ValueScalars, NullHasNullKind) {
    EXPECT_EQ(Value::null().kind(), Value::Kind::Null);
}

TEST(ValueScalars, BooleanRoundTrips) {
    EXPECT_EQ(Value::boolean(true).kind(), Value::Kind::Boolean);
    EXPECT_TRUE(Value::boolean(true).booleanValue());
    EXPECT_FALSE(Value::boolean(false).booleanValue());
}

TEST(ValueScalars, NumberRoundTrips) {
    EXPECT_EQ(Value::number(1.5).kind(), Value::Kind::Number);
    EXPECT_EQ(Value::number(1.5).numberValue(), 1.5);
    EXPECT_EQ(Value::number(-0.0).numberValue(), -0.0);
}

TEST(ValueScalars, NumberKeepsSpecialValues) {
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_EQ(Value::number(inf).numberValue(), inf);
    EXPECT_TRUE(std::isnan(Value::number(std::numeric_limits<double>::quiet_NaN()).numberValue()));
}

TEST(ValueIdentity, ScalarsAreNeverSameAggregate) {
    EXPECT_FALSE(Value::null().sameAggregate(Value::null()));
    EXPECT_FALSE(Value::number(1.0).sameAggregate(Value::number(1.0)));
    EXPECT_FALSE(Value::boolean(true).sameAggregate(Value::boolean(true)));
}

TEST(ValueIdentity, DifferentKindsAreNotSame) {
    EXPECT_FALSE(Value::null().sameAggregate(Value::number(0.0)));
}

}  // namespace
```

- [ ] **Шаг 2: Зарегистрировать тест в сборке**

`core/tests/CMakeLists.txt`, список исходников — добавить `value_test.cpp` после `smoke_test.cpp`:

```cmake
add_executable(chupascript_tests
    ast_test.cpp
    lexer_test.cpp
    parser_test.cpp
    smoke_test.cpp
    value_test.cpp
)
```

- [ ] **Шаг 3: Убедиться, что не собирается**

Run: `cmake -B build && cmake --build build -j`
Expected: ошибка компиляции — у `Value` нет `booleanValue`, `numberValue`, `sameAggregate`, а в объединении лежат заглушки.

- [ ] **Шаг 4: Переписать `core/src/value.hpp`**

Заменить содержимое файла целиком:

```cpp
#pragma once
#include <cassert>
#include <cstdint>
#include <type_traits>

namespace CS {

class Context;

/// Значение ChupaScript. Шесть типов из docs/semantics.md §2.1.
///
/// Строки и агрегаты адресуются индексами в пулы Context: значение без своего
/// контекста бессмысленно, разрешить индекс может только тот контекст, который
/// его выдал. Поэтому создать строку, массив или объект способен лишь Context —
/// соответствующие фабрики закрыты.
///
/// Раскладка и обоснование:
/// docs/superpowers/specs/2026-08-11-chupascript-values-design.md §4.
class Value {
   public:
    /// Вид значения. Закрытый список из docs/semantics.md §2.1.
    enum class Kind : std::uint8_t { Null, Boolean, Number, String, Object, Array };

    static Value null() noexcept {
        Value v;
        v.kind_ = Kind::Null;
        return v;
    }

    static Value boolean(bool value) noexcept {
        Value v;
        v.kind_ = Kind::Boolean;
        v.boolean_ = value;
        return v;
    }

    static Value number(double value) noexcept {
        Value v;
        v.kind_ = Kind::Number;
        v.number_ = value;
        return v;
    }

    Kind kind() const noexcept { return kind_; }

    /// Предусловие: kind() == Kind::Boolean.
    bool booleanValue() const noexcept {
        assert(kind_ == Kind::Boolean);
        return boolean_;
    }

    /// Предусловие: kind() == Kind::Number.
    double numberValue() const noexcept {
        assert(kind_ == Kind::Number);
        return number_;
    }

    /// Один ли это агрегат — сравнивает вид и индекс заголовка.
    ///
    /// У скаляров идентичности нет (docs/semantics.md §5.4), для них всегда
    /// false, в том числе при сравнении значения с самим собой.
    bool sameAggregate(Value other) const noexcept {
        if (kind_ != other.kind_) { return false; }
        if (kind_ != Kind::Array && kind_ != Kind::Object) { return false; }
        return index_ == other.index_;
    }

   private:
    friend class Context;

    /// Индексы полны как тип, поэтому без закрытых фабрик любой код собрал бы
    /// значение-агрегат с произвольным номером заголовка. Закрываем доступом.
    static Value string(std::uint32_t offset, std::uint32_t length) noexcept {
        Value v;
        v.kind_ = Kind::String;
        v.length_ = length;
        v.index_ = offset;
        return v;
    }

    static Value array(std::uint32_t index) noexcept {
        Value v;
        v.kind_ = Kind::Array;
        v.index_ = index;
        return v;
    }

    static Value object(std::uint32_t index) noexcept {
        Value v;
        v.kind_ = Kind::Object;
        v.index_ = index;
        return v;
    }

    std::uint32_t index() const noexcept { return index_; }
    std::uint32_t length() const noexcept { return length_; }

    Value() noexcept : kind_(Kind::Null), length_(0), number_(0.0) {}

    Kind kind_;             // смещение 0
    std::uint32_t length_;  // смещение 4 — длина строки в байтах
    // TODO(B2): восемь байт вместо шестнадцати достижимы только через
    // NaN-boxing: double в объединении задаёт и размер, и выравнивание.
    union {  // смещение 8
        bool boolean_;
        double number_;
        std::uint32_t index_;
    };
};

static_assert(sizeof(Value) == 16, "Value должен оставаться в 16 байтах");
static_assert(std::is_trivially_copyable_v<Value>,
              "диапазоны значений копируются в пулах целиком");

}  // namespace CS
```

Объявления `strictEqual` и `looseEqual` из файла уходят: определений у них не было никогда, а равенство требует приведений из `docs/semantics.md` §4 и живёт в слое вычислителя (`docs/backlog.md` B15).

- [ ] **Шаг 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "ValueLayout|ValueScalars|ValueIdentity"`
Expected: 8 тестов PASS.

- [ ] **Шаг 6: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 155 тестов PASS (147 было + 8).

- [ ] **Шаг 7: Коммит**

```bash
git add core/src/value.hpp core/tests/value_test.cpp core/tests/CMakeLists.txt
git commit -m "Rewrite Value as a sixteen-byte tagged union over pool indices"
```

---

## Задача 2: `Context` — пулы и строки

**Files:**
- Create: `core/src/context.hpp`, `core/src/context.cpp`
- Create: `core/tests/context_test.cpp`
- Modify: `core/CMakeLists.txt:1-6`, `core/tests/CMakeLists.txt:1-7`

**Interfaces:**
- Consumes: `CS::Value` из задачи 1 — приватные `Value::string(offset, length)`, `index()`, `length()` доступны, потому что `Context` объявлен другом.
- Produces: `CS::Context` с `makeString(std::string_view)`, `string(Value)`, `bytesUsed()`, `bytesReserved()`; приватные `appendText(std::string_view)` и `textAt(std::uint32_t, std::uint32_t)`, которыми пользуются задачи 5 и 6.

- [ ] **Шаг 1: Написать тест строк**

Создать `core/tests/context_test.cpp`:

```cpp
#include "context.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using CS::Context;
using CS::Value;

TEST(ContextString, RoundTripsBytes) {
    Context ctx;
    const Value v = ctx.makeString("привет");
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_EQ(ctx.string(v), "привет");
}

TEST(ContextString, EmptyStringIsEmptyView) {
    Context ctx;
    const Value v = ctx.makeString("");
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_TRUE(ctx.string(v).empty());
}

TEST(ContextString, LengthIsCountedInBytes) {
    Context ctx;
    // Шесть кириллических букв — двенадцать байт (semantics.md §2.1).
    EXPECT_EQ(ctx.string(ctx.makeString("привет")).size(), 12u);
}

TEST(ContextString, KeepsEmbeddedNulByte) {
    Context ctx;
    const std::string bytes("a\0b", 3);
    const Value v = ctx.makeString(bytes);
    EXPECT_EQ(ctx.string(v).size(), 3u);
    EXPECT_EQ(ctx.string(v)[1], '\0');
}

TEST(ContextString, EqualStringsAreStoredTwice) {
    Context ctx;
    const Value a = ctx.makeString("одинаково");
    const Value b = ctx.makeString("одинаково");
    EXPECT_EQ(ctx.string(a), ctx.string(b));
    // Дедупликации нет: второй экземпляр занял место (спека §6).
    EXPECT_NE(ctx.string(a).data(), ctx.string(b).data());
}

TEST(ContextString, AcceptsSliceOfItsOwnTextPool) {
    Context ctx;
    // Копирование строки, которая уже лежит в пуле: источник может переехать
    // прямо во время копирования, и наивный insert здесь был бы UB.
    Value seed = ctx.makeString("исходная строка");
    for (int i = 0; i < 64; ++i) {
        seed = ctx.makeString(ctx.string(seed));
    }
    EXPECT_EQ(ctx.string(seed), "исходная строка");
}

TEST(ContextMetrics, EmptyContextUsesNothing) {
    Context ctx;
    EXPECT_EQ(ctx.bytesUsed(), 0u);
}

TEST(ContextMetrics, StringAddsItsBytes) {
    Context ctx;
    const std::size_t before = ctx.bytesUsed();
    ctx.makeString("12345");
    EXPECT_EQ(ctx.bytesUsed(), before + 5u);
}

}  // namespace
```

- [ ] **Шаг 2: Зарегистрировать в сборке**

`core/CMakeLists.txt`, список исходников:

```cmake
add_library(chupascript STATIC
    src/ast.cpp
    src/context.cpp
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
    lexer_test.cpp
    parser_test.cpp
    smoke_test.cpp
    value_test.cpp
)
```

- [ ] **Шаг 3: Убедиться, что не собирается**

Run: `cmake -B build && cmake --build build -j`
Expected: ошибка — `context.hpp` не существует.

- [ ] **Шаг 4: Написать `core/src/context.hpp`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

#include "value.hpp"

namespace CS {

namespace detail {
/// Заголовки агрегатов и запись объекта. Определены только в context.cpp:
/// снаружи это неполные типы, и раскладку хранилища не видит никто.
struct ArrayRep;
struct ObjectRep;
struct Entry;
}  // namespace detail

/// Хранилище значений ChupaScript.
///
/// Владеет всем, что породил; поштучного освобождения нет, вся память уходит
/// разом в деструкторе. Значения адресуют пулы индексами, поэтому пулы вправе
/// переезжать при росте — а вот указатель на элемент пула переживает лишь до
/// ближайшей мутации, и наружу такие указатели этот класс не отдаёт.
///
/// Обоснование раскладки:
/// docs/superpowers/specs/2026-08-11-chupascript-values-design.md §5–§7.
class Context {
   public:
    Context();
    /// Определён в context.cpp: в заголовке типы пулов ещё неполны.
    ~Context();

    Context(const Context &) = delete;
    Context &operator=(const Context &) = delete;

    // ─── создание ───

    /// Копирует байты в пул текста. Допускает срез собственного пула.
    Value makeString(std::string_view bytes);

    // ─── чтение ───

    /// Предусловие: v.kind() == Value::Kind::String.
    std::string_view string(Value v) const noexcept;

    // ─── метрики ───

    /// Сколько байт занято выданными данными.
    std::size_t bytesUsed() const noexcept;
    /// Сколько байт занято у аллокатора, включая запас пулов.
    std::size_t bytesReserved() const noexcept;

   private:
    std::uint32_t appendText(std::string_view bytes);
    std::string_view textAt(std::uint32_t offset, std::uint32_t length) const noexcept;

    std::vector<Value> pool_;                   // элементы массивов, диапазонами
    std::vector<detail::ArrayRep> arrays_;      // заголовки массивов
    std::vector<detail::ObjectRep> objects_;    // заголовки объектов
    std::vector<detail::Entry> entries_;        // пары объектов, диапазонами
    std::vector<char> text_;                    // байты строк и ключей
};

}  // namespace CS
```

- [ ] **Шаг 5: Написать `core/src/context.cpp`**

```cpp
#include "context.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>

namespace CS {

namespace detail {

struct ArrayRep {
    std::uint32_t start;     // индекс первого элемента в pool_
    std::uint32_t count;
    std::uint32_t capacity;
};

struct Entry {
    std::uint32_t keyOffset;  // индекс первого байта ключа в text_
    std::uint32_t keyLength;
    Value value;
};

struct ObjectRep {
    std::uint32_t start;     // индекс первой пары в entries_
    std::uint32_t count;
    std::uint32_t capacity;
};

}  // namespace detail

Context::Context() = default;
Context::~Context() = default;

std::uint32_t Context::appendText(std::string_view bytes) {
    const std::uint32_t offset = static_cast<std::uint32_t>(text_.size());
    assert(text_.size() + bytes.size() <= 0xffffffffu && "пул текста перерос uint32");

    // bytes вправе указывать внутрь text_ — так выглядит objectSet(o,
    // ctx.string(k), v). Рост пула переселит буфер, и указатель источника
    // повиснет прямо посреди копирования, поэтому положение источника
    // запоминается смещением, а не адресом.
    const char *first = text_.data();
    const std::uintptr_t base = reinterpret_cast<std::uintptr_t>(first);
    const std::uintptr_t from = reinterpret_cast<std::uintptr_t>(bytes.data());
    // Граница строгая: непустой срез пула начинается строго внутри него, а пустой
    // источник уходит раньше, чем понадобится адрес. Включающая граница приняла бы
    // за алиас чужой буфер, оказавшийся вплотную за пулом, и скопировала бы нули.
    const bool aliases = first != nullptr && from >= base && from < base + text_.size();
    const std::size_t inner = aliases ? static_cast<std::size_t>(from - base) : 0;

    text_.resize(text_.size() + bytes.size());
    if (bytes.empty()) { return offset; }

    const char *source = aliases ? text_.data() + inner : bytes.data();
    std::memcpy(text_.data() + offset, source, bytes.size());
    return offset;
}

std::string_view Context::textAt(std::uint32_t offset,
                                 std::uint32_t length) const noexcept {
    if (length == 0) { return {}; }
    return std::string_view(text_.data() + offset, length);
}

Value Context::makeString(std::string_view bytes) {
    const std::uint32_t offset = appendText(bytes);
    return Value::string(offset, static_cast<std::uint32_t>(bytes.size()));
}

std::string_view Context::string(Value v) const noexcept {
    assert(v.kind() == Value::Kind::String);
    return textAt(v.index(), v.length());
}

std::size_t Context::bytesUsed() const noexcept {
    return pool_.size() * sizeof(Value) +
           arrays_.size() * sizeof(detail::ArrayRep) +
           objects_.size() * sizeof(detail::ObjectRep) +
           entries_.size() * sizeof(detail::Entry) + text_.size();
}

std::size_t Context::bytesReserved() const noexcept {
    return pool_.capacity() * sizeof(Value) +
           arrays_.capacity() * sizeof(detail::ArrayRep) +
           objects_.capacity() * sizeof(detail::ObjectRep) +
           entries_.capacity() * sizeof(detail::Entry) + text_.capacity();
}

}  // namespace CS
```

- [ ] **Шаг 6: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "ContextString|ContextMetrics"`
Expected: 8 тестов PASS.

- [ ] **Шаг 7: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 163 теста PASS.

- [ ] **Шаг 8: Коммит**

```bash
git add core/src/context.hpp core/src/context.cpp core/tests/context_test.cpp \
        core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "Add the Context storage facade with a text pool"
```

---

## Задача 3: Массивы — создание и чтение

**Files:**
- Modify: `core/src/context.hpp` (методы массива), `core/src/context.cpp`
- Modify: `core/tests/context_test.cpp` (дописать группу тестов)

**Interfaces:**
- Consumes: `Context` и `Value` из задач 1–2.
- Produces: `makeArray(std::uint32_t capacity = 0)`, `arrayCount(Value)`, `arrayAt(Value, std::uint32_t)`, `arraySet(Value, std::uint32_t, Value)`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/context_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(ContextArray, EmptyArrayHasNoElements) {
    Context ctx;
    const Value a = ctx.makeArray();
    EXPECT_EQ(a.kind(), Value::Kind::Array);
    EXPECT_EQ(ctx.arrayCount(a), 0u);
}

TEST(ContextArray, CapacityDoesNotCreateElements) {
    Context ctx;
    EXPECT_EQ(ctx.arrayCount(ctx.makeArray(16)), 0u);
}

TEST(ContextArray, ReadBeyondEndGivesNull) {
    Context ctx;
    const Value a = ctx.makeArray();
    // semantics.md §6.1: чтение за границей — штатная ситуация.
    EXPECT_EQ(ctx.arrayAt(a, 0).kind(), Value::Kind::Null);
    EXPECT_EQ(ctx.arrayAt(a, 1000).kind(), Value::Kind::Null);
}

TEST(ContextArray, WriteBeyondEndIsRefused) {
    Context ctx;
    const Value a = ctx.makeArray(8);
    // semantics.md §7.2: запись за границу — ошибка, ёмкость её не оправдывает.
    EXPECT_FALSE(ctx.arraySet(a, 0, Value::number(1.0)));
}

TEST(ContextArray, TwoEmptyArraysAreDistinct) {
    Context ctx;
    EXPECT_FALSE(ctx.makeArray().sameAggregate(ctx.makeArray()));
}

TEST(ContextArray, CopyOfValueIsTheSameArray) {
    Context ctx;
    const Value a = ctx.makeArray();
    const Value b = a;
    EXPECT_TRUE(a.sameAggregate(b));
}
```

- [ ] **Шаг 2: Убедиться, что не собирается**

Run: `cmake --build build -j`
Expected: ошибка — у `Context` нет `makeArray`, `arrayCount`, `arrayAt`, `arraySet`.

- [ ] **Шаг 3: Объявить методы в `core/src/context.hpp`**

В секцию «создание» после `makeString`:

```cpp
    /// Создаёт пустой массив. capacity — сколько элементов выделить заранее;
    /// на длину не влияет, элементы добавляет только arrayPush.
    Value makeArray(std::uint32_t capacity = 0);
```

В секцию «чтение» после `string`:

```cpp
    /// Предусловие: a.kind() == Value::Kind::Array.
    std::uint32_t arrayCount(Value a) const noexcept;

    /// Элемент либо null за границей (docs/semantics.md §6.1).
    /// Предусловие: a.kind() == Value::Kind::Array.
    Value arrayAt(Value a, std::uint32_t index) const noexcept;
```

Завести секцию «изменение» перед секцией «метрики»:

```cpp
    // ─── изменение ───

    /// Заменяет элемент. false за границей — по docs/semantics.md §7.2 это
    /// ошибка, диагностику формулирует вызывающий.
    /// Предусловие: a.kind() == Value::Kind::Array.
    bool arraySet(Value a, std::uint32_t index, Value v) noexcept;
```

В приватную секцию, рядом с `appendText`:

```cpp
    void growArray(detail::ArrayRep &rep, std::uint32_t needed);
```

- [ ] **Шаг 4: Написать тела в `core/src/context.cpp`**

После `makeString` и `string`:

```cpp
void Context::growArray(detail::ArrayRep &rep, std::uint32_t needed) {
    if (needed <= rep.capacity) { return; }

    std::uint32_t capacity = rep.capacity == 0 ? 4 : rep.capacity;
    while (capacity < needed) {
        assert(capacity <= 0x7fffffffu && "массив перерос uint32");
        capacity *= 2;
    }

    // Новый диапазон дописывается в хвост, старый бросается мусором:
    // освобождения по одному нет (спека §5). Индекс заголовка при этом не
    // меняется — на нём стоит идентичность и все алиасы.
    const std::uint32_t start = static_cast<std::uint32_t>(pool_.size());
    pool_.insert(pool_.end(), capacity, Value::null());
    for (std::uint32_t i = 0; i < rep.count; ++i) {
        pool_[start + i] = pool_[rep.start + i];
    }

    rep.start = start;
    rep.capacity = capacity;
}

Value Context::makeArray(std::uint32_t capacity) {
    const std::uint32_t index = static_cast<std::uint32_t>(arrays_.size());
    arrays_.push_back(detail::ArrayRep{0, 0, 0});
    if (capacity > 0) { growArray(arrays_[index], capacity); }
    return Value::array(index);
}

std::uint32_t Context::arrayCount(Value a) const noexcept {
    assert(a.kind() == Value::Kind::Array);
    return arrays_[a.index()].count;
}

Value Context::arrayAt(Value a, std::uint32_t index) const noexcept {
    assert(a.kind() == Value::Kind::Array);
    const detail::ArrayRep &rep = arrays_[a.index()];
    if (index >= rep.count) { return Value::null(); }
    return pool_[rep.start + index];
}

bool Context::arraySet(Value a, std::uint32_t index, Value v) noexcept {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayRep &rep = arrays_[a.index()];
    if (index >= rep.count) { return false; }
    pool_[rep.start + index] = v;
    return true;
}
```

- [ ] **Шаг 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R ContextArray`
Expected: 6 тестов PASS.

- [ ] **Шаг 6: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 169 тестов PASS.

- [ ] **Шаг 7: Коммит**

```bash
git add core/src/context.hpp core/src/context.cpp core/tests/context_test.cpp
git commit -m "Add array creation and reading to the Context"
```

---

## Задача 4: Массивы — рост, мутации и алиасы

**Files:**
- Modify: `core/src/context.hpp`, `core/src/context.cpp`
- Modify: `core/tests/context_test.cpp`

**Interfaces:**
- Consumes: `makeArray`, `arrayCount`, `arrayAt`, `arraySet`, `growArray` из задачи 3.
- Produces: `arrayPush(Value, Value)`, `arrayPop(Value, Value *)`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/context_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(ContextArrayMutation, PushAppendsInOrder) {
    Context ctx;
    const Value a = ctx.makeArray();
    ctx.arrayPush(a, Value::number(1.0));
    ctx.arrayPush(a, Value::number(2.0));
    ASSERT_EQ(ctx.arrayCount(a), 2u);
    EXPECT_EQ(ctx.arrayAt(a, 0).numberValue(), 1.0);
    EXPECT_EQ(ctx.arrayAt(a, 1).numberValue(), 2.0);
}

TEST(ContextArrayMutation, SetReplacesExistingElement) {
    Context ctx;
    const Value a = ctx.makeArray();
    ctx.arrayPush(a, Value::number(1.0));
    EXPECT_TRUE(ctx.arraySet(a, 0, Value::boolean(true)));
    EXPECT_TRUE(ctx.arrayAt(a, 0).booleanValue());
}

TEST(ContextArrayMutation, PopReturnsLastAndShrinks) {
    Context ctx;
    const Value a = ctx.makeArray();
    ctx.arrayPush(a, Value::number(1.0));
    ctx.arrayPush(a, Value::number(2.0));

    Value taken = Value::null();
    ASSERT_TRUE(ctx.arrayPop(a, &taken));
    EXPECT_EQ(taken.numberValue(), 2.0);
    EXPECT_EQ(ctx.arrayCount(a), 1u);
}

TEST(ContextArrayMutation, PopOnEmptyIsRefused) {
    Context ctx;
    Value taken = Value::number(7.0);
    EXPECT_FALSE(ctx.arrayPop(ctx.makeArray(), &taken));
    // Отказ не трогает выходной параметр.
    EXPECT_EQ(taken.numberValue(), 7.0);
}

TEST(ContextArrayMutation, AliasSurvivesGrowth) {
    Context ctx;
    const Value a = ctx.makeArray();
    const Value alias = a;

    // Рост через все удвоения: 4, 8, 16, 32 — данные переезжают четырежды.
    for (int i = 0; i < 40; ++i) {
        ctx.arrayPush(a, Value::number(static_cast<double>(i)));
    }

    // semantics.md §2.3: изменение через одно имя видно через второе.
    EXPECT_EQ(ctx.arrayCount(alias), 40u);
    EXPECT_EQ(ctx.arrayAt(alias, 0).numberValue(), 0.0);
    EXPECT_EQ(ctx.arrayAt(alias, 39).numberValue(), 39.0);
    EXPECT_TRUE(a.sameAggregate(alias));
}

TEST(ContextArrayMutation, WriteThroughAliasIsSeenByOriginal) {
    Context ctx;
    const Value a = ctx.makeArray();
    ctx.arrayPush(a, Value::number(1.0));
    const Value alias = a;

    ASSERT_TRUE(ctx.arraySet(alias, 0, Value::number(99.0)));
    EXPECT_EQ(ctx.arrayAt(a, 0).numberValue(), 99.0);
}

TEST(ContextArrayMutation, NestedArrayKeepsIdentity) {
    Context ctx;
    const Value outer = ctx.makeArray();
    const Value inner = ctx.makeArray();
    ctx.arrayPush(outer, inner);
    ctx.arrayPush(inner, Value::number(1.0));

    const Value fetched = ctx.arrayAt(outer, 0);
    EXPECT_TRUE(fetched.sameAggregate(inner));
    EXPECT_EQ(ctx.arrayCount(fetched), 1u);
}

TEST(ContextArrayMutation, ArrayMayContainItself) {
    Context ctx;
    const Value a = ctx.makeArray();
    ctx.arrayPush(a, a);
    // semantics.md §2.3: цикл допустим, рекурсивного обхода в слое нет.
    EXPECT_TRUE(ctx.arrayAt(a, 0).sameAggregate(a));
}

TEST(ContextArrayMutation, PreallocatedCapacityGrowsNothing) {
    Context ctx;
    const Value a = ctx.makeArray(64);
    const std::size_t afterReserve = ctx.bytesUsed();
    for (int i = 0; i < 64; ++i) {
        ctx.arrayPush(a, Value::number(static_cast<double>(i)));
    }
    // Размер известен заранее — переездов и мусора нет (спека §5).
    EXPECT_EQ(ctx.bytesUsed(), afterReserve);
}

TEST(ContextArrayMutation, GrowthLeavesGarbageBehind) {
    Context ctx;
    const Value a = ctx.makeArray();
    // Заголовок массива уже учтён; дальше растёт только пул элементов, поэтому
    // сравнивается прирост, а не абсолютное число: размер заголовка тесту
    // недоступен — тип неполон вне context.cpp.
    const std::size_t afterHeader = ctx.bytesUsed();
    for (int i = 0; i < 64; ++i) {
        ctx.arrayPush(a, Value::number(static_cast<double>(i)));
    }
    // 4 + 8 + 16 + 32 + 64 = 124 слота выделено под 64 элемента: разница —
    // брошенный мусор, освобождения по одному нет (docs/backlog.md B1).
    EXPECT_EQ(ctx.bytesUsed() - afterHeader, 124u * sizeof(Value));
}
```

- [ ] **Шаг 2: Убедиться, что не собирается**

Run: `cmake --build build -j`
Expected: ошибка — у `Context` нет `arrayPush` и `arrayPop`.

- [ ] **Шаг 3: Объявить методы в `core/src/context.hpp`**

В секцию «изменение», после `arraySet`:

```cpp
    /// Добавляет элемент в конец. Единственный способ расширить массив
    /// (docs/semantics.md §6.1).
    /// Предусловие: a.kind() == Value::Kind::Array.
    void arrayPush(Value a, Value v);

    /// Снимает последний элемент в *out. false на пустом массиве; выходной
    /// параметр при отказе не меняется. out допускает nullptr.
    /// Предусловие: a.kind() == Value::Kind::Array.
    bool arrayPop(Value a, Value *out) noexcept;
```

- [ ] **Шаг 4: Написать тела в `core/src/context.cpp`**

После `arraySet`:

```cpp
void Context::arrayPush(Value a, Value v) {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayRep &rep = arrays_[a.index()];
    // v пришёл копией, поэтому переезд pool_ внутри growArray ему не страшен.
    growArray(rep, rep.count + 1);
    pool_[rep.start + rep.count] = v;
    rep.count += 1;
}

bool Context::arrayPop(Value a, Value *out) noexcept {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayRep &rep = arrays_[a.index()];
    if (rep.count == 0) { return false; }
    rep.count -= 1;
    if (out != nullptr) { *out = pool_[rep.start + rep.count]; }
    return true;
}
```

- [ ] **Шаг 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R ContextArray`
Expected: 16 тестов PASS (6 из задачи 3 и 10 новых).

- [ ] **Шаг 6: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 179 тестов PASS.

- [ ] **Шаг 7: Прогнать под санитайзерами**

```bash
cmake -B build-asan -DCHUPASCRIPT_SANITIZE_ADDRESS=ON -DCHUPASCRIPT_SANITIZE_UNDEFINED=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

Expected: 179 PASS, ни одного отчёта санитайзера. Рост массивов — главное место, где переезд пула может дать чтение освобождённой памяти.

- [ ] **Шаг 8: Коммит**

```bash
git add core/src/context.hpp core/src/context.cpp core/tests/context_test.cpp
git commit -m "Grow arrays without breaking aliases"
```

---

## Задача 5: Объекты — создание, поиск и чтение

**Files:**
- Modify: `core/src/context.hpp`, `core/src/context.cpp`
- Modify: `core/tests/context_test.cpp`

**Interfaces:**
- Consumes: `appendText`, `textAt` из задачи 2.
- Produces: `makeObject(std::uint32_t capacity = 0)`, `objectCount(Value)`, `objectGet(Value, std::string_view)`, `objectHas(Value, std::string_view)`, `objectKeyAt(Value, std::uint32_t)`, `objectValueAt(Value, std::uint32_t)`; приватный `findKey(const detail::ObjectRep &, std::string_view, bool *)`.

`objectSet` реализуется здесь же, вместе с чтением: без него объект нечем наполнить, и любой тест чтения проверял бы только пустой объект. Задача 6 берёт на себя то, что вокруг вставки — порядок, рост, алиасы, — и нового кода не добавляет.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/context_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(ContextObject, EmptyObjectHasNoKeys) {
    Context ctx;
    const Value o = ctx.makeObject();
    EXPECT_EQ(o.kind(), Value::Kind::Object);
    EXPECT_EQ(ctx.objectCount(o), 0u);
}

TEST(ContextObject, MissingKeyReadsAsNull) {
    Context ctx;
    const Value o = ctx.makeObject();
    // semantics.md §6.2: отсутствующий ключ читается как null.
    EXPECT_EQ(ctx.objectGet(o, "нет").kind(), Value::Kind::Null);
    EXPECT_FALSE(ctx.objectHas(o, "нет"));
}

TEST(ContextObject, StoredValueIsFound) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "count", Value::number(3.0));
    EXPECT_TRUE(ctx.objectHas(o, "count"));
    EXPECT_EQ(ctx.objectGet(o, "count").numberValue(), 3.0);
    EXPECT_EQ(ctx.objectCount(o), 1u);
}

TEST(ContextObject, NullValueIsDistinctFromAbsence) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "key", Value::null());
    // semantics.md §6.2: отличить одно от другого можно только через has.
    EXPECT_EQ(ctx.objectGet(o, "key").kind(), Value::Kind::Null);
    EXPECT_TRUE(ctx.objectHas(o, "key"));
}

TEST(ContextObject, FindsKeyAmongMany) {
    Context ctx;
    const Value o = ctx.makeObject();
    const char *keys[] = {"zeta", "alpha", "mu", "beta", "omega", "kappa", "iota"};
    for (int i = 0; i < 7; ++i) {
        ctx.objectSet(o, keys[i], Value::number(static_cast<double>(i)));
    }
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(ctx.objectGet(o, keys[i]).numberValue(), static_cast<double>(i));
    }
    EXPECT_EQ(ctx.objectCount(o), 7u);
}

TEST(ContextObject, PrefixKeyIsNotConfusedWithLongerOne) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "item", Value::number(1.0));
    ctx.objectSet(o, "items", Value::number(2.0));
    EXPECT_EQ(ctx.objectGet(o, "item").numberValue(), 1.0);
    EXPECT_EQ(ctx.objectGet(o, "items").numberValue(), 2.0);
}

TEST(ContextObject, NonAsciiKeyIsFound) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "имя", ctx.makeString("Вася"));
    EXPECT_EQ(ctx.string(ctx.objectGet(o, "имя")), "Вася");
}

TEST(ContextObject, EmptyKeyIsAKeyLikeAnyOther) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "", Value::number(1.0));
    EXPECT_TRUE(ctx.objectHas(o, ""));
    EXPECT_EQ(ctx.objectGet(o, "").numberValue(), 1.0);
}

TEST(ContextObject, EnumerationYieldsEveryKey) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "b", Value::number(2.0));
    ctx.objectSet(o, "a", Value::number(1.0));

    ASSERT_EQ(ctx.objectCount(o), 2u);
    std::string seen;
    for (std::uint32_t i = 0; i < ctx.objectCount(o); ++i) {
        seen += ctx.objectKeyAt(o, i);
        seen += '=';
        seen += std::to_string(static_cast<int>(ctx.objectValueAt(o, i).numberValue()));
        seen += ';';
    }
    // Порядок наружу не обещан (semantics.md §2.1), но хранение отсортировано.
    EXPECT_EQ(seen, "a=1;b=2;");
}

TEST(ContextObject, EnumerationBeyondEndIsEmpty) {
    Context ctx;
    const Value o = ctx.makeObject();
    EXPECT_TRUE(ctx.objectKeyAt(o, 0).empty());
    EXPECT_EQ(ctx.objectValueAt(o, 0).kind(), Value::Kind::Null);
}

TEST(ContextObject, TwoEmptyObjectsAreDistinct) {
    Context ctx;
    EXPECT_FALSE(ctx.makeObject().sameAggregate(ctx.makeObject()));
}
```

- [ ] **Шаг 2: Убедиться, что не собирается**

Run: `cmake --build build -j`
Expected: ошибка — у `Context` нет `makeObject`, `objectCount`, `objectGet`, `objectHas`, `objectKeyAt`, `objectValueAt`, `objectSet`.

- [ ] **Шаг 3: Объявить методы в `core/src/context.hpp`**

В секцию «создание», после `makeArray`:

```cpp
    /// Создаёт пустой объект. capacity — сколько пар выделить заранее.
    Value makeObject(std::uint32_t capacity = 0);
```

В секцию «чтение», после `arrayAt`:

```cpp
    /// Предусловие: o.kind() == Value::Kind::Object.
    std::uint32_t objectCount(Value o) const noexcept;

    /// Значение либо null, если ключа нет (docs/semantics.md §6.2).
    /// Предусловие: o.kind() == Value::Kind::Object.
    Value objectGet(Value o, std::string_view key) const noexcept;

    /// Есть ли ключ. Отличает записанный null от отсутствия — иначе их не
    /// различить (docs/semantics.md §6.2, §8.3).
    /// Предусловие: o.kind() == Value::Kind::Object.
    bool objectHas(Value o, std::string_view key) const noexcept;

    /// Ключ по порядковому номеру либо пустой срез за границей. Порядок
    /// перечисления наружу не обещан (docs/semantics.md §2.1).
    /// Предусловие: o.kind() == Value::Kind::Object.
    std::string_view objectKeyAt(Value o, std::uint32_t i) const noexcept;

    /// Значение по порядковому номеру либо null за границей.
    /// Предусловие: o.kind() == Value::Kind::Object.
    Value objectValueAt(Value o, std::uint32_t i) const noexcept;
```

В секцию «изменение», после `arrayPop`:

```cpp
    /// Записывает значение по ключу: заменяет существующее либо создаёт ключ
    /// (docs/semantics.md §6.2). Байты нового ключа копируются в пул текста.
    /// Предусловие: o.kind() == Value::Kind::Object.
    void objectSet(Value o, std::string_view key, Value v);
```

В приватную секцию, после `growArray`:

```cpp
    void growObject(detail::ObjectRep &rep, std::uint32_t needed);

    /// Номер ключа, а если ключа нет — место, куда его вставить, чтобы
    /// сортировка сохранилась. found получает признак находки.
    std::uint32_t findKey(const detail::ObjectRep &rep, std::string_view key,
                          bool *found) const noexcept;
```

- [ ] **Шаг 4: Написать тела в `core/src/context.cpp`**

После `arrayPop`:

```cpp
void Context::growObject(detail::ObjectRep &rep, std::uint32_t needed) {
    if (needed <= rep.capacity) { return; }

    std::uint32_t capacity = rep.capacity == 0 ? 4 : rep.capacity;
    while (capacity < needed) {
        assert(capacity <= 0x7fffffffu && "объект перерос uint32");
        capacity *= 2;
    }

    const std::uint32_t start = static_cast<std::uint32_t>(entries_.size());
    entries_.insert(entries_.end(), capacity, detail::Entry{0, 0, Value::null()});
    for (std::uint32_t i = 0; i < rep.count; ++i) {
        entries_[start + i] = entries_[rep.start + i];
    }

    rep.start = start;
    rep.capacity = capacity;
}

std::uint32_t Context::findKey(const detail::ObjectRep &rep, std::string_view key,
                               bool *found) const noexcept {
    // Пары отсортированы по ключу, поиск двоичный: на типичных 3–20 ключах
    // это дешевле хеш-таблицы и не выделяет ничего сверх самого массива.
    std::uint32_t low = 0;
    std::uint32_t high = rep.count;
    while (low < high) {
        const std::uint32_t mid = low + (high - low) / 2;
        const detail::Entry &entry = entries_[rep.start + mid];
        const std::string_view candidate = textAt(entry.keyOffset, entry.keyLength);
        if (candidate < key) {
            low = mid + 1;
        } else if (key < candidate) {
            high = mid;
        } else {
            *found = true;
            return mid;
        }
    }
    *found = false;
    return low;
}

Value Context::makeObject(std::uint32_t capacity) {
    const std::uint32_t index = static_cast<std::uint32_t>(objects_.size());
    objects_.push_back(detail::ObjectRep{0, 0, 0});
    if (capacity > 0) { growObject(objects_[index], capacity); }
    return Value::object(index);
}

std::uint32_t Context::objectCount(Value o) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    return objects_[o.index()].count;
}

Value Context::objectGet(Value o, std::string_view key) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectRep &rep = objects_[o.index()];
    bool found = false;
    const std::uint32_t at = findKey(rep, key, &found);
    if (!found) { return Value::null(); }
    return entries_[rep.start + at].value;
}

bool Context::objectHas(Value o, std::string_view key) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    bool found = false;
    findKey(objects_[o.index()], key, &found);
    return found;
}

std::string_view Context::objectKeyAt(Value o, std::uint32_t i) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectRep &rep = objects_[o.index()];
    if (i >= rep.count) { return {}; }
    const detail::Entry &entry = entries_[rep.start + i];
    return textAt(entry.keyOffset, entry.keyLength);
}

Value Context::objectValueAt(Value o, std::uint32_t i) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectRep &rep = objects_[o.index()];
    if (i >= rep.count) { return Value::null(); }
    return entries_[rep.start + i].value;
}

void Context::objectSet(Value o, std::string_view key, Value v) {
    assert(o.kind() == Value::Kind::Object);
    detail::ObjectRep &rep = objects_[o.index()];

    bool found = false;
    const std::uint32_t at = findKey(rep, key, &found);
    if (found) {
        entries_[rep.start + at].value = v;
        return;
    }

    // appendText вправе переселить text_, поэтому смещение берётся до роста
    // entries_, а срез key после этой строки уже не трогаем.
    const std::uint32_t keyOffset = appendText(key);
    const std::uint32_t keyLength = static_cast<std::uint32_t>(key.size());

    growObject(rep, rep.count + 1);
    for (std::uint32_t i = rep.count; i > at; --i) {
        entries_[rep.start + i] = entries_[rep.start + i - 1];
    }
    entries_[rep.start + at] = detail::Entry{keyOffset, keyLength, v};
    rep.count += 1;
}
```

- [ ] **Шаг 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R ContextObject`
Expected: 11 тестов PASS.

- [ ] **Шаг 6: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 190 тестов PASS.

- [ ] **Шаг 7: Коммит**

```bash
git add core/src/context.hpp core/src/context.cpp core/tests/context_test.cpp
git commit -m "Add objects as sorted key-value ranges"
```

---

## Задача 6: Объекты — порядок, рост и мутации

**Files:**
- Modify: `core/tests/context_test.cpp` (только тесты)

**Interfaces:**
- Consumes: весь набор из задачи 5. Нового кода не пишется — задача проверяет свойства, которых предыдущие тесты не касались, и правит реализацию, только если тест их не подтвердит.
- Produces: ничего.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/context_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(ContextObjectMutation, RepeatedKeyReplacesValue) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "k", Value::number(1.0));
    ctx.objectSet(o, "k", Value::number(2.0));
    EXPECT_EQ(ctx.objectCount(o), 1u);
    EXPECT_EQ(ctx.objectGet(o, "k").numberValue(), 2.0);
}

TEST(ContextObjectMutation, ReplacementCopiesNoKeyBytes) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "k", Value::number(1.0));
    const std::size_t before = ctx.bytesUsed();
    ctx.objectSet(o, "k", Value::number(2.0));
    EXPECT_EQ(ctx.bytesUsed(), before);
}

TEST(ContextObjectMutation, InsertionKeepsSortedOrder) {
    Context ctx;
    const Value o = ctx.makeObject();
    const char *keys[] = {"delta", "alpha", "charlie", "bravo", "echo"};
    for (const char *key : keys) { ctx.objectSet(o, key, Value::null()); }

    std::string seen;
    for (std::uint32_t i = 0; i < ctx.objectCount(o); ++i) {
        seen += ctx.objectKeyAt(o, i);
        seen += ' ';
    }
    EXPECT_EQ(seen, "alpha bravo charlie delta echo ");
}

TEST(ContextObjectMutation, EveryKeySurvivesGrowth) {
    Context ctx;
    const Value o = ctx.makeObject();
    // Тридцать ключей — рост через 4, 8, 16, 32: пары переезжают четырежды.
    for (int i = 0; i < 30; ++i) {
        ctx.objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)));
    }
    ASSERT_EQ(ctx.objectCount(o), 30u);
    for (int i = 0; i < 30; ++i) {
        EXPECT_EQ(ctx.objectGet(o, "key" + std::to_string(i)).numberValue(),
                  static_cast<double>(i));
    }
}

TEST(ContextObjectMutation, AliasSeesNewKey) {
    Context ctx;
    const Value o = ctx.makeObject();
    const Value alias = o;
    for (int i = 0; i < 30; ++i) {
        ctx.objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)));
    }
    // semantics.md §2.3: изменение через одно имя видно через второе.
    EXPECT_EQ(ctx.objectCount(alias), 30u);
    EXPECT_EQ(ctx.objectGet(alias, "key29").numberValue(), 29.0);
    EXPECT_TRUE(o.sameAggregate(alias));
}

TEST(ContextObjectMutation, KeyTakenFromTheSameContextWorks) {
    Context ctx;
    const Value o = ctx.makeObject();
    const Value keyValue = ctx.makeString("динамический");
    // Ключ — срез собственного пула текста: приём, которым пользуется obj[k].
    ctx.objectSet(o, ctx.string(keyValue), Value::number(5.0));
    EXPECT_EQ(ctx.objectGet(o, "динамический").numberValue(), 5.0);
}

TEST(ContextObjectMutation, ObjectMayContainItself) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "self", o);
    // semantics.md §2.3: obj['self'] = obj — корректная программа.
    EXPECT_TRUE(ctx.objectGet(o, "self").sameAggregate(o));
    EXPECT_EQ(ctx.objectCount(o), 1u);
}

TEST(ContextObjectMutation, ObjectHoldsArrayAndArrayHoldsObject) {
    Context ctx;
    const Value o = ctx.makeObject();
    const Value a = ctx.makeArray();
    ctx.objectSet(o, "items", a);
    ctx.arrayPush(a, o);

    EXPECT_TRUE(ctx.objectGet(o, "items").sameAggregate(a));
    EXPECT_TRUE(ctx.arrayAt(a, 0).sameAggregate(o));
}

TEST(ContextObjectMutation, PushIntoStoredArrayIsSeenThroughTheObject) {
    Context ctx;
    const Value o = ctx.makeObject();
    const Value a = ctx.makeArray();
    ctx.objectSet(o, "items", a);

    for (int i = 0; i < 20; ++i) {
        ctx.arrayPush(ctx.objectGet(o, "items"), Value::number(static_cast<double>(i)));
    }
    EXPECT_EQ(ctx.arrayCount(a), 20u);
}

TEST(ContextObjectMutation, PreallocatedCapacityGrowsNothing) {
    Context ctx;
    const Value o = ctx.makeObject(8);
    const std::size_t afterReserve = ctx.bytesUsed();
    for (int i = 0; i < 8; ++i) {
        ctx.objectSet(o, "k" + std::to_string(i), Value::null());
    }
    // Пары не переезжали; выросло ровно на байты восьми двухсимвольных ключей.
    EXPECT_EQ(ctx.bytesUsed(), afterReserve + 16u);
}
```

- [ ] **Шаг 2: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R ContextObjectMutation`
Expected: 10 тестов PASS. Если какой-то падает — правится `core/src/context.cpp`, тест не трогается.

- [ ] **Шаг 3: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 200 тестов PASS.

- [ ] **Шаг 4: Прогнать под санитайзерами**

```bash
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

Expected: 200 PASS, ни одного отчёта санитайзера.

- [ ] **Шаг 5: Прогнать с `-Werror`**

```bash
cmake -B build-werror -DCHUPASCRIPT_WERROR=ON
cmake --build build-werror -j
ctest --test-dir build-werror --output-on-failure
```

Expected: сборка без предупреждений, 200 PASS.

- [ ] **Шаг 6: Коммит**

```bash
git add core/tests/context_test.cpp
git commit -m "Cover object ordering, growth and aliasing"
```

---

## Задача 7: Бенчмарки и база производительности

**Files:**
- Create: `benchmarks/store_benchmark.cpp`
- Modify: `benchmarks/CMakeLists.txt:3-7`
- Modify: `benchmarks/baseline.json`

**Interfaces:**
- Consumes: весь `Context` из задач 2–5.
- Produces: базу для решения по `docs/backlog.md` B1.

- [ ] **Шаг 1: Написать бенчмарки**

Создать `benchmarks/store_benchmark.cpp`:

```cpp
// База производительности слоя хранения: цена роста, поиска и обхода.
// Служит основанием для решения по docs/backlog.md B1 — заменять ли пулы
// ареной.
#include <benchmark/benchmark.h>

#include <string>
#include <vector>

#include "context.hpp"

namespace {

using CS::Context;
using CS::Value;

/// Наполнение массива через push: цена удвоений и переездов.
void BM_Store_ArrayPush(benchmark::State &state) {
    const int count = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Context ctx;
        const Value a = ctx.makeArray();
        for (int i = 0; i < count; ++i) {
            ctx.arrayPush(a, Value::number(static_cast<double>(i)));
        }
        // Локальная переменная, а не временное значение: DoNotOptimize от
        // rvalue не переживает смены версии Google Benchmark.
        std::uint32_t filled = ctx.arrayCount(a);
        benchmark::DoNotOptimize(filled);
    }
    state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_Store_ArrayPush)->Arg(1000);

/// То же с заранее известным размером: столько стоило бы наполнение без роста.
void BM_Store_ArrayPushReserved(benchmark::State &state) {
    const int count = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Context ctx;
        const Value a = ctx.makeArray(static_cast<std::uint32_t>(count));
        for (int i = 0; i < count; ++i) {
            ctx.arrayPush(a, Value::number(static_cast<double>(i)));
        }
        std::uint32_t filled = ctx.arrayCount(a);
        benchmark::DoNotOptimize(filled);
    }
    state.SetItemsProcessed(state.iterations() * count);
}
BENCHMARK(BM_Store_ArrayPushReserved)->Arg(1000);

/// Обход массива: цена разыменования через индекс.
void BM_Store_ArrayTraverse(benchmark::State &state) {
    Context ctx;
    const Value a = ctx.makeArray(1000);
    for (int i = 0; i < 1000; ++i) {
        ctx.arrayPush(a, Value::number(static_cast<double>(i)));
    }

    for (auto _ : state) {
        double sum = 0.0;
        for (std::uint32_t i = 0; i < ctx.arrayCount(a); ++i) {
            sum += ctx.arrayAt(a, i).numberValue();
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * 1000);
}
BENCHMARK(BM_Store_ArrayTraverse);

/// Объект заданного размера с ключами вида "keyNN".
Value makeFilledObject(Context &ctx, int keys) {
    const Value o = ctx.makeObject(static_cast<std::uint32_t>(keys));
    for (int i = 0; i < keys; ++i) {
        ctx.objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)));
    }
    return o;
}

/// Поиск ключа: проверка утверждения «двоичный поиск дешевле хеша на 3–20».
void BM_Store_ObjectGet(benchmark::State &state) {
    const int keys = static_cast<int>(state.range(0));
    Context ctx;
    const Value o = makeFilledObject(ctx, keys);
    const std::string last = "key" + std::to_string(keys - 1);

    for (auto _ : state) {
        Value found = ctx.objectGet(o, last);
        benchmark::DoNotOptimize(found);
    }
}
BENCHMARK(BM_Store_ObjectGet)->Arg(3)->Arg(8)->Arg(20);

/// Вставка нового ключа: цена сдвига хвоста и роста.
void BM_Store_ObjectInsert(benchmark::State &state) {
    const int keys = static_cast<int>(state.range(0));
    for (auto _ : state) {
        Context ctx;
        const Value o = makeFilledObject(ctx, keys);
        std::uint32_t filled = ctx.objectCount(o);
        benchmark::DoNotOptimize(filled);
    }
    state.SetItemsProcessed(state.iterations() * keys);
}
BENCHMARK(BM_Store_ObjectInsert)->Arg(3)->Arg(8)->Arg(20);

/// Создание строки: копия в пул текста.
void BM_Store_MakeString(benchmark::State &state) {
    const std::string text(32, 'x');
    for (auto _ : state) {
        Context ctx;
        for (int i = 0; i < 100; ++i) {
            Value made = ctx.makeString(text);
            benchmark::DoNotOptimize(made);
        }
    }
    state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK(BM_Store_MakeString);

}  // namespace
```

- [ ] **Шаг 2: Зарегистрировать в сборке**

`benchmarks/CMakeLists.txt`, список исходников:

```cmake
add_executable(chupascript_benchmarks
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
./build-rel/benchmarks/chupascript_benchmarks --benchmark_filter=Store
```

Expected: десять строк, ни одной с `SkipWithError`. Шесть функций, из них две с тремя значениями `Arg` каждая: 1 + 1 + 1 + 3 + 3 + 1. Проверить глазами два числа: `BM_Store_ArrayPush/1000` обязан быть заметно дороже `BM_Store_ArrayPushReserved/1000` — это и есть цена удвоений; `BM_Store_ObjectGet` обязан расти от 3 к 20 ключам полого, а не линейно.

- [ ] **Шаг 4: Записать базу**

Прогнать весь набор бенчмарков с повторами и обновить базу:

```bash
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_out=/tmp/store-baseline.json --benchmark_out_format=json
cp /tmp/store-baseline.json benchmarks/baseline.json
python3 -c "
import json
data = json.load(open('benchmarks/baseline.json', encoding='utf-8'))
names = {b['run_name'] for b in data['benchmarks']}
expected = {'BM_Store_ArrayPush', 'BM_Store_ArrayPushReserved', 'BM_Store_ArrayTraverse',
            'BM_Store_ObjectGet', 'BM_Store_ObjectInsert', 'BM_Store_MakeString'}
missing = expected - names
assert not missing, f'нет бенчмарков: {missing}'
print('файл разбирается, все BM_Store_* на месте')
"
```

Сравнение файла с самим собой прошло бы всегда: единственная проверка, которую
стоит сделать здесь, — что файл разбирается как JSON и что в нём есть все
ожидаемые имена бенчмарков.

Машина обязана быть незанятой: параллельная сборка во время замера искажает числа на десятки процентов.

- [ ] **Шаг 5: Записать в базу пометку о машине**

Добавить в `benchmarks/baseline.json` в объект `context` поле с описанием машины, чтобы сравнение не проводилось вслепую между разными компьютерами:

```bash
python3 - <<'PY'
import json, platform
path = "benchmarks/baseline.json"
with open(path, encoding="utf-8") as handle:
    data = json.load(handle)
data["context"]["chupascript_machine"] = f"{platform.machine()} {platform.platform()}"
with open(path, "w", encoding="utf-8") as handle:
    json.dump(data, handle, indent=2, ensure_ascii=False)
PY
```

- [ ] **Шаг 6: Коммит**

```bash
git add benchmarks/store_benchmark.cpp benchmarks/CMakeLists.txt benchmarks/baseline.json
git commit -m "Record the storage performance baseline"
```

---

## Задача 8: Документы

**Files:**
- Modify: `docs/backlog.md` (пункты B1, B2, новый пункт)
- Modify: `docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md` (§3, §9)

**Interfaces:**
- Consumes: решения задач 1–7.
- Produces: согласованные документы.

- [ ] **Шаг 1: Переписать B1 в `docs/backlog.md`**

Заменить тело пункта «B1. Разделить арену на постоянную и временную» так, чтобы оно отражало сегодняшнее устройство. Текст пункта:

```markdown
Хранилище — пять векторов внутри `Context` (`core/src/context.hpp`):
`pool_`, `arrays_`, `objects_`, `entries_`, `text_`. Освобождения по одному
нет: рост агрегата дописывает новый диапазон в хвост и бросает старый мусором,
а весь мусор живёт до смерти контекста. Временные значения, созданные при
пересчёте props, копятся так же — расход растёт с числом пересчётов, а не с
объёмом данных.

Разделение на постоянную часть (данные с бэкенда) и временную (значения одного
вычисления, сбрасываемые после него) закрывает и то, и другое.

Хендлы — индексы, а не указатели, поэтому замена не выходит за пределы
приватной части `Context`: ни `Value`, ни интерпретатор, ни тесты не меняются
(`docs/superpowers/specs/2026-08-11-chupascript-values-design.md` §3).

**Признак, что пора:** `bytesUsed` растёт от числа пересчётов на реальном
экране, либо `BM_Store_ArrayPush` против `BM_Store_ArrayPushReserved`
показывает, что цена роста заметна на фоне вычисления.
```

- [ ] **Шаг 2: Дополнить B2 в `docs/backlog.md`**

В пункт «B2. Компактное представление `Value`» добавить перед строкой «Признак, что пора»:

```markdown
Цель конкретна: 16 байт сейчас, 8 при NaN-boxing. Меньше шестнадцати
тег-юнион не бывает — `double` в объединении задаёт и размер, и выравнивание,
а тег добавляет девятый байт. Переход на индексы вместо указателей освободил
место внутри объединения, но структуру не сократил. Единственный путь —
убрать `double` из объединения.
```

- [ ] **Шаг 3: Добавить новый пункт в `docs/backlog.md`**

В раздел «Память», после B2:

```markdown
### B21. Нехватка памяти завершает процесс

**Где:** `core/src/context.cpp`
**Статус:** принято на MVP

`std::vector` бросает `std::bad_alloc`, до хоста через C-границу исключение не
доедет; на границе будет заглушка, завершающая процесс.

Решение сознательное: на телефоне система убивает приложение раньше, чем
аллокатор вернёт отказ, а честная обработка стоила бы признака успеха у каждой
операции над значением и отравила бы весь API ради ветки, которая не
выполняется.

**Признак, что пора:** появление хоста, для которого падение процесса
неприемлемо, — например встраивание в серверный процесс, обслуживающий много
экранов сразу.
```

- [ ] **Шаг 4: Поправить §3 спеки C API**

В `docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md`, блок «Время жизни результата», убрать абзац, начинающийся словами «Правило строже реальности», и поставить вместо него:

```markdown
Правило описывает реальность буквально. Пулы значений переезжают при росте
(`2026-08-11-chupascript-values-design.md` §3), поэтому указатель, выданный до
ближайшей мутации, после неё повисает. Хост копирует строку в свой тип при
первом касании, и держать результат дольше ему незачем.
```

- [ ] **Шаг 5: Поправить §9 спеки C API**

В таблице решений заменить строку про `ChupaValue` и дописать строку про контекст:

```markdown
| Наружу — непрозрачный `ChupaValue *` с функциями доступа | Раскладка не видна, через границу едут только примитивы и указатели. Обёртки Swift и Kotlin строят поверх этого свои типы |
| Функции доступа принимают контекст первым параметром | Значение адресует пулы индексами, и разрешить их может только выдавший контекст. Подпись перестаёт врать о времени жизни, а в Swift получается `ctx.string(of: v)` |
```

- [ ] **Шаг 6: Поправить сигнатуры в §9 спеки C API**

В блоке с объявлениями функций доступа добавить `const ChupaContext *ctx` первым параметром каждой из них:

```c
CHUPA_API ChupaKind chupa_value_kind(const ChupaContext *ctx, const ChupaValue *v);
CHUPA_API CHUPA_MUST_USE bool chupa_value_bool  (const ChupaContext *ctx, const ChupaValue *v, bool   *out);
CHUPA_API CHUPA_MUST_USE bool chupa_value_number(const ChupaContext *ctx, const ChupaValue *v, double *out);
CHUPA_API const char *CHUPA_NULLABLE
chupa_value_string(const ChupaContext *ctx, const ChupaValue *v, size_t *len);

CHUPA_API CHUPA_MUST_USE bool chupa_array_count(const ChupaContext *ctx, const ChupaValue *v, size_t *out);
CHUPA_API const ChupaValue *CHUPA_NULLABLE
chupa_array_at(const ChupaContext *ctx, const ChupaValue *v, size_t i);

CHUPA_API CHUPA_MUST_USE bool chupa_object_count(const ChupaContext *ctx, const ChupaValue *v, size_t *out);
CHUPA_API const char *CHUPA_NULLABLE
chupa_object_key_at(const ChupaContext *ctx, const ChupaValue *v, size_t i, size_t *len);
CHUPA_API const ChupaValue *CHUPA_NULLABLE
chupa_object_value_at(const ChupaContext *ctx, const ChupaValue *v, size_t i);
CHUPA_API const ChupaValue *CHUPA_NULLABLE
chupa_object_get(const ChupaContext *ctx, const ChupaValue *v, const char *key, size_t len);
```

- [ ] **Шаг 7: Отметить B15 закрытым наполовину**

В пункт «B15. `strictEqual` и `looseEqual` в `core/src/value.hpp`» дописать:

```markdown
Объявления удалены вместе с переписыванием `value.hpp`
(`2026-08-11-chupascript-values-design.md`). Равенство целиком переезжает в
слой вычислителя, где доступны приведения из `docs/semantics.md` §4.
```

- [ ] **Шаг 8: Проверить, что ссылки на B-пункты живые**

Run: `grep -n "TODO(B" core/src/*.hpp core/src/*.cpp && grep -c "^### B" docs/backlog.md`
Expected: каждый `TODO(B<N>)` из исходников имеет соответствующий заголовок `### B<N>.` в backlog; всего пунктов 21.

- [ ] **Шаг 9: Коммит**

```bash
git add docs/backlog.md docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md
git commit -m "Record the storage decisions in the backlog and the C API spec"
```

---

## Итог

| | |
|---|---|
| Задач | 8 |
| Новых файлов | 4 (`context.hpp`, `context.cpp`, `value_test.cpp`, `store_benchmark.cpp`) |
| Переписанных | 1 (`value.hpp`) |
| Тестов добавлено | 53 |
| Тестов всего | 200 |
| Бенчмарков добавлено | 10 строк из 6 функций |

Слой закончен, когда: `ctest` даёт 200 из 200 в обычной сборке, под ASan+UBSan и с `-Werror`; `benchmarks/baseline.json` содержит строки `BM_Store_*`; `docs/backlog.md` и спека C API не расходятся с реализацией.

Следующий этап — загрузка JSON: текст данных копируется в `text_`, строки без escape-последовательностей становятся срезами этой копии, ключи и элементы складываются в объекты и массивы с заранее известной ёмкостью.
