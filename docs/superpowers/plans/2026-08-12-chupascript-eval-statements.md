# ChupaScript: вычислитель, часть 3a — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Выполнять скрипты обработчиков: присваивание, составное присваивание и режим скрипта.

**Architecture:** Отдельной машинерии для левого значения не нужно — агрегаты это хендлы. Присваивание вычисляет всё, кроме последнего сегмента цели, получает хендл контейнера и пишет в него через `Context`. Составное присваивание есть `x = x op e`, где операция берётся из `applyBinary`, а подвыражения цели вычисляются по одному разу, потому что чтение и запись идут по одному и тому же хендлу.

**Tech Stack:** C++17, gtest, Google Benchmark, CMake. У самой библиотеки зависимостей нет.

**Спека:** `docs/superpowers/specs/2026-08-12-chupascript-eval-statements-design.md` — нормативна для этого плана.
**Семантика:** `docs/semantics.md` §2.3 (ссылочность), §3.1 (два режима), §6.1 и §6.2 (доступ), §6.3 (чтение у null), §7 (имена и присваивание).
**Грамматика:** `docs/grammar.md` §5.2 (стейтменты), §6.4 (однократность цели).

## Global Constraints

- **C++17.** Стандарт задан в корневом `CMakeLists.txt`.
- **Комментарии — по-русски.** Сообщения диагностики — по-английски.
- **`core/src/parser.*`, `core/src/lexer.*`, `core/src/ast.*` и `core/src/operator.*` не меняются ни одной строкой.**
- **Порядок вычисления:** подвыражения цели, затем правая часть, затем чтение и применение для составного, затем запись (`docs/semantics.md` §7.2).
- **Запись в `null` — ошибка,** хотя чтение у `null` даёт `null`: мягкость §6.3 распространяется только на чтение.
- **Запись за границу массива — ошибка `Range`,** хотя чтение за границей даёт `null`: расширяет только `push` (§6.1).
- **`arr[5] += 1` за границей даёт `Type`, а не `Range`:** по эквивалентности §7.3 сначала читается `null`, потом складывается.
- **Цель-имя отвергается** ошибкой `Name`: имя — входной слот от хоста, а не переменная скрипта.
- **Однократность вычисления цели ненаблюдаема** и тестом на поведение не проверяется — выражения чисты. Держится устройством кода.
- **Ошибка в скрипте не откатывается:** сделанное остаётся сделанным.
- Первая ошибка выигрывает; смещение — от узла, на котором остановились.
- **Сборка тестов:** `cmake -B build && cmake --build build -j`, прогон `ctest --test-dir build --output-on-failure`.
- **Сборка бенчмарков:** только Release — `cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON`.
- **Коммитить явными путями.** `git add -A` не использовать.

---

## Структура файлов

| Файл | Ответственность |
|---|---|
| `core/src/eval.hpp` | добавляется `runScript` |
| `core/src/eval.cpp` | добавляются `execute`, `assign`, `assignToKey`, `assignToIndex`, `compoundOperation` |
| `core/tests/eval_test.cpp` | дописывается |
| `benchmarks/eval_benchmark.cpp` | дописывается |

Отдельной единицы под стейтменты не заводится: присваивание — это обход дерева плюс запись, то есть ровно то, чем занят `eval.cpp`.

Существующие интерфейсы:

```cpp
// core/src/parser.hpp — не меняется
bool parseProgram(const char *source, std::uint32_t length, Ast &ast, Diagnostic &diag);

// core/src/ast.hpp
NodeKind kind(NodeId) const noexcept;   // Program, Assign, CallStatement, Member, Index, Identifier, …
TokenKind op(NodeId) const noexcept;    // у Assign — один из Assign, PlusAssign, MinusAssign, StarAssign, SlashAssign
std::uint32_t offset(NodeId) const noexcept;
std::uint32_t childCount(NodeId) const noexcept;
NodeId child(NodeId, std::uint32_t) const noexcept;
std::string_view text(NodeId) const noexcept;
NodeId root() const noexcept;

// core/src/context.hpp
Value objectGet(Value, std::string_view) const noexcept;
void objectSet(Value, std::string_view, Value);
std::uint32_t arrayCount(Value) const noexcept;
bool arraySet(Value, std::uint32_t, Value) noexcept;   // false за границей

// core/src/operator.hpp
bool applyBinary(TokenKind op, Value lhs, Value rhs, const Context &ctx,
                 std::uint32_t offset, Value *out, Diagnostic &diag);

// core/src/eval.cpp, внутренние — уже есть
bool eval(const Ast &, NodeId, Context &, Value *, Diagnostic &);
bool fail(const Ast &, NodeId, ErrorCode, const char *, Diagnostic &);
bool readIndex(const Ast &, NodeId, Context &, Value array, Value subscript, Value *, Diagnostic &);
bool coerceToString(const Ast &, NodeId, Context &, Value, char *numberBuffer,
                    std::string_view *, Diagnostic &);

// core/src/text.hpp
inline constexpr std::size_t kNumberBufferSize;
```

Узлы: у `Assign` два ребёнка — цель и значение; `op` несёт вид присваивания. У `Program` дети — стейтменты. Пустой стейтмент `;` узла не порождает: парсер его пропускает.

---

