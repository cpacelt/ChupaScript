# Сущности ядра и тонкая C-обёртка — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Перенести `Expression` и `Script` в ядро полноценными сущностями, а `c_api` свести к однополевым обёрткам — и тем самым закрыть B35, UAF-1 и UAF-3 по построению.

**Architecture:** Дерево перестаёт хранить указатель на исходник и получает его параметром (Р3). Над деревом появляется `CS::Expression` / `CS::Script` — единица, владеющая копией исходника и своим `Ast`, собираемая единственной дверью `compile` (Р1, Р2). Владеет единицей хост, а не контекст (Р4). Результат-строка отдаётся хосту во владение через `ChupaString` (Р6).

**Tech Stack:** C++20, CMake, GoogleTest, Google Benchmark, Swift (проверяется только чтением).

**Spec:** `docs/superpowers/specs/2026-08-15-chupascript-core-entities-design.md`

## Global Constraints

- Стандарт — C++20; строгие флаги предупреждений включены проектом, предупреждение равно ошибке сборки.
- Комментарии и сообщения коммитов — по-русски, как во всём репозитории. Публичный C-заголовок комментируется по-английски, как сейчас.
- `Ast` остаётся недостижимым для CLI, C и Swift: заголовок `core/src/ast.hpp` не входит в `core/include/`.
- Приватный конструктор `Expression`/`Script`: собрать единицу мимо `compile` нельзя.
- Тесты гоняются целиком после каждой задачи: `cmake --build build -j8 && ctest --test-dir build --output-on-failure`. Все 536 обязаны проходить.
- ABI ломать можно: тегов нет, потребителей нет (спека §7).
- Имя `release` в C API не употреблять — только `destroy` (Р6).
- База замеров снята до работ: `docs/benchmarks/baseline-2026-08-15.json`.

---

## Структура файлов

| Файл | Ответственность | Задача |
|---|---|---|
| `core/src/ast.hpp/.cpp` | дерево без исходника; `text(node, source)` | 1 |
| `core/src/parser.hpp/.cpp` | без изменений в подписи; внутри `reset(length)` | 1 |
| `core/src/check.hpp/.cpp` | `check(ast, source, store, ...)` | 1 |
| `core/src/eval.hpp/.cpp` | `source` параметром по всей рекурсии | 1 |
| `core/src/text.hpp/.cpp` | `literalText(ast, node, source, scratch)` | 1 |
| `core/src/data.cpp` | локальное дерево + свой `source` | 1 |
| `core/src/compile.hpp/.cpp` | без изменений в подписи | 1 |
| `core/src/expression.hpp/.cpp` | **новое**: `EvalStatus`, `CS::Expression` | 2, 3 |
| `core/src/script.hpp/.cpp` | **новое**: `CS::Script` | 2 |
| `core/src/c_api.cpp` | однополевые обёртки, перевод перечислений | 4, 5 |
| `core/include/chupascript/chupascript.h` | `*_destroy`, `ChupaString` | 4, 5 |
| `cli/main.cpp` | на `CS::Expression` / `CS::Script` | 6 |
| `swift/*.swift` | снятие префикса, новый контракт строки | 7 |

`Expression` и `Script` разведены по файлам: у них разные подписи вычисления и разные потребители; общего у них только приватное устройство, а не интерфейс.

---

### Task 1: `Ast` перестаёт хранить исходник

Механическая задача, целиком проверяемая компилятором: пока не поправлено каждое место, сборка не проходит. Делается одним заходом, потому что промежуточного собирающегося состояния у неё нет.

**Files:**
- Modify: `core/src/ast.hpp:52` (`reset`), `core/src/ast.hpp:101` (`text`), `core/src/ast.hpp:133` (`src_`), `core/src/ast.hpp:42-51` (шапка класса)
- Modify: `core/src/ast.cpp:13-21` (`reset`), `core/src/ast.cpp:239-246` (`text`)
- Modify: `core/src/parser.cpp` (вызов `ast.reset`)
- Modify: `core/src/check.hpp:24`, `core/src/check.cpp:10` (поле структуры прохода)
- Modify: `core/src/eval.hpp:22,36`, `core/src/eval.cpp` (14 функций с `const Ast &`)
- Modify: `core/src/text.hpp:26`, `core/src/text.cpp:32` (`literalText`)
- Modify: `core/src/data.cpp:17,45` (`rejectNode`, `materialize`)
- Modify: `core/src/compile.cpp:17-35` (проброс в `check`)
- Modify: `core/src/c_api.cpp:252,279` (убрать `ast->reset(src)`), `307,330,353,408` (передать исходник)
- Modify: `cli/main.cpp:47-81`
- Modify: `benchmarks/eval_benchmark.cpp:46-60,148-175,227-243`
- Test: `core/tests/ast_test.cpp`, `check_test.cpp`, `eval_test.cpp`, `text_test.cpp`, `parser_test.cpp`, `data_test.cpp`

**Interfaces:**
- Consumes: ничего
- Produces:
  ```cpp
  void Ast::reset(std::uint32_t sourceLength);
  std::string_view Ast::text(NodeId node, std::string_view source) const noexcept;
  std::uint32_t Ast::sourceLength() const noexcept;

  std::uint32_t check(Ast &ast, std::string_view source, const Store &store,
                      Diagnostic *out, std::uint32_t capacity);
  bool evalExpression(const Ast &ast, std::string_view source, Store &store,
                      Value *out, Diagnostic &diag);
  bool runScript(const Ast &ast, std::string_view source, Store &store,
                 Diagnostic &diag);
  std::string_view literalText(const Ast &ast, NodeId node,
                               std::string_view source, std::string &scratch);
  ```
  Подписи `parseExpression`, `parseScript`, `compileExpression`, `compileScript` **не меняются**: они и так принимают `source` и `length`.

- [ ] **Шаг 1: Написать падающий тест**

В `core/tests/ast_test.cpp` — тест на то, ради чего всё затевается: дерево переживает перемещение своего исходника.

```cpp
TEST(Ast, TextSurvivesSourceRelocation) {
    // Короткая строка попадает в SSO: её байты лежат внутри самого
    // std::string, и рост вектора их ФИЗИЧЕСКИ ДВИГАЕТ. Ровно так ломался
    // UAF-3 (docs/backlog.md B39).
    std::vector<std::string> sources;
    sources.reserve(1);
    sources.emplace_back("user.name");

    CS::Ast ast;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::parseExpression(sources[0].data(),
                                    static_cast<std::uint32_t>(sources[0].size()),
                                    ast, diag));

    // Вектор растёт — sources[0] уезжает на новый адрес.
    for (int i = 0; i < 8; ++i) { sources.emplace_back("x"); }

    // Дерево ничего не заметило: исходник приходит параметром.
    EXPECT_EQ(ast.text(ast.child(ast.root(), 0), sources[0]), "user");
}
```

- [ ] **Шаг 2: Прогнать тест и убедиться, что он не собирается**

Run: `cmake --build build -j8 --target chupascript_tests`
Expected: FAIL — `no matching member function for call to 'text'` (у `Ast::text` один параметр).

- [ ] **Шаг 3: Поменять `Ast`**

`core/src/ast.hpp` — снять поле, поменять две подписи, добавить длину:

```cpp
    /// Начинает новое дерево над исходником такой длины.
    ///
    /// Выбрасывает всё, что было построено раньше: Ast пригоден для повторного
    /// разбора. Самого исходника дерево не держит — узлы хранят смещения, а
    /// байты приходят параметром в text(). Поэтому Ast безразличен к тому,
    /// куда переехал буфер (docs/backlog.md B39).
    void reset(std::uint32_t sourceLength);

    /// Имя либо содержимое строкового литерала без кавычек, сырыми байтами.
    ///
    /// source обязан быть тем же текстом, над которым дерево построено.
    /// В отладочной сборке несовпадение ловится утверждением по длине.
    [[nodiscard]] std::string_view text(NodeId node,
                                        std::string_view source) const noexcept;

    /// Длина исходника, над которым построено дерево. Для утверждений и тестов.
    [[nodiscard]] std::uint32_t sourceLength() const noexcept;
```

