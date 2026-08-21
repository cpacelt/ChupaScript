# Присваивание имени — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** снять запрет «целью присваивания не может быть само имя», чтобы скрипт мог написать `liked = true;` в глобальную переменную контекста.

**Architecture:** запрет живёт в одной ветке статического прохода `core/src/check.cpp` — она удаляется. Взамен в `Store` появляется дверь записи по номеру ячейки с подъёмом эпохи внутри, а вычислитель получает настоящую ветку для цели-`Identifier` вместо защитной. Грамматика, C API и Swift-обёртка не меняются.

**Tech Stack:** C++17, CMake, GoogleTest, `tools/asan.sh`.

**Spec:** `docs/superpowers/specs/2026-08-21-assign-to-name-design.md`

## Global Constraints

- Состав контекста остаётся неизменным: присваивание **неизвестному** имени — по-прежнему ошибка компиляции с кодом `ErrorCode::Name` и текстом `unknown name`. Ослабляется только запрет менять значение имени, не запрет заводить имя.
- Подъём эпохи стоит **внутри** мутатора, а не у вызывающего. Пропущенная точка подъёма даёт молча застывший экран, а не медленную работу (`2026-08-20-expression-cache-design.md` §4.5).
- Вытесненное значение уходит в `Deferred`, а не освобождается на месте; retain нового значения идёт **до** отпускания старого, иначе `x = x` роняет счётчик в ноль.
- Ветка `default` в `assign` (`core/src/eval.cpp`) остаётся защитной — её не удалять.
- Грамматика (`docs/grammar.md`) не меняется ни на символ.
- Ветка работы: `feat/assign-to-name`, от `feat/expression-cache`. Собранный каталог — `build-dbg`.

**Сборка и прогон тестов** (одинаково во всех задачах):

```bash
cmake --build build-dbg -j8 --target chupascript_tests
./build-dbg/core/tests/chupascript_tests --gtest_filter='<фильтр>'
```

---

### Task 1: Документы

**Files:**
- Modify: `docs/semantics.md` (§2.3, §7.1, §7.2)
- Modify: `docs/superpowers/specs/2026-08-20-expression-cache-design.md` (таблица §2.2)
- Modify: `docs/backlog.md` (B27)

**Interfaces:**
- Consumes: ничего.
- Produces: имя новой двери `Store::setGlobalAt` зафиксировано в таблице зацепок эпохи; задачи 2 и 3 обязаны назвать функцию именно так.

Это задача без тестов: код она не трогает, проверяется чтением и грепом.

- [ ] **Step 1: §7.2 — снять запрет**

В `docs/semantics.md` найти абзац, начинающийся с `**Целью не может быть само имя.**`, и следующий за ним абзац, начинающийся с `Причина не в стилистике.`. Удалить оба целиком.

Строкой выше стоит вводная фраза раздела `Цель присваивания — путь внутри контекста:` с примером. Заменить её на:

```markdown
Цель присваивания — имя либо путь внутри контекста:

```
count = 1;
state.count = 1;
state.items[0] = product;
user.profile.name = 'Вася';
```

Присваивание имени меняет **привязку имени** и ничего больше. Завести имя оно
не может: цель, которой нет в контексте, — ошибка компиляции (§7.1).
```

Остальное содержимое §7.2 — порядок вычисления, запись в `null`, границы массива, составное присваивание — не трогать: оно относится к целям-путям и остаётся дословно.

- [ ] **Step 2: §7.1 — уточнить, что именно неизменно**

В `docs/semantics.md` §7.1 найти абзац:

```markdown
Контекст неизменен по составу: программа не может добавить в него имя или
удалить существующее. Изменять она может только содержимое агрегатов, лежащих в
контексте (§7.2).
```

Заменить на:

```markdown
Контекст неизменен **по составу**: программа не может добавить в него имя или
удалить существующее. Изменять она может значения имён и содержимое агрегатов,
лежащих в контексте (§7.2).
```

- [ ] **Step 3: §2.3 — новое обещание**

В `docs/semantics.md` §2.3 найти абзац:

```markdown
Создать второе имя для агрегата средствами языка нельзя: имён язык не заводит.
Алиасы возникают только там, где их создал хост.
```

Заменить на:

```markdown
Создать второе имя для агрегата средствами языка нельзя: имён язык не заводит.
Алиасы возникают только там, где их создал хост.

Обещание касается **содержимого**, а не привязки: изменение содержимого
агрегата видно через все имена, которые на него ссылаются, а присваивание имени
(§7.2) меняет привязку этого имени и ничего больше. После `a = {}` имя `b`
продолжает смотреть на прежнюю коробку с прежним содержимым.
```

- [ ] **Step 4: таблица зацепок эпохи**

В `docs/superpowers/specs/2026-08-20-expression-cache-design.md` §2.2 в таблице «зацепка / где / кто поднимает» строка ячейки глобальной переменной сегодня называет поднимающих как `` `setGlobal`, `Assign` в имя ``. Заменить это на `` `setGlobal`, `setGlobalAt` `` — список обязан быть грепаемым по именам функций, а `Assign` в имя именем функции не является.

- [ ] **Step 5: B27**

В `docs/backlog.md` у пункта `### B27. Запрет присваивания имени решается вычислителем, а не статическим проходом` заменить строку `**Статус:** закрыт` на:

```markdown
**Статус:** отменён — запрет снят целиком
(`docs/superpowers/specs/2026-08-21-assign-to-name-design.md`). Проверка,
о переезде которой пункт говорит, удалена: цель-`Identifier` законна, и
`assign()` в `core/src/eval.cpp` разбирает её настоящей веткой, а не защитной.
```

- [ ] **Step 6: проверить греп**

Run:
```bash
grep -rn "Целью не может быть само имя" docs/ ; echo "exit=$?"
```
Expected: ничего не найдено, `exit=1`.

- [ ] **Step 7: Commit**

```bash
git add docs/semantics.md docs/superpowers/specs/2026-08-20-expression-cache-design.md docs/backlog.md
git commit -m "docs: цель присваивания — имя либо путь"
```

---

### Task 2: Дверь записи `Store::setGlobalAt`

**Files:**
- Modify: `core/src/store.hpp` (объявление рядом с `setGlobal`)
- Modify: `core/src/store.cpp` (реализация рядом с `setGlobal`)
- Test: `core/tests/store_test.cpp`

**Interfaces:**
- Consumes: имя `setGlobalAt` из задачи 1.
- Produces: `void Store::setGlobalAt(GlobalSlot slot, Value v, Deferred &dead);` — задача 3 зовёт её из вычислителя.

- [ ] **Step 1: Написать падающие тесты**

Дописать в конец `core/tests/store_test.cpp`, внутрь анонимного пространства имён:

```cpp
TEST(StoreGlobals, SetGlobalAtReplacesValueAndBumpsEpoch) {
    Store store;
    Deferred dead;
    store.setGlobal("n", Value::number(1), dead);
    const CS::GlobalSlot slot = store.globalSlot("n");
    ASSERT_NE(slot, CS::kNoGlobalSlot);
    const CS::Epoch before = store.epochAt(slot);

    store.setGlobalAt(slot, Value::number(2), dead);

    EXPECT_EQ(store.globalValueAt(slot).numberValue(), 2.0);
    // Подъём эпохи стоит внутри двери: без него читатель кэша не узнает о
    // записи и экран замрёт молча.
    EXPECT_GT(store.epochAt(slot), before);
}

TEST(StoreGlobals, SetGlobalAtReleasesTheDisplacedBox) {
    Store store;
    const std::size_t empty = CS::detail::liveBoxCount();
    {
        Deferred dead;
        store.setGlobal("obj",
                        CS::makeObject(store.keys(), 0, store.clock(), dead),
                        dead);
        const CS::GlobalSlot slot = store.globalSlot("obj");
        // Ячейка — корень: без отпускания вытесненного повторная запись
        // растила бы память вечно.
        store.setGlobalAt(slot, Value::number(1), dead);
    }
    EXPECT_EQ(CS::detail::liveBoxCount(), empty);
}

TEST(StoreGlobals, SetGlobalAtSurvivesWritingTheSameValue) {
    Store store;
    const std::size_t empty = CS::detail::liveBoxCount();
    {
        Deferred dead;
        store.setGlobal("obj",
                        CS::makeObject(store.keys(), 0, store.clock(), dead),
                        dead);
        const CS::GlobalSlot slot = store.globalSlot("obj");
        // retain нового идёт до отпускания старого: иначе x = x роняет
        // счётчик в ноль между чтением и записью.
        store.setGlobalAt(slot, store.globalValueAt(slot), dead);
        EXPECT_EQ(store.globalValueAt(slot).kind(), Value::Kind::Object);
    }
    EXPECT_EQ(CS::detail::liveBoxCount(), empty);
}
```

