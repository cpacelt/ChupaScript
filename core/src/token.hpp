#pragma once
#include <cstdint>

namespace CS {

/// Вид токена. Соответствует docs/grammar.md §4.
enum class TokenKind : std::uint8_t {
    End,  ///< конец текста

    Identifier,
    Number,
    String,

    True,      ///< активные ключевые слова, §4.5
    False,
    Null,
    Reserved,  ///< зарезервировано на будущее, §4.5

    LParen,    ///< (
    RParen,    ///< )
    LBracket,  ///< [
    RBracket,  ///< ]
    LBrace,    ///< {
    RBrace,    ///< }

    Comma,      ///< ,
    Colon,      ///< :
    Semicolon,  ///< ;
    Dot,        ///< .
    Question,   ///< ?

    Plus,     ///< +
    Minus,    ///< -
    Star,     ///< *
    Slash,    ///< /
    Percent,  ///< %
    Bang,     ///< !

    Assign,       ///< =
    PlusAssign,   ///< +=
    MinusAssign,  ///< -=
    StarAssign,   ///< *=
    SlashAssign,  ///< /=

    Equal,         ///< ==
    NotEqual,      ///< !=
    Less,          ///< <
    Greater,       ///< >
    LessEqual,     ///< <=
    GreaterEqual,  ///< >=

    AndAnd,            ///< &&
    OrOr,              ///< ||
    QuestionQuestion   ///< ??
};

/// Один токен.
///
/// Ничем не владеет: [offset, offset + length) — участок исходного буфера,
/// который обязан пережить токен. Для String участок включает кавычки.
struct Token {
    TokenKind kind = TokenKind::End;
    bool hasEscape = false;    ///< String: содержит escape-последовательность
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    double number = 0.0;       ///< Number: значение литерала
};

/// Смещение содержимого строкового литерала. Требует kind == String.
inline std::uint32_t stringContentOffset(const Token &token) noexcept {
    return token.offset + 1;
}

/// Длина содержимого строкового литерала. Требует kind == String.
inline std::uint32_t stringContentLength(const Token &token) noexcept {
    return token.length - 2;
}

}  // namespace CS
