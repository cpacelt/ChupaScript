#include "script.hpp"

#include "compile.hpp"
#include "eval.hpp"

namespace CS {

namespace {

/// Rejects a unit that belongs to another Store. The check is unconditional,
/// not an assert: a release build is exactly where the wrong slot would be
/// read silently, and a silent wrong value on screen is the failure this
/// closes. Duplicated from expression.cpp rather than shared: six lines do
/// not earn a header that later needs explaining.
bool belongsHere(std::uint32_t unitStoreId, const Execution &exec,
                 Diagnostic &diag) {
    if (unitStoreId == exec.persistent().id()) { return true; }
    diag = Diagnostic{ErrorCode::Usage, 0,
                      "unit was compiled against another context"};
    return false;
}

}  // namespace

std::uint32_t Script::compile(std::string_view source, Store &store,
                              Script *out, Diagnostic *diags,
                              std::uint32_t capacity) {
    // Компиляция идёт в отдельную единицу и переносится в *out только при
    // успехе: иначе неудачный разбор портил бы уже рабочий скрипт.
    Script built;
    built.source_ = std::string(source);
    const std::uint32_t errors = compileScript(
        built.source_.data(), static_cast<std::uint32_t>(built.source_.size()),
        built.ast_, store, diags, capacity);
    if (errors != 0) { return errors; }
    built.storeId_ = store.id();
    *out = std::move(built);
    return 0;
}

bool Script::run(Execution &exec, Diagnostic &diag) const {
    if (!belongsHere(storeId_, exec, diag)) { return false; }
    return runScript(ast_, source_, exec, diag);
}

}  // namespace CS