`liveBoxCount` объявлен в `core/src/box.hpp` и существует только в отладочной сборке — `store_test.cpp` уже включает `box.hpp` и `aggregate.hpp`, дополнительных включений не требуется.

- [ ] **Step 2: Прогнать — тесты обязаны не собраться**

Run:
```bash
cmake --build build-dbg -j8 --target chupascript_tests
```
Expected: ошибка компиляции вида `no member named 'setGlobalAt' in 'CS::Store'`.

- [ ] **Step 3: Объявить дверь**

В `core/src/store.hpp` сразу после объявления `setGlobal` (там, где заканчивается его док-комментарий) добавить:

```cpp
    /// Кладёт значение в **существующую** ячейку по её номеру.
    ///
    /// Дверь программы, в отличие от setGlobal — двери хоста: номер ячейки
    /// проставлен проходом check, поэтому искать имя незачем, а завести новое
    /// нечем. Этим и держится §7.1: состав контекста программе неподвластен.
    ///
    /// Эпоха поднимается здесь, а не у вызывающего, — как и в setGlobal:
    /// запись без подъёма даёт не «медленно», а молча застывший экран
    /// (docs/superpowers/specs/2026-08-20-expression-cache-design.md §4.5).
    ///
    /// dead принимает вытесненное значение по той же причине, что и у
    /// setGlobal: ячейка — корень.
    ///
    /// Предусловие: номер выдан ЭТИМ хранилищем — как и у globalValueAt.
    void setGlobalAt(GlobalSlot slot, Value v, Deferred &dead);
```

- [ ] **Step 4: Реализовать**

В `core/src/store.cpp` сразу после тела `setGlobal` добавить:

```cpp
void Store::setGlobalAt(GlobalSlot slot, Value v, Deferred &dead) {
    assert(slot < values_.size() && "номер ячейки выдан другим хранилищем");
    // retain нового идёт первым: при записи значения в самоё себя порядок
    // наоборот уронил бы счётчик в ноль между отпусканием и присваиванием.
    detail::retainValue(v);
    Value &cell = values_[slot];
    dead.take(cell);
    cell = v;
    epochs_.bump(slot, clock_.tick());
}
```

- [ ] **Step 5: Прогнать тесты**

Run:
```bash
cmake --build build-dbg -j8 --target chupascript_tests \
  && ./build-dbg/core/tests/chupascript_tests --gtest_filter='StoreGlobals.SetGlobalAt*'
```
Expected: 3 теста, все PASS, вывод чистый.

- [ ] **Step 6: Прогнать весь набор**

Run:
```bash
./build-dbg/core/tests/chupascript_tests
```
Expected: всё зелёное — задача ничего не отнимает, только добавляет дверь.

- [ ] **Step 7: Commit**

```bash
git add core/src/store.hpp core/src/store.cpp core/tests/store_test.cpp
git commit -m "feat: Store::setGlobalAt — запись в ячейку по номеру"
```

---

### Task 3: Снять проверку и научить вычислитель писать в имя

**Files:**
- Modify: `core/src/check.cpp` (удаление ветки `case NodeKind::Assign`)
- Modify: `core/src/eval.cpp` (новая `assignToName`, новая ветка в `assign`)
- Test: `core/tests/check_test.cpp` (инверсия одного теста), `core/tests/eval_test.cpp` (новые тесты)

**Interfaces:**
- Consumes: `void Store::setGlobalAt(GlobalSlot slot, Value v, Deferred &dead)` из задачи 2.
- Produces: `name = value;` и `name op= value;` — законные стейтменты. Задача 4 на это опирается.

- [ ] **Step 1: Инвертировать тест проверки**

В `core/tests/check_test.cpp` тест `Check.AssigningToANameIsACompileError` (около строки 155) заменить целиком на:

```cpp
TEST(Check, AssigningToANameIsAllowed) {
    Store store;
    put(store, "state", "{'n': 0}");
    // Запрет снят целиком: docs/semantics.md §7.2, спека
    // docs/superpowers/specs/2026-08-21-assign-to-name-design.md. Отменяет
    // B27, ради которого проверка сюда переезжала.
    EXPECT_TRUE(checkScript(store, "state = 1;").empty());
    EXPECT_TRUE(checkScript(store, "state.n = 1;").empty());
}

TEST(Check, AssigningToAnUnknownNameIsACompileError) {
    Store store;
    put(store, "state", "{'n': 0}");
    // Ослаблен запрет менять имя, а не запрет заводить его: состав контекста
    // программе по-прежнему неподвластен (docs/semantics.md §7.1).
    //
    // Диагностика сверяется дословно, а не по коду: ErrorCode::Name носят
    // несколько проверок этого прохода, и тест по коду был бы зелёным, поймав
    // не ту.
    ASSERT_EQ(checkScript(store, "missing = 1;").size(), 1u);
    EXPECT_EQ(checkScript(store, "missing = 1;")[0].code, CS::ErrorCode::Name);
    EXPECT_STREQ(checkScript(store, "missing = 1;")[0].message, "unknown name");
}
```

- [ ] **Step 2: Написать падающие тесты вычислителя**

Дописать в конец `core/tests/eval_test.cpp`, внутрь анонимного пространства имён, ниже всех тестов `EvalAssignIndex.*`. Файл уже несёт помощники, которыми пользуются соседние тесты присваивания: `put(store, "имя", "текст литерала")` заводит переменную, `run(exec, "скрипт;")` разбирает и выполняет с требованием успеха обоих шагов, `evaluate(exec, "выражение")` возвращает `Value`. Никаких новых включений не нужно.

```cpp
TEST(EvalAssignToName, WritesAScalar) {
    Store store;
    CS::Execution exec(store);
    CS::Deferred dead;
    store.setGlobal("liked", Value::boolean(false), dead);

    run(exec, "liked = true;");

    EXPECT_TRUE(evaluate(exec, "liked").booleanValue());
}

TEST(EvalAssignToName, CompoundAssignmentReadsTheCell) {
    Store store;
    CS::Execution exec(store);
    CS::Deferred dead;
    store.setGlobal("n", Value::number(1), dead);

    // x op= e есть x = x op e, и читается x из ячейки.
    run(exec, "n += 2; n *= 3;");

    EXPECT_EQ(evaluate(exec, "n").numberValue(), 9.0);
}

TEST(EvalAssignToName, BumpsTheCellEpoch) {
    Store store;
    CS::Execution exec(store);
    CS::Deferred dead;
    store.setGlobal("n", Value::number(1), dead);
    const CS::GlobalSlot slot = store.globalSlot("n");
    const CS::Epoch before = store.epochAt(slot);

    run(exec, "n = 2;");

    // Без подъёма отслеживание зависимостей записи не увидит, и выражение,
    // читающее n, останется на прежнем значении навсегда.
    EXPECT_GT(store.epochAt(slot), before);
}

TEST(EvalAssignToName, ReplacingTheBindingLeavesTheAliasAlone) {
    Store store;
    CS::Execution exec(store);
    CS::Deferred dead;
    // Алиас создаёт хост, и создать его можно только отсюда: через C-границу
    // одну коробку под двумя именами не передать — все четыре сеттера строят
    // значение с нуля (спека §2.2).
    const Value shared = CS::makeObject(store.keys(), 1, store.clock(), dead);
    store.setGlobal("a", shared, dead);
    store.setGlobal("b", shared, dead);

    // Содержимое общее: изменение через одно имя видно через второе (§2.3).
    run(exec, "a.k = 1;");
    EXPECT_EQ(evaluate(exec, "b.k").numberValue(), 1.0);

    // Привязка — не содержимое: b продолжает смотреть на прежнюю коробку.
    run(exec, "a = 5;");
    EXPECT_EQ(evaluate(exec, "a").numberValue(), 5.0);
    EXPECT_EQ(evaluate(exec, "b.k").numberValue(), 1.0);
}
```

- [ ] **Step 3: Прогнать — тесты обязаны упасть**