Поле `const char *src_ = nullptr;` заменить на `std::uint32_t sourceLength_ = 0;`.
Шапку класса (`ast.hpp:41-42`) поправить: «Ничем не владеет: текст имён и литералов — смещения в исходнике, который передаётся аксессорам параметром».

`core/src/ast.cpp`:

```cpp
void Ast::reset(std::uint32_t sourceLength) {
    sourceLength_ = sourceLength;
    root_ = kNoNode;
    nodes_.clear();
    children_.clear();
    nodes_.push_back(Node{});  // индекс kNoNode
    // Дерево выброшено — отметка уходит вместе с ним.
    checked_ = false;
}

std::uint32_t Ast::sourceLength() const noexcept { return sourceLength_; }

std::string_view Ast::text(NodeId node, std::string_view source) const noexcept {
    assert(node < nodes_.size());
    // Дешёвая ловушка на «передали не тот исходник» (спека Р3).
    assert(source.size() == sourceLength_);
    const Node &n = nodes_[node];
    if (n.textLength == 0) { return {}; }
    assert(n.textOffset + n.textLength <= source.size());
    return source.substr(n.textOffset, n.textLength);
}
```

- [ ] **Шаг 4: Пробросить исходник через `parser`, `check`, `text`, `data`, `eval`**

Порядок — снизу вверх, чтобы каждая правка снимала ровно одну группу ошибок компилятора.

`core/src/parser.cpp`: единственный вызов `ast.reset(source)` → `ast.reset(length)`.

`core/src/text.hpp/.cpp` — `literalText` берёт исходник:

```cpp
std::string_view literalText(const Ast &ast, NodeId node,
                             std::string_view source, std::string &scratch) {
    assert(ast.kind(node) == NodeKind::String);
    const std::string_view raw = ast.text(node, source);
    return ast.hasEscape(node) ? decodeEscapes(raw, scratch) : raw;
}
```

`core/src/check.cpp:10` — у структуры прохода рядом с `const Ast &ast;` появляется `std::string_view source;`; все `ast.text(node)` внутри становятся `ast.text(node, source)`. Инициализация — из нового параметра `check`.

`core/src/data.cpp` — `rejectNode` и `materialize` получают `std::string_view source` вторым параметром после `ast`; `setVariable` уже держит `text` и передаёт его вниз.

`core/src/eval.cpp` — всем четырнадцати функциям с `const Ast &ast` дописывается `std::string_view source` сразу за ним. Четыре места зовут `ast.text(...)` (`eval.cpp:249,265,407,488`) — им дописывается `source`.

`core/src/compile.cpp` — `check(ast, std::string_view(source, length), store, out, capacity)` в обеих функциях.

- [ ] **Шаг 5: Поправить потребителей**

`core/src/c_api.cpp`: убрать оба `ast->reset(src)` (парсер делает это сам); четырём вызовам `CS::evalExpression` / `CS::runScript` передать исходник. Временно — из `c->sources`; задача 4 это уберёт совсем.

`cli/main.cpp:73,80`: `CS::runScript(ast, source, store, diag)`, `CS::evalExpression(ast, source, store, &out, diag)`.

`benchmarks/eval_benchmark.cpp`: те же три места (`runEval`, `runScriptBench`, `runCheck`) получают `source` в вызовы.

- [ ] **Шаг 6: Поправить тесты**

Механически: везде, где зовётся `ast.text(node)`, `check(ast, ...)`, `evalExpression(...)`, `runScript(...)`, `literalText(...)` — дописать исходник. Компилятор перечислит все места сам.

- [ ] **Шаг 7: Прогнать тесты**

Run: `cmake --build build -j8 && ctest --test-dir build --output-on-failure`
Expected: PASS, 537 тестов (536 прежних плюс новый из шага 1).

- [ ] **Шаг 8: Коммит**

```bash
git add core/src cli benchmarks core/tests
git commit -m "refactor: дерево больше не хранит указатель на исходник

Ast::src_ убран: узлы и так держат смещения, а байты приходят
параметром в text(). Дерево стало безразлично к перемещению
исходника — это то, чем был UAF-3 (B39).

Ломать было нечему: единственное разыменование src_ жило в
Ast::text, остальное — механический проброс параметра."
```

---

### Task 2: `CS::Expression` и `CS::Script`

**Files:**
- Create: `core/src/expression.hpp`, `core/src/expression.cpp`
- Create: `core/src/script.hpp`, `core/src/script.cpp`
- Create: `core/tests/expression_test.cpp`, `core/tests/script_test.cpp`
- Modify: `core/CMakeLists.txt` (два новых `.cpp`), `core/tests/CMakeLists.txt` (два новых теста)

**Interfaces:**
- Consumes: из задачи 1 — `check(ast, source, store, ...)`, `evalExpression(ast, source, store, out, diag)`, `runScript(ast, source, store, diag)`
- Produces:
  ```cpp
  namespace CS {
  class Expression {
   public:
      Expression() = default;
      static std::uint32_t compile(std::string_view source, const Store &store,
                                   Expression *out, Diagnostic *diags,
                                   std::uint32_t capacity);
      bool eval(Store &store, Value *out, Diagnostic &diag) const;
      [[nodiscard]] std::string_view source() const noexcept;
   private:
      std::string source_;
      Ast ast_;
  };
  class Script {
   public:
      Script() = default;
      static std::uint32_t compile(std::string_view source, const Store &store,
                                   Script *out, Diagnostic *diags,
                                   std::uint32_t capacity);
      bool run(Store &store, Diagnostic &diag) const;
      [[nodiscard]] std::string_view source() const noexcept;
   private:
      std::string source_;
      Ast ast_;
  };
  }
  ```

- [ ] **Шаг 1: Написать падающие тесты**

`core/tests/expression_test.cpp`:

```cpp
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "diagnostic.hpp"
#include "data.hpp"
#include "expression.hpp"
#include "store.hpp"

namespace {

CS::Store storeWithUser() {
    CS::Store store;
    CS::Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(store, "user", "{'name': 'Вася'}", diag));
    return store;
}

TEST(Expression, CompilesAndEvaluates) {
    CS::Store store = storeWithUser();
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("user.name", store, &expr, diags, 1), 0u);

    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.eval(store, &out, diag));
    EXPECT_EQ(store.string(out), "Вася");
}

TEST(Expression, OwnsItsSource) {
    CS::Store store = storeWithUser();
    CS::Expression expr;
    CS::Diagnostic diags[1];
    {
        // Исходник живёт в буфере, который умрёт прямо сейчас.
        std::string temporary = "user.name";
        ASSERT_EQ(CS::Expression::compile(temporary, store, &expr, diags, 1), 0u);
    }
    // Единица самодостаточна: правила «буфер обязан пережить» больше нет.
    EXPECT_EQ(expr.source(), "user.name");

    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.eval(store, &out, diag));
    EXPECT_EQ(store.string(out), "Вася");
}

TEST(Expression, SurvivesBeingMoved) {
    CS::Store store = storeWithUser();
    std::vector<CS::Expression> units;
    units.reserve(1);
    units.emplace_back();

    CS::Diagnostic diags[1];
    // Короткий исходник — та самая SSO-строка, на которой ломался UAF-3.
    ASSERT_EQ(CS::Expression::compile("user.name", store, &units[0], diags, 1), 0u);
    for (int i = 0; i < 8; ++i) { units.emplace_back(); }  // вектор переехал

    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    ASSERT_TRUE(units[0].eval(store, &out, diag));
    EXPECT_EQ(store.string(out), "Вася");
}

TEST(Expression, ReportsSyntaxError) {
    CS::Store store = storeWithUser();
    CS::Expression expr;
    CS::Diagnostic diags[2];
    EXPECT_EQ(CS::Expression::compile("user..name", store, &expr, diags, 2), 1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Syntax);
}

TEST(Expression, ReportsUnknownName) {
    CS::Store store = storeWithUser();
    CS::Expression expr;
    CS::Diagnostic diags[2];
    EXPECT_EQ(CS::Expression::compile("missing.name", store, &expr, diags, 2), 1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Name);
}

TEST(Expression, RecompileReplacesEverything) {
    CS::Store store = storeWithUser();
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("user.name", store, &expr, diags, 1), 0u);
    ASSERT_EQ(CS::Expression::compile("1 + 1", store, &expr, diags, 1), 0u);
    EXPECT_EQ(expr.source(), "1 + 1");

    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.eval(store, &out, diag));
    EXPECT_DOUBLE_EQ(out.numberValue(), 2.0);
}

}  // namespace
```

