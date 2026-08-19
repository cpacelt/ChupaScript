# Модель памяти: арена и подсчёт ссылок — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** перевести агрегаты и долгоживущие строки ChupaScript с пулов `Store` на узлы со счётчиком ссылок, оставив арену промежуточным значениям одной операции.

**Architecture:** массивы, объекты и долгоживущие строки становятся узлами в куче со встроенным счётчиком; `Value` держит указатель на узел вместо индекса в пул. Корни — только ячейки глобальных переменных и ссылки хоста; освобождение отложено до границы операции через список, который сливается в `Context::beginOperation`. Промежуточные строки остаются смещениями в байтовую арену операции, ключи объектов интернируются в таблицу с одним счётчиком на всю таблицу.

**Tech Stack:** C++17, CMake, GoogleTest, Google Benchmark.

**Spec:** `docs/superpowers/specs/2026-08-19-chupascript-memory-model-design.md`

## Global Constraints

- Стандарт — **C++17** (`CMAKE_CXX_STANDARD 17` в `CMakeLists.txt:16`). Ни `std::span`, ни designated initializers, ни `constexpr` из C++20.
- `sizeof(Value) == 16` и `std::is_trivially_copyable_v<Value>` — оба `static_assert` в `core/src/value.hpp` обязаны выжить.
- `sizeof(Ast::Node) == 24` — `static_assert` в `core/src/ast.hpp:261` обязан выжить.
- **Семантика языка не меняется ни в одной букве.** Ни один тест из `core/tests` и `cli/tests` не должен менять ожидания; править разрешено только то, что лезет во внутренности `Store` напрямую, и только по форме вызова, не по ожидаемому результату.
- **Единственное исключение из предыдущего — метрики байт.** `Store::bytesUsed`/`bytesReserved` считали пулы, а память узлов хранилищу больше не принадлежит. Тесты с конкретными ожиданиями по байтам — `core/tests/store_test.cpp:67,72,74,84`, `core/tests/data_test.cpp:258,265`, `core/tests/expression_test.cpp:243,249,265,269` — обязаны поменять ожидания, и это не послабление, а следствие того, что измеряемая величина стала другой. Каждое изменение ожидания объясняется в коммите.
- Комментарии и имена — по-русски, в стиле окружающего кода: объясняют «почему», а не «что».
- Сообщения коммитов — по-русски, тема описывает проблему, которую правка снимает. В конце каждого: `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.
- Отладочная сборка: `cmake --build build-dbg -j8`. Тесты ядра: `./build-dbg/core/tests/chupascript_tests`. Тесты CLI: `./build-dbg/cli/tests/chupa_cli_tests`.
- Новые `.cpp` ядра добавляются в `core/CMakeLists.txt`, новые тесты — в `core/tests/CMakeLists.txt`.

---

## Структура файлов

**Создаются:**
- `core/src/keytable.hpp`, `core/src/keytable.cpp` — таблица интернирования ключей: байты имён и их номера, один счётчик на таблицу.
- `core/src/node.hpp`, `core/src/node.cpp` — узлы со счётчиком: `Node`, `StrNode`, `ArrayNode`, `ObjectNode`, `Entry`, `retain`/`release`.
- `core/tests/keytable_test.cpp`, `core/tests/node_test.cpp`.

**Меняются:**
- `core/src/value.hpp` — регион `Counted` вместо `Persistent`, указатель на узел в объединении, закрытые фабрики.
- `core/src/store.hpp`, `core/src/store.cpp` — пулы агрегатов уходят, остаются байтовая арена, таблица глобальных и список отложенного освобождения.
- `core/src/execution.hpp` — временный `Store` получает общую с постоянным таблицу ключей.
- `core/src/context.hpp` — граница операции сливает список отложенного освобождения; `storeOf` уходит.
- `core/src/ast.hpp`, `core/src/ast.cpp`, `core/src/compile.cpp` — литерал хранится указателем на узел.
- `core/src/eval.cpp`, `core/src/builtin.cpp`, `core/src/data.cpp` — `promote` в местах укладки значений.
- `core/src/c_api.cpp` — граница операции у хостовых сеттеров.

---

## Task 1: Таблица интернирования ключей

**Files:**
- Create: `core/src/keytable.hpp`, `core/src/keytable.cpp`
- Create: `core/tests/keytable_test.cpp`
- Modify: `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: ничего.
- Produces: `CS::KeyTable` с `create()`, `retain(KeyTable *)`, `release(KeyTable *)`, `intern(std::string_view) -> std::uint32_t`, `find(std::string_view) const -> std::uint32_t`, `bytes(std::uint32_t) const -> std::string_view`, `count() const -> std::uint32_t`; константа `CS::kNoKey`.

- [ ] **Шаг 1: написать падающий тест**

`core/tests/keytable_test.cpp`:

```cpp
#include "keytable.hpp"

#include <gtest/gtest.h>

namespace {

using CS::KeyTable;

TEST(KeyTable, InternReturnsSameIdForSameKey) {
    KeyTable *t = KeyTable::create();
    const std::uint32_t a = t->intern("name");
    const std::uint32_t b = t->intern("name");
    EXPECT_EQ(a, b);
    EXPECT_EQ(t->count(), 1u);
    KeyTable::release(t);
}

TEST(KeyTable, InternReturnsDifferentIdsForDifferentKeys) {
    KeyTable *t = KeyTable::create();
    EXPECT_NE(t->intern("name"), t->intern("id"));
    EXPECT_EQ(t->count(), 2u);
    KeyTable::release(t);
}

TEST(KeyTable, BytesRoundTrip) {
    KeyTable *t = KeyTable::create();
    const std::uint32_t id = t->intern("привет");
    EXPECT_EQ(t->bytes(id), "привет");
    KeyTable::release(t);
}

TEST(KeyTable, FindDoesNotIntern) {
    KeyTable *t = KeyTable::create();
    EXPECT_EQ(t->find("нет"), CS::kNoKey);
    EXPECT_EQ(t->count(), 0u);
    KeyTable::release(t);
}

TEST(KeyTable, KeepsEmbeddedNulByte) {
    KeyTable *t = KeyTable::create();
    const std::string key("a\0b", 3);
    const std::uint32_t id = t->intern(key);
    EXPECT_EQ(t->bytes(id).size(), 3u);
    EXPECT_EQ(t->find(key), id);
    KeyTable::release(t);
}

TEST(KeyTable, EmptyKeyIsAKey) {
    KeyTable *t = KeyTable::create();
    const std::uint32_t id = t->intern("");
    EXPECT_NE(id, CS::kNoKey);
    EXPECT_TRUE(t->bytes(id).empty());
    EXPECT_EQ(t->find(""), id);
    KeyTable::release(t);
}

TEST(KeyTable, RetainKeepsTableAlivePastFirstRelease) {
    KeyTable *t = KeyTable::create();
    const std::uint32_t id = t->intern("name");
    KeyTable::retain(t);
    KeyTable::release(t);
    // Вторая ссылка ещё держит: читать можно.
    EXPECT_EQ(t->bytes(id), "name");
    KeyTable::release(t);
}

}  // namespace
```

- [ ] **Шаг 2: прогнать и убедиться, что не собирается**

Добавить `keytable_test.cpp` в `core/tests/CMakeLists.txt` (список `add_executable`).

Run: `cmake --build build-dbg -j8`
Expected: FAIL — `fatal error: 'keytable.hpp' file not found`.

- [ ] **Шаг 3: написать заголовок**

`core/src/keytable.hpp`:

```cpp
#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

namespace CS {

/// Ключа нет. Номером ключа быть не может: столько их не бывает.
inline constexpr std::uint32_t kNoKey = 0xffffffffu;

/// Таблица имён полей: байты ключа лежат в ней в единственном экземпляре, а
/// объекты держат четырёхбайтовый номер.
///
/// Ключи — не данные общего вида: их мало, они повторяются тысячами и почти
/// все известны на компиляции. Поэтому внутри таблицы поштучного учёта нет
/// вовсе — это арена, только дописывание. Счётчик ссылок стоит на таблице
/// целиком: её держит каждый узел-объект, и она переживает контекст ровно
/// тогда, когда её пережил хоть один объект, уехавший к хосту.
class KeyTable {
   public:
    /// Новая таблица со счётчиком 1.
    static KeyTable *create();
    static void retain(KeyTable *table) noexcept;
    /// Отпускает ссылку; на нуле разрушает таблицу.
    static void release(KeyTable *table) noexcept;

    /// Номер ключа, при надобности заводит новый.
    std::uint32_t intern(std::string_view key);

    /// Номер ключа либо kNoKey. В таблицу не пишет — нужен чтению объекта,
    /// которое не должно засорять её именами, которых в нём нет.
    [[nodiscard]] std::uint32_t find(std::string_view key) const noexcept;

    /// Байты ключа по номеру. Срез действителен, пока таблица жива и в неё не
    /// дописали: рост арены переселяет буфер.
    [[nodiscard]] std::string_view bytes(std::uint32_t id) const noexcept;

    [[nodiscard]] std::uint32_t count() const noexcept;

   private:
    KeyTable() = default;

    /// Координаты ключа в арене имён.
    struct Rec {
        std::uint32_t offset;
        std::uint32_t length;
    };

    /// Место в sorted_, где номер стоит либо должен встать. found — признак
    /// находки. Тот же двоичный поиск, что был у Store::findKey.
    std::uint32_t place(std::string_view key, bool *found) const noexcept;

    std::uint32_t rc_ = 1;
    std::vector<char> text_;       // байты имён подряд, только дописывание
    std::vector<Rec> byId_;        // номер → координаты
    std::vector<std::uint32_t> sorted_;  // номера, упорядоченные по байтам
};

}  // namespace CS
```

