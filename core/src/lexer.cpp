#include "lexer.hpp"

namespace CS {

TokenKind keywordKind(const char *, std::uint32_t) noexcept {
    return TokenKind::Identifier;
}

bool Lexer::next(Token &, Diagnostic &diag) noexcept {
    diag = Diagnostic{ErrorCode::Syntax, 0, "lexer not implemented"};
    return false;
}

bool Lexer::skipTrivia(Diagnostic &) noexcept { return true; }
bool Lexer::lexString(Token &, Diagnostic &) noexcept { return false; }
bool Lexer::lexNumber(Token &, Diagnostic &) noexcept { return false; }
void Lexer::lexIdentifier(Token &) noexcept {}

}  // namespace CS