`core/tests/script_test.cpp`:

```cpp
#include <gtest/gtest.h>

#include <string>

#include "data.hpp"
#include "diagnostic.hpp"
#include "script.hpp"
#include "store.hpp"

namespace {

TEST(Script, CompilesAndRuns) {
    CS::Store store;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, "user", "{'name': 'Вася'}", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Script::compile("user.name = 'Петя';", store, &script, diags, 1),
              0u);
    ASSERT_TRUE(script.run(store, diag));

    CS::Value user = CS::Value::null();
    ASSERT_TRUE(store.global("user", &user));
    CS::Value name = CS::Value::null();
    ASSERT_TRUE(store.objectGet(user, "name", &name));
    EXPECT_EQ(store.string(name), "Петя");
}

TEST(Script, OwnsItsSource) {
    CS::Store store;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, "n", "1", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    {
        std::string temporary = "n = n + 1;";
        ASSERT_EQ(CS::Script::compile(temporary, store, &script, diags, 1), 0u);
    }
    EXPECT_EQ(script.source(), "n = n + 1;");
    EXPECT_TRUE(script.run(store, diag));
}

TEST(Script, ReportsUnknownName) {
    CS::Store store;
    CS::Script script;
    CS::Diagnostic diags[2];
    EXPECT_EQ(CS::Script::compile("missing = 1;", store, &script, diags, 2), 1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Name);
}

}  // namespace
```

Сверься с `core/tests/eval_test.cpp` и `store_test.cpp` по именам аксессоров `Store` (`global`, `objectGet`, `string`) — если они называются иначе, поправь вызовы, а не смысл теста.

- [ ] **Шаг 2: Прогнать и убедиться, что не собирается**

Run: `cmake --build build -j8`
Expected: FAIL — `'expression.hpp' file not found`.

- [ ] **Шаг 3: Написать `core/src/expression.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string>
#include <string_view>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "store.hpp"
#include "value.hpp"

namespace CS {

/// Скомпилированное выражение: исходник и дерево, связанные навсегда.
///
/// Единица владеет копией своего исходника, поэтому правил времени жизни у
/// неё нет ни одного: буфер, из которого её собрали, может умереть сразу.
/// Владеет единицей тот, кто её создал (docs/backlog.md B35).
///
/// Ast наружу не отдаётся ни ссылкой, ни указателем: он приватная деталь.
/// Разлучить исходник и дерево снаружи нечем — обоих по отдельности снаружи
/// не существует.
class Expression {
   public:
    /// Пустая единица: ни исходника, ни дерева. Годна только под compile.
    /// Вычислять её нельзя: eval утверждается на отметке прохода, как это
    /// делает и evalExpression (core/src/eval.hpp).
    Expression() = default;

    /// Единственная дверь: разбор, проверка и связывание с исходником — разом.
    ///
    /// Возвращает число найденных ошибок; 0 — успех. Форма совпадает с
    /// compileExpression (core/src/compile.hpp): ошибка разбора даёт ровно
    /// единицу, ошибок проверки может быть сколько угодно, и в diags попадает
    /// не больше capacity первых.
    ///
    /// При отказе *out остаётся тем, чем был: неудачная компиляция не портит
    /// уже собранную единицу.
    ///
    /// Из store читается только состав имён (check.hpp); значения роли не
    /// играют, и удерживать store единица не будет.
    static std::uint32_t compile(std::string_view source, const Store &store,
                                 Expression *out, Diagnostic *diags,
                                 std::uint32_t capacity);

    /// Вычисляет выражение. При отказе возвращает false и заполняет diag;
    /// смещение считается от начала source(). При отказе *out не трогается.
    bool eval(Store &store, Value *out, Diagnostic &diag) const;

    [[nodiscard]] std::string_view source() const noexcept { return source_; }

   private:
    std::string source_;
    Ast ast_;
};

}  // namespace CS
```

Приватного конструктора здесь нет намеренно, хотя спека Р1 его называет. Он мешал бы двум законным местам: тесту, который пишет `CS::Expression expr;`, и обёртке `struct ChupaExpression { CS::Expression impl; }` из задачи 4. Инвариант держится иначе и не слабее: пустая единица имеет `source_.empty()` и `ast_.root() == kNoNode`, а `eval` на такой падает утверждением `assert(ast_.isChecked())` — ровно тем же, каким сегодня защищён `evalExpression`. Собрать «дерево не от своего исходника» по-прежнему нечем: `compile` — единственное, что пишет в оба поля, и пишет их вместе.

- [ ] **Шаг 4: Написать `core/src/expression.cpp`**

```cpp
#include "expression.hpp"

#include "compile.hpp"
#include "eval.hpp"

namespace CS {

std::uint32_t Expression::compile(std::string_view source, const Store &store,
                                  Expression *out, Diagnostic *diags,
                                  std::uint32_t capacity) {
    // Компиляция идёт в отдельную единицу и переносится в *out только при
    // успехе: иначе неудачный разбор портил бы уже рабочее выражение.
    Expression built;
    built.source_ = std::string(source);
    const std::uint32_t errors = compileExpression(
        built.source_.data(), static_cast<std::uint32_t>(built.source_.size()),
        built.ast_, store, diags, capacity);
    if (errors != 0) { return errors; }
    *out = std::move(built);
    return 0;
}

bool Expression::eval(Store &store, Value *out, Diagnostic &diag) const {
    return evalExpression(ast_, source_, store, out, diag);
}

}  // namespace CS
```

- [ ] **Шаг 5: Написать `core/src/script.hpp` и `core/src/script.cpp`**

Устроены так же. Отличия ровно два: вместо `eval` — `run(Store &store, Diagnostic &diag) const`, возвращающий `bool` (значения у скрипта нет, `docs/semantics.md` §3.1), и внутри зовётся `compileScript` / `runScript`.

```cpp
// core/src/script.cpp
#include "script.hpp"

#include "compile.hpp"
#include "eval.hpp"

namespace CS {

std::uint32_t Script::compile(std::string_view source, const Store &store,
                              Script *out, Diagnostic *diags,
                              std::uint32_t capacity) {
    Script built;
    built.source_ = std::string(source);
    const std::uint32_t errors = compileScript(
        built.source_.data(), static_cast<std::uint32_t>(built.source_.size()),
        built.ast_, store, diags, capacity);
    if (errors != 0) { return errors; }
    *out = std::move(built);
    return 0;
}

bool Script::run(Store &store, Diagnostic &diag) const {
    return runScript(ast_, source_, store, diag);
}

}  // namespace CS
```

- [ ] **Шаг 6: Прописать файлы в сборку**

`core/CMakeLists.txt` — добавить `src/expression.cpp` и `src/script.cpp` в список (порядок алфавитный: после `src/eval.cpp`, перед `src/lexer.cpp`; `src/script.cpp` — после `src/parser.cpp`).
`core/tests/CMakeLists.txt` — добавить `expression_test.cpp` и `script_test.cpp` тем же способом, каким там перечислены остальные.

- [ ] **Шаг 7: Прогнать тесты**

Run: `cmake --build build -j8 && ctest --test-dir build --output-on-failure`
Expected: PASS. Тестов стало на 9 больше.

- [ ] **Шаг 8: Коммит**

```bash
git add core/src/expression.hpp core/src/expression.cpp \
        core/src/script.hpp core/src/script.cpp \
        core/tests/expression_test.cpp core/tests/script_test.cpp \
        core/CMakeLists.txt core/tests/CMakeLists.txt
git commit -m "feat: CS::Expression и CS::Script — сущности ядра

Единица владеет копией исходника и своим деревом. Ast спрятан:
наружу не отдаётся ни ссылкой, ни указателем.

Правил времени жизни у единицы не остаётся ни одного — буфер,
из которого её собрали, может умереть сразу."
```