- [ ] **Шаг 4: написать реализацию**

`core/src/keytable.cpp`:

```cpp
#include "keytable.hpp"

#include <cassert>
#include <cstring>

namespace CS {

KeyTable *KeyTable::create() { return new KeyTable(); }

void KeyTable::retain(KeyTable *table) noexcept {
    assert(table != nullptr);
    ++table->rc_;
}

void KeyTable::release(KeyTable *table) noexcept {
    assert(table != nullptr && table->rc_ > 0);
    if (--table->rc_ == 0) { delete table; }
}

std::uint32_t KeyTable::place(std::string_view key,
                              bool *found) const noexcept {
    std::uint32_t low = 0;
    std::uint32_t high = static_cast<std::uint32_t>(sorted_.size());
    while (low < high) {
        const std::uint32_t mid = low + (high - low) / 2;
        const Rec &rec = byId_[sorted_[mid]];
        const std::string_view candidate(text_.data() + rec.offset, rec.length);
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

std::uint32_t KeyTable::find(std::string_view key) const noexcept {
    bool found = false;
    const std::uint32_t at = place(key, &found);
    return found ? sorted_[at] : kNoKey;
}

std::uint32_t KeyTable::intern(std::string_view key) {
    bool found = false;
    const std::uint32_t at = place(key, &found);
    if (found) { return sorted_[at]; }

    assert(text_.size() + key.size() <= 0xffffffffu && "арена имён переросла uint32");
    const std::uint32_t offset = static_cast<std::uint32_t>(text_.size());
    text_.insert(text_.end(), key.begin(), key.end());

    const std::uint32_t id = static_cast<std::uint32_t>(byId_.size());
    byId_.push_back(Rec{offset, static_cast<std::uint32_t>(key.size())});
    sorted_.insert(sorted_.begin() + at, id);
    return id;
}

std::string_view KeyTable::bytes(std::uint32_t id) const noexcept {
    assert(id < byId_.size() && "номер ключа выдан другой таблицей");
    // Проверяется пустота арены, а не длина: пустой ключ — законный ключ, и
    // отличаться от отсутствующего он обязан.
    if (text_.empty()) { return {}; }
    const Rec &rec = byId_[id];
    return std::string_view(text_.data() + rec.offset, rec.length);
}

std::uint32_t KeyTable::count() const noexcept {
    return static_cast<std::uint32_t>(byId_.size());
}

}  // namespace CS
```

Добавить `src/keytable.cpp` в список исходников `core/CMakeLists.txt`.

- [ ] **Шаг 5: прогнать тесты**

Run: `cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests --gtest_filter='KeyTable.*'`
Expected: PASS, 7 тестов.

- [ ] **Шаг 6: коммит**

```bash
git add core/src/keytable.hpp core/src/keytable.cpp core/tests/keytable_test.cpp core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -F - <<'MSG'
feat: байты ключа лежали в объекте столько раз, сколько объектов их завели

Тысяча объектов по три поля хранила три тысячи копий имён. Таблица
интернирования кладёт имя один раз и выдаёт номер; учёта внутри неё нет,
счётчик ссылок стоит на таблице целиком.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

## Task 2: Узлы со счётчиком ссылок

**Files:**
- Create: `core/src/node.hpp`, `core/src/node.cpp`
- Create: `core/tests/node_test.cpp`
- Modify: `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `CS::KeyTable` (Task 1).
- Produces: `CS::detail::Node`, `StrNode`, `ArrayNode`, `ObjectNode`, `Entry`; фабрики `makeStrNode(std::string_view) -> StrNode *`, `makeArrayNode(std::uint32_t capacity) -> ArrayNode *`, `makeObjectNode(KeyTable *, std::uint32_t capacity) -> ObjectNode *`; `retain(Node *)`, `release(Node *)`; `liveNodeCount() -> std::size_t`.

**Порядок:** Task 3 меняет только `value.hpp` и от узлов не зависит — его шаги 3 и 4 надо выполнить до сборки этой задачи, иначе `Value::string(StrNode *, len)` и `Value::node()` ещё не существуют. Коммитов по-прежнему два, каждый на своё.

- [ ] **Шаг 1: написать падающий тест**

`core/tests/node_test.cpp`:

```cpp
#include "node.hpp"

#include <gtest/gtest.h>

#include "keytable.hpp"

namespace {

using CS::KeyTable;
using CS::Value;
using CS::detail::ArrayNode;
using CS::detail::ObjectNode;
using CS::detail::StrNode;

TEST(Node, StringNodeKeepsBytes) {
    StrNode *s = CS::detail::makeStrNode("привет");
    EXPECT_EQ(s->view(), "привет");
    EXPECT_EQ(s->len, 12u);
    CS::detail::release(s);
}

TEST(Node, StringNodeKeepsEmbeddedNul) {
    const std::string bytes("a\0b", 3);
    StrNode *s = CS::detail::makeStrNode(bytes);
    EXPECT_EQ(s->view().size(), 3u);
    EXPECT_EQ(s->view()[1], '\0');
    CS::detail::release(s);
}

TEST(Node, ArrayNodeStartsEmpty) {
    ArrayNode *a = CS::detail::makeArrayNode(4);
    EXPECT_TRUE(a->items.empty());
    EXPECT_GE(a->items.capacity(), 4u);
    CS::detail::release(a);
}

TEST(Node, ReleaseOfArrayReleasesElements) {
    StrNode *s = CS::detail::makeStrNode("x");
    ArrayNode *a = CS::detail::makeArrayNode(1);
    a->items.push_back(Value::string(s, s->len));
    CS::detail::retain(s);           // ссылка ячейки массива
    CS::detail::release(s);          // ссылка создателя ушла, держит массив
    EXPECT_EQ(s->rc, 1u);
    CS::detail::release(a);          // массив отпускает элемент
    // Дальше s недействителен; проверка через счётчик здесь невозможна, за
    // освобождением следит санитайзер адресов в отдельном прогоне.
}

TEST(Node, ObjectNodeHoldsKeyTable) {
    KeyTable *t = KeyTable::create();
    ObjectNode *o = CS::detail::makeObjectNode(t, 2);
    KeyTable::release(t);            // ссылка создателя ушла, держит объект
    EXPECT_EQ(o->keys->intern("name"), 0u);
    CS::detail::release(o);
}

TEST(Node, LiveCountReturnsToWhereItStarted) {
    const std::size_t before = CS::detail::liveNodeCount();
    ArrayNode *a = CS::detail::makeArrayNode(0);
    EXPECT_EQ(CS::detail::liveNodeCount(), before + 1);
    CS::detail::release(a);
    EXPECT_EQ(CS::detail::liveNodeCount(), before);
}

TEST(Node, RetainKeepsNodeAlivePastFirstRelease) {
    ArrayNode *a = CS::detail::makeArrayNode(0);
    CS::detail::retain(a);
    CS::detail::release(a);
    EXPECT_EQ(a->rc, 1u);
    CS::detail::release(a);
}

}  // namespace
```

- [ ] **Шаг 2: прогнать и убедиться, что не собирается**

Добавить `node_test.cpp` в `core/tests/CMakeLists.txt`.

Run: `cmake --build build-dbg -j8`
Expected: FAIL — `fatal error: 'node.hpp' file not found`.

- [ ] **Шаг 3: написать заголовок**

`core/src/node.hpp`:

```cpp
#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

#include "value.hpp"

namespace CS {

class KeyTable;

namespace detail {

/// Заголовок всякого узла со счётчиком ссылок.
///
/// Счётчик интрузивный, а не shared_ptr: тот шестнадцать байт и в объединение
/// Value (восемь) не влезает, ломает тривиальную копируемость, требует второй
/// аллокации под блок управления и считает атомарно — а контекст однопоточный.
struct Node {
    std::uint32_t rc;
    Value::Kind kind;
};

/// Строка: заголовок и байты одной аллокацией, байты хвостом.
struct StrNode : Node {
    std::uint32_t len;
    // Дальше в той же аллокации лежат len байт. Отдельного члена нет: массив
    // переменной длины — расширение, а не стандарт, и обращаться к байтам
    // надо через view().
    [[nodiscard]] std::string_view view() const noexcept;
};

struct ArrayNode : Node {
    std::vector<Value> items;
};

/// Пара объекта: номер ключа в таблице интернирования и значение.
struct Entry {
    std::uint32_t key;
    Value value;
};

struct ObjectNode : Node {
    KeyTable *keys;               // удерживается ссылкой
    std::vector<Entry> entries;   // упорядочены по байтам ключа
};

/// Счётчик у новорождённого — 1, и эта ссылка принадлежит создателю.
StrNode *makeStrNode(std::string_view bytes);
ArrayNode *makeArrayNode(std::uint32_t capacity);
/// Ссылку на таблицу узел берёт сам.
ObjectNode *makeObjectNode(KeyTable *keys, std::uint32_t capacity);

inline void retain(Node *node) noexcept { ++node->rc; }

/// Отпускает ссылку; на нуле разрушает узел, рекурсивно отпуская содержимое.
void release(Node *node) noexcept;

/// Сколько узлов сейчас живо во всём процессе.
///
/// Метрика для тестов, и другой у нас нет: память узла хранилищу не
/// принадлежит, поэтому Store::bytesUsed её не видит и утечку ею не поймать.
std::size_t liveNodeCount() noexcept;

}  // namespace detail
}  // namespace CS
```

- [ ] **Шаг 4: написать реализацию**

`core/src/node.cpp`:

```cpp
#include "node.hpp"

#include <cassert>
#include <cstdlib>
#include <cstring>
#include <new>

#include "keytable.hpp"

namespace CS {
namespace detail {

/// Живые узлы. Однопоточно, как и весь контекст.
static std::size_t g_liveNodes = 0;

std::size_t liveNodeCount() noexcept { return g_liveNodes; }

std::string_view StrNode::view() const noexcept {
    if (len == 0) { return {}; }
    return std::string_view(reinterpret_cast<const char *>(this) + sizeof(StrNode), len);
}

StrNode *makeStrNode(std::string_view bytes) {
    assert(bytes.size() <= 0xffffffffu && "строка переросла uint32");
    void *raw = ::operator new(sizeof(StrNode) + bytes.size());
    StrNode *node = new (raw) StrNode();
    node->rc = 1;
    node->kind = Value::Kind::String;
    ++g_liveNodes;
    node->len = static_cast<std::uint32_t>(bytes.size());
    if (!bytes.empty()) {
        std::memcpy(reinterpret_cast<char *>(raw) + sizeof(StrNode), bytes.data(),
                    bytes.size());
    }
    return node;
}

ArrayNode *makeArrayNode(std::uint32_t capacity) {
    ArrayNode *node = new ArrayNode();
    node->rc = 1;
    node->kind = Value::Kind::Array;
    ++g_liveNodes;
    if (capacity > 0) { node->items.reserve(capacity); }
    return node;
}

ObjectNode *makeObjectNode(KeyTable *keys, std::uint32_t capacity) {
    assert(keys != nullptr);
    ObjectNode *node = new ObjectNode();
    node->rc = 1;
    node->kind = Value::Kind::Object;
    ++g_liveNodes;
    node->keys = keys;
    KeyTable::retain(keys);
    if (capacity > 0) { node->entries.reserve(capacity); }
    return node;
}

/// Отпустить значение, если оно вообще на что-то ссылается счётчиком.
static void releaseValue(Value v) noexcept {
    if (v.addressesStore() && v.region() == Value::Region::Counted) {
        release(v.node());
    }
}

void release(Node *node) noexcept {
    assert(node != nullptr && node->rc > 0);
    if (--node->rc != 0) { return; }
    --g_liveNodes;

    switch (node->kind) {
        case Value::Kind::String: {
            StrNode *s = static_cast<StrNode *>(node);
            s->~StrNode();
            ::operator delete(static_cast<void *>(s));
            return;
        }
        case Value::Kind::Array: {
            ArrayNode *a = static_cast<ArrayNode *>(node);
            for (Value v : a->items) { releaseValue(v); }
            delete a;
            return;
        }
        case Value::Kind::Object: {
            ObjectNode *o = static_cast<ObjectNode *>(node);
            for (const Entry &e : o->entries) { releaseValue(e.value); }
            KeyTable::release(o->keys);
            delete o;
            return;
        }
        default:
            assert(false && "узла такого вида не бывает");
            return;
    }
}

}  // namespace detail
}  // namespace CS
```

Добавить `src/node.cpp` в `core/CMakeLists.txt`.

- [ ] **Шаг 5: прогнать тесты**

Run: `cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests --gtest_filter='Node.*'`
Expected: PASS, 7 тестов.

- [ ] **Шаг 6: коммит**

```bash
git add core/src/node.hpp core/src/node.cpp core/tests/node_test.cpp core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -F - <<'MSG'
feat: агрегат жил в пуле хранилища и умирал только вместе с ним

Узел со счётчиком ссылок умеет умереть сам, когда его отпустил последний
держатель, и умеет пережить контекст, когда его держит хост. Счётчик
интрузивный: shared_ptr не влезает в объединение Value.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

## Task 3: `Value` — регион `Counted` и указатель на узел

**Files:**
- Modify: `core/src/value.hpp`
- Modify: `core/tests/value_test.cpp`
- Modify: всё, где встречается `Value::Region::Persistent` — найти через `grep -rn "Region::Persistent" core cli benchmarks`

**Interfaces:**
- Consumes: `CS::detail::Node`, `StrNode`, `ArrayNode`, `ObjectNode` (Task 2, по объявлению вперёд).
- Produces: `Value::Region::Counted`; закрытые фабрики `Value::string(detail::StrNode *, std::uint32_t)`, `Value::array(detail::ArrayNode *)`, `Value::object(detail::ObjectNode *)`; открытый `detail::Node *Value::node() const noexcept`.

- [ ] **Шаг 1: написать падающий тест**

Дописать в `core/tests/value_test.cpp`:

```cpp
TEST(ValueLayout, StaysSixteenBytesWithNodePayload) {
    // Указатель — восемь байт и ложится в то же объединение, где double.
    EXPECT_EQ(sizeof(CS::Value), 16u);
    EXPECT_TRUE(std::is_trivially_copyable<CS::Value>::value);
}

TEST(ValueRegion, CountedIsTheDefaultForScalars) {
    // У скаляра региона нет, но поле читается — оно обязано быть Counted:
    // Scratch означал бы смещение в арену, которого у скаляра не бывает.
    EXPECT_EQ(CS::Value::number(1.0).region(), CS::Value::Region::Counted);
}
```

- [ ] **Шаг 2: прогнать и убедиться, что не собирается**

Run: `cmake --build build-dbg -j8`
Expected: FAIL — `no member named 'Counted' in 'CS::Value::Region'`.

- [ ] **Шаг 3: переименовать регион**

В `core/src/value.hpp` заменить перечисление и переписать его комментарий:

```cpp
    /// Как адресуется нагрузка значения.
    ///
    /// Раньше здесь стояла шкала времени жизни, и на её порядке держался
    /// барьер записи. Барьера больше нет: узел не может оказаться
    /// короткоживущее контейнера, за это отвечает счётчик ссылок. Осталось
    /// различение способа адресации, и оно двузначно.
    ///
    /// Counted — в объединении указатель на узел; значение самодостаточно и
    /// читается без всякого хранилища. Scratch — в объединении смещение в
    /// байтовую арену операции; так адресуются только строки.
    enum class Region : std::uint8_t { Counted, Scratch };
```

Заменить значение по умолчанию: `Region region_ = Region::Counted;`

Механически заменить `Value::Region::Persistent` на `Value::Region::Counted` во всём дереве:

```bash
grep -rl "Region::Persistent" core cli benchmarks | xargs sed -i '' 's/Region::Persistent/Region::Counted/g'
```

- [ ] **Шаг 4: добавить фабрики и доступ к узлу**

В `core/src/value.hpp` объявить узлы вперёд, до `class Value`:

```cpp
namespace detail {
struct Node;
struct StrNode;
struct ArrayNode;
struct ObjectNode;
}  // namespace detail
```

Добавить в объединение член и открытый доступ:

```cpp
    /// Узел, на который значение ссылается. Предусловие: addressesStore() и
    /// region() == Region::Counted.
    [[nodiscard]] detail::Node *node() const noexcept {
        assert(addressesStore() && region_ == Region::Counted);
        return node_;
    }
