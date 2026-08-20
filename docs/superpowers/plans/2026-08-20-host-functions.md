# Функции хоста: план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Хост регистрирует собственные функции на контексте, и программа на
ChupaScript зовёт их тем же синтаксисом, что и встроенные.

**Architecture:** Имя вызова разрешается на компиляции в одну из двух таблиц —
закрытую таблицу билтинов либо таблицу, наполненную хостом; разрешённое кладётся
в узел дерева одним байтом со старшим битом-признаком. Вычисление берёт готовое
и зовёт указатель. На время вызова контекст закрыт: хосту доступны только чтение
аргументов, создание значения и `chupa_fail`.

**Tech Stack:** C++17, C API, Google Test, Google Benchmark, Swift 5 (обвязка),
CMake.

**Spec:** `docs/superpowers/specs/2026-08-20-host-functions-design.md`

## Global Constraints

Взяты из спеки и из правил, действующих в этом репозитории. Требования каждой
задачи включают этот раздел неявно.

- **К1. Комментарий начинается со схемы раскладки** — что из чего состоит и кто
  чем владеет. Технический английский — **только для нового и переписанного
  кода**; существующие русские комментарии не переводятся, если хунк их не
  переписывает целиком.
- **К2. `sizeof(Value) == 16` и тривиальная копируемость не ослабляются
  никогда.** Ни одна задача плана `Value` не трогает.
- **К3. `sizeof(Ast::Node) == 24`** — `static_assert` в `ast.hpp`. Задача 1
  меняет тип поля, но не размер узла.
- **К4. Семантика языка заморожена.** `docs/semantics.md` и `docs/grammar.md`
  правятся ровно в тех местах, что перечислены в §14 спеки, и ни в одном
  другом.
- **К5. Всякое существительное — названная сущность.** Всякое утверждение о
  времени жизни называет оба конца: от какого события до какого.
- **К6. Одна строка на каждое неочевидное решение**, называющая отвергнутую
  альтернативу.
- **К7. Повествование — в сообщение коммита**, не в комментарий кода.
- **К8. Число тестов не падает.** Если после задачи тестов стало меньше — это
  дефект задачи, а не её результат; восстановить и объяснить.
- **К9. Каждая задача заканчивается зелёной сборкой** `cmake --build build-dbg`
  и `ctest`, а задачи, трогающие время жизни, — ещё и `tools/asan.sh` с кодом 0.

---

## Порядок задач и почему он такой

Снизу вверх по зависимостям, чтобы ни одна задача не ждала следующую.

1. **Ast** учится хранить ссылку на вызываемое — без этого `check` некуда
   класть разрешённое имя хост-функции.
2. **BuiltinInfo** получает два столбца — `resolveCallee` обязан отдавать
   одинаковую форму для обеих таблиц, а у билтинов половины этой формы пока
   нет.
3. **Таблица хост-функций** и её проверки — чистый C++, без C API: так отказы
   регистрации проверяются тестами напрямую.
4. **Контекст** получает таблицу, признак «идёт вычисление» и вызов `release`
   при разрушении.
5. **resolveCallee и check** — статический проход начинает видеть обе таблицы.
6. **Стек аргументов** в `Execution` — вычислению нужен буфер до того, как
   появится ветка вызова.
8. **C API**: регистрация и создание значений — до вычисления, потому что его
   тесты собирают результат коллбэка этими функциями.
9. **Вычисление** зовёт хост-функцию.
10. **C API**: `chupa_fail`, `CHUPA_ERR_HOST`, закрытые двери.
11. **Обвязка Swift.**
12. **Документы.**
13. **Оболочка и замер.**

Задачи 1–7 не меняют наблюдаемого поведения ни для одного существующего
потребителя: до задачи 8 зарегистрировать функцию снаружи нечем. Это намеренно —
семь задач подряд проверяются тестами ядра и не могут сломать хост.

## Состав файлов

| файл | что делает | задача |
|---|---|---|
| `core/src/ast.hpp`, `ast.cpp` | `CalleeRef` в узле вместо `Builtin` | 1 |
| `core/src/builtin.hpp`, `builtin.cpp` | два столбца в `BuiltinInfo` | 2 |
| `core/src/host.hpp`, `host.cpp` | **новый** — запись таблицы, проверки регистрации | 3 |
| `core/src/context.hpp`, `context.cpp` | таблица, признак вычисления, `release` | 4 |
| `core/src/callee.hpp` | **новый** — `Callee` и `resolveCallee` | 6 |
| `core/src/check.hpp`, `check.cpp` | режим компиляции, обе таблицы, чистота | 5, 6 |
| `core/src/execution.hpp` | стек аргументов | 7 |
| `core/src/eval.cpp` | ветка вызова хост-функции | 9 |
| `core/include/chupascript/chupascript.h` | типы границы, шесть функций | 3, 8, 10 |
| `core/src/c_api.cpp` | их реализация, закрытые двери | 8, 10 |
| `Sources/ChupaScript/HostFunction.swift` | **новый** — перегрузки `register` | 11 |
| `docs/semantics.md`, `docs/grammar.md`, `docs/backlog.md` | правки из §14 спеки | 12 |
| `cli/main.cpp`, `benchmarks/host_benchmark.cpp` | `echo` и замер вызова | 13 |

**Почему `callee.hpp` отдельно от `host.hpp`.** `host.hpp` — про таблицу: что
такое запись, как её положить, за что отказать. `callee.hpp` — про разрешение
имени, и его читают `check` и `eval`, которым таблица как таковая не нужна.
Отвергнутая альтернатива — сложить оба в `host.hpp`: тогда `check.cpp` потянул
бы за собой проверки регистрации, к которым отношения не имеет.

---

## Задача 1: `Ast` хранит ссылку на вызываемое

**Files:**
- Modify: `core/src/builtin_id.hpp` — добавляется `CalleeRef` и её помощники
- Modify: `core/src/ast.hpp` — поле узла и три метода
- Modify: `core/src/ast.cpp` — их реализация
- Modify: `core/src/check.cpp:46-58` — кладёт `CalleeRef`, читает обратно
- Modify: `core/src/eval.cpp:405-419` — читает `CalleeRef`
- Test: `core/tests/ast_test.cpp`

**Interfaces:**
- Produces: `CS::CalleeRef` (перечисление на `std::uint8_t`);
  `calleeOfBuiltin(Builtin) -> CalleeRef`;
  `calleeOfHost(std::uint8_t index) -> CalleeRef`;
  `isHostCallee(CalleeRef) -> bool`;
  `builtinOfCallee(CalleeRef) -> Builtin`;
  `hostIndexOfCallee(CalleeRef) -> std::uint8_t`;
  `kNoCallee`, `kMaxHostFunctions == 127`;
  `Ast::callee(NodeId) -> CalleeRef`, `Ast::setCallee(NodeId, CalleeRef)`,
  `Ast::hasCallee(NodeId) -> bool`.

Прежние `Ast::builtinId`, `setBuiltinId`, `hasBuiltinId` **удаляются**: их
заменяют три метода выше. Оставлять обе пары нельзя — две правды об одном поле.

- [ ] **Шаг 1: тест на раскладку и обратимость**

В `core/tests/ast_test.cpp`:

```cpp
TEST(CalleeRef, BuiltinRoundTrips) {
    for (int i = 0; i <= static_cast<int>(CS::Builtin::Str); ++i) {
        const CS::Builtin id = static_cast<CS::Builtin>(i);
        const CS::CalleeRef ref = CS::calleeOfBuiltin(id);
        EXPECT_FALSE(CS::isHostCallee(ref));
        EXPECT_EQ(CS::builtinOfCallee(ref), id);
    }
}

TEST(CalleeRef, HostRoundTrips) {
    for (std::uint8_t i = 0; i < CS::kMaxHostFunctions; ++i) {
        const CS::CalleeRef ref = CS::calleeOfHost(i);
        EXPECT_TRUE(CS::isHostCallee(ref));
        EXPECT_EQ(CS::hostIndexOfCallee(ref), i);
    }
}

/// Ссылка на хост-функцию с наибольшим допустимым номером обязана
/// отличаться от «не разрешено»: иначе последняя зарегистрированная функция
/// выглядела бы неразрешённым именем.
TEST(CalleeRef, LastHostIndexIsNotNoCallee) {
    EXPECT_NE(CS::calleeOfHost(CS::kMaxHostFunctions - 1), CS::kNoCallee);
}

TEST(AstNode, CalleeStartsUnresolved) {
    CS::Ast ast;
    const CS::NodeId node = ast.addCall(0, 0);
    EXPECT_FALSE(ast.hasCallee(node));
}

TEST(AstNode, CalleeSurvivesRoundTrip) {
    CS::Ast ast;
    const CS::NodeId node = ast.addCall(0, 0);
    ast.setCallee(node, CS::calleeOfHost(5));
    EXPECT_TRUE(ast.hasCallee(node));
    EXPECT_TRUE(CS::isHostCallee(ast.callee(node)));
    EXPECT_EQ(CS::hostIndexOfCallee(ast.callee(node)), 5);
}
```

> Если в `Ast` нет метода `addCall(offset, textLength)`, соберите узел тем же
> способом, каким это делают соседние тесты в `ast_test.cpp`, — важен узел вида
> `NodeKind::Call`, а не способ его создания.

- [ ] **Шаг 2: убедиться, что тест не собирается**

Run: `cmake --build build-dbg -j8 2>&1 | head -20`
Expected: ошибка компиляции — `CalleeRef` не объявлен.

- [ ] **Шаг 3: `CalleeRef` в `builtin_id.hpp`**

Дописать после объявления `kNoBuiltin`:

```cpp
/// What the name of a Call node resolved to: a builtin, a host function, or
/// nothing yet.
///
/// LAYOUT — one byte, three ranges:
///
///   0 … 127     a builtin; the value is the Builtin itself
///   128 … 254   a host function; the index into the Context's table is
///               (value - 128), so 0 … 126
///   255         unresolved — kNoCallee
///
/// One byte because that is the room Ast::Node has: the field sits at offset
/// 2 of a node that must stay 24 bytes (ast.hpp). The rejected alternative
/// was a wider field in the four spare bytes at offset 20 — four bytes on
/// every node in the tree, paid to lift a ceiling of 127 host functions that
/// no host comes near.
///
/// Lives beside Builtin, and for the same reason stated above it: what a name
/// resolves to is a fact of parsing, and ast.hpp must learn it without
/// depending on builtin.hpp, which knows about values.
enum class CalleeRef : std::uint8_t {};

/// Bit that tells the two ranges apart.
inline constexpr std::uint8_t kHostCalleeBit = 0x80;

/// Name not resolved: a Call node before check, or a name no table holds.
inline constexpr CalleeRef kNoCallee = static_cast<CalleeRef>(255);

/// How many host functions one Context may hold. 127, not 128: index 127
/// would encode as 255, which is kNoCallee.
inline constexpr std::uint8_t kMaxHostFunctions = 127;

constexpr CalleeRef calleeOfBuiltin(Builtin id) noexcept {
    return static_cast<CalleeRef>(static_cast<std::uint8_t>(id));
}

constexpr CalleeRef calleeOfHost(std::uint8_t index) noexcept {
    return static_cast<CalleeRef>(index | kHostCalleeBit);
}

constexpr bool isHostCallee(CalleeRef ref) noexcept {
    return ref != kNoCallee &&
           (static_cast<std::uint8_t>(ref) & kHostCalleeBit) != 0;
}

/// Precondition: !isHostCallee(ref) and ref != kNoCallee.
constexpr Builtin builtinOfCallee(CalleeRef ref) noexcept {
    return static_cast<Builtin>(static_cast<std::uint8_t>(ref));
}

/// Precondition: isHostCallee(ref).
constexpr std::uint8_t hostIndexOfCallee(CalleeRef ref) noexcept {
    return static_cast<std::uint8_t>(ref) & ~kHostCalleeBit;
}

static_assert(static_cast<std::uint8_t>(Builtin::Str) < kHostCalleeBit,
              "the builtin table grew into the host range: widen CalleeRef");
```

- [ ] **Шаг 4: поле и методы в `ast.hpp`**

В `struct Node` заменить

```cpp
        /// 2 — Call; заполняет check. См. builtinId().
        Builtin builtin = kNoBuiltin;
```

на

```cpp
        /// 2 — Call; заполняет check. См. callee().
        CalleeRef callee = kNoCallee;
```

Три метода-объявления `builtinId` / `setBuiltinId` / `hasBuiltinId` заменить на:

```cpp
    /// What the name of this Call node resolved to — a builtin or a host
    /// function (builtin_id.hpp).
    ///
    /// check (core/src/check.hpp) resolves it, and it is the only place where
    /// the name of a function is looked up at all; evaluation reads what is
    /// already there. findBuiltin used to run on every evaluation of every
    /// call, reading the name text out of the source with it (docs/backlog.md
    /// B54).
    ///
    /// Precondition: the tree passed check and this node is a Call whose name
    /// is known. An unknown name never reaches evaluation — check rejects it
    /// with a Name error and the field stays kNoCallee.
    [[nodiscard]] CalleeRef callee(NodeId node) const noexcept;

    /// Records what the name resolved to. Called only by check.
    void setCallee(NodeId node, CalleeRef ref) noexcept;

    /// Is the name resolved. check asks — so it does not look the name up a
    /// second time when it checks how the result is used; evaluation does
    /// not, its answer is guaranteed.
    [[nodiscard]] bool hasCallee(NodeId node) const noexcept;
```

`static_assert(sizeof(Node) == 24, …)` не трогается: `CalleeRef` того же
размера, что `Builtin`.

- [ ] **Шаг 5: реализация в `ast.cpp`**

Найти три определения `builtinId` / `setBuiltinId` / `hasBuiltinId` и заменить
телами:

```cpp
CalleeRef Ast::callee(NodeId node) const noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::Call);
    return nodes_[node].callee;
}

void Ast::setCallee(NodeId node, CalleeRef ref) noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::Call);
    nodes_[node].callee = ref;
}

bool Ast::hasCallee(NodeId node) const noexcept {
    assert(node < nodes_.size());
    assert(nodes_[node].kind == NodeKind::Call);
    return nodes_[node].callee != kNoCallee;
}
```

> Утверждения перенесите ровно те, что стояли в прежних телах: если там
> проверялся только индекс, не добавляйте проверку вида узла, и наоборот.
> Задача переносит поле, а не ужесточает контракт.

- [ ] **Шаг 6: два места вызова**

`core/src/check.cpp`, в `checkCall` заменить `ast.setBuiltinId(node, id);` на

