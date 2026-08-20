#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "builtin_id.hpp"
#include "chupascript/chupascript.h"

namespace CS {

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
    BadFlags,    ///< DETERMINISTIC without PURE
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