```

В закрытой части — новые фабрики рядом с прежними:

```cpp
    [[nodiscard]] static Value string(detail::StrNode *node,
                                      std::uint32_t length) noexcept {
        Value v;
        v.kind_ = Kind::String;
        v.length_ = length;
        v.node_ = reinterpret_cast<detail::Node *>(node);
        v.region_ = Region::Counted;
        return v;
    }

    [[nodiscard]] static Value array(detail::ArrayNode *node) noexcept {
        Value v;
        v.kind_ = Kind::Array;
        v.node_ = reinterpret_cast<detail::Node *>(node);
        v.region_ = Region::Counted;
        return v;
    }

    [[nodiscard]] static Value object(detail::ObjectNode *node) noexcept {
        Value v;
        v.kind_ = Kind::Object;
        v.node_ = reinterpret_cast<detail::Node *>(node);
        v.region_ = Region::Counted;
        return v;
    }
```

Фабрики закрыты, но `node.cpp` их зовёт — добавить рядом с `friend class Store`:

```cpp
    friend struct detail::StrNode;
    friend StrNode *detail::makeStrNode(std::string_view);
```

**Проще и надёжнее:** объявить дружбу одной строкой на пространство имён нельзя, поэтому фабрики делаются открытыми, а защита от подделки переносится в тип аргумента: собрать `Value::array` без настоящего `ArrayNode *` нельзя, а получить `ArrayNode *` можно только из `makeArrayNode`. Прежний довод — «индексы полны как тип» — с указателями не работает: указатель не подделаешь числом. Комментарий у фабрик обновить именно так.

В объединение добавить:

```cpp
        detail::Node *node_;
```

`sameAggregate` переписать на сравнение указателей:

```cpp
    [[nodiscard]] bool sameAggregate(Value other) const noexcept {
        if (kind_ != other.kind_) { return false; }
        if (kind_ != Kind::Array && kind_ != Kind::Object) { return false; }
        return node_ == other.node_;
    }
```

Сравнение региона отсюда уходит: у агрегата он всегда `Counted`.

- [ ] **Шаг 5: прогнать все тесты**

Run: `cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests && ./build-dbg/cli/tests/chupa_cli_tests`
Expected: PASS полностью. `Store` на этом шаге всё ещё работает на индексах — переименование региона и новые фабрики поведения не меняют.

- [ ] **Шаг 6: коммит**

```bash
git add core/src/value.hpp core/tests/value_test.cpp core/src core/tests cli benchmarks
git commit -F - <<'MSG'
refactor: регион значения означал время жизни, хотя решал вопрос адресации

Persistent против Scratch была шкалой, на порядке которой стоял барьер
записи. Со счётчиком ссылок барьер не нужен, и от региона остаётся один
вопрос: указатель на узел в объединении или смещение в арену операции.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

## Task 4: Массивы на узлах и список отложенного освобождения

**Files:**
- Modify: `core/src/store.hpp`, `core/src/store.cpp`
- Modify: `core/tests/store_test.cpp`
- Modify: `core/src/context.hpp`

**Interfaces:**
- Consumes: `detail::ArrayNode`, `makeArrayNode`, `retain`, `release` (Task 2); `Value::array`, `Value::node` (Task 3).
- Produces: `Store::drainPending()`; поведение `makeArray`/`arrayPush`/`arraySet`/`arrayPop`/`arrayAt`/`arrayCount` без изменения сигнатур.

**Что здесь происходит с владением.** Узел рождается со счётчиком 1, и эта ссылка сразу уходит в `pending_` хранилища. Ячейка массива, принимая значение, берёт свою ссылку (`retain`); вытесняемое значение уходит в `pending_` вместе со своей. `drainPending()` отпускает всё накопленное — зовёт его граница операции. Корнем не становится ничто: `pending_` держит ссылку ровно до ближайшей границы.

- [ ] **Шаг 1: написать падающий тест**

Дописать в `core/tests/store_test.cpp`:

```cpp
TEST(StoreArray, PushDoesNotRelocateOnEveryElement) {
    // Массив живёт в узле, а не диапазоном в пуле: дописывание в хвост не
    // переносит прежние элементы и не бросает их мусором.
    Store store;
    const Value a = store.makeArray(0);
    for (std::uint32_t i = 0; i < 1000; ++i) {
        store.arrayPush(a, Value::number(i));
    }
    EXPECT_EQ(store.arrayCount(a), 1000u);
    EXPECT_EQ(store.arrayAt(a, 999).numberValue(), 999.0);
}

TEST(StoreArray, DrainPendingKeepsValueReachableFromRoot) {
    // Массив, попавший в глобальную переменную, слив списка переживает.
    Store store;
    const Value a = store.makeArray(1);
    store.arrayPush(a, Value::number(7));
    store.setGlobal("rows", a);
    store.drainPending();
    EXPECT_EQ(store.arrayCount(store.global("rows")), 1u);
    EXPECT_EQ(store.arrayAt(store.global("rows"), 0).numberValue(), 7.0);
}

TEST(StoreArray, NestedArraySurvivesDrainThroughItsHolder) {
    Store store;
    const Value outer = store.makeArray(1);
    const Value inner = store.makeArray(1);
    store.arrayPush(inner, Value::number(1));
    store.arrayPush(outer, inner);
    store.setGlobal("rows", outer);
    store.drainPending();
    const Value got = store.arrayAt(store.global("rows"), 0);
    EXPECT_EQ(got.kind(), Value::Kind::Array);
    EXPECT_EQ(store.arrayCount(got), 1u);
}

TEST(StoreArray, PoppedValueOutlivesTheArrayUntilDrain) {
    // Вытесненное не освобождается на месте: ссылка уходит в список
    // отложенного освобождения, и читать значение можно до границы операции.
    Store store;
    const Value a = store.makeArray(1);
    const Value inner = store.makeArray(1);
    store.arrayPush(inner, Value::number(5));
    store.arrayPush(a, inner);
    store.setGlobal("rows", a);
    store.drainPending();

    Value popped = Value::null();
    EXPECT_TRUE(store.arrayPop(store.global("rows"), &popped));
    EXPECT_EQ(store.arrayCount(popped), 1u);
    EXPECT_EQ(store.arrayAt(popped, 0).numberValue(), 5.0);
}

TEST(StoreArray, OverwriteReleasesPreviousOnDrain) {
    Store store;
    const Value a = store.makeArray(1);
    store.arrayPush(a, store.makeArray(0));
    store.setGlobal("rows", a);
    store.drainPending();
    EXPECT_TRUE(store.arraySet(store.global("rows"), 0, Value::number(1)));
    store.drainPending();
    EXPECT_EQ(store.arrayAt(store.global("rows"), 0).numberValue(), 1.0);
}
```

- [ ] **Шаг 2: прогнать и убедиться, что не собирается**

Run: `cmake --build build-dbg -j8`
Expected: FAIL — `no member named 'drainPending' in 'CS::Store'`.

- [ ] **Шаг 3: убрать пулы массивов из `Store`**

В `core/src/store.hpp`:
- удалить объявление `detail::ArrayRep` и члены `pool_`, `arrays_`;
- удалить закрытый `growArray`;
- добавить член и метод:

```cpp
    /// Отложенное освобождение: ссылки, которые надо отпустить на ближайшей
    /// границе операции.
    ///
    /// Сюда попадает ссылка новорождённого узла и всякая вытесненная ссылка —
    /// перезаписанная ячейка, снятый pop, заменённое значение глобальной
    /// переменной. Освобождать вытесненное на месте нельзя: вычислитель держит
    /// прочитанные значения голым Value без всякого RAII, и `x = a.pop()`
    /// уронил бы счётчик в ноль ровно между чтением и записью.
    ///
    /// Корнем список не является: он держит ссылку до ближайшего слива, а не
    /// до смерти хранилища. Держи он до смерти — корнем стало бы всё
    /// созданное, и освобождать было бы нечего.
    std::vector<detail::Node *> pending_;
```

```cpp
    /// Отпускает всё, что накопил список отложенного освобождения.
    /// Зовёт граница операции (Context::beginOperation).
    void drainPending() noexcept;
```

В `core/src/store.cpp`:

```cpp
void Store::drainPending() noexcept {
    // Освобождение узла может дописать в pending_ — этого не бывает сегодня,
    // но обход по индексу переживёт и такое, а range-for по вектору нет.
    for (std::size_t i = 0; i < pending_.size(); ++i) {
        detail::release(pending_[i]);
    }
    pending_.clear();
}
```

Деструктор `Store` сливает список — иначе созданное и никем не подобранное течёт:

```cpp
Store::~Store() { drainPending(); }
```

- [ ] **Шаг 4: перевести массивы на узлы**

В `core/src/store.cpp` заменить реализации:

```cpp
/// Взять ссылку, если значение вообще ссылается счётчиком.
static void retainValue(Value v) noexcept {
    if (v.addressesStore() && v.region() == Value::Region::Counted) {
        detail::retain(v.node());
    }
}

/// Отдать ссылку в список отложенного освобождения.
void Store::defer(Value v) {
    if (v.addressesStore() && v.region() == Value::Region::Counted) {
        pending_.push_back(v.node());
    }
}

Value Store::makeArray(std::uint32_t capacity) {
    detail::ArrayNode *node = detail::makeArrayNode(capacity);
    pending_.push_back(node);          // ссылка создателя
    return Value::array(node);
}

std::uint32_t Store::arrayCount(Value a) const noexcept {
    assert(a.kind() == Value::Kind::Array);
    return static_cast<std::uint32_t>(
        static_cast<const detail::ArrayNode *>(a.node())->items.size());
}

Value Store::arrayAt(Value a, std::uint32_t index) const noexcept {
    assert(a.kind() == Value::Kind::Array);
    const detail::ArrayNode *node = static_cast<const detail::ArrayNode *>(a.node());
    if (index >= node->items.size()) { return Value::null(); }
    return node->items[index];
}

bool Store::arraySet(Value a, std::uint32_t index, Value v) noexcept {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayNode *node = static_cast<detail::ArrayNode *>(a.node());
    if (index >= node->items.size()) { return false; }
    retainValue(v);
    defer(node->items[index]);         // вытесненное — не на месте
    node->items[index] = v;
    return true;
}

void Store::arrayPush(Value a, Value v) {
    assert(a.kind() == Value::Kind::Array);
    retainValue(v);
    static_cast<detail::ArrayNode *>(a.node())->items.push_back(v);
}

bool Store::arrayPop(Value a, Value *out) noexcept {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayNode *node = static_cast<detail::ArrayNode *>(a.node());
    if (node->items.empty()) { return false; }
    const Value last = node->items.back();
    node->items.pop_back();
    if (out != nullptr) { *out = last; }
    defer(last);                       // ссылка ячейки уходит в список
    return true;
}
```

Объявить `void defer(Value v);` в закрытой части `store.hpp`.

Утверждение `sameRegion` из этих методов уходит: у агрегата регион всегда `Counted`, а личности хранилища у узла нет вовсе — она ему больше не нужна.

Из `clear()` убрать `pool_.clear()` и `arrays_.clear()`, добавить `drainPending()` первой строкой — временный регион сбрасывает и байты, и накопленные ссылки.

Из `bytesUsed`/`bytesReserved` убрать слагаемые `pool_` и `arrays_`.

- [ ] **Шаг 5: связать границу операции**

В `core/src/context.hpp` расширить `beginOperation`:

```cpp
    void beginOperation() noexcept {
        exec_.scratch.clear();
        store_.drainPending();
    }
```

Дописать в комментарий к `beginOperation`, почему слив идёт в начале операции, а не в конце: результат вычисления вправе быть узлом, чья единственная ссылка — в списке, и вызывающий читает его сразу после возврата. Слив на выходе отнял бы результат ровно тогда, когда он нужен. Правило совпадает с прежним контрактом: значение годно до следующей операции.

- [ ] **Шаг 6: прогнать тесты**

Run: `cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests && ./build-dbg/cli/tests/chupa_cli_tests`
Expected: PASS. Тесты `StoreArray.*` из шага 1 проходят; прежние тесты массивов не меняются.

- [ ] **Шаг 7: прогнать под санитайзером адресов**

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-omit-frame-pointer"
cmake --build build-asan -j8 && ./build-asan/core/tests/chupascript_tests
```
Expected: PASS без сообщений санитайзера. Именно здесь ловится и утечка узла, и обращение к освобождённому.

- [ ] **Шаг 8: коммит**

```bash
git add core/src/store.hpp core/src/store.cpp core/src/context.hpp core/tests/store_test.cpp
git commit -F - <<'MSG'
perf: push переносил массив в хвост пула, а прежний диапазон бросал мусором

Массив лежал сплошным диапазоном, дописать в хвост было нельзя. Двести
push над массивом в тысячу элементов раздували пул до шести мегабайт.
Элементы переезжают в узел; вытесненное уходит в список отложенного
освобождения, который сливает граница операции.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

## Task 5: Объекты на узлах, ключи через таблицу

**Files:**
- Modify: `core/src/store.hpp`, `core/src/store.cpp`, `core/src/execution.hpp`
- Modify: `core/tests/store_test.cpp`

**Interfaces:**
- Consumes: `detail::ObjectNode`, `Entry`, `makeObjectNode` (Task 2); `KeyTable` (Task 1); `Store::defer`, `Store::drainPending` (Task 4).
- Produces: `Store::keys() -> KeyTable *`; конструктор `Store(Value::Region region, KeyTable *keys)` — при `keys == nullptr` хранилище заводит свою таблицу.

- [ ] **Шаг 1: написать падающий тест**

Дописать в `core/tests/store_test.cpp`:

```cpp
TEST(StoreObject, RepeatedKeyIsStoredOnce) {
    Store store;
    for (int i = 0; i < 100; ++i) {
        const Value o = store.makeObject(1);
        store.objectSet(o, "name", Value::number(i));
    }
    EXPECT_EQ(store.keys()->count(), 1u);
}

TEST(StoreObject, KeysSurviveTheStoreThroughAnEscapedObject) {
    // Ключи живут в таблице, таблицу держит узел-объект: объект, переживший
    // хранилище, ключи не теряет.
    Value escaped = Value::null();
    {
        Store store;
        escaped = store.makeObject(1);
        store.objectSet(escaped, "name", Value::number(1));
        CS::detail::retain(escaped.node());   // ссылка «хоста»
    }
    const CS::detail::ObjectNode *node =
        static_cast<const CS::detail::ObjectNode *>(escaped.node());
    ASSERT_EQ(node->entries.size(), 1u);
    EXPECT_EQ(node->keys->bytes(node->entries[0].key), "name");
    CS::detail::release(escaped.node());
}

TEST(StoreObject, EnumerationStaysOrderedByKeyBytes) {
    Store store;
    const Value o = store.makeObject(3);
    store.objectSet(o, "b", Value::number(2));
    store.objectSet(o, "a", Value::number(1));
    store.objectSet(o, "c", Value::number(3));
    EXPECT_EQ(store.objectKeyAt(o, 0), "a");
    EXPECT_EQ(store.objectKeyAt(o, 1), "b");
    EXPECT_EQ(store.objectKeyAt(o, 2), "c");
}

TEST(StoreObject, ReadingAbsentKeyDoesNotInternIt) {
    Store store;
    const Value o = store.makeObject(1);
    EXPECT_EQ(store.objectGet(o, "нет").kind(), Value::Kind::Null);
    EXPECT_FALSE(store.objectHas(o, "нет"));
    EXPECT_EQ(store.keys()->count(), 0u);
}

TEST(StoreObject, OverwriteReleasesPreviousOnDrain) {
    Store store;
    const Value o = store.makeObject(1);
    store.objectSet(o, "row", store.makeArray(0));
    store.setGlobal("state", o);
    store.drainPending();
    store.objectSet(store.global("state"), "row", Value::number(1));
    store.drainPending();
    EXPECT_EQ(store.objectGet(store.global("state"), "row").numberValue(), 1.0);
}
```

- [ ] **Шаг 2: прогнать и убедиться, что не собирается**

Run: `cmake --build build-dbg -j8`
Expected: FAIL — `no member named 'keys' in 'CS::Store'`.

- [ ] **Шаг 3: перевести объекты на узлы**

В `core/src/store.hpp`:
- удалить объявления `detail::ObjectRep`, `detail::Entry` (последнее переезжает в `node.hpp`) и члены `objects_`, `entries_`;
- удалить закрытый `growObject`;
- сменить конструктор и добавить доступ к таблице:

```cpp
    /// keys — таблица имён полей. nullptr означает «завести свою»; так
    /// собирают одиночное хранилище тесты и оболочка. Временный регион
    /// получает таблицу постоянного: агрегат, созданный вычислением и попавший
    /// в глобальную переменную, не копируется, и переводить номера ключей было
    /// бы негде.
    explicit Store(Value::Region region = Value::Region::Counted,
                   KeyTable *keys = nullptr);

    [[nodiscard]] KeyTable *keys() const noexcept { return keys_; }
```

```cpp
    KeyTable *keys_;   // удерживается ссылкой
```

В `core/src/store.cpp`:

```cpp
Store::Store(Value::Region region, KeyTable *keys)
    : region_(region), keys_(keys != nullptr ? keys : KeyTable::create()) {
    if (keys != nullptr) { KeyTable::retain(keys_); }
}

Store::~Store() {
    drainPending();
    KeyTable::release(keys_);
}

Value Store::makeObject(std::uint32_t capacity) {
    detail::ObjectNode *node = detail::makeObjectNode(keys_, capacity);
    pending_.push_back(node);          // ссылка создателя
    return Value::object(node);
}

/// Место ключа в записях объекта, а если ключа нет — место вставки.
/// Записи упорядочены по байтам ключа: порядок перечисления наружу формально
/// не обещан (semantics.md §2.1), но фактически он байтовый, и на нём стоит
/// вывод printValue с золотыми тестами.
std::uint32_t Store::findKey(const detail::ObjectNode &node, std::string_view key,
                             bool *found) const noexcept {
    std::uint32_t low = 0;
    std::uint32_t high = static_cast<std::uint32_t>(node.entries.size());
    while (low < high) {
        const std::uint32_t mid = low + (high - low) / 2;
        const std::string_view candidate = keys_->bytes(node.entries[mid].key);
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

std::uint32_t Store::objectCount(Value o) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    return static_cast<std::uint32_t>(
        static_cast<const detail::ObjectNode *>(o.node())->entries.size());
}

Value Store::objectGet(Value o, std::string_view key) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectNode &node =
        *static_cast<const detail::ObjectNode *>(o.node());
    bool found = false;
    const std::uint32_t at = findKey(node, key, &found);
    if (!found) { return Value::null(); }
    return node.entries[at].value;
}

bool Store::objectHas(Value o, std::string_view key) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    bool found = false;
    findKey(*static_cast<const detail::ObjectNode *>(o.node()), key, &found);
    return found;
}

std::string_view Store::objectKeyAt(Value o, std::uint32_t i) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectNode &node =
        *static_cast<const detail::ObjectNode *>(o.node());
    if (i >= node.entries.size()) { return {}; }
    return keys_->bytes(node.entries[i].key);
}

Value Store::objectValueAt(Value o, std::uint32_t i) const noexcept {
    assert(o.kind() == Value::Kind::Object);
    const detail::ObjectNode &node =
        *static_cast<const detail::ObjectNode *>(o.node());
    if (i >= node.entries.size()) { return Value::null(); }
    return node.entries[i].value;
}

void Store::objectSet(Value o, std::string_view key, Value v) {
    assert(o.kind() == Value::Kind::Object);
    detail::ObjectNode &node = *static_cast<detail::ObjectNode *>(o.node());

    bool found = false;
    const std::uint32_t at = findKey(node, key, &found);
    retainValue(v);
    if (found) {
        defer(node.entries[at].value);
        node.entries[at].value = v;
        return;
    }
    // Интернируется только тот ключ, который правда заводится: чтение
    // отсутствующего имени таблицу не засоряет.
    node.entries.insert(node.entries.begin() + at,
                        detail::Entry{keys_->intern(key), v});
}
```

Сигнатуру `findKey` в `store.hpp` поменять на `const detail::ObjectNode &`.

Из `clear()` убрать `objects_.clear()` и `entries_.clear()`; из `bytesUsed`/`bytesReserved` — соответствующие слагаемые.

- [ ] **Шаг 4: связать таблицу временного региона с постоянной**

В `core/src/execution.hpp` заменить объявление члена:

```cpp
    /// Временный регион: значения, созданные вычислением.
    ///
    /// Таблица имён у него общая с постоянным хранилищем — иначе объект,
    /// собранный вычислением и попавший в глобальную переменную, приехал бы с
    /// номерами ключей из чужой таблицы.
    Store scratch;
```

и завести в конструкторе:

```cpp
    explicit Execution(Store &persistent) noexcept
        : scratch(Value::Region::Scratch, persistent.keys()),
          persistent_(persistent) {}
```

Порядок объявления членов подогнать под порядок инициализации: `scratch` объявлен до `persistent_`, а `persistent_` нужен в списке инициализации раньше — поэтому в списке `scratch` идёт первым и берёт `persistent` прямо из параметра, а не из члена.

- [ ] **Шаг 5: прогнать тесты**

Run: `cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests && ./build-dbg/cli/tests/chupa_cli_tests`
Expected: PASS. В частности проходят золотые тесты `printer_test.cpp` — порядок перечисления не изменился.

- [ ] **Шаг 6: прогнать под санитайзером**

Run: `cmake --build build-asan -j8 && ./build-asan/core/tests/chupascript_tests`
Expected: PASS без сообщений.

- [ ] **Шаг 7: коммит**

```bash
git add core/src/store.hpp core/src/store.cpp core/src/execution.hpp core/tests/store_test.cpp
git commit -F - <<'MSG'
perf: каждый объект носил байты своих ключей, хотя имена у всех одни и те же

Пары переезжают в узел, а ключ становится четырёхбайтовым номером в общей
таблице контекста. Тысяча объектов по три поля хранит три имени вместо трёх
тысяч; таблицу держит узел, поэтому уехавший к хосту объект ключи не теряет.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

## Task 6: Строки — узел в `Counted`, смещение в `Scratch`, литералы указателем

**Files:**
- Modify: `core/src/store.hpp`, `core/src/store.cpp`
- Modify: `core/src/ast.hpp`, `core/src/ast.cpp`, `core/src/compile.cpp`, `core/src/eval.cpp`
- Modify: `core/tests/store_test.cpp`, `core/tests/ast_test.cpp`

**Interfaces:**
- Consumes: `detail::StrNode`, `makeStrNode` (Task 2); `Value::string(detail::StrNode *, std::uint32_t)` (Task 3).
- Produces: `Store::materialize(std::string_view) -> Value` (всегда узел); `Store::internLiteral(std::string_view) -> detail::StrNode *` (оснастка, живёт до смерти хранилища); `Ast::stringLiteral(NodeId) -> detail::StrNode *`, `Ast::setStringLiteral(NodeId, detail::StrNode *)`. Удаляются `Store::stringParts`, `Store::stringAt`, `Store::promoteDeep`, `Store::promoteInto`, `detail::Promoted`, `Store::writable`, `Store::sameRegion`.

**Правило, ради которого всё это.** Строка меняет представление не когда её вычислили, а когда её положили внутрь того, что умеет уехать. Промежуточная строка — смещение в арену операции; попав в агрегат или в глобальную переменную, она обязана стать узлом. Без этого `:set g = [a + b]` оставил бы в пережившем операцию узле смещение в сброшенную арену.

- [ ] **Шаг 1: написать падающий тест**

Дописать в `core/tests/store_test.cpp`:

```cpp
TEST(StorePromote, ScratchStringBecomesNodeInsideAggregate) {
    Store persistent;
    Store scratch(Value::Region::Scratch, persistent.keys());
    const Value temp = scratch.makeString("привет");
    EXPECT_EQ(temp.region(), Value::Region::Scratch);

    const Value kept = persistent.promote(scratch, temp);
    EXPECT_EQ(kept.region(), Value::Region::Counted);
    EXPECT_EQ(persistent.string(kept), "привет");

    scratch.clear();   // арена сброшена
    EXPECT_EQ(persistent.string(kept), "привет");
}

TEST(StorePromote, ScratchStringBecomesNodeEvenInsideScratchAggregate) {
    // Агрегат временного региона — такой же узел и умеет уехать: смещения в
    // сбрасываемую арену внутри него быть не должно.
    Store persistent;
    Store scratch(Value::Region::Scratch, persistent.keys());
    const Value a = scratch.makeArray(1);
    scratch.arrayPush(a, scratch.promote(scratch, scratch.makeString("x")));
    EXPECT_EQ(scratch.arrayAt(a, 0).region(), Value::Region::Counted);
}

TEST(StorePromote, AggregateIsNotCopied) {
    // Ради этого всё и затевалось: агрегат проходит границу ссылкой.
    Store persistent;
    Store scratch(Value::Region::Scratch, persistent.keys());
    const Value a = scratch.makeArray(1);
    EXPECT_TRUE(persistent.promote(scratch, a).sameAggregate(a));
}

TEST(StorePromote, ScalarPassesThrough) {
    Store persistent;
    Store scratch(Value::Region::Scratch, persistent.keys());
    EXPECT_EQ(persistent.promote(scratch, Value::number(1.0)).numberValue(), 1.0);
}

TEST(StoreString, CountedStoreMakesNodes) {
    Store store;
    EXPECT_EQ(store.makeString("a").region(), Value::Region::Counted);
}

