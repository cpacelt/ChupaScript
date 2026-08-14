#pragma once
#include <cstdint>

#include "ast.hpp"
#include "diagnostic.hpp"

namespace CS {

/// Разбирает выражение — стартовый символ Expression, docs/grammar.md §5.1.
///
/// При успехе возвращает true, заполняет ast и ставит ast.root().
/// При отказе возвращает false, заполняет diag, ast.root() остаётся kNoNode.
///
/// Буфер source обязан пережить ast: имена и литералы — срезы этого буфера
/// (docs/backlog.md B12).
bool parseExpression(const char *source, std::uint32_t length, Ast &ast,
                     Diagnostic &diag);

/// Разбирает скрипт — стартовый символ Script, docs/grammar.md §5.1.
///
/// Контракт совпадает с parseExpression. Пустой исходник даёт корень Script
/// без детей и не является ошибкой.
bool parseScript(const char *source, std::uint32_t length, Ast &ast,
                  Diagnostic &diag);

}  // namespace CS
