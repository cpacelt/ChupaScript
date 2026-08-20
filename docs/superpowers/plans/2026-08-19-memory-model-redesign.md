# Модель памяти: переработка — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** переписать модель памяти движка так, чтобы всякое `Value` было
самодостаточным: короткие строки лежат в самом значении, длинные и агрегаты —
в коробке со счётчиком ссылок, арены операции не существует, а граница с
хостом передаёт значение по адресу.

**Architecture:** `Value` остаётся шестнадцатью байтами и получает байт тега
вместо пары «вид, регион»: три бита вида, четыре бита длины встроенной строки,
один бит «встроенная либо коробка». Арена операции снимается целиком, вместе с
`Store::Role`, `Execution::promote` и правилом «строка годна до следующей
операции»; собиратель `format` переезжает в `Execution` обычным членом.
Агрегат размещается одной аллокацией — хвостом за заголовком коробки. C API
переводится на передачу значения по адресу, двузначный исход и одну структуру
ошибки.

**Tech Stack:** C++17 без расширений, CMake, GoogleTest, Google Benchmark,
SwiftPM для обёртки, ASan и TSan через `tools/`.

**Spec:** `docs/superpowers/specs/2026-08-19-memory-model-redesign-design.md`

## Global Constraints

Требования спеки §3, действующие на **каждую** задачу этого плана. Требования
каждой задачи неявно включают этот раздел.

- **К1. Язык комментариев — английский**, технический регистр. Только для
  нового и переписанного кода. Файлы, которых план не касается (`lexer`,
  `parser`, `ast` кроме литералов, `check`, `operator`, `builtin` кроме
  перечисленных мест), остаются с русскими комментариями.
- **К2. Схема раскладки первой.** Всякий тип, который чем-то владеет,
  начинается с диаграммы: что где лежит и кто чему хозяин.
- **К3. Каждое существительное — названная сущность.** Не «то, что заводит
  хост», а `the global variables the host defines`.
- **К4. У всякого утверждения о времени жизни названы оба конца.** Не «живёт
  столько же», а `is destroyed with the Context that owns it`.
- **К5. Одна строка на неочевидное решение**, и в ней — отвергнутая
  альтернатива с причиной.
- **К6. Повествование о ходе мысли — в спеку и в сообщение коммита**, не в
  заголовок типа.
- **К7.** C++17 без расширений компилятора. `static_assert(sizeof(Value) == 16)`
  и `static_assert(std::is_trivially_copyable_v<Value>)` не снимаются ни в одной
  задаче.
- **Семантика языка не меняется.** `docs/semantics.md` правится ровно в одном
  месте — ограничение про циклы (задача 10). `docs/grammar.md` не правится.
- **Сообщения коммитов** — в стиле репозитория: одна строка, по-русски,
  о том, какое несоответствие снято («что было не так»), не «что сделано».
  В конце — трейлер `Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>`.

Образец комментария, задающий планку (спека §3):

```cpp
/// Per-context state that outlives every evaluation: the global variables the
/// host defines, and the field-name table that objects intern their keys into.
/// A Store is owned by exactly one Context and is destroyed with it.
///
///   Store
///    ├── names_    vector<char>         global-variable name bytes, append-only
///    ├── slots_    vector<GlobalName>   name -> slot, sorted by name bytes
///    ├── values_   vector<Value>        slot -> value; a ROOT: holds one reference
///    └── keys_     KeyTable*            field names, shared with every ObjectBox
///
/// A slot index stays valid for the lifetime of its Store: values_ only grows
/// and its elements never move. check() therefore resolves a name to a slot
/// once, during compilation, and eval() indexes values_ directly (check.cpp).
```

---

## Порядок задач и почему он такой

Спека называет три рубежа замеров в порядке Р2 → Р3 → Р4. План ставит **Р3
раньше Р2**, и это единственное отступление от порядка спеки; сами решения не
меняются.

Причина: пока арена жива, `Value` обязано уметь хранить пару «смещение,
длина» в арену. Встроенная строка занимает те же байты. Сделать Р2 первым —
значит выдумать временную раскладку под `Region::Scratch` внутри нового тега и
через одну задачу её удалить. Обратный порядок этого не требует: снятие арены
(Р3) удаляет `Region` целиком, и Р2 приходит на `Value`, где выбирать больше
не из чего.

Цена — один промежуточный коммит, на котором `format` длиннее пятнадцати байт
уже платит аллокацию, а встроенных строк ещё нет. Замер на рубеже A этот
провал зафиксирует; рубеж B обязан его закрыть.

| # | задача | решения спеки | рубеж |
|---|---|---|---|
| 1 | счётчик живых коробок и контракт однопоточности | Р11, В1 | |
| 2 | единица помнит своё хранилище | Р10, В2 | |
| 3 | литералы переезжают в `Ast` | Р6 | |
| 4 | собиратель строк переезжает в `Execution` | Р3 (часть) | |
| 5 | арена снимается; `Store` ужимается; `stringBytes` | Р1, Р3, Р7, Р8, Б1, Б2 | **A** |
| 6 | C API: значение по адресу, `ChupaError`, `bool` | §5, Р9, В3 | |
| 7 | обёртка Swift и оболочка под новый C API | §5 | |
| 8 | встроенные короткие строки | Р2 | **B** |
| 9 | хвостовое размещение агрегата | Р4 | **C** |
| 10 | циклы: видны в отладке, записаны в семантику | Р12 | |
| 11 | замер BDUI и сведение результатов | §6 | |

Задача 6 стоит до задачи 8 не по вкусу, а по необходимости: со встроенными
строками `chupa_value_string_borrowed(ChupaValue v, ...)` читает байты из копии
параметра, умирающей на возврате (дефект В3). Передача по адресу обязана
прийти раньше, чем байты переедут внутрь значения.

## Структура файлов

**Изменяемые, с новой ответственностью:**

| файл | было | стало |
|---|---|---|
| `core/src/value.hpp` | вид + регион + длина + объединение | тег + два макета, `Value` самодостаточно |
| `core/src/box.hpp/.cpp` | заголовок, три коробки, `boxOf`, `materialized` | то же минус `materialized`, плюс `stringBytes`, хвостовое размещение |
| `core/src/store.hpp/.cpp` | одиннадцать сущностей, две роли | четыре члена: имена, ячейки, значения, таблица ключей |
| `core/src/execution.hpp` | арена + список + ссылка | собиратель + список + ссылка |
| `core/src/context.hpp/.cpp` | владелец границы, отдаёт хранилище наружу | владелец границы, номер, ячейка последнего результата |
| `core/src/ast.hpp/.cpp` | узел со ссылкой на чужой литерал | владелец литералов |
| `core/src/c_api.cpp` | значение по копии, трёхзначный исход | значение по адресу, `bool` + `ChupaError` |
| `core/include/chupascript/chupascript.h` | 28 функций, `ChupaStatus` | 29 функций, `ChupaError` |

**Новые:**

| файл | ответственность |
|---|---|
| `tools/tsan.sh` | сборка и прогон тестов под детектором гонок |
| `core/tests/value_layout_test.cpp` | раскладка `Value`: тег, границы длины, инварианты |

**Исчезающие сущности** (файлов не удаляем ни одного): `Store::Role`,
`Store::clear`, `Store::clearSlow`, `Store::makeString`, `Store::string`,
`Store::internLiteral`, `Store::beginString`/`appendToString`/`endString`/
`abortString`, `Value::Region`, `Value::length_`, `Value::scratchString`,
`Execution::promote`, `Execution::scratch`, `detail::materialized`,
`ChupaStatus`, `chupa_context_error_code`/`_offset`/`_error`.

---
### Task 1: Счётчик живых коробок и контракт однопоточности

Закрывает В1 и Р11. Задача самостоятельная: раскладки значений не касается,
поэтому идёт первой и даёт остальным задачам исправный инструмент учёта.

**Files:**
- Modify: `core/src/box.hpp` (объявление `liveBoxCount`, довод про неатомарный
  счётчик коробки)
- Modify: `core/src/box.cpp:14` (`g_liveBoxes`)
- Modify: `core/include/chupascript/chupascript.h` (блок про потоки)
- Modify: `core/tests/box_test.cpp`, `core/tests/store_test.cpp`,
  `core/tests/context_test.cpp` (обращения к `liveBoxCount` под `#ifndef NDEBUG`)
- Create: `tools/tsan.sh`
- Test: `core/tests/box_test.cpp`

**Interfaces:**
- Consumes: ничего.
- Produces: `CS::detail::liveBoxCount()` — существует только когда `NDEBUG` не
  определён; в релизной сборке объявления нет вовсе. Все последующие задачи
  зовут её из тестов только внутри `#ifndef NDEBUG`.

- [ ] **Step 1: Написать падающий тест на два контекста в двух потоках**

В `core/tests/box_test.cpp`, после существующих тестов учёта:

```cpp
#ifndef NDEBUG
/// Two Contexts on two threads: the threading contract allows this, and the
/// live-box counter is the one piece of box state that is process-wide rather
/// than per-Context. Without atomicity the two increments race and the final
/// count comes out lower than the number of boxes actually created.
TEST(Box, LiveCountSurvivesTwoContextsOnTwoThreads) {
    constexpr int kPerThread = 2000;
    const std::size_t before = CS::detail::liveBoxCount();

    auto churn = [] {
        for (int i = 0; i < kPerThread; ++i) {
            StringBox *s = CS::detail::makeStringBox("payload");
            CS::detail::release(s);
        }
    };

    std::thread a(churn);
    std::thread b(churn);
    a.join();
    b.join();

    EXPECT_EQ(CS::detail::liveBoxCount(), before);
}
#endif
```

Добавить `#include <thread>` в список заголовков теста.

- [ ] **Step 2: Прогнать под TSan и убедиться, что он падает**

Сперва завести скрипт — иначе гонку нечем показать. `tools/tsan.sh`, по
образцу `tools/asan.sh` (тот же довод про компилятор из Homebrew: рантайм
санитайзеров из Xcode 17 на macOS 26 виснет до `main`):

```bash
#!/usr/bin/env bash
# Build and run the test suites under the thread sanitizer.
#
# The compiler is deliberately not the system one, for the same reason
# tools/asan.sh gives: the sanitizer runtime shipped with Xcode 17 deadlocks
# inside its own initialisation on macOS 26, before main is reached. The LLVM
# 22 runtime from Homebrew works.
#
# What this catches: the engine's threading contract (chupascript.h) allows two
# Contexts to run on two threads, and the live-box counter is shared by all of
# them.
set -euo pipefail

LLVM_PREFIX="${LLVM_PREFIX:-/opt/homebrew/opt/llvm}"
BUILD_DIR="${BUILD_DIR:-build-tsan}"

if [[ ! -x "${LLVM_PREFIX}/bin/clang++" ]]; then
    echo "no ${LLVM_PREFIX}/bin/clang++ — install with: brew install llvm" >&2
    exit 1
fi

cmake -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_C_COMPILER="${LLVM_PREFIX}/bin/clang" \
    -DCMAKE_CXX_COMPILER="${LLVM_PREFIX}/bin/clang++" \
    -DCMAKE_C_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" \
    -DCMAKE_CXX_FLAGS="-fsanitize=thread -fno-omit-frame-pointer -g" \
    -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=thread" > /dev/null

cmake --build "${BUILD_DIR}" -j8

export TSAN_OPTIONS="halt_on_error=1:${TSAN_OPTIONS:-}"

"./${BUILD_DIR}/core/tests/chupascript_tests" "$@"
```

```bash
chmod +x tools/tsan.sh
./tools/tsan.sh --gtest_filter='Box.LiveCountSurvivesTwoContextsOnTwoThreads'
```

Ожидается: `WARNING: ThreadSanitizer: data race` на `g_liveBoxes`, ненулевой
код возврата.

- [ ] **Step 3: Сделать счётчик атомарным и отладочным**

`core/src/box.cpp`, вместо строк 12–16:

```cpp
#ifndef NDEBUG
/// Number of live boxes in this process. Debug builds only: a leaked box is
/// invisible to every other metric, because box memory belongs to no Store and
/// Store::bytesUsed cannot see it.
///
/// Atomic because two Contexts may run on two threads (chupascript.h,
/// threading contract), and this counter is the one piece of box state that is
/// process-wide rather than per-Context. Relaxed ordering is enough: the count
/// is read after all evaluation has stopped, never to synchronise one thread's
/// writes with another's reads.
std::atomic<std::size_t> g_liveBoxes{0};

std::size_t liveBoxCount() noexcept {
    return g_liveBoxes.load(std::memory_order_relaxed);
}
#endif
```

Три места учёта (`makeStringBox`, `makeArrayBox`, `makeObjectBox`) и одно
списания (`release`) обернуть в макрос, чтобы релиз не платил ни инструкции:

```cpp
#ifndef NDEBUG
#  define CHUPA_COUNT_BOX_BORN() \
       g_liveBoxes.fetch_add(1, std::memory_order_relaxed)
#  define CHUPA_COUNT_BOX_DIED() \
       g_liveBoxes.fetch_sub(1, std::memory_order_relaxed)
#else
#  define CHUPA_COUNT_BOX_BORN() ((void)0)
#  define CHUPA_COUNT_BOX_DIED() ((void)0)
#endif
```

`#include <atomic>` в `box.cpp`.

`core/src/box.hpp`, вместо объявления на строке 155:

```cpp
#ifndef NDEBUG
/// Number of boxes alive in this process right now.
///
/// Debug builds only, and that is the whole point: this is a test metric, not
/// a runtime one. Release builds carry neither the counter nor the increments.
/// Every test that reads it must be guarded by #ifndef NDEBUG too.
[[nodiscard]] std::size_t liveBoxCount() noexcept;
#endif
```

- [ ] **Step 4: Записать довод про неатомарный счётчик коробки**

`core/src/box.hpp`, в докблоке `struct Box`, вместо фразы «Счётчик
интрузивный и **неатомарный** — контекст однопоточный»:

```cpp
/// The reference count is intrusive and NOT atomic. A Context is the unit of
/// single-threadedness (chupascript.h, threading contract): one Context is
/// touched by at most one thread at a time, and a box is only ever reached
/// through the Context that created it or through a host handle the host is
/// responsible for. Making the count atomic would put a locked
/// read-modify-write on every retain and release along the single-threaded
/// path — which is every path we have measured.
```

- [ ] **Step 5: Записать контракт в публичный заголовок**

`core/include/chupascript/chupascript.h`, сразу после `CHUPA_NONNULL_BEGIN`:

```c
/* ╔══════════════════════════════════════════════════════════════════════╗
 * ║ THREADING — a ChupaContext is the unit of single-threadedness.       ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * At most one thread may touch a given ChupaContext at a time, including
 * every value, expression and script that context produced. Two DIFFERENT
 * contexts may be used from two threads simultaneously; nothing inside the
 * engine is shared between them.
 *
 * Reference counts on values are not atomic, which is why the first rule is a
 * rule and not advice: two threads retaining the same value race, and the
 * value is freed while still in use. A host that hands a ChupaValue to
 * another thread must ensure the handoff is ordered and that only one thread
 * owns it at a time.
 */
```

- [ ] **Step 6: Обернуть существующие обращения к счётчику**

Во всех трёх файлах тестов (`box_test.cpp:51,58,60,72,82,84,88,90,92`,
`store_test.cpp:85,88,421,428,880,885,887`, `context_test.cpp:157,163,179,187`)
обернуть тесты, читающие `liveBoxCount`, в `#ifndef NDEBUG` / `#endif` целиком
— не отдельные строки: тест, у которого убрали все проверки, проверять
перестаёт, и молчащий зелёный тест хуже отсутствующего.

- [ ] **Step 7: Прогнать тесты — обычные и под TSan**

```bash
cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests
./tools/tsan.sh
```
Ожидается: обе команды зелёные, гонки нет.