```cpp
        ast.setCallee(node, calleeOfBuiltin(id));
```

В `requireValue` и `requireVoid` заменить

```cpp
        if (!ast.hasBuiltinId(call)) { return; }
        if (!builtinInfo(ast.builtinId(call)).returnsValue) {
```

на

```cpp
        if (!ast.hasCallee(call)) { return; }
        if (!builtinInfo(builtinOfCallee(ast.callee(call))).returnsValue) {
```

(и симметрично во втором методе). `core/src/eval.cpp`, ветка
`NodeKind::Call`, заменить

```cpp
            const Builtin id = ast.builtinId(node);
```

на

```cpp
            const Builtin id = builtinOfCallee(ast.callee(node));
```

Хост-функций пока не существует, поэтому `isHostCallee` здесь ещё не
спрашивается: ветка появится в задаче 7.

- [ ] **Шаг 7: сборка и тесты**

Run: `cmake --build build-dbg -j8 && ctest --test-dir build-dbg --output-on-failure`
Expected: всё зелено, тестов на четыре больше, чем было.

- [ ] **Шаг 8: коммит**

```bash
git add core/src/builtin_id.hpp core/src/ast.hpp core/src/ast.cpp \
        core/src/check.cpp core/src/eval.cpp core/tests/ast_test.cpp
git commit -m "refactor: узел вызова знал только про билтины, а хост-функции ложатся в тот же байт"
```

---

## Задача 2: `BuiltinInfo` объявляет чистоту и детерминированность

**Files:**
- Modify: `core/src/builtin.hpp` — два поля в структуре
- Modify: `core/src/builtin.cpp` — двенадцать строк таблицы
- Test: `core/tests/builtin_test.cpp`

**Interfaces:**
- Consumes: ничего из задачи 1.
- Produces: `BuiltinInfo::pure`, `BuiltinInfo::deterministic` — их прочитает
  `resolveCallee` в задаче 5.

- [ ] **Шаг 1: тест на состав таблицы**

В `core/tests/builtin_test.cpp`:

```cpp
/// push и pop — единственные билтины, меняющие данные. Тест перечисляет их
/// поимённо, а не выводит из returnsValue: совпадение этих двух признаков у
/// сегодняшних двенадцати функций — доказательство docs/grammar.md §6.3, и
/// выводить одно из другого значило бы сделать это доказательство
/// непроверяемым.
TEST(BuiltinTable, PushAndPopAreTheOnlyImpureOnes) {
    for (int i = 0; i <= static_cast<int>(CS::Builtin::Str); ++i) {
        const CS::Builtin id = static_cast<CS::Builtin>(i);
        const bool mutates = id == CS::Builtin::Push || id == CS::Builtin::Pop;
        EXPECT_EQ(CS::builtinInfo(id).pure, !mutates)
            << "builtin: " << CS::builtinInfo(id).name;
    }
}

/// Детерминированность обещает, что результат можно взять из кэша; грязная
/// функция зовётся ради побочного эффекта, и пропуск вызова его отменяет.
/// Сочетание запрещено и у билтинов, и у хост-функций (спека §6).
TEST(BuiltinTable, NoImpureBuiltinClaimsDeterminism) {
    for (int i = 0; i <= static_cast<int>(CS::Builtin::Str); ++i) {
        const CS::BuiltinInfo &info =
            CS::builtinInfo(static_cast<CS::Builtin>(i));
        if (!info.pure) { EXPECT_FALSE(info.deterministic) << info.name; }
    }
}
```

- [ ] **Шаг 2: убедиться, что тест не собирается**

Run: `cmake --build build-dbg -j8 2>&1 | head -20`
Expected: `no member named 'pure' in 'CS::BuiltinInfo'`.

- [ ] **Шаг 3: два поля в структуре**

`core/src/builtin.hpp`, в `struct BuiltinInfo` после `returnsValue`:

```cpp
    bool pure;           ///< false — меняет данные (docs/grammar.md §6.3)
    bool deterministic;  ///< задел под кэш props (docs/backlog.md B29)
```

и над структурой дописать к её комментарию:

```cpp
/// pure and deterministic are declared, not derived from returnsValue.
///
/// Today the two coincide: push and pop are the only builtins that mutate,
/// and they are also the only ones returning Void. That coincidence IS
/// docs/grammar.md §6.3 — the proof that an expression cannot change data.
/// Deriving one from the other in code would hold only while the proof
/// holds, and the first builtin that both mutates and returns a value would
/// silently mark itself pure, surfacing as a wrong answer in the props cache
/// (docs/backlog.md B29) rather than as a compile error here.
```

- [ ] **Шаг 4: двенадцать строк таблицы**

`core/src/builtin.cpp`:

```cpp
constexpr BuiltinInfo kTable[] = {
    {"abs", 1, 1, true, true, true},
    {"count", 1, 1, true, true, true},
    {"format", 1, kVariadic, true, true, true},
    {"has", 2, 2, true, true, true},
    {"keys", 1, 1, true, true, true},
    {"last", 1, 1, true, true, true},
    {"max", 2, 2, true, true, true},
    {"min", 2, 2, true, true, true},
    {"pop", 1, 1, false, false, false},
    {"push", 2, 2, false, false, false},
    {"round", 1, 1, true, true, true},
    {"str", 1, 1, true, true, true},
};
```

- [ ] **Шаг 5: сборка и тесты**

Run: `cmake --build build-dbg -j8 && ctest --test-dir build-dbg --output-on-failure`
Expected: всё зелено, тестов на два больше.

- [ ] **Шаг 6: коммит**

```bash
git add core/src/builtin.hpp core/src/builtin.cpp core/tests/builtin_test.cpp
git commit -m "feat: таблица билтинов знала про арность, но не про чистоту, которой скоро мерить и хост-функции"
```

---

## Задача 3: таблица хост-функций и отказы регистрации

**Files:**
- Modify: `core/include/chupascript/chupascript.h` — **только типы**, функций
  пока не добавляется
- Create: `core/src/host.hpp`, `core/src/host.cpp`
- Modify: `core/src/CMakeLists.txt` — `host.cpp` в список
- Create: `core/tests/host_test.cpp`
- Modify: `core/tests/CMakeLists.txt` — `host_test.cpp` в список

**Interfaces:**
- Consumes: `kMaxHostFunctions` из задачи 1; `CS::isGlobalName` из
  `core/src/data.hpp`; `CS::findBuiltin` из `core/src/builtin.hpp`.
- Produces: `ChupaFunctionFlags`, `CHUPA_VARIADIC`, `ChupaHostFunction`,
  `ChupaFunction` (публичный заголовок); `CS::HostFunction`,
  `CS::RegisterOutcome`, `CS::HostTable` с методами `add`, `find`, `at`,
  `size`.

**Почему типы кладутся в публичный заголовок уже сейчас,** хотя функций C API
ещё нет: `ChupaHostFunction` и `ChupaFunction` — типы границы по определению, и
заводить в ядре их параллельные копии значило бы держать две правды об одном.
Отвергнутая альтернатива — свой тип указателя в `host.hpp` и перевод в C API:
перевод пришлось бы поддерживать верным вечно.

- [ ] **Шаг 1: тесты на все отказы регистрации**

Создать `core/tests/host_test.cpp`:

```cpp
#include <gtest/gtest.h>

#include "host.hpp"

namespace {

bool neverCalled(ChupaContext *, const ChupaValue *, size_t, ChupaValue *,
                 void *) {
    ADD_FAILURE() << "коллбэк звался там, где вызова быть не должно";
    return false;
}

/// Описание, которое проходит все проверки: тесты ниже портят его по одному
/// полю за раз, поэтому исправным он обязан быть ровно один.
///
/// Задача 4 выносит эту функцию в core/tests/host_fixture.hpp — её зовут ещё
/// три файла тестов, и копия в каждом разошлась бы при первой же правке
/// состава ChupaFunction.
ChupaFunction healthyFunction(const char *name) {
    ChupaFunction fn{};
    fn.name = name;
    fn.name_len = std::char_traits<char>::length(name);
    fn.min_args = 1;
    fn.max_args = 1;
    fn.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_PURE | CHUPA_FN_DETERMINISTIC;
    fn.call = neverCalled;
    fn.user_data = nullptr;
    fn.release = nullptr;
    return fn;
}

}  // namespace

TEST(HostTable, AcceptsHealthyDescriptor) {
    CS::HostTable table;
    EXPECT_EQ(table.add(healthyFunction("formatDate")), CS::RegisterOutcome::Ok);
    EXPECT_EQ(table.size(), 1u);
}

TEST(HostTable, RefusesNameThatIsNotAnIdentifier) {
    CS::HostTable table;
    EXPECT_EQ(table.add(healthyFunction("format date")), CS::RegisterOutcome::BadName);
    EXPECT_EQ(table.add(healthyFunction("")), CS::RegisterOutcome::BadName);
    EXPECT_EQ(table.add(healthyFunction("1st")), CS::RegisterOutcome::BadName);
}

TEST(HostTable, RefusesReservedWord) {
    CS::HostTable table;
    EXPECT_EQ(table.add(healthyFunction("return")), CS::RegisterOutcome::BadName);
    EXPECT_EQ(table.add(healthyFunction("null")), CS::RegisterOutcome::BadName);
}

/// Совпадение с билтином отвергается, а не разрешается с приоритетом: иначе
/// count переопределяется хостом и семантика docs/semantics.md §8 перестаёт
/// быть свойством языка.
TEST(HostTable, RefusesNameTakenByBuiltin) {
    CS::HostTable table;
    EXPECT_EQ(table.add(healthyFunction("count")), CS::RegisterOutcome::NameTaken);
    EXPECT_EQ(table.add(healthyFunction("format")), CS::RegisterOutcome::NameTaken);
}

TEST(HostTable, RefusesDuplicateRegistration) {
    CS::HostTable table;
    ASSERT_EQ(table.add(healthyFunction("formatDate")), CS::RegisterOutcome::Ok);
    EXPECT_EQ(table.add(healthyFunction("formatDate")), CS::RegisterOutcome::NameTaken);
    EXPECT_EQ(table.size(), 1u);
}

TEST(HostTable, RefusesNullCallback) {
    CS::HostTable table;
    ChupaFunction fn = healthyFunction("formatDate");
    fn.call = nullptr;
    EXPECT_EQ(table.add(fn), CS::RegisterOutcome::NoCallback);
}

TEST(HostTable, RefusesInvertedArity) {
    CS::HostTable table;
    ChupaFunction fn = healthyFunction("formatDate");
    fn.min_args = 3;
    fn.max_args = 2;
    EXPECT_EQ(table.add(fn), CS::RegisterOutcome::BadArity);
}

TEST(HostTable, AcceptsVariadicArity) {
    CS::HostTable table;
    ChupaFunction fn = healthyFunction("joinAll");
    fn.min_args = 1;
    fn.max_args = CHUPA_VARIADIC;
    EXPECT_EQ(table.add(fn), CS::RegisterOutcome::Ok);
}

/// Детерминированность обещает, что вызов можно пропустить, взяв результат из
/// кэша; грязная функция зовётся ради побочного эффекта, и пропуск его
/// отменяет. Объявить оба — попросить движок пропускать непропускаемое.
TEST(HostTable, RefusesDeterministicWithoutPure) {
    CS::HostTable table;
    ChupaFunction fn = healthyFunction("track");
    fn.flags = CHUPA_FN_DETERMINISTIC;
    EXPECT_EQ(table.add(fn), CS::RegisterOutcome::BadFlags);
}

TEST(HostTable, RefusesWhenFull) {
    CS::HostTable table;
    for (std::uint8_t i = 0; i < CS::kMaxHostFunctions; ++i) {
        const std::string name = "fn" + std::to_string(i);
        ChupaFunction fn = healthyFunction(name.c_str());
        fn.name_len = name.size();
        ASSERT_EQ(table.add(fn), CS::RegisterOutcome::Ok) << "на номере " << int(i);
    }
    EXPECT_EQ(table.add(healthyFunction("oneTooMany")), CS::RegisterOutcome::TableFull);
}

TEST(HostTable, FindsRegisteredByName) {
    CS::HostTable table;
    ASSERT_EQ(table.add(healthyFunction("formatDate")), CS::RegisterOutcome::Ok);
    ASSERT_EQ(table.add(healthyFunction("pluralForm")), CS::RegisterOutcome::Ok);

    std::uint8_t index = 0xff;
    EXPECT_NE(table.find("pluralForm", &index), nullptr);
    EXPECT_EQ(index, 1u);
    EXPECT_EQ(table.at(index).name, "pluralForm");
    EXPECT_EQ(table.find("noSuchName", &index), nullptr);
}

/// release зовётся ровно один раз на каждую функцию, и только при разрушении
/// таблицы: реестр только пополняется, поэтому другого момента не существует.
TEST(HostTable, ReleasesEveryUserDataExactlyOnce) {
    static int released = 0;
    released = 0;
    int firstBox = 0;
    int secondBox = 0;
    {
        CS::HostTable table;
        ChupaFunction a = healthyFunction("first");
        a.user_data = &firstBox;
        a.release = [](void *) { ++released; };
        ChupaFunction b = healthyFunction("second");
        b.user_data = &secondBox;
        b.release = [](void *) { ++released; };
        ASSERT_EQ(table.add(a), CS::RegisterOutcome::Ok);
        ASSERT_EQ(table.add(b), CS::RegisterOutcome::Ok);
        EXPECT_EQ(released, 0);
    }
    EXPECT_EQ(released, 2);
}

/// Отказ не обязан звать release: коробку хост ещё держит сам и освободит её
/// на своей стороне. Позвать её здесь значило бы освободить дважды.
TEST(HostTable, RefusedRegistrationDoesNotRelease) {
    static int released = 0;
    released = 0;
    {
        CS::HostTable table;
        ChupaFunction fn = healthyFunction("count");   // имя занято билтином
        fn.release = [](void *) { ++released; };
        ASSERT_EQ(table.add(fn), CS::RegisterOutcome::NameTaken);
    }
    EXPECT_EQ(released, 0);
}
```

- [ ] **Шаг 2: убедиться, что не собирается**

Run: `cmake --build build-dbg -j8 2>&1 | head -20`
Expected: `host.hpp` не найден.

- [ ] **Шаг 3: типы в публичном заголовке**

`core/include/chupascript/chupascript.h`, новым разделом перед секцией
вычисления:

```c
/* ─── Функции хоста: типы ────────────────────────────────────────────────
 *
 * Регистрирует их chupa_register; здесь только состав описания, потому что
 * ядро включает этот заголовок и заводить в нём параллельные копии этих
 * типов значило бы держать две правды об одном. */

typedef enum ChupaFunctionFlags {
    CHUPA_FN_NONE          = 0,
    CHUPA_FN_RETURNS_VALUE = 1u << 0,  /* без него — Void (docs/semantics.md 2.2) */
    CHUPA_FN_PURE          = 1u << 1,  /* без него — вызов только в скрипте */
    CHUPA_FN_DETERMINISTIC = 1u << 2   /* задел под кэш props; пока не читается */
} ChupaFunctionFlags;

/* Без верхней границы числа аргументов — как у format. */
#define CHUPA_VARIADIC 255

/* args заимствованы и действительны только на время вызова — правило 2
 * заголовка. Пережить вызов может лишь то, что хост удержал через
 * chupa_value_retain.
 *
 * out == NULL, если функция объявлена без CHUPA_FN_RETURNS_VALUE.
 *
 * ctx закрыт: изнутри вызова на нём разрешены только chupa_make_string,
 * chupa_fail и чтение ошибки. Всё прочее отказывает с CHUPA_ERR_USAGE.
 *
 * Возврат false — отказ; перед ним коллбэк вправе позвать chupa_fail.
 * Смещение подставляет движок — узел вызова. */
typedef bool (*ChupaHostFunction)(ChupaContext *ctx,
                                  const ChupaValue *args, size_t argc,
                                  ChupaValue *CHUPA_NULLABLE out,
                                  void *CHUPA_NULLABLE user_data);

typedef struct ChupaFunction {
    const char *name;
    size_t      name_len;
    uint8_t     min_args;
    uint8_t     max_args;   /* CHUPA_VARIADIC — без верхней границы */
    uint32_t    flags;      /* ChupaFunctionFlags */
    ChupaHostFunction call;
    void       *CHUPA_NULLABLE user_data;
    /* Зовётся ровно один раз на каждую УСПЕШНО зарегистрированную функцию из
     * chupa_context_destroy. NULL — освобождать нечего. Отказавшая
     * регистрация release не зовёт: коробку хост ещё держит сам. */
    void      (*CHUPA_NULLABLE release)(void *CHUPA_NULLABLE user_data);
} ChupaFunction;
```

- [ ] **Шаг 4: `core/src/host.hpp`**

```cpp
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "builtin_id.hpp"
#include "chupascript/chupascript.h"

namespace CS {

/// One function the host registered.
///
/// LAYOUT — what a record owns and what it merely points at:
///
///   name      std::string — an owning copy. The bytes the host passed in
///             belong to the host and this table cannot assume they outlive
///             registration.
///   call      the callback, owned by the host's code segment
///   userData  the host's receiver; this record does not own it, but it
///             OWNS THE DUTY to release it — see release below
///   release   called once, from ~HostTable, on userData
struct HostFunction {
    std::string       name;
    std::uint8_t      minArgs;
    std::uint8_t      maxArgs;
    std::uint32_t     flags;
    ChupaHostFunction call;
    void             *userData;
    void            (*release)(void *);
};

/// Why a registration was refused. Ok is not a refusal.
enum class RegisterOutcome : std::uint8_t {
    Ok,
    BadName,     ///< not an identifier, or a reserved word
    NameTaken,   ///< a builtin has it, or it is already registered
    NoCallback,  ///< call == nullptr
    BadArity,    ///< minArgs > maxArgs
    BadFlags,    ///< DETERMINISTIC without PURE
    TableFull,   ///< kMaxHostFunctions already registered
};

/// The functions one Context holds.
///
/// Append-only for its whole life: a compiled unit resolved a name into an
/// index once and forever, and removal would leave that unit unusable with no
/// sign of it in the unit itself.
///
/// Lookup by name is linear. Registrations number in the tens and the lookup
/// happens once per call site at compile time, never at evaluation — the
/// sorted table plus binary search that findBuiltin uses would buy nothing
/// and would cost the stable indices that CalleeRef stores.
class HostTable {
   public:
    HostTable() = default;

    /// Calls release on every registered function exactly once. There is no
    /// other moment: the table only ever grows, so nothing is released
    /// before this.
    ~HostTable();

    HostTable(const HostTable &) = delete;
    HostTable &operator=(const HostTable &) = delete;
    HostTable(HostTable &&) = delete;
    HostTable &operator=(HostTable &&) = delete;

    /// Copies what it needs out of desc and keeps it. On any outcome other
    /// than Ok nothing is stored and desc.release is NOT called — the host
    /// still owns the box.
    RegisterOutcome add(const ChupaFunction &desc);

    /// nullptr when no function has that name. On success *index receives the
    /// number CalleeRef will carry.
    [[nodiscard]] const HostFunction *find(std::string_view name,
                                           std::uint8_t *index) const noexcept;

    /// Precondition: index < size().
    [[nodiscard]] const HostFunction &at(std::uint8_t index) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return functions_.size(); }

   private:
    std::vector<HostFunction> functions_;
};

}  // namespace CS
```

- [ ] **Шаг 5: `core/src/host.cpp`**

```cpp
#include "host.hpp"

#include <cassert>

#include "builtin.hpp"
#include "data.hpp"

namespace CS {

HostTable::~HostTable() {
    for (const HostFunction &fn : functions_) {
        if (fn.release != nullptr) { fn.release(fn.userData); }
    }
}

RegisterOutcome HostTable::add(const ChupaFunction &desc) {
    // Порядок проверок — от дешёвых к дорогим, кроме имени: оно первым,
    // потому что негодное имя чаще всего и есть ошибка хоста, и сообщить
    // хочется именно про него, а не про случайно совпавший второй изъян.
    const std::string_view name(desc.name == nullptr ? "" : desc.name,
                                desc.name == nullptr ? 0 : desc.name_len);
    if (!isGlobalName(name)) { return RegisterOutcome::BadName; }

    Builtin ignored = Builtin::Count;
    if (findBuiltin(name, &ignored)) { return RegisterOutcome::NameTaken; }

    std::uint8_t taken = 0;
    if (find(name, &taken) != nullptr) { return RegisterOutcome::NameTaken; }

    if (desc.call == nullptr) { return RegisterOutcome::NoCallback; }
    if (desc.min_args > desc.max_args) { return RegisterOutcome::BadArity; }

    const bool pure = (desc.flags & CHUPA_FN_PURE) != 0;
    const bool deterministic = (desc.flags & CHUPA_FN_DETERMINISTIC) != 0;
    if (deterministic && !pure) { return RegisterOutcome::BadFlags; }

    if (functions_.size() >= kMaxHostFunctions) {
        return RegisterOutcome::TableFull;
    }

    functions_.push_back(HostFunction{std::string(name), desc.min_args,
                                      desc.max_args, desc.flags, desc.call,
                                      desc.user_data, desc.release});
    return RegisterOutcome::Ok;
}

const HostFunction *HostTable::find(std::string_view name,
                                    std::uint8_t *index) const noexcept {
    for (std::size_t i = 0; i < functions_.size(); ++i) {
        if (functions_[i].name == name) {
            *index = static_cast<std::uint8_t>(i);
            return &functions_[i];
        }
    }
    return nullptr;
}

const HostFunction &HostTable::at(std::uint8_t index) const noexcept {
    assert(index < functions_.size());
    return functions_[index];
}

}  // namespace CS
```

> `isGlobalName` (`core/src/data.hpp`) уже отвергает и не-идентификатор, и
> зарезервированное слово: она прогоняет имя лексером и требует, чтобы оно
> целиком было одним токеном вида `Identifier`. Своей проверки здесь заводить
> не надо — вторая копия правила разошлась бы с первой.

- [ ] **Шаг 6: два списка CMake**

В `core/src/CMakeLists.txt` добавить `host.cpp` туда же, где перечислены
`builtin.cpp` и `check.cpp`. В `core/tests/CMakeLists.txt` добавить
`host_test.cpp` рядом с `builtin_test.cpp`.

- [ ] **Шаг 7: сборка и тесты**

Run: `cmake --build build-dbg -j8 && ctest --test-dir build-dbg --output-on-failure`
Expected: зелено, тестов на тринадцать больше.

- [ ] **Шаг 8: под санитайзером**

Run: `tools/asan.sh`
Expected: код 0. Проверяется `ReleasesEveryUserDataExactlyOnce`: двойной вызов
`release` здесь не поймается, а вот утечка записи таблицы — да.

- [ ] **Шаг 9: коммит**

```bash
git add core/include/chupascript/chupascript.h core/src/host.hpp core/src/host.cpp \
        core/src/CMakeLists.txt core/tests/host_test.cpp core/tests/CMakeLists.txt
git commit -m "feat: хост объявлял функции только на словах — теперь им есть где лежать"
```

---

## Задача 4: контекст владеет таблицей и знает, что идёт вычисление

**Files:**
- Modify: `core/src/context.hpp` — таблица, два признака, три метода
- Modify: `core/src/context.cpp` — постановка и снятие признака вычисления
- Test: `core/tests/context_test.cpp`

**Interfaces:**
- Consumes: `CS::HostTable`, `CS::RegisterOutcome` из задачи 3.
- Produces: `Context::registerFunction(const ChupaFunction &) -> RegisterOutcome`;
  `Context::hosts() const -> const HostTable &`;
  `Context::isEvaluating() const -> bool`.

К перечислению `RegisterOutcome` добавляются два исхода, которые может знать
только контекст: `TooLate` (уже компилировали) и `Reentrant` (зовут изнутри
коллбэка). Таблица о компиляции и о вычислении не знает и знать не должна.

- [ ] **Шаг 1: тесты**

В `core/tests/context_test.cpp`:

```cpp
/// Порядок «регистрация → компиляция» обеспечивается, а не оставляется на
/// совести хоста: check сверяет имена в момент своего вызова и полагается,
/// что состав имён после этого не меняется (docs/backlog.md B23).
TEST(ContextHostFunctions, RefusesRegistrationAfterFirstCompile) {
    CS::Context ctx;
    ChupaFunction fn = healthyFunction("formatDate");
    EXPECT_EQ(ctx.registerFunction(fn), CS::RegisterOutcome::Ok);

    CS::Expression expr;
    CS::Diagnostic diag{};
    ASSERT_EQ(ctx.compileExpression("42", &expr, &diag, 1), 0u);

    ChupaFunction late = healthyFunction("pluralForm");
    EXPECT_EQ(ctx.registerFunction(late), CS::RegisterOutcome::TooLate);
}

TEST(ContextHostFunctions, IsNotEvaluatingOutsideEvaluation) {
    CS::Context ctx;
    EXPECT_FALSE(ctx.isEvaluating());
}

/// Признак снимается и тогда, когда вычисление завершилось ошибкой: иначе
/// один неудачный кадр закрыл бы контекст навсегда.
TEST(ContextHostFunctions, EvaluatingFlagClearsAfterFailure) {
    CS::Context ctx;
    CS::Expression expr;
    CS::Diagnostic diag{};
    ASSERT_EQ(ctx.compileExpression("count(1)", &expr, &diag, 1), 0u);

    CS::Value out = CS::Value::null();
    EXPECT_FALSE(ctx.eval(expr, &out, diag));   // count требует агрегат
    EXPECT_FALSE(ctx.isEvaluating());
}
```

`healthyFunction` — тот же помощник, что в `host_test.cpp`. Вынесите его в
`core/tests/host_fixture.hpp` и включите в оба теста: копия в двух файлах
разошлась бы при первой же правке состава `ChupaFunction`.

- [ ] **Шаг 2: убедиться, что не собирается**

Run: `cmake --build build-dbg -j8 2>&1 | head -20`
Expected: `no member named 'registerFunction' in 'CS::Context'`.

- [ ] **Шаг 3: поля и методы в `context.hpp`**

К схеме раскладки в докблоке `Context` дописать:

```
///   hosts_        HostTable — the functions the host registered
///                 (host.hpp). Append-only; every release runs from
///                 ~HostTable, that is, when this Context is destroyed.
///   compiled_     true from the first compileExpression/compileScript on
///                 this Context until it is destroyed. Registration after
///                 that point is refused (docs/backlog.md B23).
///   evaluating_   true for the duration of one eval() or run(), including
///                 the host callbacks they invoke. Every door of the C API
///                 refuses while it is up.
```

Публичные методы:

```cpp
    /// Registers a host function. Refused after the first compilation on
    /// this Context, and refused from inside a callback.
    RegisterOutcome registerFunction(const ChupaFunction &desc) {
        if (evaluating_) { return RegisterOutcome::Reentrant; }
        if (compiled_)   { return RegisterOutcome::TooLate; }
        return hosts_.add(desc);
    }

    /// The functions registered here. Read by resolveCallee (callee.hpp)
    /// during compilation and by evaluation to reach the callback.
    [[nodiscard]] const HostTable &hosts() const noexcept { return hosts_; }

    /// Is a call in flight. The C API asks before doing anything on this
    /// Context: a host callback runs in the middle of a tree walk, and a
    /// write there would drain the deferred list the walk is standing on.
    [[nodiscard]] bool isEvaluating() const noexcept { return evaluating_; }
```

`compileExpression` и `compileScript` — первой строкой тела:

```cpp
        compiled_ = true;
```

Ставится до компиляции, а не после успеха: неудачная компиляция всё равно
означает, что состав имён уже кем-то прочитан, и разрешать после неё
регистрацию значило бы делать порядок зависимым от исхода.

Приватные поля рядом с `store_`:

```cpp
    HostTable hosts_;
    bool compiled_ = false;
    bool evaluating_ = false;
```

- [ ] **Шаг 4: признак вычисления в `context.cpp`**

Завести охранника рядом с определениями `eval` и `run`:

```cpp
namespace {

/// Raises the "a call is in flight" flag for one evaluation and lowers it on
/// every way out, including the failing ones.
///
/// A plain assignment at the top and bottom of eval() would leave the flag
/// raised forever on the first failure, and one bad frame would close the
/// Context for good.
class EvaluationGuard {
   public:
    explicit EvaluationGuard(bool &flag) noexcept : flag_(flag) {
        assert(!flag_ && "повторный вход в вычисление запрещён");
        flag_ = true;
    }
    ~EvaluationGuard() { flag_ = false; }
    EvaluationGuard(const EvaluationGuard &) = delete;
    EvaluationGuard &operator=(const EvaluationGuard &) = delete;

   private:
    bool &flag_;
};

}  // namespace
```

и в начале тел `Context::eval`, `Context::evalNumber`, `Context::evalBool`,
`Context::run` — после `beginOperation()`, до собственно вычисления:

```cpp
    EvaluationGuard guard(evaluating_);
```

