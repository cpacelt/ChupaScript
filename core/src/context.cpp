#include "context.hpp"

#include <cassert>

#include "data.hpp"

namespace CS {

namespace {

/// Raises the "a call is in flight" flag for one evaluation and lowers it on
/// every way out, including the failing ones.
///
/// A plain assignment at the top and bottom of eval() would leave the flag
/// raised forever on the first failure, and one bad frame would close the
/// Context for good.
class EvaluationGuard {
   public:
    explicit EvaluationGuard(bool &flag) noexcept : flag_(flag) {
        assert(!flag_ && "повторный вход в вычисление запрещён");
        flag_ = true;
    }
    ~EvaluationGuard() { flag_ = false; }
    EvaluationGuard(const EvaluationGuard &) = delete;
    EvaluationGuard &operator=(const EvaluationGuard &) = delete;

   private:
    bool &flag_;
};

}  // namespace

bool Context::eval(const Expression &expr, Value *out, Diagnostic &diag) {
    beginOperation();
    EvaluationGuard guard(evaluating_);
    return expr.eval(exec_, out, diag);
}

EvalStatus Context::evalNumber(const Expression &expr, double *out,
                               Diagnostic &diag) {
    beginOperation();
    EvaluationGuard guard(evaluating_);
    return expr.evalNumber(exec_, out, diag);
}

EvalStatus Context::evalBool(const Expression &expr, bool *out,
                             Diagnostic &diag) {
    beginOperation();
    EvaluationGuard guard(evaluating_);
    return expr.evalBool(exec_, out, diag);
}

bool Context::setVariableText(std::string_view name, std::string_view text,
                              Diagnostic &diag) {
    beginOperation();
    return setVariable(store_, exec_.deferred(), name, text, diag);
}

bool Context::run(const Script &script, Diagnostic &diag) {
    beginOperation();
    EvaluationGuard guard(evaluating_);
    return script.run(exec_, diag);
}

}  // namespace CS
