# Кэш выражений: эпоха по адресу — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** движок начинает отвечать на вопрос «менялось ли то, от чего зависит это выражение», чтобы хост переиспользовал уже собранное значение вместо пересчёта всех props на каждый кадр.

**Architecture:** один монотонный счётчик на контекст («лента»); эпоха из него лежит в трёх местах — ячейка глобальной переменной, `ArrayBox`, `ObjectBox`; вычисление попутно записывает адреса эпох тех мест, через которые прошло, и отдаёт их наружу вместе с коробками-владельцами; читатель хранит снимок у себя и сравнивает сам, не входя в C на попадании.

**Tech Stack:** C++17, CMake 3.20, GoogleTest 1.15.2, Google Benchmark 1.9.1, публичный C-заголовок `core/include/chupascript/chupascript.h`, обёртка Swift (SPM, `Sources/ChupaScript`).

**Spec:** `docs/superpowers/specs/2026-08-20-expression-cache-design.md`

## Global Constraints

- **`CHUPA_MAX_DEPS = 4` — часть ABI.** Обёртки разворачивают по этому числу своё хранилище. Меняется один раз, по замеру задачи 10, и дальше не двигается (спека §2.3, §9).
- **Ширина эпохи — 64 бита, `typedef uint64_t ChupaEpoch`.** Не настройка: у неё ровно одно безопасное значение (§2.4.1). `uint32` запрещён — переполнение за сутки непрерывной работы даёт ложное попадание.
- **Счётчик не атомарный.** Единица однопоточности — контекст (`chupascript.h`, threading contract), как и у `Box::rc`.
- **Инкремент живёт внутри мутаторов** `core/src/aggregate.hpp`, а не рядом с ними (§2.2). Дверей ровно пять: `Store::setGlobal`, `objectSet`, `arraySet`, `arrayPush`, `arrayPop`. Пропущенная дверь даёт молча застывший экран, а не медленную работу.
- **Через границу движка не идёт ни одной записи читателям.** Движок не знает ни одного читателя (§2.1).
- **Направление огрубления — только в сторону лишнего пересчёта.** Ложное попадание недопустимо ни при каких условиях; лишний промах терпим всегда.
- **Незаполненные зависимости смотрят на вечный ноль,** а не на `nullptr`: читатель складывает ровно `CHUPA_MAX_DEPS` слов без ветвлений и без чтения счётчика. Исключение — переполнение, см. задачу 7.
- **Стиль кода репозитория:** комментарий объясняет, *почему так, а не иначе*, и называет отвергнутую альтернативу; ссылки на спеку и `docs/backlog.md` по номеру. Комментарии на русском там, где вокруг русский, на английском — где английский; в одном файле не смешивать.
- **Сборка тестов:** `cmake -B build-dbg -DCMAKE_BUILD_TYPE=Debug && cmake --build build-dbg -j && ctest --test-dir build-dbg --output-on-failure`.
- **Сборка бенчмарков:** `cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON && cmake --build build-rel -j`.
- **Санитайзеры:** `tools/asan.sh` и `tools/tsan.sh` обязаны проходить перед задачей 11.

## Три решения, принятые сверх спеки

Спека их не содержит; они приняты при разборе 21.08.2026, и задача 11 вносит их в неё.

1. **`ChupaDep` вместо голого адреса.** §2.7 требует от читателя `retain` на коробки из набора, но §2.6 отдаёт только адреса эпох, а из адреса поля внутри коробки коробку не удержать. Поэтому зависимость — пара «адрес эпохи + коробка-владелец»; у зависимости-ячейки владелец пустой, ячейке смерть не грозит.
2. **`stamp` выкинут.** §2.4 объявляет сумму приёмом читающей стороны, а §2.6 возвращал её из движка — два правила об одном. Движок отдаёт эпохи, сводит их читатель: Swift и JVM суммой, JS сравнением половин (§3.3).
3. **Выражение с некэшируемым вызовом не кэшируется.** §2.3 обосновывает динамический набор детерминированностью выражений, но хост-функция вправе быть `CHUPA_FN_EFFECT_FREE` и **не** быть `CHUPA_FN_CACHEABLE` — `now()`, `screenWidth()` (`chupascript.h:252`, `docs/semantics.md:749`). У `format('${now()}')` набор зависимостей пуст, сумма нулевая, и по §2.6 такое выражение кэшировалось бы навсегда: часы на экране встали бы. Это дыра, а не лишний пересчёт. Закрывается статически, на компиляции.

---

## Структура файлов

**Создаются:**

| файл | ответственность |
|---|---|
| `core/src/epoch.hpp` | лента (`EpochClock`), вечный ноль, кусочное хранилище эпох ячеек (`EpochSlots`), тип зависимости (`Dep`), потолок |
| `core/src/epoch.cpp` | определение вечного нуля и тела `EpochSlots` |
| `core/tests/epoch_test.cpp` | лента, куски, стабильность адреса |
| `benchmarks/cache_benchmark.cpp` | шесть выражений × три режима (§5.1, §5.2) плюс вытеснение кэша (§5.3) |
| `Sources/ChupaScript/CachedExpression.swift` | читатель: снимок, удержание зависимостей, значение |
| `Tests/ChupaScriptTests/CachedExpressionTests.swift` | попадание, промах, смерть зависимости, некэшируемость |
| `docs/benchmarks/expression-cache-2026-08-21.md` | отчёт по правилам §5.4 |

**Изменяются:**

| файл | что в нём меняется |
|---|---|
| `core/src/box.hpp`, `core/src/box.cpp` | эпоха в заголовках `ArrayBox` и `ObjectBox`, рождение берёт номер из ленты |
| `core/src/aggregate.hpp` | пять мутаторов и два создателя принимают ленту; инкремент внутри |
| `core/src/store.hpp`, `core/src/store.cpp` | лента контекста, эпохи ячеек кусками, `setGlobal` поднимает эпоху |
| `core/src/execution.hpp` | набор зависимостей текущего вычисления, доступ к ленте |
| `core/src/eval.cpp` | запись зависимостей на `Identifier`, `Member`, `Index`; передача ленты мутаторам |
| `core/src/data.cpp`, `core/src/builtin.cpp` | передача ленты создателям и мутаторам |
| `core/src/callee.hpp`, `core/src/callee.cpp` | `cacheable` у разрешённого вызова |
| `core/src/ast.hpp`, `core/src/ast.cpp` | отметка «дерево некэшируемо» |
| `core/src/check.cpp` | отметка ставится на вызове без `CHUPA_FN_CACHEABLE` |
| `core/src/expression.hpp`, `core/src/expression.cpp` | `evalTracked` |
| `core/src/context.hpp` | `evalTracked` с границей операции |
| `core/include/chupascript/chupascript.h` | `ChupaEpoch`, `ChupaDep`, `CHUPA_MAX_DEPS`, `CHUPA_DEPS_OVERFLOW`, `chupa_expression_eval_tracked` |
| `core/src/c_api.cpp` | реализация двери |
| `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`, `benchmarks/CMakeLists.txt` | новые файлы |
| `core/tests/store_test.cpp`, `box_test.cpp`, `eval_test.cpp`, `c_api_test.cpp`, `expression_test.cpp`, `check_test.cpp` | новые сигнатуры и новые проверки |
| `docs/superpowers/specs/2026-08-20-expression-cache-design.md`, `docs/backlog.md` | три решения сверх спеки, закрытие B29 |

---

### Task 1: Лента, вечный ноль и кусочные эпохи ячеек

Отдельный заголовок, а не члены `Store`, потому что мутаторы `aggregate.hpp` намеренно ничего не знают о хранилище, а ленту принимать обязаны.

**Files:**
- Create: `core/src/epoch.hpp`
- Create: `core/src/epoch.cpp`
- Create: `core/tests/epoch_test.cpp`
- Modify: `core/CMakeLists.txt`, `core/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `core/src/value.hpp` (`CS::Value`)
- Produces: `CS::Epoch` (= `std::uint64_t`), `CS::kZeroEpoch`, `CS::EpochClock` с `tick()`/`now()`, `CS::EpochSlots` с `open(slot, birth)`/`bump(slot, value)`/`addressOf(slot)`/`at(slot)`, `CS::Dep{const Epoch *epoch; Value owner;}`, `CS::kMaxDeps == 4`, `CS::kDepsOverflow == 0xffffffffu`

- [ ] **Step 1: Написать падающий тест**

Создать `core/tests/epoch_test.cpp`:

```cpp
#include <gtest/gtest.h>

#include <vector>

#include "epoch.hpp"

namespace {

using CS::Epoch;
using CS::EpochClock;
using CS::EpochSlots;

TEST(EpochClock, HandsOutStrictlyGrowingNumbers) {
    EpochClock clock;
    const Epoch first = clock.tick();
    const Epoch second = clock.tick();
    EXPECT_GT(first, 0u) << "ноль не выдаётся никогда: он занят вечным нулём";
    EXPECT_GT(second, first);
}

TEST(EpochClock, NowDoesNotAdvance) {
    EpochClock clock;
    const Epoch issued = clock.tick();
    EXPECT_EQ(clock.now(), issued);
    EXPECT_EQ(clock.now(), issued);
}

TEST(EpochSlots, ZeroIsNeverHandedOut) {
    // На вечный ноль смотрит всякая незаполненная зависимость, и он обязан
    // отличаться от любого выданного номера.
    EXPECT_EQ(CS::kZeroEpoch, 0u);
}

TEST(EpochSlots, AddressSurvivesEveryLaterSlot) {
    // Это и есть причина кусочного хранения: параллельный вектор переехал бы
    // на первом же новом имени, и адрес, отданный обёртке, провис бы (§2.2).
    EpochClock clock;
    EpochSlots slots;
    slots.open(0, clock.tick());
    const Epoch *first = slots.addressOf(0);

    for (std::uint32_t i = 1; i < 1000; ++i) { slots.open(i, clock.tick()); }

    EXPECT_EQ(slots.addressOf(0), first);
    EXPECT_EQ(*first, slots.at(0));
}

TEST(EpochSlots, BumpIsSeenThroughTheAddress) {
    EpochClock clock;
    EpochSlots slots;
    slots.open(0, clock.tick());
    const Epoch *address = slots.addressOf(0);
    const Epoch before = *address;

    slots.bump(0, clock.tick());

    EXPECT_GT(*address, before);
}

TEST(EpochSlots, EveryOpenedSlotHasItsOwnAddress) {
    EpochClock clock;
    EpochSlots slots;
    std::vector<const Epoch *> seen;
    for (std::uint32_t i = 0; i < 200; ++i) {
        slots.open(i, clock.tick());
        seen.push_back(slots.addressOf(i));
    }
    for (std::uint32_t i = 0; i < seen.size(); ++i) {
        EXPECT_EQ(*seen[i], slots.at(i));
        for (std::uint32_t j = i + 1; j < seen.size(); ++j) {
            EXPECT_NE(seen[i], seen[j]);
        }
    }
}

}  // namespace
```

Дописать `epoch_test.cpp` в список `add_executable(chupascript_tests …)` в `core/tests/CMakeLists.txt` (по алфавиту рядом с `eval_test.cpp`) и `src/epoch.cpp` — в `add_library(chupascript STATIC …)` в `core/CMakeLists.txt`.

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

Run: `cmake -B build-dbg -DCMAKE_BUILD_TYPE=Debug && cmake --build build-dbg -j 2>&1 | tail -20`
Expected: FAIL — `fatal error: 'epoch.hpp' file not found`

- [ ] **Step 3: Написать заголовок**

Создать `core/src/epoch.hpp`:

```cpp
#pragma once
#include <cstdint>
#include <vector>

#include "value.hpp"

namespace CS {

/// Номер на монотонной ленте контекста.
///
/// Шестьдесят четыре бита, и это не настройка (спека §2.4.1): у такой
/// настройки ровно одно безопасное значение, а вариант с единственным
/// правильным значением — не выбор, а приглашение выставить неправильное.
/// uint32 переполняется примерно за сутки непрерывной работы при тысяче
/// записей на кадр, и последствие переполнения — ложное попадание, то есть
/// молча застывший экран. Bevy взяла u32 и оплатила это отдельной подсистемой
/// обслуживания; PostgreSQL с 32-битным XID — обязательным VACUUM FREEZE.
using Epoch = std::uint64_t;

/// Вечный ноль: на него смотрит всякая незаполненная зависимость.
///
/// Нужен затем, чтобы читатель складывал ровно kMaxDeps слов — без проверки
/// счётчика, без ветвлений, разворачиваемым циклом. Значение не меняется
/// никогда, слагаемое нулевое, сумма от него не сдвигается.
///
/// Лента начинает выдачу с единицы (EpochClock::tick), поэтому ноль не
/// совпадает ни с одним выданным номером — незаполненную зависимость нельзя
/// спутать с настоящей.
extern const Epoch kZeroEpoch;

/// Монотонная лента одного контекста.
///
/// Один счётчик на всё: и мутация, и рождение коробки берут из него ++counter.
/// Не «каждый увеличивает своё», а «все берут из общей ленты» — так номер,
/// выданный позже, всегда больше выданного раньше, и это закрывает
/// переиспользование адресов (спека §2.1, §4.4): новая коробка, севшая на
/// адрес умершей, приносит номер строго больший всего, что читатель мог
/// видеть. Совпадение невозможно, а не маловероятно.
///
/// Не атомарный — как и Box::rc: единица однопоточности здесь контекст
/// (chupascript.h, threading contract). Атомарность положила бы locked
/// read-modify-write на каждую запись однопоточного пути.
class EpochClock {
   public:
    /// Выдаёт следующий номер. Первый выданный — 1.
    [[nodiscard]] Epoch tick() noexcept { return ++counter_; }

    /// Последний выданный номер, без выдачи нового. Для тестов и метрик.
    [[nodiscard]] Epoch now() const noexcept { return counter_; }

