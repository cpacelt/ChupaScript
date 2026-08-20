#pragma once
#include <cstdint>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "store.hpp"

namespace CS {

class HostTable;

/// Разбирает выражение и проверяет его: одна дверь вместо двух шагов.
///
/// Возвращает число найденных ошибок; 0 — успех, и дерево помечено пригодным к
/// вычислению. Ошибка разбора даёт ровно единицу: парсер останавливается на
/// первой. Ошибок проверки может быть сколько угодно, и в out попадает не
/// больше capacity первых.
///
/// При успехе байты строковых литералов укладываются в пул текста store, а
/// получившиеся значения — в узлы (Ast::stringLiteral). Хранилище поэтому
/// изменяемое: компиляция в него пишет. При отказе не записывается ничего.
///
/// Пережить дерево буфер source не обязан: имена и литералы хранятся в узлах
/// смещениями, а не срезами. Но тот же самый текст обязан быть передан
/// вычислителю (evalExpression, runScript) — иначе смещения укажут не туда.
///
/// hosts defaults to nullptr, meaning "this caller owns no host functions at
/// all". The default is safe today for one reason and one only: the sole
/// owner of a HostTable is CS::Context, and both of its compile doors pass
/// theirs (context.hpp). Every other caller — the shell, the tests, the
/// benchmarks — has no table to forget.
///
/// It stops being safe the moment a SECOND owner of a HostTable appears: a
/// tool that has a table and reaches this door without it gets `unknown
/// function` on every host name, silently and plausibly, because name
/// resolution simply finds nothing (callee.cpp, resolveCallee). A new caller
/// that owns a table MUST pass it.
///
/// check.hpp one layer down deliberately has no default, and that is not a
/// contradiction with this one: check is the last layer, called by these four
/// doors only, and each of them knows the answer without guessing — so the
/// place where a mistake would be unrecoverable is the place with no default.
/// Removing the defaults here too was rejected: sixty-seven existing call
/// sites would have to spell out a nullptr they already mean.
std::uint32_t compileExpression(const char *source, std::uint32_t length,
                                Ast &ast, Store &store, Diagnostic *out,
                                std::uint32_t capacity,
                                const HostTable *hosts = nullptr);

/// То же для скрипта (docs/semantics.md §3.1).
std::uint32_t compileScript(const char *source, std::uint32_t length, Ast &ast,
                            Store &store, Diagnostic *out,
                            std::uint32_t capacity,
                            const HostTable *hosts = nullptr);

}  // namespace CS