---

### Task 3: Извлечение результата по типу

**Files:**
- Modify: `core/src/expression.hpp`, `core/src/expression.cpp`
- Test: `core/tests/expression_test.cpp`

**Interfaces:**
- Consumes: из задачи 2 — `Expression::eval`
- Produces:
  ```cpp
  namespace CS {
  enum class EvalStatus : std::uint8_t { Ok, Null, Error };
  EvalStatus Expression::evalNumber(Store &, double *out, Diagnostic &) const;
  EvalStatus Expression::evalBool  (Store &, bool *out, Diagnostic &) const;
  EvalStatus Expression::evalString(Store &, std::string *out, Diagnostic &) const;
  }
  ```

- [ ] **Шаг 1: Написать падающие тесты**

Дописать в `core/tests/expression_test.cpp`:

```cpp
TEST(Expression, EvalNumberReturnsOk) {
    CS::Store store;
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("1 + 1", store, &expr, diags, 1), 0u);

    double out = 0.0;
    CS::Diagnostic diag;
    EXPECT_EQ(expr.evalNumber(store, &out, diag), CS::EvalStatus::Ok);
    EXPECT_DOUBLE_EQ(out, 2.0);
}

TEST(Expression, EvalNumberReturnsNullSeparately) {
    CS::Store store;
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("null", store, &expr, diags, 1), 0u);

    double out = 42.0;
    CS::Diagnostic diag;
    // Null — не ошибка и не значение: положить его в double* некуда.
    EXPECT_EQ(expr.evalNumber(store, &out, diag), CS::EvalStatus::Null);
    EXPECT_DOUBLE_EQ(out, 42.0);  // *out не тронут
}

TEST(Expression, EvalNumberOnStringIsTypeErrorWithRealOffset) {
    CS::Store store;
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("  'привет'", store, &expr, diags, 1), 0u);

    double out = 0.0;
    CS::Diagnostic diag;
    EXPECT_EQ(expr.evalNumber(store, &out, diag), CS::EvalStatus::Error);
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    // Смещение настоящее, а не ноль: прокладка ставила 0 и указывала в никуда.
    EXPECT_EQ(diag.offset, 2u);
}

TEST(Expression, EvalBoolAndString) {
    CS::Store store;
    CS::Diagnostic diag;
    CS::Diagnostic diags[1];

    CS::Expression flag;
    ASSERT_EQ(CS::Expression::compile("1 < 2", store, &flag, diags, 1), 0u);
    bool b = false;
    EXPECT_EQ(flag.evalBool(store, &b, diag), CS::EvalStatus::Ok);
    EXPECT_TRUE(b);

    CS::Expression text;
    ASSERT_EQ(CS::Expression::compile("'привет'", store, &text, diags, 1), 0u);
    std::string s;
    EXPECT_EQ(text.evalString(store, &s, diag), CS::EvalStatus::Ok);
    EXPECT_EQ(s, "привет");
}

TEST(Expression, EvalStringPropagatesEvalError) {
    CS::Store store;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, "items", "[1]", diag));

    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("items[5]", store, &expr, diags, 1), 0u);

    std::string s;
    EXPECT_EQ(expr.evalString(store, &s, diag), CS::EvalStatus::Error);
    EXPECT_EQ(diag.code, CS::ErrorCode::Range);
}
```

Если `items[5]` в этом языке даёт не `Range`, а что-то другое — сверься с `core/tests/eval_test.cpp` и подставь настоящий код; смысл теста в том, что ошибка вычисления доходит наружу нетронутой, а не подменяется ошибкой типа.

- [ ] **Шаг 2: Прогнать и убедиться, что не собирается**

Run: `cmake --build build -j8`
Expected: FAIL — `no member named 'evalNumber' in 'CS::Expression'`.

- [ ] **Шаг 3: Дописать `expression.hpp`**

```cpp
/// Исход типизированного вычисления.
///
/// Трёхзначен по необходимости: «получилось null» физически некуда положить,
/// когда выходной параметр — double *. У сырого eval такой нужды нет, там
/// null — обычное значение, и он возвращает bool.
enum class EvalStatus : std::uint8_t { Ok, Null, Error };
```

и в классе, за `eval`:

```cpp
    /// Вычисляет и достаёт результат нужного типа.
    ///
    /// Ok — значение положено в *out. Null — выражение дало null, *out не
    /// тронут. Error — ошибка вычисления либо несовпадение типа, подробности
    /// в diag, *out не тронут.
    EvalStatus evalNumber(Store &store, double *out, Diagnostic &diag) const;
    EvalStatus evalBool  (Store &store, bool *out, Diagnostic &diag) const;
    EvalStatus evalString(Store &store, std::string *out, Diagnostic &diag) const;
```

и в приватную часть — общая для всех трёх работа:

```cpp
    /// Вычисляет и проверяет вид значения. Ok — значение нужного вида лежит
    /// в *out и остаётся только достать его. Остальные исходы — как у
    /// публичных методов.
    EvalStatus evalOfKind(Store &store, Value::Kind wanted, const char *message,
                          Value *out, Diagnostic &diag) const;
```

- [ ] **Шаг 4: Дописать `expression.cpp`**

```cpp
EvalStatus Expression::evalOfKind(Store &store, Value::Kind wanted,
                                  const char *message, Value *out,
                                  Diagnostic &diag) const {
    Value value = Value::null();
    if (!eval(store, &value, diag)) { return EvalStatus::Error; }
    if (value.kind() == Value::Kind::Null) { return EvalStatus::Null; }
    if (value.kind() != wanted) {
        // Смещение настоящее: корень выражения на месте, и взять его есть где.
        // Прокладка ставила здесь ноль — то есть указывала в первый байт
        // исходника независимо от того, где на самом деле ошибка.
        diag = Diagnostic{ErrorCode::Type, ast_.offset(ast_.root()), message};
        return EvalStatus::Error;
    }
    *out = value;
    return EvalStatus::Ok;
}

EvalStatus Expression::evalNumber(Store &store, double *out,
                                  Diagnostic &diag) const {
    Value value = Value::null();
    const EvalStatus status = evalOfKind(store, Value::Kind::Number,
                                         "eval_number: value is not a number",
                                         &value, diag);
    if (status == EvalStatus::Ok) { *out = value.numberValue(); }
    return status;
}

EvalStatus Expression::evalBool(Store &store, bool *out,
                                Diagnostic &diag) const {
    Value value = Value::null();
    const EvalStatus status = evalOfKind(store, Value::Kind::Boolean,
                                         "eval_bool: value is not a boolean",
                                         &value, diag);
    if (status == EvalStatus::Ok) { *out = value.booleanValue(); }
    return status;
}

EvalStatus Expression::evalString(Store &store, std::string *out,
                                  Diagnostic &diag) const {
    Value value = Value::null();
    const EvalStatus status = evalOfKind(store, Value::Kind::String,
                                         "eval_string: value is not a string",
                                         &value, diag);
    if (status == EvalStatus::Ok) { *out = std::string(store.string(value)); }
    return status;
}
```

Сообщения об ошибке — строковые литералы со статическим временем жизни, как и все прочие в `Diagnostic` (`core/src/diagnostic.hpp`): поле `message` там — сырой `const char *`, и класть в него что-то временное нельзя.

- [ ] **Шаг 5: Прогнать тесты**

Run: `cmake --build build -j8 && ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Шаг 6: Коммит**

```bash
git add core/src/expression.hpp core/src/expression.cpp core/tests/expression_test.cpp
git commit -m "feat: типизированное вычисление в ядре

