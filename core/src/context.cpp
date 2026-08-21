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
    // Guard first, boundary second: the guard's assert is what catches a
    // reentrant evaluation in debug builds, and beginOperation() drains the
    // deferred list — running it first would do the damage before the assert
    // could report it.
    EvaluationGuard guard(evaluating_);
    beginOperation();
    return expr.eval(exec_, out, diag);
}

bool Context::evalTracked(const Expression &expr, Value *out, Dep *deps,
                          std::uint32_t *n, Diagnostic &diag) {
    EvaluationGuard guard(evaluating_);
    beginOperation();
    return expr.evalTracked(exec_, out, deps, n, diag);
}

EvalStatus Context::evalNumber(const Expression &expr, double *out,
                               Diagnostic &diag) {
    EvaluationGuard guard(evaluating_);
    beginOperation();
    return expr.evalNumber(exec_, out, diag);
}

EvalStatus Context::evalBool(const Expression &expr, bool *out,
                             Diagnostic &diag) {
    EvaluationGuard guard(evaluating_);
    beginOperation();
    return expr.evalBool(exec_, out, diag);
}

bool Context::setVariableText(std::string_view name, std::string_view text,
                              Diagnostic &diag) {
    // A refusal, not an assert: this signature has a way to say no, and the
    // C++ door has to say it in release too — cli/echo.cpp shows registration
    // going straight through CS::Context, so a callback holding a
    // CS::Context * reaches here without ever crossing the C API's own guard.
    if (evaluating_) {
        diag = Diagnostic{ErrorCode::Usage, 0, kClosedMessage};
        return false;
    }
    beginOperation();
    return setVariable(store_, exec_.deferred(), name, text, diag);
}

bool Context::run(const Script &script, Diagnostic &diag) {
    EvaluationGuard guard(evaluating_);
    beginOperation();
    return script.run(exec_, diag);
}

}  // namespace CS
