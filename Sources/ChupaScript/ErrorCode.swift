import ChupaScriptC

/// Разновидность ошибки движка.
///
/// Собственный тип, а не импортированный `ChupaErrorCode`: C-модуль
/// `ChupaScriptC` — деталь реализации обвязки, и потребитель не обязан его
/// импортировать, чтобы разобрать ошибку.
public enum ErrorCode: Sendable, Equatable {

    /// Ошибки нет. В `Error` не встречается — отсутствие ошибки выражается
    /// значением `nil`, а не этим случаем. Существует потому, что C API
    /// сообщает «ошибки нет» именно кодом, и разбор обязан его назвать.
    case none

    /// Текст не разобрался (`docs/grammar.md`).
    case syntax

    /// Имя неизвестно, либо цель присваивания негодная
    /// (`docs/semantics.md` §7).
    case name

    /// Значение оказалось не того типа, которого требует операция.
    case type

    /// Индекс за границей массива.
    case range

    /// Текст значения, поставленного хостом, разобрать не удалось.
    case data

    /// Нарушение контракта C API самим вызывающим.
    case usage

    /// Память не выделилась.
    case memory

    /// Значение корректно, но тип хоста из него не собирается.
    ///
    /// Возникает только в обвязке: движок вернул строку `'centre'`, а в
    /// перечислении, объявленном хостом, такого случая нет. Отдельный код, а не
    /// `type`, потому что чинить надо в разных местах — `type` указывает на
    /// текст выражения, этот на контент либо на неполноту перечисления.
    ///
    /// Единственный код, у которого `Error.offset` равен `nil`: позиции в
    /// исходном тексте у такой ошибки нет, текст выражения безупречен.
    case unrepresentable

    /// Код, которого не существовало на момент сборки этой обвязки.
    ///
    /// Случай нужен, чтобы обновление движка не превращалось в аварию на
    /// стороне хоста: неизвестный код доедет до него числом, а не потеряется
    /// и не свалит разбор.
    ///
    /// Тип совпадает с `ChupaErrorCode.rawValue` — сужать до `Int32` нельзя:
    /// `Int32(_:)` на непомещающемся значении роняет процесс, и ветка,
    /// существующая ради выживания, оказалась бы единственной, которая не
    /// выживает.
    case unrecognized(UInt32)
}

extension ErrorCode {

    /// Разбор кода, пришедшего из C API.
    internal init(_ raw: ChupaErrorCode) {
        switch raw {
        case CHUPA_ERR_NONE:   self = .none
        case CHUPA_ERR_SYNTAX: self = .syntax
        case CHUPA_ERR_NAME:   self = .name
        case CHUPA_ERR_TYPE:   self = .type
        case CHUPA_ERR_RANGE:  self = .range
        case CHUPA_ERR_DATA:   self = .data
        case CHUPA_ERR_USAGE:  self = .usage
        case CHUPA_ERR_MEMORY: self = .memory
        default:               self = .unrecognized(raw.rawValue)
        }
    }
}

extension ErrorCode: CustomStringConvertible {

    public var description: String {
        switch self {
        case .none:                    return "none"
        case .syntax:                  return "syntax"
        case .name:                    return "name"
        case .type:                    return "type"
        case .range:                   return "range"
        case .data:                    return "data"
        case .usage:                   return "usage"
        case .memory:                  return "memory"
        case .unrepresentable:         return "unrepresentable"
        case .unrecognized(let value): return "unrecognized(\(value))"
        }
    }
}
