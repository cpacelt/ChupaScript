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

    const char c = src_[pos_];

    // Двухбайтовые токены выигрывают у однобайтовых: правило максимального
    // жевания, docs/grammar.md §4.3.
    const bool hasNext = pos_ + 1 < len_;
    const char n = hasNext ? src_[pos_ + 1] : '\0';

    const auto emit = [&out, this](TokenKind kind, std::uint32_t length) noexcept {
        out.kind = kind;
        out.length = length;
        pos_ += length;
        return true;
    };

    switch (c) {
        case '(': return emit(TokenKind::LParen, 1);
        case ')': return emit(TokenKind::RParen, 1);
        case '[': return emit(TokenKind::LBracket, 1);
        case ']': return emit(TokenKind::RBracket, 1);
        case '{': return emit(TokenKind::LBrace, 1);
        case '}': return emit(TokenKind::RBrace, 1);
        case ',': return emit(TokenKind::Comma, 1);
        case ':': return emit(TokenKind::Colon, 1);
        case ';': return emit(TokenKind::Semicolon, 1);
        case '.': return emit(TokenKind::Dot, 1);

        case '+': return emit(n == '=' ? TokenKind::PlusAssign : TokenKind::Plus,
                              n == '=' ? 2 : 1);
        case '-': return emit(n == '=' ? TokenKind::MinusAssign : TokenKind::Minus,
                              n == '=' ? 2 : 1);
        case '*': return emit(n == '=' ? TokenKind::StarAssign : TokenKind::Star,
                              n == '=' ? 2 : 1);
        // '//' и '/*' уже съедены skipTrivia, здесь остаются только '/=' и '/'.
        case '/': return emit(n == '=' ? TokenKind::SlashAssign : TokenKind::Slash,
                              n == '=' ? 2 : 1);
        case '%': return emit(TokenKind::Percent, 1);

        case '=': return emit(n == '=' ? TokenKind::Equal : TokenKind::Assign,
                              n == '=' ? 2 : 1);
        case '!': return emit(n == '=' ? TokenKind::NotEqual : TokenKind::Bang,
                              n == '=' ? 2 : 1);
        case '<': return emit(n == '=' ? TokenKind::LessEqual : TokenKind::Less,
                              n == '=' ? 2 : 1);
        case '>': return emit(n == '=' ? TokenKind::GreaterEqual : TokenKind::Greater,
                              n == '=' ? 2 : 1);
        case '?': return emit(n == '?' ? TokenKind::QuestionQuestion : TokenKind::Question,
                              n == '?' ? 2 : 1);

        // Одиночные '&' и '|' в языке отсутствуют, docs/grammar.md §4.8.
        case '&':
            if (n == '&') {
                return emit(TokenKind::AndAnd, 2);
            }
            break;
        case '|':
            if (n == '|') {
                return emit(TokenKind::OrOr, 2);
            }
            break;

        default:
            break;
    }

    diag = Diagnostic{ErrorCode::Syntax, pos_, "unexpected byte"};
    return false;
}

bool Lexer::lexString(Token &, Diagnostic &) noexcept { return false; }
bool Lexer::lexNumber(Token &, Diagnostic &) noexcept { return false; }
void Lexer::lexIdentifier(Token &) noexcept {}

}  // namespace CS