evalNumber/evalBool/evalString переезжают из c_api.cpp, где лежали
тремя копиями. Заодно чинится смещение: прокладка ставила ноль,
то есть указывала в первый байт исходника независимо от того, где
на самом деле ошибка."
```

---

### Task 4: `c_api` переходит на сущности ядра

Здесь закрываются B35, UAF-3 и утечка четырёх векторов.

**Files:**
- Modify: `core/include/chupascript/chupascript.h:101-105` (добавить два `destroy`), `89-99` (комментарий UAF-2 не трогать)
- Modify: `core/src/c_api.cpp:26-109` (структуры), `238-296` (компиляция), `300-344` (вычисление), `402-415` (`chupa_run`)
- Test: `core/tests/c_api_test.cpp`

**Interfaces:**
- Consumes: из задач 2 и 3 — `CS::Expression`, `CS::Script`, `CS::EvalStatus`
- Produces:
  ```c
  CHUPA_API void chupa_expression_destroy(ChupaExpression *CHUPA_NULLABLE e);
  CHUPA_API void chupa_script_destroy(ChupaScript *CHUPA_NULLABLE s);
  ```
  ```cpp
  struct ChupaExpression { CS::Expression impl; };
  struct ChupaScript     { CS::Script     impl; };
  ```

- [ ] **Шаг 1: Написать падающие тесты**

Дописать в `core/tests/c_api_test.cpp`:

```cpp
TEST(CApi, SecondCompileDoesNotBreakTheFirst) {
    // Ровно тот сценарий, на котором ломался UAF-3 (B39): оба исходника
    // короче 23 байт, то есть оба попадали в SSO.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(chupa_context_set(ctx, "a", 1, "2", 1));
    ASSERT_TRUE(chupa_context_set(ctx, "b", 1, "3", 1));

    ChupaExpression* first = chupa_compile_expression(ctx, "a + b", 5);
    ASSERT_NE(first, nullptr);
    ChupaExpression* second = chupa_compile_expression(ctx, "a * b", 5);
    ASSERT_NE(second, nullptr);

    double out = 0.0;
    EXPECT_EQ(chupa_eval_number(ctx, first, &out), CHUPA_OK);
    EXPECT_DOUBLE_EQ(out, 5.0);

    chupa_expression_destroy(first);
    chupa_expression_destroy(second);
    chupa_context_destroy(ctx);
}

TEST(CApi, ExpressionOutlivesNothingAndIsFreedByHost) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // Компиляция в цикле больше не растит контекст по построению.
    for (int i = 0; i < 1000; ++i) {
        ChupaExpression* e = chupa_compile_expression(ctx, "1 + 1", 5);
        ASSERT_NE(e, nullptr);
        chupa_expression_destroy(e);
    }
    chupa_context_destroy(ctx);
}

TEST(CApi, FailedCompileLeavesNothingBehind) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(chupa_compile_expression(ctx, "a..b", 4), nullptr);
    }
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_SYNTAX);
    chupa_context_destroy(ctx);
}

TEST(CApi, DestroyAcceptsNull) {
    chupa_expression_destroy(nullptr);
    chupa_script_destroy(nullptr);
}

TEST(CApi, ScriptIsOwnedByHost) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(chupa_context_set(ctx, "n", 1, "1", 1));

    ChupaScript* s = chupa_compile_script(ctx, "n = n + 1;", 10);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(chupa_run(ctx, s));
    chupa_script_destroy(s);

    ChupaExpression* e = chupa_compile_expression(ctx, "n", 1);
    ASSERT_NE(e, nullptr);
    double out = 0.0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_DOUBLE_EQ(out, 2.0);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}
```

- [ ] **Шаг 2: Прогнать и убедиться, что не собирается**

Run: `cmake --build build -j8`
Expected: FAIL — `use of undeclared identifier 'chupa_expression_destroy'`.

- [ ] **Шаг 3: Дописать заголовок**

В `core/include/chupascript/chupascript.h`, сразу за `chupa_compile_script`:

```c
/* Compiled units are owned by the caller, not by the context. Destroying the
 * context does not free them; destroying a unit does not touch the context.
 * A unit may be destroyed in any order relative to the context it was
 * compiled against — it holds no reference to it. */
CHUPA_API void chupa_expression_destroy(ChupaExpression *CHUPA_NULLABLE e);
CHUPA_API void chupa_script_destroy(ChupaScript *CHUPA_NULLABLE s);
```

- [ ] **Шаг 4: Переписать структуры в `c_api.cpp`**

Из `struct ChupaContext` уходят четыре вектора и весь блок комментариев про UAF-3 и утечку (`c_api.cpp:29-70`). Остаётся:

```cpp
struct ChupaContext {
    CS::Store engine;

    /// Состояние последней ошибки в стиле errno — идиома C, вынужденная тем,
    /// что второе значение из функции здесь вернуть нечем. В C++ ошибка
    /// приходит выходным параметром Diagnostic & (core/src/expression.hpp).
    CS::Diagnostic lastError;

    // ╔════════════════════════════════════════════════════════════════════╗
    // ║ UAF-2 (C-половина) — колбэк переживает того, на кого указывает     ║
    // ╚════════════════════════════════════════════════════════════════════╝
    // ... блок оставить как есть: этим планом B38 не закрывается ...
    ChupaRedrawListener redrawListener = nullptr;
    void* redrawUserData = nullptr;
    // ... notifyRedraw, setError, clearError — без изменений ...
};

struct ChupaExpression { CS::Expression impl; };
struct ChupaScript     { CS::Script     impl; };
```

Заголовки `ast.hpp`, `compile.hpp`, `eval.hpp`, `<memory>`, `<vector>` из `c_api.cpp` больше не нужны — их место занимают `expression.hpp` и `script.hpp`. Убери лишние включения: строгие флаги на неиспользуемое включение не ругаются, но оставлять их значит врать про зависимости.

Заодно поправь шапку файла (`c_api.cpp:1-5`): «Compile/eval/run/error-reporting functions are filled in by Tasks 3–5» — ссылка на давно исполненный план, её надо снять.

- [ ] **Шаг 5: Переписать компиляцию**

```cpp
ChupaExpression* chupa_compile_expression(ChupaContext* ctx,
                                          const char* source, size_t len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto e = std::make_unique<::ChupaExpression>();

    CS::Diagnostic diag;
    const std::uint32_t errors = CS::Expression::compile(
        std::string_view(source, len), c->engine, &e->impl, &diag, 1);
    if (errors != 0) {
        c->setError(diag);
        return nullptr;   // unique_ptr уносит с собой всё, что успело завестись
    }
    c->clearError();
    return reinterpret_cast<ChupaExpression*>(e.release());
}
```

`chupa_compile_script` — то же самое с `CS::Script::compile`.

```cpp
void chupa_expression_destroy(ChupaExpression* e) {
    delete reinterpret_cast<::ChupaExpression*>(e);
}

void chupa_script_destroy(ChupaScript* s) {
    delete reinterpret_cast<::ChupaScript*>(s);
}
```

`delete nullptr` законен, поэтому проверки на null не нужно — но `CHUPA_NULLABLE` в заголовке обязателен, иначе Swift не даст передать `nil`.

- [ ] **Шаг 6: Переписать вычисление и запуск**

```cpp
namespace {

/// Перевод исхода ядра в исход C. Единственная работа, которая остаётся
/// прокладке после переезда: два перечисления об одном и том же.
ChupaStatus toStatus(CS::EvalStatus status) {
    switch (status) {
        case CS::EvalStatus::Ok:    return CHUPA_OK;
        case CS::EvalStatus::Null:  return CHUPA_NULL;
        case CS::EvalStatus::Error: return CHUPA_ERROR;
    }
    return CHUPA_ERROR;
}

}  // namespace

