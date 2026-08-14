#include "compile.hpp"

#include "check.hpp"
#include "parser.hpp"

namespace CS {
namespace {

std::uint32_t reportParseFailure(const Diagnostic &diag, Diagnostic *out,
                                 std::uint32_t capacity) {
    if (capacity > 0 && out != nullptr) { out[0] = diag; }
    return 1;
}

}  // namespace

std::uint32_t compileExpression(const char *source, std::uint32_t length,
                                Ast &ast, const Store &store, Diagnostic *out,
                                std::uint32_t capacity) {
    Diagnostic diag;
    if (!parseExpression(source, length, ast, diag)) {
        return reportParseFailure(diag, out, capacity);
    }
    return check(ast, store, out, capacity);
}

std::uint32_t compileScript(const char *source, std::uint32_t length, Ast &ast,
                            const Store &store, Diagnostic *out,
                            std::uint32_t capacity) {
    Diagnostic diag;
    if (!parseScript(source, length, ast, diag)) {
        return reportParseFailure(diag, out, capacity);
    }
    return check(ast, store, out, capacity);
}

}  // namespace CS