- [ ] **Step 8: Проверить, что релизная сборка не знает счётчика**

```bash
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release > /dev/null && cmake --build build-rel -j8
nm -C build-rel/core/libchupascript.a 2>/dev/null | grep -c liveBoxCount
```
Ожидается: `0`.

- [ ] **Step 9: Commit**

```bash
git add core/src/box.hpp core/src/box.cpp core/include/chupascript/chupascript.h \
        core/tests/box_test.cpp core/tests/store_test.cpp core/tests/context_test.cpp \
        tools/tsan.sh
git commit -m "$(cat <<'MSG'
fix: счётчик живых коробок был общим на процесс, а контракт разрешает два потока

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 2: Единица помнит своё хранилище

Закрывает В2 и Р10. Спека называет носителем номера `Context`; план вешает
номер на `Store`, и это то же самое требование в более узком месте: у контекста
ровно одно хранилище, а компиляция (`Expression::compile`) уже получает
именно `Store &` и контекста не видит. Ставить номер на контекст значило бы
тащить контекст в сигнатуру компиляции ради поля, которое и так однозначно.

**Files:**
- Modify: `core/src/store.hpp` (член `id_`, метод `id()`), `core/src/store.cpp`
  (выдача номера)
- Modify: `core/src/expression.hpp/.cpp`, `core/src/script.hpp/.cpp` (член
  `storeId_`, проверка в `eval`/`run`)
- Test: `core/tests/expression_test.cpp`, `core/tests/script_test.cpp`

**Interfaces:**
- Consumes: ничего.
- Produces:
  - `std::uint32_t CS::Store::id() const noexcept`
  - `Expression::eval/evalNumber/evalBool/evalString` и `Script::run` при
    несовпадении номера возвращают отказ с
    `Diagnostic{ErrorCode::Usage, 0, "unit was compiled against another context"}`
    **в любой сборке**, а не только в отладочной.

- [ ] **Step 1: Написать падающий тест**

В `core/tests/expression_test.cpp`:

```cpp
/// A unit carries the identity of the Store it was compiled against, and
/// refuses to run on any other one. Without the check the slot number resolved
/// at compile time would index the other Store's values_ and return whichever
/// variable happens to sit there — silently, in release builds.
TEST(Expression, RefusesToEvaluateOnAnotherStore) {
    CS::Store home;
    CS::Store foreign;
    CS::Deferred dead;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(home, dead, "x", "1", diag));
    ASSERT_TRUE(CS::setVariable(foreign, dead, "y", "2", diag));

    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("x", home, &expr, diags, 1), 0u);

    CS::Execution elsewhere(foreign);
    CS::Value out = CS::Value::null();
    CS::Diagnostic failure;
    EXPECT_FALSE(expr.eval(elsewhere, &out, failure));
    EXPECT_EQ(failure.code, CS::ErrorCode::Usage);
}
```

Тот же тест для `Script::run` в `core/tests/script_test.cpp`. Цель
присваивания — поле, а не имя: `state = {...}` грамматика разбирает, но
компиляция отвергает (`docs/semantics.md` §7.2), поэтому телом скрипта берётся
`x.n = 1;` над объектом `{'n': 1}`, заведённым в `home`.

- [ ] **Step 2: Прогнать и убедиться, что он падает**

```bash
cmake --build build-dbg -j8 && \
  ./build-dbg/core/tests/chupascript_tests --gtest_filter='*RefusesToEvaluateOnAnother*'
```
Ожидается: падение по `assert(slot < globalValues_.size())` в
`Store::globalValueAt` либо `EXPECT_FALSE` не выполнилось — оба исхода
показывают, что проверки нет.

- [ ] **Step 3: Дать хранилищу номер**

`core/src/store.hpp`, в публичной части:

```cpp
    /// Identity of this Store, unique among all Stores created in this
    /// process.
    ///
    /// A compiled unit records the id of the Store it was compiled against and
    /// refuses to run on any other one (Expression::eval). Without it a unit
    /// evaluated on a foreign Context would index that Context's values_ with a
    /// slot number THIS Store handed out, and return whichever variable happens
    /// to sit there.
    ///
    /// A number rather than the Store's address: an address is reused the
    /// moment one Store is destroyed and the next is allocated in its place,
    /// and a unit outliving its Context is exactly the case this check exists
    /// for.
    [[nodiscard]] std::uint32_t id() const noexcept { return id_; }
```

и в приватной части, рядом с `keys_`:

```cpp
    const std::uint32_t id_;
```

`core/src/store.cpp`, рядом с `GlobalName`:

```cpp
namespace {

/// Source of Store ids. Atomic because two threads may each create a Context
/// (chupascript.h, threading contract), and the ids they get must differ.
/// Starts at 1 so that a zero-initialised id in a compiled unit never matches
/// a real Store.
std::atomic<std::uint32_t> g_nextStoreId{1};

}  // namespace
```

Конструктор:

```cpp
Store::Store(Role role)
    : keys_(role == Role::Globals ? KeyTable::create() : nullptr),
      id_(g_nextStoreId.fetch_add(1, std::memory_order_relaxed)) {}
```

Порядок инициализации обязан совпасть с порядком объявления членов — иначе
`-Werror` на `-Wreorder`. Объявить `id_` после `keys_`.

- [ ] **Step 4: Записать номер в единицу и проверять его**

`core/src/expression.hpp`, приватная часть:

```cpp
    /// Id of the Store this unit was compiled against; 0 until compile()
    /// succeeds. Every eval entry point compares it against the Store the
    /// Execution runs over and refuses a mismatch — see Store::id().
    std::uint32_t storeId_ = 0;
```

`core/src/expression.cpp`, в `compile` после успешной компиляции — `out->storeId_ = store.id();`
и общая проверка, зовущаяся из всех четырёх точек входа:

```cpp
namespace {

/// Rejects a unit that belongs to another Store. The check is unconditional,
/// not an assert: a release build is exactly where the wrong slot would be
/// read silently, and a silent wrong value on screen is the failure this
/// closes.
bool belongsHere(std::uint32_t unitStoreId, const Execution &exec,
                 Diagnostic &diag) {
    if (unitStoreId == exec.persistent().id()) { return true; }
    diag = Diagnostic{ErrorCode::Usage, 0,
                      "unit was compiled against another context"};
    return false;
}

}  // namespace
```

`Expression::eval` первой строкой: `if (!belongsHere(storeId_, exec, diag)) { return false; }`.
`evalNumber`, `evalBool`, `evalString` первой строкой:
`if (!belongsHere(storeId_, exec, diag)) { return EvalStatus::Error; }`.

То же в `core/src/script.hpp` / `script.cpp` для `Script::run`; функцию
`belongsHere` продублировать в анонимном пространстве `script.cpp` — общего
заголовка ради шести строк не заводим, и это осознанно: единственная
альтернатива — новый файл, который потом придётся объяснять.

- [ ] **Step 5: Прогнать тесты**

```bash
cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests
```
Ожидается: всё зелено, включая два новых теста.

- [ ] **Step 6: Проверить, что проверка есть и в релизе**

```bash
cmake --build build-rel -j8 && ./build-rel/core/tests/chupascript_tests \
  --gtest_filter='*RefusesToEvaluateOnAnother*'
```
Ожидается: зелено. Тест не читает `liveBoxCount`, поэтому под `NDEBUG` живёт
как есть.

- [ ] **Step 7: Обновить обещание в публичном заголовке**

`core/include/chupascript/chupascript.h`, в блоке про владение единицами,
вместо «Debug builds trap on it; release builds do not check»:

```c
 * A unit MUST be evaluated on the very context it was compiled against.
 * Every evaluation checks this — in release builds too — and a mismatch fails
 * with CHUPA_ERR_USAGE, touching no output. The check exists because
 * compilation resolves every global name to a slot in that context's store,
 * and another store's slots address other variables: without it the call would
 * return a neighbouring variable's value and look successful.
```

- [ ] **Step 8: Commit**

```bash
git add core/src/store.hpp core/src/store.cpp core/src/expression.hpp \
        core/src/expression.cpp core/src/script.hpp core/src/script.cpp \
        core/include/chupascript/chupascript.h \
        core/tests/expression_test.cpp core/tests/script_test.cpp
git commit -m "$(cat <<'MSG'
fix: единица адресовала ячейки чужого хранилища, и ловил это только assert

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 3: Литералы переезжают в `Ast`

Решение Р6. Коробки строковых литералов принадлежат дереву разбора, а не
хранилищу контекста: литерал — часть программы.

**Files:**
- Modify: `core/src/ast.hpp` (член `literals_`, метод `internLiteral`,
  деструктор, запрет копии), `core/src/ast.cpp`
- Modify: `core/src/compile.cpp:41` (`internStringLiterals`)
- Modify: `core/src/store.hpp`/`store.cpp` (снять `internLiteral` и `literals_`)
- Test: `core/tests/ast_test.cpp`, `core/tests/expression_test.cpp`

**Interfaces:**
- Consumes: `CS::detail::makeStringBox`, `CS::detail::release` (задача 1 их не
  трогала).
- Produces:
  - `detail::StringBox *CS::Ast::internLiteral(std::string_view bytes)` —
    создаёт коробку, оставляет её ссылку за деревом и отдаёт указатель.
  - `CS::Ast` становится move-only: копирование удалено, перемещение
    объявлено. `Expression` и `Script` наследуют это свойство.
  - `CS::Store::internLiteral` **исчезает**.

- [ ] **Step 1: Написать падающий тест**

В `core/tests/expression_test.cpp`:

```cpp
#ifndef NDEBUG
/// A string literal belongs to the Ast that parsed it, not to the Store the
/// unit was compiled against: the literal is part of the PROGRAM. The box
/// therefore outlives the Store and dies with the unit.
TEST(Expression, LiteralOutlivesTheStoreItWasCompiledAgainst) {
    const std::size_t before = CS::detail::liveBoxCount();

    CS::Expression expr;
    {
        CS::Store store;
        CS::Diagnostic diags[1];
        ASSERT_EQ(CS::Expression::compile("'literal'", store, &expr, diags, 1), 0u);
        EXPECT_EQ(CS::detail::liveBoxCount(), before + 1);
    }
    // The Store is gone; the literal is not.
    EXPECT_EQ(CS::detail::liveBoxCount(), before + 1);

    expr = CS::Expression{};
    EXPECT_EQ(CS::detail::liveBoxCount(), before);
}
#endif
```

- [ ] **Step 2: Прогнать и убедиться, что он падает**

```bash
cmake --build build-dbg -j8 && \
  ./build-dbg/core/tests/chupascript_tests --gtest_filter='*LiteralOutlives*'
```
Ожидается: вторая проверка не выполняется — счётчик вернулся к `before`,
потому что коробку освободил деструктор `Store`.

- [ ] **Step 3: Сделать дерево владельцем литералов**

`core/src/ast.hpp`, в публичной части рядом с `setStringLiteral`:

```cpp
    /// Lays the bytes of one string literal into a box owned by THIS Ast and
    /// returns it. Called once per String node, by compilation
    /// (core/src/compile.hpp).
    ///
    ///   Ast
    ///    ├── nodes_      vector<Node>          the tree itself
    ///    ├── children_   vector<NodeId>        child lists
    ///    └── literals_   vector<StringBox *>   one reference each, released
    ///                                          when this Ast is destroyed
    ///
    /// The box lives from this call until the Ast is destroyed. It is not a
    /// value the program creates but a constant the program contains, so it
    /// never enters the deferred-release list: the first operation boundary
    /// would take it away from the tree that still points at it.
    detail::StringBox *internLiteral(std::string_view bytes);
```

и, ниже объявления класса:

```cpp
    /// Declared because the Ast owns literal boxes: the implicit destructor
    /// would leak one reference per string literal. Defined in ast.cpp, where
    /// StringBox is a complete type.
    ~Ast();

    /// Move-only. Copying would give two Asts one reference each to the same
    /// literal boxes and free them twice; there is no use for a copy — a unit
    /// is compiled, evaluated and destroyed.
    Ast(const Ast &) = delete;
    Ast &operator=(const Ast &) = delete;
    Ast(Ast &&) noexcept;
    Ast &operator=(Ast &&) noexcept;
```

Конструктор по умолчанию объявить явно (`Ast() = default;`) — объявление
любого из перечисленных его отменяет.

`core/src/ast.cpp`:

```cpp
Ast::~Ast() {
    for (detail::StringBox *literal : literals_) { detail::release(literal); }
}

Ast::Ast(Ast &&) noexcept = default;
Ast &Ast::operator=(Ast &&other) noexcept {
    if (this == &other) { return *this; }
    // Release first: assignment overwrites literals_ wholesale, and the boxes
    // this Ast held would otherwise leak.
    for (detail::StringBox *literal : literals_) { detail::release(literal); }
    nodes_ = std::move(other.nodes_);
    children_ = std::move(other.children_);
    literals_ = std::move(other.literals_);
    sourceLength_ = other.sourceLength_;
    root_ = other.root_;
    checked_ = other.checked_;
    return *this;
}

detail::StringBox *Ast::internLiteral(std::string_view bytes) {
    detail::StringBox *box = detail::makeStringBox(bytes);
    literals_.push_back(box);
    return box;
}
```

Добавить `#include "box.hpp"` в `ast.cpp` и член
`std::vector<detail::StringBox *> literals_;` в приватную часть `ast.hpp`.

- [ ] **Step 4: Перенаправить укладку**

`core/src/compile.cpp`, в `internStringLiterals`: параметр `Store &store`
убрать, вызов заменить на

```cpp
        ast.setStringLiteral(node,
                             ast.internLiteral(literalText(ast, node, source, scratch)));
```

Оба вызова `internStringLiterals(ast, text, store)` → `internStringLiterals(ast, text)`.
Параметр `Store &store` у `compileExpression`/`compileScript` остаётся: его
читает `check` (разрешение имён в номера ячеек).

- [ ] **Step 5: Снять литералы с хранилища**

`core/src/store.hpp`: удалить объявление `internLiteral` вместе с докблоком и
член `literals_` вместе с секцией «оснастка: литералы единиц».
`core/src/store.cpp`: удалить определение `internLiteral` и строку
`for (detail::StringBox *literal : literals_) { detail::release(literal); }`
из деструктора.

Докблок класса `Store` (строки 22–43) поправить: слова «имена глобальных
переменных и коробки строковых литералов» теряют вторую половину.

- [ ] **Step 6: Прогнать тесты**

```bash
cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests
```
Ожидается: зелено. Если `compileIn` в `core/tests/context_test.cpp:15`
перестал компилироваться (`Expression` больше не копируется), заменить
возврат по значению на выходной параметр:

```cpp
/// Compiles an expression against the Context's store; requires success.
/// Takes an out-parameter rather than returning: an Expression owns its
/// literal boxes and is move-only.
void compileIn(CS::Context &ctx, std::string_view source, CS::Expression *out) {
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile(source, ctx.store(), out, diags, 1), 0u)
        << diags[0].message;
}
```

(`ASSERT_*` в функции с возвратом `void` законен, `EXPECT_*` был вынужденной
заменой.)

- [ ] **Step 7: Прогнать ASan**

```bash
./tools/asan.sh
```
Ожидается: зелено, утечек нет. Этот шаг здесь обязателен: задача переносит
владение, и двойное освобождение либо утечка — ровно то, что она может
принести.

- [ ] **Step 8: Commit**