> Если `evalNumber`/`evalBool` реализованы через `eval`, охранник нужен только
> в `eval`: два охранника на одном флаге сработали бы утверждением. Проверьте
> по коду и поставьте ровно там, где вычисление начинается один раз.

- [ ] **Шаг 5: сборка и тесты**

Run: `cmake --build build-dbg -j8 && ctest --test-dir build-dbg --output-on-failure`
Expected: зелено, тестов на три больше.

- [ ] **Шаг 6: коммит**

```bash
git add core/src/context.hpp core/src/context.cpp core/tests/context_test.cpp \
        core/tests/host_fixture.hpp core/tests/host_test.cpp
git commit -m "feat: таблица функций жила сама по себе, а порядок и повторный вход знает только контекст"
```

---

## Задача 5: `CompileEnv` — компиляция получает одну обстановку вместо двух аргументов

**Files:**
- Modify: `core/src/check.hpp`, `check.cpp` — параметр
- Modify: `core/src/compile.hpp`, `compile.cpp` — параметр
- Modify: `core/src/expression.hpp`, `expression.cpp` — параметр
- Modify: `core/src/script.hpp`, `script.cpp` — параметр
- Modify: `core/src/context.hpp` — собирает обстановку
- Modify: тесты, зовущие компиляцию напрямую: `check_test.cpp`,
  `expression_test.cpp`, `script_test.cpp`, `eval_test.cpp`

**Это чистый рефакторинг: поведение не меняется ни в одном месте.** Отдельной
задачей — потому что диффов много и они механические, а следующая задача
меняет смысл; смешав их, ревьюер не отличит одно от другого.

**Interfaces:**
- Consumes: `CS::HostTable` из задачи 3.
- Produces: `CS::CompileMode` (`Expression` | `Script`); `CS::CompileEnv`;
  `Expression::compile(std::string_view, const CompileEnv &, Expression *, Diagnostic *, std::uint32_t)`;
  `Script::compile` той же формы.

- [ ] **Шаг 1: `CompileEnv` в `check.hpp`**

```cpp
/// Which of the two modes the tree being compiled is (docs/semantics.md
/// §3.1). check needs it for one rule and one only: an impure host function
/// may be called from a script and may not be called from an expression.
enum class CompileMode : std::uint8_t { Expression, Script };

/// Everything a compilation resolves names against.
///
/// LAYOUT — three references, none owned here:
///
///   store  Store &          — global variable names → slot numbers
///   hosts  const HostTable& — host function names → table indices
///   mode   CompileMode      — which rule set applies
///
/// One parameter rather than three: check, compileExpression, Expression and
/// Script all pass the same set straight through, and every future addition
/// to it would otherwise churn four signatures again. The rejected
/// alternative was defaulted parameters — they would have let a test compile
/// without saying which mode it meant, and the mode decides whether an impure
/// call is an error.
struct CompileEnv {
    Store           &store;
    const HostTable &hosts;
    CompileMode      mode;
};
```

- [ ] **Шаг 2: протащить параметр**

Механически: везде, где сегодня передаётся `Store &store` в цепочке
`Expression::compile` → `compileExpression` → `checkAst`, передаётся
`const CompileEnv &env`, а обращения `store.` становятся `env.store.`.
`Script::compile` — то же с `CompileMode::Script`.

`Context::compileExpression`:

```cpp
    [[nodiscard]] std::uint32_t compileExpression(std::string_view source,
                                                  Expression *out,
                                                  Diagnostic *diags,
                                                  std::uint32_t capacity) {
        compiled_ = true;
        const CompileEnv env{store_, hosts_, CompileMode::Expression};
        return Expression::compile(source, env, out, diags, capacity);
    }
```

`Context::compileScript` — то же с `CompileMode::Script`.

- [ ] **Шаг 3: тесты, зовущие компиляцию напрямую**

Заведите в `core/tests/host_fixture.hpp` помощника, чтобы не повторять сборку
обстановки в каждом тесте:

```cpp
/// Обстановка компиляции для тестов, у которых хост-функций нет.
///
/// Пустая таблица живёт статически: CompileEnv держит ссылку, и временная
/// таблица умерла бы на точке с запятой.
inline CS::CompileEnv testEnv(CS::Store &store, CS::CompileMode mode) {
    static const CS::HostTable empty;
    return CS::CompileEnv{store, empty, mode};
}
```

- [ ] **Шаг 4: сборка и тесты**

Run: `cmake --build build-dbg -j8 && ctest --test-dir build-dbg --output-on-failure`
Expected: зелено, **число тестов не изменилось** — это рефакторинг. Если
изменилось, значит тест потерян: восстановите (К8).

- [ ] **Шаг 5: коммит**

```bash
git add core/src/check.hpp core/src/check.cpp core/src/compile.hpp core/src/compile.cpp \
        core/src/expression.hpp core/src/expression.cpp core/src/script.hpp core/src/script.cpp \
        core/src/context.hpp core/tests/
git commit -m "refactor: компиляция брала хранилище, а имён у неё скоро будет две таблицы и режим"
```

---

## Задача 6: `resolveCallee` и статический проход видит обе таблицы

**Files:**
- Create: `core/src/callee.hpp`, `core/src/callee.cpp`
- Modify: `core/src/CMakeLists.txt`
- Modify: `core/src/check.cpp` — `checkCall`, `requireValue`, `requireVoid`
- Test: `core/tests/check_test.cpp`

**Interfaces:**
- Consumes: `CalleeRef` (задача 1), `BuiltinInfo::pure` (задача 2), `HostTable`
  (задача 3), `CompileEnv::mode` (задача 5).
- Produces: `CS::Callee`; `CS::resolveCallee(const HostTable &, std::string_view) -> Callee`.

**Отклонение от спеки:** спека §11.3 объявляет `resolveCallee(const Context &,
std::string_view)`. Берём `const HostTable &`: имена функций в хранилище не
живут, а зависимость `check.cpp → context.hpp` завела бы статический проход в
знание о границах операций, к которым он отношения не имеет.

- [ ] **Шаг 1: тесты на новые диагностики**

В `core/tests/check_test.cpp`:

```cpp
TEST(CheckHostFunctions, ResolvesRegisteredName) {
    CS::Store store;
    CS::HostTable hosts;
    ASSERT_EQ(hosts.add(healthyFunction("formatDate")), CS::RegisterOutcome::Ok);
    const CS::CompileEnv env{store, hosts, CS::CompileMode::Expression};

    CS::Ast ast;
    CS::Diagnostic diags[4]{};
    EXPECT_EQ(compileExpressionText("formatDate(1)", ast, env, diags, 4), 0u);
}

TEST(CheckHostFunctions, UnknownNameStillReportsUnknownFunction) {
    CS::Store store;
    CS::HostTable hosts;
    const CS::CompileEnv env{store, hosts, CS::CompileMode::Expression};

    CS::Ast ast;
    CS::Diagnostic diags[4]{};
    EXPECT_EQ(compileExpressionText("noSuchFunction(1)", ast, env, diags, 4), 1u);
    EXPECT_STREQ(diags[0].message, "unknown function");
}

TEST(CheckHostFunctions, ArityIsCheckedForHostFunctions) {
    CS::Store store;
    CS::HostTable hosts;
    ASSERT_EQ(hosts.add(healthyFunction("formatDate")), CS::RegisterOutcome::Ok);
    const CS::CompileEnv env{store, hosts, CS::CompileMode::Expression};

    CS::Ast ast;
    CS::Diagnostic diags[4]{};
    EXPECT_EQ(compileExpressionText("formatDate(1, 2)", ast, env, diags, 4), 1u);
    EXPECT_STREQ(diags[0].message, "wrong number of arguments");
}

TEST(CheckHostFunctions, VariadicHostFunctionAcceptsAnyCountAboveMinimum) {
    CS::Store store;
    CS::HostTable hosts;
    ChupaFunction fn = healthyFunction("joinAll");
    fn.min_args = 1;
    fn.max_args = CHUPA_VARIADIC;
    ASSERT_EQ(hosts.add(fn), CS::RegisterOutcome::Ok);
    const CS::CompileEnv env{store, hosts, CS::CompileMode::Expression};

    CS::Ast ast;
    CS::Diagnostic diags[4]{};
    EXPECT_EQ(compileExpressionText("joinAll(1, 2, 3, 4, 5)", ast, env, diags, 4), 0u);
    EXPECT_EQ(compileExpressionText("joinAll()", ast, env, diags, 4), 1u);
}

/// Грязная функция в выражении — ошибка компиляции. Это и есть та проверка,
/// которой docs/grammar.md §6.3 раньше не требовал: он ВЫВОДИЛ чистоту из
/// «грязное не возвращает значения», а хост-функция вправе эту посылку
/// нарушить.
TEST(CheckHostFunctions, ImpureFunctionIsRefusedInAnExpression) {
    CS::Store store;
    CS::HostTable hosts;
    ChupaFunction fn = healthyFunction("track");
    fn.flags = CHUPA_FN_RETURNS_VALUE;   // возвращает значение и грязная
    ASSERT_EQ(hosts.add(fn), CS::RegisterOutcome::Ok);
    const CS::CompileEnv env{store, hosts, CS::CompileMode::Expression};

    CS::Ast ast;
    CS::Diagnostic diags[4]{};
    EXPECT_EQ(compileExpressionText("track(1)", ast, env, diags, 4), 1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Usage);
}

TEST(CheckHostFunctions, ImpureFunctionIsAllowedInAScript) {
    CS::Store store;
    CS::HostTable hosts;
    ChupaFunction fn = healthyFunction("track");
    fn.flags = CHUPA_FN_RETURNS_VALUE;
    ASSERT_EQ(hosts.add(fn), CS::RegisterOutcome::Ok);
    const CS::CompileEnv env{store, hosts, CS::CompileMode::Script};

    CS::Ast ast;
    CS::Diagnostic diags[4]{};
    EXPECT_EQ(compileScriptText("x.n = track(1);", ast, env, diags, 4), 0u);
}

/// push и pop грязные, но новая диагностика их не касается: их случай уже
/// закрыт правилом «результат Void употреблять нельзя», и вторая жалоба на
/// тот же факт удвоила бы вывод компилятора.
TEST(CheckHostFunctions, ImpureBuiltinKeepsItsOldSingleDiagnostic) {
    CS::Store store;
    CS::HostTable hosts;
    const CS::CompileEnv env{store, hosts, CS::CompileMode::Expression};

    CS::Ast ast;
    CS::Diagnostic diags[4]{};
    EXPECT_EQ(compileExpressionText("push(items, 1)", ast, env, diags, 4), 1u);
}
```

> Помощники `compileExpressionText` / `compileScriptText` — те, которыми
> `check_test.cpp` уже пользуется; после задачи 5 они принимают `CompileEnv`.
> Если их нет, соберите вызов так же, как соседние тесты в этом файле.

- [ ] **Шаг 2: убедиться, что тесты падают**

Run: `ctest --test-dir build-dbg -R Check --output-on-failure 2>&1 | tail -20`
Expected: `CheckHostFunctions.*` падают — имя не резолвится.

- [ ] **Шаг 3: `core/src/callee.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string_view>

#include "builtin_id.hpp"
#include "chupascript/chupascript.h"

namespace CS {

class HostTable;

/// What a call site resolved to, in one shape for both tables.
///
/// LAYOUT — what each field means and who fills it:
///
///   ref           kNoCallee when the name is in neither table; otherwise
///                 what goes into the Call node (builtin_id.hpp)
///   minArgs       from BuiltinInfo or from the registration
///   maxArgs       kVariadic — no upper bound
///   returnsValue  false — Void (docs/semantics.md §2.2)
///   pure          false — may not be called from an expression
///   call          the callback; set only when isHostCallee(ref)
///   userData      the host's receiver; set only when isHostCallee(ref)
///
/// One shape for both tables, and one function producing it, because
/// otherwise check.cpp and eval.cpp each grow their own "builtin or host"
/// pair of branches and the rule for what counts as a call is smeared across
/// two files.
///
/// The callback travels inside Callee rather than as an index into the
/// HostTable: the struct is then self-contained, there is no second visit to
/// the table, and the question "is this index still good" never arises.
/// check reads neither of those two fields.
struct Callee {
    CalleeRef         ref = kNoCallee;
    std::uint8_t      minArgs = 0;
    std::uint8_t      maxArgs = 0;
    bool              returnsValue = false;
    bool              pure = false;
    ChupaHostFunction call = nullptr;
    void             *userData = nullptr;
};

/// Builtins first; a name in both tables is impossible, chupa_register
/// refuses it (host.hpp).
[[nodiscard]] Callee resolveCallee(const HostTable &hosts,
                                   std::string_view name) noexcept;

/// The same for a name already resolved: evaluation has the CalleeRef in the
/// node and must not look the name up a second time.
///
/// Precondition: ref != kNoCallee.
[[nodiscard]] Callee calleeOf(const HostTable &hosts, CalleeRef ref) noexcept;

}  // namespace CS
```

- [ ] **Шаг 4: `core/src/callee.cpp`**

```cpp
#include "callee.hpp"

#include <cassert>

#include "builtin.hpp"
#include "host.hpp"

namespace CS {
namespace {

Callee fromBuiltin(Builtin id) noexcept {
    const BuiltinInfo &info = builtinInfo(id);
    Callee out;
    out.ref = calleeOfBuiltin(id);
    out.minArgs = info.minArgs;
    out.maxArgs = info.maxArgs;
    out.returnsValue = info.returnsValue;
    out.pure = info.pure;
    return out;
}

Callee fromHost(const HostFunction &fn, std::uint8_t index) noexcept {
    Callee out;
    out.ref = calleeOfHost(index);
    out.minArgs = fn.minArgs;
    out.maxArgs = fn.maxArgs;
    out.returnsValue = (fn.flags & CHUPA_FN_RETURNS_VALUE) != 0;
    out.pure = (fn.flags & CHUPA_FN_PURE) != 0;
    out.call = fn.call;
    out.userData = fn.userData;
    return out;
}

}  // namespace

Callee resolveCallee(const HostTable &hosts, std::string_view name) noexcept {
    Builtin id = Builtin::Count;
    if (findBuiltin(name, &id)) { return fromBuiltin(id); }

    std::uint8_t index = 0;
    if (const HostFunction *fn = hosts.find(name, &index)) {
        return fromHost(*fn, index);
    }
    return Callee{};
}

Callee calleeOf(const HostTable &hosts, CalleeRef ref) noexcept {
    assert(ref != kNoCallee);
    if (!isHostCallee(ref)) { return fromBuiltin(builtinOfCallee(ref)); }
    const std::uint8_t index = hostIndexOfCallee(ref);
    return fromHost(hosts.at(index), index);
}

}  // namespace CS
```