## Задача 1: Режим скрипта и присваивание по имени поля

**Files:**
- Modify: `core/src/eval.hpp`, `core/src/eval.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `eval`, `fail`; `parseProgram`; `Context::objectGet`, `objectSet`.
- Produces: `bool CS::runScript(const Ast &ast, Context &ctx, Diagnostic &diag)`; внутренние `execute`, `assign`, `assignToKey`. Задачи 2 и 3 расширяют `assign` и добавляют `assignToIndex` и `compoundOperation`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
/// Разбирает и выполняет скрипт; требует успеха обоих шагов.
void run(Context &ctx, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    ASSERT_TRUE(CS::parseProgram(text.data(),
                                 static_cast<std::uint32_t>(text.size()), ast,
                                 diag))
        << diag.message;
    ASSERT_TRUE(CS::runScript(ast, ctx, diag)) << diag.message;
}

/// Разбирает успешно, выполняет с отказом; возвращает диагностику выполнения.
Diagnostic runError(Context &ctx, std::string_view text) {
    Ast ast;
    Diagnostic diag;
    EXPECT_TRUE(CS::parseProgram(text.data(),
                                 static_cast<std::uint32_t>(text.size()), ast,
                                 diag))
        << diag.message;
    EXPECT_FALSE(CS::runScript(ast, ctx, diag));
    return diag;
}

TEST(EvalAssign, ExistingKeyIsReplaced) {
    Context ctx;
    put(ctx, "state", "{'count': 1}");
    run(ctx, "state.count = 42;");
    EXPECT_EQ(evaluate(ctx, "state.count").numberValue(), 42.0);
}

TEST(EvalAssign, MissingKeyIsCreated) {
    Context ctx;
    put(ctx, "state", "{}");
    // docs/semantics.md §6.2: запись создаёт ключ, если его нет.
    run(ctx, "state.fresh = 'значение';");
    EXPECT_EQ(ctx.string(evaluate(ctx, "state.fresh")), "значение");
}

TEST(EvalAssign, ValueMayBeAnyExpression) {
    Context ctx;
    put(ctx, "state", "{'a': 2, 'b': 3}");
    run(ctx, "state.sum = state.a * state.b + 1;");
    EXPECT_EQ(evaluate(ctx, "state.sum").numberValue(), 7.0);
}

TEST(EvalAssign, DeepPathIsWritable) {
    Context ctx;
    put(ctx, "user", "{'profile': {'city': {}}}");
    run(ctx, "user.profile.city.name = 'Москва';");
    EXPECT_EQ(ctx.string(evaluate(ctx, "user.profile.city.name")), "Москва");
}

TEST(EvalAssign, WritingIntoNullIsAnError) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // docs/semantics.md §7.2: мягкость §6.3 распространяется только на чтение.
    // Обе половины обязательны: без второй правило вырождается.
    EXPECT_EQ(evaluate(ctx, "user.profile.name").kind(), Value::Kind::Null);
    EXPECT_EQ(runError(ctx, "user.profile.name = 'Вася';").code,
              CS::ErrorCode::Type);
}

TEST(EvalAssign, WritingAKeyOffANonObjectIsAnError) {
    Context ctx;
    put(ctx, "count", "3");
    put(ctx, "items", "[1]");
    EXPECT_EQ(runError(ctx, "count.x = 1;").code, CS::ErrorCode::Type);
    EXPECT_EQ(runError(ctx, "items.x = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssign, AssigningToANameIsAnError) {
    Context ctx;
    put(ctx, "state", "{'a': 1}");
    // Имя — входной слот от хоста, а не переменная скрипта: состав имён
    // программе неподвластен (§7.1), а замена значения целиком порвала бы
    // алиасы, которые §2.3 обещает наблюдаемыми.
    EXPECT_EQ(runError(ctx, "state = 1;").code, CS::ErrorCode::Name);
    // А путь внутрь — работает.
    run(ctx, "state.a = 2;");
    EXPECT_EQ(evaluate(ctx, "state.a").numberValue(), 2.0);
}

TEST(EvalAssign, UnknownNameIsAnError) {
    Context ctx;
    EXPECT_EQ(runError(ctx, "usre.a = 1;").code, CS::ErrorCode::Name);
}

TEST(EvalAssign, ErrorInTheValueLeavesTheTargetUntouched) {
    Context ctx;
    put(ctx, "state", "{'a': 1}");
    EXPECT_EQ(runError(ctx, "state.a = usre;").code, CS::ErrorCode::Name);
    EXPECT_EQ(evaluate(ctx, "state.a").numberValue(), 1.0);
}

TEST(EvalScript, EmptyScriptSucceeds) {
    Context ctx;
    run(ctx, "");
    run(ctx, ";;;");
}

TEST(EvalScript, CallStatementIsNotSupportedYet) {
    Context ctx;
    put(ctx, "items", "[]");
    // Вызовы приходят с частью 3b. Сообщение говорит про стейтмент, а не про
    // выражение: цикл по стейтментам различает виды сам.
    const Diagnostic diag = runError(ctx, "push(items, 1);");
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    EXPECT_STREQ(diag.message, "statement form is not supported");
}
```

- [ ] **Шаг 2: Убедиться, что не собирается**

