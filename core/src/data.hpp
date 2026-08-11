#pragma once
#include <string_view>

#include "context.hpp"
#include "diagnostic.hpp"

namespace CS {

/// Кладёт в контекст значение под именем name.
///
/// text — литерал ChupaScript целиком: число, строка, true, false, null,
/// массив или объект, произвольно вложенные
/// (docs/superpowers/specs/2026-08-11-chupascript-data-design.md §3).
/// Выражение литералом не является и отвергается: данные не вычисляются.
///
/// name обязано быть идентификатором (docs/grammar.md §4.4) и не совпадать с
/// зарезервированным словом (§4.5) — иначе программа не сможет к нему
/// обратиться.
///
/// При отказе возвращает false, заполняет diag и не заводит корня. Смещение в
/// diag считается от начала text — кроме ErrorCode::Name, для которой
/// смещение не определено (имя не является частью text).
bool setVariable(Context &ctx, std::string_view name, std::string_view text,
                 Diagnostic &diag);

}  // namespace CS
