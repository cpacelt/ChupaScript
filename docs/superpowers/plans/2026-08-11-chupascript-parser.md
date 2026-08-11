# ChupaScript: парсер и черновик AST — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Превратить поток токенов лексера в дерево разбора по `docs/grammar.md` §5, отвергая ранние ошибки §5.5, с полным покрытием тестами и защитой от деградации производительности.

**Architecture:** Рекурсивный спуск, построчно транскрибирующий правила §5.3. Хранение дерева спрятано за строителем и аксессорами `Ast`, поэтому все решения о памяти (`docs/backlog.md` B6–B10) откладываются и меняют только `ast.cpp`. Узел не владеет ничем: имена и литералы — срезы исходного буфера. Ошибка возвращается признаком `false` и заполняет `Diagnostic` — та же схема, что у лексера.

**Tech Stack:** C++17, CMake ≥ 3.20, GoogleTest 1.15.2, Google Benchmark 1.9.1, Apple clang / libc++.

## Порядок работы

Три фазы, порядок обязателен:

1. **Интерфейсы и тесты** (задачи 1–3): заголовки и заглушки, полный набор тестов по спецификации, бенчмарки. После фазы проект собирается, тесты красные.
2. **Реализация порциями** (задачи 4–8): каждая порция зеленит свою группу тестов и обязана не ухудшить базы предыдущих порций.
3. **Замыкание** (задача 9): санитайзеры, полный прогон, фиксация базы.

## Global Constraints

- **Стандарт:** C++17, без расширений (`CMAKE_CXX_EXTENSIONS OFF`).
- **Пространство имён:** `CS`.
- **Размещение:** внутренние заголовки в `core/src/`. Парсер внутренний, в `core/include/chupascript/chupascript.h` ничего не добавляется.
- **Предупреждения:** `-Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast -Wcast-align -Wunused -Wdouble-promotion -Wformat=2`. Приведения только `static_cast`.
- **Комментарии по-русски, тексты диагностик по-английски** — как в существующем `core/src/lexer.cpp` (`"unterminated string literal"`, `"malformed numeric literal"`).
- **`noexcept` только там, где нет аллокаций.** Аксессоры `Ast` — `noexcept`; строитель `Ast` и функции разбора — **нет**: черновик аллоцирует (`std::vector`), и `noexcept` превратил бы нехватку памяти в `std::terminate`. Изоляция исключений от C-границы — задача слоя компиляции, не этого.
- **Прямой доступ к `Node` вне `ast.cpp` запрещён**, включая тесты и бенчмарки. Читать дерево можно только через аксессоры `Ast`. Нарушение этого правила уничтожает шов, ради которого написан весь слой, и является дефектом, а не стилистикой.
- **Смещения — `std::uint32_t`.** Исходник длиннее 4 ГиБ не поддерживается.
- **Каждое решение о хранении помечено в коде** комментарием `// TODO(B<N>)` со ссылкой на `docs/backlog.md`.
- **Сборка тестов:** `cmake -B build && cmake --build build -j`, прогон `ctest --test-dir build --output-on-failure`.
- **Сборка бенчмарков:** только Release — `cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON`.

## Решение сверх спецификации: предел глубины рекурсии

Спецификация о глубине рекурсии молчит, и без предела тексты вида `((((((…`,
`!!!!!!…` и `1 ?? 1 ?? 1 ?? …` роняют процесс переполнением стека. Вход
недоверенный: макет приходит с бэкенда.

Вводится `kMaxDepth = 96`. Счётчик увеличивается на входе в **три** правила —
`ternary()`, `nilCoalesce()` и `unary()`. Это ровно те правила цепочки §5.3,
которые рекурсируют сами в себя: `ternary()` — через скобки, индекс, аргумент,
элемент литерала и ветви `? :`; `nilCoalesce()` — по цепочке `??`;
`unary()` — по цепочке префиксных `!` и `-`. Остальные правила используют цикл
и глубину не наращивают.

Отсюда цена: один уровень вложенности скобок стоит трёх единиц (проход по
цепочке задевает все три охраняемых правила), а звено цепочки `??` или `!` —
одной. 96 единиц — это около 320 кадров стека в худшем случае, то есть меньше
сотни килобайт при 512 КиБ стека вторичного потока. Двадцати уровней
вложенности в осмысленном макете не бывает; выражение props глубже пяти не
встречается.

Превышение — ошибка `ErrorCode::Syntax` «expression nesting too deep».

**Это единственное место, где план выходит за спецификацию.** Если предел не
нужен — удаляется вместе с четырьмя тестами `ParserLimits`, и в §5 спеки
править ничего не надо, потому что там его нет.

## Структура файлов

| Файл | Ответственность |
|---|---|
| `core/src/ast.hpp` | `NodeKind`, `Node`, `NodeId`, класс `Ast` — хранение, строитель, аксессоры |
| `core/src/ast.cpp` | тела строителя и аксессоров |
| `core/src/parser.hpp` | две функции разбора — вся публичная поверхность слоя |
| `core/src/parser.cpp` | рекурсивный спуск; класс `Parser` в анонимном пространстве имён |
| `core/tests/ast_test.cpp` | тесты строителя и аксессоров, без парсера |
| `core/tests/parser_test.cpp` | тесты разбора по `docs/grammar.md` §5 |
| `benchmarks/parser_benchmark.cpp` | шесть бенчмарков по подмножествам грамматики |

---

## Task 1: Интерфейсы и сборка

**Files:**
- Create: `core/src/ast.hpp`, `core/src/ast.cpp`, `core/src/parser.hpp`, `core/src/parser.cpp`
- Modify: `core/CMakeLists.txt`

**Interfaces:**
- Consumes: `CS::Token`, `CS::TokenKind` из `core/src/token.hpp`; `CS::Diagnostic`, `CS::ErrorCode` из `core/src/diagnostic.hpp`.
- Produces: всё содержимое `ast.hpp` и `parser.hpp`, перечисленное ниже. Задачи 2–9 опираются на эти сигнатуры дословно.

- [ ] **Step 1: Создать `core/src/ast.hpp`**

```cpp
#pragma once
#include <cstdint>
#include <string_view>
#include <vector>

#include "token.hpp"

namespace CS {

/// Вид узла дерева разбора. Соответствует docs/grammar.md §5.
enum class NodeKind : std::uint8_t {
    Invalid,  ///< узел с индексом kNoNode; в готовом дереве не встречается

    Program,        ///< дети: стейтменты
    Assign,         ///< дети: цель, значение; op — один из = += -= *= /=
    CallStatement,  ///< дети: вызов

    Conditional,  ///< дети: условие, ветвь-да, ветвь-нет
    Binary,       ///< дети: левый, правый; op — оператор
    Unary,        ///< дети: операнд; op — ! либо -
    Index,        ///< дети: база, индекс
    Member,       ///< дети: база; текст — имя поля
    Call,         ///< дети: аргументы; текст — имя функции

    Identifier,  ///< текст — имя
    Number,      ///< number — значение
    String,      ///< текст — содержимое без кавычек; hasEscape
    Boolean,     ///< boolean — значение
    Null,
    Array,   ///< дети: элементы
    Object   ///< дети: чередование ключ, значение, ключ, значение
};

using NodeId = std::uint32_t;

/// Отсутствие узла. Индекс 0 занят узлом-пустышкой вида Invalid.
inline constexpr NodeId kNoNode = 0;

/// Узел дерева.
///
/// Черновик: поля не пересекаются, узел заведомо толстый.
/// Упаковка отложена — см. docs/backlog.md B6.
///
/// Читать напрямую нельзя нигде, кроме ast.cpp: единственный доступ — через
/// аксессоры Ast. Это шов, ради которого решения B6–B10 остаются отложенными.
struct Node {
    NodeKind kind = NodeKind::Invalid;
    TokenKind op = TokenKind::End;
    std::uint32_t offset = 0;
    std::uint32_t childStart = 0;
    std::uint32_t childCount = 0;
    std::uint32_t textOffset = 0;
    std::uint32_t textLength = 0;
    double number = 0.0;
    bool boolean = false;
    bool hasEscape = false;
};

/// Дерево разбора: хранение, строитель, аксессоры.
///
/// Ничем не владеет: текст имён и литералов — срезы исходного буфера, который
/// обязан пережить Ast (docs/backlog.md B12).
class Ast {
   public:
    Ast();

    /// Запоминает буфер, из которого аксессор text() режет имена и литералы.
    /// Длина не хранится: смещения приходят из токенов того же буфера.
    void setSource(const char *source) noexcept;

    /// Объявляет узел корнем дерева.
    void setRoot(NodeId node) noexcept;

    // ─── строитель: единственный способ создать узел ───

    NodeId number(const Token &token);
    NodeId string(const Token &token);
    NodeId boolean(const Token &token);
    NodeId null(const Token &token);
    NodeId identifier(const Token &token);
    NodeId member(NodeId base, const Token &name);
    NodeId index(NodeId base, NodeId subscript, std::uint32_t offset);
    NodeId unary(TokenKind op, NodeId operand, std::uint32_t offset);
    NodeId binary(TokenKind op, NodeId lhs, NodeId rhs, std::uint32_t offset);
    NodeId conditional(NodeId condition, NodeId whenTrue, NodeId whenFalse,
                       std::uint32_t offset);
    NodeId call(const Token &name, const NodeId *args, std::uint32_t count);
    NodeId array(const NodeId *items, std::uint32_t count, std::uint32_t offset);
    NodeId object(const NodeId *pairs, std::uint32_t count, std::uint32_t offset);
    NodeId assign(TokenKind op, NodeId target, NodeId value, std::uint32_t offset);
    NodeId callStatement(NodeId callNode, std::uint32_t offset);
    NodeId program(const NodeId *statements, std::uint32_t count);

    // ─── аксессоры: единственный способ прочитать узел ───

    /// Корень дерева; kNoNode, если разбор не удался.
    [[nodiscard]] NodeId root() const noexcept;

    [[nodiscard]] NodeKind kind(NodeId node) const noexcept;
    [[nodiscard]] TokenKind op(NodeId node) const noexcept;
    [[nodiscard]] std::uint32_t offset(NodeId node) const noexcept;

    [[nodiscard]] std::uint32_t childCount(NodeId node) const noexcept;
    /// kNoNode, если index за границей диапазона детей.
    [[nodiscard]] NodeId child(NodeId node, std::uint32_t index) const noexcept;

    [[nodiscard]] double numberValue(NodeId node) const noexcept;
    [[nodiscard]] bool boolValue(NodeId node) const noexcept;
    /// Имя либо содержимое строкового литерала без кавычек, сырыми байтами.
    [[nodiscard]] std::string_view text(NodeId node) const noexcept;
    [[nodiscard]] bool hasEscape(NodeId node) const noexcept;

    /// Число узлов, включая пустышку с индексом kNoNode. Для тестов и замеров.
    [[nodiscard]] std::uint32_t nodeCount() const noexcept;

   private:
    NodeId add(const Node &node);
    std::uint32_t pushChildren(const NodeId *ids, std::uint32_t count);

    const char *src_ = nullptr;
    NodeId root_ = kNoNode;
    std::vector<Node> nodes_;      // TODO(B7): переехать в арену контекста
    std::vector<NodeId> children_; // TODO(B10): боковой пул детей
};

}  // namespace CS
```

