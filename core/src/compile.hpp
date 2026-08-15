#pragma once
#include <cstdint>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "store.hpp"

namespace CS {

/// Разбирает выражение и проверяет его: одна дверь вместо двух шагов.
///
/// Возвращает число найденных ошибок; 0 — успех, и дерево помечено пригодным к
/// вычислению. Ошибка разбора даёт ровно единицу: парсер останавливается на
/// первой. Ошибок проверки может быть сколько угодно, и в out попадает не
/// больше capacity первых.
///
/// Пережить дерево буфер source не обязан: имена и литералы хранятся в узлах
/// смещениями, а не срезами. Но тот же самый текст обязан быть передан
/// вычислителю (evalExpression, runScript) — иначе смещения укажут не туда.
std::uint32_t compileExpression(const char *source, std::uint32_t length,
                                Ast &ast, const Store &store, Diagnostic *out,
                                std::uint32_t capacity);

/// То же для скрипта (docs/semantics.md §3.1).
std::uint32_t compileScript(const char *source, std::uint32_t length, Ast &ast,
                            const Store &store, Diagnostic *out,
                            std::uint32_t capacity);

}  // namespace CS
