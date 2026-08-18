#include "context.hpp"

namespace CS {

bool Context::eval(const Expression &expr, Value *out, Diagnostic &diag) {
    return expr.eval(store_, exec_, out, diag);
}

EvalStatus Context::evalNumber(const Expression &expr, double *out,
                               Diagnostic &diag) {
    return expr.evalNumber(store_, exec_, out, diag);
}

EvalStatus Context::evalBool(const Expression &expr, bool *out,
                             Diagnostic &diag) {
    return expr.evalBool(store_, exec_, out, diag);
}

EvalStatus Context::evalString(const Expression &expr, std::string_view *out,
                               Diagnostic &diag) {
    return expr.evalString(store_, exec_, out, diag);
}

bool Context::run(const Script &script, Diagnostic &diag) {
    return script.run(store_, exec_, diag);
}

}  // namespace CS