- [ ] **Step 2: Создать `core/src/ast.cpp` с заглушками**

Задача 4 заменит тела целиком. Сейчас нужно только чтобы собиралось и линковалось.

```cpp
#include "ast.hpp"

namespace CS {

Ast::Ast() { nodes_.push_back(Node{}); }

void Ast::setSource(const char *source) noexcept { src_ = source; }

void Ast::setRoot(NodeId node) noexcept { root_ = node; }

NodeId Ast::add(const Node &) { return kNoNode; }
std::uint32_t Ast::pushChildren(const NodeId *, std::uint32_t) { return 0; }

NodeId Ast::number(const Token &) { return kNoNode; }
NodeId Ast::string(const Token &) { return kNoNode; }
NodeId Ast::boolean(const Token &) { return kNoNode; }
NodeId Ast::null(const Token &) { return kNoNode; }
NodeId Ast::identifier(const Token &) { return kNoNode; }
NodeId Ast::member(NodeId, const Token &) { return kNoNode; }
NodeId Ast::index(NodeId, NodeId, std::uint32_t) { return kNoNode; }
NodeId Ast::unary(TokenKind, NodeId, std::uint32_t) { return kNoNode; }
NodeId Ast::binary(TokenKind, NodeId, NodeId, std::uint32_t) { return kNoNode; }
NodeId Ast::conditional(NodeId, NodeId, NodeId, std::uint32_t) { return kNoNode; }
NodeId Ast::call(const Token &, const NodeId *, std::uint32_t) { return kNoNode; }
NodeId Ast::array(const NodeId *, std::uint32_t, std::uint32_t) { return kNoNode; }
NodeId Ast::object(const NodeId *, std::uint32_t, std::uint32_t) { return kNoNode; }
NodeId Ast::assign(TokenKind, NodeId, NodeId, std::uint32_t) { return kNoNode; }
NodeId Ast::callStatement(NodeId, std::uint32_t) { return kNoNode; }
NodeId Ast::program(const NodeId *, std::uint32_t) { return kNoNode; }

NodeId Ast::root() const noexcept { return root_; }
NodeKind Ast::kind(NodeId) const noexcept { return NodeKind::Invalid; }
TokenKind Ast::op(NodeId) const noexcept { return TokenKind::End; }
std::uint32_t Ast::offset(NodeId) const noexcept { return 0; }
std::uint32_t Ast::childCount(NodeId) const noexcept { return 0; }
NodeId Ast::child(NodeId, std::uint32_t) const noexcept { return kNoNode; }
double Ast::numberValue(NodeId) const noexcept { return 0.0; }
bool Ast::boolValue(NodeId) const noexcept { return false; }
std::string_view Ast::text(NodeId) const noexcept { return {}; }
bool Ast::hasEscape(NodeId) const noexcept { return false; }
std::uint32_t Ast::nodeCount() const noexcept {
    return static_cast<std::uint32_t>(nodes_.size());
}

}  // namespace CS
```

- [ ] **Step 3: Создать `core/src/parser.hpp`**

```cpp
#pragma once
#include <cstdint>

#include "ast.hpp"
#include "diagnostic.hpp"

namespace CS {

/// Разбирает выражение — стартовый символ Expression, docs/grammar.md §5.1.
///
/// При успехе возвращает true, заполняет ast и ставит ast.root().
/// При отказе возвращает false, заполняет diag, ast.root() остаётся kNoNode.
///
/// Буфер source обязан пережить ast: имена и литералы — срезы этого буфера
/// (docs/backlog.md B12).
bool parseExpression(const char *source, std::uint32_t length, Ast &ast,
                     Diagnostic &diag);

/// Разбирает программу — стартовый символ Program, docs/grammar.md §5.1.
///
/// Контракт совпадает с parseExpression. Пустой исходник даёт корень Program
/// без детей и не является ошибкой.
bool parseProgram(const char *source, std::uint32_t length, Ast &ast,
                  Diagnostic &diag);

}  // namespace CS
```

- [ ] **Step 4: Создать `core/src/parser.cpp` с заглушками**

```cpp
#include "parser.hpp"

namespace CS {

bool parseExpression(const char *source, std::uint32_t length, Ast &ast,
                     Diagnostic &diag) {
    ast.setSource(source);
    static_cast<void>(length);
    diag = Diagnostic{ErrorCode::Syntax, 0, "expression parsing not implemented"};
    return false;
}

bool parseProgram(const char *source, std::uint32_t length, Ast &ast,
                  Diagnostic &diag) {
    ast.setSource(source);
    static_cast<void>(length);
    diag = Diagnostic{ErrorCode::Syntax, 0, "program parsing not implemented"};
    return false;
}

}  // namespace CS
```

- [ ] **Step 5: Подключить новые единицы трансляции**

В `core/CMakeLists.txt` заменить список источников:

```cmake
add_library(chupascript STATIC
    src/ast.cpp
    src/lexer.cpp
    src/parser.cpp
    src/version.cpp
)
```

- [ ] **Step 6: Собрать**

Run: `cmake -B build && cmake --build build -j`
Expected: сборка проходит, предупреждений нет.

- [ ] **Step 7: Прогнать существующие тесты**

Run: `ctest --test-dir build --output-on-failure`
Expected: 53 теста лексера проходят, новых тестов пока нет.

- [ ] **Step 8: Коммит**

```bash
git add core/src/ast.hpp core/src/ast.cpp core/src/parser.hpp core/src/parser.cpp core/CMakeLists.txt
git commit -m "Add parser and AST interfaces"
```

---

## Task 2: Тесты

**Files:**
- Create: `core/tests/ast_test.cpp`, `core/tests/parser_test.cpp`
- Modify: `core/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: весь `ast.hpp` и `parser.hpp` из задачи 1.
- Produces: вспомогательные функции `dump`, `parseExpr`, `parseProg` внутри `parser_test.cpp` — они локальны файлу, другие задачи их не используют.

**Ключевая идея тестов.** Утверждение вида «корень — `Binary`» пропускает ровно ту ошибку, ради которой писался тест. Поэтому форма дерева проверяется целиком: функция `dump` печатает дерево S-выражением, и тест сравнивает строку. `a + b * c` обязан дать `(+ a (* b c))`, а не «корень плюс».

Негативные тесты утверждают `diag.code` **и** `diag.offset`. Смещение — единственное, что делает диагностику полезной, и единственное, что легко сломать незаметно.

- [ ] **Step 1: Создать `core/tests/ast_test.cpp`**

```cpp
// Тесты строителя и аксессоров Ast. Парсер здесь не участвует.
#include <gtest/gtest.h>

#include <cstdint>
#include <string>

#include "ast.hpp"
#include "token.hpp"

namespace {

using CS::NodeKind;
using CS::TokenKind;

/// Токен-идентификатор, указывающий в source.
CS::Token ident(std::uint32_t offset, std::uint32_t length) {
    CS::Token token;
    token.kind = TokenKind::Identifier;
    token.offset = offset;
    token.length = length;
    return token;
}

/// Токен-число.
CS::Token number(double value, std::uint32_t offset) {
    CS::Token token;
    token.kind = TokenKind::Number;
    token.offset = offset;
    token.length = 1;
    token.number = value;
    return token;
}

/// Токен-строка. offset указывает на открывающую кавычку.
CS::Token string(std::uint32_t offset, std::uint32_t length, bool hasEscape) {
    CS::Token token;
    token.kind = TokenKind::String;
    token.offset = offset;
    token.length = length;
    token.hasEscape = hasEscape;
    return token;
}

TEST(AstShape, EmptyTreeHasNoRoot) {
    const CS::Ast ast;
    EXPECT_EQ(ast.root(), CS::kNoNode);
    EXPECT_EQ(ast.kind(CS::kNoNode), NodeKind::Invalid);
}

TEST(AstShape, NumberKeepsValueAndOffset) {
    CS::Ast ast;
    const CS::NodeId node = ast.number(number(2.5, 7));
    EXPECT_NE(node, CS::kNoNode);
    EXPECT_EQ(ast.kind(node), NodeKind::Number);
    EXPECT_DOUBLE_EQ(ast.numberValue(node), 2.5);
    EXPECT_EQ(ast.offset(node), 7u);
    EXPECT_EQ(ast.childCount(node), 0u);
}

TEST(AstShape, StringStripsQuotesAndKeepsEscapeFlag) {
    const std::string source = "x = 'абв'";
    CS::Ast ast;
    ast.setSource(source.data());
    // 'абв' занимает [4, 12): кавычки плюс шесть байт кириллицы.
    const CS::NodeId node = ast.string(string(4, 8, true));
    EXPECT_EQ(ast.kind(node), NodeKind::String);
    EXPECT_EQ(ast.text(node), "абв");
    EXPECT_TRUE(ast.hasEscape(node));
    EXPECT_EQ(ast.offset(node), 4u);
}

TEST(AstShape, EmptyStringYieldsEmptyText) {
    const std::string source = "''";
    CS::Ast ast;
    ast.setSource(source.data());
    const CS::NodeId node = ast.string(string(0, 2, false));
    EXPECT_EQ(ast.text(node), "");
    EXPECT_FALSE(ast.hasEscape(node));
}

TEST(AstShape, BooleanTakesValueFromTokenKind) {
    CS::Ast ast;
    CS::Token yes;
    yes.kind = TokenKind::True;
    CS::Token no;
    no.kind = TokenKind::False;
    EXPECT_TRUE(ast.boolValue(ast.boolean(yes)));
    EXPECT_FALSE(ast.boolValue(ast.boolean(no)));
}

TEST(AstShape, IdentifierTextIsSourceSlice) {
    const std::string source = "user.name";
    CS::Ast ast;
    ast.setSource(source.data());
    const CS::NodeId node = ast.identifier(ident(0, 4));
    EXPECT_EQ(ast.kind(node), NodeKind::Identifier);
    EXPECT_EQ(ast.text(node), "user");
}

TEST(AstShape, BinaryKeepsChildrenInOrder) {
    CS::Ast ast;
    const CS::NodeId lhs = ast.number(number(1.0, 0));
    const CS::NodeId rhs = ast.number(number(2.0, 4));
    const CS::NodeId node = ast.binary(TokenKind::Plus, lhs, rhs, 2);
    EXPECT_EQ(ast.kind(node), NodeKind::Binary);
    EXPECT_EQ(ast.op(node), TokenKind::Plus);
    EXPECT_EQ(ast.offset(node), 2u);
    ASSERT_EQ(ast.childCount(node), 2u);
    EXPECT_EQ(ast.child(node, 0), lhs);
    EXPECT_EQ(ast.child(node, 1), rhs);
}

TEST(AstShape, CallCopiesArgumentsInOrder) {
    const std::string source = "min(1, 2, 3)";
    CS::Ast ast;
    ast.setSource(source.data());
    const CS::NodeId args[3] = {ast.number(number(1.0, 4)),
                                ast.number(number(2.0, 7)),
                                ast.number(number(3.0, 10))};
    const CS::NodeId node = ast.call(ident(0, 3), args, 3);
    EXPECT_EQ(ast.kind(node), NodeKind::Call);
    EXPECT_EQ(ast.text(node), "min");
    ASSERT_EQ(ast.childCount(node), 3u);
    EXPECT_EQ(ast.child(node, 0), args[0]);
    EXPECT_EQ(ast.child(node, 2), args[2]);
}

TEST(AstShape, ObjectKeepsPairsInterleaved) {
    const std::string source = "{ 'a': 1 }";
    CS::Ast ast;
    ast.setSource(source.data());
    const CS::NodeId pairs[2] = {ast.string(string(2, 3, false)),
                                 ast.number(number(1.0, 7))};
    const CS::NodeId node = ast.object(pairs, 2, 0);
    EXPECT_EQ(ast.kind(node), NodeKind::Object);
    ASSERT_EQ(ast.childCount(node), 2u);
    EXPECT_EQ(ast.kind(ast.child(node, 0)), NodeKind::String);
    EXPECT_EQ(ast.kind(ast.child(node, 1)), NodeKind::Number);
}

TEST(AstShape, NestedChildRangesDoNotOverlap) {
    CS::Ast ast;
    const CS::NodeId inner[2] = {ast.number(number(1.0, 0)),
                                 ast.number(number(2.0, 3))};
    const CS::NodeId innerArray = ast.array(inner, 2, 0);
    const CS::NodeId outer[2] = {innerArray, ast.number(number(3.0, 9))};
    const CS::NodeId outerArray = ast.array(outer, 2, 0);

    ASSERT_EQ(ast.childCount(innerArray), 2u);
    ASSERT_EQ(ast.childCount(outerArray), 2u);
    EXPECT_EQ(ast.child(innerArray, 0), inner[0]);
    EXPECT_EQ(ast.child(innerArray, 1), inner[1]);
    EXPECT_EQ(ast.child(outerArray, 0), innerArray);
    EXPECT_EQ(ast.child(outerArray, 1), outer[1]);
}

TEST(AstShape, ChildOutOfRangeYieldsNoNode) {
    CS::Ast ast;
    const CS::NodeId lhs = ast.number(number(1.0, 0));
    const CS::NodeId rhs = ast.number(number(2.0, 4));
    const CS::NodeId node = ast.binary(TokenKind::Plus, lhs, rhs, 2);
    EXPECT_EQ(ast.child(node, 2), CS::kNoNode);
    EXPECT_EQ(ast.child(node, 1000), CS::kNoNode);
}

TEST(AstShape, EveryBuilderProducesDistinctNodes) {
    CS::Ast ast;
    const CS::NodeId first = ast.number(number(1.0, 0));
    const CS::NodeId second = ast.number(number(1.0, 0));
    EXPECT_NE(first, second);
    EXPECT_EQ(ast.nodeCount(), 3u);  // пустышка плюс два узла
}

TEST(AstShape, RootIsWhatWasSet) {
    CS::Ast ast;
    const CS::NodeId node = ast.number(number(1.0, 0));
    ast.setRoot(node);
    EXPECT_EQ(ast.root(), node);
}

}  // namespace
```

- [ ] **Step 2: Создать `core/tests/parser_test.cpp` — вспомогательная часть**

```cpp
// Тесты парсера по docs/grammar.md §5.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "parser.hpp"