   private:
    Epoch counter_ = 0;
};

/// Эпохи ячеек глобальных переменных, разложенные кусками.
///
/// Куски, а не параллельный вектор, ровно по одной причине: вектор переезжает
/// при росте, и адрес, отданный обёртке, провис бы на первом же setGlobal с
/// новым именем. Номера ячеек при этом вечны — удаления глобальных переменных
/// в языке нет, ячейки не переиспользуются.
///
///   blocks_ : vector<Epoch *>   сам вектор вправе переезжать, наружу не идёт
///   кусок   : 64 эпохи, 512 байт, выделяется раз и не двигается никогда
///
///   адрес эпохи ячейки s = &blocks_[s >> 6][s & 63]
///
/// Отвергнуто «все имена поставлены до компиляции» [B23]: в OKBDUI
/// addVariables зовётся по мере разбора дерева виджетов, поэтому компиляция
/// одного props случается раньше, чем заведена переменная соседнего. Куски
/// законны при любом порядке заведения имён.
class EpochSlots {
   public:
    EpochSlots() = default;
    ~EpochSlots();

    EpochSlots(const EpochSlots &) = delete;
    EpochSlots &operator=(const EpochSlots &) = delete;

    /// Заводит ячейку с её эпохой рождения.
    /// Предусловие: slot == count(), ячейки заводятся подряд и не удаляются.
    void open(std::uint32_t slot, Epoch birth);

    /// Кладёт новый номер в ячейку. Предусловие: ячейка заведена.
    void bump(std::uint32_t slot, Epoch value) noexcept;

    /// Адрес эпохи. Жив, пока живо это хранилище: кусок не переезжает никогда.
    /// Предусловие: ячейка заведена.
    [[nodiscard]] const Epoch *addressOf(std::uint32_t slot) const noexcept;

    /// Значение эпохи. Предусловие: ячейка заведена.
    [[nodiscard]] Epoch at(std::uint32_t slot) const noexcept;

    [[nodiscard]] std::uint32_t count() const noexcept { return count_; }

   private:
    /// Шестьдесят четыре эпохи в куске — 512 байт, восемь строк кэша.
    /// Мельче — больше кусков на вектор, крупнее — заметный неполный хвост у
    /// экрана с десятком переменных.
    static constexpr std::uint32_t kBlockShift = 6;
    static constexpr std::uint32_t kBlockSize = 1u << kBlockShift;
    static constexpr std::uint32_t kBlockMask = kBlockSize - 1;

    std::vector<Epoch *> blocks_;
    std::uint32_t count_ = 0;
};

/// Сколько зависимостей движок записывает у одного выражения.
///
/// Больше — выражение не кэшируется вовсе. Потолок объявлен числом, известным
/// на компиляции, потому что по нему обёртки разворачивают своё хранилище: в
/// Swift набор становится кортежем — ни аллокации, ни ARC, ни проверок границ.
///
/// Часть ABI (CHUPA_MAX_DEPS в chupascript.h). Обоснование числа — спека §4.1:
/// дорогие выражения ровно те, у которых зависимостей больше одной, а путь из
/// трёх сегментов сюда укладывается.
inline constexpr std::uint32_t kMaxDeps = 4;

/// Мест по пути оказалось больше kMaxDeps, либо в дереве есть некэшируемый
/// вызов: выражение не кэшируется.
inline constexpr std::uint32_t kDepsOverflow = 0xffffffffu;

/// Одна зависимость выражения.
///
/// Пара, а не голый адрес, потому что читателю мало прочитать эпоху — он
/// обязан удержать то, внутри чего она лежит (спека §2.7). Из адреса поля
/// внутри коробки коробку не восстановить, не зашив её раскладку в хост на
/// всех трёх платформах, поэтому коробка приезжает рядом.
///
/// owner пуст у зависимости-ячейки: эпохи ячеек живут в EpochSlots и умирают
/// вместе с хранилищем, удерживать там нечего.
struct Dep {
    const Epoch *epoch = &kZeroEpoch;
    Value owner = Value::null();
};

}  // namespace CS
```

- [ ] **Step 4: Написать тело**

Создать `core/src/epoch.cpp`:

```cpp
#include "epoch.hpp"

#include <cassert>

namespace CS {

const Epoch kZeroEpoch = 0;

EpochSlots::~EpochSlots() {
    for (Epoch *block : blocks_) { delete[] block; }
}

void EpochSlots::open(std::uint32_t slot, Epoch birth) {
    assert(slot == count_ && "ячейки заводятся подряд и не удаляются");
    const std::uint32_t block = slot >> kBlockShift;
    if (block == blocks_.size()) {
        // Куски нулями не набиваются: заведённой считается ячейка, до которой
        // дошёл open, и только её вправе читать addressOf.
        blocks_.push_back(new Epoch[kBlockSize]);
    }
    blocks_[block][slot & kBlockMask] = birth;
    count_ = slot + 1;
}

void EpochSlots::bump(std::uint32_t slot, Epoch value) noexcept {
    assert(slot < count_ && "ячейка не заведена");
    blocks_[slot >> kBlockShift][slot & kBlockMask] = value;
}

const Epoch *EpochSlots::addressOf(std::uint32_t slot) const noexcept {
    assert(slot < count_ && "ячейка не заведена");
    return &blocks_[slot >> kBlockShift][slot & kBlockMask];
}

Epoch EpochSlots::at(std::uint32_t slot) const noexcept {
    return *addressOf(slot);
}

}  // namespace CS
```

- [ ] **Step 5: Прогнать тесты**

Run: `cmake --build build-dbg -j && ctest --test-dir build-dbg -R 'Epoch' --output-on-failure`
Expected: PASS, шесть тестов

- [ ] **Step 6: Коммит**

```bash
git add core/src/epoch.hpp core/src/epoch.cpp core/tests/epoch_test.cpp \
        core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "feat: у эпох не было ни ленты, ни места, где адрес переживает рост"
```

---

### Task 2: Хранилище заводит ленту и эпохи ячеек

**Files:**
- Modify: `core/src/store.hpp`, `core/src/store.cpp`
- Test: `core/tests/store_test.cpp`

**Interfaces:**
- Consumes: `CS::EpochClock`, `CS::EpochSlots`, `CS::Epoch` (задача 1)
- Produces: `Store::clock()` → `EpochClock &`, `Store::epochAddressAt(GlobalSlot)` → `const Epoch *`, `Store::epochAt(GlobalSlot)` → `Epoch`; `Store::setGlobal` поднимает эпоху ячейки на всякой записи и заводит её при рождении ячейки

- [ ] **Step 1: Написать падающий тест**

Дописать в `core/tests/store_test.cpp`:

```cpp
TEST(StoreEpoch, WritingAGlobalMovesItsEpoch) {
    Store store;
    CS::Deferred dead;
    store.setGlobal("width", Value::number(320.0), dead);
    const CS::GlobalSlot slot = store.globalSlot("width");
    const CS::Epoch before = store.epochAt(slot);

    store.setGlobal("width", Value::number(375.0), dead);

    EXPECT_GT(store.epochAt(slot), before);
}

TEST(StoreEpoch, AddressOfACellSurvivesLaterNames) {
    // Ровно тот случай, ради которого эпохи лежат кусками: OKBDUI заводит
    // переменные по мере разбора дерева виджетов, и адрес, отданный обёртке
    // раньше, обязан пережить всякое следующее имя (спека §2.2).
    Store store;
    CS::Deferred dead;
    store.setGlobal("first", Value::number(1.0), dead);
    const CS::Epoch *address = store.epochAddressAt(store.globalSlot("first"));

    for (int i = 0; i < 500; ++i) {
        store.setGlobal("name" + std::to_string(i), Value::number(i), dead);
    }

    EXPECT_EQ(store.epochAddressAt(store.globalSlot("first")), address);
    store.setGlobal("first", Value::number(2.0), dead);
    EXPECT_EQ(*address, store.epochAt(store.globalSlot("first")));
}

TEST(StoreEpoch, EveryCellIsBornWithItsOwnNumber) {
    Store store;
    CS::Deferred dead;
    store.setGlobal("a", Value::number(1.0), dead);
    store.setGlobal("b", Value::number(2.0), dead);

    EXPECT_NE(store.epochAt(store.globalSlot("a")),
              store.epochAt(store.globalSlot("b")));
}

TEST(StoreEpoch, TheClockIsSharedByCellsAndBoxes) {
    // Одна лента на контекст: номер, выданный позже, строго больше выданного
    // раньше, кому бы он ни достался (спека §2.1).
    Store store;
    CS::Deferred dead;
    store.setGlobal("a", Value::number(1.0), dead);
    const CS::Epoch afterCell = store.clock().now();
    const CS::Epoch fresh = store.clock().tick();

    EXPECT_GT(fresh, afterCell);
}
```

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

Run: `cmake --build build-dbg -j 2>&1 | grep -m3 'error'`
Expected: FAIL — `no member named 'epochAt' in 'CS::Store'`

- [ ] **Step 3: Завести ленту и эпохи в хранилище**

В `core/src/store.hpp` добавить `#include "epoch.hpp"`, а в публичную часть — рядом с `globalValueAt`:

```cpp
    /// Лента этого контекста. Отдаётся изменяемой намеренно: номера из неё
    /// берут не только ячейки, но и коробки, а те создаются и меняются
    /// свободными функциями aggregate.hpp, которым хранилища не полагается.
    ///
    /// Мимо ленты записи не существует — это проверяется грепом по пяти
    /// дверям (спека §2.2), а не дисциплиной.
    [[nodiscard]] EpochClock &clock() noexcept { return clock_; }

    /// Адрес эпохи ячейки. Жив, пока живо хранилище (эпохи лежат кусками,
    /// epoch.hpp). Это тот самый адрес, что уезжает к читателю в Dep::epoch.
    ///
    /// Предусловие: номер выдан ЭТИМ хранилищем — как и у globalValueAt.
    [[nodiscard]] const Epoch *epochAddressAt(GlobalSlot slot) const noexcept {
        return epochs_.addressOf(slot);
    }

    /// Значение эпохи ячейки. Для тестов и отладки; горячий путь читает по
    /// адресу и в движок не входит.
    [[nodiscard]] Epoch epochAt(GlobalSlot slot) const noexcept {
        return epochs_.at(slot);
    }
```

В приватную часть, рядом с `values_`:

```cpp
    /// Лента контекста: из неё берут номер и ячейки, и коробки.
    EpochClock clock_;

    /// Эпохи ячеек, параллельные values_ по номеру, но лежащие кусками:
    /// параллельный вектор переехал бы вместе с values_ и увёл бы за собой
    /// адрес, отданный читателю (epoch.hpp, спека §2.2).
    EpochSlots epochs_;
```

- [ ] **Step 4: Поднять эпоху в обеих ветках `setGlobal`**

В `core/src/store.cpp`, в `Store::setGlobal`, в ветке найденного имени — сразу после `slot = v;`:

```cpp
        // Эпоха поднимается здесь, а не у вызывающего: запись без подъёма даёт
        // не «медленно», а молча застывший экран (спека §4.5), поэтому она
        // обязана быть невыразима мимо этой строки.
        epochs_.bump(slots_[at].slot, clock_.tick());
        return;
```

В ветке нового имени — сразу после `values_.push_back(v);`:

```cpp
    // Рождение ячейки берёт номер из той же ленты, что и мутация: номер,
    // выданный позже, строго больше выданного раньше, и на этом стоит §4.4.
    epochs_.open(slot, clock_.tick());
```

- [ ] **Step 5: Прогнать тесты**

Run: `cmake --build build-dbg -j && ctest --test-dir build-dbg -R 'Store' --output-on-failure`
Expected: PASS, включая четыре новых

- [ ] **Step 6: Коммит**

```bash
git add core/src/store.hpp core/src/store.cpp core/tests/store_test.cpp
git commit -m "feat: запись в глобальную переменную не оставляла следа, по которому её можно заметить"
```

---

### Task 3: Эпоха в заголовке коробки, номер — при рождении

Только `ArrayBox` и `ObjectBox`. `StringBox` эпохи не получает: изменить строку в языке нечем, мутирующих операций над строками нет, ручка на строку вечно свежая (спека §2.8). Восемь байт на всякую строку ради поля, которое никогда не сдвинется, — цена ни за что.

**Files:**
- Modify: `core/src/box.hpp`, `core/src/box.cpp`
- Modify: `core/src/aggregate.hpp` (создатели принимают ленту)
- Modify: `core/src/execution.hpp` (`clock()`), `core/src/eval.cpp:390,406`, `core/src/data.cpp:72,87`, `core/src/builtin.cpp:236`
- Test: `core/tests/box_test.cpp`, обновление `core/tests/store_test.cpp`

**Interfaces:**
- Consumes: `CS::EpochClock`, `CS::Epoch` (задача 1); `Store::clock()` (задача 2)
- Produces: `detail::ArrayBox::epoch`, `detail::ObjectBox::epoch`; `detail::makeArrayBox(capacity, Epoch birth)`, `detail::makeObjectBox(keys, capacity, Epoch birth)`; `CS::makeArray(capacity, EpochClock &, Deferred &)`, `CS::makeObject(KeyTable *, capacity, EpochClock &, Deferred &)`; `CS::epochAddressOf(Value)` → `const Epoch *`; `Execution::clock()` → `EpochClock &`

- [ ] **Step 1: Написать падающий тест**

Дописать в `core/tests/box_test.cpp`:

```cpp
TEST(BoxEpoch, BothAggregatesKeepTheEpochAtTheSameOffset) {
    // Одинаковое смещение — не совпадение, а требование: epochAddressOf
    // отвечает одной строкой на оба вида, и от вида коробки ответ не зависит.
    EXPECT_EQ(offsetof(CS::detail::ArrayBox, epoch),
              offsetof(CS::detail::ObjectBox, epoch));
}

TEST(BoxEpoch, ANewBoxTakesItsNumberFromTheClock) {
    CS::Store store;
    CS::Deferred dead;
    const CS::Epoch before = store.clock().now();

    const Value a = CS::makeArray(0, store.clock(), dead);

    EXPECT_GT(*CS::epochAddressOf(a), before);
}

TEST(BoxEpoch, ABoxBornLaterCarriesAStrictlyGreaterNumber) {
    // Это и закрывает переиспользование адреса (спека §4.4): коробка, севшая
    // на адрес умершей, приносит номер больше всего, что читатель мог видеть.
    CS::Store store;
    CS::Deferred dead;
    const Value first = CS::makeArray(0, store.clock(), dead);
    const Value second = CS::makeObject(store.keys(), 0, store.clock(), dead);

    EXPECT_GT(*CS::epochAddressOf(second), *CS::epochAddressOf(first));
}

TEST(BoxEpoch, ADeadBoxAddressReusedCannotLookUnchanged) {
    CS::Store store;
    CS::Deferred dead;
    CS::Epoch seen = 0;
    const void *address = nullptr;
    {
        CS::Deferred scoped;
        const Value doomed = CS::makeArray(0, store.clock(), scoped);
        seen = *CS::epochAddressOf(doomed);
        address = doomed.box();
    }  // scoped слит — коробка освобождена

    const Value fresh = CS::makeArray(0, store.clock(), dead);
    if (fresh.box() == address) {
        EXPECT_GT(*CS::epochAddressOf(fresh), seen);
    } else {
        GTEST_SKIP() << "аллокатор отдал другой адрес — проверять нечего";
    }
}
```

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

Run: `cmake --build build-dbg -j 2>&1 | grep -m3 'error'`
Expected: FAIL — `no member named 'epoch' in 'CS::detail::ArrayBox'`

- [ ] **Step 3: Положить эпоху в заголовки и взять номер при рождении**

В `core/src/box.hpp` добавить `#include "epoch.hpp"`, затем в `ArrayBox` — первым членом после `Box`:

```cpp
struct ArrayBox : Box {
    /// Эпоха коробки — первым членом после общего заголовка, и в ObjectBox
    /// тоже первым: одно смещение на оба вида, поэтому epochAddressOf
    /// отвечает без ветвления по виду.
    ///
    /// Наружу смещение не объявляется: адрес эпохи уезжает к читателю
    /// готовым, в Dep::epoch, и считать его хосту незачем. Так раскладка
    /// коробки остаётся приватной на всех трёх платформах.
    Epoch epoch;

    std::uint32_t len;
    std::uint32_t cap;
    Value *data;
    // остальные члены и методы — без изменений
};
```

То же поле первым членом в `ObjectBox` — сразу после заголовка `Box` и перед `keys`. Ниже, рядом с объявлениями создателей:

```cpp
/// Счётчик у новорождённого — 1, и эта ссылка принадлежит создателю.
/// birth — номер с ленты контекста: рождение берёт из неё так же, как мутация.
StringBox *makeStringBox(std::string_view bytes);
ArrayBox *makeArrayBox(std::uint32_t capacity, Epoch birth);
ObjectBox *makeObjectBox(KeyTable *keys, std::uint32_t capacity, Epoch birth);
```

В конец `box.hpp`, рядом со свободной `stringBytes`:

```cpp
/// Адрес эпохи агрегата — то, что уезжает читателю зависимостью.
///
/// Предусловие: v.kind() — Array либо Object. У скаляра и у строки эпохи нет
/// и быть не может: скаляру не за что зацепиться, а строку в языке нечем
/// изменить (спека §2.8).
[[nodiscard]] inline const Epoch *epochAddressOf(const Value &v) noexcept {
    assert(v.kind() == Value::Kind::Array || v.kind() == Value::Kind::Object);
    return &static_cast<const detail::ArrayBox *>(v.box())->epoch;
}
```

В `core/src/box.cpp` — `makeArrayBox` и `makeObjectBox` принимают `Epoch birth` и пишут `box->epoch = birth;` рядом с установкой `rc = 1`. Там же добавить:

```cpp
static_assert(offsetof(detail::ArrayBox, epoch) ==
                  offsetof(detail::ObjectBox, epoch),
              "epochAddressOf читает эпоху одним смещением на оба вида");
```

- [ ] **Step 4: Провести ленту до создателей**

В `core/src/aggregate.hpp` добавить `#include "epoch.hpp"` и заменить два создателя:

```cpp
/// Создаёт пустой массив. capacity — сколько элементов выделить заранее.
///
/// clock — лента контекста: рождение берёт номер оттуда же, откуда мутация.
/// Параметром, а не из хранилища: коробке хранилище не нужно ни для чего, и
/// это свойство здесь ценится — прочитать агрегат вправе кто угодно, в том
/// числе когда контекста уже нет. Лента нужна только на запись, а записи
/// снаружи контекста в языке не существует.
[[nodiscard]] inline Value makeArray(std::uint32_t capacity, EpochClock &clock,
                                     Deferred &dead) {
    detail::ArrayBox *box = detail::makeArrayBox(capacity, clock.tick());
    dead.take(box);
    return Value::array(box);
}

[[nodiscard]] inline Value makeObject(KeyTable *keys, std::uint32_t capacity,
                                      EpochClock &clock, Deferred &dead) {
    detail::ObjectBox *box = detail::makeObjectBox(keys, capacity, clock.tick());
    dead.take(box);
    return Value::object(box);
}
```

В `core/src/execution.hpp`, рядом с `keys()`:

```cpp
    /// Лента контекста, через хранилище: мутаторы и создатели агрегатов
    /// принимают её параметром (aggregate.hpp).
    [[nodiscard]] EpochClock &clock() noexcept { return store_.clock(); }
```

Обновить вызывающих: `core/src/eval.cpp:390` → `CS::makeArray(count, exec.clock(), exec.deferred())`, `:406` → `CS::makeObject(exec.keys(), count / 2, exec.clock(), exec.deferred())`; `core/src/data.cpp:72,87` → `store.clock()` вместо ничего (у `buildValue` `store` уже на руках); `core/src/builtin.cpp:236` → `CS::makeArray(size, exec.clock(), exec.deferred())`.

- [ ] **Step 5: Обновить тесты, которые звали создателей**

Run: `cmake --build build-dbg -j 2>&1 | grep -c 'error'` — увидеть список мест в `core/tests/store_test.cpp` и добавить в каждый вызов `store.clock()` перед `dead`. Правка механическая; ни одного теста по смыслу менять не надо.

- [ ] **Step 6: Прогнать все тесты**

Run: `cmake --build build-dbg -j && ctest --test-dir build-dbg --output-on-failure`
Expected: PASS целиком

- [ ] **Step 7: Коммит**

```bash
git add core/src/box.hpp core/src/box.cpp core/src/aggregate.hpp \
        core/src/execution.hpp core/src/eval.cpp core/src/data.cpp \
        core/src/builtin.cpp core/tests/box_test.cpp core/tests/store_test.cpp
git commit -m "feat: у коробки не было номера, по которому её отличить от занявшей её адрес"
```

---

### Task 4: Инкремент внутри четырёх мутаторов агрегата

Пятая дверь — `Store::setGlobal` — закрыта задачей 2. Здесь остальные четыре.

**Files:**
- Modify: `core/src/aggregate.hpp`
- Modify: `core/src/eval.cpp:600,649,681`, `core/src/builtin.cpp:274,283`, `core/src/data.cpp` (сборка литерала объекта)
- Test: `core/tests/box_test.cpp`, обновление `core/tests/store_test.cpp`

**Interfaces:**
- Consumes: `Execution::clock()`, `epochAddressOf` (задача 3)
- Produces: `arraySet(Value, uint32_t, Value, EpochClock &, Deferred &)`, `arrayPush(Value, Value, EpochClock &)`, `arrayPop(Value, Value *, EpochClock &, Deferred &)`, `objectSet(Value, string_view, Value, EpochClock &, Deferred &)` — каждый поднимает эпоху своей коробки

- [ ] **Step 1: Написать падающий тест**

Дописать в `core/tests/box_test.cpp`:

```cpp
namespace {

/// Все двери, которыми в языке меняют агрегат. Список закрыт спецификацией:
/// мутирующих билтинов ровно два — Push и Pop, — а больше данные не меняет
/// ничего (спека §2.2). Тест обязан перечислять их все: пропущенная дверь
/// даёт не медленную работу, а молча застывший экран.
TEST(BoxEpoch, EveryMutatingDoorMovesTheEpoch) {
    CS::Store store;
    CS::Deferred dead;
    CS::EpochClock &clock = store.clock();

    const Value array = CS::makeArray(4, clock, dead);
    const CS::Epoch *arrayEpoch = CS::epochAddressOf(array);

    CS::Epoch before = *arrayEpoch;
    CS::arrayPush(array, Value::number(1.0), clock);
    EXPECT_GT(*arrayEpoch, before) << "arrayPush";

    before = *arrayEpoch;
    EXPECT_TRUE(CS::arraySet(array, 0, Value::number(2.0), clock, dead));
    EXPECT_GT(*arrayEpoch, before) << "arraySet";

    before = *arrayEpoch;
    EXPECT_TRUE(CS::arrayPop(array, nullptr, clock, dead));
    EXPECT_GT(*arrayEpoch, before) << "arrayPop";

    const Value object = CS::makeObject(store.keys(), 2, clock, dead);
    const CS::Epoch *objectEpoch = CS::epochAddressOf(object);

    before = *objectEpoch;
    CS::objectSet(object, "name", Value::number(3.0), clock, dead);
    EXPECT_GT(*objectEpoch, before) << "objectSet: новый ключ";

    before = *objectEpoch;
    CS::objectSet(object, "name", Value::number(4.0), clock, dead);
    EXPECT_GT(*objectEpoch, before) << "objectSet: существующий ключ";
}

TEST(BoxEpoch, AFailedArraySetLeavesTheEpochAlone) {
    // Запись за границей — не запись: ничего не изменилось, значит и ответ
    // «изменилось» давать не за что.
    CS::Store store;
    CS::Deferred dead;
    const Value array = CS::makeArray(4, store.clock(), dead);
    const CS::Epoch before = *CS::epochAddressOf(array);

    EXPECT_FALSE(CS::arraySet(array, 7, Value::number(1.0), store.clock(), dead));

    EXPECT_EQ(*CS::epochAddressOf(array), before);
}

TEST(BoxEpoch, APopFromAnEmptyArrayLeavesTheEpochAlone) {
    CS::Store store;
    CS::Deferred dead;
    const Value array = CS::makeArray(0, store.clock(), dead);
    const CS::Epoch before = *CS::epochAddressOf(array);

    EXPECT_FALSE(CS::arrayPop(array, nullptr, store.clock(), dead));

    EXPECT_EQ(*CS::epochAddressOf(array), before);
}

TEST(BoxEpoch, TouchingOneAggregateDoesNotMoveAnother) {
    // То, ради чего схема с коробками стоит своих денег (спека §3.2): правка
    // соседнего элемента не задевает читателя нулевого.
    CS::Store store;
    CS::Deferred dead;
    const Value first = CS::makeObject(store.keys(), 1, store.clock(), dead);
    const Value second = CS::makeObject(store.keys(), 1, store.clock(), dead);
    const CS::Epoch untouched = *CS::epochAddressOf(first);

    CS::objectSet(second, "name", Value::number(1.0), store.clock(), dead);

    EXPECT_EQ(*CS::epochAddressOf(first), untouched);
}

}  // namespace
```

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

Run: `cmake --build build-dbg -j 2>&1 | grep -m3 'error'`
Expected: FAIL — `too many arguments to function call` у `CS::arrayPush`

- [ ] **Step 3: Поднять эпоху внутри мутаторов**

В `core/src/aggregate.hpp` заменить блок «изменение». Комментарий раздела дополнить:

```cpp
// ─── изменение ───
//
// Вытесненная ссылка уходит в Deferred, а не освобождается на месте: см.
// deferred.hpp.
//
// Лента приходит параметром, и подъём эпохи стоит ВНУТРИ мутатора, а не рядом
// с вызовом. Причина в цене ошибки: пропущенная точка инкремента даёт не
// «медленно», а молча застывший экран (спека §4.5), поэтому мутация обязана
// быть невыразима мимо подъёма. Дверей ровно пять — эти четыре и
// Store::setGlobal, — и список проверяется грепом, а не дисциплиной.
```

```cpp
inline bool arraySet(Value a, std::uint32_t index, Value v, EpochClock &clock,
                     Deferred &dead) noexcept {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayBox *box = static_cast<detail::ArrayBox *>(a.box());
    // Эпоха не двигается на отказе: за границей ничего не изменилось, и
    // отвечать «изменилось» не за что.
    if (index >= box->size()) { return false; }
    detail::retainValue(v);
    dead.take(box->at(index));
    box->set(index, v);
    box->epoch = clock.tick();
    return true;
}

inline void arrayPush(Value a, Value v, EpochClock &clock) {
    assert(a.kind() == Value::Kind::Array);
    detail::retainValue(v);
    detail::ArrayBox *box = static_cast<detail::ArrayBox *>(a.box());
    box->push(v);
    box->epoch = clock.tick();
}

inline bool arrayPop(Value a, Value *out, EpochClock &clock,
                     Deferred &dead) noexcept {
    assert(a.kind() == Value::Kind::Array);
    detail::ArrayBox *box = static_cast<detail::ArrayBox *>(a.box());
    if (box->size() == 0) { return false; }
    const Value last = box->pop();
    if (out != nullptr) { *out = last; }
    dead.take(last);
    box->epoch = clock.tick();
    return true;
}

inline void objectSet(Value o, std::string_view key, Value v, EpochClock &clock,
                      Deferred &dead) {
    assert(o.kind() == Value::Kind::Object);
    detail::ObjectBox &box = *static_cast<detail::ObjectBox *>(o.box());

    const std::uint32_t prefix = detail::keyPrefix(key);
    bool found = false;
    const std::uint32_t at = detail::findEntry(box, key, prefix, &found);
    detail::retainValue(v);
    // Один подъём на обе ветки: и замена значения, и заведение ключа —
    // изменение объекта, и различать их читателю нечем и незачем.
    box.epoch = clock.tick();
    if (found) {
        dead.take(box.at(at).value);
        box.setValue(at, v);
        return;
    }
    box.insert(at, detail::Entry{box.keys->intern(key), prefix, v});
}
```

