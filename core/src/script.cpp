#include "script.hpp"

#include "compile.hpp"
#include "eval.hpp"

namespace CS {

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
    *out = std::move(built);
    return 0;
}

bool Script::run(Store &store, Diagnostic &diag) const {
    return runScript(ast_, source_, store, diag);
}

}  // namespace CS