namespace {

using CS::NodeKind;
using CS::TokenKind;

struct Parsed {
    CS::Ast ast;
    bool ok = false;
    CS::Diagnostic diag;
};

/// Исходник обязан пережить Parsed: дерево держит на него срезы.
Parsed parseExpr(const std::string &source) {
    Parsed result;
    result.ok = CS::parseExpression(source.data(),
                                    static_cast<std::uint32_t>(source.size()),
                                    result.ast, result.diag);
    return result;
}

Parsed parseProg(const std::string &source) {
    Parsed result;
    result.ok = CS::parseProgram(source.data(),
                                 static_cast<std::uint32_t>(source.size()),
                                 result.ast, result.diag);
    return result;
}

const char *opName(TokenKind kind) {
    switch (kind) {
        case TokenKind::Plus: return "+";
        case TokenKind::Minus: return "-";
        case TokenKind::Star: return "*";
        case TokenKind::Slash: return "/";
        case TokenKind::Percent: return "%";
        case TokenKind::Bang: return "!";
        case TokenKind::Equal: return "==";
        case TokenKind::NotEqual: return "!=";
        case TokenKind::Less: return "<";
        case TokenKind::Greater: return ">";
        case TokenKind::LessEqual: return "<=";
        case TokenKind::GreaterEqual: return ">=";
        case TokenKind::AndAnd: return "&&";
        case TokenKind::OrOr: return "||";
        case TokenKind::QuestionQuestion: return "??";
        case TokenKind::Assign: return "=";
        case TokenKind::PlusAssign: return "+=";
        case TokenKind::MinusAssign: return "-=";
        case TokenKind::StarAssign: return "*=";
        case TokenKind::SlashAssign: return "/=";
        default: return "?";
    }
}

std::string numberText(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", value);
    return buffer;
}

std::string dump(const CS::Ast &ast, CS::NodeId node);

/// "(head child child …)"
std::string listOf(const CS::Ast &ast, CS::NodeId node, const std::string &head) {
    std::string result = "(" + head;
    for (std::uint32_t i = 0; i < ast.childCount(node); ++i) {
        result += " " + dump(ast, ast.child(node, i));
    }
    return result + ")";
}

/// Печатает поддерево S-выражением. Форма зафиксирована тестами.
std::string dump(const CS::Ast &ast, CS::NodeId node) {
    switch (ast.kind(node)) {
        case NodeKind::Invalid:
            return "<invalid>";
        case NodeKind::Number:
            return numberText(ast.numberValue(node));
        case NodeKind::String:
            return "'" + std::string(ast.text(node)) + "'";
        case NodeKind::Boolean:
            return ast.boolValue(node) ? "true" : "false";
        case NodeKind::Null:
            return "null";
        case NodeKind::Identifier:
            return std::string(ast.text(node));
        case NodeKind::Member:
            return "(. " + dump(ast, ast.child(node, 0)) + " " +
                   std::string(ast.text(node)) + ")";
        case NodeKind::Index:
            return listOf(ast, node, "[]");
        case NodeKind::Call:
            return listOf(ast, node, "call " + std::string(ast.text(node)));
        case NodeKind::Unary:
            return listOf(ast, node, std::string("u") + opName(ast.op(node)));
        case NodeKind::Binary:
            return listOf(ast, node, opName(ast.op(node)));
        case NodeKind::Conditional:
            return listOf(ast, node, "?:");
        case NodeKind::Array:
            return listOf(ast, node, "array");
        case NodeKind::Object:
            return listOf(ast, node, "object");
        case NodeKind::Assign:
            return listOf(ast, node, opName(ast.op(node)));
        case NodeKind::CallStatement:
            return listOf(ast, node, "stmt");
        case NodeKind::Program:
            return listOf(ast, node, "program");
    }
    return "<unknown>";
}

/// Разбирает выражение и печатает дерево. Пустая строка, если разбор не удался.
std::string expr(const std::string &source) {
    const Parsed parsed = parseExpr(source);
    if (!parsed.ok) {
        return "";
    }
    return dump(parsed.ast, parsed.ast.root());
}

/// То же для программы.
std::string prog(const std::string &source) {
    const Parsed parsed = parseProg(source);
    if (!parsed.ok) {
        return "";
    }
    return dump(parsed.ast, parsed.ast.root());
}

}  // namespace
```

- [ ] **Step 3: Дописать в `parser_test.cpp` группы `ParserPrimary`, `ParserPrecedence`, `ParserAssociativity`**

Эти зеленеют в задаче 5, кроме четырёх тестов про агрегаты — они помечены и зеленеют в задаче 7.

```cpp
// ─── §5.3: Primary ───────────────────────────────────────────────────

TEST(ParserPrimary, NumberLiteral) {
    const std::string source = "42";
    EXPECT_EQ(expr(source), "42");
}

TEST(ParserPrimary, FractionalNumberLiteral) {
    const std::string source = "12.75";
    EXPECT_EQ(expr(source), "12.75");
}

TEST(ParserPrimary, StringLiteral) {
    const std::string source = "'привет'";
    EXPECT_EQ(expr(source), "'привет'");
}

TEST(ParserPrimary, StringLiteralKeepsRawEscapes) {
    const std::string source = "'a\\nb'";
    const Parsed parsed = parseExpr(source);
    ASSERT_TRUE(parsed.ok);
    const CS::NodeId root = parsed.ast.root();
    EXPECT_EQ(parsed.ast.kind(root), NodeKind::String);
    EXPECT_EQ(parsed.ast.text(root), "a\\nb");
    EXPECT_TRUE(parsed.ast.hasEscape(root));
}

TEST(ParserPrimary, BooleanLiterals) {
    const std::string yes = "true";
    const std::string no = "false";
    EXPECT_EQ(expr(yes), "true");
    EXPECT_EQ(expr(no), "false");
}

TEST(ParserPrimary, NullLiteral) {
    const std::string source = "null";
    EXPECT_EQ(expr(source), "null");
}

TEST(ParserPrimary, Identifier) {
    const std::string source = "state";
    EXPECT_EQ(expr(source), "state");
}

TEST(ParserPrimary, ParenthesesProduceNoNode) {
    const std::string source = "(((a)))";
    EXPECT_EQ(expr(source), "a");
}

// Зеленеют в задаче 7.
TEST(ParserPrimary, EmptyArrayLiteral) {
    const std::string source = "[]";
    EXPECT_EQ(expr(source), "(array)");
}

TEST(ParserPrimary, ArrayLiteralKeepsOrder) {
    const std::string source = "[1, 'a', null]";
    EXPECT_EQ(expr(source), "(array 1 'a' null)");
}

TEST(ParserPrimary, EmptyObjectLiteral) {
    const std::string source = "{}";
    EXPECT_EQ(expr(source), "(object)");
}

TEST(ParserPrimary, ObjectLiteralInterleavesKeysAndValues) {
    const std::string source = "{ 'a': 1, 'b': 2 }";
    EXPECT_EQ(expr(source), "(object 'a' 1 'b' 2)");
}

TEST(ParserPrimary, AggregatesNest) {
    const std::string source = "{ 'xs': [1, [2]] }";
    EXPECT_EQ(expr(source), "(object 'xs' (array 1 (array 2)))");
}

// ─── §5.4: приоритет ─────────────────────────────────────────────────

TEST(ParserPrecedence, MultiplicativeBindsTighterThanAdditive) {
    const std::string source = "a + b * c";
    EXPECT_EQ(expr(source), "(+ a (* b c))");
}

TEST(ParserPrecedence, ParenthesesOverridePrecedence) {
    const std::string source = "(a + b) * c";
    EXPECT_EQ(expr(source), "(* (+ a b) c)");
}

TEST(ParserPrecedence, UnaryBindsTighterThanMultiplicative) {
    const std::string source = "-a * b";
    EXPECT_EQ(expr(source), "(* (u- a) b)");
}

TEST(ParserPrecedence, NilCoalesceBindsLooserThanAdditive) {
    const std::string source = "a ?? b + c";
    EXPECT_EQ(expr(source), "(?? a (+ b c))");
}

