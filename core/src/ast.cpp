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