Run: `cmake --build build -j`
Expected: ошибка — в пространстве имён `CS` нет `runScript`.

- [ ] **Шаг 3: Объявить `runScript` в `core/src/eval.hpp`**

После объявления `evalExpression`:

```cpp
/// Выполняет разобранный скрипт: последовательность стейтментов.
///
/// Дерево обязано быть построено parseProgram **успешно**: у неудачного разбора
/// корень равен kNoNode. Буфер исходника обязан пережить выполнение — имена и
/// содержимое литералов в дереве это его срезы.
///
/// Значения нет: результат это признак успеха (docs/semantics.md §3.1). Ошибка
/// прерывает выполнение посередине, и сделанное остаётся сделанным — откатывать
/// нечего, предыдущих состояний хранилище не держит.
bool runScript(const Ast &ast, Context &ctx, Diagnostic &diag);
```

- [ ] **Шаг 4: Написать присваивание в `core/src/eval.cpp`**

Добавить `#include "operator.hpp"` — он уже есть с части 2, проверить. В анонимное пространство имён, после `eval`, добавить:

```cpp
/// Присваивание по имени поля: base.k = v.
///
/// Порядок вычисления — подвыражения цели, затем правая часть
/// (docs/semantics.md §7.2).
bool assignToKey(const Ast &ast, NodeId node, NodeId target, Context &ctx,
                 Diagnostic &diag) {
    Value base = Value::null();
    if (!eval(ast, ast.child(target, 0), ctx, &base, diag)) { return false; }

    Value value = Value::null();
    if (!eval(ast, ast.child(node, 1), ctx, &value, diag)) { return false; }

    // Запись в null — ошибка: мягкость §6.3 распространяется только на чтение,
    // а молчаливо пропущенная запись потеряла бы данные без следа.
    if (base.kind() != Value::Kind::Object) {
        return fail(ast, target, ErrorCode::Type, "only objects have keys",
                    diag);
    }

    // Имя поля берётся из узла буквально, как при чтении (§6.2).
    ctx.objectSet(base, ast.text(target), value);
    return true;
}

/// Присваивание: разбирает форму цели и передаёт дальше.
bool assign(const Ast &ast, NodeId node, Context &ctx, Diagnostic &diag) {
    const NodeId target = ast.child(node, 0);
    switch (ast.kind(target)) {
        case NodeKind::Member:
            return assignToKey(ast, node, target, ctx, diag);

        case NodeKind::Identifier:
            // Имя — входной слот от хоста, а не переменная скрипта. Состав
            // имён программе неподвластен (§7.1), а замена значения целиком
            // порвала бы алиасы, которые §2.3 обещает наблюдаемыми.
            return fail(ast, target, ErrorCode::Name,
                        "cannot assign to a variable name", diag);

        default:
            // Грамматика строит целью только Identifier, Member и Index
            // (docs/grammar.md §5.2); Index приходит следующей задачей.
            return fail(ast, target, ErrorCode::Type,
                        "invalid assignment target", diag);
    }
}

/// Выполняет один стейтмент.
bool execute(const Ast &ast, NodeId node, Context &ctx, Diagnostic &diag) {
    switch (ast.kind(node)) {
        case NodeKind::Assign:
            return assign(ast, node, ctx, diag);

        default:
            // Сегодня сюда попадает только CallStatement: вызовы приходят с
            // частью 3b. Отдавать его в eval нельзя — вышло бы сообщение про
            // выражение там, где речь о стейтменте.
            return fail(ast, node, ErrorCode::Type,
                        "statement form is not supported", diag);
    }
}
```

- [ ] **Шаг 5: Написать `runScript`**

После определения `evalExpression`, вне анонимного пространства имён:

```cpp
bool runScript(const Ast &ast, Context &ctx, Diagnostic &diag) {
    assert(ast.root() != kNoNode && "дерево обязано быть разобрано успешно");
    const NodeId program = ast.root();
    assert(ast.kind(program) == NodeKind::Program &&
           "runScript ждёт дерево от parseProgram");

    const std::uint32_t count = ast.childCount(program);
    for (std::uint32_t i = 0; i < count; ++i) {
        // Ошибка прерывает выполнение, а сделанное остаётся сделанным:
        // откатывать нечего.
        if (!execute(ast, ast.child(program, i), ctx, diag)) { return false; }
    }
    return true;
}
```

- [ ] **Шаг 6: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "EvalAssign|EvalScript"`
Expected: 11 тестов PASS.

- [ ] **Шаг 7: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 386 тестов PASS (375 было + 11).

- [ ] **Шаг 8: Коммит**

```bash
git add core/src/eval.hpp core/src/eval.cpp core/tests/eval_test.cpp
git commit -m "Run scripts and assign to object keys"
```

---

## Задача 2: Присваивание по индексу

**Files:**
- Modify: `core/src/eval.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `assign`, `eval`, `fail` из задачи 1; `coerceToString`, `kNumberBufferSize`; `Context::arrayCount`, `arraySet`, `objectSet`.
- Produces: внутренняя `assignToIndex`; ветка `NodeKind::Index` в `assign`.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(EvalAssignIndex, ArrayElementIsReplaced) {
    Context ctx;
    put(ctx, "items", "[10, 20, 30]");
    run(ctx, "items[1] = 99;");
    EXPECT_EQ(evaluate(ctx, "items[1]").numberValue(), 99.0);
    EXPECT_EQ(evaluate(ctx, "items[0]").numberValue(), 10.0);
}

