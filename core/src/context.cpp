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

    // Вычисленная строка (format, str) живёт в арене операции: коробки у неё нет,
    // и удержать её нечем. На этом пути она обязана стать коробкой — иначе
    // chupa_value_retain оказался бы молчаливой ложью, а хост узнал бы об этом
    // через чтение освобождённой арены.
    //
    // Прочее проходит как есть: скаляр самодостаточен, коробка уже коробка.
    *out = exec_.promote(value);
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
    return setVariable(store_, name, text, diag);
}

bool Context::run(const Script &script, Diagnostic &diag) {
    beginOperation();
    return script.run(exec_, diag);
}

}  // namespace CS