- [ ] **Шаг 5: `checkCall` через `resolveCallee`**

`core/src/check.cpp`, заменить тело `checkCall` (сохранив разбор шаблона
`format` в конце дословно):

```cpp
    void checkCall(NodeId node) {
        const Callee callee = resolveCallee(env.hosts, ast.text(node, source));
        if (callee.ref == kNoCallee) {
            report(node, ErrorCode::Name, "unknown function");
            return;
        }
        // Кладётся до проверки арности намеренно: неверное число аргументов —
        // ошибка, до вычисления такое дерево не доходит, а разрешение всё
        // равно верное, и хранить его половинчато не за что.
        ast.setCallee(node, callee.ref);

        const std::uint32_t count = ast.childCount(node);
        if (count < callee.minArgs ||
            (callee.maxArgs != kVariadic && count > callee.maxArgs)) {
            report(node, ErrorCode::Name, "wrong number of arguments");
            return;
        }

        // Грязный вызов в выражении. Спрашивается только у хост-функций: у
        // билтинов тот же факт уже закрыт правилом §6.2 «результат Void
        // употреблять нельзя», и это правило и есть доказательство §6.3.
        // Вторая жалоба на один факт удвоила бы вывод компилятора, а первым
        // сообщением осталось бы менее точное.
        if (isHostCallee(callee.ref) && !callee.pure &&
            env.mode == CompileMode::Expression) {
            report(node, ErrorCode::Usage,
                   "impure function cannot be called from an expression");
            return;
        }

        if (isHostCallee(callee.ref) ||
            builtinOfCallee(callee.ref) != Builtin::Format) {
            return;
        }
        // …дальше прежний разбор шаблона format, без изменений…
    }
```

`requireValue` и `requireVoid` — через ту же дверь:

```cpp
    void requireValue(NodeId call) {
        if (!ast.hasCallee(call)) { return; }  // уже сообщено
        if (!calleeOf(env.hosts, ast.callee(call)).returnsValue) {
            report(call, ErrorCode::Name, "builtin does not return a value");
        }
    }
```

> Текст сообщения не меняйте: «builtin does not return a value» проверяется
> существующими тестами. Уточнение формулировки под хост-функции — отдельная
> задача, и не эта.

- [ ] **Шаг 6: сборка и тесты**

Run: `cmake --build build-dbg -j8 && ctest --test-dir build-dbg --output-on-failure`
Expected: зелено, тестов на семь больше.

- [ ] **Шаг 7: коммит**

```bash
git add core/src/callee.hpp core/src/callee.cpp core/src/CMakeLists.txt \
        core/src/check.cpp core/tests/check_test.cpp
git commit -m "feat: проход знал одну таблицу функций, а имя теперь может прийти и от хоста"
```

---

## Задача 7: `Execution` получает стек аргументов и доступ к таблице

**Files:**
- Modify: `core/src/execution.hpp`
- Modify: `core/src/context.hpp` — конструирование `exec_`
- Modify: тесты, конструирующие `Execution` напрямую
- Test: `core/tests/execution_test.cpp` (создать, если такого файла нет, и
  добавить в `core/tests/CMakeLists.txt`)

**Interfaces:**
- Consumes: `HostTable` (задача 3).
- Produces: `Execution(Store &, const HostTable &)`;
  `Execution::hosts() const -> const HostTable &`;
  `Execution::hostHandle() const -> ChupaContext *`;
  `Execution::setHostHandle(ChupaContext *)`;
  `CS::ArgFrame(Execution &, std::uint32_t count)` с `operator[]`, `data()`,
  `size()`.

**Отклонение от спеки, которое она не предусмотрела.** §5 объявляет, что
коллбэк принимает `ChupaContext *`, но не говорит, откуда его берёт
вычислитель: `eval` знает только `Execution`, а `ChupaContext` — тип границы,
определённый в `c_api.cpp`. Решение: `Execution` носит этот указатель
**непрозрачным** — он объявлен вперёд, никогда не разыменовывается ядром и
только передаётся в коллбэк. Ставит его `chupa_context_create`. Отвергнутая
альтернатива — вывести `ChupaContext *` из `CS::Context *` приведением типа:
она держалась бы на совпадении раскладки двух структур, и первое же поле,
добавленное в начало `ChupaContext`, сломало бы её молча.

- [ ] **Шаг 1: тест на стек**

```cpp
/// Аргумент внешнего вызова сам может быть вызовом, и вложенный занимает
/// стек раньше, чем внешний собрал свои. Один общий буфер здесь отдал бы
/// внешнему вызову то, что записал вложенный.
TEST(ArgFrame, NestedFrameDoesNotDisturbTheOuterOne) {
    CS::Store store;
    CS::HostTable hosts;
    CS::Execution exec(store, hosts);

    CS::ArgFrame outer(exec, 2);
    outer[0] = CS::Value::number(1.0);
    {
        CS::ArgFrame inner(exec, 3);
        inner[0] = CS::Value::number(100.0);
        inner[1] = CS::Value::number(200.0);
        inner[2] = CS::Value::number(300.0);
        EXPECT_EQ(inner.size(), 3u);
    }
    outer[1] = CS::Value::number(2.0);

    EXPECT_EQ(outer.size(), 2u);
    EXPECT_EQ(outer.data()[0].numberValue(), 1.0);
    EXPECT_EQ(outer.data()[1].numberValue(), 2.0);
}

/// Вариадичность исключает любой фиксированный потолок: kMaxFixedArgs
/// рассчитан на два, а хост вправе объявить сколько угодно.
TEST(ArgFrame, HoldsMoreThanTheBuiltinCeiling) {
    CS::Store store;
    CS::HostTable hosts;
    CS::Execution exec(store, hosts);

    CS::ArgFrame frame(exec, 64);
    for (std::uint32_t i = 0; i < 64; ++i) {
        frame[i] = CS::Value::number(static_cast<double>(i));
    }
    for (std::uint32_t i = 0; i < 64; ++i) {
        EXPECT_EQ(frame.data()[i].numberValue(), static_cast<double>(i));
    }
}

TEST(ArgFrame, EmptyFrameIsUsable) {
    CS::Store store;
    CS::HostTable hosts;
    CS::Execution exec(store, hosts);
    CS::ArgFrame frame(exec, 0);
    EXPECT_EQ(frame.size(), 0u);
}
```

- [ ] **Шаг 2: убедиться, что не собирается**

Run: `cmake --build build-dbg -j8 2>&1 | head -20`
Expected: `no type named 'ArgFrame' in namespace 'CS'`.

- [ ] **Шаг 3: поля и методы `Execution`**

К схеме раскладки в докблоке дописать:

```
///   Execution::hosts_       &HostTable — the functions the host registered;
///                           owned by the Context, read to reach a callback
///   Execution::hostHandle_  ChupaContext * — passed to a callback as its
///                           first argument and never dereferenced here.
///                           Opaque on purpose: this is the C boundary's
///                           type, and the core has no business inside it.
///   Execution::argStack_    Value storage shared by every call in flight.
///                           One call owns the half-open range
///                           [base, base + count); an ArgFrame keeps that
///                           range and gives it back on destruction.
```

Объявление и члены:

```cpp
struct ChupaContext;   // непрозрачный: определён в c_api.cpp

class Execution {
   public:
    Execution(Store &store, const HostTable &hosts) noexcept
        : store_(store), hosts_(hosts) {}

    [[nodiscard]] const HostTable &hosts() const noexcept { return hosts_; }

    [[nodiscard]] ChupaContext *hostHandle() const noexcept {
        return hostHandle_;
    }
    void setHostHandle(ChupaContext *handle) noexcept { hostHandle_ = handle; }

   private:
    friend class ArgFrame;

    Store             &store_;
    const HostTable   &hosts_;
    ChupaContext      *hostHandle_ = nullptr;
    std::vector<Value> argStack_;
    // …существующие builder_ и deferred_…
};
```

- [ ] **Шаг 4: `ArgFrame`**

В том же заголовке, после `Execution`:

```cpp
/// One call's arguments, living on the Execution's shared stack.
///
/// LAYOUT — the frame owns a half-open range and nothing else:
///
///   exec_   &Execution — whose stack this range is cut from
///   base_   index of the first slot
///   count_  how many slots
///
/// Lifetime: the range is valid from construction to destruction of this
/// frame. Frames nest and unwind in order, because an argument of an outer
/// call may itself be a call.
class ArgFrame {
   public:
    ArgFrame(Execution &exec, std::uint32_t count)
        : exec_(exec),
          base_(exec.argStack_.size()),
          count_(count) {
        // Value has no public default constructor (value.hpp keeps its
        // factories closed), so resize needs the fill argument.
        exec_.argStack_.resize(base_ + count, Value::null());
    }

    ~ArgFrame() { exec_.argStack_.resize(base_, Value::null()); }

    ArgFrame(const ArgFrame &) = delete;
    ArgFrame &operator=(const ArgFrame &) = delete;

    Value &operator[](std::uint32_t i) noexcept {
        assert(i < count_);
        return exec_.argStack_[base_ + i];
    }

    /// The arguments as a contiguous block.
    ///
    /// Recomputed on every call and NEVER cached by the caller: a nested
    /// frame may have grown the vector and moved its buffer since the last
    /// time. Taking this pointer is safe exactly once — after every argument
    /// of this call has been evaluated, when no nested frame is left alive
    /// and nothing can push again, because a host callback runs on a closed
    /// context.
    [[nodiscard]] const Value *data() const noexcept {
        return exec_.argStack_.data() + base_;
    }

    [[nodiscard]] std::uint32_t size() const noexcept { return count_; }

   private:
    Execution   &exec_;
    std::size_t  base_;
    std::uint32_t count_;
};
```

- [ ] **Шаг 5: место конструирования**

`core/src/context.hpp`: `Execution exec_{store_};` становится
`Execution exec_{store_, hosts_};`. **Порядок объявления полей важен:**
`hosts_` обязан стоять до `exec_`, иначе ссылка связывается с ещё не
построенным членом. Проверьте порядок и, если нужно, переставьте с
комментарием, почему порядок значим.

Тесты, конструирующие `Execution` напрямую, получают вторым аргументом пустую
таблицу из `host_fixture.hpp`.

- [ ] **Шаг 6: сборка, тесты, санитайзер**

Run: `cmake --build build-dbg -j8 && ctest --test-dir build-dbg --output-on-failure && tools/asan.sh`
Expected: зелено, код 0, тестов на три больше.

- [ ] **Шаг 7: коммит**

```bash
git add core/src/execution.hpp core/src/context.hpp core/tests/
git commit -m "feat: буфер аргументов был рассчитан на двух, а вложенный вызов занимал бы его раньше внешнего"
```

---

## Задача 8: C API — регистрация и создание значений

**Files:**
- Modify: `core/include/chupascript/chupascript.h` — пять функций
- Modify: `core/src/c_api.cpp`
- Modify: `core/src/context.hpp` — `makeString`
- Test: `core/tests/c_api_test.cpp`

**Interfaces:**
- Consumes: `Context::registerFunction` (задача 4), `Execution::setHostHandle`
  (задача 7).
- Ставится перед задачей 9 (вычисление): её тесты пишут результат коллбэка
  через `chupa_make_*`.
- Produces: `chupa_register`, `chupa_make_null`, `chupa_make_bool`,
  `chupa_make_number`, `chupa_make_string`;
  `Context::makeString(std::string_view) -> Value`.

- [ ] **Шаг 1: тесты**

```cpp
TEST(CApiRegister, AcceptsAndRefusesThroughTheSameDoor) {
    ChupaContext *ctx = chupa_context_create();
    ChupaFunction fn = healthyFunction("formatDate");
    EXPECT_TRUE(chupa_register(ctx, &fn));

    ChupaFunction taken = healthyFunction("count");
    EXPECT_FALSE(chupa_register(ctx, &taken));
    ChupaError err;
    chupa_context_error(ctx, &err);
    EXPECT_EQ(err.code, CHUPA_ERR_NAME);

    chupa_context_destroy(ctx);
}

TEST(CApiRegister, RefusesAfterFirstCompile) {
    ChupaContext *ctx = chupa_context_create();
    ChupaExpression *e = chupa_compile_expression(ctx, "42", 2);
    ASSERT_NE(e, nullptr);

    ChupaFunction fn = healthyFunction("formatDate");
    EXPECT_FALSE(chupa_register(ctx, &fn));
    ChupaError err;
    chupa_context_error(ctx, &err);
    EXPECT_EQ(err.code, CHUPA_ERR_USAGE);

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

/// release зовётся при разрушении контекста, а не при отказе регистрации.
TEST(CApiRegister, ReleaseRunsOnContextDestroy) {
    static int released = 0;
    released = 0;
    ChupaContext *ctx = chupa_context_create();
    ChupaFunction fn = healthyFunction("formatDate");
    fn.release = [](void *) { ++released; };
    ASSERT_TRUE(chupa_register(ctx, &fn));
    EXPECT_EQ(released, 0);
    chupa_context_destroy(ctx);
    EXPECT_EQ(released, 1);
}

TEST(CApiMake, ScalarsNeedNoContext) {
    ChupaValue v{};
    chupa_make_null(&v);
    EXPECT_EQ(chupa_value_kind(&v), CHUPA_KIND_NULL);
    chupa_make_bool(&v, true);
    EXPECT_EQ(chupa_value_kind(&v), CHUPA_KIND_BOOL);
    EXPECT_TRUE(chupa_value_bool(&v));
    chupa_make_number(&v, 3.5);
    EXPECT_EQ(chupa_value_number(&v), 3.5);
}

/// Строка длиннее пятнадцати байт — коробка; короткая лежит внутри значения.
/// Проверяются обе, потому что путь у них разный.
TEST(CApiMake, StringWorksOnBothSidesOfTheInlineBoundary) {
    ChupaContext *ctx = chupa_context_create();
    const char shortText[] = "Вася";
    const char longText[] = "Длинное название карточки из ленты товаров";

    ChupaValue v{};
    ASSERT_TRUE(chupa_make_string(ctx, shortText, sizeof shortText - 1, &v));
    const char *bytes = nullptr;
    size_t len = 0;
    chupa_value_string(&v, &bytes, &len);
    EXPECT_EQ(std::string(bytes, len), shortText);

    ASSERT_TRUE(chupa_make_string(ctx, longText, sizeof longText - 1, &v));
    chupa_value_string(&v, &bytes, &len);
    EXPECT_EQ(std::string(bytes, len), longText);

    chupa_context_destroy(ctx);
}
```

