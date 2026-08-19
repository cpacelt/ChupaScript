#include "context.hpp"

#include "data.hpp"

namespace CS {

bool Context::eval(const Expression &expr, Value *out, Diagnostic &diag) {
    beginOperation();
    return expr.eval(exec_, out, diag);
}

bool Context::evalValue(const Expression &expr, Value *out, Diagnostic &diag) {
    beginOperation();
    Value value = Value::null();
    if (!expr.eval(exec_, &value, diag)) { return false; }

    // Every String, Object or Array a compiled expression can produce is
    // already a self-contained box (there is no other way to address one
    // any more), so there is nothing left to promote here: this used to be
    // where a temporary-arena string became a box, and that arena is gone.
    *out = value;
    return true;
}

EvalStatus Context::evalNumber(const Expression &expr, double *out,
                               Diagnostic &diag) {
    beginOperation();
    return expr.evalNumber(exec_, out, diag);
}

EvalStatus Context::evalBool(const Expression &expr, bool *out,
                             Diagnostic &diag) {
    beginOperation();
    return expr.evalBool(exec_, out, diag);
}

EvalStatus Context::evalString(const Expression &expr, std::string_view *out,
                               Diagnostic &diag) {
    beginOperation();
    return expr.evalString(exec_, out, diag);
}

bool Context::setVariableText(std::string_view name, std::string_view text,
                              Diagnostic &diag) {
    beginOperation();
    return setVariable(store_, exec_.deferred(), name, text, diag);
}

bool Context::run(const Script &script, Diagnostic &diag) {
    beginOperation();
    return script.run(exec_, diag);
}

}  // namespace CS
