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

/// Дерево разбора: хранение, строитель, аксессоры.
///
/// Ничем не владеет: текст имён и литералов — срезы исходного буфера, который
/// обязан пережить Ast (docs/backlog.md B12).
class Ast {
   public:
    Ast();

    /// Начинает новое дерево над этим исходником.
    ///
    /// Выбрасывает всё, что было построено раньше: Ast пригоден для повторного
    /// разбора. Буфер source обязан пережить Ast — имена и литералы хранятся
    /// срезами (docs/backlog.md B12).
    void reset(const char *source);

    /// Объявляет узел корнем дерева.
    void setRoot(NodeId node) noexcept;

    /// Помечает дерево прошедшим статические проверки (core/src/check.hpp).
    ///
    /// Ставит её только check при нуле находок; вычислитель требует её
    /// утверждением. Так «забыли проверить» падает на первом же тесте, а в
    /// релизе не стоит ничего.
    void markChecked() noexcept { checked_ = true; }
    [[nodiscard]] bool isChecked() const noexcept { return checked_; }

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
    /// Узел дерева.
    ///
    /// Черновик: поля не пересекаются, узел заведомо толстый.
    /// Упаковка отложена — см. docs/backlog.md B6.
    ///
    /// Раскладка скрыта языком, а не соглашением: Node — приватный вложенный
    /// тип Ast, и вне ast.cpp к полям обратиться нельзя. Единственный доступ
    /// снаружи — через аксессоры Ast. Это шов, ради которого решения B6–B10
    /// остаются отложенными.
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

    NodeId add(const Node &node);
    std::uint32_t pushChildren(const NodeId *ids, std::uint32_t count);

    const char *src_ = nullptr;
    NodeId root_ = kNoNode;
    bool checked_ = false;
    std::vector<Node> nodes_;      // TODO(B7): переехать в арену контекста
    std::vector<NodeId> children_; // TODO(B10): боковой пул детей
};

}  // namespace CS