- [ ] **Шаг 2: убедиться, что не собирается**

Run: `cmake --build build-dbg -j8 2>&1 | head -20`

- [ ] **Шаг 3: объявления в заголовке**

Дописать к разделу типов из задачи 3:

```c
/* Обязана быть вызвана ДО первой компиляции на этом контексте: check сверяет
 * имена в момент своего вызова и полагается, что состав имён после этого не
 * меняется. Отказ — false, код в ошибке контекста. */
CHUPA_API CHUPA_MUST_USE bool
chupa_register(ChupaContext *ctx, const ChupaFunction *fn);

/* ─── Создание значений ──────────────────────────────────────────────────
 * Нужны коллбэку, чтобы собрать результат. Первые три памяти не выделяют
 * вовсе; строка до пятнадцати байт тоже ложится внутрь значения. */

CHUPA_API void chupa_make_null  (ChupaValue *out);
CHUPA_API void chupa_make_bool  (ChupaValue *out, bool value);
CHUPA_API void chupa_make_number(ChupaValue *out, double value);

/* Байты обязаны быть корректным UTF-8 — то же обязательство хоста, что у
 * chupa_context_set_string. false — не хватило памяти, код CHUPA_ERR_MEMORY.
 *
 * Созданное значение живёт до ближайшей границы операции на ctx, как всякое
 * значение, созданное самим движком; удержать дольше — chupa_value_retain. */
CHUPA_API CHUPA_MUST_USE bool
chupa_make_string(ChupaContext *ctx, const char *bytes, size_t len,
                  ChupaValue *out);
```

- [ ] **Шаг 4: `Context::makeString`**

`core/src/context.hpp`, публичным методом:

```cpp
    /// Materialises a string the host handed over, on this Context's terms.
    ///
    /// Exists so the deferred list stays private: chupa_make_string needs to
    /// deposit the creator reference somewhere, and handing out
    /// exec_.deferred() would open every rule this class owns.
    [[nodiscard]] Value makeString(std::string_view text) {
        return CS::materialize(text, exec_.deferred());
    }
```

- [ ] **Шаг 5: реализация в `c_api.cpp`**

```cpp
bool chupa_register(ChupaContext *ctx, const ChupaFunction *fn) {
    if (ctx == nullptr || fn == nullptr) { return false; }
    const CS::RegisterOutcome outcome = ctx->cs.registerFunction(*fn);
    if (outcome == CS::RegisterOutcome::Ok) {
        ctx->clearError();
        return true;
    }
    // Ассерт вместе с отказом: код регистрации статичен и выполняется до
    // всего, поэтому разработчик увидит это на первом же запуске у себя, а
    // не пользователь на устройстве. В релизе утверждение исчезает, и
    // настоящим контрактом остаётся false.
    assert(false && "chupa_register отказал — см. код ошибки контекста");
    ctx->setError(errorFor(outcome));
    return false;
}
```

`errorFor` — свободная функция в том же анонимном пространстве имён,
переводящая девять исходов `RegisterOutcome` в пару «код, сообщение» по
таблице §8.1 спеки: семь из задачи 3 плюс `TooLate` и `Reentrant` из задачи 4,
и `Ok`, до которого она не доходит. Пишите
её `switch`ем без `default`: тогда добавленный исход не пройдёт мимо
компилятора.

```cpp
void chupa_make_null(ChupaValue *out) { *asValue(out) = CS::Value::null(); }
void chupa_make_bool(ChupaValue *out, bool v) { *asValue(out) = CS::Value::boolean(v); }
void chupa_make_number(ChupaValue *out, double v) { *asValue(out) = CS::Value::number(v); }

bool chupa_make_string(ChupaContext *ctx, const char *bytes, size_t len,
                       ChupaValue *out) {
    if (ctx == nullptr || out == nullptr) { return false; }
    *asValue(out) = ctx->cs.makeString(std::string_view(bytes == nullptr ? "" : bytes, len));
    return true;
}
```

> Имена фабрик `Value` (`Value::boolean`, `Value::number`) сверьте по
> `core/src/value.hpp` — в плане они названы по памяти, а в коде могут
> отличаться. `asValue` — тот же помощник приведения, что уже используется в
> этом файле; если его нет, заведите рядом с существующим `fromC`.

- [ ] **Шаг 6: непрозрачный указатель**

В `chupa_context_create`, сразу после создания `ChupaContext`:

```cpp
    // Коллбэк хост-функции принимает ChupaContext *, а ядро такого указателя
    // не имеет и иметь не должно: это тип границы. Он проносится насквозь.
    c->cs.setHostHandle(c);
```

`Context::setHostHandle` — однострочная переадресация в `exec_`.

- [ ] **Шаг 7: сборка, тесты, санитайзер**

Run: `cmake --build build-dbg -j8 && ctest --test-dir build-dbg --output-on-failure && tools/asan.sh`
Expected: зелено, код 0. Тесты с ожидаемым `assert` в отладочной сборке
пометьте `GTEST_SKIP()` под `#ifndef NDEBUG` либо проверяйте их только в
релизной сборке — выбор запишите в отчёте.

- [ ] **Шаг 8: коммит**

```bash
git add core/include/chupascript/chupascript.h core/src/c_api.cpp \
        core/src/context.hpp core/tests/c_api_test.cpp
git commit -m "feat: хост мог класть значения в переменные, но не мог создать ни одного"
```

---

## Задача 9: вычисление зовёт хост-функцию

**Files:**
- Modify: `core/src/eval.cpp` — ветка `NodeKind::Call`, новая функция
- Test: `core/tests/c_api_test.cpp`

**Interfaces:**
- Consumes: `ArgFrame` (задача 7), `calleeOf` (задача 6), `Execution::hosts()`
  и `hostHandle()` (задача 7), `chupa_make_*` (задача 8).
- Produces: ничего наружу; ветка внутренняя.

- [ ] **Шаг 1: тесты**

Тесты живут в `core/tests/c_api_test.cpp`, а не в `eval_test.cpp`: коллбэку
нужен непрозрачный указатель контекста, который ставит только
`chupa_context_create`, и путь целиком проходим лишь через C API.

```cpp
namespace {

double g_base = 0.0;

/// Складывает все аргументы поверх базы из user_data: через неё проверяется,
/// что получатель доезжает — коллбэк один, а получателей у него много.
bool addUp(ChupaContext *, const ChupaValue *args, size_t argc,
           ChupaValue *out, void *user_data) {
    double acc = *static_cast<double *>(user_data);
    for (size_t i = 0; i < argc; ++i) { acc += chupa_value_number(&args[i]); }
    chupa_make_number(out, acc);
    return true;
}

bool sizeOf(ChupaContext *, const ChupaValue *args, size_t, ChupaValue *out,
            void *) {
    chupa_make_number(out, static_cast<double>(chupa_array_count(&args[0])));
    return true;
}

bool makeLongString(ChupaContext *ctx, const ChupaValue *, size_t,
                    ChupaValue *out, void *) {
    static const char text[] =
        "Длинное название карточки, какое приходит с бэкенда";
    return chupa_make_string(ctx, text, sizeof text - 1, out);
}

/// Возвращает первый аргумент как есть — значение, созданное не им.
bool identity(ChupaContext *, const ChupaValue *args, size_t, ChupaValue *out,
              void *) {
    *out = args[0];
    return true;
}

bool alwaysRefuses(ChupaContext *, const ChupaValue *, size_t, ChupaValue *,
                   void *) {
    return false;
}

/// Описание с заданной арностью и коллбэком.
///
/// Кладётся в host_fixture.hpp рядом с healthyFunction, а та выражается через
/// неё: healthyFunction(name) есть described(name, 1, 1, neverCalled). Двух
/// сборщиков описания в одном файле тестов быть не должно.
ChupaFunction described(const char *name, std::uint8_t minArgs,
                        std::uint8_t maxArgs, ChupaHostFunction call,
                        void *userData = nullptr) {
    ChupaFunction fn{};
    fn.name = name;
    fn.name_len = std::char_traits<char>::length(name);
    fn.min_args = minArgs;
    fn.max_args = maxArgs;
    fn.flags = CHUPA_FN_RETURNS_VALUE | CHUPA_FN_PURE | CHUPA_FN_DETERMINISTIC;
    fn.call = call;
    fn.user_data = userData;
    return fn;
}

/// Компилирует и вычисляет одно выражение, отдавая значение наружу.
/// Единица разрушается здесь же: результат — скаляр либо значение, чьи байты
/// принадлежат не ей.
bool evalText(ChupaContext *ctx, const char *text, ChupaValue *out) {
    ChupaExpression *e = chupa_compile_expression(ctx, text, std::strlen(text));
    if (e == nullptr) { return false; }
    const bool ok = chupa_eval(ctx, e, out);
    chupa_expression_destroy(e);
    return ok;
}

}  // namespace

TEST(EvalHostCall, ArgumentsArriveInOrderAndCount) {
    ChupaContext *ctx = chupa_context_create();
    g_base = 0.0;
    ChupaFunction fn = described("addUp", 0, CHUPA_VARIADIC, addUp, &g_base);
    ASSERT_TRUE(chupa_register(ctx, &fn));

    ChupaValue out{};
    ASSERT_TRUE(evalText(ctx, "addUp(1, 2, 3)", &out));
    EXPECT_EQ(chupa_value_number(&out), 6.0);

    chupa_context_destroy(ctx);
}

TEST(EvalHostCall, UserDataReachesTheCallback) {
    ChupaContext *ctx = chupa_context_create();
    g_base = 10.0;
    ChupaFunction fn = described("addUp", 0, CHUPA_VARIADIC, addUp, &g_base);
    ASSERT_TRUE(chupa_register(ctx, &fn));

    ChupaValue out{};
    ASSERT_TRUE(evalText(ctx, "addUp(1)", &out));
    EXPECT_EQ(chupa_value_number(&out), 11.0);

    chupa_context_destroy(ctx);
}

/// Агрегат в аргументе читается теми же функциями, что и результат eval, и
/// контекста для этого не требует.
TEST(EvalHostCall, AggregateArgumentIsReadable) {
    ChupaContext *ctx = chupa_context_create();
    ChupaFunction fn = described("sizeOf", 1, 1, sizeOf);
    ASSERT_TRUE(chupa_register(ctx, &fn));
    ASSERT_TRUE(chupa_context_set_data(ctx, "items", 5, "[1,2,3,4]", 9));

    ChupaValue out{};
    ASSERT_TRUE(evalText(ctx, "sizeOf(items)", &out));
    EXPECT_EQ(chupa_value_number(&out), 4.0);

    chupa_context_destroy(ctx);
}

/// Строка длиннее пятнадцати байт — коробка, и ссылка создателя лежит в
/// списке отложенного освобождения контекста. Читается она ПОСЛЕ возврата из
/// eval: список сливается на следующей операции, а не на этой.
TEST(EvalHostCall, ReturnedStringOutlivesTheCall) {
    ChupaContext *ctx = chupa_context_create();
    ChupaFunction fn = described("makeLong", 0, 0, makeLongString);
    ASSERT_TRUE(chupa_register(ctx, &fn));

    ChupaValue out{};
    ASSERT_TRUE(evalText(ctx, "makeLong()", &out));
    const char *bytes = nullptr;
    size_t len = 0;
    chupa_value_string(&out, &bytes, &len);
    EXPECT_EQ(std::string(bytes, len),
              "Длинное название карточки, какое приходит с бэкенда");

    chupa_context_destroy(ctx);
}

/// Вернуть аргумент как есть безопасно: его удерживает ссылка, оставленная
/// вычислением подвыражения, и она лежит в том же списке.
TEST(EvalHostCall, ReturningAnArgumentUnchangedIsSafe) {
    ChupaContext *ctx = chupa_context_create();
    ChupaFunction fn = described("echoBack", 1, 1, identity);
    ASSERT_TRUE(chupa_register(ctx, &fn));
    const char *text = "Длинный текст, который не поместится внутрь значения";
    ASSERT_TRUE(chupa_context_set_string(ctx, "longText", 8, text,
                                         std::strlen(text)));

    ChupaValue out{};
    ASSERT_TRUE(evalText(ctx, "echoBack(longText)", &out));
    const char *bytes = nullptr;
    size_t len = 0;
    chupa_value_string(&out, &bytes, &len);
    EXPECT_EQ(std::string(bytes, len), text);

    chupa_context_destroy(ctx);
}

TEST(EvalHostCall, RefusalFailsTheEvaluationAtTheCallOffset) {
    ChupaContext *ctx = chupa_context_create();
    ChupaFunction fn = described("willFail", 1, 1, alwaysRefuses);
    ASSERT_TRUE(chupa_register(ctx, &fn));

    ChupaValue out{};
    EXPECT_FALSE(evalText(ctx, "1 + willFail(2)", &out));
    ChupaError err;
    chupa_context_error(ctx, &err);
    EXPECT_EQ(err.offset, 4u);   // позиция 'w' в "1 + willFail(2)"

    chupa_context_destroy(ctx);
}

/// Тот самый случай, ради которого стек: вложенный вызов занимает аргументы
/// раньше, чем внешний собрал свои.
TEST(EvalHostCall, NestedHostCallsBothGetTheirOwnArguments) {
    ChupaContext *ctx = chupa_context_create();
    g_base = 0.0;
    ChupaFunction fn = described("addUp", 0, CHUPA_VARIADIC, addUp, &g_base);
    ASSERT_TRUE(chupa_register(ctx, &fn));

    ChupaValue out{};
    ASSERT_TRUE(evalText(ctx, "addUp(addUp(1, 2), 3)", &out));
    EXPECT_EQ(chupa_value_number(&out), 6.0);

    chupa_context_destroy(ctx);
}
```

`chupa_make_number` и `chupa_make_string`, которыми пользуются коллбэки в
тестах выше, приходят из задачи 8 — она идёт перед этой именно поэтому.
Зависимость односторонняя: C API о ветке вычисления не знает.

- [ ] **Шаг 2: убедиться, что тесты падают**

Run: `ctest --test-dir build-dbg -R EvalHostCall --output-on-failure 2>&1 | tail -20`
Expected: падают — ветки нет.

- [ ] **Шаг 3: ветка в `NodeKind::Call`**

Перед существующей проверкой на `Builtin::Format`:

```cpp
            const CalleeRef ref = ast.callee(node);
            if (isHostCallee(ref)) {
                return evalHostCall(ast, source, node, exec, out, diag);
            }
            const Builtin id = builtinOfCallee(ref);
```

- [ ] **Шаг 4: `evalHostCall`**