TEST(ParserPrecedence, NilCoalesceBindsTighterThanComparison) {
    const std::string source = "a ?? b == c";
    EXPECT_EQ(expr(source), "(== (?? a b) c)");
}

TEST(ParserPrecedence, ComparisonBindsTighterThanAnd) {
    const std::string source = "a < b && c > d";
    EXPECT_EQ(expr(source), "(&& (< a b) (> c d))");
}

TEST(ParserPrecedence, AndBindsTighterThanOr) {
    const std::string source = "a || b && c";
    EXPECT_EQ(expr(source), "(|| a (&& b c))");
}

TEST(ParserPrecedence, TernaryIsLowest) {
    const std::string source = "a || b ? c + d : e";
    EXPECT_EQ(expr(source), "(?: (|| a b) (+ c d) e)");
}

TEST(ParserPrecedence, ModuloSitsWithMultiplicative) {
    const std::string source = "a + b % c";
    EXPECT_EQ(expr(source), "(+ a (% b c))");
}

// ─── §5.4: ассоциативность ───────────────────────────────────────────

TEST(ParserAssociativity, AdditiveIsLeft) {
    const std::string source = "a - b - c";
    EXPECT_EQ(expr(source), "(- (- a b) c)");
}

TEST(ParserAssociativity, MultiplicativeIsLeft) {
    const std::string source = "a / b / c";
    EXPECT_EQ(expr(source), "(/ (/ a b) c)");
}

TEST(ParserAssociativity, OrIsLeft) {
    const std::string source = "a || b || c";
    EXPECT_EQ(expr(source), "(|| (|| a b) c)");
}

TEST(ParserAssociativity, AndIsLeft) {
    const std::string source = "a && b && c";
    EXPECT_EQ(expr(source), "(&& (&& a b) c)");
}

TEST(ParserAssociativity, NilCoalesceIsRight) {
    const std::string source = "a ?? b ?? c";
    EXPECT_EQ(expr(source), "(?? a (?? b c))");
}

TEST(ParserAssociativity, TernaryIsRight) {
    const std::string source = "a ? b : c ? d : e";
    EXPECT_EQ(expr(source), "(?: a b (?: c d e))");
}

TEST(ParserAssociativity, UnaryIsRight) {
    const std::string source = "!!a";
    EXPECT_EQ(expr(source), "(u! (u! a))");
}
```

- [ ] **Step 4: Дописать группу `ParserPostfix` (зеленеет в задаче 6)**

```cpp
// ─── §5.3: Postfix и Call ────────────────────────────────────────────

TEST(ParserPostfix, MemberAccess) {
    const std::string source = "user.name";
    EXPECT_EQ(expr(source), "(. user name)");
}

TEST(ParserPostfix, MemberChain) {
    const std::string source = "a.b.c";
    EXPECT_EQ(expr(source), "(. (. a b) c)");
}

TEST(ParserPostfix, Index) {
    const std::string source = "items[0]";
    EXPECT_EQ(expr(source), "([] items 0)");
}

TEST(ParserPostfix, IndexTakesFullExpression) {
    const std::string source = "items[i + 1]";
    EXPECT_EQ(expr(source), "([] items (+ i 1))");
}

TEST(ParserPostfix, MixedChain) {
    const std::string source = "a.b[0].c";
    EXPECT_EQ(expr(source), "(. ([] (. a b) 0) c)");
}

TEST(ParserPostfix, CallWithoutArguments) {
    const std::string source = "keys()";
    EXPECT_EQ(expr(source), "(call keys)");
}

TEST(ParserPostfix, CallWithArguments) {
    const std::string source = "min(a, b + 1)";
    EXPECT_EQ(expr(source), "(call min a (+ b 1))");
}

TEST(ParserPostfix, CallIsPostfixBase) {
    const std::string source = "keys(o)[0]";
    EXPECT_EQ(expr(source), "([] (call keys o) 0)");
}

TEST(ParserPostfix, UnaryAppliesToWholeChain) {
    const std::string source = "-a.b";
    EXPECT_EQ(expr(source), "(u- (. a b))");
}

TEST(ParserPostfix, NestedCallInArgument) {
    const std::string source = "min(count(a), 1)";
    EXPECT_EQ(expr(source), "(call min (call count a) 1)");
}
```

- [ ] **Step 5: Дописать группы `ParserStatement` и `ParserModes` (зеленеют в задаче 8)**

```cpp
// ─── §5.2: стейтменты ────────────────────────────────────────────────

TEST(ParserStatement, EmptyProgramHasEmptyRoot) {
    const std::string source = "";
    EXPECT_EQ(prog(source), "(program)");
}

TEST(ParserStatement, WhitespaceOnlyProgramHasEmptyRoot) {
    const std::string source = "  // только комментарий\n";
    EXPECT_EQ(prog(source), "(program)");
}

TEST(ParserStatement, EmptyStatementProducesNoNode) {
    const std::string source = ";;;";
    EXPECT_EQ(prog(source), "(program)");
}

TEST(ParserStatement, SimpleAssignment) {
    const std::string source = "a = 1;";
    EXPECT_EQ(prog(source), "(program (= a 1))");
}

TEST(ParserStatement, CompoundAssignments) {
    const std::string plus = "a += 1;";
    const std::string minus = "a -= 1;";
    const std::string star = "a *= 1;";
    const std::string slash = "a /= 1;";
    EXPECT_EQ(prog(plus), "(program (+= a 1))");
    EXPECT_EQ(prog(minus), "(program (-= a 1))");
    EXPECT_EQ(prog(star), "(program (*= a 1))");
    EXPECT_EQ(prog(slash), "(program (/= a 1))");
}

TEST(ParserStatement, AssignmentToMember) {
    const std::string source = "state.total = 1;";
    EXPECT_EQ(prog(source), "(program (= (. state total) 1))");
}

TEST(ParserStatement, AssignmentToIndex) {
    const std::string source = "items[0] = 1;";
    EXPECT_EQ(prog(source), "(program (= ([] items 0) 1))");
}

TEST(ParserStatement, AssignmentTargetMayHaveCallInSubscript) {
    const std::string source = "arr[idx()] += 1;";
    EXPECT_EQ(prog(source), "(program (+= ([] arr (call idx)) 1))");
}

TEST(ParserStatement, CallStatement) {
    const std::string source = "push(items, 1);";
    EXPECT_EQ(prog(source), "(program (stmt (call push items 1)))");
}

TEST(ParserStatement, SeveralStatementsKeepOrder) {
    const std::string source = "a = 1; push(b, 2); c += 3;";
    EXPECT_EQ(prog(source),
              "(program (= a 1) (stmt (call push b 2)) (+= c 3))");
}

TEST(ParserStatement, StatementsSpanLines) {
    const std::string source = "a = 1;\n\nb = 2;\n";
    EXPECT_EQ(prog(source), "(program (= a 1) (= b 2))");
}

// ─── §5.1: два стартовых символа ─────────────────────────────────────

TEST(ParserModes, ExpressionModeRejectsAssignment) {
    const std::string source = "a = 1";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 2u);
}

TEST(ParserModes, ExpressionModeRejectsSemicolon) {
    const std::string source = "a + b;";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 5u);
}

TEST(ParserModes, ExpressionModeRejectsEmptySource) {
    const std::string source = "";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

TEST(ParserModes, ProgramModeRejectsBareExpression) {
    const std::string source = "a + 1;";
    const Parsed parsed = parseProg(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

TEST(ParserModes, ExpressionModeAcceptsWhatProgramModeRejects) {
    const std::string source = "a + 1";
    EXPECT_EQ(expr(source), "(+ a 1)");
}
```

- [ ] **Step 6: Дописать группы `ParserEarlyErrors`, `ParserLexerErrors`, `ParserLimits`**

Зеленеют по частям: задача 5 — первые пять и `ParserLexerErrors`; задача 6 — четыре про постфикс и вызов; задача 7 — четыре про агрегаты; задача 8 — четыре про стейтменты. `ParserLimits` зеленеет в задаче 5.

```cpp
// ─── §5.5: ранние ошибки парсера ─────────────────────────────────────

TEST(ParserEarlyErrors, ChainedComparison) {
    const std::string source = "a < b < c";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 6u);
}

TEST(ParserEarlyErrors, TrailingTextAfterExpression) {
    const std::string source = "1 + 1 2";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 6u);
}

TEST(ParserEarlyErrors, MissingOperand) {
    const std::string source = "1 +";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 3u);
}

TEST(ParserEarlyErrors, UnclosedParenthesis) {
    const std::string source = "(1 + 2";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 6u);
}

TEST(ParserEarlyErrors, TernaryWithoutColon) {
    const std::string source = "a ? b";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 5u);
}

TEST(ParserEarlyErrors, DotWithoutName) {
    const std::string source = "a.1";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 2u);
}

TEST(ParserEarlyErrors, UnclosedBracket) {
    const std::string source = "a[0";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 3u);
}

TEST(ParserEarlyErrors, TrailingCommaInCall) {
    const std::string source = "f(a, b,)";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 7u);
}

TEST(ParserEarlyErrors, UnclosedCall) {
    const std::string source = "f(a";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 3u);
}

TEST(ParserEarlyErrors, TrailingCommaInArray) {
    const std::string source = "[1, 2,]";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 6u);
}

TEST(ParserEarlyErrors, TrailingCommaInObject) {
    const std::string source = "{ 'a': 1, }";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 10u);
}

TEST(ParserEarlyErrors, ObjectKeyMustBeStringLiteral) {
    const std::string source = "{ x: 1 }";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 2u);
}

TEST(ParserEarlyErrors, ObjectPairNeedsColon) {
    const std::string source = "{ 'a' 1 }";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 6u);
}

