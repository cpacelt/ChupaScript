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
/// Пережить ast буфер source не обязан: дерево его не держит. Имена и литералы
/// в узлах — смещения, а байты приходят параметром в Ast::text. Тот же самый
/// текст, однако, придётся передать и всем последующим шагам — проверке,
/// вычислению, — иначе смещения укажут не туда (docs/backlog.md B12).
bool parseExpression(const char *source, std::uint32_t length, Ast &ast,
                     Diagnostic &diag);

/// Разбирает скрипт — стартовый символ Script, docs/grammar.md §5.1.
///
/// Контракт совпадает с parseExpression. Пустой исходник даёт корень Script
/// без детей и не является ошибкой.
bool parseScript(const char *source, std::uint32_t length, Ast &ast,
                  Diagnostic &diag);

}  // namespace CS