TEST(EvalAssignIndex, WritingBeyondTheEndIsAnError) {
    Context ctx;
    put(ctx, "items", "[10]");
    // docs/semantics.md §6.1: чтение за границей штатно, запись за границу —
    // намерение создать элемент, для чего существует push. Обе половины
    // обязательны.
    EXPECT_EQ(evaluate(ctx, "items[1]").kind(), Value::Kind::Null);
    EXPECT_EQ(runError(ctx, "items[1] = 1;").code, CS::ErrorCode::Range);
    EXPECT_EQ(runError(ctx, "items[1000000] = 1;").code, CS::ErrorCode::Range);
}

TEST(EvalAssignIndex, FractionalAndNegativeIndicesAreErrors) {
    Context ctx;
    put(ctx, "items", "[10, 20]");
    put(ctx, "minusOne", "-1");
    EXPECT_EQ(runError(ctx, "items[0.5] = 1;").code, CS::ErrorCode::Range);
    EXPECT_EQ(runError(ctx, "items[minusOne] = 1;").code, CS::ErrorCode::Range);
}

TEST(EvalAssignIndex, NonNumberArrayIndexIsAnError) {
    Context ctx;
    put(ctx, "items", "[10, 20]");
    EXPECT_EQ(runError(ctx, "items['0'] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, ObjectKeyIsWritten) {
    Context ctx;
    put(ctx, "o", "{'a': 1}");
    run(ctx, "o['a'] = 2;");
    run(ctx, "o['fresh'] = 3;");
    EXPECT_EQ(evaluate(ctx, "o.a").numberValue(), 2.0);
    EXPECT_EQ(evaluate(ctx, "o.fresh").numberValue(), 3.0);
}

TEST(EvalAssignIndex, ScalarKeysAreCoercedToString) {
    Context ctx;
    put(ctx, "o", "{}");
    // docs/semantics.md §4.1: ключ объекта — одна из трёх позиций, требующих
    // String; правила приведения те же, что при чтении.
    run(ctx, "o[0] = 'ноль';");
    run(ctx, "o[true] = 'да';");
    run(ctx, "o[null] = 'ничего';");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o['0']")), "ноль");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o['true']")), "да");
    EXPECT_EQ(ctx.string(evaluate(ctx, "o['null']")), "ничего");
}