ChupaStatus chupa_eval_number(ChupaContext* ctx, ChupaExpression* e,
                              double* out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();
    return toStatus(expr->impl.evalNumber(c->engine, out, c->lastError));
}
```

`chupa_eval_bool` — так же через `evalBool`. `chupa_eval_string` пока оставь на старой подписи, переведя её на `expr->impl.evalString` во временную `std::string` и отдавая указатель внутрь неё **нельзя** — временная умрёт. На этот шаг переведи его на `expr->impl.eval` + ручной разбор, как было, и оставь метку UAF-1 на месте: задача 5 снимет и её, и подпись.

`chupa_run`:

```cpp
bool chupa_run(ChupaContext* ctx, ChupaScript* script) {
    if (!ctx || !script) { return false; }
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* s = reinterpret_cast<::ChupaScript*>(script);
    CS::Diagnostic diag;
    if (!s->impl.run(c->engine, diag)) {
        c->setError(diag);
        return false;
    }
    c->clearError();
    c->notifyRedraw();
    return true;
}
```

Внимание к `clearError`: `evalNumber` пишет прямо в `c->lastError`, поэтому чистить надо **до** вызова, а не после — иначе успешный путь затрёт диагностику, которую сам же и поставил. В старом коде чистка стояла после `evalExpression` и перед разбором типа; теперь это одно действие.

- [ ] **Шаг 7: Поправить существующие тесты**

`core/tests/c_api_test.cpp` нигде не освобождал единицы — теперь надо. Пройди по файлу и добавь `chupa_expression_destroy` / `chupa_script_destroy` перед каждым `chupa_context_destroy`. Без этого тесты пройдут, но задача 8 (ASan) покажет утечки.

- [ ] **Шаг 8: Прогнать тесты**

Run: `cmake --build build -j8 && ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Шаг 9: Коммит**

```bash
git add core/include core/src/c_api.cpp core/tests/c_api_test.cpp
git commit -m "refactor: единицами владеет хост, а не контекст

Из ChupaContext уходят четыре вектора владения, а с ними — утечка
(освободить одно выражение было нечем) и UAF-3 (адрес внутри
растущего вектора).

Обёртки стали однополевыми: вся работа переехала в CS::Expression
и CS::Script. Закрывает B35."
```

---

### Task 5: `ChupaString` — строка отдаётся хосту во владение

**Files:**
- Modify: `core/include/chupascript/chupascript.h:37-39` (новый typedef), `113-125` (подпись и комментарий)
- Modify: `core/src/c_api.cpp` (`chupa_eval_string`, три новые функции)
- Test: `core/tests/c_api_test.cpp`

**Interfaces:**
- Consumes: из задачи 3 — `CS::Expression::evalString(Store &, std::string *, Diagnostic &)`
- Produces:
  ```c
  typedef struct ChupaString ChupaString;
  CHUPA_API CHUPA_MUST_USE ChupaStatus
  chupa_eval_string(ChupaContext *ctx, ChupaExpression *e,
                    ChupaString *CHUPA_NULLABLE *out);
  CHUPA_API const char *chupa_string_bytes(const ChupaString *s,
                                           size_t *CHUPA_NULLABLE len);
  CHUPA_API void chupa_string_destroy(ChupaString *CHUPA_NULLABLE s);
  ```

- [ ] **Шаг 1: Написать падающие тесты**

```cpp
TEST(CApi, EvalStringHandsOverOwnership) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ChupaExpression* e = chupa_compile_expression(ctx, "'привет'", 15);
    ASSERT_NE(e, nullptr);

    ChupaString* s = nullptr;
    ASSERT_EQ(chupa_eval_string(ctx, e, &s), CHUPA_OK);
    ASSERT_NE(s, nullptr);

    size_t len = 0;
    const char* bytes = chupa_string_bytes(s, &len);
    EXPECT_EQ(std::string(bytes, len), "привет");

    chupa_string_destroy(s);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, EvalStringSurvivesStoreMutation) {
    // Это и есть UAF-1: раньше указатель смотрел внутрь пула движка, и любая
    // следующая операция над контекстом могла его подвесить.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ChupaExpression* e = chupa_compile_expression(ctx, "'привет'", 15);
    ASSERT_NE(e, nullptr);

    ChupaString* s = nullptr;
    ASSERT_EQ(chupa_eval_string(ctx, e, &s), CHUPA_OK);

    // Растим пул текста так, чтобы он заведомо переехал.
    for (int i = 0; i < 1000; ++i) {
        chupa_context_set_string(ctx, "filler", 6,
                                 "довольно длинная строка для роста пула", 68);
    }

    size_t len = 0;
    const char* bytes = chupa_string_bytes(s, &len);
    EXPECT_EQ(std::string(bytes, len), "привет");  // байты наши, не движка

    chupa_string_destroy(s);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, EvalStringOnNullLeavesOutUntouched) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ChupaExpression* e = chupa_compile_expression(ctx, "null", 4);
    ASSERT_NE(e, nullptr);

    ChupaString* s = nullptr;
    EXPECT_EQ(chupa_eval_string(ctx, e, &s), CHUPA_NULL);
    EXPECT_EQ(s, nullptr);  // отдавать нечего — и не отдано

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, EvalStringOnNumberIsTypeError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ChupaExpression* e = chupa_compile_expression(ctx, "42", 2);
    ASSERT_NE(e, nullptr);

    ChupaString* s = nullptr;
    EXPECT_EQ(chupa_eval_string(ctx, e, &s), CHUPA_ERROR);
    EXPECT_EQ(s, nullptr);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_TYPE);

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, StringDestroyAcceptsNull) {
    chupa_string_destroy(nullptr);
}

TEST(CApi, StringBytesAcceptsNullLength) {
    ChupaContext* ctx = chupa_context_create();
    ChupaExpression* e = chupa_compile_expression(ctx, "'ok'", 4);
    ASSERT_NE(e, nullptr);
    ChupaString* s = nullptr;
    ASSERT_EQ(chupa_eval_string(ctx, e, &s), CHUPA_OK);
    EXPECT_STREQ(chupa_string_bytes(s, nullptr), "ok");
    chupa_string_destroy(s);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}
```

Длины строковых литералов в байтах (`"'привет'"` — 15, а не 8) проверь сам: кириллица в UTF-8 занимает по два байта. Проще и надёжнее писать `std::string_view src = "'привет'"; ... chupa_compile_expression(ctx, src.data(), src.size())`.

- [ ] **Шаг 2: Прогнать и убедиться, что не собирается**

Run: `cmake --build build -j8`
Expected: FAIL — `unknown type name 'ChupaString'`.

- [ ] **Шаг 3: Поменять заголовок**

К трём typedef'ам (`chupascript.h:37-39`) добавить четвёртый:

```c
typedef struct ChupaString     ChupaString;
```

Блок UAF-1 (`chupascript.h:113-122`) заменить целиком:

```c
/* Evaluates the expression as a string.
 *
 * On CHUPA_OK, *out receives a ChupaString the CALLER now owns and must
 * release with chupa_string_destroy. On CHUPA_NULL and CHUPA_ERROR, *out is
 * left untouched and there is nothing to destroy. */
CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_string(ChupaContext *ctx, ChupaExpression *e,
                  ChupaString *CHUPA_NULLABLE *out);

/* The bytes are valid until chupa_string_destroy and not one moment longer.
 * They are not NUL-terminated by contract; pass len if you need the length.
 * Never freed by the caller — the ChupaString owns them. */
CHUPA_API const char *chupa_string_bytes(const ChupaString *s,
                                         size_t *CHUPA_NULLABLE len);

CHUPA_API void chupa_string_destroy(ChupaString *CHUPA_NULLABLE s);
```

Замечание про NUL: `std::string::c_str()` терминатор даёт всегда, так что `chupa_string_bytes` фактически вернёт строку с нулём на конце. Но в контракт этого писать не надо — иначе внутреннее устройство станет обещанием, а именно ради свободы его менять (пул строк, §5 спеки) весь этот тип и заводится. Тест `StringBytesAcceptsNullLength` пользуется `EXPECT_STREQ` и на этой фактической терминации стоит; если она тебя смущает — перепиши его на `chupa_string_bytes(s, &len)` и сравнение по длине.

- [ ] **Шаг 4: Реализовать в `c_api.cpp`**

