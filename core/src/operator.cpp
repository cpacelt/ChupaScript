#include "operator.hpp"

#include "box.hpp"

#include <cassert>
#include <cmath>

namespace CS {
namespace {

/// Записывает отказ по несовпадению типа операнда.
///
/// Единственный класс ошибок этой единицы: всё, что может пойти не так в
/// главе 5, — это неподходящий тип.
bool failType(std::uint32_t offset, const char *message, Diagnostic &diag) {
    diag = Diagnostic{ErrorCode::Type, offset, message};
    return false;
}

/// Оба ли операнда числа. Арифметика и сравнения порядка требуют этого
/// (docs/semantics.md §5.2, §5.3); приведения к числу в языке нет.
bool bothNumbers(Value lhs, Value rhs) {
    return lhs.kind() == Value::Kind::Number && rhs.kind() == Value::Kind::Number;
}

/// Арифметика (docs/semantics.md §5.2).
bool applyArithmetic(TokenKind op, Value lhs, Value rhs, std::uint32_t offset,
                     Value *out, Diagnostic &diag) {
    // Только Number, оба операнда. Конкатенации строк через + нет: строки
    // собирает format.
    if (!bothNumbers(lhs, rhs)) {
        return failType(offset, "arithmetic requires numbers", diag);
    }

    const double a = lhs.numberValue();
    const double b = rhs.numberValue();
    double result = 0.0;
    switch (op) {
        case TokenKind::Plus: result = a + b; break;
        case TokenKind::Minus: result = a - b; break;
        case TokenKind::Star: result = a * b; break;
        // Деление на ноль даёт бесконечность по IEEE 754 и ошибкой не является.
        case TokenKind::Slash: result = a / b; break;
        // Знак делимого: это std::fmod, а не остаток от целого деления.
        case TokenKind::Percent: result = std::fmod(a, b); break;
        // Отказ, а не break: иначе под NDEBUG функция вернула бы успех со
        // сфабрикованным значением, а неверное значение хуже ошибки.
        default:
            assert(false && "не арифметическая операция");
            return failType(offset, "unsupported arithmetic operator", diag);
    }

    *out = Value::number(result);
    return true;
}

/// Сравнение порядка (docs/semantics.md §5.3).
///
/// Только Number, оба операнда. Если хотя бы один NaN, все четыре оператора
/// дают false — поэтому **ни один из них не выводится отрицанием соседа**:
/// !(1 < NaN) истинно, тогда как 1 >= NaN ложно. Встроенные сравнения C++
/// ведут себя с NaN ровно так, как требует §5.3, и используются напрямую.
bool applyOrdering(TokenKind op, Value lhs, Value rhs, std::uint32_t offset,
                   Value *out, Diagnostic &diag) {
    if (!bothNumbers(lhs, rhs)) {
        return failType(offset, "ordering requires numbers", diag);
    }

    const double a = lhs.numberValue();
    const double b = rhs.numberValue();
    bool result = false;
    switch (op) {
        case TokenKind::Less: result = a < b; break;
        case TokenKind::Greater: result = a > b; break;
        case TokenKind::LessEqual: result = a <= b; break;
        case TokenKind::GreaterEqual: result = a >= b; break;
        default:
            assert(false && "не операция порядка");
            return failType(offset, "unsupported ordering operator", diag);
    }

    *out = Value::boolean(result);
    return true;
}

/// Равенство (docs/semantics.md §5.4).
///
/// Правила применяются в порядке перечисления, и порядок существенен: null
/// проверяется раньше несовпадения типов, поэтому null == 5 даёт false, а
/// 1 == '1' — ошибку.
bool valuesEqual(Value lhs, Value rhs, std::uint32_t offset, bool *out,
                 Diagnostic &diag) {
    // 1. Один из операндов null: равно тогда и только тогда, когда второй тоже.
    if (lhs.kind() == Value::Kind::Null || rhs.kind() == Value::Kind::Null) {
        *out = lhs.kind() == Value::Kind::Null && rhs.kind() == Value::Kind::Null;
        return true;
    }

    // 2. Типы различаются — ошибка.
    if (lhs.kind() != rhs.kind()) {
        return failType(offset, "equality requires operands of the same type", diag);
    }

    switch (lhs.kind()) {
        // 3. NaN не равен ничему, включая себя: это обычное сравнение double.
        case Value::Kind::Number:
            *out = lhs.numberValue() == rhs.numberValue();
            return true;

        // 4. Побайтово, без нормализации юникода.
        case Value::Kind::String:
            *out = stringBytes(lhs) == stringBytes(rhs);
            return true;

        // 5. По значению.
        case Value::Kind::Boolean:
            *out = lhs.booleanValue() == rhs.booleanValue();
            return true;

        // 6. По идентичности. Литерал создаёт новый агрегат при каждом
        // вычислении (§2.3), поэтому state.items == [1, 2] ложно всегда.
        case Value::Kind::Object:
        case Value::Kind::Array:
            *out = lhs.sameAggregate(rhs);
            return true;

        default:
            assert(false && "Null обработан правилом 1");
            return failType(offset, "equality is not defined for this type", diag);
    }
}

}  // namespace

bool applyUnary(TokenKind op, Value operand, std::uint32_t offset, Value *out,
                Diagnostic &diag) {
    switch (op) {
        case TokenKind::Bang:
            if (operand.kind() != Value::Kind::Boolean) {
                return failType(offset, "! requires a boolean", diag);
            }
            *out = Value::boolean(!operand.booleanValue());
            return true;

        case TokenKind::Minus:
            if (operand.kind() != Value::Kind::Number) {
                return failType(offset, "unary minus requires a number", diag);
            }
            // Над NaN даёт NaN, над нулём — отрицательный ноль: и то и другое
            // получается само, потому что это обычное отрицание double.
            *out = Value::number(-operand.numberValue());
            return true;

        default:
            assert(false && "applyUnary принимает только Bang и Minus");
            return failType(offset, "unsupported unary operator", diag);
    }
}

bool applyBinary(TokenKind op, Value lhs, Value rhs, std::uint32_t offset,
                 Value *out, Diagnostic &diag) {
    switch (op) {
        case TokenKind::Plus:
        case TokenKind::Minus:
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent:
            return applyArithmetic(op, lhs, rhs, offset, out, diag);

        case TokenKind::Less:
        case TokenKind::Greater:
        case TokenKind::LessEqual:
        case TokenKind::GreaterEqual:
            return applyOrdering(op, lhs, rhs, offset, out, diag);

        case TokenKind::Equal:
        case TokenKind::NotEqual: {
            bool equal = false;
            if (!valuesEqual(lhs, rhs, offset, &equal, diag)) {
                return false;
            }
            // != реализуется отрицанием ==, а не отдельной таблицей: тогда
            // разойтись они не могут по построению. Отказ уже вернулся выше,
            // поэтому «включая случай ошибки» выполняется само.
            *out = Value::boolean(op == TokenKind::Equal ? equal : !equal);
            return true;
        }

        default:
            assert(false && "applyBinary принимает только операции без короткого замыкания");
            return failType(offset, "unsupported binary operator", diag);
    }
}

}  // namespace CS
