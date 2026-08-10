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
    /// В конце текста возвращает true и out.kind == TokenKind::End, и делает
    /// это идемпотентно: повторные вызовы после конца текста снова дают End.
    ///
    /// Возвращает false при ошибке и заполняет diag. С этого момента лексер
    /// необратимо неисправен: pos_ больше не двигается, и каждый следующий
    /// вызов заново возвращает тот же diag, не читая исходник.
    bool next(Token &out, Diagnostic &diag) noexcept;

   private:
    bool skipTrivia(Diagnostic &diag) noexcept;
    bool lexString(Token &out, Diagnostic &diag) noexcept;
    bool lexNumber(Token &out, Diagnostic &diag) noexcept;
    void lexIdentifier(Token &out) noexcept;

    /// Записывает отказ, копирует его вызывающему и запоминает навсегда.
    bool fail(Diagnostic &diag, std::uint32_t offset, const char *message) noexcept;

    const char *src_;
    std::uint32_t len_;
    std::uint32_t pos_;
    Diagnostic failure_{};
};

}  // namespace CS
