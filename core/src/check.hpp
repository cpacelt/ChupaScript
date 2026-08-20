#pragma once
#include <cstdint>
#include <string_view>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "store.hpp"

namespace CS {

class HostTable;

/// Which of the two modes a tree is compiled in (docs/semantics.md §3.1).
///
/// check needs it for one rule and one only: a host function that is not
/// effect-free may be called from a script and may not be called from an
/// expression.
///
/// NOT a parameter of the compile entry points: the door decides it.
/// compileExpression is the expression mode and compileScript is the script
/// mode, always; passing it separately would admit the pair "expression door,
/// script mode", which does not exist and which nothing could then reject.
enum class CompileMode : std::uint8_t { Expression, Script };

/// Проверяет дерево правилами docs/grammar.md §6 — теми, что требуют сведений
/// за пределами грамматики.
///
/// Возвращает, сколько ошибок нашлось; в out кладёт не больше capacity первых.
/// Возвращённое число может превысить capacity: вызывающий узнаёт, что нашлось
/// больше, чем поместилось. Ноль — дерево пригодно к вычислению, и на нём
/// ставится отметка markChecked.
///
/// Из store читается **только состав имён** (hasGlobal): значения проверкам не
/// нужны, поэтому инструменту валидации довольно хранилища, где под каждым
/// объявленным именем лежит null.
///
/// Проход не останавливается на первой ошибке — иначе исправлять пришлось бы по
/// одной.
///
/// source обязан быть тем же текстом, над которым дерево построено: проверке
/// нужны имена, а их дерево хранит смещениями в нём.
///
/// hosts and mode carry no default: only the four compile doors call check,
/// and each of them knows both without guessing — hosts is whatever table
/// (possibly none) the caller passed in, and mode is fixed by which door it
/// is. A default here would let a fifth caller silently pick "no hosts,
/// expression mode" instead of saying so.
///
/// The doors above check DO default hosts to nullptr (compile.hpp), and the
/// two rules live together on purpose: those doors are reached by tools and
/// tests that genuinely own no HostTable, while this one is reached only by
/// them, where a silent wrong answer would be the end of the line.
std::uint32_t check(Ast &ast, std::string_view source, const Store &store,
                    Diagnostic *out, std::uint32_t capacity,
                    const HostTable *hosts, CompileMode mode);

}  // namespace CS
