#include "lexer.hpp"

namespace CS {

TokenKind keywordKind(const char *, std::uint32_t) noexcept {
    return TokenKind::Identifier;
}

bool Lexer::skipTrivia(Diagnostic &diag) noexcept {
    for (;;) {
        while (pos_ < len_) {
            const char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
                continue;
            }
            break;
        }

        if (pos_ + 1 < len_ && src_[pos_] == '/' && src_[pos_ + 1] == '/') {
            pos_ += 2;
            while (pos_ < len_ && src_[pos_] != '\n') {
                ++pos_;
            }
            continue;
        }

        if (pos_ + 1 < len_ && src_[pos_] == '/' && src_[pos_ + 1] == '*') {
            const std::uint32_t start = pos_;
            pos_ += 2;
            for (;;) {
                if (pos_ + 1 >= len_) {
                    pos_ = len_;
                    diag = Diagnostic{ErrorCode::Syntax, start,
                                      "unterminated block comment"};
                    return false;
                }
                if (src_[pos_] == '*' && src_[pos_ + 1] == '/') {
                    pos_ += 2;
                    break;
                }
                ++pos_;
            }
            continue;
        }

        return true;
    }
}

bool Lexer::next(Token &out, Diagnostic &diag) noexcept {
    if (!skipTrivia(diag)) {
        return false;
    }

    out = Token{};
    out.offset = pos_;

    if (pos_ >= len_) {
        out.kind = TokenKind::End;
        return true;
    }

    diag = Diagnostic{ErrorCode::Syntax, pos_, "unexpected byte"};
    return false;
}

bool Lexer::lexString(Token &, Diagnostic &) noexcept { return false; }
bool Lexer::lexNumber(Token &, Diagnostic &) noexcept { return false; }
void Lexer::lexIdentifier(Token &) noexcept {}

}  // namespace CS