```cpp
/// Строка, отданная хосту во владение (спека Р6).
///
/// Однополевая обёртка: наружу видно только имя типа, внутри — обычная
/// std::string. Отдельное выделение на строку — сознательный долг; сменить
/// его на пул внутри контекста можно не трогая ни заголовок, ни Swift.
struct ChupaString { std::string text; };

ChupaStatus chupa_eval_string(ChupaContext* ctx, ChupaExpression* e,
                              ChupaString** out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);

    std::string text;
    c->clearError();
    const CS::EvalStatus status = expr->impl.evalString(c->engine, &text,
                                                        c->lastError);
    if (status != CS::EvalStatus::Ok) { return toStatus(status); }

    auto* s = new (std::nothrow) ::ChupaString{std::move(text)};
    if (s == nullptr) {
        c->setError({CS::ErrorCode::Memory, 0, "eval_string: out of memory"});
        return CHUPA_ERROR;
    }
    *out = reinterpret_cast<ChupaString*>(s);
    return CHUPA_OK;
}

const char* chupa_string_bytes(const ChupaString* s, size_t* len) {
    const auto* impl = reinterpret_cast<const ::ChupaString*>(s);
    if (len) { *len = impl->text.size(); }
    return impl->text.c_str();
}

void chupa_string_destroy(ChupaString* s) {
    delete reinterpret_cast<::ChupaString*>(s);
}
```

Метку UAF-1 (`c_api.cpp:367-394`) удалить целиком: описанного ею дефекта больше нет.

- [ ] **Шаг 5: Прогнать тесты**

Run: `cmake --build build -j8 && ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Шаг 6: Коммит**

```bash
git add core/include core/src/c_api.cpp core/tests/c_api_test.cpp
git commit -m "feat: ChupaString — строка отдаётся хосту во владение

Раньше наружу уходил указатель внутрь пула движка с неописанным
окном валидности: любая следующая операция над контекстом могла
его подвесить (UAF-1, B37).

Теперь наружу уходит объект во владении хоста, а окно его байтов
определено и подчинено самому хосту. Цена — выделение на строку;
записана в спеке как долг с готовым выходом."
```

---

### Task 6: CLI переходит на сущности

**Files:**
- Modify: `cli/main.cpp:16-19` (включения), `45-85` (`runCode`)
- Test: ручной прогон REPL; автотестов у CLI нет

**Interfaces:**
- Consumes: `CS::Expression`, `CS::Script` из задач 2 и 3

- [ ] **Шаг 1: Разнести `runCode` на две функции**

Заменить `#include "compile.hpp"` и `#include "eval.hpp"` на `#include "expression.hpp"` и `#include "script.hpp"`; `#include "ast.hpp"` убрать.

Сегодня `runCode` расходится по флагу `asScript` в трёх местах из четырёх, а после переезда у веток стали разными и типы. Разнеси её надвое, а решение по флагу подними к вызывающему (`cli/main.cpp:214`).

```cpp
/// Общая часть обеих веток: напечатать находки компиляции.
/// Возвращает true, если компиляция удалась.
bool reportCompile(std::uint32_t errors, std::string_view source,
                   std::uint32_t indent, const CS::Diagnostic *found,
                   std::uint32_t capacity) {
    if (errors == 0) { return true; }
    const std::uint32_t shown = errors < capacity ? errors : capacity;
    for (std::uint32_t i = 0; i < shown; ++i) {
        chupa::reportDiagnostic(std::cout, source, indent, found[i]);
    }
    return false;
}

void runExpression(CS::Store &store, std::string_view source,
                   std::uint32_t indent) {
    CS::Diagnostic found[kMaxDiagnostics];
    CS::Expression expr;
    const std::uint32_t errors =
        CS::Expression::compile(source, store, &expr, found, kMaxDiagnostics);
    if (!reportCompile(errors, source, indent, found, kMaxDiagnostics)) { return; }

    // Сырой путь, а не evalString: CLI печатает null наравне со всем прочим
    // и умеет агрегаты (cli/printer.cpp:93-106). Трёхзначность ему мешала бы.
    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    if (!expr.eval(store, &out, diag)) {
        chupa::reportDiagnostic(std::cout, source, indent, diag);
        return;
    }
    // ... печать результата — как сейчас, cli/main.cpp:82-85 ...
}

void runScriptSource(CS::Store &store, std::string_view source,
                     std::uint32_t indent) {
    CS::Diagnostic found[kMaxDiagnostics];
    CS::Script script;
    const std::uint32_t errors =
        CS::Script::compile(source, store, &script, found, kMaxDiagnostics);
    if (!reportCompile(errors, source, indent, found, kMaxDiagnostics)) { return; }

    // Скрипт исполняется ради действия, значения у него нет.
    CS::Diagnostic diag;
    if (!script.run(store, diag)) {
        chupa::reportDiagnostic(std::cout, source, indent, diag);
    }
}
```

Имя `runScriptSource`, а не `runScript`: свободная функция `CS::runScript` уже есть, и одинаковые имена в соседних областях читаются хуже, чем чуть длиннее.

`kMaxDiagnostics` и точный вид отчёта об ошибках возьми из нынешнего `cli/main.cpp:48-64` — приведённый здесь `reportCompile` пересказывает его по смыслу, но имя константы и обрезание по `capacity` там могут быть устроены иначе. Не выдумывай своё.

У вызывающего (`cli/main.cpp:214`) вместо `runCode(store, source, indent, isScript)`:

```cpp
        if (isScript) {
            runScriptSource(store, source, indent);
        } else {
            runExpression(store, source, indent);
        }
```

- [ ] **Шаг 2: Собрать и прогнать вручную**

Run: `cmake --build build -j8 && echo -e "set user = {'name': 'Вася'}\nexpr: user.name\nscript: user.name = 'Петя';\nexpr: user.name\nexpr: user..name" | ./build/cli/chupascript`
Expected: `Вася`, затем `Петя`, затем синтаксическая ошибка с подчёркиванием на нужном месте. Команды REPL посмотри в `cli/main.cpp:95` и подставь настоящие — приведённые здесь могут не совпасть.

- [ ] **Шаг 3: Прогнать тесты**

Run: `ctest --test-dir build --output-on-failure`
Expected: PASS.

- [ ] **Шаг 4: Коммит**

```bash
git add cli/main.cpp
git commit -m "refactor: CLI на CS::Expression и CS::Script

Автор CLI получает те же средства, что автор на Swift: держать
CS::Ast собственным полем и повторять работу прокладки больше
не нужно."
```

---

### Task 7: Swift теряет префикс и переходит на новый контракт строки

**Files:**
- Modify: `swift/ChupaContext.swift`, `swift/ChupaExpression.swift`, `swift/ChupaScript.swift`, `swift/ChupaError.swift`, и пятый файл в `swift/` — посмотри список сам
- Rename: файлы вслед за типами (`git mv`)

**Interfaces:**
- Consumes: C API после задач 4 и 5

**Проверяется только чтением.** Собрать Swift сейчас нечем — ни `Package.swift`, ни проекта, ни тестов (`docs/backlog.md` B40). Это записано как известный пробел, а не упущение; работа по B40 идёт отдельным планом следом.

- [ ] **Шаг 1: Переименовать типы**

| было | стало |
|---|---|
| `ChupaContext` | `Context` |
| `ChupaExpression` | `Expression` |
| `ChupaScript` (класс) | `Script` |
| `ChupaContextDelegate` | `ContextDelegate` |
| `ChupaError` | `ChupaScript.Error` — см. ниже |

Про `Error`: имя занято протоколом стандартной библиотеки Swift, и тип с таким именем внутри модуля его затенит. Возьми `Error` и внутри модуля пиши `Swift.Error` там, где имелся в виду протокол. Если по ходу окажется, что тип у нас всегда об одном из двух — компиляции или вычислении, — возьми `CompileError` / `EvalError` вместо одного `Error`; спека это разрешает явно.

Файлы переименуй вслед за типами через `git mv`, чтобы история осталась связной.

- [ ] **Шаг 2: Освободить единицы**

У `Expression` и `Script` появляется `deinit`:

```swift
deinit {
    chupa_expression_destroy(handle)
}
```

