#include "callee.hpp"

#include <cassert>

#include "builtin.hpp"
#include "host.hpp"

namespace CS {
namespace {

Callee fromBuiltin(Builtin id) noexcept {
    const BuiltinInfo &info = builtinInfo(id);
    Callee out;
    out.ref = calleeOfBuiltin(id);
    out.minArgs = info.minArgs;
    out.maxArgs = info.maxArgs;
    out.returnsValue = info.returnsValue;
    out.pure = info.pure;
    return out;
}

Callee fromHost(const HostFunction &fn, std::uint8_t index) noexcept {
    Callee out;
    out.ref = calleeOfHost(index);
    out.minArgs = fn.minArgs;
    out.maxArgs = fn.maxArgs;
    out.returnsValue = (fn.flags & CHUPA_FN_RETURNS_VALUE) != 0;
    out.pure = (fn.flags & CHUPA_FN_PURE) != 0;
    out.call = fn.call;
    out.userData = fn.userData;
    return out;
}

}  // namespace

Callee resolveCallee(const HostTable *hosts, std::string_view name) noexcept {
    Builtin id = Builtin::Count;
    if (findBuiltin(name, &id)) { return fromBuiltin(id); }

    if (hosts == nullptr) { return Callee{}; }
    std::uint8_t index = 0;
    if (const HostFunction *fn = hosts->find(name, &index)) {
        return fromHost(*fn, index);
    }
    return Callee{};
}

Callee calleeOf(const HostTable *hosts, CalleeRef ref) noexcept {
    assert(ref != kNoCallee);
    if (!isHostCallee(ref)) { return fromBuiltin(builtinOfCallee(ref)); }
    assert(hosts != nullptr &&
           "the node names a host function and there is no table");
    const std::uint8_t index = hostIndexOfCallee(ref);
    return fromHost(hosts->at(index), index);
}

}  // namespace CS