```bash
git add core/src/ast.hpp core/src/ast.cpp core/src/compile.cpp core/src/compile.hpp \
        core/src/store.hpp core/src/store.cpp core/tests/expression_test.cpp \
        core/tests/context_test.cpp
git commit -m "$(cat <<'MSG'
refactor: литерал — часть программы, а владело им состояние контекста

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---
### Task 4: Собиратель строк переезжает в `Execution`, вычисленная строка становится коробкой

Первая половина Р3. После этой задачи **ни одно значение вида `Scratch` больше
не создаётся**: `format` и `str` отдают коробку. Дефект Б1 закрывается здесь —
продвигать становится нечего.

Арена (`Execution::scratch`, `Value::Region`, `promote`, `materialized`) на
этом шаге ещё стоит, но уже ничем не наполняется. Снимает её задача 5; порознь
потому, что первая половина меняет поведение и проверяется тестом, а вторая
только удаляет и проверяется тем, что всё осталось зелёным.

**Files:**
- Modify: `core/src/execution.hpp` (член `builder_` и четыре метода сборки)
- Modify: `core/src/store.hpp`/`store.cpp` (снять `makeString`, `beginString`,
  `appendToString`, `endString`, `abortString`, член `build_`)
- Modify: `core/src/eval.cpp:126-188` (`evalFormat`)
- Modify: `core/src/builtin.cpp:296-311` (`Builtin::Str`)
- Modify: `core/src/context.hpp` (`setGlobal`) — второй дефект того же семейства,
  что и Б1, найденный при исполнении: единственной ссылкой у `v` бывает ссылка
  создателя в списке отложенного освобождения, а `beginOperation()` сливает
  именно этот список — то есть освобождает коробку до того, как её удержит
  ячейка. Удержать надо **до** границы, а ссылку положить в свежий список.
- Modify: `core/tests/store_test.cpp` (тесты снятого API)
- Modify: `core/tests/operator_test.cpp:72,99` (два вызова `store.makeString`)
- Modify: `benchmarks/store_benchmark.cpp:115-127` (`BM_Store_MakeString`)
- Test: `core/tests/context_test.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `CS::materialize(std::string_view, Deferred &)` из
  `core/src/aggregate.hpp` — без изменений.
- Produces:
  - `std::uint32_t CS::Execution::beginString() noexcept`
  - `void CS::Execution::appendToString(std::string_view)`
  - `Value CS::Execution::endString(std::uint32_t mark)` — отдаёт **коробку**;
    её ссылка создателя уже лежит в списке отложенного освобождения.
  - `void CS::Execution::abortString(std::uint32_t mark) noexcept`
  - `CS::Store::makeString` и четыре метода сборки **исчезают**.

- [ ] **Step 1: Написать падающий тест — воспроизведение Б1**

В `core/tests/context_test.cpp`:

```cpp
/// A computed string handed straight back into a global variable keeps its
/// bytes. This is defect Б1 from the design document: setGlobal opened the
/// operation boundary, which cleared the arena, and only then promoted the
/// value out of that same arena — yielding an empty slice that no assert
/// caught, because the promoted box was a genuine box, merely an empty one.
TEST(Context, ComputedStringSurvivesBeingStoredInAGlobal) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("who", "'Вася'", diag));

    CS::Expression expr;
    compileIn(ctx, "format('привет, ${}', who)", &expr);

    CS::Value computed = CS::Value::null();
    ASSERT_TRUE(ctx.eval(expr, &computed, diag)) << diag.message;
    ASSERT_EQ(computed.kind(), CS::Value::Kind::String);

    ctx.setGlobal("saved", computed);
    EXPECT_EQ(ctx.string(ctx.store().global("saved")), "привет, Вася");
}

/// And it stays readable across any number of later operations: a boxed string
/// is owned by the global's slot, not by the operation that produced it.
TEST(Context, StoredComputedStringSurvivesLaterOperations) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("who", "'Вася'", diag));

    CS::Expression build;
    compileIn(ctx, "format('привет, ${}', who)", &build);
    CS::Value computed = CS::Value::null();
    ASSERT_TRUE(ctx.eval(build, &computed, diag)) << diag.message;
    ctx.setGlobal("saved", computed);

    CS::Expression noise;
    compileIn(ctx, "format('${} ${}', 1, 2)", &noise);
    for (int i = 0; i < 8; ++i) {
        CS::Value ignored = CS::Value::null();
        ASSERT_TRUE(ctx.eval(noise, &ignored, diag)) << diag.message;
    }

    EXPECT_EQ(ctx.string(ctx.store().global("saved")), "привет, Вася");
}
```

- [ ] **Step 2: Прогнать и убедиться, что первый падает**

```bash
cmake --build build-dbg -j8 && \
  ./build-dbg/core/tests/chupascript_tests --gtest_filter='Context.*ComputedString*'
```
Ожидается: `ComputedStringSurvivesBeingStoredInAGlobal` падает — сравнение
получает пустую строку вместо `привет, Вася`.

- [ ] **Step 3: Завести собиратель в `Execution`**

`core/src/execution.hpp`, в публичной части:

```cpp
    // ─── string builder ───
    //
    //   Execution::builder_    a plain std::string, NOT a region
    //   ┌──────────────────────────────────────────┐
    //   │ ...outer format's pieces... │ inner's... │
    //   └──────────────────────────▲──────────────▲┘
    //                        outer mark      inner mark
    //
    // format (docs/semantics.md §8.8) needs a growing buffer because the
    // length of its result is not known before the last piece is appended.
    //
    // The buffer is NOT a memory region: no Value ever points into it, it
    // lives from beginString to the matching endString, and clearing it is a
    // detail of one function rather than a rule of the memory model. What it
    // keeps from the arena it replaces is the capacity: endString truncates
    // the buffer back to the mark and leaves the allocation, so a steady
    // stream of format calls stops reaching the allocator.
    //
    // A mark is a POSITION, not a pointer: appending may move the buffer's
    // bytes, and a pointer taken before the move would address freed memory.
    // Nested format works for the same reason — the inner call's mark sits
    // above the outer call's, and endString lifts only the tail above it.

    /// Starts building a string. The returned mark goes to endString or
    /// abortString; every path out of a build must pass through one of them.
    [[nodiscard]] std::uint32_t beginString() noexcept {
        assert(builder_.size() <= 0xffffffffu && "string builder outgrew uint32");
        return static_cast<std::uint32_t>(builder_.size());
    }

    void appendToString(std::string_view bytes) { builder_.append(bytes); }

    /// Finishes the build: copies everything appended above the mark into a
    /// box and truncates the buffer back to the mark.
    ///
    /// The result is a box rather than an offset into a region, and that is
    /// the whole change: a box is self-contained, so the caller may store it
    /// in a global variable, put it into an aggregate or hand it to the host
    /// without asking anyone's permission first. The creator's reference goes
    /// to this Execution's deferred list, so a result nobody keeps dies at the
    /// next operation boundary instead of leaking.
    [[nodiscard]] Value endString(std::uint32_t mark) {
        const Value result =
            CS::materialize(std::string_view(builder_).substr(mark), deferred_);
        builder_.resize(mark);
        return result;
    }

    /// Abandons the build, truncating the buffer back to the mark.
    void abortString(std::uint32_t mark) noexcept { builder_.resize(mark); }
```

и в приватной части, перед `deferred_`:

```cpp
    /// Scratch buffer for format. Declared before deferred_ only for
    /// readability; it owns nothing that ordering could affect.
    std::string builder_;
```

Добавить `#include <string>` и `#include <string_view>`.

- [ ] **Step 4: Перевести `format` на собиратель выполнения**

`core/src/eval.cpp`, в `evalFormat`: удалить строки
`Store &result = exec.scratch;` и `const Store &from = exec.scratch;`,
заменить `result.` на `exec.`, `from.string(tmpl)` на `exec.string(tmpl)`.

Докблок функции (строки 100–124) переписать по-английски: три абзаца про
переселение пула теряют предмет — шаблон теперь коробка и не переезжает
никогда.

```cpp
/// Evaluates format (docs/semantics.md §8.8).
///
/// The template is read where it lies and the result is assembled in
/// Execution's builder; the two are different buffers, and that is what makes
/// evaluating an argument in the middle of a build safe. Both the template and
/// every piece produced by an argument are boxes now, so nothing an argument
/// does can move the bytes this loop is reading — the concern that made the
/// old code re-slice the template on every iteration is gone with the arena.
///
/// The template is parsed by nextFormatPiece (builtin.hpp) — the same function
/// the static pass uses on a literal template (check.cpp), so the rule
/// "$${} is a literal, ${} is a placeholder" is written once and keeps both
/// readers in step by construction rather than by agreement.
```

- [ ] **Step 5: Перевести `str` на коробку**

`core/src/builtin.cpp`, ветка `Builtin::Str`, вместо
`*out = exec.scratch.makeString(text);`:

```cpp
            // materialize, not an arena offset: the result of str is an
            // ordinary value and its caller may do anything with it —
            // including putting it into an aggregate or a global variable.
            *out = CS::materialize(text, exec.deferred());
```

- [ ] **Step 6: Снять сборку строк с хранилища**

`core/src/store.hpp`: удалить объявления `makeString`, `beginString`,
`appendToString`, `endString`, `abortString` вместе с длинной секцией
комментариев «сборка строки по частям» (строки 96–133), а также член
`std::string build_;`.
`core/src/store.cpp`: удалить одноимённые определения; из `clearSlow` убрать
`build_.clear();`; из `bytesReserved` убрать `build_.capacity() +`.

- [ ] **Step 7: Убрать тесты снятого API**

`core/tests/store_test.cpp`: удалить тесты, зовущие `makeString`,
`beginString`, `appendToString`, `endString`, `abortString`. Найти их:

```bash
grep -n "makeString\|beginString\|appendToString\|endString\|abortString" core/tests/store_test.cpp
```

Тесты сборки строки не пропадают, а переезжают: их предмет теперь у
`Execution`. Перенести по одному в новый блок `core/tests/context_test.cpp`,
переписав через `CS::Execution` и проверяя, что результат — коробка:

```cpp
/// The builder hands back a box, and a box is readable without asking any
/// Store: that is what lets a format result be stored, pushed and returned.
TEST(Execution, BuildsAStringIntoABox) {
    CS::Store store;
    CS::Execution exec(store);

    const std::uint32_t mark = exec.beginString();
    exec.appendToString("при");
    exec.appendToString("вет");
    const CS::Value built = exec.endString(mark);

    EXPECT_EQ(built.kind(), CS::Value::Kind::String);
    EXPECT_EQ(built.region(), CS::Value::Region::Boxed);
    EXPECT_EQ(exec.string(built), "привет");
}

/// A nested build finishes before the outer one continues, because the inner
/// mark sits above the outer mark in the same buffer.
TEST(Execution, NestedBuildTakesOnlyItsOwnTail) {
    CS::Store store;
    CS::Execution exec(store);

    const std::uint32_t outer = exec.beginString();
    exec.appendToString("a");
    const std::uint32_t inner = exec.beginString();
    exec.appendToString("bc");
    const CS::Value innerResult = exec.endString(inner);
    exec.appendToString("d");
    const CS::Value outerResult = exec.endString(outer);

    EXPECT_EQ(exec.string(innerResult), "bc");
    EXPECT_EQ(exec.string(outerResult), "ad");
}

/// An abandoned build leaves nothing behind for the next one to pick up.
TEST(Execution, AbortedBuildLeavesNoTail) {
    CS::Store store;
    CS::Execution exec(store);

    const std::uint32_t first = exec.beginString();
    exec.appendToString("discarded");
    exec.abortString(first);

    const std::uint32_t second = exec.beginString();
    exec.appendToString("kept");
    EXPECT_EQ(exec.string(exec.endString(second)), "kept");
}
```

- [ ] **Step 8: Перевести оставшихся вызывающих `makeString`**

`core/tests/operator_test.cpp:72,99` — `store.makeString("a")` →
`CS::materialize("a", dead)`; список отложенного освобождения в этих тестах
уже есть, иначе завести локальный `CS::Deferred dead;`.

`benchmarks/store_benchmark.cpp` — замер `BM_Store_MakeString` мерил укладку
байт в арену, которой больше нет. Переименовать в `BM_Value_Materialize` и
мерить то, что её заменило:

```cpp
/// Creating a string: bytes copied into a reference-counted box.
void BM_Value_Materialize(benchmark::State &state) {
    const std::string text(32, 'x');
    for (auto _ : state) {
        CS::Deferred dead;
        for (int i = 0; i < 100; ++i) {
            Value made = CS::materialize(text, dead);
            benchmark::DoNotOptimize(made);
        }
    }
    state.SetItemsProcessed(state.iterations() * 100);
}
BENCHMARK(BM_Value_Materialize);
```

Имя меняется намеренно: старое обещало замер метода `Store`, а хранилище
строк больше не делает. Следствие назвать вслух — `tools/bench-compare.py`
на рубеже A вернёт код 1, потому что `BM_Store_MakeString` пропал из прогона
(это его штатное поведение при исчезновении замера из базы). Считать это
ожидаемым и записать в отчёт рубежа; база перезаписывается только в задаче 11.

- [ ] **Step 9: Прогнать тесты**

```bash
cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests
```
Ожидается: зелено, включая оба теста шага 1.

- [ ] **Step 10: Прогнать ASan**

```bash
./tools/asan.sh
```
Ожидается: зелено. Задача создаёт коробку там, где раньше был бамп; утечка
ссылки создателя — самый вероятный её промах.

- [ ] **Step 11: Commit**

```bash
git add core/src/execution.hpp core/src/store.hpp core/src/store.cpp \
        core/src/eval.cpp core/src/builtin.cpp benchmarks/store_benchmark.cpp \
        core/tests/store_test.cpp core/tests/context_test.cpp \
        core/tests/operator_test.cpp
git commit -m "$(cat <<'MSG'
fix: вычисленная строка была смещением в арену, которую граница операции стирала до её чтения

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 5: Арена снимается, `Store` ужимается, строка читает себя сама

Вторая половина Р3 плюс Р1, Р7, Р8 и дефект Б2. Задача почти целиком —
удаление: после задачи 4 ничего из снимаемого не работает.

**Рубеж замеров A.**

**Files:**
- Modify: `core/src/value.hpp` (снять `Region`, `length_`, `scratchString`,
  `index`, `length`, `friend class Store`)
- Modify: `core/src/box.hpp` (снять `materialized`, упростить `boxOf`, завести
  `stringBytes`)
- Modify: `core/src/store.hpp`/`store.cpp` (снять `Role`, `clear`, `clearSlow`,
  `string`; переименовать члены)
- Modify: `core/src/execution.hpp` (снять `scratch`, `promote`; `persistent()`
  → `store()`)
- Modify: `core/src/context.hpp`/`context.cpp` (граница операции, `store()`
  только константный, `compileExpression`/`compileScript`)
- Modify: `core/src/aggregate.hpp` (снять четыре `assert(materialized(...))`)
- Modify: `core/src/operator.hpp`/`operator.cpp` (снять оба параметра-хранилища)
- Modify: `core/src/builtin.hpp`/`builtin.cpp` (`coerceScalarToString` без
  хранилища), `core/src/eval.cpp` (`coerceToString`, вызовы `applyBinary`,
  `promote`), `core/src/expression.cpp`, `core/src/data.cpp`
- Modify: `core/src/c_api.cpp` (две компиляции через контекст)
- Modify: `cli/main.cpp`, `cli/printer.cpp`, `cli/tests/printer_test.cpp`
- Modify: `benchmarks/eval_benchmark.cpp` (обращения к `ctx.store()`,
  счётчики `temp_bytes_per_iter`)
- Test: `core/tests/value_test.cpp`, `core/tests/store_test.cpp`,
  `core/tests/operator_test.cpp`, `core/tests/context_test.cpp`,
  `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: всё, что произвела задача 4.
