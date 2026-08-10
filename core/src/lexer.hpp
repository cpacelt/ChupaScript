#pragma once
#include <cstdint>

#include "diagnostic.hpp"
#include "token.hpp"

namespace CS {

/// Классифицирует последовательность как ключевое слово (docs/grammar.md §4.5).
///
/// Возвращает TokenKind::Identifier, если слово не зарезервировано.
TokenKind keywordKind(const char *text, std::uint32_t length) noexcept;

/// Сканер исходного текста.
///
/// Не аллоцирует и не бросает исключений. Буфер source обязан пережить лексер.
class Lexer {
   public:
    Lexer(const char *source, std::uint32_t length) noexcept
        : src_(source), len_(length), pos_(0) {}

    /// Читает очередной токен.
    ///
    /// Возвращает false при ошибке и заполняет diag. В конце текста возвращает
    /// true и out.kind == TokenKind::End; повторные вызовы после этого дают
    /// End снова.
    bool next(Token &out, Diagnostic &diag) noexcept;

   private:
    bool skipTrivia(Diagnostic &diag) noexcept;
    bool lexString(Token &out, Diagnostic &diag) noexcept;
    bool lexNumber(Token &out, Diagnostic &diag) noexcept;
    void lexIdentifier(Token &out) noexcept;

    [[maybe_unused]] const char *src_;
    [[maybe_unused]] std::uint32_t len_;
    [[maybe_unused]] std::uint32_t pos_;
};

}  // namespace CS