Run:
```bash
cmake --build build-dbg -j8 --target chupascript_tests \
  && ./build-dbg/core/tests/chupascript_tests \
     --gtest_filter='EvalAssignToName.*:Check.AssigningToA*'
```
Expected: `Check.AssigningToANameIsAllowed` падает — `checkScript(store, "state = 1;")` даёт 1 диагностику вместо 0. Все четыре `EvalAssignToName.*` падают внутри помощника `run` на `ASSERT_EQ(errors, 0u)` с сообщением `cannot assign to a variable name`.

- [ ] **Step 4: Удалить ветку проверки**

В `core/src/check.cpp` в `Checker::checkNode` удалить целиком:

```cpp
            case NodeKind::Assign:
                // Целью не может быть само имя (docs/semantics.md §7.2).
                if (ast.kind(ast.child(node, 0)) == NodeKind::Identifier) {
                    report(ast.child(node, 0), ErrorCode::Name,
                           "cannot assign to a variable name");
                }
                break;
```

Замены не требуется: ветка `case NodeKind::Identifier` того же `switch` обходит **все** узлы дерева, включая цель присваивания, — она и отвергает неизвестное имя, и кладёт в узел номер ячейки.

- [ ] **Step 5: Научить вычислитель**

В `core/src/eval.cpp` непосредственно перед функцией `assign` (той, что начинается с комментария `/// Присваивание: разбирает форму цели и передаёт дальше.`) добавить:

```cpp
/// Присваивание имени: name = v.
///
/// Подвыражений у цели нет, поэтому требование §7.2 «подвыражения цели
/// вычисляются до правой части» выполняется пусто.
///
/// Узел цели не вычисляется вовсе: номер ячейки проставлен проходом check и
/// читается прямо из дерева. Пройти через eval значило бы записать ячейку в
/// набор зависимостей — а запись в неё чтением не является, и зависимость
/// была бы ложной.
bool assignToName(const Ast &ast, std::string_view source, NodeId node,
                  NodeId target, Execution &exec, Diagnostic &diag) {
    const GlobalSlot slot = ast.globalValuesSlot(target);

    Value value = Value::null();
    if (!eval(ast, source, ast.child(node, 1), exec, &value, diag)) { return false; }

    const TokenKind op = ast.op(node);
    if (op != TokenKind::Assign) {
        // x op= e есть x = x op e; порядок «правая часть, затем текущее
        // значение» — тот же, что у цели-пути (assignToKey выше).
        const Value current = exec.store().globalValueAt(slot);
        Value combined = Value::null();
        if (!applyBinary(compoundOperation(op), current, value,
                         ast.offset(node), &combined, diag)) {
            return false;
        }
        value = combined;
    }

    exec.store().setGlobalAt(slot, value, exec.deferred());
    return true;
}
```

Затем в `switch` внутри `assign` добавить ветку первой, перед `case NodeKind::Member`:

```cpp
        case NodeKind::Identifier:
            return assignToName(ast, source, node, target, exec, diag);
```

И поправить комментарий в ветке `default` — он сегодня объясняет, что `Identifier` отсеян проходом `check`. Заменить его текст на:

```cpp
            // Грамматика строит целью Identifier, Member и Index
            // (docs/grammar.md §5.2) — все три разобраны выше. Ветка
            // защитная, на случай непроверенного дерева.
```

- [ ] **Step 6: Прогнать целевые тесты**

Run:
```bash
cmake --build build-dbg -j8 --target chupascript_tests \
  && ./build-dbg/core/tests/chupascript_tests \
     --gtest_filter='EvalAssignToName.*:Check.AssigningToA*'
```
Expected: 6 тестов, все PASS.

- [ ] **Step 7: Прогнать весь набор**

Run:
```bash
./build-dbg/core/tests/chupascript_tests
```
Expected: всё зелёное. Тесты, согнутые под снятое правило (`Script.OwnsItsSource`, `Script.ReportsUnknownName`, `Script.RefusesToEvaluateOnAnotherStore`), пользуются целями-путями и продолжают проходить — распрямляет их задача 4.

- [ ] **Step 8: Commit**

```bash
git add core/src/check.cpp core/src/eval.cpp core/tests/check_test.cpp core/tests/eval_test.cpp
git commit -m "feat: присваивание имени"
```

---

### Task 4: Распрямить согнутые тесты и проверить сквозь

**Files:**
- Modify: `core/tests/script_test.cpp` (три теста)
- Modify: `core/tests/context_test.cpp` (комментарий у `ContextMemory.RewrittenGlobalDoesNotGrowForever`)

