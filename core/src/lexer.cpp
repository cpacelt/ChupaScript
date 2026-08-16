#include <cstring>
#include <limits>

#include "double-conversion/string-to-double.h"
#include "lexer.hpp"

namespace CS {

namespace {

bool sameAs(const char *text, const char *word, std::uint32_t length) noexcept {
    return std::memcmp(text, word, length) == 0;
}

// Разбор числового литерала. Флаги пустые намеренно: пролёт строит сканер ниже
// и подаёт только [0-9]+ либо [0-9]+.[0-9]+ — ни знака, ни экспоненты, ни hex,
// ни пробелов. Всё, что конвертер умеет сверх этого, нам не нужно и только
// расширило бы принимаемый язык мимо docs/grammar.md §4.3.
//
// Значения для пустой строки и мусора — NaN: сканер таких пролётов не строит,
// и попадание сюда означало бы рассинхрон сканера с конвертером. Отличить его
// можно по числу разобранных символов, чем проверка ниже и занимается.
const double_conversion::StringToDoubleConverter &numberParser() noexcept {
    static const double_conversion::StringToDoubleConverter parser(
        double_conversion::StringToDoubleConverter::NO_FLAGS,
        std::numeric_limits<double>::quiet_NaN(),   // пустая строка
        std::numeric_limits<double>::quiet_NaN(),   // мусор
        nullptr,                                    // символа бесконечности нет
        nullptr);                                   // символа NaN нет
    return parser;
}

}  // namespace

bool Lexer::fail(Diagnostic &diag, std::uint32_t offset, const char *message) noexcept {
    failure_ = Diagnostic{ErrorCode::Syntax, offset, message};
    diag = failure_;
    return false;
}

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
                sameAs(text, "case", 4) || sameAs(text, "void", 4) ||
                sameAs(text, "type", 4)) {
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
                    return fail(diag, start, "unterminated block comment");
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
    if (failure_.code != ErrorCode::None) {
        diag = failure_;
        return false;
    }

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

    if (c == '\'' || c == '"') {
        return lexString(out, diag);
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

    return fail(diag, pos_, "unexpected byte");
}

bool Lexer::lexString(Token &out, Diagnostic &diag) noexcept {
    const char quote = src_[pos_];
    const std::uint32_t start = pos_;
    std::uint32_t end = pos_ + 1;
    bool hasEscape = false;

    for (;;) {
        if (end >= len_) {
            return fail(diag, start, "unterminated string literal");
        }

        const char c = src_[end];

        if (c == quote) {
            ++end;
            break;
        }

        // Сырой перевод строки обрывает литерал, чтобы забытая кавычка не
        // поглотила остаток исходника (docs/grammar.md §4.9).
        if (c == '\n' || c == '\r') {
            return fail(diag, end, "line break in string literal");
        }

        if (c == '\\') {
            if (end + 1 >= len_) {
                return fail(diag, start, "unterminated string literal");
            }
            const char escaped = src_[end + 1];
            if (escaped != '\\' && escaped != '\'' && escaped != '"' &&
                escaped != 'n' && escaped != 't') {
                return fail(diag, end, "unknown escape sequence");
            }
            hasEscape = true;
            end += 2;
            continue;
        }

        // Байты >= 0x80 переносятся без интерпретации: UTF-8 не декодируется.
        ++end;
    }

    out.kind = TokenKind::String;
    out.length = end - start;
    out.hasEscape = hasEscape;
    pos_ = end;
    return true;
}

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

    // Литерал вне диапазона double даёт значение IEEE, а не ошибку: Number
    // включает ±Infinity (docs/semantics.md §2.1), и язык последовательно
    // предпочитает значение IEEE отказу (§5.2). Конвертер сам возвращает
    // бесконечность при переполнении вверх и ноль при переполнении вниз, так
    // что различать их здесь нечем и незачем.
    int consumed = 0;
    out.number = numberParser().StringToDouble(
        src_ + start, static_cast<int>(end - start), &consumed);
    if (consumed != static_cast<int>(end - start)) {
        // Защитная проверка: для пролётов, которые строит этот сканер (только
        // цифры, либо цифры '.' цифры), недостижима, но остаётся единственной
        // структурной защитой разбора.
        return fail(diag, start, "malformed numeric literal");
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
