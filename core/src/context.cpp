#include "context.hpp"

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

EvalStatus Context::evalString(const Expression &expr, std::string_view *out,
                               Diagnostic &diag) {
    beginOperation();
    return expr.evalString(exec_, out, diag);
}

bool Context::run(const Script &script, Diagnostic &diag) {
    beginOperation();
    return script.run(exec_, diag);
}

}  // namespace CS