- Produces:
  - `std::string_view CS::stringBytes(const Value &v) noexcept` — единственный способ
    прочитать байты строки. Предусловие: `v.kind() == Value::Kind::String`.
  - `Store &CS::Execution::store() noexcept` (переименование `persistent()`).
  - `const Store &CS::Context::store() const noexcept` — **только
    константный**.
  - `std::uint32_t CS::Context::compileExpression(std::string_view source, Expression *out, Diagnostic *diags, std::uint32_t capacity)`
    и одноимённый `compileScript` — единственный способ снаружи получить
    единицу для этого контекста.
  - `bool CS::applyBinary(TokenKind op, Value lhs, Value rhs, std::uint32_t offset, Value *out, Diagnostic &diag)`
  - `bool CS::coerceScalarToString(Value v, char *numberBuffer, std::string_view *out, std::uint32_t offset, Diagnostic &diag)`
  - `Value::Region`, `Execution::promote`, `detail::materialized`,
    `Store::Role`, `Store::clear`, `Store::string`, `Context::store()`
    изменяемый — **исчезают**.

- [ ] **Step 1: Написать тест, закрывающий Б2**

В `core/tests/context_test.cpp`:

```cpp
/// The Context owns the operation boundary, so nothing outside it may write
/// into the store: a write that skipped the boundary would leave the newborn
/// box's creator reference in a list nobody drains. The type says so — the
/// store is handed out const, and the only ways in are the Context's own
/// operations.
TEST(Context, HandsOutItsStoreForReadingOnly) {
    static_assert(
        std::is_const_v<std::remove_reference_t<decltype(std::declval<CS::Context &>().store())>>,
        "Context::store() must not hand out a mutable Store");
}
```

Плюс тест на новую дверь компиляции:

```cpp
TEST(Context, CompilesAgainstItsOwnStore) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("x", "41", diag));

    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(ctx.compileExpression("x + 1", &expr, diags, 1), 0u) << diags[0].message;

    double out = 0.0;
    ASSERT_EQ(ctx.evalNumber(expr, &out, diag), CS::EvalStatus::Ok);
    EXPECT_EQ(out, 42.0);
}
```

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

```bash
cmake --build build-dbg -j8 2>&1 | tail -20
```
Ожидается: ошибка компиляции — `static_assert` не выполнен и
`compileExpression` у `Context` нет.

- [ ] **Step 3: Завести `stringBytes`**

`core/src/box.hpp`, после закрытия `namespace detail`:

```cpp
/// The bytes of a string value.
///
///   stringBytes(v) ──> the StringBox v points at ──> its tail bytes
///
/// One function, and it asks nobody: a string value carries everything needed
/// to read it. Five callers used to answer this question in five different
/// ways — Store::string, Execution::string, Context::string,
/// coerceScalarToString and a hand-written cast in c_api.cpp — because a value
/// used to be an offset into one particular Store's pool, and every caller had
/// to know WHICH Store. There is no such question left to ask.
///
/// The slice stays valid as long as the box does, which is as long as someone
/// holds a reference to it. Not NUL-terminated.
///
/// Takes the value BY REFERENCE, not by copy, and will keep doing so: task 8
/// moves short strings' bytes inside the value itself, and a by-copy parameter
/// would hand back a pointer into a parameter that dies on return.
///
/// Precondition: v.kind() == Value::Kind::String.
[[nodiscard]] inline std::string_view stringBytes(const Value &v) noexcept {
    assert(v.kind() == Value::Kind::String);
    return static_cast<const detail::StringBox *>(v.box())->view();
}
```

- [ ] **Step 4: Убрать регион из `Value`**

`core/src/value.hpp`:
- удалить `enum class Region`, метод `region()`, член `region_`, член
  `length_`, приватные `scratchString`, `index`, `length`, `friend class Store`
  и член объединения `std::uint32_t index_`;
- `Value::string(detail::StringBox *box, std::uint32_t length)` теряет второй
  параметр — длина живёт в коробке;
- `addressesStore()` переименовать в `referencesBox()`: слово «хранилище» в
  вопросе больше не участвует;
- докблок класса переписать по-английски.

```cpp
/// A ChupaScript value: sixteen bytes, trivially copyable, self-contained.
///
///   Value
///    ├── kind_    Kind     Null | Boolean | Number | String | Object | Array
///    └── union
///         ├── boolean_   bool
///         ├── number_    double
///         └── box_       detail::Box *   String, Array, Object
///
/// Self-contained is the one rule of the memory model: a value can be read
/// anywhere, at any time, including after the Context that produced it has
/// been destroyed. An aggregate and a long string are addressed by a pointer
/// to a reference-counted box; a box carries its own bytes and, for an object,
/// its own field-name table.
///
/// There used to be a second representation — an offset into the operation's
/// arena — and with it three more lifetime rules to hold in mind at once.
/// The arena is gone (design document Р3), and so are they.
```

Обновить `TODO(B2)`: NaN-boxing закрыт насовсем (спека Р2), комментарий
удалить целиком, а не переписать.

- [ ] **Step 5: Упростить `boxOf` и снять `materialized`**

`core/src/box.hpp`:

```cpp
/// The box a value owns a reference to, or nullptr when it owns none.
///
/// One predicate for the whole engine. Four callers ask "does this value hold
/// a reference" — retain, release, the deferred list and the host boundary —
/// and each of them used to spell the condition out its own way.
[[nodiscard]] inline Box *boxOf(Value v) noexcept {
    return v.referencesBox() ? v.box() : nullptr;
}
```

`detail::materialized` удалить вместе с докблоком.

- [ ] **Step 6: Ужать `Store`**

`core/src/store.hpp` — новый докблок класса (образец из Global Constraints
годится дословно) и четыре члена:

```cpp
    std::vector<char> names_;               // global-variable name bytes
    std::vector<detail::GlobalName> slots_; // name -> slot, sorted by bytes
    std::vector<Value> values_;             // slot -> value; a ROOT
    KeyTable *keys_;                        // field names, shared with objects
    const std::uint32_t id_;                // Task 2
```

Удалить: `enum class Role`, параметр конструктора, `clear`, `clearSlow`,
`string`, `bytesUsed`/`bytesReserved` пересчитать на новые члены.
`appendText` → `appendName`, `textAt` → `nameAt`. `globalNames_` → `slots_`,
`globalValues_` → `values_`.

Обработку алиаса в `appendName` **сохранить** и объяснить одним предложением,
назвав единственный способ её вызвать:

```cpp
    // The bytes may point into names_ itself: setGlobal(store.globalNameAt(i),
    // v) is the one way to arrive here with such a slice. Growing the pool
    // moves the buffer, so the source is remembered as an offset rather than
    // an address — an address would dangle halfway through the copy.
```

Конструктор: `Store::Store() : keys_(KeyTable::create()),
  id_(g_nextStoreId.fetch_add(1, std::memory_order_relaxed)) {}`.

- [ ] **Step 7: Ужать `Execution`**

`core/src/execution.hpp`: удалить член `Store scratch`, метод `promote`,
метод `string` (его заменил `stringBytes`); `persistent()` переименовать в
`store()`. Новый докблок:

```cpp
/// The state of one evaluation: the string builder it assembles format results
/// in, the deferred-release list it drops evicted references into, and the
/// Store it reads globals from.
///
///   Execution
///    ├── builder_    std::string     format's scratch buffer, not a region
///    ├── deferred_   Deferred        references to drop at the next boundary
///    └── store_      Store &         the Context's globals and key table
///
/// Separate from Context for one reason, and it is a strong one: THE EVALUATOR
/// MUST NOT BE ABLE TO OPEN AN OPERATION BOUNDARY. Context::beginOperation is
/// private, and eval only ever sees an Execution, where drain() is out of
/// reach. A boundary opened in the middle of an expression would release the
/// intermediate values that expression is still holding.
///
/// Neither copyable nor movable: the reference ties this instance to one Store
/// for its whole life. An Execution is destroyed with the Context that owns it.
```

- [ ] **Step 8: Переписать `Context`**

`core/src/context.hpp`:

```cpp
    /// The operation boundary: every reference the previous operation deferred
    /// is dropped here.
    ///
    /// At the START of an operation, not at the end: the caller reads the
    /// result right after the call returns, and dropping references on the way
    /// out would take it away exactly when it is wanted. The rule the host sees
    /// is unchanged — a value is good until the next call on this context, and
    /// a host that needs it longer takes a reference.
    ///
    /// Once per operation, not per statement or loop iteration: `acc = acc +
    /// str(x)` inside a `for` holds a value that must outlive the iteration.
    void beginOperation() noexcept { exec_.deferred().drain(); }
```

`setGlobal` теряет `promote`:

```cpp
    /// Writes a global variable on behalf of the host.
    ///
    /// An operation, not a bare write: the boundary drains the previous
    /// operation's deferred references, and a host that only writes and never
    /// evaluates — the ordinary shape of a screen's first frame — would
    /// otherwise grow that list until the Context dies.
    void setGlobal(std::string_view name, Value v) {
        beginOperation();
        store_.setGlobal(name, v, exec_.deferred());
    }
```

`store()` — только константный. Добавить две двери компиляции:

```cpp
    /// Compiles an expression against this Context's store.
    ///
    /// The only way in from outside: compilation resolves global names to slot
    /// numbers in this store, so the unit it produces belongs to this Context
    /// and refuses to run anywhere else (Store::id).
    std::uint32_t compileExpression(std::string_view source, Expression *out,
                                    Diagnostic *diags, std::uint32_t capacity) {
        return Expression::compile(source, store_, out, diags, capacity);
    }

    std::uint32_t compileScript(std::string_view source, Script *out,
                                Diagnostic *diags, std::uint32_t capacity) {
        return Script::compile(source, store_, out, diags, capacity);
    }
```

`Context::string` удалить: снаружи её звал только `cli/printer.cpp`, и там
теперь `CS::stringBytes`. `temporaryBytesUsed` удалить — временного региона
нет; выяснить и снять её вызовы:

```bash
grep -rn "temporaryBytesUsed" core cli benchmarks Sources
```

- [ ] **Step 9: Снять хранилища с остальных сигнатур**

- `core/src/operator.hpp`/`.cpp`: `applyBinary` теряет `lhsStore`/`rhsStore`;
  `operator.cpp:111` → `stringBytes(lhs) == stringBytes(rhs)`. Докблок про два
  региона удалить целиком — предмет исчез.
- `core/src/builtin.hpp`/`.cpp`: `coerceScalarToString` теряет `const Store &`;
  строка 182 → `*out = stringBytes(v);`; строка 207 (`const Store &first`) и
  все `first.`/`exec.scratch` удалить.
- `core/src/eval.cpp`: `coerceToString` теряет параметр-хранилище; четыре
  вызова `applyBinary` (строки 423, 538, 584, 624) теряют по два аргумента;
  пять вызовов `exec.promote(...)` (строки 339, 360, 548, 599, 611) —
  заменить значением как есть; `exec.persistent()` (строка 279) →
  `exec.store()`.
- `core/src/expression.cpp:71`: `*out = exec.string(value);` → сперва связать
  результат именованной ссылкой, потом `*out = stringBytes(value);` — со
  встроенными строками (задача 8) срез будет указывать внутрь `value`.
- `core/src/expression.cpp` и `core/src/script.cpp`: `belongsHere` из задачи 2
  зовёт `exec.persistent()` — переименовать на `exec.store()`.
- `core/src/aggregate.hpp`: удалить четыре `assert(detail::materialized(v) && ...)`.
- `core/src/data.cpp`: `buildValue` теряет параметр `Store &store` везде, кроме
  `store.keys()` в ветке `NodeKind::Object` — этот вызов остаётся.

- [ ] **Step 10: Обновить всех, кто брал хранилище изменяемым**

Константный `store()` ломает восемь мест за пределами ядра. Каждое — это не
починка сборки, а перевод на ту дверь, которая у контекста и предназначена
для этого действия.

- `core/src/c_api.cpp:229,244`: `CS::Expression::compile(..., c->impl.store(), ...)`
  → `c->impl.compileExpression(std::string_view(source, len), &e->impl, &diag, 1)`;
  то же для скрипта.
- `cli/main.cpp:69,93`: `CS::Expression::compile(source, ctx.store(), ...)` →
  `ctx.compileExpression(source, &expr, found, kMaxReported)`; то же для скрипта.
- `cli/main.cpp:166`: `const CS::Store &store = ctx.store();` — остаётся, чтение.
- `cli/main.cpp:203`: `runSet(ctx.store(), argument)` → перевести `runSet` на
  `CS::Context &` и `ctx.setVariableText(name, text, diag)`.
- `cli/printer.cpp:73`: `ctx.string(value)` → сперва связать значение
  именованной ссылкой, затем `CS::stringBytes(value)`.
- `cli/tests/printer_test.cpp` — десять мест вида `Store &store = ctx.store();`
  и вспомогательная `put(store, name, text)`. Перевести помощника на контекст:

```cpp
/// Seeds a global from literal text and hands its value back. Goes through the
/// Context because a write to the store is an operation, with a boundary.
Value put(CS::Context &ctx, std::string_view name, std::string_view text) {
    CS::Diagnostic diag;
    EXPECT_TRUE(ctx.setVariableText(name, text, diag)) << diag.message;
    return ctx.store().global(name);
}
```

  и снять локальные `Store &store` из всех тестов, где они держались только
  ради `put`.
- `benchmarks/eval_benchmark.cpp:53,218,223,229,335,765,771`:
  `CS::setVariable(ctx.store(), dead, name, text, diag)` →
  `ctx.setVariableText(name, text, diag)`;
  `CS::Expression::compile(source, ctx.store(), ...)` → `ctx.compileExpression(...)`;
  `fill(ctx.store())` → перевести `fill` на `CS::Context &`.
  Замеры, работающие с самостоятельным `Store store;` (а не через контекст),
  не трогать — там хранилище своё и изменяемое законно.
- `benchmarks/eval_benchmark.cpp:787,822`: счётчики `temp_bytes_per_iter`
  удалить вместе с подсчётом итераций, который их кормит. Мерить нечего:
  временного региона не существует. Замер `BM_Eval_ContextAggregate` и
  `BM_Eval_RawAggregate` остаются — исчезает только счётчик, а не имя, поэтому
  сравнение с базой не ломается.

- [ ] **Step 11: Обновить тесты**

`core/tests/value_test.cpp` — снять проверки региона и длины, оставив
`sizeof`, тривиальную копируемость и виды.
`core/tests/store_test.cpp` — снять тесты `clear`, `Role::Arena` и
`Store::string`; тест на алиас имени переписать через `globalNameAt`:

```cpp
/// A name slice taken out of the store and fed straight back in must survive
/// the pool growing under it — the one way to reach appendName with a source
/// that points into names_.
TEST(Store, AcceptsItsOwnNameSliceBack) {
    CS::Store store;
    CS::Deferred dead;
    store.setGlobal("width", CS::Value::number(320), dead);

    const std::string_view borrowed = store.globalNameAt(0);
    store.setGlobal(borrowed, CS::Value::number(640), dead);

    EXPECT_EQ(store.globalCount(), 1u);
    EXPECT_EQ(store.global("width").numberValue(), 640.0);
}
```

`core/tests/operator_test.cpp` — снять оба аргумента-хранилища из вызовов
`applyBinary`.
`core/tests/context_test.cpp` — три теста собирателя, заведённых задачей 4,
опираются на снятые здесь `Execution::string`, `Context::string` и
`Value::region()`:

```cpp
    EXPECT_EQ(built.kind(), CS::Value::Kind::String);
    EXPECT_EQ(CS::stringBytes(built), "привет");
```

Проверку региона удалить, а не заменить: региона больше нет, и заменять её
нечем — самодостаточность значения проверяет то, что байты читаются вовсе без
хранилища. В тестах Б1 из задачи 4 `ctx.string(...)` заменить на
`CS::stringBytes(...)`, связав значение глобальной переменной именованной
ссылкой.

- [ ] **Step 12: Прогнать всё**

```bash
cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests && \
  ./build-dbg/cli/tests/chupa_cli_tests
./tools/asan.sh
```
Ожидается: зелено везде.

- [ ] **Step 13: Рубеж A — замер**

```bash
cmake --build build-rel -j8
./build-rel/benchmarks/chupascript_benchmarks \
  --benchmark_repetitions=9 --benchmark_report_aggregates_only=true \
  --benchmark_format=json > /tmp/bench-checkpoint-a.json
