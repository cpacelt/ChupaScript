#pragma once
#include <cstdint>
#include <string_view>

#include "builtin_id.hpp"
#include "chupascript/chupascript.h"

namespace CS {

class HostTable;

/// What a call site resolved to, in one shape for both tables.
///
/// LAYOUT — what each field means and who fills it:
///
///   ref           kNoCallee when the name is in neither table; otherwise
///                 what goes into the Call node (builtin_id.hpp)
///   minArgs       from BuiltinInfo or from the registration
///   maxArgs       kVariadic — no upper bound
///   returnsValue  false — Void (docs/semantics.md §2.2)
///   effectFree    false — may not be called from an expression
///   call          the callback; set only when isHostCallee(ref)
///   userData      the host's receiver; set only when isHostCallee(ref)
///   cacheable     те же аргументы дают тот же ответ. У билтина — всегда:
///                 часов и флагов среди них нет. У хост-функции — из
///                 CHUPA_FN_CACHEABLE, и её отсутствие означает, что
///                 выражение с таким вызовом не кэшируется вовсе.
///
/// One shape for both tables, and one function producing it, because
/// otherwise check.cpp and eval.cpp each grow their own "builtin or host"
/// pair of branches and the rule for what counts as a call is smeared across
/// two files.
///
/// The callback travels inside Callee rather than as an index into the
/// HostTable: the struct is then self-contained, there is no second visit to
/// the table, and the question "is this index still good" never arises.
/// check reads neither of those two fields.
struct Callee {
    CalleeRef         ref = kNoCallee;
    std::uint8_t      minArgs = 0;
    std::uint8_t      maxArgs = 0;
    bool              returnsValue = false;
    bool              effectFree = false;
    ChupaHostFunction call = nullptr;
    void             *userData = nullptr;
    bool              cacheable = false;
};

/// Builtins first; a name in both tables is impossible, chupa_register
/// refuses it (host.hpp).
/// hosts may be nullptr — a compilation that was given no table has no host
/// functions at all, and the name simply will not be found among them.
[[nodiscard]] Callee resolveCallee(const HostTable *hosts,
                                   std::string_view name) noexcept;

/// The same for a name already resolved: evaluation has the CalleeRef in the
/// node and must not look the name up a second time.
///
/// Precondition: ref != kNoCallee.
[[nodiscard]] Callee calleeOf(const HostTable *hosts, CalleeRef ref) noexcept;

}  // namespace CS
