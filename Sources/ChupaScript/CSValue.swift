import ChupaScriptC

/// Тип, который умеет быть результатом вычисления.
///
/// Реализован для `Double`, `Bool` и `String` — ровно для тех трёх, под которые
/// в C API есть вход. Четвёртому взяться неоткуда, поэтому реализовывать
/// протокол вручную не нужно и незачем: он публичный только потому, что
/// участвует в ограничении публичного расширения `Expression`.
///
/// Требование существует ради статической диспетчеризации: `eval()` обязан
/// вызвать разную C-функцию в зависимости от типа результата, а выбрать её по
/// типу в Swift можно только так.
///
/// Свои типы подключаются через `RawRepresentable` и не требуют ни строчки:
///
///     enum Align: String { case left, right }
///     let align = try expression.eval()   // Align?
///
/// Работает потому, что `Expression<T>` параметр не ограничивает, а `eval()`
/// приходит из расширения `where T: RawRepresentable, T.RawValue: CSValue`.
/// Цепочка разворачивается рекурсивно: если `RawValue` сам обёртка, разбор
/// доходит до базового типа.
public protocol CSValue {

    /// Достать значение этого типа из вычисленного выражения.
    ///
    /// Возвращает `nil`, если выражение дало `null`. Бросает `Error`, если
    /// движок сообщил об ошибке, включая несовпадение типа.
    ///
    /// Параметр обобщён по `U`, потому что тип, который достают, не обязан
    /// совпадать с тем, что обещает выражение: `Expression<Align>` вычисляет
    /// свой `String`, и `String` про `Align` знать не должен.
    static func chupaEval<U>(from expression: Expression<U>) throws -> Self?
}

extension Double: CSValue {

    public static func chupaEval<U>(from expression: Expression<U>) throws -> Double? {
        var out: Double = 0
        switch chupa_eval_number(expression.context.handle, expression.handle, &out) {
        case CHUPA_OK:   return out
        case CHUPA_NULL: return nil
        default:         throw expression.context.makeError()
        }
    }
}

extension Bool: CSValue {

    public static func chupaEval<U>(from expression: Expression<U>) throws -> Bool? {
        var out = false
        switch chupa_eval_bool(expression.context.handle, expression.handle, &out) {
        case CHUPA_OK:   return out
        case CHUPA_NULL: return nil
        default:         throw expression.context.makeError()
        }
    }
}

extension String: CSValue {

    /// Байты движок отдаёт в `ChupaString`, которую этот вызов обязан
    /// освободить; возвращаемая строка — их копия.
    public static func chupaEval<U>(from expression: Expression<U>) throws -> String? {
        var raw: OpaquePointer?
        switch chupa_eval_string(expression.context.handle, expression.handle, &raw) {
        case CHUPA_OK:
            guard let raw else { return nil }
            defer { chupa_string_destroy(raw) }
            var length = 0
            let bytes = chupa_string_bytes(raw, &length)
            return String(decoding: UnsafeRawBufferPointer(start: bytes, count: length),
                          as: UTF8.self)
        case CHUPA_NULL:
            return nil
        default:
            throw expression.context.makeError()
        }
    }
}