- [ ] **Step 4: Обновить вызывающих**

`core/src/eval.cpp`: `:396` `arrayPush(array, element, exec.clock())`; `:416` и `:681` `objectSet(…, exec.clock(), exec.deferred())`; `:600` то же; `:649` `arraySet(…, exec.clock(), exec.deferred())`.
`core/src/builtin.cpp`: `:240` `arrayPush(result, …, exec.clock())`; `:274` `arrayPush(args[0], args[1], exec.clock())`; `:283` `arrayPop(args[0], nullptr, exec.clock(), exec.deferred())`.
`core/src/data.cpp`: сборка литерала — `arrayPush(array, element, store.clock())`, `objectSet(object, …, store.clock(), dead)`.
Тесты `core/tests/store_test.cpp` — механически, `store.clock()` перед `dead` (у `arrayPush` — вместо отсутствующего последнего параметра).

- [ ] **Step 5: Проверить, что дверей ровно пять**

Run:
```bash
grep -rn 'epoch = clock.tick()\|epochs_.bump(\|epochs_.open(' core/src/
```
Expected: шесть строк — четыре в `aggregate.hpp`, две в `store.cpp` (`bump` и `open`); больше подъёмов эпохи в движке нет нигде.

- [ ] **Step 6: Прогнать все тесты**

Run: `cmake --build build-dbg -j && ctest --test-dir build-dbg --output-on-failure`
Expected: PASS целиком

- [ ] **Step 7: Коммит**

```bash
git add core/src/aggregate.hpp core/src/eval.cpp core/src/builtin.cpp \
        core/src/data.cpp core/tests/box_test.cpp core/tests/store_test.cpp
git commit -m "feat: агрегат менялся молча — снаружи было не отличить тронутый от нетронутого"
```

---

### Task 5: Вычисление записывает, где побывало

**Files:**
- Modify: `core/src/execution.hpp` (набор зависимостей)
- Modify: `core/src/eval.cpp` (`Identifier`, `Member`, `Index`, вход `evalExpression`)
- Test: `core/tests/execution_test.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `CS::Dep`, `CS::kMaxDeps`, `CS::kDepsOverflow` (задача 1); `Store::epochAddressAt` (задача 2); `epochAddressOf` (задача 3)
- Produces: `CS::DepSet` с `reset()`, `add(const Epoch *, Value owner)`, `count()`, `overflowed()`, `at(i)`; `Execution::deps()` → `DepSet &`

- [ ] **Step 1: Написать падающий тест**

Дописать в `core/tests/execution_test.cpp`:

```cpp
TEST(DepSet, RecordsUpToTheCeiling) {
    CS::DepSet deps;
    CS::Epoch a = 1, b = 2, c = 3, d = 4;
    deps.add(&a, Value::null());
    deps.add(&b, Value::null());
    deps.add(&c, Value::null());
    deps.add(&d, Value::null());

    EXPECT_EQ(deps.count(), CS::kMaxDeps);
    EXPECT_FALSE(deps.overflowed());
}

TEST(DepSet, OneMoreThanTheCeilingIsOverflow) {
    // Направление огрубления безопасное: лишний пересчёт, но никогда ложное
    // попадание (спека §2.3).
    CS::DepSet deps;
    CS::Epoch words[5] = {1, 2, 3, 4, 5};
    for (CS::Epoch &word : words) { deps.add(&word, Value::null()); }

    EXPECT_TRUE(deps.overflowed());
}

TEST(DepSet, TheSameAddressTwiceTakesOneSlot) {
    // a.x + a.y трогает ячейку a дважды и коробку a дважды. Без слияния такое
    // выражение переполнялось бы на ровном месте.
    CS::DepSet deps;
    CS::Epoch a = 1, b = 2;
    deps.add(&a, Value::null());
    deps.add(&b, Value::null());
    deps.add(&a, Value::null());

    EXPECT_EQ(deps.count(), 2u);
}

TEST(DepSet, ResetForgetsEverythingIncludingOverflow) {
    CS::DepSet deps;
    CS::Epoch words[5] = {1, 2, 3, 4, 5};
    for (CS::Epoch &word : words) { deps.add(&word, Value::null()); }

    deps.reset();

    EXPECT_EQ(deps.count(), 0u);
    EXPECT_FALSE(deps.overflowed());
}
```

Дописать в `core/tests/eval_test.cpp` (фикстура файла уже собирает `Store` и `Execution`):

```cpp
TEST(EvalDeps, AConstantDependsOnNothing) {
    Fixture f;
    Value out = Value::null();
    ASSERT_TRUE(f.eval("42", &out));
    EXPECT_EQ(f.exec.deps().count(), 0u);
    EXPECT_FALSE(f.exec.deps().overflowed());
}

TEST(EvalDeps, ABareScalarDependsOnItsCell) {
    Fixture f;
    f.set("button_enabled", "true");
    Value out = Value::null();
    ASSERT_TRUE(f.eval("button_enabled", &out));

    ASSERT_EQ(f.exec.deps().count(), 1u);
    EXPECT_EQ(f.exec.deps().at(0).epoch,
              f.store.epochAddressAt(f.store.globalSlot("button_enabled")));
    EXPECT_EQ(f.exec.deps().at(0).owner.kind(), Value::Kind::Null)
        << "у ячейки владельца нет: её эпоха живёт столько же, сколько хранилище";
}

TEST(EvalDeps, APathRecordsCellAndEveryBoxOnTheWay) {
    Fixture f;
    f.set("users", "[{'name': 'Вася'}, {'name': 'Петя'}]");
    Value out = Value::null();
    ASSERT_TRUE(f.eval("users[0].name", &out));

    ASSERT_EQ(f.exec.deps().count(), 3u);
    EXPECT_EQ(f.exec.deps().at(0).epoch,
              f.store.epochAddressAt(f.store.globalSlot("users")));
    EXPECT_EQ(f.exec.deps().at(1).owner.kind(), Value::Kind::Array);
    EXPECT_EQ(f.exec.deps().at(2).owner.kind(), Value::Kind::Object);
}

TEST(EvalDeps, ADeepPathOverflows) {
    Fixture f;
    f.set("u", "{'a': {'b': {'c': {'d': {'e': 1}}}}}");
    Value out = Value::null();
    ASSERT_TRUE(f.eval("u.a.b.c.d.e", &out));

    EXPECT_TRUE(f.exec.deps().overflowed());
}

TEST(EvalDeps, TheSetIsRebuiltOnEveryEvaluation) {
    // Набор верен до следующего вычисления этого выражения — у выражения с
    // путями он меняется от вычисления к вычислению (спека §2.6).
    Fixture f;
    f.set("a", "1");
    Value out = Value::null();
    ASSERT_TRUE(f.eval("a", &out));
    ASSERT_TRUE(f.eval("42", &out));

    EXPECT_EQ(f.exec.deps().count(), 0u);
}
```

Если в `eval_test.cpp` нет фикстуры с полями `store`/`exec`/`eval`/`set` — завести её в этом же файле по образцу соседних тестов, не меняя существующих.

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

Run: `cmake --build build-dbg -j 2>&1 | grep -m3 'error'`
Expected: FAIL — `no type named 'DepSet' in namespace 'CS'`

- [ ] **Step 3: Завести набор зависимостей**

В `core/src/execution.hpp` добавить `#include "epoch.hpp"` и перед классом `Execution`:

```cpp
/// Зависимости одного вычисления: где оно побывало.
///
/// Набор = ячейки прочитанных имён + коробки, через которые прошёл спуск. То
/// есть ровно то, что вычисление и так трогает: выяснять ничего не надо, надо
/// записывать тронутое (спека §2.3).
///
/// Потолок — kMaxDeps. Не поместилось — выражение не кэшируется никогда и
/// считается как сегодня. Направление огрубления безопасное: лишний пересчёт,
/// но никогда ложное попадание.
///
/// Пишется на всяком вычислении, а не только на отслеживаемом. Отдельный режим
/// «сейчас записываем» был отвергнут: он раздваивает путь вычисления, и ошибка
/// в редкой половине не ловится ничем, кроме экрана, который перестал
/// обновляться. Цена — четыре записи в горячий массив; она меряется в задаче 10
/// (BM_Eval_Constant до и после), и §5.5 требует, чтобы она осталась в шуме.
class DepSet {
   public:
    void reset() noexcept {
        count_ = 0;
        overflowed_ = false;
    }

    /// Записывает место. Повтор адреса слиянием: `a.x + a.y` трогает и ячейку,
    /// и коробку дважды, и без слияния переполнялся бы на ровном месте.
    /// Сравнений не больше трёх — набор крошечный, ни хеша, ни сортировки.
    void add(const Epoch *epoch, Value owner) noexcept {
        for (std::uint32_t i = 0; i < count_; ++i) {
            if (deps_[i].epoch == epoch) { return; }
        }
        if (count_ == kMaxDeps) {
            overflowed_ = true;
            return;
        }
        deps_[count_++] = Dep{epoch, owner};
    }

    [[nodiscard]] std::uint32_t count() const noexcept { return count_; }
    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }

    /// Предусловие: i < count().
    [[nodiscard]] const Dep &at(std::uint32_t i) const noexcept {
        assert(i < count_);
        return deps_[i];
    }

   private:
    Dep deps_[kMaxDeps];
    std::uint32_t count_ = 0;
    bool overflowed_ = false;
};
```

В `Execution` — член и доступ:

```cpp
    /// Зависимости текущего вычисления. Сбрасывается на входе в evalExpression
    /// и заполняется по ходу обхода.
    [[nodiscard]] DepSet &deps() noexcept { return deps_; }
    [[nodiscard]] const DepSet &deps() const noexcept { return deps_; }
```

```cpp
    DepSet deps_;
```

Дополнить LAYOUT-комментарий класса строкой про `deps_`.

- [ ] **Step 4: Записывать места по ходу обхода**

В `core/src/eval.cpp`, в `evalExpression` (публичный вход), первой строкой:

```cpp
    // Набор строится заново на всяком вычислении: у выражения с путями он
    // меняется от раза к разу — прошлый раз ушёл влево, этот уйдёт вправо.
    exec.deps().reset();
```

В `case NodeKind::Identifier`:

```cpp
        case NodeKind::Identifier: {
            const GlobalSlot slot = ast.globalValuesSlot(node);
            // Зависимость — ячейка, а не значение в ней: значение
            // тривиально копируемо и ходит по значению, эпохе там не жить
            // (спека §7, «Эпоха в Value»). Владельца у ячейки нет: её эпоха
            // живёт столько же, сколько хранилище.
            exec.deps().add(exec.store().epochAddressAt(slot), Value::null());
            *out = exec.store().globalValueAt(slot);
            return true;
        }
```

В `case NodeKind::Member` и `case NodeKind::Index` — сразу после того, как вычислена `base`, и до чтения из неё:

```cpp
            // Коробка, через которую идёт спуск, — тоже зависимость: правка
            // соседнего элемента не должна будить читателя нулевого (§3.2).
            // Владелец приезжает рядом, чтобы читателю было за что взяться
            // ретейном: без этого он держал бы голый адрес внутри коробки,
            // которую счётчик ссылок вправе освободить (§2.7).
            if (base.kind() == Value::Kind::Array ||
                base.kind() == Value::Kind::Object) {
                exec.deps().add(CS::epochAddressOf(base), base);
            }
```

- [ ] **Step 5: Прогнать тесты**

Run: `cmake --build build-dbg -j && ctest --test-dir build-dbg -R 'Dep|Eval' --output-on-failure`
Expected: PASS

- [ ] **Step 6: Коммит**

```bash
git add core/src/execution.hpp core/src/eval.cpp \
        core/tests/execution_test.cpp core/tests/eval_test.cpp
git commit -m "feat: вычисление знало, где побывало, но никому этого не рассказывало"
```

---

### Task 6: Выражение с некэшируемым вызовом не кэшируется

Третье решение сверх спеки. `format('${now()}')` не зависит ни от одной ячейки и ни от одной коробки: набор пуст, сумма нулевая — и по §2.6 такое выражение кэшировалось бы навсегда. Часы на экране встали бы. Это дыра, а не лишний пересчёт, и закрывается она статически: список вызываемых известен на компиляции.

**Files:**
- Modify: `core/src/callee.hpp`, `core/src/callee.cpp`
- Modify: `core/src/ast.hpp`, `core/src/ast.cpp`
- Modify: `core/src/check.cpp` (`checkCall`)
- Modify: `core/src/expression.hpp`
- Test: `core/tests/check_test.cpp`, `core/tests/host_test.cpp`

**Interfaces:**
- Consumes: `Callee` (`core/src/callee.hpp`), `CHUPA_FN_CACHEABLE` (`chupascript.h:252`)
- Produces: `Callee::cacheable`; `Ast::markUncacheable()`, `Ast::isCacheable()`; `Expression::isCacheable()`

- [ ] **Step 1: Написать падающий тест**

Дописать в `core/tests/host_test.cpp` (там уже есть регистрация хост-функций):

