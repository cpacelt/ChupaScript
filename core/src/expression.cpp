#include "expression.hpp"

#include "compile.hpp"
#include "eval.hpp"

namespace CS {

std::uint32_t Expression::compile(std::string_view source, Store &store,
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

bool Expression::eval(Store &store, Execution &exec, Value *out,
                      Diagnostic &diag) const {
    return evalExpression(ast_, source_, store, exec, out, diag);
}

EvalStatus Expression::evalOfKind(Store &store, Execution &exec,
                                  Value::Kind wanted, const char *message,
                                  Value *out, Diagnostic &diag) const {
    Value value = Value::null();
    if (!eval(store, exec, &value, diag)) { return EvalStatus::Error; }
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

EvalStatus Expression::evalNumber(Store &store, Execution &exec, double *out,
                                  Diagnostic &diag) const {
    Value value = Value::null();
    const EvalStatus status = evalOfKind(store, exec, Value::Kind::Number,
                                         "eval_number: value is not a number",
                                         &value, diag);
    if (status == EvalStatus::Ok) { *out = value.numberValue(); }
    return status;
}

EvalStatus Expression::evalBool(Store &store, Execution &exec, bool *out,
                                Diagnostic &diag) const {
    Value value = Value::null();
    const EvalStatus status = evalOfKind(store, exec, Value::Kind::Boolean,
                                         "eval_bool: value is not a boolean",
                                         &value, diag);
    if (status == EvalStatus::Ok) { *out = value.booleanValue(); }
    return status;
}

EvalStatus Expression::evalString(Store &store, Execution &exec,
                                  std::string_view *out,
                                  Diagnostic &diag) const {
    Value value = Value::null();
    const EvalStatus status = evalOfKind(store, exec, Value::Kind::String,
                                         "eval_string: value is not a string",
                                         &value, diag);
    if (status == EvalStatus::Ok) { *out = storeOf(store, exec, value).string(value); }
    return status;
}

}  // namespace CS