python3 tools/bench-compare.py benchmarks/baseline.json /tmp/bench-checkpoint-a.json
```

Записать результат в `docs/benchmarks/2026-08-19-memory-model-checkpoint-a.md`
с прямо названным ожиданием:

| замер | ожидание на рубеже A |
|---|---|
| `BM_Eval_Constant` | без изменений |
| `BM_Eval_StringLiteral` | без изменений — литерал уложен на компиляции |
| `BM_Eval_FormatNumber` | **хуже**: результат стал аллокацией, встроенных строк ещё нет |
| `BM_Eval_ArrayLiteral` / `ObjectLiteral` | без изменений |

Провал `FormatNumber` здесь — ожидаемая цена промежуточного состояния, а не
регрессия: закрывает его задача 8, и её рубеж B обязан вернуть замер как
минимум к базовому уровню. Если `FormatNumber` **не** ухудшился — значит
`format` где-то не дошёл до коробки; разобраться до перехода дальше.

- [ ] **Step 14: Commit**

```bash
git add -A core cli docs/benchmarks
git commit -m "$(cat <<'MSG'
refactor: арена операции отвечала на вопрос «в каком хранилище лежит значение», которого больше не задают

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---
### Task 6: C API — значение по адресу, двузначный исход, одна структура ошибки

Спека §5 целиком плюс Р9. Стоит **до** встроенных строк: с ними
`chupa_value_string_borrowed(ChupaValue v, ...)` читает байты из копии
параметра, умирающей на возврате (дефект В3).

**Files:**
- Modify: `core/include/chupascript/chupascript.h` (поверхность целиком)
- Modify: `core/src/c_api.cpp`
- Modify: `core/src/context.hpp` (ячейка последнего результата, Р9)
- Modify: `benchmarks/host_benchmark.cpp` — единственный потребитель C API за
  пределами ядра и обёртки; зовёт `chupa_context_set`, `chupa_context_error`
  старой формы, `chupa_value_string_borrowed` и четыре функции значения по
  копии. Без него `build-rel` не собирается, а план обязан оставлять дерево
  собранным после каждого коммита.
- Modify: `core/src/context.cpp`/`context.hpp` — снять `evalValue` и
  `evalString`: их звали только `chupa_eval_value` и
  `chupa_eval_string_borrowed`, а обе функции эта задача удаляет.
- Test: `core/tests/c_api_test.cpp`, `core/tests/smoke_test.cpp`

**Interfaces:**
- Consumes: `CS::stringBytes`, `CS::Context::compileExpression/compileScript`
  из задачи 5.
- Produces:
  - `const CS::Value &CS::Context::keepResult(Value v)` — кладёт значение в
    корень контекста до ближайшей границы и отдаёт ссылку на **хранимую**
    копию.
  - Публичный C API в составе спеки §5.3.
  - `ChupaStatus`, `chupa_context_error_code`, `chupa_context_error_offset`,
    старый `chupa_context_error`, `chupa_eval_value`,
    `chupa_eval_string_borrowed`, `chupa_value_string_borrowed`,
    `chupa_context_set` — **исчезают**.

- [ ] **Step 1: Написать падающий тест — В3 и новая поверхность**

В `core/tests/c_api_test.cpp`:

```cpp
/// Bytes borrowed from a value stay readable after the function that produced
/// them has returned. Defect В3: while the value was passed by copy, the bytes
/// of a short string would live inside that copy — a parameter that dies on
/// return — and the caller would read a dead stack frame.
TEST(CApi, StringBytesOutliveTheCallThatProducedThem) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_TRUE(chupa_context_set_data(ctx, "s", 1, "'short'", 7));

    ChupaExpression* e = chupa_compile_expression(ctx, "s", 1);
    ASSERT_NE(e, nullptr);

    ChupaValue v;
    ASSERT_TRUE(chupa_eval(ctx, e, &v));
    ASSERT_EQ(chupa_value_kind(&v), CHUPA_KIND_STRING);

    const char* bytes = nullptr;
    size_t len = 0;
    chupa_value_string(&v, &bytes, &len);
    EXPECT_EQ(std::string(bytes, len), "short");

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

/// Null is a kind, not an outcome: chupa_eval succeeds and reports it.
TEST(CApi, EvalReportsNullAsAKind) {
    ChupaContext* ctx = chupa_context_create();
    ChupaExpression* e = chupa_compile_expression(ctx, "null", 4);
    ASSERT_NE(e, nullptr);

    ChupaValue v;
    EXPECT_TRUE(chupa_eval(ctx, e, &v));
    EXPECT_EQ(chupa_value_kind(&v), CHUPA_KIND_NULL);

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

/// The shortcuts keep the three-way answer, but in the error rather than in
/// the return: CHUPA_ERR_NONE means the expression evaluated to null,
/// CHUPA_ERR_TYPE means it produced another kind.
TEST(CApi, NumberShortcutTellsNullFromWrongKind) {
    ChupaContext* ctx = chupa_context_create();

    ChupaExpression* nul = chupa_compile_expression(ctx, "null", 4);
    ChupaExpression* text = chupa_compile_expression(ctx, "'x'", 3);
    ASSERT_NE(nul, nullptr);
    ASSERT_NE(text, nullptr);

    double out = 1.0;
    ChupaError err;

    EXPECT_FALSE(chupa_eval_number(ctx, nul, &out));
    chupa_context_error(ctx, &err);
    EXPECT_EQ(err.code, CHUPA_ERR_NONE);
    EXPECT_EQ(out, 1.0);

    EXPECT_FALSE(chupa_eval_number(ctx, text, &out));
    chupa_context_error(ctx, &err);
    EXPECT_EQ(err.code, CHUPA_ERR_TYPE);

    chupa_expression_destroy(nul);
    chupa_expression_destroy(text);
    chupa_context_destroy(ctx);
}

/// A unit evaluated on a foreign context fails with CHUPA_ERR_USAGE instead of
/// reading a neighbouring variable's slot (defect В2, task 2, seen from C).
TEST(CApi, RefusesAUnitFromAnotherContext) {
    ChupaContext* home = chupa_context_create();
    ChupaContext* other = chupa_context_create();
    ASSERT_TRUE(chupa_context_set_number(home, "x", 1, 42.0));

    ChupaExpression* e = chupa_compile_expression(home, "x", 1);
    ASSERT_NE(e, nullptr);

    double out = 0.0;
    EXPECT_FALSE(chupa_eval_number(other, e, &out));
    ChupaError err;
    chupa_context_error(other, &err);
    EXPECT_EQ(err.code, CHUPA_ERR_USAGE);

    chupa_expression_destroy(e);
    chupa_context_destroy(other);
    chupa_context_destroy(home);
}
```

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

```bash
cmake --build build-dbg -j8 2>&1 | tail -20
```
Ожидается: `chupa_context_set_data`, `chupa_eval`, `chupa_value_string`,
`ChupaError` не объявлены.

- [ ] **Step 3: Переписать публичный заголовок**

`core/include/chupascript/chupascript.h`. Три правила ставятся в шапку, сразу
после блока про потоки из задачи 1:

```c
/* ╔══════════════════════════════════════════════════════════════════════╗
 * ║ OWNERSHIP — three rules, and this header holds nothing else.         ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 *
 * 1. A VALUE IS BORROWED UNTIL YOU RETAIN IT.
 *    chupa_eval yields a value that stays alive until the next call on that
 *    context. To keep it longer, call chupa_value_retain, and
 *    chupa_value_release when done.
 *
 * 2. BYTES ARE BORROWED FROM THE VALUE WHOSE ADDRESS YOU PASSED.
 *    A string's bytes and an object's key bytes live exactly as long as YOUR
 *    ChupaValue variable does — the one whose address went into
 *    chupa_value_string. They are NOT NUL-terminated; use the length.
 *
 * 3. A NESTED VALUE IS BORROWED FROM ITS PARENT.
 *    chupa_array_at and its neighbours take no reference of their own. A
 *    nested value that must outlive the aggregate holding it needs an explicit
 *    chupa_value_retain.
 *
 * Every function over a value takes it BY ADDRESS — const ChupaValue * — with
 * no exceptions. A short string's bytes live inside the value itself, so a
 * by-copy parameter would hand back a pointer into a copy that dies when the
 * function returns. One way of passing removes that mistake from the set of
 * expressible ones rather than from the set of documented ones.
 */
```

Заменить `ChupaStatus` на структуру ошибки:

```c
/* One call, one struct. Three separate accessors used to answer one question
 * in three round trips, and a caller who read the code but not the offset got
 * a half-answer. */
typedef struct ChupaError {
    ChupaErrorCode code;         /* CHUPA_ERR_NONE when the last call succeeded */
    size_t         offset;       /* byte offset into the compiled source        */
    const char    *message;      /* borrowed; valid until the next call on ctx  */
    size_t         message_len;
} ChupaError;

CHUPA_API void chupa_context_error(const ChupaContext *ctx, ChupaError *out);
```

Остальная поверхность — дословно из спеки §5.3. Удалить `enum ChupaStatus`,
`chupa_context_error_code`, `chupa_context_error_offset`, старый
`chupa_context_error`, `chupa_eval_value`, `chupa_eval_string_borrowed`,
`chupa_value_string_borrowed`; переименовать `chupa_context_set` в
`chupa_context_set_data` (он принимает **текст литерала**, а не значение — имя
`set` об этом молчало).

Абзацы, потерявшие предмет, удалить целиком, не переписывая: про переезд пула
(пула нет), про освобождение временного региона (региона нет), про
материализацию строки ради честности `retain` (материализовывать нечего),
объяснение суффикса `_borrowed` (заимствование стало общим правилом и живёт в
шапке).

- [ ] **Step 4: Завести ячейку последнего результата**

`core/src/context.hpp`, приватный член и публичный метод:

```cpp
    /// The value the last string shortcut borrowed its bytes from.
    ///
    /// A ROOT: it holds one reference, taken in keepResult and dropped at the
    /// next operation boundary. It exists for chupa_eval_string, which hands
    /// the host a pointer to the result's bytes: for a short string those
    /// bytes live inside the value itself, so a local variable inside
    /// c_api.cpp would die on return and take them with it. The header's
    /// promise — "the bytes stay valid until the next call that touches this
    /// context" — is met here literally, and now for one reason instead of
    /// three.
    Value lastResult_ = Value::null();
```

```cpp
    /// Keeps a value alive until the next operation boundary and returns a
    /// reference to the STORED copy — the one whose address the caller may
    /// borrow bytes out of.
    ///
    /// Returns a reference, not a value: bytes borrowed from a returned copy
    /// would die with that copy at the end of the caller's full expression.
    const Value &keepResult(Value v) {
        assert(lastResult_.kind() == Value::Kind::Null &&
               "the boundary must have cleared the previous result");
        detail::retainValue(v);
        lastResult_ = v;
        return lastResult_;
    }
```

`beginOperation` дополняется:

```cpp
    void beginOperation() noexcept {
        exec_.deferred().drain();
        detail::releaseValue(lastResult_);
        lastResult_ = Value::null();
    }
```

Деструктор `Context` объявить и отпустить в нём `lastResult_`: без этого
удержанное значение утекает вместе с контекстом.

- [ ] **Step 5: Переписать `c_api.cpp`**

Ключевые места:

```cpp
void chupa_context_error(const ChupaContext* ctx, ChupaError* out) {
    if (!ctx || !out) { return; }
    const auto* c = reinterpret_cast<const ::ChupaContext*>(ctx);
    const char* message = c->lastError.message ? c->lastError.message : "";
    out->code = toCode(c->lastError.code);
    out->offset = c->lastError.offset;
    out->message = message;
    out->message_len = std::strlen(message);
}

bool chupa_eval(ChupaContext* ctx, ChupaExpression* e, ChupaValue* out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();

    CS::Value value = CS::Value::null();
    if (!c->impl.eval(expr->impl, &value, c->lastError)) { return false; }
    // Null is a kind, not an outcome: the value says so itself, and a separate
    // return code for it existed only because a double * had nowhere to put
    // "it came out null".
    *out = toC(value);
    return true;
}

bool chupa_eval_string(ChupaContext* ctx, ChupaExpression* e,
                       const char** bytes, size_t* len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();

    CS::Value value = CS::Value::null();
    if (!c->impl.eval(expr->impl, &value, c->lastError)) { return false; }
    if (value.kind() == CS::Value::Kind::Null) { return false; }  // error stays NONE
    if (value.kind() != CS::Value::Kind::String) {
        c->setError({CS::ErrorCode::Type, 0, "expression is not a string"});
        return false;
    }

    // Borrowed from the Context's own slot, not from a local: a short string's
    // bytes live inside the value, and a local would die on return.
    const CS::Value& kept = c->impl.keepResult(value);
    const std::string_view text = CS::stringBytes(kept);
    *bytes = text.data();
    *len = text.size();
    return true;
}

void chupa_value_string(const ChupaValue* v, const char** bytes, size_t* len) {
    const std::string_view text = CS::stringBytes(fromC(v));
    *bytes = text.data();
    *len = text.size();
}
```

`fromC` переписать под адрес — и это важнее, чем выглядит:

```cpp
/// Reinterprets the host's sixteen bytes as a CS::Value IN PLACE.
///
/// A reference, not a copy: a short string's bytes live inside the value, and
/// every slice handed back to the host points into the host's own variable.
const CS::Value& fromC(const ChupaValue* v) {
    return *reinterpret_cast<const CS::Value*>(v);
}
```

Компиляция идёт через контекст: `c->impl.compileExpression(...)` вместо
`CS::Expression::compile(..., c->impl.store(), ...)`.

Все функции обхода (`chupa_array_count`, `chupa_array_at`,
`chupa_object_count`, `chupa_object_key_at`, `chupa_object_value_at`,
`chupa_object_get`, `chupa_value_kind`, `chupa_value_bool`,
`chupa_value_number`, `chupa_value_retain`, `chupa_value_release`) принимают
`const ChupaValue *`; те, что отдавали `ChupaValue`, отдают его через
`ChupaValue *out`.

Ярлыки `chupa_eval_number`/`_bool` — тем же образом, что `chupa_eval_string`:
`false` на null с `CHUPA_ERR_NONE`, `false` на другом виде с `CHUPA_ERR_TYPE`.
Функция `toStatus` исчезает. Перевод `CS::ErrorCode` → `ChupaErrorCode` живёт
сейчас телом снятой `chupa_context_error_code`; вынести его в анонимное
пространство как `ChupaErrorCode toCode(CS::ErrorCode)` вместе с восемью
`static_assert`, которые стерегут расхождение перечислений.

- [ ] **Step 6: Прогнать тесты**

```bash
cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests
```
Ожидается: зелено. Существующие тесты `c_api_test.cpp` придётся править
массово — механически: `CHUPA_OK` → `true`, `chupa_eval_value` → `chupa_eval`,
значение по адресу. Проверить, что ни один тест не удалён молча:

```bash
git diff --stat core/tests/c_api_test.cpp
grep -c '^TEST' core/tests/c_api_test.cpp
```
Число тестов не должно уменьшиться иначе как на те, что проверяли снятые
понятия (`CHUPA_NULL` как исход) — каждое такое удаление назвать в сообщении
коммита.

- [ ] **Step 7: Прогнать ASan — он и есть проверка В3**

```bash
./tools/asan.sh --gtest_filter='CApi.*'
```
Ожидается: зелено. Тест `StringBytesOutliveTheCallThatProducedThem` сегодня
проходит и без исправления (байты в коробке); его настоящая проверка наступает
в задаче 8, и ASan там поймает разницу. Записать это в теле теста комментарием,
чтобы он не выглядел бессмысленным до задачи 8.

- [ ] **Step 8: Commit**