```cpp
namespace {

bool returnsSeven(ChupaContext *, const ChupaValue *, uint8_t,
                  ChupaValue *out) {
    chupa_make_number(out, 7.0);
    return true;
}

}  // namespace

TEST(HostCacheable, AnExpressionOverACacheableFunctionStaysCacheable) {
    CS::Context ctx;
    ChupaFunction desc{};
    desc.name = "seven";
    desc.name_len = 5;
    desc.min_args = 0;
    desc.max_args = 0;
    desc.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_EFFECT_FREE |
                 CHUPA_FN_CACHEABLE;
    desc.call = returnsSeven;
    ASSERT_EQ(ctx.registerFunction(desc), CS::RegisterOutcome::Ok);

    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(ctx.compileExpression("seven()", &expr, diags, 4), 0u);

    EXPECT_TRUE(expr.isCacheable());
}

TEST(HostCacheable, AnExpressionOverAClockIsNotCacheable) {
    // now() и screenWidth() — «без эффектов, но некэшируемая»
    // (docs/semantics.md §8.9). Набор зависимостей у такого выражения пуст, и
    // без этой отметки оно кэшировалось бы навсегда: часы бы встали.
    CS::Context ctx;
    ChupaFunction desc{};
    desc.name = "now";
    desc.name_len = 3;
    desc.min_args = 0;
    desc.max_args = 0;
    desc.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_EFFECT_FREE;  // без CACHEABLE
    desc.call = returnsSeven;
    ASSERT_EQ(ctx.registerFunction(desc), CS::RegisterOutcome::Ok);

    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(ctx.compileExpression("format('${now()}')", &expr, diags, 4), 0u);

    EXPECT_FALSE(expr.isCacheable());
}

TEST(HostCacheable, TheMarkSurvivesDepth) {
    // Вызов может сидеть где угодно в дереве: отметка на дереве, а не на узле.
    CS::Context ctx;
    ChupaFunction desc{};
    desc.name = "now";
    desc.name_len = 3;
    desc.min_args = 0;
    desc.max_args = 0;
    desc.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_EFFECT_FREE;
    desc.call = returnsSeven;
    ASSERT_EQ(ctx.registerFunction(desc), CS::RegisterOutcome::Ok);

    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(ctx.compileExpression("1 + (2 * now())", &expr, diags, 4), 0u);

    EXPECT_FALSE(expr.isCacheable());
}

TEST(BuiltinsAreCacheable, EveryBuiltinReachableFromAnExpressionIsCacheable) {
    // Билтины детерминированы: часов и флагов среди них нет, а мутирующие
    // Push и Pop до выражения не доходят — их результат Void, и §6.2 отвергает
    // такое выражение раньше.
    CS::Context ctx;
    CS::Diagnostic diags[4];
    CS::Expression expr;
    ASSERT_EQ(ctx.compileExpression("count([1, 2, 3])", &expr, diags, 4), 0u);

    EXPECT_TRUE(expr.isCacheable());
}
```

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

Run: `cmake --build build-dbg -j 2>&1 | grep -m3 'error'`
Expected: FAIL — `no member named 'isCacheable' in 'CS::Expression'`

- [ ] **Step 3: Провести признак от регистрации до дерева**

`core/src/callee.hpp` — поле в `Callee` и строка в LAYOUT-комментарии:

```cpp
///   cacheable     те же аргументы дают тот же ответ. У билтина — всегда:
///                 часов и флагов среди них нет. У хост-функции — из
///                 CHUPA_FN_CACHEABLE, и её отсутствие означает, что
///                 выражение с таким вызовом не кэшируется вовсе.
```

```cpp
    bool              cacheable = false;
```

`core/src/callee.cpp` — в `fromBuiltin` добавить `out.cacheable = true;` с комментарием, в `fromHost` — `out.cacheable = (fn.flags & CHUPA_FN_CACHEABLE) != 0;`.

`core/src/ast.hpp` — рядом с `markChecked`:

```cpp
    /// Помечает дерево некэшируемым: в нём есть вызов, который на тех же
    /// входах вправе ответить иначе (chupascript.h, CHUPA_FN_CACHEABLE).
    ///
    /// Отметка на дереве, а не на узле: вопрос, который по ней решается, —
    /// «годится ли прошлое значение ЭТОГО выражения», и один такой вызов где
    /// угодно в дереве отвечает «нет» за всё выражение.
    ///
    /// Ставится только на компиляции: список вызываемых там уже известен, и
    /// платить за это на каждом вычислении незачем.
    void markUncacheable() noexcept { cacheable_ = false; }
    [[nodiscard]] bool isCacheable() const noexcept { return cacheable_; }
```

```cpp
    bool cacheable_ = true;
```

`core/src/ast.cpp` — в `Ast::reset` рядом с `checked_ = false;` дописать `cacheable_ = true;`.

`core/src/check.cpp` — в `checkCall`, сразу после проверки эффектов (после блока `if (isHostCallee(callee.ref) && !callee.effectFree …)`):

```cpp
        // Не ошибка, а свойство: такое выражение законно, просто его нельзя
        // кэшировать. Дыра, которую это закрывает, — выражение вроде
        // format('${now()}'), у которого набор зависимостей пуст, а значит
        // сумма нулевая и попадание вечное.
        if (!callee.cacheable) { ast.markUncacheable(); }
```

`core/src/expression.hpp` — рядом с `source()`:

```cpp
    /// Годится ли прошлое значение этого выражения, пока его зависимости не
    /// двигались. Ложь — в дереве есть вызов, отвечающий на тех же входах
    /// иначе; evalTracked у такого выражения всегда отдаёт kDepsOverflow.
    [[nodiscard]] bool isCacheable() const noexcept { return ast_.isCacheable(); }
```

- [ ] **Step 4: Прогнать тесты**

Run: `cmake --build build-dbg -j && ctest --test-dir build-dbg -R 'Host|Cacheable|Check' --output-on-failure`
Expected: PASS

- [ ] **Step 5: Коммит**

```bash
git add core/src/callee.hpp core/src/callee.cpp core/src/ast.hpp core/src/ast.cpp \
        core/src/check.cpp core/src/expression.hpp core/tests/host_test.cpp
git commit -m "feat: выражение с часами внутри выглядело как выражение из одних литералов"
```

---

### Task 7: `evalTracked` — вычислить и заодно отдать зависимости

Одним вызовом, а не парой «вычислить» и «спросить зависимости»: второй вход в C стоил бы 9.2 нс ровно на промахе, а движок обошёл эти места, вычисляя, и сложить их ему бесплатно (спека §2.6, §7).

**Files:**
- Modify: `core/src/expression.hpp`, `core/src/expression.cpp`
- Modify: `core/src/context.hpp`
- Test: `core/tests/expression_test.cpp`, `core/tests/context_test.cpp`

**Interfaces:**
- Consumes: `DepSet` (задача 5), `Expression::isCacheable()` (задача 6)
- Produces: `Expression::evalTracked(Execution &, Value *out, Dep *deps, std::uint32_t *n, Diagnostic &)`; `Context::evalTracked(const Expression &, Value *out, Dep *deps, std::uint32_t *n, Diagnostic &)`

**Контракт заполнения `deps` — держится тестами шага 1:**

| исход | `*n` | `deps[i]` |
|---|---|---|
| успех, `k ≤ kMaxDeps` зависимостей | `k` | первые `k` — настоящие; хвост `{&kZeroEpoch, null}` |
| зависимостей больше потолка | `kDepsOverflow` | все `{nullptr, null}` |
| в дереве некэшируемый вызов | `kDepsOverflow` | все `{nullptr, null}` |
| результат — массив либо объект | `kDepsOverflow` | все `{nullptr, null}` |
| отказ вычисления | `kDepsOverflow` | все `{nullptr, null}` |

Строка про агрегат — это спека §2.8. Кэшировать там нечего: возврат ручки это копия шестнадцати байт, тогда как `String` стоит 45 нс. За этим стоит граница ответственности: **движок отвечает только за то, что прочитал сам.** Вернув ручку на массив, он ручается за саму ручку — что в ячейке лежит тот же массив, — и ни за что внутри; всё, что хост прочитает через неё своими `chupa_object_get`, его собственная зависимость. Длинная строка под правило **не** попадает, хотя тоже лежит в коробке: изменить `StringBox` в языке нечем, ручка на строку вечно свежая, и обобщение правила на строки выключило бы кэш ровно там, ради чего он затевался.

Хвост смотрит на вечный ноль затем, чтобы читатель складывал ровно `kMaxDeps` слов без ветвлений. На переполнении, наоборот, стоит `nullptr`: читатель, забывший посмотреть на `*n`, получит падение на первом же прогоне, а не молча застывший экран через неделю. Оба поведения — часть контракта, а не следствие реализации.

- [ ] **Step 1: Написать падающий тест**

Дописать в `core/tests/expression_test.cpp`:

```cpp
TEST(EvalTracked, AConstantFillsTheWholeSetWithTheEternalZero) {
    Store store;
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("42", store, &expr, diags, 4), 0u);

    Value out = Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.evalTracked(exec, &out, deps, &n, diag));

    EXPECT_EQ(n, 0u) << "ноль — законный ответ, и это не переполнение";
    for (const CS::Dep &dep : deps) {
        EXPECT_EQ(dep.epoch, &CS::kZeroEpoch);
        EXPECT_EQ(dep.owner.kind(), Value::Kind::Null);
    }
}

TEST(EvalTracked, TheTailPointsAtTheEternalZero) {
    // Читатель складывает ровно kMaxDeps слов, не заглядывая в счётчик, —
    // ради этого хвост обязан быть настоящим нулевым слагаемым, а не nullptr.
    Store store;
    CS::Deferred dead;
    store.setGlobal("a", Value::number(1.0), dead);
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("a", store, &expr, diags, 4), 0u);

    Value out = Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.evalTracked(exec, &out, deps, &n, diag));

    ASSERT_EQ(n, 1u);
    EXPECT_NE(deps[0].epoch, &CS::kZeroEpoch);
    for (std::uint32_t i = 1; i < CS::kMaxDeps; ++i) {
        EXPECT_EQ(deps[i].epoch, &CS::kZeroEpoch);
    }
}

TEST(EvalTracked, TheSumMovesExactlyWhenTheAnswerCanChange) {
    Store store;
    CS::Deferred dead;
    CS::Diagnostic setup;
    ASSERT_TRUE(CS::setVariable(store, dead, "users",
                                "[{'name': 'Вася'}, {'name': 'Петя'}]", setup));
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("users[0].name", store, &expr, diags, 4), 0u);

    Value out = Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.evalTracked(exec, &out, deps, &n, diag));
    ASSERT_EQ(n, 3u);

    const auto sum = [&deps]() {
        CS::Epoch total = 0;
        for (const CS::Dep &dep : deps) { total += *dep.epoch; }
        return total;
    };
    const CS::Epoch snapshot = sum();

    // Правка соседнего элемента читателя нулевого не задевает — то, ради чего
    // схема с коробками стоит своих денег (спека §3.2).
    const Value users = store.global("users");
    CS::objectSet(CS::arrayAt(users, 1), "name", Value::null(), store.clock(), dead);
    EXPECT_EQ(sum(), snapshot);

    // Правка своего — задевает.
    CS::objectSet(CS::arrayAt(users, 0), "name", Value::null(), store.clock(), dead);
    EXPECT_GT(sum(), snapshot);
}

TEST(EvalTracked, OverflowPoisonsTheSetSoAForgetfulReaderCrashesLoudly) {
    Store store;
    CS::Deferred dead;
    CS::Diagnostic setup;
    ASSERT_TRUE(CS::setVariable(store, dead, "u",
                                "{'a': {'b': {'c': {'d': {'e': 1}}}}}", setup));
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("u.a.b.c.d.e", store, &expr, diags, 4), 0u);

    Value out = Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.evalTracked(exec, &out, deps, &n, diag));

    EXPECT_EQ(n, CS::kDepsOverflow);
    for (const CS::Dep &dep : deps) { EXPECT_EQ(dep.epoch, nullptr); }
}

TEST(EvalTracked, AnAggregateResultIsNotCached) {
    // Спека §2.8: кэшировать нечего — возврат ручки это копия шестнадцати
    // байт. И граница ответственности: за содержимое агрегата движок не
    // ручается, читает его хост своими вызовами.
    Store store;
    CS::Deferred dead;
    CS::Diagnostic setup;
    ASSERT_TRUE(CS::setVariable(store, dead, "items", "[1, 2, 3]", setup));
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("items", store, &expr, diags, 4), 0u);

    Value out = Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.evalTracked(exec, &out, deps, &n, diag));

    EXPECT_EQ(out.kind(), Value::Kind::Array);
    EXPECT_EQ(n, CS::kDepsOverflow);
}

TEST(EvalTracked, ALongStringResultIsStillCached) {
    // Строка под правило §2.8 не попадает: мутирующих операций над строками в
    // языке нет, ручка на строку вечно свежая. Обобщив правило на строки, кэш
    // выключили бы ровно там, ради чего он затевался.
    Store store;
    CS::Deferred dead;
    CS::Diagnostic setup;
    ASSERT_TRUE(CS::setVariable(store, dead, "title",
                                "'строка заведомо длиннее пятнадцати байт'",
                                setup));
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("title", store, &expr, diags, 4), 0u);

    Value out = Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.evalTracked(exec, &out, deps, &n, diag));

    EXPECT_EQ(out.kind(), Value::Kind::String);
    EXPECT_EQ(n, 1u);
}

TEST(EvalTracked, AFailedEvaluationLeavesNothingToCache) {
    Store store;
    CS::Deferred dead;
    store.setGlobal("n", Value::number(1.0), dead);
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("n.field", store, &expr, diags, 4), 0u);

    Value out = Value::boolean(true);
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    EXPECT_FALSE(expr.evalTracked(exec, &out, deps, &n, diag));

    EXPECT_EQ(n, CS::kDepsOverflow);
    EXPECT_EQ(out.kind(), Value::Kind::Boolean) << "при отказе *out не трогается";
}

TEST(EvalTracked, AUnitFromAnotherStoreIsRefused) {
    Store mine;
    Store other;
    CS::Execution exec(mine);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("42", other, &expr, diags, 4), 0u);

    Value out = Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    EXPECT_FALSE(expr.evalTracked(exec, &out, deps, &n, diag));
    EXPECT_EQ(diag.code, CS::ErrorCode::Usage);
}
```

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