TEST(EvalAssignIndex, AggregateKeyIsAnError) {
    Context ctx;
    put(ctx, "o", "{}");
    put(ctx, "items", "[1]");
    EXPECT_EQ(runError(ctx, "o[items] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, WritingIntoNullIsAnError) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    EXPECT_EQ(evaluate(ctx, "user.missing[0]").kind(), Value::Kind::Null);
    EXPECT_EQ(runError(ctx, "user.missing[0] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, WritingIntoANonAggregateIsAnError) {
    Context ctx;
    put(ctx, "count", "3");
    put(ctx, "name", "'Вася'");
    EXPECT_EQ(runError(ctx, "count[0] = 1;").code, CS::ErrorCode::Type);
    EXPECT_EQ(runError(ctx, "name[0] = 1;").code, CS::ErrorCode::Type);
}

TEST(EvalAssignIndex, ChainedTargetWorks) {
    Context ctx;
    put(ctx, "state", "{'rows': [{'cells': [1, 2]}]}");
    run(ctx, "state.rows[0].cells[1] = 99;");
    EXPECT_EQ(evaluate(ctx, "state.rows[0].cells[1]").numberValue(), 99.0);
}

TEST(EvalAssignIndex, SubscriptMayBeAnExpression) {
    Context ctx;
    put(ctx, "items", "[10, 20, 30]");
    put(ctx, "i", "1");
    run(ctx, "items[i + 1] = 99;");
    EXPECT_EQ(evaluate(ctx, "items[2]").numberValue(), 99.0);
}
```

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalAssignIndex`
Expected: FAIL — цель вида `Index` попадает в `default` функции `assign`.

- [ ] **Шаг 3: Написать `assignToIndex` в `core/src/eval.cpp`**

В анонимное пространство имён, после `assignToKey`:

```cpp
/// Присваивание по индексу: base[i] = v.
bool assignToIndex(const Ast &ast, NodeId node, NodeId target, Context &ctx,
                   Diagnostic &diag) {
    // Порядок: база, индекс, затем правая часть (docs/semantics.md §7.2).
    Value base = Value::null();
    if (!eval(ast, ast.child(target, 0), ctx, &base, diag)) { return false; }
    Value subscript = Value::null();
    if (!eval(ast, ast.child(target, 1), ctx, &subscript, diag)) { return false; }

    Value value = Value::null();
    if (!eval(ast, ast.child(node, 1), ctx, &value, diag)) { return false; }

    switch (base.kind()) {
        case Value::Kind::Array: {
            // Требования к индексу те же, что при чтении (§6.1).
            if (subscript.kind() != Value::Kind::Number) {
                return fail(ast, target, ErrorCode::Type,
                            "array index must be a number", diag);
            }
            const double index = subscript.numberValue();
            if (!std::isfinite(index) || index < 0.0 ||
                index != std::floor(index)) {
                return fail(ast, target, ErrorCode::Range,
                            "array index must be a non-negative integer", diag);
            }
            // Запись за границу — ошибка: расширяет только push (§6.1).
            // Сравнение в double, потому что индекс может превышать uint32.
            if (index >= static_cast<double>(ctx.arrayCount(base))) {
                return fail(ast, target, ErrorCode::Range,
                            "array index is out of bounds", diag);
            }
            // Границу проверили выше, поэтому запись не отказывает.
            static_cast<void>(
                ctx.arraySet(base, static_cast<std::uint32_t>(index), value));
            return true;
        }

        case Value::Kind::Object: {
            char buffer[kNumberBufferSize];
            std::string_view key;
            if (!coerceToString(ast, target, ctx, subscript, buffer, &key,
                                diag)) {
                return false;
            }
            ctx.objectSet(base, key, value);
            return true;
        }

        default:
            // Запись в null — ошибка, как и по имени поля.
            return fail(ast, target, ErrorCode::Type,
                        "only arrays and objects can be assigned by index",
                        diag);
    }
}
```

- [ ] **Шаг 4: Подключить в `assign`**

В `switch` функции `assign`, перед `case NodeKind::Identifier`:

```cpp
        case NodeKind::Index:
            return assignToIndex(ast, node, target, ctx, diag);
```

- [ ] **Шаг 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalAssignIndex`
Expected: 11 тестов PASS.

- [ ] **Шаг 6: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 397 тестов PASS.

- [ ] **Шаг 7: Коммит**

```bash
git add core/src/eval.cpp core/tests/eval_test.cpp
git commit -m "Assign to array elements and object keys by index"
```

---

## Задача 3: Составное присваивание

**Files:**
- Modify: `core/src/eval.cpp`, `core/tests/eval_test.cpp`

**Interfaces:**
- Consumes: `assignToKey`, `assignToIndex` из задач 1–2; `applyBinary`; `readIndex`; `Context::objectGet`.
- Produces: внутренняя `compoundOperation`; ветки составного присваивания в `assignToKey` и `assignToIndex`.

- [ ] **Шаг 1: Написать тесты**

Сначала добавить `#include <cmath>` в `core/tests/eval_test.cpp` — в блок
стандартных заголовков, перед `<cstdint>`. Его там нет, а `std::isinf` ниже
нужен.

Дописать перед закрывающим `}  // namespace`:

```cpp
TEST(EvalCompound, FourOperatorsWorkOnAKey) {
    Context ctx;
    put(ctx, "s", "{'n': 10}");
    run(ctx, "s.n += 5;");
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 15.0);
    run(ctx, "s.n -= 3;");
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 12.0);
    run(ctx, "s.n *= 2;");
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 24.0);
    run(ctx, "s.n /= 4;");
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 6.0);
}

TEST(EvalCompound, WorksOnAnArrayElement) {
    Context ctx;
    put(ctx, "items", "[1, 2, 3]");
    run(ctx, "items[1] += 10;");
    EXPECT_EQ(evaluate(ctx, "items[1]").numberValue(), 12.0);
}

TEST(EvalCompound, WorksOnAnObjectKeyByIndex) {
    Context ctx;
    put(ctx, "o", "{'a': 1}");
    run(ctx, "o['a'] += 1;");
    EXPECT_EQ(evaluate(ctx, "o.a").numberValue(), 2.0);
}

TEST(EvalCompound, TypeMismatchIsAnError) {
    Context ctx;
    put(ctx, "s", "{'text': 'а'}");
    // Операция берётся из applyBinary, поэтому правила типов те же, что у
    // обычного оператора: конкатенации строк через + нет.
    EXPECT_EQ(runError(ctx, "s.text += 'б';").code, CS::ErrorCode::Type);
}

TEST(EvalCompound, MissingKeyReadsAsNullAndThenFails) {
    Context ctx;
    put(ctx, "s", "{}");
    // Чтение отсутствующего ключа даёт null (§6.2), а null + 1 — ошибка типа.
    EXPECT_EQ(runError(ctx, "s.missing += 1;").code, CS::ErrorCode::Type);
}

TEST(EvalCompound, BeyondTheEndGivesTypeNotRange) {
    Context ctx;
    put(ctx, "items", "[1, 2]");
    // docs/semantics.md §7.3: x += e есть x = x + e. Сначала читается items[5],
    // что штатно даёт null, затем вычисляется null + 1 — ошибка типа. До
    // проверки границы записи дело не доходит, поэтому Type, а не Range.
    // Простое присваивание туда же даёт Range — обе строки обязательны.
    EXPECT_EQ(runError(ctx, "items[5] += 1;").code, CS::ErrorCode::Type);
    EXPECT_EQ(runError(ctx, "items[5] = 1;").code, CS::ErrorCode::Range);
}

TEST(EvalCompound, ErrorLeavesTheTargetUntouched) {
    Context ctx;
    put(ctx, "s", "{'n': 10}");
    EXPECT_EQ(runError(ctx, "s.n += 'а';").code, CS::ErrorCode::Type);
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 10.0);
}

TEST(EvalCompound, DivisionByZeroFollowsIEEE) {
    Context ctx;
    put(ctx, "s", "{'n': 1}");
    // Деление на ноль даёт бесконечность, а не ошибку (§5.2).
    run(ctx, "s.n /= 0;");
    EXPECT_TRUE(std::isinf(evaluate(ctx, "s.n").numberValue()));
}

TEST(EvalCompound, DeepTargetWorks) {
    Context ctx;
    put(ctx, "state", "{'rows': [{'n': 1}]}");
    put(ctx, "i", "0");
    // Однократность вычисления цели (docs/grammar.md §6.4) в этом языке
    // ненаблюдаема: выражения чисты, поэтому повторное вычисление дало бы тот
    // же результат. Тест проверяет лишь, что сложная цель вообще работает;
    // само требование держится устройством кода, а не этой проверкой.
    run(ctx, "state.rows[i].n += 41;");
    EXPECT_EQ(evaluate(ctx, "state.rows[0].n").numberValue(), 42.0);
}
```

- [ ] **Шаг 2: Убедиться, что тесты падают**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalCompound`
Expected: FAIL — составное присваивание пока записывает правую часть как есть, игнорируя прежнее значение.

- [ ] **Шаг 3: Добавить `compoundOperation` в `core/src/eval.cpp`**

В анонимное пространство имён, перед `assignToKey`:

```cpp
/// Соответствие составного оператора обычному (docs/semantics.md §7.3).
///
/// Операции %= в языке нет (docs/grammar.md §5.2).
TokenKind compoundOperation(TokenKind op) {
    switch (op) {
        case TokenKind::PlusAssign: return TokenKind::Plus;
        case TokenKind::MinusAssign: return TokenKind::Minus;
        case TokenKind::StarAssign: return TokenKind::Star;
        case TokenKind::SlashAssign: return TokenKind::Slash;
        default:
            assert(false && "не составной оператор присваивания");
            return TokenKind::Plus;
    }
}
```

- [ ] **Шаг 4: Дополнить `assignToKey`**

Заменить последние две строки (`ctx.objectSet(...)` и `return true;`) на:

```cpp
    const std::string_view key = ast.text(target);

    const TokenKind op = ast.op(node);
    if (op != TokenKind::Assign) {
        // x op= e есть x = x op e. Чтение идёт по уже вычисленной базе,
        // поэтому подвыражения цели вычислены ровно один раз (§6.4).
        const Value current = ctx.objectGet(base, key);
        Value combined = Value::null();
        if (!applyBinary(compoundOperation(op), current, value, ctx,
                         ast.offset(node), &combined, diag)) {
            return false;
        }
        value = combined;
    }

    ctx.objectSet(base, key, value);
    return true;
```

- [ ] **Шаг 5: Дополнить `assignToIndex`**

В ветке `case Value::Kind::Array`, между проверкой целочисленности индекса и проверкой границы, вставить:

```cpp
            const TokenKind op = ast.op(node);
            if (op != TokenKind::Assign) {
                // Чтение за границей штатно даёт null, поэтому items[5] += 1
                // упирается не в границу записи, а в сложение с null: Type, а
                // не Range (§7.3).
                Value current = Value::null();
                if (!readIndex(ast, target, ctx, base, subscript, &current,
                               diag)) {
                    return false;
                }
                Value combined = Value::null();
                if (!applyBinary(compoundOperation(op), current, value, ctx,
                                 ast.offset(node), &combined, diag)) {
                    return false;
                }
                value = combined;
            }
```

В ветке `case Value::Kind::Object`, между получением ключа и записью, вставить:

```cpp
            const TokenKind op = ast.op(node);
            if (op != TokenKind::Assign) {
                const Value current = ctx.objectGet(base, key);
                Value combined = Value::null();
                if (!applyBinary(compoundOperation(op), current, value, ctx,
                                 ast.offset(node), &combined, diag)) {
                    return false;
                }
                value = combined;
            }
```

Обе ветки объявляют `op` внутри своего блока: `-Wshadow` не сработает, потому что во внешней области его нет.

- [ ] **Шаг 6: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalCompound`
Expected: 9 тестов PASS.

- [ ] **Шаг 7: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 406 тестов PASS.

- [ ] **Шаг 8: Коммит**

```bash
git add core/src/eval.cpp core/tests/eval_test.cpp
git commit -m "Apply compound assignment through the same handle"
```

---

## Задача 4: Поведение скрипта

**Files:**
- Modify: `core/tests/eval_test.cpp` (только тесты)

**Interfaces:**
- Consumes: весь `runScript` из задач 1–3. Нового кода не пишется; если тест не проходит, правится `core/src/eval.cpp`.
- Produces: ничего.

- [ ] **Шаг 1: Написать тесты**

Дописать в `core/tests/eval_test.cpp` перед закрывающим `}  // namespace`:

```cpp
TEST(EvalScriptBehaviour, StatementsApplyInOrder) {
    Context ctx;
    put(ctx, "s", "{'n': 0}");
    run(ctx, "s.n = 1; s.n = 2; s.n = 3;");
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 3.0);
}

TEST(EvalScriptBehaviour, LaterStatementsSeeEarlierWrites) {
    Context ctx;
    put(ctx, "s", "{'a': 1}");
    run(ctx, "s.b = s.a + 1; s.c = s.b + 1;");
    EXPECT_EQ(evaluate(ctx, "s.c").numberValue(), 3.0);
}

TEST(EvalScriptBehaviour, ErrorStopsTheScriptAndKeepsWhatWasDone) {
    Context ctx;
    put(ctx, "s", "{'a': 0, 'b': 0, 'c': 0}");
    // docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md: откатывать
    // нечего, предыдущих состояний хранилище не держит. Обработчик, упавший на
    // третьем присваивании из пяти, оставит первые два применёнными.
    const Diagnostic diag =
        runError(ctx, "s.a = 1; s.b = 2; s.x = usre; s.c = 3;");
    EXPECT_EQ(diag.code, CS::ErrorCode::Name);
    EXPECT_EQ(evaluate(ctx, "s.a").numberValue(), 1.0);
    EXPECT_EQ(evaluate(ctx, "s.b").numberValue(), 2.0);
    EXPECT_EQ(evaluate(ctx, "s.c").numberValue(), 0.0);
    EXPECT_FALSE(ctx.objectHas(ctx.root("s"), "x"));
}

TEST(EvalScriptBehaviour, MutationIsVisibleThroughAnotherName) {
    Context ctx;
    // Хост кладёт один агрегат под двумя именами: значения — хендлы, поэтому
    // это тот же массив (docs/semantics.md §2.3).
    put(ctx, "state", "{'items': [1, 2]}");
    const Value items = ctx.objectGet(ctx.root("state"), "items");
    ctx.setRoot("shortcut", items);

    run(ctx, "state.items[0] = 99;");
    EXPECT_EQ(evaluate(ctx, "shortcut[0]").numberValue(), 99.0);
}

TEST(EvalScriptBehaviour, EmptyStatementsAreSkipped) {
    Context ctx;
    put(ctx, "s", "{'n': 0}");
    run(ctx, ";; s.n = 1 ;;");
    EXPECT_EQ(evaluate(ctx, "s.n").numberValue(), 1.0);
}

TEST(EvalScriptBehaviour, DeepPathInsideAScript) {
    Context ctx;
    put(ctx, "state", "{'rows': [{'cells': [0]}, {'cells': [0]}]}");
    run(ctx, "state.rows[0].cells[0] = 1; state.rows[1].cells[0] = 2;");
    EXPECT_EQ(evaluate(ctx, "state.rows[0].cells[0]").numberValue(), 1.0);
    EXPECT_EQ(evaluate(ctx, "state.rows[1].cells[0]").numberValue(), 2.0);
}
```

- [ ] **Шаг 2: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R EvalScriptBehaviour`
Expected: 6 тестов PASS. Если какой-то падает — правится `core/src/eval.cpp`, тест не трогается: он выражает требование спеки.

- [ ] **Шаг 3: Прогнать весь набор**

Run: `ctest --test-dir build --output-on-failure`
Expected: 412 тестов PASS.

- [ ] **Шаг 4: Прогнать под санитайзерами и с `-Werror`**

```bash
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
cmake --build build-werror -j && ctest --test-dir build-werror --output-on-failure
```

Expected: 412 PASS в обеих, ни одного отчёта санитайзера, ни одного предупреждения.

- [ ] **Шаг 5: Коммит**

```bash
git add core/tests/eval_test.cpp
git commit -m "Cover script ordering, partial application and referentiality"
```

---

## Задача 5: Бенчмарки

**Files:**
- Modify: `benchmarks/eval_benchmark.cpp`, `benchmarks/baseline.json`

**Interfaces:**
- Consumes: `runScript`, `parseProgram`, `setVariable`.
- Produces: базу для сравнения при части 3b.

- [ ] **Шаг 1: Дописать бенчмарки**

В `benchmarks/eval_benchmark.cpp`, в анонимное пространство имён перед `}  // namespace`:

```cpp
/// Общая часть для скриптов: наполнить контекст, разобрать, мерить выполнение.
///
/// Контекст создаётся заново на каждой итерации: скрипт меняет данные, и без
/// пересоздания вторая итерация работала бы уже на изменённых. Цена создания
/// входит в измерение — читать эти строки имеет смысл в сравнении друг с
/// другом, а не с BM_Eval_* для выражений.
void runScriptBench(benchmark::State &state, std::string_view source) {
    Ast ast;
    Diagnostic diag;
    if (!CS::parseProgram(source.data(),
                          static_cast<std::uint32_t>(source.size()), ast,
                          diag)) {
        state.SkipWithError("parseProgram failed");
        return;
    }

    for (auto _ : state) {
        Context ctx;
        if (!fill(ctx)) {
            state.SkipWithError("setVariable failed");
            return;
        }
        bool ok = CS::runScript(ast, ctx, diag);
        if (!ok) {
            state.SkipWithError("runScript failed");
            return;
        }
        benchmark::DoNotOptimize(ok);
    }
}

/// Присваивание в путь из двух сегментов — самая частая форма в обработчике.
void BM_Eval_Assign(benchmark::State &state) {
    runScriptBench(state, "user.name = 'Петя';");
}
BENCHMARK(BM_Eval_Assign);

/// Составное присваивание туда же: чтение, операция, запись.
void BM_Eval_CompoundAssign(benchmark::State &state) {
    runScriptBench(state, "user.profile.city.code.zip += 1;");
}
BENCHMARK(BM_Eval_CompoundAssign);

/// Скрипт из пяти присваиваний — цена обхода Program.
void BM_Eval_Script(benchmark::State &state) {
    runScriptBench(state,
                   "user.a = 1; user.b = 2; user.c = 3; user.d = 4;"
                   " user.e = 5;");
}
BENCHMARK(BM_Eval_Script);
```

- [ ] **Шаг 2: Собрать в Release и прогнать**

```bash
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks --benchmark_filter=Eval
```

Expected: тринадцать строк `BM_Eval_*` — десять прежних и три новых, — ни одной с `SkipWithError`.

Посмотреть глазами: `BM_Eval_Script` обязан быть дороже `BM_Eval_Assign` — пять присваиваний против одного. Если нет, сообщи и не записывай базу.

- [ ] **Шаг 3: Проверить, что прежние семейства не деградировали**

```bash
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_out=/tmp/stmt-current.json --benchmark_out_format=json
python3 tools/bench-compare.py benchmarks/baseline.json /tmp/stmt-current.json
```

Этап трогает `core/src/eval.cpp`, поэтому прежние `BM_Eval_*` под подозрением; `BM_Lex_*`, `BM_Parse_*`, `BM_Store_*` и `BM_Data_*` меняться не должны.

Порог различимости на этой машине около восьми процентов (`docs/backlog.md` B24), и там же записано, что однонаправленный сдвиг целого семейства — признак среды, а не кода. Выше порога — разберись до записи базы и напиши, что нашёл. Машина при замере обязана быть незанятой.

- [ ] **Шаг 4: Записать базу**

```bash
cp /tmp/stmt-current.json benchmarks/baseline.json
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
git commit -m "Record the statement performance baseline"
```

---

## Задача 6: Документы

**Files:**
- Modify: `docs/semantics.md` (§7.2), `docs/grammar.md` (§5.2), `docs/backlog.md` (B14)

**Interfaces:**
- Consumes: решения задач 1–5.
- Produces: согласованные документы.

- [ ] **Шаг 1: Добавить правило в `docs/semantics.md` §7.2**

После абзаца «Цель присваивания — путь внутри контекста» с тремя примерами, перед списком «Все подвыражения цели вычисляются…», вставить:

```markdown
**Целью не может быть само имя.** `state = {...};` разбирается грамматикой, но
отвергается: имя ставит хост, программа же меняет только содержимое агрегатов,
лежащих в контексте (§7.1).

Причина не в стилистике. Замена значения имени целиком выбросила бы агрегат, на
который могли остаться ссылки из других имён, а §2.3 обещает, что изменение
через одно имя видно через второе. После `state = {}` алиас продолжил бы
указывать на прежний массив, и данные разъехались бы тихо.
```

- [ ] **Шаг 2: Уточнить `docs/grammar.md` §5.2**

Предложение «`LeftHandSide` допускает произвольную цепочку индексов и полей: `a`, `a[0]`, `a.b`, `a.b[i].c` — все корректные цели присваивания.» заменить на:

```markdown
`LeftHandSide` допускает произвольную цепочку индексов и полей: `a[0]`, `a.b`,
`a.b[i].c`. Голое имя `a` грамматика тоже принимает, но семантика его отвергает
(`docs/semantics.md` §7.2): грамматика описывает форму, а не допустимость.
```

- [ ] **Шаг 3: Отметить в `docs/backlog.md` B14**

В пункт «B14. Глава 10 `docs/semantics.md` не написана» дописать в конец тела:

```markdown
Один из трёх частных вопросов главы закрыт: присваивание корневому имени целиком
недопустимо, и правило записано в `docs/semantics.md` §7.2 вместе с причиной.
Остаются два: что происходит со значением, отданным хостом в программу и
обратно, и переживают ли значения вызов.
```

- [ ] **Шаг 4: Проверить счёт и прогнать тесты**

Run: `grep -c "^### B" docs/backlog.md`
Expected: 25 — число пунктов не меняется.

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: 412 тестов PASS.

- [ ] **Шаг 5: Коммит**

```bash
git add docs/semantics.md docs/grammar.md docs/backlog.md
git commit -m "Record that a bare name is not an assignment target"
```

---

## Итог

| | |
|---|---|
| Задач | 6 |
| Новых файлов | 0 |
| Изменённых | `eval.hpp`, `eval.cpp`, `eval_test.cpp`, `eval_benchmark.cpp`, `baseline.json`, `semantics.md`, `grammar.md`, `backlog.md` |
| Тестов добавлено | 37 |
| Тестов всего | 412 |
| Бенчмарков добавлено | 3 |
| Строк изменено в парсере, лексере, дереве и операторах | 0 |

Часть 3a закончена, когда: `ctest` даёт 412 из 412 в обычной сборке, под ASan+UBSan и с `-Werror`; `benchmarks/baseline.json` содержит `BM_Eval_Assign`, `BM_Eval_CompoundAssign` и `BM_Eval_Script`; `docs/semantics.md` §7.2 называет правило про имя как цель.

Следующая часть — 3b: тринадцать встроенных функций, их таблица и статические проверки §6.1 и §6.2 грамматики. Она закрывает B11 и завершает вычислитель.
