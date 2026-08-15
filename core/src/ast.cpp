#include "ast.hpp"

#include <cassert>

namespace CS {

Ast::Ast() {
    // Индекс kNoNode занят пустышкой, чтобы 0 означал «нет узла» и обращение
    // к нему было безопасным.
    nodes_.push_back(Node{});
}

void Ast::reset(std::uint32_t sourceLength) {
    sourceLength_ = sourceLength;
    root_ = kNoNode;
    nodes_.clear();
    children_.clear();
    nodes_.push_back(Node{});  // индекс kNoNode
    // Дерево выброшено — отметка уходит вместе с ним.
    checked_ = false;
}

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

NodeId Ast::script(const NodeId *statements, std::uint32_t count) {
    Node node;
    node.kind = NodeKind::Script;
    node.offset = 0;
    node.childStart = pushChildren(statements, count);
    node.childCount = count;
    return add(node);
}

// ─── аксессоры ───────────────────────────────────────────────────────

NodeId Ast::root() const noexcept { return root_; }

NodeKind Ast::kind(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return nodes_[node].kind;
}

TokenKind Ast::op(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return nodes_[node].op;
}

std::uint32_t Ast::offset(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return nodes_[node].offset;
}

std::uint32_t Ast::childCount(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return nodes_[node].childCount;
}

NodeId Ast::child(NodeId node, std::uint32_t index) const noexcept {
    assert(node < nodes_.size());
    const Node &n = nodes_[node];
    if (index >= n.childCount) {
        return kNoNode;
    }
    return children_[n.childStart + index];
}

double Ast::numberValue(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return nodes_[node].number;
}

bool Ast::boolValue(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return nodes_[node].boolean;
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

bool Ast::hasEscape(NodeId node) const noexcept {
    assert(node < nodes_.size());
    return nodes_[node].hasEscape;
}

std::uint32_t Ast::nodeCount() const noexcept {
    return static_cast<std::uint32_t>(nodes_.size());
}

}  // namespace CS
