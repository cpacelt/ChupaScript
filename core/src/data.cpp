#include "data.hpp"

#include <cassert>
#include <cstdint>

#include "ast.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "token.hpp"

namespace CS {
namespace {

/// Записывает отказ «в данных выражение недопустимо» с местом узла.
bool rejectNode(const Ast &ast, NodeId node, Diagnostic &diag) {
    diag = Diagnostic{ErrorCode::Data, ast.offset(node),
                      "expression is not allowed in data"};
    return false;
}

/// Идентификатор ли это по docs/grammar.md §4.2 и не ключевое ли слово (§4.5).
///
/// Проверка выполняется лексером, а не своей таблицей: так набор ключевых слов
/// и ограничение на ASCII заведомо совпадают с языком и не разъедутся с ним.
bool isRootName(std::string_view name) noexcept {
    if (name.empty() || name.size() > 0xffffffffu) { return false; }

    Lexer lexer(name.data(), static_cast<std::uint32_t>(name.size()));
    Diagnostic ignored;

    Token first;
    if (!lexer.next(first, ignored)) { return false; }
    if (first.kind != TokenKind::Identifier) { return false; }
    // Токен обязан покрывать имя целиком: иначе " state" и "state " прошли бы,
    // а обратиться к такому корню нельзя.
    if (first.offset != 0 || first.length != name.size()) { return false; }

    Token tail;
    if (!lexer.next(tail, ignored)) { return false; }
    return tail.kind == TokenKind::End;
}

// ctx пока не используется: агрегаты, которым нужен контекст для создания
// значений, появятся в задачах 3–5.
bool materialize(const Ast &ast, NodeId node, [[maybe_unused]] Context &ctx,
                 Value *out, Diagnostic &diag) {
    switch (ast.kind(node)) {
        case NodeKind::Number:
            *out = Value::number(ast.numberValue(node));
            return true;

        case NodeKind::Boolean:
            *out = Value::boolean(ast.boolValue(node));
            return true;

        case NodeKind::Null:
            *out = Value::null();
            return true;

        default:
            return rejectNode(ast, node, diag);
    }
}

}  // namespace

bool setVariable(Context &ctx, std::string_view name, std::string_view text,
                 Diagnostic &diag) {
    if (!isRootName(name)) {
        diag = Diagnostic{ErrorCode::Name, 0, "root name must be an identifier"};
        return false;
    }

    Ast ast;
    if (!parseExpression(text.data(), static_cast<std::uint32_t>(text.size()), ast,
                         diag)) {
        return false;
    }

    Value value = Value::null();
    if (!materialize(ast, ast.root(), ctx, &value, diag)) { return false; }

    // Корень заводится только после успеха: отказ не оставляет имени.
    ctx.setRoot(name, value);
    return true;
}

}  // namespace CS