TEST(StoreString, ScratchStoreMakesOffsets) {
    Store persistent;
    Store scratch(Value::Region::Scratch, persistent.keys());
    EXPECT_EQ(scratch.makeString("a").region(), Value::Region::Scratch);
}
```

Дописать в `core/tests/ast_test.cpp`:

```cpp
TEST(AstStringLiteral, KeepsNodePointer) {
    CS::Store store;
    CS::Ast ast;
    // Узлы заводятся единственной фабрикой Ast::add(const Node &) — так же,
    // как это делают уже существующие тесты этого файла.
    CS::Ast::Node raw;
    raw.kind = CS::NodeKind::String;
    raw.offset = 0;
    raw.textLength = 3;
    const CS::NodeId node = ast.add(raw);

    CS::detail::StrNode *literal = store.internLiteral("abc");
    ast.setStringLiteral(node, literal);
    EXPECT_TRUE(ast.hasStringLiteral(node));
    EXPECT_EQ(ast.stringLiteral(node), literal);
    EXPECT_EQ(ast.stringLiteral(node)->view(), "abc");
}
```

Если `Ast::Node` закрыт для тестов, взять форму заведения узла из соседних тестов `core/tests/ast_test.cpp` — важно здесь только то, что литерал кладётся и достаётся указателем.

- [ ] **Шаг 2: прогнать и убедиться, что не собирается**

Run: `cmake --build build-dbg -j8`
Expected: FAIL — `no member named 'materialize' in 'CS::Store'`, `no member named 'internLiteral'`.

- [ ] **Шаг 3: перевести строки `Store` на узлы**

В `core/src/store.cpp`:

```cpp
Value Store::makeString(std::string_view bytes) {
    // Разное поведение по региону — не исключение, а вся суть деления:
    // промежуточная строка живёт в арене операции и стоит 0.22 нс, строка,
    // которой предстоит пережить операцию, — узел и стоит аллокацию.
    if (region_ == Value::Region::Scratch) {
        const std::uint32_t offset = appendText(bytes);
        return Value::string(offset, static_cast<std::uint32_t>(bytes.size()),
                             Value::Region::Scratch);
    }
    return materialize(bytes);
}

Value Store::materialize(std::string_view bytes) {
    detail::StrNode *node = detail::makeStrNode(bytes);
    pending_.push_back(node);          // ссылка создателя
    return Value::string(node, node->len);
}

detail::StrNode *Store::internLiteral(std::string_view bytes) {
    // Оснастка: литерал часть программы, а не создаваемое значение. Он живёт
    // до смерти хранилища и в список отложенного освобождения не попадает —
    // иначе первая же граница операции забрала бы его у дерева разбора.
    detail::StrNode *node = detail::makeStrNode(bytes);
    literals_.push_back(node);
    return node;
}

std::string_view Store::string(Value v) const noexcept {
    assert(v.kind() == Value::Kind::String);
    if (v.region() == Value::Region::Scratch) { return textAt(v.index(), v.length()); }
    return static_cast<const detail::StrNode *>(v.node())->view();
}

Value Store::promote(const Store &from, Value v) {
    // Единственный переезд во всём движке. Агрегат и узел-строка проходят как
    // есть — копирования агрегатов больше нет нигде.
    if (v.kind() != Value::Kind::String || v.region() != Value::Region::Scratch) {
        return v;
    }
    return materialize(from.string(v));
}
```

`promote` перестаёт быть встроенным в заголовке: быстрый путь теперь две проверки полей, а медленный — не рекурсия, и разносить их незачем. Комментарий о встраивании (замер 23.6 против 20.4 нс) снять вместе с ним.

Удалить: `promoteDeep`, `promoteInto`, `detail::Promoted`, `writable`, `sameRegion`, `stringParts`, `stringAt`. Все утверждения `assert(writable(v) && ...)` в `arraySet`, `arrayPush`, `objectSet`, `setGlobal` заменить на:

```cpp
    assert((v.kind() != Value::Kind::String || v.region() != Value::Region::Scratch) &&
           "строка временного региона не материализована: нужен promote");
```

Добавить член `std::vector<detail::StrNode *> literals_;` и отпускать его в деструкторе:

```cpp
Store::~Store() {
    drainPending();
    for (detail::StrNode *literal : literals_) { detail::release(literal); }
    KeyTable::release(keys_);
}
```

- [ ] **Шаг 4: перевести литерал в дереве на указатель**

В `core/src/ast.hpp` заменить член объединения:

```cpp
            /// String, после укладки: узел строки, которым владеет хранилище
            /// контекста. Восемь байт — ровно столько же, сколько занимала
            /// пара «смещение, длина», так что sizeof(Node) == 24 держится.
            detail::StrNode *literal;
```

и объявить `namespace detail { struct StrNode; }` вперёд.

В `core/src/ast.cpp`:

```cpp
detail::StrNode *Ast::stringLiteral(NodeId node) const noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::String &&
           "уложенный литерал бывает только у строкового литерала");
    assert(hasStringLiteral(node) && "литерал обязан быть уложен");
    return nodes_[node].payload.literal;
}

void Ast::setStringLiteral(NodeId node, detail::StrNode *literal) noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::String &&
           "уложенный литерал бывает только у строкового литерала");
    nodes_[node].payload.literal = literal;
    nodes_[node].flags |= kFlagLiteral;
}
```

В `core/src/compile.cpp:33` — `internStringLiterals`:

```cpp
    for (NodeId node = 1; node <= root; ++node) {
        if (ast.kind(node) != NodeKind::String) { continue; }
        ast.setStringLiteral(node,
                             store.internLiteral(literalText(ast, node, source, scratch)));
    }
```

Комментарий о том, почему узел хранит не `Value`, переписать: причина прежняя — шестнадцать байт не влезли бы, — но раскладывать значение больше не нужно, узел строки самодостаточен.

В `core/src/eval.cpp:264` — чтение литерала:

```cpp
            detail::StrNode *literal = ast.stringLiteral(node);
            // Литерал — часть программы, а не создаваемое значение: ссылку на
            // него держит хранилище контекста до самой своей смерти.
            *out = Value::string(literal, literal->len);
            return true;
```

В `core/src/eval.cpp:363` — ключ объектного литерала:

```cpp
                detail::StrNode *key = ast.stringLiteral(ast.child(node, i));
                exec.scratch.objectSet(object, key->view(), value);
```

Прежний комментарий про «пулы разные, алиас не при чём» снять: пулов больше нет, байты ключа берутся прямо из узла литерала и уезжают в таблицу имён.

- [ ] **Шаг 5: прогнать тесты**

Run: `cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests && ./build-dbg/cli/tests/chupa_cli_tests`
Expected: PASS. Ожидания ни одного теста не менялись.

- [ ] **Шаг 6: прогнать под санитайзером**

Run: `cmake --build build-asan -j8 && ./build-asan/core/tests/chupascript_tests`
Expected: PASS без сообщений.

- [ ] **Шаг 7: коммит**

```bash
git add core/src/store.hpp core/src/store.cpp core/src/ast.hpp core/src/ast.cpp core/src/compile.cpp core/src/eval.cpp core/tests/store_test.cpp core/tests/ast_test.cpp
git commit -F - <<'MSG'
refactor: promote копировал агрегат вглубь, хотя копировать надо было строку

Продвижение обходило массив с объектом рекурсивно, держало таблицу
пересылки от повторов и циклов и делало копию всего дерева. Агрегат теперь
узел и проходит границу ссылкой; переездом остаётся один случай — байты
строки из арены операции в собственный узел.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

## Task 7: Места укладки, границы операции у хостовых сеттеров, `Context::storeOf`

**Files:**
- Modify: `core/src/eval.cpp`, `core/src/builtin.cpp`, `core/src/data.cpp`, `core/src/c_api.cpp`, `core/src/context.hpp`, `core/src/execution.hpp`
- Modify: `core/tests/context_test.cpp`, `core/tests/c_api_test.cpp`

**Interfaces:**
- Consumes: `Store::promote` (Task 6), `Store::drainPending` (Task 4).
- Produces: `Context::beginOperation()` доступен хостовым сеттерам через `Context::setGlobal(std::string_view, ...)`; `Context::storeOf` удалён.

- [ ] **Шаг 1: написать падающий тест**

Дописать в `core/tests/context_test.cpp`:

```cpp
TEST(ContextMemory, ArrayInGlobalOutlivesManyOperations) {
    // Массив укоренён глобальной переменной: сколько бы операций ни прошло,
    // слив списка отложенного освобождения его не тронет.
    CS::Context ctx;
    CS::Diagnostic diag;
    CS::Script script;
    ASSERT_EQ(CS::Script::compile("rows = [1, 2, 3];", ctx.store(), &script, &diag, 1), 0u);
    ASSERT_TRUE(ctx.run(script, diag));

    CS::Expression expr;
    ASSERT_EQ(CS::Expression::compile("rows[2]", ctx.store(), &expr, &diag, 1), 0u);
    for (int i = 0; i < 100; ++i) {
        double got = 0.0;
        ASSERT_EQ(ctx.evalNumber(expr, &got, diag), CS::EvalStatus::Ok);
        EXPECT_EQ(got, 3.0);
    }
}

TEST(ContextMemory, ReassignedGlobalDoesNotGrowForever) {
    // Прежний массив вытесняется и освобождается точно — раньше он оставался
    // в пуле навсегда. Меряется счётчиком живых узлов, а не bytesReserved:
    // память узлов хранилищу не принадлежит, и его метрика её не видит.
    CS::Context ctx;
    CS::Diagnostic diag;
    CS::Script script;
    ASSERT_EQ(CS::Script::compile("rows = [1, 2, 3];", ctx.store(), &script, &diag, 1), 0u);
    ASSERT_TRUE(ctx.run(script, diag));

    const std::size_t after_first = CS::detail::liveNodeCount();
    for (int i = 0; i < 200; ++i) { ASSERT_TRUE(ctx.run(script, diag)); }
    // Двухсот массивов подряд быть не должно: живым остаётся ровно последний.
    // Допуск в один узел — на тот, что ещё держит список отложенного
    // освобождения до ближайшей границы.
    EXPECT_LE(CS::detail::liveNodeCount(), after_first + 1);
}

TEST(ContextMemory, StringInsideGlobalArraySurvivesTheOperation) {
    CS::Context ctx;
    CS::Diagnostic diag;
    CS::Script script;
    ASSERT_EQ(CS::Script::compile("rows = [\"a\" + \"b\"];", ctx.store(), &script, &diag, 1),
              0u);
    ASSERT_TRUE(ctx.run(script, diag));

    CS::Expression expr;
    ASSERT_EQ(CS::Expression::compile("rows[0]", ctx.store(), &expr, &diag, 1), 0u);
    std::string_view got;
    ASSERT_EQ(ctx.evalString(expr, &got, diag), CS::EvalStatus::Ok);
    EXPECT_EQ(got, "ab");
}
```

- [ ] **Шаг 2: прогнать и убедиться, что падает**

Run: `cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests --gtest_filter='ContextMemory.*'`
Expected: FAIL — `ContextMemory.StringInsideGlobalArraySurvivesTheOperation` падает либо срабатывает утверждение «строка временного региона не материализована»: `eval.cpp:363` кладёт значение в объект без `promote`.

- [ ] **Шаг 3: расставить `promote` во всех местах укладки**

Найти их: `grep -rn "arrayPush\|arraySet\|objectSet\|setGlobal" core/src | grep -v store.cpp`

Каждое место обязано пропустить укладываемое значение через `promote` того хранилища, которому принадлежит **контейнер**:

- `core/src/eval.cpp:363` (значение в литерал объекта) — `exec.scratch.promote(exec.storeOf(value), value)`;
- `core/src/eval.cpp:556` — уже зовёт `promote`, проверить, что источник берётся через `exec.storeOf(value)`;
- `core/src/eval.cpp:654` — добавить `promote`;
- элементы литерала массива (`NodeKind::Array` в `eval.cpp`) — добавить `promote`;
- `core/src/builtin.cpp:270` (`Push`) — уже зовёт `promote`, оставить;
- `core/src/data.cpp:87` — добавить `promote`; там источник и приёмник — одно хранилище, и вызов вырождается в проверку поля.

- [ ] **Шаг 4: дать хостовым сеттерам границу операции**

Хостовые сеттеры создают узлы вне вычисления, и ссылку создателя из `pending_` надо кому-то слить. В `core/src/context.hpp` добавить:

```cpp
    /// Запись глобальной переменной от хоста. Операция, а не голая запись:
    /// созданные здесь узлы попадают в список отложенного освобождения, и без
    /// границы он рос бы до конца жизни контекста.
    void setGlobal(std::string_view name, Value v) {
        beginOperation();
        store_.setGlobal(name, store_.promote(exec_.storeOf(v), v));
    }
```

В `core/src/c_api.cpp` перевести `chupa_context_set*` на `Context::setGlobal` вместо прямого `store().setGlobal(...)`.

- [ ] **Шаг 5: убрать `Context::storeOf`**

Удалить метод из `core/src/context.hpp` вместе с комментарием: наружу отдаётся либо скаляр, либо `Counted`-значение, а такому хранилище не нужно. Поправить места, где его звали (`grep -rn "storeOf" cli core/tests`) — `Execution::storeOf` остаётся и им пользуется вычислитель, но снаружи контекста он больше не нужен.

В комментарии `Execution::storeOf` сузить роль: агрегат теперь самодостаточен, разрешать по региону надо только промежуточную строку.

- [ ] **Шаг 6: прогнать все тесты**

Run: `cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests && ./build-dbg/cli/tests/chupa_cli_tests`
Expected: PASS полностью.

- [ ] **Шаг 7: прогнать под санитайзером**

Run: `cmake --build build-asan -j8 && ./build-asan/core/tests/chupascript_tests && ./build-asan/cli/tests/chupa_cli_tests`
Expected: PASS без сообщений.

- [ ] **Шаг 8: коммит**

```bash
git add core/src/eval.cpp core/src/builtin.cpp core/src/data.cpp core/src/c_api.cpp core/src/context.hpp core/src/execution.hpp core/tests/context_test.cpp core/tests/c_api_test.cpp
git commit -F - <<'MSG'
fix: значение временного региона попадало в переживающий его узел

Промежуточная строка ложилась в агрегат как смещение в арену, которую
граница операции сбрасывает. Все места укладки проводят значение через
promote; хостовые сеттеры становятся операциями, иначе список отложенного
освобождения рос бы до конца жизни контекста.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

## Task 8: Замер и сравнение с базой

**Files:**
- Modify: `benchmarks/store_benchmark.cpp` — привести к новому API, если он там задет
- Create: `docs/benchmarks/2026-08-19-memory-model.md`

**Interfaces:**
- Consumes: всё предыдущее.
- Produces: отчёт со сравнением до и после.

**Честность результата — предмет этой задачи, а не побочный эффект.** База снята до начала работ; порог шума назначен по базе; ожидаемые проигрыши названы в спеке до прогона. Ни одно из трёх задним числом не меняется.

- [ ] **Шаг 1: собрать релиз с бенчмарками**

```bash
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON
cmake --build build-rel -j8
```
Expected: сборка без ошибок. Если `benchmarks/store_benchmark.cpp` лезет в удалённые методы (`stringParts`, `stringAt`), поправить форму вызова, но не то, что бенчмарк меряет.

- [ ] **Шаг 2: убедиться, что набор бенчмарков не поредел**

Run: `./build-rel/benchmarks/chupascript_benchmarks --benchmark_list_tests | wc -l`
Expected: 76 — столько же, сколько в базе. Меньше означает, что бенчмарк выпал, и сравнение будет неполным.

- [ ] **Шаг 3: снять замер тем же способом, что базу**

```bash
./build-rel/benchmarks/chupascript_benchmarks \
  --benchmark_repetitions=11 \
  --benchmark_report_aggregates_only=true \
  --benchmark_format=json \
  --benchmark_out=after.json
```
Ожидаемое время прогона — около десяти минут. Машина та же, что у базы; иначе сравнение недействительно и `bench-compare.py` вернёт код 2.

- [ ] **Шаг 4: сравнить**

База снята до начала работ и лежит в каталоге-черновике сессии:
`/private/tmp/claude-502/-Users-roman-putincev-ChupaScript/83e5bcf8-48c2-4883-adc2-7c57ce346ee5/scratchpad/bench/before.json`.
Если её там уже нет, базу надо снять заново с коммита `4a13014^` тем же
способом — но тогда и порог шума пересчитать по новой базе.

Run: `python3 tools/bench-compare.py <before.json> after.json --threshold 10`
Expected: отчёт. Код возврата 1 означает деградацию выше порога — это не повод править порог, это повод разобраться.

- [ ] **Шаг 5: записать отчёт**

`docs/benchmarks/2026-08-19-memory-model.md` — таблица «до / после / разница» по всем 76 бенчмаркам, и отдельно:
- **что подорожало** — с прямым указанием, входило ли это в список ожидаемых проигрышей из спеки §5. Подорожавшее сверх списка называется как неожиданное, а не объясняется задним числом;
- **что подешевело**;
- **что не изменилось** — всё, что уложилось в порог шума 10%, объявляется неизменившимся, а не выигрышем.

- [ ] **Шаг 6: коммит**

```bash
git add docs/benchmarks/2026-08-19-memory-model.md benchmarks/
git commit -F - <<'MSG'
docs: замер модели памяти до и после перехода на счётчик ссылок

Тот же набор из 76 бенчмарков, та же машина, 11 повторов. Порог шума 10%
назначен по базе до начала работ, ожидаемые проигрыши названы в спеке до
прогона.

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
```

---

## Что этот план не делает

- **Циклы не собираются.** `push(users, users)` даёт счётчик, который не дойдёт до нуля (спека §6.1). Решать отдельно.
- **Ключ на компиляции не разрешается.** Интернирование это открывает, но узел дерева номер ключа не получает (спека §6.2, [B53]).
- **C API агрегатов не появляется.** [B59] эта работа делает возможным, но `chupascript.h` не трогает. Саму запись [B59] после этой работы надо переписать: её раздел «Почему не подсчёт ссылок» противоречит принятой модели.
- **Гранулярность `notifyRedraw` не меняется** (спека §6.3).