Раньше их освобождал контекст, и делал это только в момент своей смерти. Теперь владелец — Swift-объект, и хранить `internal let context: Context` ради времени жизни больше не нужно. Держать ссылку на контекст всё равно надо — методы вычисления в него ходят, — но это ссылка ради работы, а не ради времени жизни; поправь комментарий, если он утверждает обратное (`swift/ChupaScript.swift`).

- [ ] **Шаг 3: Перевести `evalString` на `ChupaString`**

```swift
public func evalString() throws -> String? {
    var raw: OpaquePointer?
    switch chupa_eval_string(context.handle, handle, &raw) {
    case CHUPA_OK:
        guard let raw else { return nil }
        defer { chupa_string_destroy(raw) }
        var len = 0
        guard let bytes = chupa_string_bytes(raw, &len) else { return nil }
        return String(decoding: UnsafeRawBufferPointer(start: bytes, count: len),
                      as: UTF8.self)
    case CHUPA_NULL:
        return nil
    default:
        throw context.lastError()
    }
}
```

`defer` здесь обязателен: без него ранний выход из-за некорректного UTF-8 утёк бы строкой. Имя `context.lastError()` — по нынешнему коду; посмотри, как ошибка достаётся сегодня, и не выдумывай новый способ.

- [ ] **Шаг 4: Перечитать написанное**

Компилятора нет, поэтому единственная проверка — чтение. Пройди по всем файлам `swift/` и убедись: не осталось ни одного `Chupa`-префикса в именах Swift-типов; каждый `chupa_*_destroy` вызывается ровно один раз на объект; ни один указатель не переживает свой `destroy`.

- [ ] **Шаг 5: Коммит**

```bash
git add swift
git commit -m "refactor: Swift-типы теряют префикс Chupa

Модуль в Swift и есть пространство имён — префиксы остались от
Objective-C, где их не было. Заодно снимается затенение: Swift-класс
ChupaContext носил то же имя, что C-структура.

Собрать это нечем (B40) — проверено чтением, пробел записан."
```

---

### Task 8: Приёмка под ASan и UBSan

**Files:**
- Не меняется ничего. Если что-то всплывёт — правится там, где всплыло, и коммитится отдельно.

- [ ] **Шаг 1: Собрать отдельным каталогом**

```bash
cmake -B build-asan -DCMAKE_BUILD_TYPE=Debug \
      -DCHUPASCRIPT_SANITIZE_ADDRESS=ON \
      -DCHUPASCRIPT_SANITIZE_UNDEFINED=ON \
      -DCHUPASCRIPT_BUILD_TESTS=ON
cmake --build build-asan -j8
```

Точные имена опций — в `CMakeLists.txt:47-91`; они ни разу не запускались, так что вполне возможно, что сборка с ними сама по себе не пройдёт. Это тоже находка, и чинить её — часть задачи.

- [ ] **Шаг 2: Прогнать**

```bash
ASAN_OPTIONS=detect_leaks=1 ctest --test-dir build-asan --output-on-failure
```

Expected: PASS, ноль отчётов санитайзеров, ноль утечек.

На macOS `detect_leaks` у ASan может быть недоступен — тогда проверь утечки отдельно через `leaks --atExit -- ./build-asan/core/tests/chupascript_tests` (имя цели посмотри в `core/tests/CMakeLists.txt`).

- [ ] **Шаг 3: Убедиться, что санитайзер вообще работает**

Прогон без единой находки ничего не доказывает, пока не показано, что находки он показывать умеет. Временно верни UAF-3: в `Expression::compile` замени копию исходника на `string_view` на параметр и скомпилируй два коротких выражения подряд.

Expected: ASan печатает `heap-use-after-free`.

Верни правку назад (`git checkout -- core/src/expression.cpp`) и перепроверь, что прогон снова чист.

- [ ] **Шаг 4: Записать результат**

Добавь в `docs/backlog.md` строку о том, что прогон под санитайзерами состоялся и с какими находками; если находок не было — так и напиши. Если санитайзеры пришлось чинить, чтобы они собрались, — это отдельная запись.

- [ ] **Шаг 5: Коммит**

```bash
git add docs/backlog.md
git commit -m "test: прогон под ASan и UBSan

Условие приёмки рефакторинга: до него отличить починенный UAF от
переехавшего было нечем. Санитайзеры проверены на заведомой
поломке — находки они показывают."
```

---

### Task 9: Замер и сравнение с базой

**Files:**
- Create: `docs/benchmarks/after-refactor-2026-08-15.json`
- Modify: `docs/superpowers/specs/2026-08-15-chupascript-core-entities-design.md` (§5, цифра долга)

База снята до работ и лежит в `docs/benchmarks/baseline-2026-08-15.json`. Ключевые строки на этой машине:

| строка | база |
|---|---|
| `BM_Eval_ShortPath` | 31.8 нс |
| `BM_Eval_DeepPath` | 61.0 нс |
| `BM_Eval_Arithmetic` | 87.7 нс |
| `BM_Eval_Format` | 132.9 нс |
| `BM_Check_Props` | 107.6 нс |

- [ ] **Шаг 1: Пересобрать и померить тем же способом**

```bash
cmake --build build-rel -j8
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_min_time=0.3s --benchmark_repetitions=5 \
    --benchmark_report_aggregates_only=true \
    --benchmark_out_format=json \
    --benchmark_out=docs/benchmarks/after-refactor-2026-08-15.json
```

Машина должна быть та же и по возможности в том же состоянии: сравнивать замеры с разных прогонов под нагрузкой бессмысленно.

- [ ] **Шаг 2: Сравнить**

```bash
python3 build-rel/_deps/googlebenchmark-src/tools/compare.py benchmarks \
    docs/benchmarks/baseline-2026-08-15.json \
    docs/benchmarks/after-refactor-2026-08-15.json
```

Путь к `compare.py` проверь: он приходит вместе с Google Benchmark и может лежать иначе. Если его нет — сравни медианы вручную по двум JSON.

- [ ] **Шаг 3: Разобрать расхождения**

Ожидания, против которых читается результат:

- **`BM_Eval_*` — в пределах шума.** Р3 меняет чтение `src_` из памяти на регистр и добавляет два регистра параметра в рекурсию. Если какая-то строка просела больше чем на 5%, это не «цена решения», а повод посмотреть, не потерялся ли inline у `Ast::text` — она была `noexcept` и определена в `.cpp`, и лишний параметр мог сдвинуть решение компилятора.
- **`BM_Check_*` — в пределах шума** по той же причине.
- **Компиляция** замерами не покрыта вовсе: `BM_Parse_*` меряет разбор, а копию исходника (Р2) никто не меряет. Заводить строку под неё сейчас не надо — она вне перерисовки, что и записано в спеке Р2.
- **Строковый путь** замерами тоже не покрыт: `chupa_eval_string` в бенчмарках не участвует. Цена `ChupaString` — одно-два выделения на строку — известна из спеки и вынесена в долг; мерить её имеет смысл тогда же, когда будет что с чем сравнивать, то есть при заведении пула.

- [ ] **Шаг 4: Записать результат**

В §5 спеки, в абзаце про новый долг, замени общее «десятки пар malloc/free на кадр» на измеренное, если задача 3 дала повод его измерить. Если нет — оставь как есть и допиши одной строкой, что горячий путь замерами проверен и не просел, со ссылкой на оба файла в `docs/benchmarks/`.

- [ ] **Шаг 5: Коммит**

```bash
git add docs/benchmarks docs/superpowers/specs
git commit -m "test: замер после рефакторинга против базы

База снята до работ, замер после — тем же способом на той же
машине. Горячий путь не просел: Р3 меняет чтение указателя из
памяти на регистр."
```

---

## Что этот план не делает

- **B38 (UAF-2), колбэк перерисовки.** Отдельная тема, вместе с B36.
- **B40, сборка и тесты Swift.** Отдельный план следом; Swift здесь проверяется чтением, и это записано.
- **B34, регистрация хост-функций.** Отдельная работа; на неё этот рефакторинг только опирается — Р6 упоминает её как повод не выбирать буфер вызывающего.
- **Пул строк внутри контекста.** Заведён как долг в §5 спеки. Признак, что пора: строковые props всплыли в профиле перерисовки.