Run: `cmake --build build-dbg -j 2>&1 | grep -m3 'error'`
Expected: FAIL — `no member named 'evalTracked' in 'CS::Expression'`

- [ ] **Step 3: Написать метод**

`core/src/expression.hpp`, рядом с `eval`:

```cpp
    /// Вычисляет и заодно отдаёт зависимости — места, чьи эпохи движок
    /// прочитал по пути.
    ///
    /// deps — буфер вызывающего ровно на kMaxDeps записей. Заполняется
    /// ЦЕЛИКОМ и на всяком исходе: незанятый хвост смотрит на kZeroEpoch,
    /// чтобы читатель складывал ровно kMaxDeps слов, не заглядывая в *n и не
    /// ветвясь.
    ///
    /// *n — сколько записано, либо kDepsOverflow. Ноль — законный ответ и не
    /// то же самое, что переполнение: выражение из одних литералов не зависит
    /// ни от чего, его сумма всегда нулевая, и оно кэшируется навсегда.
    ///
    /// kDepsOverflow приходит в трёх случаях: мест по пути оказалось больше
    /// потолка; в дереве есть вызов, отвечающий на тех же входах иначе
    /// (isCacheable); вычисление отказало. Во всех трёх deps набивается
    /// nullptr'ами — читатель, забывший посмотреть на *n, падает на первом же
    /// прогоне, а не показывает застывший экран через неделю.
    ///
    /// Отдельного метода «спросить зависимости» нет намеренно: он стоил бы
    /// второго входа в C ровно на промахе.
    bool evalTracked(Execution &exec, Value *out, Dep *deps, std::uint32_t *n,
                     Diagnostic &diag) const;
```

`core/src/expression.cpp`:

```cpp
namespace {

/// Набивает набор так, чтобы читатель, не посмотревший на *n, упал сразу.
void poison(Dep *deps, std::uint32_t *n) noexcept {
    for (std::uint32_t i = 0; i < kMaxDeps; ++i) { deps[i] = Dep{nullptr, Value::null()}; }
    *n = kDepsOverflow;
}

}  // namespace

bool Expression::evalTracked(Execution &exec, Value *out, Dep *deps,
                             std::uint32_t *n, Diagnostic &diag) const {
    if (!exec.acceptsUnit(storeId_, diag)) {
        poison(deps, n);
        return false;
    }
    if (!evalExpression(ast_, source_, exec, out, diag)) {
        poison(deps, n);
        return false;
    }
    // Отметка с компиляции, а не разбор дерева здесь: список вызываемых
    // известен с компиляции, и платить за него на каждом промахе незачем.
    // Агрегат в результате — спека §2.8: кэшировать нечего, а ручаться за
    // содержимое движок и не может. Строка сюда НЕ попадает: изменить её в
    // языке нечем.
    const bool aggregate = out->kind() == Value::Kind::Array ||
                           out->kind() == Value::Kind::Object;
    if (!ast_.isCacheable() || aggregate || exec.deps().overflowed()) {
        poison(deps, n);
        return true;
    }

    const DepSet &found = exec.deps();
    for (std::uint32_t i = 0; i < kMaxDeps; ++i) {
        deps[i] = i < found.count() ? found.at(i) : Dep{&kZeroEpoch, Value::null()};
    }
    *n = found.count();
    return true;
}
```

`core/src/context.hpp`, рядом с `eval` — объявление; тело в `context.cpp` по образцу `Context::eval`:

```cpp
bool Context::evalTracked(const Expression &expr, Value *out, Dep *deps,
                          std::uint32_t *n, Diagnostic &diag) {
    EvaluationGuard guard(evaluating_);
    beginOperation();
    return expr.evalTracked(exec_, out, deps, n, diag);
}
```

- [ ] **Step 4: Прогнать тесты**

Run: `cmake --build build-dbg -j && ctest --test-dir build-dbg -R 'EvalTracked|Expression|Context' --output-on-failure`
Expected: PASS

- [ ] **Step 5: Коммит**

```bash
git add core/src/expression.hpp core/src/expression.cpp core/src/context.hpp \
        core/src/context.cpp core/tests/expression_test.cpp core/tests/context_test.cpp
git commit -m "feat: движок вычислял, но не отвечал на вопрос, надо ли было вычислять"
```

---

### Task 8: Дверь наружу — `chupa_expression_eval_tracked`

**Files:**
- Modify: `core/include/chupascript/chupascript.h`
- Modify: `core/src/c_api.cpp`
- Test: `core/tests/c_api_test.cpp`

**Interfaces:**
- Consumes: `Context::evalTracked` (задача 7)
- Produces: `ChupaEpoch`, `CHUPA_MAX_DEPS`, `CHUPA_DEPS_OVERFLOW`, `ChupaDep`, `chupa_expression_eval_tracked(ChupaContext *, ChupaExpression *, ChupaValue *out, ChupaDep deps[CHUPA_MAX_DEPS], uint32_t *n)`

Ошибка приезжает через `chupa_context_error`, как у `chupa_eval`, а не выходным `ChupaDiagnostic *`: такого типа в этом API нет, и заводить второй способ сообщать об отказе ради одной двери незачем. Набросок §2.6 спеки на этом месте расходится с домом — задача 11 правит спеку.

- [ ] **Step 1: Написать падающий тест**

Дописать в `core/tests/c_api_test.cpp`:

```cpp
TEST(CApiTracked, AScalarDependsOnOneCellAndHitsForever) {
    ChupaContext *ctx = chupa_context_create();
    ASSERT_TRUE(chupa_context_set_bool(ctx, "flag", 4, true));
    ChupaExpression *expr = chupa_compile_expression(ctx, "flag", 4);
    ASSERT_NE(expr, nullptr);

    ChupaValue out;
    ChupaDep deps[CHUPA_MAX_DEPS];
    uint32_t n = 0;
    ASSERT_TRUE(chupa_expression_eval_tracked(ctx, expr, &out, deps, &n));
    ASSERT_EQ(n, 1u);

    const auto sum = [&deps]() {
        uint64_t total = 0;
        for (const ChupaDep &dep : deps) { total += *dep.epoch; }
        return total;
    };
    const uint64_t snapshot = sum();

    // Ничего не писали — сумма не двинулась, читателю входить в C незачем.
    EXPECT_EQ(sum(), snapshot);

    ASSERT_TRUE(chupa_context_set_bool(ctx, "flag", 4, false));
    EXPECT_GT(sum(), snapshot);

    chupa_expression_destroy(expr);
    chupa_context_destroy(ctx);
}

TEST(CApiTracked, ABoxDependencyComesWithSomethingToHoldOnTo) {
    ChupaContext *ctx = chupa_context_create();
    ASSERT_TRUE(chupa_context_set_data(ctx, "user", 4, "{'name': 'Вася'}", 18));
    ChupaExpression *expr = chupa_compile_expression(ctx, "user.name", 9);
    ASSERT_NE(expr, nullptr);

    ChupaValue out;
    ChupaDep deps[CHUPA_MAX_DEPS];
    uint32_t n = 0;
    ASSERT_TRUE(chupa_expression_eval_tracked(ctx, expr, &out, deps, &n));
    ASSERT_EQ(n, 2u);

    EXPECT_EQ(chupa_value_kind(&deps[0].owner), CHUPA_KIND_NULL)
        << "у зависимости-ячейки владельца нет";
    EXPECT_EQ(chupa_value_kind(&deps[1].owner), CHUPA_KIND_OBJECT);

    // Держимся за коробку и переписываем переменную: адрес эпохи обязан
    // остаться читаемым, иначе следующий кадр прочтёт освобождённую память.
    chupa_value_retain(&deps[1].owner);
    const uint64_t held = *deps[1].epoch;
    ASSERT_TRUE(chupa_context_set_data(ctx, "user", 4, "{'name': 'Петя'}", 18));
    EXPECT_EQ(*deps[1].epoch, held) << "удержанная коробка не менялась";
    chupa_value_release(&deps[1].owner);

    chupa_expression_destroy(expr);
    chupa_context_destroy(ctx);
}

static bool alwaysSeven(ChupaContext *, const ChupaValue *, uint8_t,
                        ChupaValue *out) {
    chupa_make_number(out, 7.0);
    return true;
}

TEST(CApiTracked, AnUncacheableCallReportsOverflow) {
    // now() объявлена без CHUPA_FN_CACHEABLE: на тех же входах она вправе
    // ответить иначе, и набор зависимостей у выражения с ней пуст. Без этой
    // отметки такое выражение кэшировалось бы навсегда — часы бы встали.
    ChupaContext *ctx = chupa_context_create();
    ChupaFunction desc{};
    desc.name = "now";
    desc.name_len = 3;
    desc.min_args = 0;
    desc.max_args = 0;
    desc.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_EFFECT_FREE;
    desc.call = alwaysSeven;
    ASSERT_TRUE(chupa_register(ctx, &desc));

    ChupaExpression *expr = chupa_compile_expression(ctx, "now()", 5);
    ASSERT_NE(expr, nullptr);

    ChupaValue out;
    ChupaDep deps[CHUPA_MAX_DEPS];
    uint32_t n = 0;
    ASSERT_TRUE(chupa_expression_eval_tracked(ctx, expr, &out, deps, &n));

    EXPECT_EQ(n, CHUPA_DEPS_OVERFLOW);
    for (const ChupaDep &dep : deps) { EXPECT_EQ(dep.epoch, nullptr); }

    chupa_expression_destroy(expr);
    chupa_context_destroy(ctx);
}

TEST(CApiTracked, RefusedWhileACallbackIsRunning) {
    // Та же дверь, что и у chupa_eval: колбэк, дотянувшийся до вычисления на
    // том же контексте, слил бы список отложенного освобождения, на котором
    // стоит идущий обход. Проверяется тем же приёмом, что соседний тест на
    // chupa_eval: колбэк зовёт закрытую дверь и обязан получить отказ с
    // CHUPA_ERR_USAGE, а само вычисление — завершиться успешно.
    ChupaContext *ctx = chupa_context_create();
    ChupaFunction desc{};
    desc.name = "probe";
    desc.name_len = 5;
    desc.min_args = 0;
    desc.max_args = 0;
    desc.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_EFFECT_FREE | CHUPA_FN_CACHEABLE;
    desc.call = [](ChupaContext *inner, const ChupaValue *, uint8_t,
                   ChupaValue *out) -> bool {
        ChupaValue ignored;
        ChupaDep deps[CHUPA_MAX_DEPS];
        uint32_t n = 0;
        // Дверь закрыта: вернуть true отсюда нельзя.
        EXPECT_FALSE(chupa_expression_eval_tracked(inner, nullptr, &ignored,
                                                   deps, &n));
        ChupaError error;
        chupa_context_error(inner, &error);
        EXPECT_EQ(error.code, CHUPA_ERR_USAGE);
        chupa_make_number(out, 1.0);
        return true;
    };
    ASSERT_TRUE(chupa_register(ctx, &desc));

    ChupaExpression *expr = chupa_compile_expression(ctx, "probe()", 7);
    ASSERT_NE(expr, nullptr);

    ChupaValue out;
    ChupaDep deps[CHUPA_MAX_DEPS];
    uint32_t n = 0;
    EXPECT_TRUE(chupa_expression_eval_tracked(ctx, expr, &out, deps, &n));

    chupa_expression_destroy(expr);
    chupa_context_destroy(ctx);
}
```

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

Run: `cmake --build build-dbg -j 2>&1 | grep -m3 'error'`
Expected: FAIL — `use of undeclared identifier 'CHUPA_MAX_DEPS'`

- [ ] **Step 3: Объявить типы в публичном заголовке**

В `core/include/chupascript/chupascript.h`, отдельным разделом перед «Evaluation»:

```c
/* ─── Кэш выражений: эпохи ───────────────────────────────────────────────
 *
 * Движок НЕ отдаёт кэшированное значение. Он отвечает на вопрос «менялось ли
 * то, от чего это выражение зависит»; значение, которое хост уже держит, хост
 * переиспользует сам. Причина в String: получить байты дёшево, а собрать из
 * них строку стоит ~45 нс — почти всё чтение целиком.
 *
 * Эпоха — номер на монотонной ленте контекста. Всякая мутация и всякое
 * рождение агрегата берут из неё следующий номер. Номер только растёт, и
 * увеличивает его только движок: состояние «я это видел» живёт у читателя,
 * поэтому читателей можно заводить и убивать когда угодно, а через границу не
 * идёт ни одной записи.
 *
 * Хост читает эпоху по адресу своей родной идиомой, без вызова:
 *
 *   iOS / Swift      UnsafePointer<UInt64>.pointee
 *   Android / JNI    NewDirectByteBuffer однажды, дальше getLong
 *   Web / WASM       адрес есть смещение в линейной памяти; вью поверх
 *                    memory.buffer
 *
 * «Адрес в памяти движка», а не «указатель C»: в wasm32 указатель и есть
 * 32-битное смещение, и один и тот же ABI годится всем троим. Рост памяти
 * WASM отцепляет вью — смещение переживает рост, вью нет; пересоздать вью
 * обязана обёртка. Порядок байтов движковый: для JNI это
 * ByteBuffer.order(nativeOrder()).
 *
 * Сумма эпох как снимок — приём читающей стороны, а не часть контракта.
 * Swift и JVM складывают, потому что 64-битная арифметика им даётся даром;
 * JS сравнивает половины через Uint32Array и ни одного BigInt не заводит.
 * Поэтому движок суммы не возвращает: он отдаёт эпохи. */

typedef uint64_t ChupaEpoch;

/* Сколько зависимостей движок записывает у одного выражения. Больше —
 * выражение не кэшируется вовсе.
 *
 * ╔══════════════════════════════════════════════════════════════════════╗
 * ║ ЭТО ABI. Обёртка, собранная при 4, и движок, пересобранный при 8,    ║
 * ║ разойдутся МОЛЧА: движок запишет больше, чем обёртка прочитает.      ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * Число фиксируется до первого релиза и дальше не двигается. Сверять его в
 * рантайме было бы вторым механизмом там, где хватает одного решения. */
#define CHUPA_MAX_DEPS 4

/* Кэшировать нельзя: мест по пути больше CHUPA_MAX_DEPS, либо в выражении
 * есть вызов, который на тех же входах вправе ответить иначе (см.
 * CHUPA_FN_CACHEABLE), либо вычисление отказало. */
#define CHUPA_DEPS_OVERFLOW 0xffffffffu

/* Одна зависимость выражения: что читать и за что держаться.
 *
 * epoch — адрес эпохи. Читается прямо, без вызова, на каждом кадре.
 * owner — то, внутри чего эта эпоха лежит. Держать его ОБЯЗАТЕЛЬНО:
 *         chupa_value_retain при захвате набора, chupa_value_release при
 *         следующем захвате. Без этого адрес указывает внутрь коробки,
 *         которую счётчик ссылок вправе освободить, и следующий кадр прочтёт
 *         освобождённую память.
 *         CHUPA_KIND_NULL — зависимость это ячейка глобальной переменной;
 *         её эпоха живёт столько же, сколько контекст, и держать нечего.
 *         retain и release на таком значении — no-op, так что цикл у хоста
 *         остаётся без ветвлений. */
typedef struct ChupaDep {
    const ChupaEpoch *epoch;
    ChupaValue        owner;
} ChupaDep;

/* Вычислить и заодно отдать зависимости.
 *
 * deps — буфер вызывающего ровно на CHUPA_MAX_DEPS записей. Заполняется
 *        ЦЕЛИКОМ и на всяком исходе. Незанятый хвост смотрит на вечный ноль
 *        движка: читатель складывает ровно CHUPA_MAX_DEPS слов, не заглядывая
 *        в *n и не ветвясь.
 * n    — сколько записано, либо CHUPA_DEPS_OVERFLOW. Ноль — законный ответ и
 *        НЕ то же самое, что переполнение: выражение из одних литералов не
 *        зависит ни от чего и кэшируется навсегда.
 *
 * ╔══════════════════════════════════════════════════════════════════════╗
 * ║ При CHUPA_DEPS_OVERFLOW каждый deps[i].epoch равен NULL.             ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * Намеренно: читатель, забывший посмотреть на *n, падает на первом прогоне,
 * а не показывает застывший экран через неделю.
 *
 * Отказ сообщается как у chupa_eval: false и chupa_context_error. Значение в
 * *out боррowed по тому же правилу 1 заголовка, что и у chupa_eval. */
CHUPA_API CHUPA_MUST_USE bool
chupa_expression_eval_tracked(ChupaContext *ctx, ChupaExpression *e,
                              ChupaValue *out, ChupaDep deps[CHUPA_MAX_DEPS],
                              uint32_t *n);
```

Там же: `static_assert`-эквивалент в `c_api.cpp` — `static_assert(CHUPA_MAX_DEPS == CS::kMaxDeps, "потолок объявлен дважды и разошёлся");` и то же для `CHUPA_DEPS_OVERFLOW`/`CS::kDepsOverflow`.

- [ ] **Step 4: Реализовать дверь**

В `core/src/c_api.cpp`, рядом с `chupa_eval`:

```cpp
bool chupa_expression_eval_tracked(ChupaContext* ctx, ChupaExpression* e,
                                   ChupaValue* out, ChupaDep deps[CHUPA_MAX_DEPS],
                                   uint32_t* n) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return false; }
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();

    CS::Value value = CS::Value::null();
    CS::Dep found[CS::kMaxDeps];
    std::uint32_t count = 0;
    CS::Diagnostic diag;
    if (!c->impl.evalTracked(expr->impl, &value, found, &count, diag)) {
        for (std::uint32_t i = 0; i < CHUPA_MAX_DEPS; ++i) {
            deps[i].epoch = nullptr;
            toC(CS::Value::null(), &deps[i].owner);
        }
        *n = CHUPA_DEPS_OVERFLOW;
        c->setError(diag);
        return false;
    }
    // See chupa_eval above: same reason for the explicit clear on Ok.
    c->clearError();
    for (std::uint32_t i = 0; i < CHUPA_MAX_DEPS; ++i) {
        deps[i].epoch = found[i].epoch;
        toC(found[i].owner, &deps[i].owner);
    }
    *n = count;
    toC(value, out);
    return true;
}
```

- [ ] **Step 5: Сверить имена полей дескриптора и приём возврата числа**

Тесты шага 1 написаны по памяти о форме `ChupaFunction` и о том, как соседи файла кладут число в `ChupaValue`. Перед прогоном сверить оба с `core/include/chupascript/chupascript.h` (`typedef struct ChupaFunction`) и с ближайшим тестом `c_api_test.cpp`, который возвращает число из колбэка, и поправить, если имена расходятся. Смысл тестов от этого не меняется — меняется запись.

- [ ] **Step 6: Прогнать всё, включая санитайзеры**

Run: `cmake --build build-dbg -j && ctest --test-dir build-dbg --output-on-failure && ./tools/asan.sh`
Expected: PASS, ASan чист

- [ ] **Step 7: Коммит**

```bash
git add core/include/chupascript/chupascript.h core/src/c_api.cpp core/tests/c_api_test.cpp
git commit -m "feat: хосту нечем было спросить, изменилось ли то, от чего зависит выражение"
```

---

### Task 9: Читатель на Swift

Снимок хранится у читателя, а не в `Expression`: одно выражение могут держать два виджета, и кэш у каждого свой (спека §2.5).

**Files:**
- Create: `Sources/ChupaScript/CachedExpression.swift`
- Create: `Tests/ChupaScriptTests/CachedExpressionTests.swift`

**Interfaces:**
- Consumes: `chupa_expression_eval_tracked`, `ChupaDep`, `CHUPA_MAX_DEPS` (задача 8); `Expression<T>` (`Sources/ChupaScript/Expression.swift`)
- Produces: `public protocol CSCached { static func chupaValue(_ value: ChupaValue) throws -> Self? }`; `public final class CachedExpression<T: CSCached>` с `init(_ expression: Expression<T>)` и `func value() throws -> T?`

- [ ] **Step 1: Написать падающий тест**

Создать `Tests/ChupaScriptTests/CachedExpressionTests.swift`:

```swift
import XCTest
@testable import ChupaScript

final class CachedExpressionTests: XCTestCase {

    func testRepeatedReadsDoNotReenterTheEngine() throws {
        let ctx = Context()
        try ctx.set("name", "Вася")
        let cached = CachedExpression(try ctx.compile(expression: "name") as Expression<String>)

        XCTAssertEqual(try cached.value(), "Вася")
        XCTAssertEqual(try cached.value(), "Вася")
        XCTAssertEqual(cached.missCount, 1, "второй кадр обязан обойтись без входа в C")
    }

    func testAWriteIsSeenOnTheNextRead() throws {
        let ctx = Context()
        try ctx.set("name", "Вася")
        let cached = CachedExpression(try ctx.compile(expression: "name") as Expression<String>)
        XCTAssertEqual(try cached.value(), "Вася")

        try ctx.set("name", "Петя")

        XCTAssertEqual(try cached.value(), "Петя")
        XCTAssertEqual(cached.missCount, 2)
    }

    func testTouchingANeighbourDoesNotMiss() throws {
        // То, ради чего схема с коробками стоит своих денег (спека §3.2).
        let ctx = Context()
        try ctx.set("users", text: "[{'name': 'Вася'}, {'name': 'Петя'}]")
        let cached = CachedExpression(try ctx.compile(expression: "users[0].name") as Expression<String>)
        XCTAssertEqual(try cached.value(), "Вася")

        try ctx.run(try ctx.compile(script: "users[1].name = 'Аня'"))

        XCTAssertEqual(try cached.value(), "Вася")
        XCTAssertEqual(cached.missCount, 1)
    }

    func testADependencyOutlivingItsVariableIsStillSafeToRead() throws {
        // Читатель держит коробки из набора ретейном, поэтому переписанная
        // переменная не уводит из-под него память (спека §2.7). Проверяется
        // под ASan сборкой пакета: `swift test -Xswiftc -sanitize=address`.
        let ctx = Context()
        try ctx.set("users", text: "[{'name': 'Вася'}]")
        let cached = CachedExpression(try ctx.compile(expression: "users[0].name") as Expression<String>)
        XCTAssertEqual(try cached.value(), "Вася")

        try ctx.set("users", text: "[{'name': 'Петя'}]")

        XCTAssertEqual(try cached.value(), "Петя")
    }

    func testAnUncacheableExpressionRecomputesEveryTime() throws {
        // Выражение с некэшируемым вызовом: n == CHUPA_DEPS_OVERFLOW, читатель
        // ставит себе флаг один раз и больше в набор не смотрит.
        let ctx = Context()
        try ctx.register("now", flags: [.returnsValue, .effectFree]) { 7.0 }
        let cached = CachedExpression(
            try ctx.compile(expression: "format('${now()}')") as Expression<String>)

        _ = try cached.value()
        _ = try cached.value()

        XCTAssertEqual(cached.missCount, 2)
    }

    func testAConstantNeverMissesTwice() throws {
        let ctx = Context()
        let cached = CachedExpression(try ctx.compile(expression: "42") as Expression<Double>)

        XCTAssertEqual(try cached.value(), 42)
        XCTAssertEqual(try cached.value(), 42)
        XCTAssertEqual(cached.missCount, 1)
    }
}
```

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

Run: `swift test 2>&1 | grep -m3 'error'`
Expected: FAIL — `cannot find 'CachedExpression' in scope`

- [ ] **Step 3: Написать читателя**

Создать `Sources/ChupaScript/CachedExpression.swift`:

```swift
import ChupaScriptC

/// Тип, который умеет собраться из уже вычисленного значения.
///
/// Отдельно от `CSValue`, и это не дублирование: `CSValue.chupaEval` принимает
/// **выражение** и сам зовёт типизированный вход C API, а кэшу вход не нужен —
/// у него на руках уже готовый `ChupaValue`, привезённый одним общим вызовом
/// вместе с зависимостями. Второго входа в C ради типа здесь быть не должно.
public protocol CSCached {
    /// `nil` — значение оказалось null. Бросает при несовпадении вида.
    static func chupaValue(_ value: ChupaValue) throws -> Self?
}

/// Выражение со снимком: пересчитывает себя, только когда что-то из того, от
/// чего оно зависит, сдвинулось.
///
/// **Снимок живёт здесь, а не в `Expression`.** Одно скомпилированное
/// выражение могут держать два виджета, и кэш у каждого свой (спека §2.5).
///
/// **Сумма, а не массив эпох.** Слагаемые только растут, значит сумма только
/// растёт: выросла хоть одна — сумма строго выросла, и погасить её нечем. XOR
/// на это место не годится, он монотонность не сохраняет: `A=2,B=4` даёт 6, и
/// после `A=3,B=5` снова 6 — два изменения погасили друг друга.
///
/// **Складываются всегда все `CHUPA_MAX_DEPS` слов.** Незанятый хвост движок
/// направляет на вечный ноль, поэтому проверка идёт без ветвлений и без
/// чтения счётчика: четыре загрузки и три сложения.
///
/// **Коробки из набора удерживаются.** Иначе адрес эпохи указывал бы внутрь
/// коробки, которую счётчик ссылок вправе освободить (спека §2.7).
public final class CachedExpression<T: CSCached> {

    private let expression: Expression<T>

    /// Адреса эпох кортежем, а не массивом: массив — аллокация, ARC и
    /// проверки границ на каждом кадре, а число слагаемых известно на
    /// компиляции (CHUPA_MAX_DEPS — часть ABI).
    private var epochs: (UnsafePointer<UInt64>?, UnsafePointer<UInt64>?,
                         UnsafePointer<UInt64>?, UnsafePointer<UInt64>?)

    /// Коробки, за которые держимся. Отпускаются при следующем захвате.
    private var owners: (ChupaValue, ChupaValue, ChupaValue, ChupaValue)

    private var snapshot: UInt64 = 0
    private var cached: T?
    private var hasValue = false

    /// Выражение с некэшируемым вызовом либо со слишком длинным путём: набор
    /// не годится, считаем каждый раз. Флаг ставится один раз, при первом
    /// захвате, и дальше набор не читается вовсе.
    private var uncacheable = false

    /// Сколько раз пришлось войти в движок. Читают тесты и бенчмарки: доля
    /// попаданий — число, которое §5.2 требует печатать рядом с результатом.
    public private(set) var missCount = 0

    public init(_ expression: Expression<T>) {
        self.expression = expression
        self.epochs = (nil, nil, nil, nil)
        self.owners = (ChupaValue(), ChupaValue(), ChupaValue(), ChupaValue())
    }

    deinit { releaseOwners() }

    public func value() throws -> T? {
        if hasValue && !uncacheable && sum() == snapshot { return cached }

        var out = ChupaValue()
        var deps = [ChupaDep](repeating: ChupaDep(), count: Int(CHUPA_MAX_DEPS))
        var n: UInt32 = 0
        missCount += 1
        guard chupa_expression_eval_tracked(expression.context.handle,
                                           expression.handle, &out, &deps, &n)
        else { throw expression.context.makeError() }

        capture(deps: deps, n: n)
        cached = try T.chupaValue(out)
        hasValue = true
        return cached
    }

    private func capture(deps: [ChupaDep], n: UInt32) {
        releaseOwners()
        uncacheable = n == CHUPA_DEPS_OVERFLOW
        guard !uncacheable else {
            epochs = (nil, nil, nil, nil)
            owners = (ChupaValue(), ChupaValue(), ChupaValue(), ChupaValue())
            return
        }
        // retain на скаляре и на пустом значении — no-op, поэтому ветвиться по
        // виду зависимости не надо: у ячейки owner пуст по контракту.
        for i in 0..<Int(CHUPA_MAX_DEPS) { withUnsafePointer(to: deps[i].owner) { chupa_value_retain($0) } }
        epochs = (deps[0].epoch, deps[1].epoch, deps[2].epoch, deps[3].epoch)
        owners = (deps[0].owner, deps[1].owner, deps[2].owner, deps[3].owner)
        snapshot = sum()
    }

    private func releaseOwners() {
        guard hasValue && !uncacheable else { return }
        withUnsafePointer(to: owners.0) { chupa_value_release($0) }
        withUnsafePointer(to: owners.1) { chupa_value_release($0) }
        withUnsafePointer(to: owners.2) { chupa_value_release($0) }
        withUnsafePointer(to: owners.3) { chupa_value_release($0) }
    }

    /// Четыре загрузки и три сложения, без ветвлений. `&+` намеренно:
    /// переполнение шестидесяти четырёх бит здесь — не ошибка, а событие,
    /// до которого нужно 2^64 инкрементов.
    private func sum() -> UInt64 {
        guard let a = epochs.0, let b = epochs.1,
              let c = epochs.2, let d = epochs.3 else { return 0 }
        return a.pointee &+ b.pointee &+ c.pointee &+ d.pointee
    }
}

extension Double: CSCached {
    public static func chupaValue(_ value: ChupaValue) throws -> Double? {
        var v = value
        switch chupa_value_kind(&v) {
        case CHUPA_KIND_NULL:   return nil
        case CHUPA_KIND_NUMBER: return chupa_value_number(&v)
        default: throw Error(code: .type, message: "value is not a number", offset: nil)
        }
    }
}

extension Bool: CSCached {
    public static func chupaValue(_ value: ChupaValue) throws -> Bool? {
        var v = value
        switch chupa_value_kind(&v) {
        case CHUPA_KIND_NULL: return nil
        case CHUPA_KIND_BOOL: return chupa_value_bool(&v)
        default: throw Error(code: .type, message: "value is not a boolean", offset: nil)
        }
    }
}

extension String: CSCached {

    /// Байты берутся у значения, а не у контекста, и это важно: у кэша на
    /// руках уже готовый ChupaValue, и лишний вход в C ради тех же байт
    /// съел бы ровно тот выигрыш, ради которого всё затевалось.
    ///
    /// Кодировка не проверяется — то же решение и по той же причине, что в
    /// CSValue.swift: ревалидация была тремя четвертями цены чтения длинной
    /// строки, а всё, что попадает в движок, пришло из String.utf8.
    public static func chupaValue(_ value: ChupaValue) throws -> String? {
        var v = value
        switch chupa_value_kind(&v) {
        case CHUPA_KIND_NULL: return nil
        case CHUPA_KIND_STRING:
            var bytes: UnsafePointer<CChar>?
            var length = 0
            chupa_value_string(&v, &bytes, &length)
            return String.chupaFromValidUTF8(bytes, count: length)
        default: throw Error(code: .type, message: "value is not a string", offset: nil)
        }
    }
}

extension CSCached where Self: RawRepresentable, Self.RawValue: CSCached {

    /// Умолчание, а не вторая перегрузка, — ровно по той причине, что
    /// разобрана у CSValue: две перегрузки с одинаковой сигнатурой ломались от
    /// чужого ретроактивного конформанса String: RawRepresentable.
    public static func chupaValue(_ value: ChupaValue) throws -> Self? {
        guard let raw = try RawValue.chupaValue(value) else { return nil }
        guard let wrapped = Self(rawValue: raw) else {
            throw Error(code: .unrepresentable,
                        message: "'\(raw)' is not a valid \(Self.self)",
                        offset: nil)
        }
        return wrapped
    }
}
```