Рядом с `evalFormat`:

```cpp
/// Calls one host function.
///
/// The arguments are evaluated left to right into a frame on the Execution's
/// stack; the callback then sees them as one contiguous block. Nothing
/// retains them: each is held by the reference its own sub-expression left in
/// the deferred list, and that list is drained at the operation boundary,
/// which is after this whole evaluation returns to the host.
bool evalHostCall(const Ast &ast, std::string_view source, NodeId node,
                  Execution &exec, Value *out, Diagnostic &diag) {
    const Callee callee = calleeOf(exec.hosts(), ast.callee(node));
    const std::uint32_t count = ast.childCount(node);

    ArgFrame frame(exec, count);
    for (std::uint32_t i = 0; i < count; ++i) {
        if (!eval(ast, source, ast.child(node, i), exec, &frame[i], diag)) {
            return false;
        }
    }

    // out передаётся только тем, кто объявил, что возвращает значение:
    // иначе Void-функция записала бы в него мусор, а вызывающий стейтмент
    // прочитал бы его как результат.
    Value produced = Value::null();
    ChupaValue *slot =
        callee.returnsValue ? reinterpret_cast<ChupaValue *>(&produced)
                            : nullptr;

    const bool ok = callee.call(
        exec.hostHandle(),
        reinterpret_cast<const ChupaValue *>(frame.data()),
        count, slot, callee.userData);

    if (!ok) {
        // Сообщение и код кладёт chupa_fail (задача 10); здесь остаётся
        // смещение, которое хост знать не может, — узел вызова.
        return failHostCall(ast, node, exec, diag);
    }
    *out = produced;
    return true;
}
```

> `reinterpret_cast` между `Value` и `ChupaValue` уже применяется в
> `c_api.cpp` и держится на `static_assert`, сверяющем размер и тривиальную
> копируемость. **Перенесите эти `static_assert` в общее место** (например, в
> `value.hpp` рядом с самим `Value`), чтобы их не оказалось две штуки в двух
> файлах, и сошлитесь на них здесь одной строкой.

`failHostCall` на этом шаге — заглушка, ставящая `ErrorCode::Usage` и
сообщение `"host function failed"`; задача 10 заменит её настоящей, читающей
то, что положил `chupa_fail`.

- [ ] **Шаг 5: сборка, тесты, санитайзер**

Run: `cmake --build build-dbg -j8 && ctest --test-dir build-dbg --output-on-failure && tools/asan.sh`
Expected: зелено, код 0.

- [ ] **Шаг 6: коммит**

```bash
git add core/src/eval.cpp core/tests/c_api_test.cpp
git commit -m "feat: имя хост-функции разрешалось, но звать её было нечем"
```

---

## Задача 10: `chupa_fail`, `CHUPA_ERR_HOST` и закрытый контекст

**Files:**
- Modify: `core/include/chupascript/chupascript.h` — код ошибки, `chupa_fail`,
  ослабление контракта `ChupaError.message`
- Modify: `core/src/c_api.cpp` — буфер сообщения, стражи на каждой двери
- Modify: `core/src/context.hpp` — буфер отказа хоста
- Modify: `core/src/eval.cpp` — `failHostCall` вместо заглушки
- Test: `core/tests/c_api_test.cpp`

**Interfaces:**
- Consumes: `Context::isEvaluating()` (задача 4), заглушка `failHostCall`
  (задача 8).
- Produces: `CHUPA_ERR_HOST`; `chupa_fail`;
  `Context::setHostFailure(ErrorCode, std::string_view)`;
  `Context::takeHostFailure() -> Diagnostic`.

- [ ] **Шаг 1: тесты**

```cpp
namespace {

/// Пробует каждую закрытую дверь и запоминает, все ли отказали.
bool g_everyDoorRefused = false;

bool probesClosedDoors(ChupaContext *ctx, const ChupaValue *, size_t,
                       ChupaValue *out, void *) {
    auto refusedWithUsage = [ctx](bool ok) {
        if (ok) { return false; }
        ChupaError err;
        chupa_context_error(ctx, &err);
        return err.code == CHUPA_ERR_USAGE;
    };

    ChupaFunction late{};
    late.name = "tooLate";
    late.name_len = 7;
    late.call = probesClosedDoors;

    g_everyDoorRefused =
        refusedWithUsage(chupa_context_set_number(ctx, "x", 1, 1.0)) &&
        refusedWithUsage(chupa_compile_expression(ctx, "1", 1) != nullptr) &&
        refusedWithUsage(chupa_register(ctx, &late));

    chupa_make_number(out, 0.0);
    return true;
}

/// Читать значения изнутри коллбэка можно: чтение контекста не касается.
bool readsItsArgument(ChupaContext *, const ChupaValue *args, size_t,
                      ChupaValue *out, void *) {
    const bool isArray = chupa_value_kind(&args[0]) == CHUPA_KIND_ARRAY;
    chupa_make_number(out, isArray ? static_cast<double>(chupa_array_count(&args[0]))
                                   : -1.0);
    return true;
}

bool failsWithReason(ChupaContext *ctx, const ChupaValue *, size_t,
                     ChupaValue *, void *) {
    // Сообщение собирается на стеке: chupa_fail копирует байты немедленно,
    // поэтому буфер дальше не нужен.
    char message[64];
    std::snprintf(message, sizeof message, "нет такой локали: %s", "xx_YY");
    chupa_fail(ctx, CHUPA_ERR_TYPE, message, std::strlen(message));
    return false;
}

bool failsSilently(ChupaContext *, const ChupaValue *, size_t, ChupaValue *,
                   void *) {
    return false;
}

}  // namespace

/// Каждая запрещённая дверь отказывает, и вычисление после этого доходит до
/// конца корректно: отказ — это отказ, а не порча состояния.
TEST(CApiClosedContext, EveryWriteDoorRefusesFromInsideACallback) {
    ChupaContext *ctx = chupa_context_create();
    g_everyDoorRefused = false;
    ChupaFunction fn = described("probe", 0, 0, probesClosedDoors);
    ASSERT_TRUE(chupa_register(ctx, &fn));

    ChupaValue out{};
    EXPECT_TRUE(evalText(ctx, "probe()", &out));
    EXPECT_TRUE(g_everyDoorRefused);

    chupa_context_destroy(ctx);
}

TEST(CApiClosedContext, ReadingValuesFromInsideACallbackIsAllowed) {
    ChupaContext *ctx = chupa_context_create();
    ChupaFunction fn = described("readIt", 1, 1, readsItsArgument);
    ASSERT_TRUE(chupa_register(ctx, &fn));
    ASSERT_TRUE(chupa_context_set_data(ctx, "items", 5, "[1,2,3]", 7));

    ChupaValue out{};
    ASSERT_TRUE(evalText(ctx, "readIt(items)", &out));
    EXPECT_EQ(chupa_value_number(&out), 3.0);

    chupa_context_destroy(ctx);
}

/// Байты сообщения копируются немедленно, поэтому буфер вызывающего дальше не
/// нужен — а код берётся тот, что задал хост, а не общий.
TEST(CApiHostFailure, FailMessageAndCodeReachTheHostVerbatim) {
    ChupaContext *ctx = chupa_context_create();
    ChupaFunction fn = described("pickLocale", 0, 0, failsWithReason);
    ASSERT_TRUE(chupa_register(ctx, &fn));

    ChupaValue out{};
    EXPECT_FALSE(evalText(ctx, "pickLocale()", &out));
    ChupaError err;
    chupa_context_error(ctx, &err);
    EXPECT_EQ(err.code, CHUPA_ERR_TYPE);
    EXPECT_EQ(std::string(err.message, err.message_len),
              "нет такой локали: xx_YY");

    chupa_context_destroy(ctx);
}

TEST(CApiHostFailure, RefusalWithoutFailGetsErrHost) {
    ChupaContext *ctx = chupa_context_create();
    ChupaFunction fn = described("quiet", 0, 0, failsSilently);
    ASSERT_TRUE(chupa_register(ctx, &fn));

    ChupaValue out{};
    EXPECT_FALSE(evalText(ctx, "quiet()", &out));
    ChupaError err;
    chupa_context_error(ctx, &err);
    EXPECT_EQ(err.code, CHUPA_ERR_HOST);

    chupa_context_destroy(ctx);
}

/// Причина одного отказа не должна достаться следующему: takeHostFailure
/// сбрасывает поля.
TEST(CApiHostFailure, ReasonDoesNotLeakIntoTheNextRefusal) {
    ChupaContext *ctx = chupa_context_create();
    ChupaFunction loud = described("pickLocale", 0, 0, failsWithReason);
    ChupaFunction quiet = described("quiet", 0, 0, failsSilently);
    ASSERT_TRUE(chupa_register(ctx, &loud));
    ASSERT_TRUE(chupa_register(ctx, &quiet));

    ChupaValue out{};
    EXPECT_FALSE(evalText(ctx, "pickLocale()", &out));
    EXPECT_FALSE(evalText(ctx, "quiet()", &out));
    ChupaError err;
    chupa_context_error(ctx, &err);
    EXPECT_EQ(err.code, CHUPA_ERR_HOST);
    EXPECT_EQ(std::string(err.message, err.message_len), "host function failed");

    chupa_context_destroy(ctx);
}

- [ ] **Шаг 2: убедиться, что тесты падают**

- [ ] **Шаг 3: код ошибки и ослабление контракта**

К `ChupaErrorCode` добавить `CHUPA_ERR_HOST` **последним элементом**: значения
существующих кодов не должны сдвинуться, а `static_assert`ы в `c_api.cpp`
сверяют их с `CS::ErrorCode` — добавьте туда же `ErrorCode::Host` и парный
`static_assert`.

Блок комментария у `ChupaError` переписать:

```c
/* Reports the outcome of the last call made on ctx. code is CHUPA_ERR_NONE
 * when that call succeeded; offset and message are meaningful only when it
 * did not.
 *
 * message is valid until the NEXT call on this ctx — the same rule as every
 * borrowed value in this header (rule 1). It used to be documented as a
 * process-lifetime literal that outlived ctx itself; chupa_fail ended that,
 * because a host's reason for refusing is text the host assembles, and
 * demanding a literal would have left a Kotlin host with one static string
 * for every failure. Messages the engine itself produces are still literals;
 * that is no longer something a caller may rely on. */
```

- [ ] **Шаг 4: `chupa_fail`**

```c
/* Задаёт причину отказа. Зовётся только изнутри коллбэка хост-функции; вне
 * его — ничего не делает и ставит CHUPA_ERR_USAGE.
 *
 * Байты сообщения копируются немедленно, поэтому буфер вызывающего дальше не
 * нужен. Смещение подставляет движок — узел вызова: хост его знать не может. */
CHUPA_API void chupa_fail(ChupaContext *ctx, ChupaErrorCode code,
                          const char *msg, size_t len);
```

`Context` заводит под это два поля:

```cpp
    /// The reason the last host callback gave for refusing.
    ///
    /// LAYOUT — a code and a copy of the bytes:
    ///
    ///   hostFailureCode_  set by chupa_fail, read by failHostCall
    ///   hostFailureText_  an OWNING copy; the host's buffer is gone by the
    ///                     time anyone reads this
    ///
    /// A field of its own rather than the general error slot: the general
    /// slot is overwritten by the very failure this one is describing, and
    /// the order of the two writes would decide which message survived.
    ErrorCode   hostFailureCode_ = ErrorCode::None;
    std::string hostFailureText_;
```

и два метода — `setHostFailure(ErrorCode, std::string_view)` и
`takeHostFailure()`, второй возвращает `Diagnostic` и сбрасывает поля, чтобы
причина одного отказа не досталась следующему.

- [ ] **Шаг 5: стражи на дверях**

В `c_api.cpp` завести один помощник и позвать его первой строкой в каждой
двери, кроме `chupa_make_*`, `chupa_fail`, `chupa_context_error` и всех
`chupa_value_*` / `chupa_array_*` / `chupa_object_*`:

```cpp
/// Отказ, если на контексте прямо сейчас идёт вычисление.
///
/// Страж работает в релизе, а не только под assert: без него ошибка хоста
/// проявляется не отказом, а сливом списка отложенного освобождения посреди
/// обхода дерева — то есть тихо испорченными данными на чужом устройстве.
///
/// Перечислять, что именно опасно, значило бы поддерживать этот список верным
/// вечно; закрыто всё, что пишет, компилирует или вычисляет.
bool refuseWhileEvaluating(::ChupaContext *c) {
    if (!c->cs.isEvaluating()) { return false; }
    c->setError({CS::ErrorCode::Usage, 0,
                 "the context is closed while a host function is running"});
    return true;
}
```

Список закрываемых дверей: `chupa_context_set_data`, `chupa_context_set_bool`,
`chupa_context_set_number`, `chupa_context_set_string`,
`chupa_compile_expression`, `chupa_compile_script`, `chupa_eval`,
`chupa_eval_number`, `chupa_eval_bool`, `chupa_eval_string`, `chupa_run`,
`chupa_register`, `chupa_context_destroy`, `chupa_context_on_redraw`.

> `chupa_context_destroy` изнутри коллбэка — разрушение контекста, по стеку
> которого прямо сейчас идёт вычисление. Отказ здесь означает утечку, если
> хост так поступил, — и это лучше, чем разрушение из-под себя.

- [ ] **Шаг 6: `failHostCall` по-настоящему**

Заглушка из задачи 8 заменяется чтением того, что положил `chupa_fail`:
взять `Diagnostic` через `takeHostFailure()`, подставить смещение узла вызова,
а при пустой причине — `ErrorCode::Host` и `"host function failed"`.

> Ядру для этого нужен доступ к `Context`, которого у `eval` нет. Возьмите
> причину тем же путём, каким берётся `hostHandle`: положите в `Execution`
> два поля отказа и переадресуйте `Context::setHostFailure` в них. Тогда
> `eval` читает у `Execution`, а `c_api.cpp` пишет через `Context`, и
> непрозрачный указатель для этого не разыменовывается.

- [ ] **Шаг 7: сборка, тесты, санитайзер**

Run: `cmake --build build-dbg -j8 && ctest --test-dir build-dbg --output-on-failure && tools/asan.sh`

- [ ] **Шаг 8: коммит**

```bash
git add core/include/chupascript/chupascript.h core/src/c_api.cpp \
        core/src/context.hpp core/src/execution.hpp core/src/eval.cpp \
        core/tests/c_api_test.cpp
