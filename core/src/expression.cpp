#include "expression.hpp"

#include "compile.hpp"
#include "eval.hpp"

namespace CS {

std::uint32_t Expression::compile(std::string_view source, Store &store,
                                  Expression *out, Diagnostic *diags,
                                  std::uint32_t capacity,
                                  const HostTable *hosts) {
    // Компиляция идёт в отдельную единицу и переносится в *out только при
    // успехе: иначе неудачный разбор портил бы уже рабочее выражение.
    Expression built;
    built.source_ = std::string(source);
    const std::uint32_t errors = compileExpression(
        built.source_.data(), static_cast<std::uint32_t>(built.source_.size()),
        built.ast_, store, diags, capacity, hosts);
    if (errors != 0) { return errors; }
    built.storeId_ = store.id();
    *out = std::move(built);
    return 0;
}

bool Expression::eval(Execution &exec, Value *out,
                      Diagnostic &diag) const {
    if (!exec.acceptsUnit(storeId_, diag)) { return false; }
    return evalExpression(ast_, source_, exec, out, diag);
}

namespace {

/// Набивает набор так, чтобы читатель, не посмотревший на *n, упал сразу.
void poison(Dep *deps, std::uint32_t *n) noexcept {
    for (std::uint32_t i = 0; i < kMaxDeps; ++i) { deps[i] = Dep{nullptr, Value::null()}; }
    *n = kDepsOverflow;
}

}  // namespace

bool Expression::evalTracked(Execution &exec, Value *out, Dep *deps,
                             std::uint32_t *n, Diagnostic &diag) const {
    if (!exec.acceptsUnit(storeId_, diag)) {
        poison(deps, n);
        return false;
    }
    if (!evalExpression(ast_, source_, exec, out, diag)) {
        poison(deps, n);
        return false;
    }
    // Отметка с компиляции, а не разбор дерева здесь: список вызываемых
    // известен с компиляции, и платить за него на каждом промахе незачем.
    // Агрегат в результате — спека §2.8: кэшировать нечего, а ручаться за
    // содержимое движок и не может. Строка сюда НЕ попадает: изменить её в
    // языке нечем.
    const bool aggregate = out->kind() == Value::Kind::Array ||
                           out->kind() == Value::Kind::Object;
    if (!ast_.isCacheable() || aggregate || exec.deps().overflowed()) {
        poison(deps, n);
        return true;
    }

    const DepSet &found = exec.deps();
    for (std::uint32_t i = 0; i < kMaxDeps; ++i) {
        deps[i] = i < found.count() ? found.at(i) : Dep{&kZeroEpoch, Value::null()};
    }
    *n = found.count();
    return true;
}

EvalStatus Expression::evalOfKind(Execution &exec, Value::Kind wanted,
                                  const char *message, Value *out,
                                  Diagnostic &diag) const {
    Value value = Value::null();
    if (!eval(exec, &value, diag)) { return EvalStatus::Error; }
    if (value.kind() == Value::Kind::Null) { return EvalStatus::Null; }
    if (value.kind() != wanted) {
        // Смещение настоящее: корень выражения на месте, и взять его есть где.
        // Прокладка ставила здесь ноль — то есть указывала в первый байт
        // исходника независимо от того, где на самом деле ошибка.
        diag = Diagnostic{ErrorCode::Type, ast_.offset(ast_.root()), message};
        return EvalStatus::Error;
    }
    *out = value;
    return EvalStatus::Ok;
}

EvalStatus Expression::evalNumber(Execution &exec, double *out,
                                  Diagnostic &diag) const {
    if (!exec.acceptsUnit(storeId_, diag)) { return EvalStatus::Error; }
    Value value = Value::null();
    const EvalStatus status = evalOfKind(exec, Value::Kind::Number,
                                         "eval_number: value is not a number",
                                         &value, diag);
    if (status == EvalStatus::Ok) { *out = value.numberValue(); }
    return status;
}

EvalStatus Expression::evalBool(Execution &exec, bool *out,
                                Diagnostic &diag) const {
    if (!exec.acceptsUnit(storeId_, diag)) { return EvalStatus::Error; }
    Value value = Value::null();
    const EvalStatus status = evalOfKind(exec, Value::Kind::Boolean,
                                         "eval_bool: value is not a boolean",
                                         &value, diag);
    if (status == EvalStatus::Ok) { *out = value.booleanValue(); }
    return status;
}

}  // namespace CS
