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