git commit -m "feat: отказ хост-функции был безымянным, а контекст во время её вызова — открытым настежь"
```

---

## Задача 11: обвязка Swift

**Files:**
- Create: `Sources/ChupaScript/HostFunction.swift`
- Modify: `Sources/ChupaScript/Error.swift` — публичный почленный инициализатор
- Modify: `Sources/ChupaScript/Context.swift` — ничего, кроме включения нового
  файла в сборку, если список файлов задан явно
- Test: `Tests/ChupaScriptTests/HostFunctionTests.swift`

**Interfaces:**
- Consumes: `chupa_register`, `chupa_make_*`, `chupa_fail` (задачи 9, 10).
- Produces: `ChupaScript.FunctionFlags`; `ChupaScript.CSConvertible`;
  перегрузки `Context.register` на арности 0…4 и сырую.

**Новый протокол, а не расширение `CSValue`.** Существующий `CSValue`
(`Sources/ChupaScript/CSValue.swift`) отвечает на вопрос «как достать себя из
вычисленного `Expression`» — у него на входе выражение, а не значение.
Хост-функции нужен перевод `ChupaValue` ⇄ Swift в обе стороны, и это другой
вопрос. Отвергнутая альтернатива — дописать два метода в `CSValue`: тогда
всякий тип, участвующий в `Expression`, обязан был бы уметь и обратный перевод,
которого от него никто не просит.

- [ ] **Шаг 1: тесты**

```swift
func testTypedClosureReceivesTypedArguments() throws {
    let context = Context()
    try context.register("addUp") { (a: Double, b: Double) -> Double in a + b }
    let expression: Expression<Double> = try context.compile(expression: "addUp(2, 3)")
    XCTAssertEqual(try expression.eval(), 5)
}

func testStringArgumentAndResult() throws {
    let context = Context()
    try context.register("shout") { (text: String) -> String in text.uppercased() }
    try context.set("name", "вася")
    let expression: Expression<String> = try context.compile(expression: "shout(name)")
    XCTAssertEqual(try expression.eval(), "ВАСЯ")
}

/// Виды аргументов проверяет трамплин: движок их не сверяет (спека §10.2).
func testWrongArgumentTypeBecomesAnError() throws {
    let context = Context()
    try context.register("shout") { (text: String) -> String in text.uppercased() }
    let expression: Expression<String> = try context.compile(expression: "shout(42)")
    XCTAssertThrowsError(try expression.eval())
}

/// Текст брошенной ошибки доезжает до хоста целиком, потому что chupa_fail
/// копирует байты.
func testThrownErrorKeepsItsMessage() throws {
    struct Boom: Swift.Error {}
    let context = Context()
    try context.register("boom") { (_: Double) -> Double in throw Boom() }
    let expression: Expression<Double> = try context.compile(expression: "boom(1)")
    XCTAssertThrowsError(try expression.eval()) { error in
        XCTAssertTrue("\(error)".contains("Boom"))
    }
}

/// Грязная функция в выражении отвергается компиляцией, а в скрипте нет.
func testImpureFunctionIsRefusedInAnExpression() throws {
    let context = Context()
    try context.register("track", flags: [.returnsValue]) { (_: Double) -> Double in 0 }
    XCTAssertThrowsError(try context.compile(expression: "track(1)") as Expression<Double>)
}

/// Замыкание не переживает контекст и не течёт: release снимает удержание.
func testClosureIsReleasedWithTheContext() throws {
    final class Witness { static var alive = 0; init() { Witness.alive += 1 }
                          deinit { Witness.alive -= 1 } }
    XCTAssertEqual(Witness.alive, 0)
    do {
        let witness = Witness()
        let context = Context()
        try context.register("keep") { (_: Double) -> Double in
            _ = witness
            return 0
        }
        XCTAssertEqual(Witness.alive, 1)
        _ = context
    }
    XCTAssertEqual(Witness.alive, 0)
}
```

> В репозитории Swift-тесты уже есть (26 штук). Если целевой файл называется
> иначе, положите тесты рядом с существующими, а не заводите второй каталог.

- [ ] **Шаг 2: убедиться, что не собирается**

Run: `swift build 2>&1 | head -20`

- [ ] **Шаг 3: `FunctionFlags` и `CSConvertible`**

```swift
public struct FunctionFlags: OptionSet, Sendable {
    public let rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }

    public static let returnsValue  = FunctionFlags(rawValue: 1 << 0)
    public static let pure          = FunctionFlags(rawValue: 1 << 1)
    public static let deterministic = FunctionFlags(rawValue: 1 << 2)
}

/// Перевод одного значения через C-границу в обе стороны.
public protocol CSConvertible {
    /// nil — значение не того вида. Проверку видов движок не делает
    /// (спека §10.2), поэтому она здесь.
    static func fromChupa(_ value: ChupaValue) -> Self?

    /// false — создать значение не удалось; ошибка уже в контексте.
    func intoChupa(_ handle: OpaquePointer,
                   _ out: UnsafeMutablePointer<ChupaValue>) -> Bool
}
```

Соответствия для `Double`, `Bool`, `String` и `Optional where Wrapped:
CSConvertible`. У `Optional` `fromChupa` даёт `.some(nil)` на `CHUPA_KIND_NULL`
— иначе `Double?` в аргументе был бы неотличим от несовпадения типа.

- [ ] **Шаг 4: коробка и трамплин**

```swift
/// Что живёт между регистрацией и разрушением контекста.
///
/// LAYOUT:
///   body     замыкание, вызываемое трамплином
///   thrown   ошибка последнего отказа; трамплин кладёт, Context достаёт
///
/// Коробка передаётся в user_data через passRetained, а release снимает
/// удержание. Массив коробок полем Context не нужен: владение выражено самим
/// дескриптором, а не тем, что кто-то не забыл сложить коробку в поле.
final class HostBox {
    let body: (UnsafePointer<ChupaValue>?, Int, UnsafeMutablePointer<ChupaValue>?) throws -> Void
    init(body: @escaping ...) { self.body = body }
}
```

Трамплин — свободная функция с `@convention(c)`, достающая коробку из
`user_data`, зовущая `body`, ловящая `Swift.Error` и переводящая её в
`chupa_fail` + `false`.

> Ловить надо `Swift.Error`, а не `Error`: внутри модуля `Error` — собственный
> тип обвязки, затеняющий протокол.

- [ ] **Шаг 5: перегрузки `register`**

По одной на арность 0…4 плюс сырая. Тело у всех одно: собрать `ChupaFunction`,
завернуть замыкание в `HostBox`, позвать `chupa_register`, бросить при отказе.
Различаются только разбором аргументов, поэтому общую часть выносите в один
приватный метод, принимающий уже готовое `body`.

```swift
extension Context {
    public func register<A: CSConvertible, R: CSConvertible>(
        _ name: String,
        flags: FunctionFlags = [.returnsValue, .pure, .deterministic],
        _ body: @escaping (A) throws -> R
    ) throws {
        try registerRaw(name, minArgs: 1, maxArgs: 1, flags: flags) { args, _, out in
            guard let a = A.fromChupa(args[0]) else {
                throw Error(code: .type, message: "\(name): аргумент 1 не \(A.self)", offset: nil)
            }
            _ = try body(a).intoChupa(self.handle, out!)
        }
    }
}
```

> Умолчание флагов здесь — «чистая, детерминированная, со значением», хотя в C
> ноль означает обратное. Это не рассогласование: в C ноль получается у того,
> кто поле не заполнил, а в Swift умолчание пишет автор обвязки осознанно.

- [ ] **Шаг 6: публичный инициализатор `Error`**

Почленный инициализатор `Error` сегодня внутренний; хосту требуется создавать
свою ошибку. Объявить `public init`.

- [ ] **Шаг 7: сборка и тесты**

Run: `swift build && swift test`
Expected: зелено, Swift-тестов на шесть больше.

- [ ] **Шаг 8: коммит**

```bash
git add Sources/ChupaScript/HostFunction.swift Sources/ChupaScript/Error.swift Tests/
git commit -m "feat: C API умел звать функции хоста, а прикладной код видел бы для этого ChupaValue"
```

---

## Задача 12: документы

**Files:**
- Modify: `docs/semantics.md` §10
- Modify: `docs/grammar.md` §6.3
- Modify: `docs/backlog.md` — B23, B29, B31, B34, новые пункты

- [ ] **Шаг 1: `docs/semantics.md` §10**

Дописать определение хост-функции; правило «наружу только скаляр, внутрь любое
значение»; смысл `pure` и `deterministic` таблицей из §6 спеки; правило «грязная
только в скрипте»; порядок «регистрация → компиляция → вычисление».

- [ ] **Шаг 2: `docs/grammar.md` §6.3**

Абзац «Отдельной проверки для этого не требуется…» заменить на:

```markdown
Для билтинов отдельной проверки не требуется: билтин, изменяющий данные, не
возвращает значения, а использование результата `Void`-билтина запрещено
правилом §6.2; присваивания же недоступны в режиме `Expression` грамматически
(§5.2).

Для функций хоста этот вывод неверен: хост вправе объявить функцию, которая и
меняет состояние, и возвращает значение. Поэтому у них чистота **объявляется**
при регистрации и **проверяется** статическим проходом: вызов функции, не
объявленной чистой, в дереве режима `Expression` — ошибка компиляции.

Свойство оставляет за хостом право кэшировать значения props, вычислять их
лениво, вне порядка обхода дерева и пропускать для невидимых вьюх. Для
билтинов оно держится по устройству, для функций хоста — по объявлению, и
движком не проверяется: `now()` от честного форматирования неотличим.
```

- [ ] **Шаг 3: `docs/backlog.md`**

**B34 закрывается.** Статус меняется на «закрыт задачей функций хоста»; в тело
дописывается, что закрыт он устройством — контекст закрыт на время вызова, — а
не аккуратностью каждого места, и что исходная формулировка про переезд пулов
потеряла предмет вместе с пулами.

**B23** — отметка, что для имён функций порядок обеспечен признаком
«уже компилировали», а для корней остаётся открытым.

**B29** — отметка, что `CHUPA_FN_DETERMINISTIC` заведён под него и до него не
читается.

**B31** остаётся как есть: вычислитель по-прежнему дереву доверяет, проверки
чистоты в `eval` нет.

Три новых пункта: возврат агрегатов из хост-функции; маски видов аргументов при
появлении второй обвязки; нулевое копирование строки-аргумента в обвязке Swift.

- [ ] **Шаг 4: коммит**

```bash
git add docs/semantics.md docs/grammar.md docs/backlog.md
git commit -m "docs: грамматика выводила чистоту выражения из посылки, которую хост-функция ломает"
```

---

## Задача 13: оболочка и замер

**Files:**
- Modify: `cli/main.cpp` — регистрация `echo`
- Create: `benchmarks/host_benchmark.cpp`
- Modify: `benchmarks/CMakeLists.txt`
- Test: `cli/tests/` — существующий набор

- [ ] **Шаг 1: `echo` в оболочке**

Регистрируется одна демонстрационная функция: `echo(x)`, `Void`, печатает
аргумент. Иначе потрогать механизм руками негде — оболочка единственный живой
хост в репозитории. Команды `:register` нет, регистрация из скрипта не
выражается.

- [ ] **Шаг 2: тест оболочки**

Скрипт `echo('привет');` печатает `привет`. Проверяется тем же способом, каким
проверяются прочие команды оболочки.

- [ ] **Шаг 3: замер**

```cpp
/// Хост-функция без аргументов против билтина той же формы: разница и есть
/// цена вызова через указатель плюс перевод аргументов.
///
/// Второй случай — с одной строкой на входе: там появляется коробка, и без
/// него таблица отвечала бы только на половину вопроса.
```

Два замера: `BM_Host_CallVoid` (ноль аргументов, `Void`) против
`BM_Host_Builtin_Count` и `BM_Host_CallString` (один строковый аргумент).

- [ ] **Шаг 4: снять и положить**

Run: `cmake --build build-rel -j8 && ./build-rel/benchmarks/chupascript_benchmarks --benchmark_filter=Host`

Результат положить в `docs/benchmarks/2026-08-20-host-functions.md` вместе с
именем машины и режимом сборки, как это сделано в соседних файлах.

- [ ] **Шаг 5: коммит**

```bash
git add cli/main.cpp benchmarks/host_benchmark.cpp benchmarks/CMakeLists.txt \
        cli/tests/ docs/benchmarks/2026-08-20-host-functions.md
git commit -m "feat: механизм работал, но потрогать его руками было негде и цена вызова была неизвестна"
```

---

## Отклонения от спеки

Записаны здесь, а не спрятаны в задачах: спека утверждена, и расхождения с ней
— решения плана, за которые он отвечает.

| № | спека говорит | план делает | почему |
|---|---|---|---|
| 1 | `resolveCallee(const Context &, …)` (§11.3) | `resolveCallee(const HostTable &, …)` | имена функций в хранилище не живут; зависимость `check.cpp → context.hpp` завела бы статический проход в знание о границах операций |
| 2 | — | ссылка на вызываемое ограничена 127 функциями (`kMaxHostFunctions`), отказ `TableFull` | спека не назвала потолка; он следует из одного байта в узле, а расширить узел значит заплатить четыре байта на каждый узел дерева |
| 3 | коллбэк принимает `ChupaContext *` (§5) | `Execution` носит его непрозрачным, ставит `chupa_context_create` | спека не сказала, откуда вычислитель его берёт; приведение `CS::Context *` → `ChupaContext *` держалось бы на совпадении раскладок |
| 4 | «грязная в выражении — ошибка» (§6) | проверяется **только у хост-функций** | у билтинов тот же факт уже закрыт правилом §6.2, и вторая жалоба удвоила бы вывод компилятора |
| 5 | `Execution` заводит стек аргументов (§11.5) | плюс `ArgFrame` как RAII поверх него | спека описала стек, но не назвала, кто возвращает отрезок; без RAII отрезок теряется на любом раннем возврате из вычисления аргумента |

## Покрытие спеки задачами

| раздел спеки | задачи |
|---|---|
| §4 что пересекает границу | 8, 9 |
| §5 заголовок, дескриптор, флаги одним полем | 3, 9 |
| §6 флаги, противоречие, столбцы у билтинов | 2, 3, 6 |
| §7 имена, столкновение | 3, 9 |
| §8 порядок, отказы регистрации | 3, 4, 9 |
| §9 закрытый контекст, B34 | 4, 10 |
| §10 ошибки, контракт сообщения, типы на хосте | 10, 11 |
| §11 устройство в ядре | 1, 3, 6, 7, 8 |
| §12 обвязка Swift | 11 |
| §13 тесты | входят в каждую задачу |
| §14 другие документы | 12 |
| §15 открытые вопросы | не реализуются намеренно |
