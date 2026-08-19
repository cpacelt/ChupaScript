#include "compile.hpp"

#include <string>
#include <string_view>

#include "check.hpp"
#include "parser.hpp"
#include "text.hpp"

namespace CS {
namespace {

std::uint32_t reportParseFailure(const Diagnostic &diag, Diagnostic *out,
                                 std::uint32_t capacity) {
    if (capacity > 0 && out != nullptr) { out[0] = diag; }
    return 1;
}

/// Lays the bytes of each string literal into a box owned by the tree itself
/// (Ast::internLiteral) and stores the resulting pointer in the node.
///
/// A literal is immutable, so one copy suffices for the whole unit's life,
/// and evaluation reduces to reading the ready value out of the node.
/// Previously the copy was remade on every evaluation and went into the
/// store's pool, which is never freed piecemeal — so a string expression
/// re-evaluated every frame grew memory monotonically (docs/backlog.md B51).
///
/// Escapes are decoded right here, so the scratch buffer is set up once for
/// the whole pass rather than once per evaluation.
///
/// Called only after a successful check: on a tree with errors there is no
/// point laying out literals, and the boxes would sit unused until the
/// tree's death.
void internStringLiterals(Ast &ast, std::string_view source) {
    const NodeId root = ast.root();
    std::string scratch;
    // Тот же плоский цикл, что и в check: узлы лежат в пост-обходе, и обойти
    // надо все — литерал бывает где угодно, вплоть до ключа объекта.
    for (NodeId node = 1; node <= root; ++node) {
        if (ast.kind(node) != NodeKind::String) { continue; }
        // Узел дерева хранит указатель, а не готовый Value: шестнадцать байт в
        // него не влезли бы, не растя сам узел, а восемь ложатся туда, где у
        // узла без детей всё равно пустота.
        ast.setStringLiteral(node,
                             ast.internLiteral(literalText(ast, node, source, scratch)));
    }
}

}  // namespace

std::uint32_t compileExpression(const char *source, std::uint32_t length,
                                Ast &ast, Store &store, Diagnostic *out,
                                std::uint32_t capacity) {
    Diagnostic diag;
    if (!parseExpression(source, length, ast, diag)) {
        return reportParseFailure(diag, out, capacity);
    }
    const std::string_view text(source, length);
    const std::uint32_t errors = check(ast, text, store, out, capacity);
    if (errors == 0) { internStringLiterals(ast, text); }
    return errors;
}

std::uint32_t compileScript(const char *source, std::uint32_t length, Ast &ast,
                            Store &store, Diagnostic *out,
                            std::uint32_t capacity) {
    Diagnostic diag;
    if (!parseScript(source, length, ast, diag)) {
        return reportParseFailure(diag, out, capacity);
    }
    const std::string_view text(source, length);
    const std::uint32_t errors = check(ast, text, store, out, capacity);
    if (errors == 0) { internStringLiterals(ast, text); }
    return errors;
}

}  // namespace CS