Тип результата у кэша обязан удержать сам `ChupaValue` до разбора: значение борроwed и живёт до следующей операции над контекстом (`chupascript.h`, правило 1). Здесь это выполняется само собой — между вызовом и разбором движок не трогается ничем.

- [ ] **Step 4: Прогнать тесты**

Run: `swift test 2>&1 | tail -20`
Expected: PASS

- [ ] **Step 5: Прогнать под ASan**

Run: `swift test -Xswiftc -sanitize=address 2>&1 | tail -20`
Expected: PASS — это и есть проверка того, что удержание зависимостей работает

- [ ] **Step 6: Коммит**

```bash
git add Sources/ChupaScript/CachedExpression.swift Tests/ChupaScriptTests/CachedExpressionTests.swift
git commit -m "feat: обёртка пересобирала String на каждом кадре, даже когда ничего не менялось"
```

---

### Task 10: Замеры и решение по потолку

Состав замеров задан спекой §5 **до** первого прогона именно затем, чтобы отбирать было нечего. Ни одна строка не выбирается по результату и ни одна не выпадает из отчёта.

**Files:**
- Create: `benchmarks/cache_benchmark.cpp`
- Modify: `benchmarks/CMakeLists.txt`
- Create: `docs/benchmarks/expression-cache-2026-08-21.md`

**Interfaces:**
- Consumes: `Expression::evalTracked` (задача 7), `CS::Context`
- Produces: отчёт по правилам §5.4; решение по `CHUPA_MAX_DEPS` (§9), зафиксированное в заголовке

- [ ] **Step 1: Написать бенчмарк**

Создать `benchmarks/cache_benchmark.cpp`. Обязательный состав — шесть строк, все меряются всегда (§5.1):

| строка | зачем | зависимостей |
|---|---|---|
| `42` | константа: набор пуст, попадание вечное | 0 |
| `button_enabled` | голый скаляр — массовый props | 1 |
| `user.name` | один сегмент — самый частый осмысленный props | 2 |
| `users[0].name` | индекс плюс поле | 3 |
| `a > 0 && user.name != ''` | две переменные и путь | 3 |
| `u.a.b.c.d.e` | сверх потолка: **не кэшируется**, платит проверку впустую | overflow |

Три режима на каждую строку (§5.2), и каждый — отдельный `BENCHMARK`:

1. `BM_Cache_<Строка>_AllHits` — между кадрами не пишется ничего. Верхняя граница выигрыша.
2. `BM_Cache_<Строка>_NoHits` — каждый кадр пишет переменную, от которой зависит всё: прогресс анимации, таймер. Здесь кэш платит и не получает; это число решает, не сделали ли мы хуже.
3. `BM_Cache_<Строка>_Mixed` — доля попаданий берётся из устройства потребителя (одна запись → полная раскладка → чтение всех выражений), а не назначается. Доля печатается рядом с числом через `state.counters["hit_rate"]`; число без указанной доли — не результат.

Плюс `BM_Cache_<Строка>_Baseline` на каждую строку — то же выражение через `chupa_eval`, без кэша, в том же прогоне и на том же дереве. §5.4 требует сравнения с версией без кэша из того же коммита, и одна бинарка это условие выполняет буквально.

Читатель на стороне бенчмарка — та же арифметика, что в `CachedExpression.swift`: сумма четырёх слов, безусловные `retain`/`release` на владельцах.

Отдельно — прогон с **вытесненным** кэшем процессора (§5.3): вращение множества деревьев по кругу, как в `layout-synthetics-2026-08-18`, а не прогулка по буферу. Главный подозреваемый на ухудшение — обращение к заголовку `ObjectBox`, к которому на попадании иначе не пришли бы вовсе.

Дописать `cache_benchmark.cpp` в `add_executable(chupascript_benchmarks …)`.

- [ ] **Step 2: Собрать и прогнать**

Run:
```bash
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks --benchmark_filter='Cache' \
    --benchmark_repetitions=7 --benchmark_report_aggregates_only=true
```
Expected: числа по всем восемнадцати сочетаниям плюс вытесненный прогон

- [ ] **Step 3: Померить цену записи и раскладку**

§5.5 требует двух чисел помимо выражений:

```bash
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_filter='BM_Eval_AssignHot|BM_Eval_ScriptHot|BM_Eval_Constant' \
    --benchmark_repetitions=7 --benchmark_report_aggregates_only=true
```

Сравнить с числами до задачи 1 (собрать тот же бенчмарк на `git stash` либо на коммите `637d495`-предке ветки). Ожидание: в шуме. Инкремент эпохи и запись зависимостей платят все, включая тех, кто кэшем не пользуется, — и `BM_Eval_Constant` это ловит.

Затем прогнать синтетику раскладки `docs/benchmarks/layout-synthetics-2026-08-18` целиком — единственная цифра, отвечающая на вопрос «стало ли лучше на самом деле».

- [ ] **Step 4: Решить потолок (§9 спеки)**

Пересобрать с `-DCHUPA_MAX_DEPS_OVERRIDE=8` (временный `target_compile_definitions` на цели бенчмарков; в заголовке `#ifndef CHUPA_MAX_DEPS` вокруг `#define`) и прогнать те же восемнадцать сочетаний. Решение принимается по двум числам: сколько выражений реального экрана перестало переполняться и на сколько подорожала проверка у массового скаляра.

Решение — **однократное**: после него временный `#ifndef` и определение сборки удаляются, число остаётся в заголовке жёстко. Померить один раз и не трогать.

- [ ] **Step 5: Написать отчёт**

Создать `docs/benchmarks/expression-cache-2026-08-21.md` по правилам §5.4:

- **Строки, где стало хуже, идут первыми.** Не в конце, не в сноске.
- **Промах опубликован рядом с попаданием**, из того же прогона.
- **Итог выносится по смеси и по враждебному режиму**, а не по лучшей строке.
- **Сравнение — на одном дереве и одних данных** с версией без кэша из того же коммита.
- Ни одна строка не удаляется задним числом; бессмысленная — с причиной рядом.
- Шапка как у соседних отчётов: дата, машина, компилятор, флаги, метод.
- Отдельным разделом — решение по потолку и числа, на которых оно стоит.

**Условие отказа (§5.6):** если враждебный режим показал заметную деградацию либо вытесненный кэш — что попадание на пути дороже ожидаемого, область режется до выражений без точек. Это остаётся выигрышем: скаляры и есть большинство. Решение принимается по числам, а не переголосовыванием дизайна. Урезание оформляется отдельным коммитом с отчётом в качестве обоснования.

- [ ] **Step 6: Коммит**

```bash
git add benchmarks/cache_benchmark.cpp benchmarks/CMakeLists.txt \
        docs/benchmarks/expression-cache-2026-08-21.md \
        core/include/chupascript/chupascript.h
git commit -m "bench: выигрыш кэша был обещанием, пока рядом не встали цена промаха и враждебный режим"
```

---

### Task 11: Спека и бэклог договаривают то, что решено при реализации

**Files:**
- Modify: `docs/superpowers/specs/2026-08-20-expression-cache-design.md`
- Modify: `docs/backlog.md` (B29)

- [ ] **Step 1: Внести три решения в спеку**

- §2.6 — сигнатура: `ChupaDep` вместо голого адреса, `stamp` убран, отказ через `chupa_context_error`. Добавить абзац: набор отдаёт и коробку-владельца, потому что §2.7 требует ретейна, а из адреса эпохи коробку не удержать.
- §2.4 — снять противоречие: движок суммы не возвращает вовсе, сумма целиком приём читающей стороны.
- §2.6 — правило заполнения: набор заполняется целиком, хвост на вечный ноль, переполнение набивается `nullptr`; читатель складывает `CHUPA_MAX_DEPS` слов без ветвлений.
- §2.3 — дописать третий случай некэшируемости: вызов без `CHUPA_FN_CACHEABLE`. Поправить обоснование динамического набора: детерминированность выражений перестала быть безусловной с приходом хост-функций (`docs/semantics.md` §8.9), и `now()` — не лишний пересчёт, а дыра.
- §3.2 — в список «лишний пересчёт, но не дыра» ничего не добавляется; дыра, найденная в разборе 21.08.2026, названа отдельно и закрыта статически.
- Шапка — статус: план написан, ссылка на `docs/superpowers/plans/2026-08-21-expression-cache.md`.

- [ ] **Step 2: Закрыть B29**

В `docs/backlog.md` пометить B29 закрытым, со ссылкой на спеку и план; оставить два вывода B29, отменённых §8 спеки, помеченными как устаревшие — они уже так помечены, проверить формулировку.

- [ ] **Step 3: Прогнать всё в последний раз**

Run:
```bash
cmake --build build-dbg -j && ctest --test-dir build-dbg --output-on-failure
swift test
./tools/asan.sh && ./tools/tsan.sh
```
Expected: PASS везде

- [ ] **Step 4: Коммит**

```bash
git add docs/superpowers/specs/2026-08-20-expression-cache-design.md docs/backlog.md
git commit -m "docs: спека расходилась с тем, что выяснилось при реализации, в трёх местах"
```

---

## Что не входит

- **Журнал изменений (§6 спеки)** — `chupa_context_changes`, дедупликация битом, потолок и флаг переполнения, а с ними снос `VariablesStorage.swift` и подписки по имени в `ok/sdk/OKBDUI`. Спека сама называет это обязательным продолжением, а не частью работы: кэш работает и без журнала, а пока журнала нет, хост вправе обойти виджеты и проверить каждому его сумму (§6.6). Отдельный план.
- **Ранняя отсечка `guard variable.value != value`** (§6.1) — переезжает в `setGlobal` вместе с журналом, там же и меряется.
- **Обёртки для Android и Web.** Контракт спроектирован под них (§3.3), реализуются отдельно.
- **Где хост хранит значения** — его дело.
- **Пропуск невидимых вью**, который `docs/grammar.md` §6.3 разрешает отдельно.

## Порядок

Задачи 1 → 2 → 3 → 4 → 5 → 6 → 7 → 8 идут строго по цепочке: каждая следующая стоит на типах предыдущей. Задача 6 (некэшируемые вызовы) от 5 не зависит и может идти параллельно с 5, если исполнителей двое. Задачи 9 и 10 обе стоят на 8; 10 не зависит от 9. Задача 11 закрывает.