```bash
git add core/include/chupascript/chupascript.h core/src/c_api.cpp \
        core/src/context.hpp core/tests/c_api_test.cpp core/tests/smoke_test.cpp
git commit -m "$(cat <<'MSG'
refactor: граница с хостом передавала значение копией, а байты собирается держать внутри него

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 7: Обёртка Swift под новый C API

Отдельная задача, потому что у неё своя сборка и свой прогон: SwiftPM, не
CMake. Ядро после задачи 6 зелено, обёртка — нет.

**Files:**
- Modify: `Sources/ChupaScript/Context.swift` (`chupa_context_set` →
  `_set_data`, три вызова ошибки → один)
- Modify: `Sources/ChupaScript/CSValue.swift` (`chupa_eval_*`, исход `bool`)
- Modify: `Sources/ChupaScript/Error.swift` (`ChupaError`)
- Modify: `Tests/ChupaScriptTests/*.swift` по факту падений
- Test: `Tests/ChupaScriptTests/PublicAPITests.swift`,
  `Tests/ChupaScriptTests/EndToEndTests.swift`

**Interfaces:**
- Consumes: поверхность C API из задачи 6.
- Produces: публичный Swift API без изменений сигнатур — если какая-то
  сигнатура всё же меняется, назвать её в сообщении коммита поимённо.

- [ ] **Step 1: Прогнать тесты Swift и собрать список падений**

```bash
swift build 2>&1 | tail -40
```
Ожидается: ошибки на `chupa_context_set`, `chupa_eval_value`,
`chupa_eval_string_borrowed`, `ChupaStatus`, `chupa_context_error_code`.
Список записать — он и есть план правок.

- [ ] **Step 2: Перевести чтение ошибки на структуру**

`Sources/ChupaScript/Error.swift` — вместо трёх вызовов:

```swift
/// Reads the context's last error in one crossing of the C boundary.
/// The message is borrowed from the context and copied here immediately: it
/// stays valid only until the next call on that context.
static func read(from handle: OpaquePointer) -> ChupaScriptError? {
    var raw = ChupaError()
    chupa_context_error(handle, &raw)
    guard raw.code != CHUPA_ERR_NONE else { return nil }
    let message = raw.message.map { String(validUTF8Bytes: $0, count: raw.message_len) } ?? ""
    return ChupaScriptError(code: ErrorCode(raw.code), offset: raw.offset, message: message)
}
```

(Точное имя инициализатора строки взять из `Sources/ChupaScript/UTF8.swift` —
обёртка не перепроверяет UTF-8 намеренно.)

- [ ] **Step 3: Перевести вычисления на двузначный исход**

`Sources/ChupaScript/CSValue.swift`: три места вида

```swift
switch chupa_eval_number(handle, expr, &out) {
case CHUPA_OK: return out
case CHUPA_NULL: return nil
default: throw ...
}
```

становятся

```swift
// false means "no number came out"; the error says which — a null result
// leaves the error at CHUPA_ERR_NONE, a wrong kind sets CHUPA_ERR_TYPE, and
// anything else is a real failure.
guard chupa_eval_number(handle, expr, &out) else {
    if let error = ChupaScriptError.read(from: handle) { throw error }
    return nil
}
return out
```

- [ ] **Step 4: Переименовать сеттер данных**

`Sources/ChupaScript/Context.swift`: `chupa_context_set` →
`chupa_context_set_data`. Публичное имя метода обёртки менять только если оно
повторяло `set` буквально; в этом случае переименовать в `setData` и назвать
это в сообщении коммита как ломающее изменение обёртки.

- [ ] **Step 5: Прогнать тесты Swift**

```bash
swift test 2>&1 | tail -30
```
Ожидается: зелено.

- [ ] **Step 6: Проверить, что подмодули не разошлись**

```bash
grep -rn "chupa_" Sources Tests | grep -o 'chupa_[a-z_]*' | sort -u
```
Ожидается: ни одного имени, которого нет в
`core/include/chupascript/chupascript.h`. Сверить:

```bash
grep -o 'chupa_[a-z_]*' core/include/chupascript/chupascript.h | sort -u
```

- [ ] **Step 7: Commit**

```bash
git add Sources Tests
git commit -m "$(cat <<'MSG'
refactor: обёртка разбирала трёхзначный исход, которого у границы больше нет

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---
### Task 8: Встроенные короткие строки

Решение Р2 — то, ради чего вся работа. Строка до пятнадцати байт лежит в самом
`Value`: ни коробки, ни счётчика, ни аллокации.

**Рубеж замеров B.**

**Files:**
- Modify: `core/src/value.hpp` (раскладка целиком)
- Modify: `core/src/box.hpp` (`stringBytes`, `boxOf`)
- Modify: `core/src/aggregate.hpp` (`materialize` выбирает представление)
- Modify: `core/tests/box_test.cpp` (сигнатура `Value::string`)
- Create: `core/tests/value_layout_test.cpp`
- Modify: `core/tests/CMakeLists.txt`
- Test: `core/tests/value_layout_test.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `CS::stringBytes` из задачи 5, граница по адресу из задачи 6.
- Produces:
  - `static constexpr std::size_t CS::Value::kInlineCapacity = 15;`
  - `static Value CS::Value::inlineString(std::string_view bytes) noexcept`
  - `bool CS::Value::isInlineString() const noexcept`
  - `std::string_view CS::Value::inlineBytes() const noexcept`
  - `std::string_view stringBytes(Value &&) = delete;` — временное значение
    больше не годится в источник байт.
  - `CS::materialize` выбирает представление по длине; коробку создаёт только
    для строк длиннее `kInlineCapacity`.

- [ ] **Step 1: Написать падающий тест на раскладку**

Новый файл `core/tests/value_layout_test.cpp`:

```cpp
#include "value.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <string>

#include "box.hpp"

namespace {

using CS::Value;

TEST(ValueLayout, StaysSixteenBytesAndTriviallyCopyable) {
    static_assert(sizeof(Value) == 16, "Value must stay sixteen bytes");
    static_assert(std::is_trivially_copyable_v<Value>, "");
    static_assert(alignof(Value) == 8, "the double in the payload sets this");
}

TEST(ValueLayout, HoldsFifteenBytesInline) {
    const std::string bytes(Value::kInlineCapacity, 'x');
    const Value v = Value::inlineString(bytes);
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_TRUE(v.isInlineString());
    EXPECT_EQ(CS::stringBytes(v), bytes);
}

TEST(ValueLayout, EmptyStringIsInline) {
    const Value v = Value::inlineString("");
    EXPECT_TRUE(v.isInlineString());
    EXPECT_TRUE(CS::stringBytes(v).empty());
}

TEST(ValueLayout, InlineStringKeepsEmbeddedNul) {
    const std::string bytes("a\0b", 3);
    const Value v = Value::inlineString(bytes);
    EXPECT_EQ(CS::stringBytes(v).size(), 3u);
    EXPECT_EQ(CS::stringBytes(v)[1], '\0');
}

/// The bytes past the length are zero. Not decoration: it is what makes
/// comparing two inline strings a comparison of the sixteen bytes, without
/// consulting the length and without memcmp over a variable range.
TEST(ValueLayout, PadsWithZeroesSoEqualStringsHaveEqualBytes) {
    // Built from two different, longer sources, so anything the factory failed
    // to zero would differ between them.
    const Value a = Value::inlineString(std::string_view("ab_leftover_one", 2));
    const Value b = Value::inlineString(std::string_view("ab?????????????", 2));
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(Value)), 0);
    EXPECT_EQ(CS::stringBytes(a), "ab");
}

/// Sixteen bytes is one too many: the box path starts here.
TEST(ValueLayout, SixteenBytesGoesToABox) {
    CS::Deferred dead;
    const std::string bytes(Value::kInlineCapacity + 1, 'x');
    const Value v = CS::materialize(bytes, dead);
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_FALSE(v.isInlineString());
    EXPECT_EQ(CS::stringBytes(v), bytes);
}

/// A short string costs no box at all — the point of the whole change.
TEST(ValueLayout, ShortStringAllocatesNothing) {
#ifndef NDEBUG
    CS::Deferred dead;
    const std::size_t before = CS::detail::liveBoxCount();
    const Value v = CS::materialize("center", dead);
    EXPECT_EQ(CS::detail::liveBoxCount(), before);
    EXPECT_EQ(CS::stringBytes(v), "center");
#endif
}

}  // namespace
```

Дописать `value_layout_test.cpp` в `core/tests/CMakeLists.txt`.

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

```bash
cmake --build build-dbg -j8 2>&1 | tail -20
```
Ожидается: у `Value` нет ни `kInlineCapacity`, ни `inlineString`, ни
`isInlineString`.

- [ ] **Step 3: Переписать раскладку `Value`**

`core/src/value.hpp`:

```cpp
/// A ChupaScript value: sixteen bytes, trivially copyable, self-contained.
///
///         0        1                                              15
///       ┌────┬────────────────────────────────────────────────────┐
/// Inline│ tag│ b0 b1 b2 b3 b4 b5 b6 b7 b8 b9 b10 b11 b12 b13 b14  │  string <= 15
///       └────┴────────────────────────────────────────────────────┘
///       ┌────┬───────────────────┬───────────────────────────────┐
/// Number│ tag│      padding      │            double             │
///       └────┴───────────────────┴───────────────────────────────┘
///       ┌────┬───────────────────┬───────────────────────────────┐
/// Boxed │ tag│      padding      │           Box *               │  long string,
///       └────┴───────────────────┴───────────────────────────────┘  array, object
///       ┌────┬─┐
/// Bool  │ tag│b│                                    Null: the tag alone
///       └────┴─┘
///
/// The tag byte:
///
///    bit  7   6 5 4 3   2 1 0
///         │   └───┬───┘ └─┬─┘
///         │       │       └── kind: Null 0, Boolean 1, Number 2,
///         │       │            String 3, Object 4, Array 5
///         │       └────────── inline string length, 0..15; meaningful only
///         │                    when the kind is String
///         └────────────────── string is inline (1) or boxed (0); meaningful
///                              only when the kind is String
///
/// Self-contained is the one rule of the memory model: a value can be read
/// anywhere, at any time, including after the Context that produced it has
/// been destroyed. A short string carries its bytes; a long string and an
/// aggregate carry a pointer to a reference-counted box, and the box carries
/// its bytes and — for an object — its own field-name table.
///
/// INVARIANT: in an inline string, the bytes past the length are zero. That is
/// what makes comparing two inline strings a comparison of two eight-byte
/// words, with no length to consult and no memcmp over a variable range.
///
/// NaN-boxing was rejected permanently (design document Р2): eight bytes are
/// spent entirely on the double and the tags, leaving no room for string
/// bytes, and the BDUI measurements name strings as the only place the engine
/// loses.
class Value {
   public:
    enum class Kind : std::uint8_t {
        Null = 0, Boolean = 1, Number = 2, String = 3, Object = 4, Array = 5
    };

    /// The longest string that fits inside a value: sixteen bytes minus the
    /// tag.
    static constexpr std::size_t kInlineCapacity = 15;

    [[nodiscard]] static Value null() noexcept { return Value{}; }

    [[nodiscard]] static Value boolean(bool value) noexcept {
        Value v;
        v.wide_.tag = tagOf(Kind::Boolean);
        v.wide_.flag = value;
        return v;
    }

    [[nodiscard]] static Value number(double value) noexcept {
        Value v;
        v.wide_.tag = tagOf(Kind::Number);
        v.wide_.number = value;
        return v;
    }

    /// A string short enough to live inside the value.
    /// Precondition: bytes.size() <= kInlineCapacity.
    [[nodiscard]] static Value inlineString(std::string_view bytes) noexcept {
        assert(bytes.size() <= kInlineCapacity);
        // The default constructor value-initialises wide_, which zeroes all
        // sixteen bytes including the padding — so every byte past the length
        // is already zero, which is the invariant the equality fast path
        // stands on.
        Value v;
        v.inline_.tag = static_cast<std::uint8_t>(
            tagOf(Kind::String) |
            (static_cast<std::uint8_t>(bytes.size()) << kLengthShift) |
            kInlineFlag);
        if (!bytes.empty()) {
            std::memcpy(v.inline_.bytes, bytes.data(), bytes.size());
        }
        return v;
    }

    /// A string too long to live inside the value. The box carries its own
    /// length; the tag's length field stays zero and is never read for a boxed
    /// string.
    [[nodiscard]] static Value string(detail::StringBox *box) noexcept {
        Value v;
        v.wide_.tag = tagOf(Kind::String);  // inline flag left clear
        v.wide_.box = reinterpret_cast<detail::Box *>(box);
        return v;
    }

    [[nodiscard]] static Value array(detail::ArrayBox *box) noexcept {
        Value v;
        v.wide_.tag = tagOf(Kind::Array);
        v.wide_.box = reinterpret_cast<detail::Box *>(box);
        return v;
    }

    [[nodiscard]] static Value object(detail::ObjectBox *box) noexcept {
        Value v;
        v.wide_.tag = tagOf(Kind::Object);
        v.wide_.box = reinterpret_cast<detail::Box *>(box);
        return v;
    }

    /// The box this value references.
    /// Precondition: referencesBox().
    [[nodiscard]] detail::Box *box() const noexcept {
        assert(referencesBox());
        return wide_.box;
    }

    /// Precondition: kind() == Kind::Boolean.
    [[nodiscard]] bool booleanValue() const noexcept {
        assert(kind() == Kind::Boolean);
        return wide_.flag;
    }

    /// Precondition: kind() == Kind::Number.
    [[nodiscard]] double numberValue() const noexcept {
        assert(kind() == Kind::Number);
        return wide_.number;
    }

    /// Are these two the same aggregate — same kind, same box.
    ///
    /// Scalars have no identity (docs/semantics.md §5.4), so this is false for
    /// them even when a value is compared with itself.
    [[nodiscard]] bool sameAggregate(const Value &other) const noexcept {
        const Kind k = kind();
        if (k != other.kind()) { return false; }
        if (k != Kind::Array && k != Kind::Object) { return false; }
        return wide_.box == other.wide_.box;
    }

    [[nodiscard]] Kind kind() const noexcept {
        return static_cast<Kind>(wide_.tag & kKindMask);
    }

    /// Precondition: kind() == Kind::String.
    [[nodiscard]] bool isInlineString() const noexcept {
        assert(kind() == Kind::String);
        return (wide_.tag & kInlineFlag) != 0;
    }

    /// The bytes of an inline string. Points INTO this value, so it lives
    /// exactly as long as this value does — read it through stringBytes,
    /// which says so at every call site.
    /// Precondition: kind() == Kind::String && isInlineString().
    [[nodiscard]] std::string_view inlineBytes() const noexcept {
        assert(kind() == Kind::String && isInlineString());
        return std::string_view(inline_.bytes, inlineLength());
    }

    /// Does this value own a reference to a box.
    [[nodiscard]] bool referencesBox() const noexcept {
        const Kind k = kind();
        if (k == Kind::Object || k == Kind::Array) { return true; }
        return k == Kind::String && (wide_.tag & kInlineFlag) == 0;
    }

   private:
    static constexpr std::uint8_t kKindMask = 0x07;    // bits 0-2
    static constexpr std::uint8_t kLengthShift = 3;    // bits 3-6
    static constexpr std::uint8_t kLengthMask = 0x78;
    static constexpr std::uint8_t kInlineFlag = 0x80;  // bit 7

    static constexpr std::uint8_t tagOf(Kind kind) noexcept {
        return static_cast<std::uint8_t>(kind);
    }

    [[nodiscard]] std::size_t inlineLength() const noexcept {
        return static_cast<std::size_t>((wide_.tag & kLengthMask) >> kLengthShift);
    }

    /// Both layouts are standard-layout and share the tag as their common
    /// initial sequence, so the tag may be read through either member whichever
    /// one is active ([class.mem]). Nothing else may.
    struct Inline { std::uint8_t tag; char bytes[15]; };
    struct Wide {
        std::uint8_t tag;
        std::uint8_t pad[7];
        union { double number; bool flag; detail::Box *box; };
    };

    Value() noexcept : wide_{} {}  // tag 0 == Kind::Null

    union { Inline inline_; Wide wide_; };
};

static_assert(sizeof(Value) == 16, "Value must stay sixteen bytes");
static_assert(alignof(Value) == 8, "the double in the payload sets the alignment");
static_assert(std::is_trivially_copyable_v<Value>,
              "values are copied in bulk inside aggregates");
```

Добавить `#include <cstring>` и `#include <string_view>`.

- [ ] **Step 4: Научить `stringBytes` встроенной строке и запретить временное**

`core/src/box.hpp`:

```cpp
[[nodiscard]] inline std::string_view stringBytes(const Value &v) noexcept {
    assert(v.kind() == Value::Kind::String);
    if (v.isInlineString()) { return v.inlineBytes(); }
    return static_cast<const detail::StringBox *>(v.box())->view();
}

/// A temporary is not a valid source of bytes: an inline string's bytes live
/// inside the value, and the value would die at the end of the full expression
/// while the slice was still being read. Bind it to a named const Value &.
std::string_view stringBytes(Value &&) = delete;
```

Запрет обязательно повлечёт ошибки компиляции на местах вида
`stringBytes(arrayAt(a, i))`. Это и есть ревизия, ради которой он ставится:
каждое такое место переписать через именованную ссылку

```cpp
const Value &element = arrayAt(a, i);
const std::string_view text = stringBytes(element);
```

и убедиться, что срез не переживает `element`.

- [ ] **Step 5: Научить `materialize` выбирать представление**

`core/src/aggregate.hpp`:

```cpp
/// Turns bytes into a string value.
///
///   <= Value::kInlineCapacity  ->  the bytes go inside the value; no box,
///                                  no reference count, no allocation
///   longer                     ->  a box, whose creator reference goes to
///                                  dead and is dropped at the next boundary
///
/// dead is untouched on the inline path, and that is the point: the hot BDUI
/// cases — a colour, a key, an identifier, str() over a number — stop
/// allocating entirely.
[[nodiscard]] inline Value materialize(std::string_view bytes, Deferred &dead) {
    if (bytes.size() <= Value::kInlineCapacity) {
        return Value::inlineString(bytes);
    }
    detail::StringBox *box = detail::makeStringBox(bytes);
    dead.take(box);
    return Value::string(box);
}
```

- [ ] **Step 6: Починить прочие вызывающие**

```bash
cmake --build build-dbg -j8 2>&1 | grep -E "error" | head -40
```
Ожидаемые места — те, где `stringBytes` получает временное значение (шаг 4), и
те, где читался снятый `Value::region()`. Каждое переписать через именованную
ссылку; ни одного не «починить» приведением типа.

Литералы остаются коробками намеренно, включая короткие (спека Р6): узел
дерева — двадцать четыре байта, под литерал в нём отведено восемь, а
встроенное `Value` требует шестнадцати. Проверить, что `Ast::internLiteral` и
чтение узла литерала в `core/src/eval.cpp` по-прежнему идут через
`Value::string(box)` и во встроенное представление не сваливаются.

- [ ] **Step 7: Прогнать всё**

```bash
cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests && \
  ./build-dbg/cli/tests/chupa_cli_tests
./tools/asan.sh
swift test
```
Ожидается: зелено везде. ASan здесь — настоящая проверка В3: тест
`CApi.StringBytesOutliveTheCallThatProducedThem` теперь ходит по встроенной
строке, и передача по копии дала бы чтение мёртвого кадра.

- [ ] **Step 8: Рубеж B — замер**

```bash
cmake --build build-rel -j8
./build-rel/benchmarks/chupascript_benchmarks \
  --benchmark_repetitions=9 --benchmark_report_aggregates_only=true \
  --benchmark_format=json > /tmp/bench-checkpoint-b.json
python3 tools/bench-compare.py benchmarks/baseline.json /tmp/bench-checkpoint-b.json
python3 tools/bench-compare.py /tmp/bench-checkpoint-a.json /tmp/bench-checkpoint-b.json
```

**Сравнивать надо против двух точек, и они отвечают на разные вопросы.**
Выяснилось на рубеже A: `benchmarks/baseline.json` был снят на чужой машине, и
`tools/bench-compare.py` отказывался сравнивать вовсе (код 2), из-за чего рубеж
не сказал ничего. База переснята на этой машине в точке ветвления `f8f0273`
(коммит `e49f512`). Но между `f8f0273` и началом этой работы лежат 57 коммитов
конверсии на счётчик ссылок, поэтому:

- **против `benchmarks/baseline.json`** — что изменилось с эпохи арен, включая
  ту конверсию. Долгий отсчёт, этой работе не принадлежит.
- **против предыдущего рубежа** (`/tmp/bench-checkpoint-a.json` и далее) — что
  стоила **эта** задача. Это и есть число, по которому судят её ожидания.

Обе таблицы кладутся в документ рубежа, каждая с подписью, на какой вопрос
отвечает. Смешивать их в одну — тот же дефект, что и межмашинное сравнение:
число есть, смысла нет.

Ожидания, названные заранее:

| замер | ожидание на рубеже B |
|---|---|
| `BM_Eval_FormatNumber` | **не ждём заметного улучшения** — см. поправку ниже |
| `BM_Eval_StringLiteral` | без изменений — литерал уложен на компиляции |
| `BM_Eval_Constant` | без изменений |
| `BM_Eval_ShortPath` | без изменений либо быстрее |
| `BM_Eval_ArrayLiteral` / `ObjectLiteral` | без изменений — их черёд в задаче 9 |

**Поправка, внесённая после рубежа A (замер на одной машине против `3b3283d`).**
План утверждал, что `BM_Eval_FormatNumber` обязан ухудшиться после задач 4–5:
вычисленная строка стала аллокацией там, где был бамп в арене. Замер этого не
показал — ровно, −0.7%. Утверждение не подтвердилось, и вместе с ним стало
сомнительным зеркальное ожидание для рубежа B: если аллокация не была узким
местом, её устранение заметного выигрыша не даст.

Правдоподобное объяснение — **не проверенное**: замер упирается в перевод
double в кратчайшую десятичную запись (`formatNumber`, docs/semantics.md §4.3),
рядом с которым одна аллокация теряется. Проверить это дешевле, чем гадать:
если после встроенных строк `FormatNumber` остался ровным, а
`BM_Value_Materialize` заметно ускорился — объяснение верно, и это не провал
задачи 8, а неверно названное ожидание.

Что на рубеже B действительно должно улучшиться: `BM_Value_Materialize` для
строк короче шестнадцати байт — там аллокация исчезает целиком, и другого
объяснения у этого замера нет.

Записать в `docs/benchmarks/2026-08-19-memory-model-checkpoint-b.md`.

- [ ] **Step 9: Commit**

```bash
git add -A core cli benchmarks docs/benchmarks
git commit -m "$(cat <<'MSG'
perf: короткая строка стоила аллокации, хотя целиком помещалась в само значение

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 9: Хвостовое размещение агрегата

Решение Р4. Агрегат стоит одной аллокацией вместо двух: заголовок и элементы
рядом, как уже сделано у `StringBox`.

**Рубеж замеров C.**

**Files:**
- Modify: `core/src/box.hpp` (`ArrayBox`, `ObjectBox`, фабрики), `core/src/box.cpp`
- Modify: `core/src/aggregate.hpp` (обращения к `items`/`entries`)
- Modify: `core/src/c_api.cpp` (`asArray`/`asObject` и пять функций обхода)
- Test: `core/tests/box_test.cpp`

**Interfaces:**
- Consumes: всё предыдущее.
- Produces:
  - `ArrayBox`: `std::uint32_t len`, `std::uint32_t cap`, `Value *data`,
    методы `size()`, `at(i)`, `push(Value)`, `pop()`, `set(i, v)`.
  - `ObjectBox`: то же для `Entry`, плюс `insert(std::uint32_t at, const Entry &)`.
  - `std::vector` из обеих коробок **исчезает**; `box->items` и
    `box->entries` больше не существуют как члены.

- [ ] **Step 1: Написать падающий тест**

В `core/tests/box_test.cpp`:

```cpp
#ifndef NDEBUG
/// A literal aggregate costs one allocation, not two: the elements live in the
/// tail right behind the header, the way StringBox's bytes already do.
TEST(Box, LiteralAggregateIsOneAllocation) {
    ArrayBox *a = CS::detail::makeArrayBox(3);
    const char *header = reinterpret_cast<const char *>(a);
    const char *elements = reinterpret_cast<const char *>(a->data);
    EXPECT_EQ(elements, header + sizeof(ArrayBox));
    CS::detail::release(a);
}

/// Outgrowing the tail moves the ELEMENTS, never the box: every Value pointing
/// at this box keeps pointing at it. Growing the box itself would invalidate
/// all of them at once.
TEST(Box, BoxAddressSurvivesOutgrowingTheTail) {
    ArrayBox *a = CS::detail::makeArrayBox(1);
    const ArrayBox *before = a;
    a->push(Value::number(1));
    a->push(Value::number(2));
    a->push(Value::number(3));
    EXPECT_EQ(a, before);
    EXPECT_EQ(a->size(), 3u);
    EXPECT_EQ(a->at(2).numberValue(), 3.0);
    EXPECT_NE(reinterpret_cast<const char *>(a->data),
              reinterpret_cast<const char *>(a) + sizeof(ArrayBox));
    CS::detail::release(a);
}
#endif
```

- [ ] **Step 2: Прогнать и убедиться, что не собирается**

```bash
cmake --build build-dbg -j8 2>&1 | tail -20
```
Ожидается: у `ArrayBox` нет ни `data`, ни `push`, ни `size`.

- [ ] **Step 3: Переписать `ArrayBox`**

`core/src/box.hpp`:

```cpp
/// An array: header and elements in one allocation, elements right behind the
/// header.
///
///   ArrayBox                              one allocation
///  ┌──────────────────────────────┐
///  │ rc, kind                     │ Box
///  │ len, cap                     │
///  │ data ──────────┐             │      points into the tail while cap
///  ├────────────────▼─────────────┤      suffices; at a separate buffer after
///  │ Value  Value  Value  ...     │      the array outgrows it
///  └──────────────────────────────┘      tail, cap slots
///
/// THE BOX'S ADDRESS NEVER CHANGES, not even once the array has outgrown the
/// tail: only data moves. Every Value pointing at this box therefore stays
/// correct — which is what growing the box itself could not offer.
///
/// A literal fits the tail exactly: its size is known before the box is made,
/// and makeArray is already called with the exact capacity. Growth happens
/// only to an array grown through push (docs/semantics.md §8.5).
struct ArrayBox : Box {
    std::uint32_t len;
    std::uint32_t cap;
    Value *data;

    [[nodiscard]] std::uint32_t size() const noexcept { return len; }
    [[nodiscard]] const Value &at(std::uint32_t i) const noexcept {
        assert(i < len);
        return data[i];
    }
    void set(std::uint32_t i, Value v) noexcept { assert(i < len); data[i] = v; }
    void push(Value v);
    /// Precondition: len > 0.
    Value pop() noexcept { assert(len > 0); return data[--len]; }

    /// The tail's address. data equals it until the array outgrows the tail;
    /// after that the tail is dead space and data points at a separate buffer.
    [[nodiscard]] Value *tail() noexcept {
        return reinterpret_cast<Value *>(reinterpret_cast<char *>(this) +
                                         sizeof(ArrayBox));
    }
};
```

`core/src/box.cpp`:

```cpp
namespace {

/// Next capacity for an aggregate that has run out of room. Doubling, with a
/// floor of four: an array grown through push takes elements one at a time,
/// and a floor keeps the first few pushes from reallocating on every step.
std::uint32_t grownCapacity(std::uint32_t cap) noexcept {
    if (cap < 4) { return 4u; }
    // The clamp is not decoration: cap * 2 wraps to zero at 2^31, and a zero
    // capacity would make push write past the end of a zero-sized buffer.
    // An aggregate that large is a different problem, and it fails loudly.
    assert(cap <= 0x7fffffffu && "aggregate outgrew uint32 capacity");
    return cap * 2u;
}

}  // namespace

ArrayBox *makeArrayBox(std::uint32_t capacity) {
    void *raw = ::operator new(sizeof(ArrayBox) + capacity * sizeof(Value));
    ArrayBox *box = new (raw) ArrayBox;
    box->rc = 1;
    box->kind = Value::Kind::Array;
    box->len = 0;
    box->cap = capacity;
    box->data = box->tail();
    CHUPA_COUNT_BOX_BORN();
    return box;
}

void ArrayBox::push(Value v) {
    if (len == cap) {
        const std::uint32_t grown = grownCapacity(cap);
        Value *moved = static_cast<Value *>(::operator new(grown * sizeof(Value)));
        // Value is trivially copyable, so moving the elements is a copy of the
        // bytes and no reference counts change hands.
        std::memcpy(moved, data, len * sizeof(Value));
        if (data != tail()) { ::operator delete(static_cast<void *>(data)); }
        data = moved;
        cap = grown;
    }
    data[len++] = v;
}
```

`release`, ветка `Array`:

```cpp
        case Value::Kind::Array: {
            ArrayBox *a = static_cast<ArrayBox *>(box);
            for (std::uint32_t i = 0; i < a->len; ++i) { releaseValue(a->data[i]); }
            if (a->data != a->tail()) {
                ::operator delete(static_cast<void *>(a->data));
            }
            a->~ArrayBox();
            ::operator delete(static_cast<void *>(a));
            return;
        }
```

- [ ] **Step 4: Переписать `ObjectBox` тем же образом**

```cpp
///   ObjectBox                             one allocation
///  ┌──────────────────────────────┐
///  │ rc, kind                     │ Box
///  │ keys ── KeyTable *           │      held by reference
///  │ len, cap                     │
///  │ data ──────────┐             │
///  ├────────────────▼─────────────┤
///  │ Entry  Entry  Entry  ...     │      tail, cap slots, sorted by key bytes
///  └──────────────────────────────┘
struct ObjectBox : Box {
    KeyTable *keys;
    std::uint32_t len;
    std::uint32_t cap;
    Entry *data;

    [[nodiscard]] std::uint32_t size() const noexcept { return len; }
    [[nodiscard]] const Entry &at(std::uint32_t i) const noexcept {
        assert(i < len);
        return data[i];
    }
    void setValue(std::uint32_t i, Value v) noexcept {
        assert(i < len);
        data[i].value = v;
    }
    /// Inserts a pair at position `at`, keeping the pairs sorted by key bytes.
    void insert(std::uint32_t at, const Entry &entry);
    [[nodiscard]] Entry *tail() noexcept {
        return reinterpret_cast<Entry *>(reinterpret_cast<char *>(this) +
                                         sizeof(ObjectBox));
    }
};
```

`insert` растит по тому же правилу и сдвигает хвост через `std::memmove`
(`Entry` тривиально копируем: `uint32`, `uint32`, `Value`):

```cpp
void ObjectBox::insert(std::uint32_t at, const Entry &entry) {
    assert(at <= len);
    if (len == cap) {
        const std::uint32_t grown = grownCapacity(cap);
        Entry *moved = static_cast<Entry *>(::operator new(grown * sizeof(Entry)));
        std::memcpy(moved, data, len * sizeof(Entry));
        if (data != tail()) { ::operator delete(static_cast<void *>(data)); }
        data = moved;
        cap = grown;
    }
    // memmove, not memcpy: the source and destination ranges overlap by
    // everything but one slot.
    std::memmove(data + at + 1, data + at, (len - at) * sizeof(Entry));
    data[at] = entry;
    ++len;
}
```

- [ ] **Step 5: Перевести вызывающих**

- `core/src/box.cpp`: `findEntry` — `box.entries[mid]` → `box.at(mid)`,
  `box.entries.size()` → `box.size()`.
- `core/src/aggregate.hpp`: восемь функций чтения и пять изменения — на новые
  методы. `arrayPush` → `box->push(v)`; `arrayPop` → `box->pop()`;
  `objectSet` → `box.setValue(at, v)` либо `box.insert(at, Entry{...})`.
- `core/src/c_api.cpp`: `chupa_array_count/_at`, `chupa_object_count/_key_at/
  _value_at/_get` — на те же методы.

- [ ] **Step 6: Прогнать всё**

```bash
cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests && \
  ./build-dbg/cli/tests/chupa_cli_tests
./tools/asan.sh
swift test
```
Ожидается: зелено везде. ASan здесь обязателен: задача перекладывает
управление памятью с `std::vector` на ручное, и парность
`::operator new` / `::operator delete` — её главный риск.

- [ ] **Step 7: Рубеж C — замер**

```bash
cmake --build build-rel -j8
./build-rel/benchmarks/chupascript_benchmarks \
  --benchmark_repetitions=9 --benchmark_report_aggregates_only=true \
  --benchmark_format=json > /tmp/bench-checkpoint-c.json
python3 tools/bench-compare.py benchmarks/baseline.json /tmp/bench-checkpoint-c.json
python3 tools/bench-compare.py /tmp/bench-checkpoint-b.json /tmp/bench-checkpoint-c.json
```

**Сравнивать надо против двух точек, и они отвечают на разные вопросы.**
Выяснилось на рубеже A: `benchmarks/baseline.json` был снят на чужой машине, и
`tools/bench-compare.py` отказывался сравнивать вовсе (код 2), из-за чего рубеж
не сказал ничего. База переснята на этой машине в точке ветвления `f8f0273`
(коммит `e49f512`). Но между `f8f0273` и началом этой работы лежат 57 коммитов
конверсии на счётчик ссылок, поэтому:

- **против `benchmarks/baseline.json`** — что изменилось с эпохи арен, включая
  ту конверсию. Долгий отсчёт, этой работе не принадлежит.
- **против предыдущего рубежа** (`/tmp/bench-checkpoint-a.json` и далее) — что
  стоила **эта** задача. Это и есть число, по которому судят её ожидания.

Обе таблицы кладутся в документ рубежа, каждая с подписью, на какой вопрос
отвечает. Смешивать их в одну — тот же дефект, что и межмашинное сравнение:
число есть, смысла нет.

| замер | ожидание на рубеже C |
|---|---|
| `BM_Eval_ArrayLiteral` | **быстрее**: одна аллокация вместо двух |
| `BM_Eval_ObjectLiteral` | **быстрее**: то же |
| остальные | без изменений относительно рубежа B |

Записать в `docs/benchmarks/2026-08-19-memory-model-checkpoint-c.md`. Если
`ArrayLiteral`/`ObjectLiteral` оказались в верху профиля живого экрана —
сработал порог пересмотра слэб-аллокатора, названный в спеке Р4: зафиксировать
это отдельным пунктом, но в этой работе не решать.

- [ ] **Step 8: Commit**

```bash
git add -A core benchmarks docs/benchmarks
git commit -m "$(cat <<'MSG'
perf: агрегат стоил двух аллокаций там, где хвост за заголовком обходится одной

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---
### Task 10: Циклы — записаны в семантику и видны в прогоне

Решение Р12. Сборщика циклов не будет; вместо него — запись в спецификации
языка и прогон, который на цикле краснеет.

**Два отступления от спеки, оба назвать вслух при ревью:**

1. **Механизм.** Спека предлагает вести в отладочной сборке список созданных
   контекстом коробок и разбирать его при разрушении контекста. План этого не
   делает и опирается на LeakSanitizer: цикл — это память, недостижимая ни от
   стека, ни от глобальных, то есть ровно то, что детектор утечек уже находит.
   Собственный список стоил бы двух указателей в каждой коробке (в отладке —
   иной размер `Box`, чем в релизе) и повторял бы работу инструмента. Порог
   пересмотра: если понадобится **назвать место создания** цикла в терминах
   исходника ChupaScript, а не стека C++, свой список придётся завести.
2. **Место записи.** Спека называет §6 `docs/semantics.md`; про циклы там
   написано в **§2.3**, и абзац встаёт туда.

**Files:**
- Modify: `docs/semantics.md` §2.3
- Create: `core/tests/cycle_leak_main.cpp`
- Modify: `core/tests/CMakeLists.txt` (отдельная цель)
- Modify: `tools/asan.sh` (прогон, ожидающий отказа)

**Interfaces:**
- Consumes: всё предыдущее.
- Produces: исполняемый `chupascript_cycle_leak` — программа, которая
  **обязана** завершиться ненулевым кодом под ASan и нулевым без него.

- [ ] **Step 1: Написать программу, создающую цикл**

`core/tests/cycle_leak_main.cpp`:

```cpp
// A program that deliberately leaks one cycle, so that the leak detector has
// something to find.
//
// Not a gtest case: a passing test suite must stay green, and this program's
// whole purpose is to make the sanitizer report a leak. tools/asan.sh runs it
// separately and requires a NON-zero exit.
//
// Reference counting will never collect this. The language allows it in two
// lines (docs/semantics.md §2.3), a collector would cost more than the rest of
// the engine, and the BDUI screens this engine serves receive their data as a
// tree from the backend. So the limitation is documented, and the tooling
// makes an accidental one visible instead of silent.
#include <cstdio>

#include "context.hpp"

int main() {
    CS::Context ctx;
    CS::Diagnostic diag;
    if (!ctx.setVariableText("state", "{'items': []}", diag)) {
        std::fputs("setup failed\n", stderr);
        return 2;
    }

    CS::Script script;
    CS::Diagnostic diags[1];
    if (ctx.compileScript("state['self'] = state;", &script, diags, 1) != 0) {
        std::fputs(diags[0].message, stderr);
        return 2;
    }
    if (!ctx.run(script, diag)) {
        std::fputs(diag.message, stderr);
        return 2;
    }
    // The Context is destroyed here; the object holding itself is not.
    return 0;
}
```

- [ ] **Step 2: Собрать цель и убедиться, что без санитайзера она проходит**

`core/tests/CMakeLists.txt`:

```cmake
# A standalone program, not a test case: it leaks a cycle on purpose so that
# the leak detector has something to find. tools/asan.sh runs it and requires a
# non-zero exit; an ordinary build runs it and requires zero.
add_executable(chupascript_cycle_leak cycle_leak_main.cpp)
target_include_directories(chupascript_cycle_leak PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_link_libraries(chupascript_cycle_leak PRIVATE
    chupascript
    chupascript_compile_options
)
```

```bash
cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_cycle_leak; echo "exit=$?"
```
Ожидается: `exit=0` — без детектора цикл невидим, и это ровно то, что задача
исправляет в инструментах.

- [ ] **Step 2a: Сперва сделать так, чтобы «чисто» было достижимо**

Выяснилось на задаче 8: `tools/asan.sh` возвращает 1 **всегда**, потому что семь
тестов намеренно строят циклы со ссылками (со ссылкой на `docs/semantics.md`
§2.3) и потому текут — 577 байт в ядре, 495 в оболочке, число к числу одинаково
от задачи к задаче. Пока это так, требование «программа с циклом обязана
провалиться» не несёт сигнала: провалено и без неё.

Разорвать цикл в конце каждого из семи тестов средствами языка — присваиванием
`null` в поле либо снятием элемента через `pop`. Проверяемое ими свойство от
этого не страдает: §2.3 обещает, что цикл **строится** и что ни одна операция на
нём не зацикливается, а не что он живёт до конца процесса. После этого
`tools/asan.sh` начинает отвечать «чисто», и единственным течущим остаётся
`chupascript_cycle_leak` — ради чего он и заводится.

Найти их: `grep -rn "semantics.md §2.3\|self" core/tests cli/tests`.

Прогнать `./tools/asan.sh` и убедиться, что он **зелёный** — до того, как
добавлять программу с циклом. Иначе шаг 4 проверяет не то, что думает.

- [ ] **Step 3: Научить `tools/asan.sh` требовать отказа**

В конец `tools/asan.sh`, до `exit`:

```bash
# The cycle program must FAIL here: a reference-counted cycle is unreachable
# memory, which is exactly what the leak detector reports. A zero exit means
# the detector stopped seeing it — the check has gone quiet, not the defect.
if "./${BUILD_DIR}/core/tests/chupascript_cycle_leak" 2>/dev/null; then
    echo "cycle leak went unreported — the leak detector is not doing its job" >&2
    status=1
fi
```

- [ ] **Step 4: Прогнать и убедиться, что цикл виден**

```bash
./tools/asan.sh
```
Ожидается: зелено — то есть тестовые наборы прошли **и** программа с циклом
отчиталась утечкой. Проверить глазами, что в выводе есть
`ERROR: LeakSanitizer: detected memory leaks` от `chupascript_cycle_leak`.

- [ ] **Step 5: Записать ограничение в спецификацию языка**

`docs/semantics.md`, §2.3, после абзаца «Ссылочность допускает циклы»:

```markdown
Цикл, однако, **не освобождается**. Память движка держится счётчиком ссылок, а
счётчик цикл не соберёт никогда: две коробки, ссылающиеся друг на друга,
удерживают друг друга сами. `obj['self'] = obj` и `push(obj.items, obj)` —
корректные программы, но каждая из них оставляет свои коробки жить до конца
процесса, даже после разрушения контекста.

Сборщика циклов в движке нет и не планируется: это отдельная подсистема,
стоящая больше всего остального движка, а данные, ради которых движок писался,
приходят с бэкенда деревом. Отладочный прогон под детектором утечек
(`tools/asan.sh`) на таком цикле краснеет — случайный цикл виден, а не молчит.
```

- [ ] **Step 6: Commit**

```bash
git add docs/semantics.md core/tests/cycle_leak_main.cpp \
        core/tests/CMakeLists.txt tools/asan.sh
git commit -m "$(cat <<'MSG'
docs: ограничение про циклы знал тот, кто читал design-документ, а не тот, кто пишет скрипт

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

### Task 11: Замер BDUI и сведение результатов

Единственная проверка, отвечающая на вопрос, ради которого всё затевалось:
перестали ли строки быть слабым местом. Микрозамеры на него не отвечают —
отвечает монорепозиторий.

**Files:**
- Create: `docs/benchmarks/2026-08-19-bdui-after-memory-model-redesign.md`
- Modify: `benchmarks/baseline.json` (перезапись базы — только после того, как
  все три рубежа сведены)
- Modify: `docs/superpowers/specs/2026-08-19-memory-model-redesign-design.md`
  (пометка о выполненности, если решения разошлись с планом)

**Interfaces:**
- Consumes: собранные библиотеки после задачи 9.
- Produces: сводный документ замеров и обновлённая база.

- [ ] **Step 1: Собрать релизную библиотеку для монорепозитория**

По той же процедуре, что дала замер от 17.08.2026 — воспроизвести шаги из
`docs/benchmarks/bdui-after-borrowed-string-2026-08-17.md`, раздел про
подготовку. Ничего нового не выдумывать: сопоставимы только замеры, снятые
одинаково.

- [ ] **Step 2: Прогнать `ExpressionEvalBenchmark_Tests` и `WidgetPropertyBenchmark_Tests`**

Прогонять на той же машине, что и замер от 17.08.2026: абсолютные числа между
машинами несопоставимы (`tools/bench-compare.py`, докстринг).

- [ ] **Step 3: Свести таблицу отношений**

`docs/benchmarks/2026-08-19-bdui-after-memory-model-redesign.md` — та же
таблица, что в спеке §2.3, с новой колонкой:

| кейс | было (17.08) | стало | цель |
|---|---|---|---|
| константа: число, флаг | 4.38 | | не хуже |
| отображение, попадание | 4.10 | | не хуже |
| переменная: число | 1.51 | | не хуже |
| **интерполяция** | **0.85** | | **> 1.0** |
| **переменная: строка** | **0.69** | | **> 1.0** |

Замер от 17.08.2026 сделан **до** прихода счётчика ссылок — это записать в
документе прямо, чтобы сравнение не выглядело чище, чем оно есть.

- [ ] **Step 4: Назвать исход честно**

Если интерполяция либо переменная-строка не перешли 1.0 — так и написать, с
разбором где именно уходит время, и завести пункт в `docs/backlog.md`.
Работа от этого не отменяется: четыре дефекта закрыты и одна модель времени
жизни заменила четыре независимо от того, как легли числа. Но заявление «строки
перестали быть слабым местом» без чисел не делается.

- [ ] **Step 5: Обновить базу микрозамеров**

```bash
cmake --build build-rel -j8
./build-rel/benchmarks/chupascript_benchmarks \
  --benchmark_repetitions=9 --benchmark_report_aggregates_only=true \
  --benchmark_format=json > benchmarks/baseline.json
```

Только после того, как все три рубежа сведены и объяснены: перезаписанная база
скрывает регрессию, которую ещё не разобрали.

- [ ] **Step 6: Полная проверка перед закрытием**

```bash
cmake --build build-dbg -j8 && ./build-dbg/core/tests/chupascript_tests
./build-dbg/cli/tests/chupa_cli_tests
cmake --build build-rel -j8 && ./build-rel/core/tests/chupascript_tests
./tools/asan.sh
./tools/tsan.sh
swift test
```
Ожидается: зелено во всех шести.

- [ ] **Step 7: Commit**

```bash
git add docs/benchmarks benchmarks/baseline.json docs/backlog.md
git commit -m "$(cat <<'MSG'
docs: заявление «строки перестали быть слабым местом» стояло без чисел, на которых держится

Co-Authored-By: Claude Opus 5 (1M context) <noreply@anthropic.com>
MSG
)"
```

---

## Сводка покрытия спеки

| решение спеки | задача |
|---|---|
| Р1 самодостаточное значение | 5 (снятие региона), 8 (встроенные строки) |
| Р2 раскладка `Value` | 8 |
| Р3 арены нет | 4 (собиратель), 5 (снятие) |
| Р4 хвостовое размещение, без слэба | 9 |
| Р5 владение: три отношения | 5 (корни), 6 (`lastResult_`), 9 (содержание) |
| Р6 литералы у `Ast` | 3 |
| Р7 что остаётся от `Store` | 5 |
| Р8 `stringBytes` | 5, дополнено в 8 |
| Р9 `Context` и `Execution` | 5 (состав), 6 (`lastResult_`) |
| Р10 единица помнит контекст | 2 |
| Р11 потоковый контракт | 1 |
| Р12 циклы | 10 |
| §5 граница с хостом | 6, 7 |
| §6 проверка: тесты | по одному в каждой задаче |
| §6 проверка: три рубежа | 5 (A), 8 (B), 9 (C) |
| §6 замер BDUI | 11 |
| Б1 | 4 |
| Б2 | 5 |
| В1 | 1 |
| В2 | 2 |
| В3 | 6 (устройство), 8 (проверка под ASan) |

## Отступления от спеки, названные заранее

1. **Порядок Р3 перед Р2** — иначе пришлось бы выдумать временную раскладку под
   `Region::Scratch` внутри нового тега и через задачу её удалить. Разобрано в
   разделе «Порядок задач».
2. **Номер на `Store`, а не на `Context` (Р10)** — у контекста ровно одно
   хранилище, а компиляция уже получает `Store &`. Разобрано в задаче 2.
3. **Цикл ловит LeakSanitizer, а не собственный список коробок (Р12)** — тот же
   результат без двух указателей в каждой коробке. Порог пересмотра назван в
   задаче 10.
4. **Абзац про циклы встаёт в §2.3 `docs/semantics.md`, а не в §6** — про циклы
   написано именно там.

Каждое отступление — вопрос к ревью, а не решённое дело.
