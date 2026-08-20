#pragma once
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

#include "builtin_id.hpp"
#include "chupascript/chupascript.h"
#include "value.hpp"

namespace CS {

// ─── ChupaValue ⇄ Value: one crossing, shared by c_api.cpp and eval.cpp ───
//
// ChupaValue is the same sixteen bytes as CS::Value, and fromC reinterprets a
// host pointer as a CS::Value IN PLACE — no copy, no table, no allocation.
// c_api.cpp uses this to hand the host slices into its own ChupaValue
// variables (defect В3); eval.cpp uses it to hand a host callback the
// ArgFrame's contiguous block without copying it first.
//
// Defined once, here, because host.hpp is already included on both sides of
// the boundary — c_api.cpp transitively, through context.hpp's HostTable
// field, eval.cpp directly, for calleeOf's Callee. Two definitions in two
// files would be the "two truths about one thing" defect task 1 already
// fixed once.

static_assert(sizeof(ChupaValue) == sizeof(CS::Value),
              "ChupaValue обязан совпадать с CS::Value байт в байт");
static_assert(alignof(ChupaValue) >= alignof(CS::Value),
              "выравнивание ChupaValue не должно быть слабее");

/// Reinterprets the host's sixteen bytes as a CS::Value IN PLACE.
///
/// A reference, not a copy: a short string's bytes live inside the value, and
/// every slice handed back to the host points into the host's own variable.
inline const CS::Value &fromC(const ChupaValue *v) {
    return *reinterpret_cast<const CS::Value *>(v);
}

/// The forward direction of fromC: a Value's address seen as a ChupaValue's,
/// in place — no copy. toC cannot stand in for this: it copies one value
/// into a host slot, and an ArgFrame's block needs a view over many Values
/// at once, elementwise, with nothing moved. Both static_asserts above are
/// what license this direction too — size and alignment are exactly what an
/// elementwise view needs, for one Value or for an array of them alike.
inline ChupaValue *asC(Value *v) noexcept {
    return reinterpret_cast<ChupaValue *>(v);
}

/// const counterpart of asC above — see it for the reasoning.
inline const ChupaValue *asC(const Value *v) noexcept {
    return reinterpret_cast<const ChupaValue *>(v);
}

/// Copies a CS::Value into a host-owned output slot. The reverse of fromC:
/// here the host's storage is the destination, so a copy is unavoidable and
/// harmless — the sixteen bytes just landed in the caller's own variable,
/// which is exactly where the by-address contract wants them.
inline void toC(CS::Value v, ChupaValue *out) {
    std::memcpy(out, &v, sizeof(*out));
}

/// One function the host registered.
///
/// LAYOUT — what a record owns and what it merely points at:
///
///   name      std::string — an owning copy. The bytes the host passed in
///             belong to the host and this table cannot assume they outlive
///             registration.
///   call      the callback, owned by the host's code segment
///   userData  the host's receiver; this record does not own it, but it
///             OWNS THE DUTY to release it — see release below
///   release   called once, from ~HostTable, on userData
struct HostFunction {
    std::string       name;
    std::uint8_t      minArgs;
    std::uint8_t      maxArgs;
    std::uint32_t     flags;
    ChupaHostFunction call;
    void             *userData;
    void            (*release)(void *);
};

/// Why a registration was refused. Ok is not a refusal.
enum class RegisterOutcome : std::uint8_t {
    Ok,
    BadName,     ///< not an identifier, or a reserved word
    NameTaken,   ///< a builtin has it, or it is already registered
    NoCallback,  ///< call == nullptr
    BadArity,    ///< minArgs > maxArgs
    BadFlags,    ///< CACHEABLE without EFFECT_FREE
    TableFull,   ///< kMaxHostFunctions already registered
    TooLate,     ///< a compileExpression/compileScript already ran on this Context
    Reentrant,   ///< called from inside a host callback
};

/// The functions one Context holds.
///
/// Append-only for its whole life: a compiled unit resolved a name into an
/// index once and forever, and removal would leave that unit unusable with no
/// sign of it in the unit itself.
///
/// Lookup by name is linear. Registrations number in the tens and the lookup
/// happens once per call site at compile time, never at evaluation — the
/// sorted table plus binary search that findBuiltin uses would buy nothing
/// and would cost the stable indices that CalleeRef stores.
class HostTable {
   public:
    HostTable() = default;

    /// Calls release on every registered function exactly once. There is no
    /// other moment: the table only ever grows, so nothing is released
    /// before this.
    ~HostTable();

    HostTable(const HostTable &) = delete;
    HostTable &operator=(const HostTable &) = delete;
    HostTable(HostTable &&) = delete;
    HostTable &operator=(HostTable &&) = delete;

    /// Copies what it needs out of desc and keeps it. On any outcome other
    /// than Ok nothing is stored and desc.release is NOT called — the host
    /// still owns the box.
    RegisterOutcome add(const ChupaFunction &desc);

    /// nullptr when no function has that name. On success *index receives the
    /// number CalleeRef will carry.
    [[nodiscard]] const HostFunction *find(std::string_view name,
                                           std::uint8_t *index) const noexcept;

    /// Precondition: index < size().
    [[nodiscard]] const HostFunction &at(std::uint8_t index) const noexcept;

    [[nodiscard]] std::size_t size() const noexcept { return functions_.size(); }

   private:
    std::vector<HostFunction> functions_;
};

}  // namespace CS
