/// Псевдонимы с префиксом `CS` для каждого публичного типа модуля.
///
/// Модуль называет типы коротко — `Context`, `Expression`, `Error`, — и внутри
/// своего пространства имён это правильно. У потребителя же половина этих имён
/// занята: `Error` — протокол стандартной библиотеки, `Expression` с iOS 18
/// объявляет Foundation, `Script` встречается в чужих SDK. Писать
/// `ChupaScript.Expression` на каждой строке многословно, а
/// `import ChupaScript` без квалификации даёт неоднозначность.
///
/// Псевдонимы дают короткое и заведомо свободное написание. Они дополняют
/// исходные имена, а не заменяют их: обе формы обозначают один и тот же тип, и
/// значения свободно ходят между ними.
///
///     let context = CSContext()
///     let price: CSExpression = try context.compile(expression: "cart.total")
///
/// Новый публичный тип обязан появиться и здесь — иначе набор станет
/// дырявым, и это заметят ровно в тот момент, когда понадобится недостающее
/// имя.

public typealias CSContext = Context
public typealias CSContextDelegate = ContextDelegate
public typealias CSError = Error
public typealias CSErrorCode = ErrorCode
public typealias CSExpression = Expression
public typealias CSScript = Script
