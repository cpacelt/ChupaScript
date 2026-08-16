import ChupaScriptC

/// Тип, который умеет быть результатом вычисления.
///
/// Реализован для `Double`, `Bool` и `String` — ровно для тех трёх, под которые
/// в C API есть вход.
///
/// Требование существует ради статической диспетчеризации: `eval()` обязан
/// вызвать разную C-функцию в зависимости от типа результата, а выбрать её по
/// типу в Swift можно только так.
///
/// Свои типы подключаются одним словом — тело приезжает из умолчания ниже:
///
///     enum Align: String, CSValue { case left, right }
///     let align = try expression.eval()   // Align?
///
/// Цепочка разворачивается рекурсивно: если `RawValue` сам обёртка, разбор
/// доходит до базового типа.
///
/// **Почему конформанс обязателен.** Раньше обёртки работали вовсе без него:
/// `eval()` был объявлен дважды — для `T: CSValue` и для `T: RawRepresentable`.
/// Две перегрузки с одинаковой сигнатурой `() throws -> T?`, различаемые только
/// ограничениями, — конструкция, которая ломается от любого пересечения
/// конформансов. Первый же хост её и сломал: `CourierSwift` объявляет
/// `extension String: @retroactive RawRepresentable`, после чего под
/// `Expression<String>` подходили обе перегрузки, и вызвать `eval()` стало
/// нельзя ничем — типы возврата совпадают, разводить нечем. Теперь объявление
/// одно, выбирать не из чего. Сторожит `RetroactiveConformanceTests`.
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

// MARK: - Обёртки над базовыми типами

extension CSValue where Self: RawRepresentable, Self.RawValue: CSValue {

    /// Разбор обёртки: вычислить сырьё и собрать из него `Self`.
    ///
    /// Умолчание, а не отдельная перегрузка `eval()`, — см. объяснение у
    /// протокола. Тип, объявивший свою реализацию, берёт её: свидетелем
    /// требования становится явный член, а умолчание идёт в ход только когда
    /// своего нет. На это опирается `String`, у которого из-за чужого
    /// конформанса `RawValue == Self`: попади он сюда, разбор ушёл бы в самого
    /// себя без выхода.
    ///
    /// Помимо исходов базового разбора бросает `.unrepresentable`, когда сырьё
    /// корректно, но `Self` из него не собирается: движок вернул `'centre'`, а
    /// в перечислении такого случая нет. Отдельный код, а не `.type`, потому что
    /// чинить надо в разных местах — `.type` указывает на текст выражения,
    /// `.unrepresentable` на контент либо на неполноту самого перечисления.
    public static func chupaEval<U>(from expression: Expression<U>) throws -> Self? {
        guard let raw = try RawValue.chupaEval(from: expression) else { return nil }
        guard let value = Self(rawValue: raw) else {
            throw Error(code: .unrepresentable,
                        message: "'\(raw)' is not a valid \(Self.self)",
                        offset: nil)
        }
        return value
    }
}

// MARK: - Базовые типы

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
    ///
    /// Байты декодируются с заменой: `String` в языке — последовательность байт
    /// (`docs/semantics.md` §2.1), а не заведомо корректный UTF-8, и негодная
    /// последовательность станет U+FFFD, а не ошибкой. Для рендера это верно:
    /// испорченный контент показывается испорченным, а не роняет кадр.
    public static func chupaEval<U>(from expression: Expression<U>) throws -> String? {
        var raw: OpaquePointer?
        switch chupa_eval_string(expression.context.handle, expression.handle, &raw) {
        case CHUPA_OK:
            guard let raw else {
                // По контракту (chupascript.h, chupa_eval_string) на CHUPA_OK
                // указатель непустой. Ветка недостижима, но вернуть отсюда nil
                // нельзя: nil означает «язык дал null», и сломанный движок
                // отрапортовал бы валидным значением.
                throw Error(code: .usage,
                            message: "chupa_eval_string returned OK with no string",
                            offset: nil)
            }
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
