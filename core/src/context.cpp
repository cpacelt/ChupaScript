#include "context.hpp"

#include "data.hpp"

namespace CS {

bool Context::eval(const Expression &expr, Value *out, Diagnostic &diag) {
    beginOperation();
    return expr.eval(exec_, out, diag);
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
