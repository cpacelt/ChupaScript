#include "expression.hpp"

#include "compile.hpp"
#include "eval.hpp"

namespace CS {

std::uint32_t Expression::compile(std::string_view source, const Store &store,
                                  Expression *out, Diagnostic *diags,
                                  std::uint32_t capacity) {
    // Компиляция идёт в отдельную единицу и переносится в *out только при
    // успехе: иначе неудачный разбор портил бы уже рабочее выражение.
    Expression built;
    built.source_ = std::string(source);
    const std::uint32_t errors = compileExpression(
        built.source_.data(), static_cast<std::uint32_t>(built.source_.size()),
        built.ast_, store, diags, capacity);
    if (errors != 0) { return errors; }
    *out = std::move(built);
    return 0;
}

bool Expression::eval(Store &store, Value *out, Diagnostic &diag) const {
    return evalExpression(ast_, source_, store, out, diag);
}

}  // namespace CS