TEST(ParserEarlyErrors, StatementReducesToNoProduction) {
    const std::string source = "user.name;";
    const Parsed parsed = parseProg(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

TEST(ParserEarlyErrors, StatementCannotStartWithLiteral) {
    const std::string source = "1 + 1;";
    const Parsed parsed = parseProg(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

TEST(ParserEarlyErrors, AssignmentTargetCannotContainCall) {
    const std::string source = "f(a).b = 1;";
    const Parsed parsed = parseProg(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

TEST(ParserEarlyErrors, StatementNeedsSemicolon) {
    const std::string source = "a = 1";
    const Parsed parsed = parseProg(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 5u);
}

// ─── ошибки лексера проходят насквозь ────────────────────────────────

TEST(ParserLexerErrors, UnterminatedStringKeepsLexerDiagnostic) {
    const std::string source = "'abc";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

TEST(ParserLexerErrors, ReservedWordIsNotAnExpression) {
    const std::string source = "class";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

// ─── предел глубины рекурсии ─────────────────────────────────────────

TEST(ParserLimits, DeepButAcceptableNestingParses) {
    std::string source(20, '(');
    source += "1";
    source.append(20, ')');
    const Parsed parsed = parseExpr(source);
    EXPECT_TRUE(parsed.ok);
}

TEST(ParserLimits, ExcessiveNestingIsRejectedNotCrashing) {
    std::string source(5000, '(');
    source += "1";
    source.append(5000, ')');
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
}

TEST(ParserLimits, DeepUnaryChainIsRejectedNotCrashing) {
    std::string source(5000, '!');
    source += "a";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
}

TEST(ParserLimits, DeepNilCoalesceChainIsRejectedNotCrashing) {
    std::string source = "a";
    for (int i = 0; i < 5000; ++i) {
        source += " ?? a";
    }
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
}
```

- [ ] **Step 7: Подключить оба файла тестов**

В `core/tests/CMakeLists.txt`:

```cmake
add_executable(chupascript_tests
    ast_test.cpp
    lexer_test.cpp
    parser_test.cpp
    smoke_test.cpp
)
```

- [ ] **Step 8: Собрать и убедиться, что тесты красные**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: сборка проходит; 53 теста лексера зелёные; 13 тестов `AstShape` и 78 тестов парсера падают.

- [ ] **Step 9: Коммит**

```bash
git add core/tests/ast_test.cpp core/tests/parser_test.cpp core/tests/CMakeLists.txt
git commit -m "Add AST and parser test suites"
```

---

## Task 3: Бенчмарки

**Files:**
- Create: `benchmarks/parser_benchmark.cpp`
- Modify: `benchmarks/CMakeLists.txt`

**Interfaces:**
- Consumes: `CS::parseExpression`, `CS::parseProgram`, `CS::Ast`, `CS::Diagnostic`.
- Produces: имена бенчмарков `BM_Parse_Chain`, `BM_Parse_Precedence`, `BM_Parse_Postfix`, `BM_Parse_Aggregates`, `BM_Parse_Props`, `BM_Parse_Handler` — задача 9 фиксирует по ним базу.

Каждый бенчмарк становится осмысленным, когда садится его порция реализации, и с этого момента обязан не деградировать от последующих порций.

- [ ] **Step 1: Создать `benchmarks/parser_benchmark.cpp`**

```cpp
// Бенчмарки парсера, по одному на подмножество грамматики.
//
// Замеряется разбор целиком: поток токенов лексера плюс построение дерева.
// Отдельно лексер уже замерен в lexer_benchmark.cpp.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "parser.hpp"

namespace {

/// "unit sep unit sep … unit"
std::string join(const std::string &unit, const std::string &separator,
                 int times) {
    std::string result;
    result.reserve((unit.size() + separator.size()) *
                   static_cast<std::size_t>(times));
    for (int i = 0; i < times; ++i) {
        if (i != 0) {
            result += separator;
        }
        result += unit;
    }
    return result;
}

std::string repeat(const std::string &unit, int times) {
    return join(unit, "", times);
}

void runExpression(benchmark::State &state, const std::string &source) {
    for (auto _ : state) {
        CS::Ast ast;
        CS::Diagnostic diag;
        const bool ok = CS::parseExpression(
            source.data(), static_cast<std::uint32_t>(source.size()), ast, diag);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(ast);
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                            static_cast<std::int64_t>(source.size()));
}

void runProgram(benchmark::State &state, const std::string &source) {
    for (auto _ : state) {
        CS::Ast ast;
        CS::Diagnostic diag;
        const bool ok = CS::parseProgram(
            source.data(), static_cast<std::uint32_t>(source.size()), ast, diag);
        benchmark::DoNotOptimize(ok);
        benchmark::DoNotOptimize(ast);
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                            static_cast<std::int64_t>(source.size()));
}

}  // namespace

// Цена спуска: на каждый операнд проходится вся цепочка из десяти правил.
static void BM_Parse_Chain(benchmark::State &state) {
    runExpression(state, join("a", " + ", 400));
}
BENCHMARK(BM_Parse_Chain);

static void BM_Parse_Precedence(benchmark::State &state) {
    runExpression(state, join("a * b + c < d", " || ", 200));
}
BENCHMARK(BM_Parse_Precedence);

static void BM_Parse_Postfix(benchmark::State &state) {
    runExpression(state, join("user.profile.name[0].value", " ?? ", 150));
}
BENCHMARK(BM_Parse_Postfix);

static void BM_Parse_Aggregates(benchmark::State &state) {
    runExpression(state, join("{ 'a': 1, 'b': [1, 2, 3] }", " ?? ", 150));
}
BENCHMARK(BM_Parse_Aggregates);

static void BM_Parse_Props(benchmark::State &state) {
    runExpression(state,
                  join("product.discount != null"
                       " ? product.price * (1 - product.discount)"
                       " : product.price",
                       " ?? ", 100));
}
BENCHMARK(BM_Parse_Props);

static void BM_Parse_Handler(benchmark::State &state) {
    runProgram(state, repeat("push(state.items, product);"
                             "state.badge = count(state.items);"
                             "state.total += product.price;"
                             "state.label = format('добавлено ${} на ${}',"
                             " product.name, product.price);",
                             50));
}
BENCHMARK(BM_Parse_Handler);
```

- [ ] **Step 2: Подключить бенчмарк**

В `benchmarks/CMakeLists.txt`:

```cmake
add_executable(chupascript_benchmarks
    eval_benchmark.cpp
    lexer_benchmark.cpp
    parser_benchmark.cpp
)
```

- [ ] **Step 3: Собрать Release и убедиться, что бенчмарки запускаются**

```bash
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks --benchmark_filter=BM_Parse --benchmark_min_time=0.01s
```

Expected: шесть бенчмарков отрабатывают. Числа сейчас бессмысленны — заглушка отказывает сразу, — но запуск обязан быть чистым.

- [ ] **Step 4: Коммит**

```bash
git add benchmarks/parser_benchmark.cpp benchmarks/CMakeLists.txt
git commit -m "Add parser benchmarks"
```

---

## Task 4: Реализация `Ast`

**Files:**
- Modify: `core/src/ast.cpp` (заменить заглушки из задачи 1 целиком)
- Test: `core/tests/ast_test.cpp`

**Interfaces:**
- Consumes: объявления из `ast.hpp` (задача 1).
- Produces: рабочие строитель и аксессоры. Задачи 5–8 строят дерево только через них.

- [ ] **Step 1: Убедиться, что тесты `AstShape` красные**

Run: `ctest --test-dir build --output-on-failure -R AstShape`
Expected: 13 тестов падают.

- [ ] **Step 2: Заменить `core/src/ast.cpp` целиком**

```cpp
#include "ast.hpp"

namespace CS {

Ast::Ast() {
    // Индекс kNoNode занят пустышкой, чтобы 0 означал «нет узла» и обращение
    // к нему было безопасным.
    nodes_.push_back(Node{});
}

void Ast::setSource(const char *source) noexcept { src_ = source; }

void Ast::setRoot(NodeId node) noexcept { root_ = node; }

NodeId Ast::add(const Node &node) {
    nodes_.push_back(node);
    return static_cast<NodeId>(nodes_.size() - 1);
}

std::uint32_t Ast::pushChildren(const NodeId *ids, std::uint32_t count) {
    const auto start = static_cast<std::uint32_t>(children_.size());
    children_.insert(children_.end(), ids, ids + count);
    return start;
}

// ─── строитель ───────────────────────────────────────────────────────

NodeId Ast::number(const Token &token) {
    Node node;
    node.kind = NodeKind::Number;
    node.offset = token.offset;
    node.number = token.number;
    return add(node);
}

NodeId Ast::string(const Token &token) {
    Node node;
    node.kind = NodeKind::String;
    node.offset = token.offset;
    node.textOffset = stringContentOffset(token);
    node.textLength = stringContentLength(token);
    node.hasEscape = token.hasEscape;
    return add(node);
}

NodeId Ast::boolean(const Token &token) {
    Node node;
    node.kind = NodeKind::Boolean;
    node.offset = token.offset;
    node.boolean = token.kind == TokenKind::True;
    return add(node);
}

NodeId Ast::null(const Token &token) {
    Node node;
    node.kind = NodeKind::Null;
    node.offset = token.offset;
    return add(node);
}

NodeId Ast::identifier(const Token &token) {
    Node node;
    node.kind = NodeKind::Identifier;
    node.offset = token.offset;
    node.textOffset = token.offset;
    node.textLength = token.length;
    return add(node);
}

NodeId Ast::member(NodeId base, const Token &name) {
    Node node;
    node.kind = NodeKind::Member;
    // Смещение — имя поля: на него указывает сообщение об отсутствующем ключе.
    node.offset = name.offset;
    node.textOffset = name.offset;
    node.textLength = name.length;
    node.childStart = pushChildren(&base, 1);
    node.childCount = 1;
    return add(node);
}

NodeId Ast::index(NodeId base, NodeId subscript, std::uint32_t offset) {
    const NodeId kids[2] = {base, subscript};
    Node node;
    node.kind = NodeKind::Index;
    node.offset = offset;
    node.childStart = pushChildren(kids, 2);
    node.childCount = 2;
    return add(node);
}

NodeId Ast::unary(TokenKind op, NodeId operand, std::uint32_t offset) {
    Node node;
    node.kind = NodeKind::Unary;
    node.op = op;
    node.offset = offset;
    node.childStart = pushChildren(&operand, 1);
    node.childCount = 1;
    return add(node);
}

NodeId Ast::binary(TokenKind op, NodeId lhs, NodeId rhs, std::uint32_t offset) {
    const NodeId kids[2] = {lhs, rhs};
    Node node;
    node.kind = NodeKind::Binary;
    node.op = op;
    node.offset = offset;
    node.childStart = pushChildren(kids, 2);
    node.childCount = 2;
    return add(node);
}

NodeId Ast::conditional(NodeId condition, NodeId whenTrue, NodeId whenFalse,
                        std::uint32_t offset) {
    const NodeId kids[3] = {condition, whenTrue, whenFalse};
    Node node;
    node.kind = NodeKind::Conditional;
    node.offset = offset;
    node.childStart = pushChildren(kids, 3);
    node.childCount = 3;
    return add(node);
}

NodeId Ast::call(const Token &name, const NodeId *args, std::uint32_t count) {
    Node node;
    node.kind = NodeKind::Call;
    node.offset = name.offset;
    node.textOffset = name.offset;
    node.textLength = name.length;
    node.childStart = pushChildren(args, count);
    node.childCount = count;
    return add(node);
}

NodeId Ast::array(const NodeId *items, std::uint32_t count,
                  std::uint32_t offset) {
    Node node;
    node.kind = NodeKind::Array;
    node.offset = offset;
    node.childStart = pushChildren(items, count);
    node.childCount = count;
    return add(node);
}

NodeId Ast::object(const NodeId *pairs, std::uint32_t count,
                   std::uint32_t offset) {
    // count — длина массива, то есть 2n при n парах: дети чередуются.
    Node node;
    node.kind = NodeKind::Object;
    node.offset = offset;
    node.childStart = pushChildren(pairs, count);
    node.childCount = count;
    return add(node);
}

NodeId Ast::assign(TokenKind op, NodeId target, NodeId value,
                   std::uint32_t offset) {
    const NodeId kids[2] = {target, value};
    Node node;
    node.kind = NodeKind::Assign;
    node.op = op;
    node.offset = offset;
    node.childStart = pushChildren(kids, 2);
    node.childCount = 2;
    return add(node);
}

NodeId Ast::callStatement(NodeId callNode, std::uint32_t offset) {
    Node node;
    node.kind = NodeKind::CallStatement;
    node.offset = offset;
    node.childStart = pushChildren(&callNode, 1);
    node.childCount = 1;
    return add(node);
}

NodeId Ast::program(const NodeId *statements, std::uint32_t count) {
    Node node;
    node.kind = NodeKind::Program;
    node.offset = 0;
    node.childStart = pushChildren(statements, count);
    node.childCount = count;
    return add(node);
}

// ─── аксессоры ───────────────────────────────────────────────────────

NodeId Ast::root() const noexcept { return root_; }

NodeKind Ast::kind(NodeId node) const noexcept { return nodes_[node].kind; }

TokenKind Ast::op(NodeId node) const noexcept { return nodes_[node].op; }

std::uint32_t Ast::offset(NodeId node) const noexcept {
    return nodes_[node].offset;
}

std::uint32_t Ast::childCount(NodeId node) const noexcept {
    return nodes_[node].childCount;
}

NodeId Ast::child(NodeId node, std::uint32_t index) const noexcept {
    const Node &n = nodes_[node];
    if (index >= n.childCount) {
        return kNoNode;
    }
    return children_[n.childStart + index];
}

double Ast::numberValue(NodeId node) const noexcept {
    return nodes_[node].number;
}

bool Ast::boolValue(NodeId node) const noexcept { return nodes_[node].boolean; }

std::string_view Ast::text(NodeId node) const noexcept {
    const Node &n = nodes_[node];
    if (n.textLength == 0 || src_ == nullptr) {
        return {};
    }
    return std::string_view(src_ + n.textOffset, n.textLength);
}

bool Ast::hasEscape(NodeId node) const noexcept { return nodes_[node].hasEscape; }

std::uint32_t Ast::nodeCount() const noexcept {
    return static_cast<std::uint32_t>(nodes_.size());
}

}  // namespace CS
```

- [ ] **Step 3: Собрать и прогнать тесты `AstShape`**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R AstShape`
Expected: 13 тестов зелёные.

- [ ] **Step 4: Прогнать всё**

Run: `ctest --test-dir build --output-on-failure`
Expected: 53 лексера + 13 `AstShape` зелёные; 78 тестов парсера падают.

- [ ] **Step 5: Коммит**

```bash
git add core/src/ast.cpp
git commit -m "Implement the AST builder and accessors"
```

---

## Task 5: Выражения — цепочка приоритетов

**Files:**
- Modify: `core/src/parser.cpp`
- Test: `core/tests/parser_test.cpp`

**Interfaces:**
- Consumes: `CS::Lexer`, `CS::Ast`, `CS::Diagnostic`.
- Produces: класс `Parser` в анонимном пространстве имён `parser.cpp` с приватными методами `ternary`, `logicalOr`, `logicalAnd`, `comparison`, `nilCoalesce`, `additive`, `multiplicative`, `unary`, `postfix`, `primary`. Задачи 6–8 дописывают в этот же класс.

Порция закрывает `Primary` без агрегатов и вызовов, `Unary` и всю цепочку приоритетов, плюс точку входа `parseExpression`.

- [ ] **Step 1: Заменить `core/src/parser.cpp` целиком**

```cpp
#include "parser.hpp"

#include <cstdint>
#include <vector>

#include "lexer.hpp"
#include "token.hpp"

namespace CS {
namespace {

/// Предел глубины рекурсии парсера.
///
/// Вход недоверенный, а без предела тексты вида "((((((…", "!!!!!!…" и
/// "1 ?? 1 ?? 1 ?? …" роняют процесс переполнением стека. Счётчик растёт в
/// трёх самотрекурсивных правилах — ternary, nilCoalesce, unary, — поэтому
/// один уровень вложенности скобок стоит трёх единиц, а цепочка из '!' или
/// '??' — одной за звено.
///
/// 96 единиц — это около 320 кадров стека в худшем случае, то есть меньше
/// сотни килобайт. Двадцати уровней вложенности в макете не бывает.
constexpr std::uint32_t kMaxDepth = 96;

bool isComparisonOp(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::Equal:
        case TokenKind::NotEqual:
        case TokenKind::Less:
        case TokenKind::Greater:
        case TokenKind::LessEqual:
        case TokenKind::GreaterEqual:
            return true;
        default:
            return false;
    }
}

class Parser {
   public:
    Parser(const char *source, std::uint32_t length, Ast &ast)
        : lexer_(source, length), ast_(ast) {}

    /// Стартовый символ Expression, docs/grammar.md §5.1.
    bool runExpression(Diagnostic &diag);

   private:
    /// Считает глубину вложенности; уменьшает её на любом выходе из правила.
    class DepthGuard {
       public:
        explicit DepthGuard(std::uint32_t &depth) noexcept : depth_(depth) {
            ++depth_;
        }
        ~DepthGuard() { --depth_; }
        DepthGuard(const DepthGuard &) = delete;
        DepthGuard &operator=(const DepthGuard &) = delete;

       private:
        std::uint32_t &depth_;
    };

    bool advance();
    [[nodiscard]] bool at(TokenKind kind) const noexcept {
        return cur_.kind == kind;
    }

    /// Записывает первый отказ и возвращает kNoNode.
    ///
    /// Последующие вызовы отказ не переписывают: диагностика лексера,
    /// пришедшая первой, обязана дойти до вызывающего неизменной.
    NodeId fail(std::uint32_t offset, const char *message);

    NodeId ternary();
    NodeId logicalOr();
    NodeId logicalAnd();
    NodeId comparison();
    NodeId nilCoalesce();
    NodeId additive();
    NodeId multiplicative();
    NodeId unary();
    NodeId postfix();
    NodeId primary();

    Lexer lexer_;
    Ast &ast_;
    Token cur_{};
    Diagnostic diag_{};
    bool failed_ = false;
    std::uint32_t depth_ = 0;
};

bool Parser::advance() {
    if (!lexer_.next(cur_, diag_)) {
        failed_ = true;
        return false;
    }
    return true;
}

NodeId Parser::fail(std::uint32_t offset, const char *message) {
    if (!failed_) {
        diag_ = Diagnostic{ErrorCode::Syntax, offset, message};
        failed_ = true;
    }
    return kNoNode;
}

// ─── §5.3, от низшего приоритета к высшему ───────────────────────────

NodeId Parser::ternary() {
    const DepthGuard guard(depth_);
    if (depth_ > kMaxDepth) {
        return fail(cur_.offset, "expression nesting too deep");
    }
    const NodeId condition = logicalOr();
    if (condition == kNoNode) {
        return kNoNode;
    }
    if (!at(TokenKind::Question)) {
        return condition;
    }
    const std::uint32_t offset = cur_.offset;
    if (!advance()) {
        return kNoNode;
    }
    const NodeId whenTrue = ternary();
    if (whenTrue == kNoNode) {
        return kNoNode;
    }
    if (!at(TokenKind::Colon)) {
        return fail(cur_.offset, "expected ':' in conditional expression");
    }
    if (!advance()) {
        return kNoNode;
    }
    const NodeId whenFalse = ternary();
    if (whenFalse == kNoNode) {
        return kNoNode;
    }
    return ast_.conditional(condition, whenTrue, whenFalse, offset);
}

NodeId Parser::logicalOr() {
    NodeId lhs = logicalAnd();
    if (lhs == kNoNode) {
        return kNoNode;
    }
    while (at(TokenKind::OrOr)) {
        const std::uint32_t offset = cur_.offset;
        if (!advance()) {
            return kNoNode;
        }
        const NodeId rhs = logicalAnd();
        if (rhs == kNoNode) {
            return kNoNode;
        }
        lhs = ast_.binary(TokenKind::OrOr, lhs, rhs, offset);
    }
    return lhs;
}

NodeId Parser::logicalAnd() {
    NodeId lhs = comparison();
    if (lhs == kNoNode) {
        return kNoNode;
    }
    while (at(TokenKind::AndAnd)) {
        const std::uint32_t offset = cur_.offset;
        if (!advance()) {
            return kNoNode;
        }
        const NodeId rhs = comparison();
        if (rhs == kNoNode) {
            return kNoNode;
        }
        lhs = ast_.binary(TokenKind::AndAnd, lhs, rhs, offset);
    }
    return lhs;
}

NodeId Parser::comparison() {
    const NodeId lhs = nilCoalesce();
    if (lhs == kNoNode) {
        return kNoNode;
    }
    if (!isComparisonOp(cur_.kind)) {
        return lhs;
    }
    const TokenKind op = cur_.kind;
    const std::uint32_t offset = cur_.offset;
    if (!advance()) {
        return kNoNode;
    }
    const NodeId rhs = nilCoalesce();
    if (rhs == kNoNode) {
        return kNoNode;
    }
    // §5.4: уровень неассоциативен. Второе сравнение подряд — ранняя ошибка
    // §5.5, а не «лишний текст»: сообщение обязано называть причину.
    if (isComparisonOp(cur_.kind)) {
        return fail(cur_.offset, "chained comparison is not allowed");
    }
    return ast_.binary(op, lhs, rhs, offset);
}

NodeId Parser::nilCoalesce() {
    const DepthGuard guard(depth_);
    if (depth_ > kMaxDepth) {
        return fail(cur_.offset, "expression nesting too deep");
    }
    const NodeId lhs = additive();
    if (lhs == kNoNode) {
        return kNoNode;
    }
    if (!at(TokenKind::QuestionQuestion)) {
        return lhs;
    }
    const std::uint32_t offset = cur_.offset;
    if (!advance()) {
        return kNoNode;
    }
    // Правая ассоциативность — рекурсия в себя, а не цикл.
    const NodeId rhs = nilCoalesce();
    if (rhs == kNoNode) {
        return kNoNode;
    }
    return ast_.binary(TokenKind::QuestionQuestion, lhs, rhs, offset);
}

NodeId Parser::additive() {
    NodeId lhs = multiplicative();
    if (lhs == kNoNode) {
        return kNoNode;
    }
    while (at(TokenKind::Plus) || at(TokenKind::Minus)) {
        const TokenKind op = cur_.kind;
        const std::uint32_t offset = cur_.offset;
        if (!advance()) {
            return kNoNode;
        }
        const NodeId rhs = multiplicative();
        if (rhs == kNoNode) {
            return kNoNode;
        }
        lhs = ast_.binary(op, lhs, rhs, offset);
    }
    return lhs;
}

NodeId Parser::multiplicative() {
    NodeId lhs = unary();
    if (lhs == kNoNode) {
        return kNoNode;
    }
    while (at(TokenKind::Star) || at(TokenKind::Slash) ||
           at(TokenKind::Percent)) {
        const TokenKind op = cur_.kind;
        const std::uint32_t offset = cur_.offset;
        if (!advance()) {
            return kNoNode;
        }
        const NodeId rhs = unary();
        if (rhs == kNoNode) {
            return kNoNode;
        }
        lhs = ast_.binary(op, lhs, rhs, offset);
    }
    return lhs;
}

NodeId Parser::unary() {
    const DepthGuard guard(depth_);
    if (depth_ > kMaxDepth) {
        return fail(cur_.offset, "expression nesting too deep");
    }
    if (at(TokenKind::Bang) || at(TokenKind::Minus)) {
        const TokenKind op = cur_.kind;
        const std::uint32_t offset = cur_.offset;
        if (!advance()) {
            return kNoNode;
        }
        const NodeId operand = unary();
        if (operand == kNoNode) {
            return kNoNode;
        }
        return ast_.unary(op, operand, offset);
    }
    return postfix();
}

NodeId Parser::postfix() {
    // Постфиксные операции садятся в задаче 6.
    return primary();
}

NodeId Parser::primary() {
    switch (cur_.kind) {
        case TokenKind::Number: {
            const Token token = cur_;
            if (!advance()) {
                return kNoNode;
            }
            return ast_.number(token);
        }
        case TokenKind::String: {
            const Token token = cur_;
            if (!advance()) {
                return kNoNode;
            }
            return ast_.string(token);
        }
        case TokenKind::True:
        case TokenKind::False: {
            const Token token = cur_;
            if (!advance()) {
                return kNoNode;
            }
            return ast_.boolean(token);
        }
        case TokenKind::Null: {
            const Token token = cur_;
            if (!advance()) {
                return kNoNode;
            }
            return ast_.null(token);
        }
        case TokenKind::Identifier: {
            const Token token = cur_;
            if (!advance()) {
                return kNoNode;
            }
            return ast_.identifier(token);
        }
        case TokenKind::LParen: {
            if (!advance()) {
                return kNoNode;
            }
            // Скобки узла не порождают: группировка уже выражена формой дерева.
            const NodeId inner = ternary();
            if (inner == kNoNode) {
                return kNoNode;
            }
            if (!at(TokenKind::RParen)) {
                return fail(cur_.offset, "expected ')'");
            }
            if (!advance()) {
                return kNoNode;
            }
            return inner;
        }
        default:
            return fail(cur_.offset, "expected an expression");
    }
}

bool Parser::runExpression(Diagnostic &diag) {
    if (!advance()) {
        diag = diag_;
        return false;
    }
    const NodeId root = ternary();
    if (root == kNoNode) {
        diag = diag_;
        return false;
    }
    if (at(TokenKind::Semicolon)) {
        fail(cur_.offset, "';' is not allowed in expression mode");
        diag = diag_;
        return false;
    }
    if (!at(TokenKind::End)) {
        fail(cur_.offset, "trailing text after expression");
        diag = diag_;
        return false;
    }
    ast_.setRoot(root);
    return true;
}

}  // namespace

bool parseExpression(const char *source, std::uint32_t length, Ast &ast,
                     Diagnostic &diag) {
    ast.setSource(source);
    Parser parser(source, length, ast);
    return parser.runExpression(diag);
}

bool parseProgram(const char *source, std::uint32_t length, Ast &ast,
                  Diagnostic &diag) {
    ast.setSource(source);
    static_cast<void>(length);
    diag = Diagnostic{ErrorCode::Syntax, 0, "program parsing not implemented"};
    return false;
}

}  // namespace CS
```

- [ ] **Step 2: Собрать**

Run: `cmake --build build -j`
Expected: сборка чистая.

- [ ] **Step 3: Прогнать зеленеющие группы**

Run: `ctest --test-dir build --output-on-failure -R "ParserPrecedence|ParserAssociativity|ParserLimits"`
Expected: 9 + 7 + 4 = 20 тестов зелёные.

- [ ] **Step 4: Прогнать остальное и сверить ожидания**

Run: `ctest --test-dir build --output-on-failure`
Expected зелёные: 53 лексера, 13 `AstShape`, 20 из шага 3, 8 из `ParserPrimary` (все, кроме пяти про агрегаты), 5 из `ParserEarlyErrors` (`ChainedComparison`, `TrailingTextAfterExpression`, `MissingOperand`, `UnclosedParenthesis`, `TernaryWithoutColon`), 2 из `ParserLexerErrors`, 3 из `ParserModes` (`ExpressionModeRejectsAssignment`, `ExpressionModeRejectsSemicolon`, `ExpressionModeRejectsEmptySource`).
Expected красные: `ParserPostfix` (10), `ParserStatement` (11), оставшиеся `ParserPrimary` (5), `ParserEarlyErrors` (12), `ParserModes` (2).

- [ ] **Step 5: Снять базу своих бенчмарков**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_filter='BM_Parse_(Chain|Precedence)' \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > /tmp/parse-task5.json
```

Сохранить файл — задачи 6–8 сверяются с ним и обязаны не ухудшить эти два бенчмарка больше чем на 10 %.

- [ ] **Step 6: Коммит**

```bash
git add core/src/parser.cpp
git commit -m "Implement the expression precedence chain"
```

---

## Task 6: Постфикс и вызовы

**Files:**
- Modify: `core/src/parser.cpp`
- Test: `core/tests/parser_test.cpp`

**Interfaces:**
- Consumes: класс `Parser` из задачи 5.
- Produces: метод `NodeId callArguments(const Token &name)` и рабочий `postfix()`; поле `std::vector<NodeId> scratch_`, которым задача 7 пользуется для агрегатов.

- [ ] **Step 1: Убедиться, что `ParserPostfix` красная**

Run: `ctest --test-dir build --output-on-failure -R ParserPostfix`
Expected: 10 тестов падают.

- [ ] **Step 2: Добавить общий буфер списков и объявление метода**

В объявление класса `Parser` дописать в приватную часть:

```cpp
    NodeId callArguments(const Token &name);

    /// Общий буфер списков детей: аргументы вызова, элементы литералов,
    /// стейтменты программы. Правило помечает вершину, толкает свои узлы и
    /// откатывает буфер, забрав их.
    ///
    /// Откат делается только на успешном пути: после отказа разбор
    /// прекращается навсегда, и остатки в буфере никого не касаются.
    std::vector<NodeId> scratch_;   // TODO(B10): вместе с боковым пулом детей
```

- [ ] **Step 3: Заменить `Parser::postfix()`**

```cpp
NodeId Parser::postfix() {
    NodeId base = primary();
    if (base == kNoNode) {
        return kNoNode;
    }
    for (;;) {
        if (at(TokenKind::Dot)) {
            if (!advance()) {
                return kNoNode;
            }
            if (!at(TokenKind::Identifier)) {
                return fail(cur_.offset, "expected a field name after '.'");
            }
            const Token name = cur_;
            if (!advance()) {
                return kNoNode;
            }
            base = ast_.member(base, name);
        } else if (at(TokenKind::LBracket)) {
            const std::uint32_t offset = cur_.offset;
            if (!advance()) {
                return kNoNode;
            }
            const NodeId subscript = ternary();
            if (subscript == kNoNode) {
                return kNoNode;
            }
            if (!at(TokenKind::RBracket)) {
                return fail(cur_.offset, "expected ']'");
            }
            if (!advance()) {
                return kNoNode;
            }
            base = ast_.index(base, subscript, offset);
        } else {
            return base;
        }
    }
}
```

- [ ] **Step 4: Добавить `Parser::callArguments` и ветку вызова в `primary()`**

Новый метод — рядом с `postfix()`:

```cpp
NodeId Parser::callArguments(const Token &name) {
    // Вход: cur_ — открывающая скобка вызова.
    if (!advance()) {
        return kNoNode;
    }
    const std::size_t mark = scratch_.size();
    if (!at(TokenKind::RParen)) {
        for (;;) {
            const NodeId arg = ternary();
            if (arg == kNoNode) {
                return kNoNode;
            }
            scratch_.push_back(arg);
            if (!at(TokenKind::Comma)) {
                break;
            }
            if (!advance()) {
                return kNoNode;
            }
            if (at(TokenKind::RParen)) {
                return fail(cur_.offset, "trailing comma");
            }
        }
    }
    if (!at(TokenKind::RParen)) {
        return fail(cur_.offset, "expected ')'");
    }
    if (!advance()) {
        return kNoNode;
    }
    const auto count = static_cast<std::uint32_t>(scratch_.size() - mark);
    const NodeId node = ast_.call(name, scratch_.data() + mark, count);
    scratch_.resize(mark);
    return node;
}
```

В `primary()` ветку `TokenKind::Identifier` заменить на:

```cpp
        case TokenKind::Identifier: {
            const Token token = cur_;
            if (!advance()) {
                return kNoNode;
            }
            // §5.3: Call — альтернатива Primary, а не постфиксная операция.
            // Поэтому f(a)(b) не разбирается, а keys(o)[0] — да.
            if (at(TokenKind::LParen)) {
                return callArguments(token);
            }
            return ast_.identifier(token);
        }
```

- [ ] **Step 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "ParserPostfix|ParserEarlyErrors"`
Expected: `ParserPostfix` — все 10 зелёные; в `ParserEarlyErrors` добавились `DotWithoutName`, `UnclosedBracket`, `TrailingCommaInCall`, `UnclosedCall` — итого 9 зелёных из 17.

- [ ] **Step 6: Проверить, что база задачи 5 не деградировала**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_filter='BM_Parse_(Chain|Precedence)' \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > /tmp/parse-task6.json
python3 tools/bench-compare.py /tmp/parse-task5.json /tmp/parse-task6.json
```

Expected: код возврата 0. Если 1 — разобраться до коммита: постфикс не должен трогать цену цепочки приоритетов.

- [ ] **Step 7: Коммит**

```bash
git add core/src/parser.cpp
git commit -m "Implement postfix access and calls"
```

---

## Task 7: Литералы массива и объекта

**Files:**
- Modify: `core/src/parser.cpp`
- Test: `core/tests/parser_test.cpp`

**Interfaces:**
- Consumes: `Parser::scratch_` и `Parser::callArguments` из задачи 6.
- Produces: методы `NodeId arrayLiteral()` и `NodeId objectLiteral()`.

- [ ] **Step 1: Убедиться, что тесты агрегатов красные**

Run: `ctest --test-dir build --output-on-failure -R "ParserPrimary"`
Expected: 8 зелёных, 5 красных (`EmptyArrayLiteral`, `ArrayLiteralKeepsOrder`, `EmptyObjectLiteral`, `ObjectLiteralInterleavesKeysAndValues`, `AggregatesNest`).

- [ ] **Step 2: Объявить методы**

В приватную часть класса `Parser`:

```cpp
    NodeId arrayLiteral();
    NodeId objectLiteral();
```

- [ ] **Step 3: Добавить тела рядом с `callArguments`**

```cpp
NodeId Parser::arrayLiteral() {
    const std::uint32_t offset = cur_.offset;  // '['
    if (!advance()) {
        return kNoNode;
    }
    const std::size_t mark = scratch_.size();
    if (!at(TokenKind::RBracket)) {
        for (;;) {
            const NodeId item = ternary();
            if (item == kNoNode) {
                return kNoNode;
            }
            scratch_.push_back(item);
            if (!at(TokenKind::Comma)) {
                break;
            }
            if (!advance()) {
                return kNoNode;
            }
            if (at(TokenKind::RBracket)) {
                return fail(cur_.offset, "trailing comma");
            }
        }
    }
    if (!at(TokenKind::RBracket)) {
        return fail(cur_.offset, "expected ']'");
    }
    if (!advance()) {
        return kNoNode;
    }
    const auto count = static_cast<std::uint32_t>(scratch_.size() - mark);
    const NodeId node = ast_.array(scratch_.data() + mark, count, offset);
    scratch_.resize(mark);
    return node;
}

NodeId Parser::objectLiteral() {
    const std::uint32_t offset = cur_.offset;  // '{'
    if (!advance()) {
        return kNoNode;
    }
    const std::size_t mark = scratch_.size();
    if (!at(TokenKind::RBrace)) {
        for (;;) {
            if (!at(TokenKind::String)) {
                return fail(cur_.offset,
                            "object key must be a string literal");
            }
            const Token key = cur_;
            if (!advance()) {
                return kNoNode;
            }
            if (!at(TokenKind::Colon)) {
                return fail(cur_.offset, "expected ':' after object key");
            }
            if (!advance()) {
                return kNoNode;
            }
            // Ключ кладётся до разбора значения: вложенный литерал пометит
            // вершину буфера уже за ним и вернёт её на место сам.
            scratch_.push_back(ast_.string(key));
            const NodeId value = ternary();
            if (value == kNoNode) {
                return kNoNode;
            }
            scratch_.push_back(value);
            if (!at(TokenKind::Comma)) {
                break;
            }
            if (!advance()) {
                return kNoNode;
            }
            if (at(TokenKind::RBrace)) {
                return fail(cur_.offset, "trailing comma");
            }
        }
    }
    if (!at(TokenKind::RBrace)) {
        return fail(cur_.offset, "expected '}'");
    }
    if (!advance()) {
        return kNoNode;
    }
    const auto count = static_cast<std::uint32_t>(scratch_.size() - mark);
    const NodeId node = ast_.object(scratch_.data() + mark, count, offset);
    scratch_.resize(mark);
    return node;
}
```

- [ ] **Step 4: Добавить две ветки в `primary()`**

Перед `default:`:

```cpp
        case TokenKind::LBracket:
            return arrayLiteral();
        case TokenKind::LBrace:
            return objectLiteral();
```

- [ ] **Step 5: Собрать и прогнать**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure -R "ParserPrimary|ParserEarlyErrors"`
Expected: `ParserPrimary` — все 13 зелёные; в `ParserEarlyErrors` добавились `TrailingCommaInArray`, `TrailingCommaInObject`, `ObjectKeyMustBeStringLiteral`, `ObjectPairNeedsColon` — итого 13 зелёных из 17.

- [ ] **Step 6: Проверить базы задач 5 и 6**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_filter='BM_Parse_(Chain|Precedence|Postfix)' \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > /tmp/parse-task7.json
python3 tools/bench-compare.py /tmp/parse-task6.json /tmp/parse-task7.json
```

Expected: код возврата 0. `BM_Parse_Postfix` в базе задачи 6 отсутствует, и это нормально: сравнение падает только на пропавших бенчмарках, не на новых.

- [ ] **Step 7: Коммит**

```bash
git add core/src/parser.cpp
git commit -m "Implement array and object literals"
```

---

## Task 8: Стейтменты и режим программы

**Files:**
- Modify: `core/src/parser.cpp`
- Test: `core/tests/parser_test.cpp`

**Interfaces:**
- Consumes: весь `Parser` из задач 5–7.
- Produces: `bool Parser::statement(NodeId &out)`, `bool Parser::runProgram(Diagnostic &diag)`, свободная функция `parseProgram` в рабочем виде.

- [ ] **Step 1: Убедиться, что `ParserStatement` красная**

Run: `ctest --test-dir build --output-on-failure -R "ParserStatement|ParserModes"`
Expected: `ParserStatement` — 11 красных; `ParserModes` — 3 зелёных, 2 красных.

- [ ] **Step 2: Добавить распознаватель операторов присваивания**

Рядом с `isComparisonOp`:

```cpp
bool isAssignOp(TokenKind kind) noexcept {
    switch (kind) {
        case TokenKind::Assign:
        case TokenKind::PlusAssign:
        case TokenKind::MinusAssign:
        case TokenKind::StarAssign:
        case TokenKind::SlashAssign:
            return true;
        default:
            return false;
    }
}
```

- [ ] **Step 3: Объявить методы стейтментов**

В публичную часть класса `Parser`:

```cpp
    /// Стартовый символ Program, docs/grammar.md §5.1.
    bool runProgram(Diagnostic &diag);
```

В приватную:

```cpp
    /// Разбирает один стейтмент.
    ///
    /// Возвращает false при отказе. Пустой стейтмент — успех с out == kNoNode:
    /// ';' узла не порождает.
    bool statement(NodeId &out);

    /// Проверяет, что поддерево — LeftHandSide из §5.2: имя, к которому
    /// применены только '.' и '[]'. Вызов в цепочке делает цель недопустимой.
    [[nodiscard]] bool isLeftHandSide(NodeId node) const noexcept;
```

- [ ] **Step 4: Добавить тела**

```cpp
bool Parser::isLeftHandSide(NodeId node) const noexcept {
    NodeId current = node;
    for (;;) {
        switch (ast_.kind(current)) {
            case NodeKind::Identifier:
                return true;
            case NodeKind::Member:
            case NodeKind::Index:
                // Индексное выражение вправе содержать вызовы: ограничение
                // §5.2 касается только основания цепочки.
                current = ast_.child(current, 0);
                break;
            default:
                return false;
        }
    }
}

bool Parser::statement(NodeId &out) {
    out = kNoNode;
    if (at(TokenKind::Semicolon)) {
        return advance();  // EmptyStatement узла не порождает
    }
    if (!at(TokenKind::Identifier)) {
        fail(cur_.offset,
             "statement must be an assignment or a call");
        return false;
    }
    const std::uint32_t start = cur_.offset;
    const NodeId chain = postfix();
    if (chain == kNoNode) {
        return false;
    }

    if (isAssignOp(cur_.kind)) {
        const TokenKind op = cur_.kind;
        const std::uint32_t opOffset = cur_.offset;
        if (!isLeftHandSide(chain)) {
            fail(start,
                 "assignment target must be a name with '.' and '[]' access");
            return false;
        }
        if (!advance()) {
            return false;
        }
        const NodeId value = ternary();
        if (value == kNoNode) {
            return false;
        }
        if (!at(TokenKind::Semicolon)) {
            fail(cur_.offset, "expected ';'");
            return false;
        }
        if (!advance()) {
            return false;
        }
        out = ast_.assign(op, chain, value, opOffset);
        return true;
    }

    if (at(TokenKind::Semicolon) && ast_.kind(chain) == NodeKind::Call) {
        if (!advance()) {
            return false;
        }
        out = ast_.callStatement(chain, start);
        return true;
    }

    fail(start, "statement must be an assignment or a call");
    return false;
}

bool Parser::runProgram(Diagnostic &diag) {
    if (!advance()) {
        diag = diag_;
        return false;
    }
    const std::size_t mark = scratch_.size();
    while (!at(TokenKind::End)) {
        NodeId node = kNoNode;
        if (!statement(node)) {
            diag = diag_;
            return false;
        }
        if (node != kNoNode) {
            scratch_.push_back(node);
        }
    }
    const auto count = static_cast<std::uint32_t>(scratch_.size() - mark);
    ast_.setRoot(ast_.program(scratch_.data() + mark, count));
    scratch_.resize(mark);
    return true;
}
```

- [ ] **Step 5: Заменить свободную функцию `parseProgram`**

```cpp
bool parseProgram(const char *source, std::uint32_t length, Ast &ast,
                  Diagnostic &diag) {
    ast.setSource(source);
    Parser parser(source, length, ast);
    return parser.runProgram(diag);
}
```

- [ ] **Step 6: Собрать и прогнать всё**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: все тесты зелёные — 53 лексера, 13 `AstShape`, 78 парсера.

- [ ] **Step 7: Проверить базы предыдущих задач**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_filter='BM_Parse_(Chain|Precedence|Postfix|Aggregates)' \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > /tmp/parse-task8.json
python3 tools/bench-compare.py /tmp/parse-task7.json /tmp/parse-task8.json
```

Expected: код возврата 0.

- [ ] **Step 8: Коммит**

```bash
git add core/src/parser.cpp
git commit -m "Implement statements and program mode"
```

---

## Task 9: Замыкание слоя

**Files:**
- Modify: `benchmarks/baseline.json`

**Interfaces:**
- Consumes: всё, сделанное задачами 1–8.
- Produces: обновлённую базу производительности, включающую шесть бенчмарков парсера рядом с шестью бенчмарками лексера.

- [ ] **Step 1: Полный прогон с `-Werror`**

```bash
cmake -B build-werror -DCHUPASCRIPT_WERROR=ON
cmake --build build-werror -j
ctest --test-dir build-werror --output-on-failure
```

Expected: сборка без единого предупреждения, 144 теста зелёные.

- [ ] **Step 2: Прогон под ASan и UBSan**

```bash
cmake -B build-asan -DCHUPASCRIPT_SANITIZE_ADDRESS=ON -DCHUPASCRIPT_SANITIZE_UNDEFINED=ON
cmake --build build-asan -j
ctest --test-dir build-asan --output-on-failure
```

Expected: 144 теста зелёные, ни одного сообщения санитайзера. Особое внимание — четырём тестам `ParserLimits`: они существуют затем, чтобы падение стека проявилось здесь, а не на устройстве.

- [ ] **Step 3: Снять полную базу**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_format=json > /tmp/full-baseline.json
```

Проверить в выводе, что `cv` каждого бенчмарка не превышает 0.05. Если превышает — машина занята, повторить на спокойной системе.

- [ ] **Step 4: Проверить, что лексер не деградировал**

```bash
python3 tools/bench-compare.py benchmarks/baseline.json /tmp/full-baseline.json
```

Expected: код возврата 0. Бенчмарки лексера в базе есть, парсера — нет; новые не сравниваются, пропавших нет.

- [ ] **Step 5: Записать новую базу с меткой машины**

```bash
python3 - <<'PY'
import json, platform
with open('/tmp/full-baseline.json') as f:
    data = json.load(f)
data['context']['chupascript_machine'] = f"{platform.machine()} {platform.platform()}"
with open('benchmarks/baseline.json', 'w') as f:
    json.dump(data, f, indent=2, ensure_ascii=False)
    f.write('\n')
PY
```

- [ ] **Step 6: Убедиться, что новая база сама с собой сходится**

```bash
python3 tools/bench-compare.py benchmarks/baseline.json /tmp/full-baseline.json
```

Expected: код возврата 0.

- [ ] **Step 7: Коммит**

```bash
git add benchmarks/baseline.json
git commit -m "Record the parser performance baseline"
```

---

## Ожидаемые итоги

| Показатель | Значение |
|---|---|
| Тестов после задачи 9 | 144: 53 лексера, 13 `AstShape`, 78 парсера |
| Новых файлов | 6 |
| Бенчмарков в базе | 12: шесть лексера, шесть парсера |
| Комментариев `TODO(B<N>)` в коде | B7 и B10 в `ast.hpp`, B10 в `parser.cpp` |

Что слой **не** делает и делать не должен: проверок §6.1 и §6.2 (B11), раскодирования экранирования (B9), интернирования имён (B8), любой работы с ареной (B1, B7).