**Interfaces:**
- Consumes: `name = value;` из задачи 3.
- Produces: ничего.

Три теста написаны в обход снятого правила и несут объяснительные комментарии. Комментарии врут с момента задачи 3, а сами тесты проверяют не ту форму, ради которой писались.

- [ ] **Step 1: `Script.OwnsItsSource`**

В `core/tests/script_test.cpp` в тесте `Script.OwnsItsSource` удалить комментарий, начинающийся со слов `// Прямое присваивание имени переменной запрещено языком`, и вернуть тест к форме своего брифа: переменная `obj` заводится как число, временный исходник — `"n = n + 1;"`.

```cpp
TEST(Script, OwnsItsSource) {
    CS::Deferred dead;
    CS::Store store;
    CS::Execution exec(store);
    CS::Diagnostic diag;
    store.setGlobal("n", CS::Value::number(1), dead);

    CS::Script script;
    CS::Diagnostic diags[1];
    {
        std::string temporary = "n = n + 1;";
        ASSERT_EQ(CS::Script::compile(temporary, store, &script, diags, 1), 0u);
    }
    // Временный буфер умер вместе с областью видимости: единица обязана
    // держать собственную копию исходника.
    EXPECT_EQ(script.source(), "n = n + 1;");
    EXPECT_TRUE(script.run(exec, diag));
    EXPECT_EQ(store.global("n").numberValue(), 2.0);
}
```

- [ ] **Step 2: `Script.ReportsUnknownName`**

Там же: удалить комментарий, начинающийся со слов `// Цель присваивания — Member, а не голый Identifier`, и заменить исходник `"missing.field = 1;"` на `"missing = 1;"`. Ожидание — по-прежнему одна диагностика с кодом `CS::ErrorCode::Name` и текстом `"unknown name"`; сверка текста дословно сохраняется, и её обоснование остаётся верным (код `Name` носят несколько проверок прохода).

- [ ] **Step 3: `Script.RefusesToEvaluateOnAnotherStore`**

Там же: удалить комментарий, начинающийся со слов `// Direct assignment to a variable name is rejected by the language`, завести переменную `x` числом и компилировать `"x = 2;"` вместо `"x.n = 2;"`:

```cpp
    ASSERT_TRUE(CS::setVariable(home, dead, "x", "1", diag));
```

Остальную часть теста — отказ выполняться на чужом хранилище — не трогать.

- [ ] **Step 4: комментарий в `context_test.cpp`**

В `core/tests/context_test.cpp` у теста `ContextMemory.RewrittenGlobalDoesNotGrowForever` первый абзац комментария сегодня утверждает, что переписывает переменную только хост, «потому что присваивать переменную целиком язык не даёт». Утверждение перестало быть верным. Заменить абзац на:

```cpp
    // Здесь меряется путь хоста — setVariableText: бэкенд шлёт новые данные на
    // каждое обновление экрана, и это основной случай перезаписи. Путь
    // программы (присваивание имени, docs/semantics.md §7.2) отпускает
    // вытесненное той же дверью Store и меряется в store_test.cpp
    // (StoreGlobals.SetGlobalAtReleasesTheDisplacedBox).
```

Сам тест не трогать.

- [ ] **Step 5: Прогнать весь набор**

Run:
```bash
cmake --build build-dbg -j8 --target chupascript_tests \
  && ./build-dbg/core/tests/chupascript_tests
```
Expected: всё зелёное, вывод чистый.

- [ ] **Step 6: Прогнать под санитайзером**

Run:
```bash
./tools/asan.sh
```
Expected: зелёно. Задача 2 добавила отпускание вытесненной коробки на новом пути — течь обязана не появиться.

- [ ] **Step 7: Сквозная проверка в REPL**

Run:
```bash
cmake --build build-rel -j8 --target chupa
printf ':set liked = false\nscript: liked = true;\nexpr: liked\n' | ./build-rel/cli/chupa -repl
```
Expected: `script:` отрабатывает без диагностики, `expr: liked` печатает `true`. Это исходный отказ, с которого работа началась.

- [ ] **Step 8: Commit**

```bash
git add core/tests/script_test.cpp core/tests/context_test.cpp
git commit -m "test: распрямить тесты, согнутые под снятое правило"
```
