import Foundation

/// Error from the ChupaScript engine.
///
/// One type covers both compilation and evaluation: the engine reports both
/// through the same context slots (`chupa_context_error_code` and friends),
/// and `code` already tells the two apart. Naming it `Error` shadows the
/// standard-library protocol inside this module, so the protocol is spelled
/// `Swift.Error` here; outside the module both names stay unambiguous.
public struct Error: Swift.Error, CustomStringConvertible, Equatable, Sendable {
    public let code: ErrorCode
    public let message: String

    /// Смещение в байтах внутри исходного текста, на которое указывает ошибка.
    ///
    /// `nil`, когда указывать не на что: ошибку подняла обвязка, а не движок, и
    /// текст выражения при этом корректен. Единственный такой случай сегодня —
    /// `ErrorCode.unrepresentable`. Ноль был бы враньём: инструмент нарисовал
    /// бы каретку под первым символом безупречного выражения.
    public let offset: Int?

    /// Собрать ошибку вручную.
    ///
    /// Публичный не для полноты картины: хост, оборачивающий движок в свои
    /// типы, упирается ровно в тот же случай, что и обвязка, — значение
    /// корректно, а тип хоста из него не собирается (`ErrorCode.unrepresentable`).
    /// Так, OKBDUI разбирает значения свойств виджетов из JSON. Без этого
    /// инициализатора каждому такому хосту пришлось бы завести собственную
    /// ошибку о том же самом, и потребителю достались бы два типа вместо одного.
    ///
    /// `offset` при этом обычно `nil` — см. докблок свойства.
    public init(code: ErrorCode, message: String, offset: Int?) {
        self.code = code
        self.message = message
        self.offset = offset
    }

    public var description: String {
        guard let offset else { return "\(code): \(message)" }
        return "\(code) at \(offset): \(message)"
    }
}
