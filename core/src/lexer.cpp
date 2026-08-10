#include <charconv>
#include <cstring>
#include <system_error>

#include "lexer.hpp"

namespace CS {

namespace {

bool sameAs(const char *text, const char *word, std::uint32_t length) noexcept {
    return std::memcmp(text, word, length) == 0;
}

}  // namespace

TokenKind keywordKind(const char *text, std::uint32_t length) noexcept {
    // Переключатель по длине отсекает почти всё до единого memcmp.
    switch (length) {
        case 2:
            if (sameAs(text, "if", 2) || sameAs(text, "do", 2) ||
                sameAs(text, "in", 2)) {
                return TokenKind::Reserved;
            }
            break;
        case 3:
            if (sameAs(text, "let", 3) || sameAs(text, "var", 3) ||
                sameAs(text, "val", 3) || sameAs(text, "for", 3) ||
                sameAs(text, "new", 3) || sameAs(text, "try", 3)) {
                return TokenKind::Reserved;
            }
            break;
        case 4:
            if (sameAs(text, "true", 4)) {
                return TokenKind::True;
            }
            if (sameAs(text, "null", 4)) {
                return TokenKind::Null;
            }
            if (sameAs(text, "else", 4) || sameAs(text, "func", 4) ||
                sameAs(text, "this", 4) || sameAs(text, "self", 4) ||
                sameAs(text, "case", 4) || sameAs(text, "void", 4)) {
                return TokenKind::Reserved;
            }
            break;
        case 5:
            if (sameAs(text, "false", 5)) {
                return TokenKind::False;
            }
            if (sameAs(text, "while", 5) || sameAs(text, "break", 5) ||
                sameAs(text, "const", 5) || sameAs(text, "catch", 5) ||
                sameAs(text, "throw", 5) || sameAs(text, "class", 5)) {
                return TokenKind::Reserved;
            }
            break;
        case 6:
            if (sameAs(text, "return", 6) || sameAs(text, "typeof", 6) ||
                sameAs(text, "delete", 6) || sameAs(text, "switch", 6) ||
                sameAs(text, "import", 6) || sameAs(text, "export", 6)) {
                return TokenKind::Reserved;
            }
            break;
        case 7:
            if (sameAs(text, "default", 7)) {
                return TokenKind::Reserved;
            }
            break;
        case 8:
            if (sameAs(text, "continue", 8) || sameAs(text, "function", 8)) {
                return TokenKind::Reserved;
            }
            break;
        default:
            break;
    }
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

    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        lexIdentifier(out);
        return true;
    }

    if (c >= '0' && c <= '9') {
        return lexNumber(out, diag);
    }

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

bool Lexer::lexNumber(Token &out, Diagnostic &diag) noexcept {
    const std::uint32_t start = pos_;
    std::uint32_t end = pos_;
    while (end < len_ && src_[end] >= '0' && src_[end] <= '9') {
        ++end;
    }

    // Дробная часть берётся, только если за точкой стоит цифра: '3.' числом не
    // является и распадается на Number и Dot (docs/grammar.md §4.3).
    if (end + 1 < len_ && src_[end] == '.' && src_[end + 1] >= '0' &&
        src_[end + 1] <= '9') {
        end += 2;
        while (end < len_ && src_[end] >= '0' && src_[end] <= '9') {
            ++end;
        }
    }

    const std::from_chars_result parsed = std::from_chars(
        src_ + start, src_ + end, out.number, std::chars_format::fixed);
    if (parsed.ec != std::errc() || parsed.ptr != src_ + end) {
        diag = Diagnostic{ErrorCode::Syntax, start, "numeric literal out of range"};
        return false;
    }

    out.kind = TokenKind::Number;
    out.length = end - start;
    pos_ = end;
    return true;
}

void Lexer::lexIdentifier(Token &out) noexcept {
    std::uint32_t end = pos_ + 1;
    while (end < len_) {
        const char c = src_[end];
        const bool isPart = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '_';
        if (!isPart) {
            break;
        }
        ++end;
    }
    out.length = end - pos_;
    out.kind = keywordKind(src_ + pos_, out.length);
    pos_ = end;
}

}  // namespace CS
