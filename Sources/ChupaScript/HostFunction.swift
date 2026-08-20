import ChupaScriptC

/// Флаги хост-функции — Swift-зеркало `ChupaFunctionFlags`.
///
/// Умолчание на `register` ниже — `[.returnsValue, .pure, .deterministic]` —
/// не расходится с тем, что в C нулевой флаг значит противоположное
/// («ничего»): в C ноль получает тот, кто поле не заполнил, а в Swift
/// умолчание пишет автор обвязки осознанно, значит и с чистой совестью
/// разрешает движку кэшировать результат и звать функцию из выражения.
public struct FunctionFlags: OptionSet, Sendable {
    public let rawValue: UInt32
    public init(rawValue: UInt32) { self.rawValue = rawValue }

    /// Без него функция ничего не возвращает — вызываема только из скрипта
    /// (`docs/semantics.md` §2.2).
    public static let returnsValue  = FunctionFlags(rawValue: 1 << 0)
    /// Без него функцию нельзя позвать из выражения — только из скрипта.
    public static let pure          = FunctionFlags(rawValue: 1 << 1)
    /// Разрешает движку не звать функцию повторно с теми же аргументами.
    public static let deterministic = FunctionFlags(rawValue: 1 << 2)
}

/// Перевод одного значения через C-границу в обе стороны.
///
/// Не расширение `CSValue` (`Sources/ChupaScript/CSValue.swift`): тот протокол
/// отвечает на другой вопрос — как достать себя из вычисленного `Expression`,
/// у него на входе выражение, а не значение. Впиши сюда перевод `ChupaValue` и
/// всякий тип, участвующий в `Expression` (включая обёртки над `RawRepresentable`,
/// которым обратный перевод не нужен никогда), был бы обязан его реализовать.
public protocol CSConvertible {

    /// nil — значение не того вида. Проверку видов движок не делает
    /// (спека §10.2), поэтому она здесь: движок знает только арность вызова.
    static func fromChupa(_ value: ChupaValue) -> Self?

    /// false — создать значение не удалось. Причина уже лежит в ошибке
    /// контекста (`chupa_make_string` пишет туда `CHUPA_ERR_MEMORY` при
    /// нехватке памяти и `CHUPA_ERR_USAGE`, если выходного слота нет), но
    /// отказом вызова это само по себе не становится: `chupa_fail` изнутри
    /// `chupa_make_string` не зовётся, и поднять отказ обязан тот, кто получил
    /// false, — трамплин ниже делает это броском.
    func intoChupa(_ handle: OpaquePointer,
                   _ out: UnsafeMutablePointer<ChupaValue>) -> Bool
}

extension Double: CSConvertible {
    public static func fromChupa(_ value: ChupaValue) -> Double? {
        var v = value
        guard chupa_value_kind(&v) == CHUPA_KIND_NUMBER else { return nil }
        return chupa_value_number(&v)
    }

    public func intoChupa(_ handle: OpaquePointer,
                          _ out: UnsafeMutablePointer<ChupaValue>) -> Bool {
        // handle не нужен: у числа нет пути отказа — chupa_make_number ничего
        // не выделяет и не бывает false.
        chupa_make_number(out, self)
        return true
    }
}

extension Bool: CSConvertible {
    public static func fromChupa(_ value: ChupaValue) -> Bool? {
        var v = value
        guard chupa_value_kind(&v) == CHUPA_KIND_BOOL else { return nil }
        return chupa_value_bool(&v)
    }

    public func intoChupa(_ handle: OpaquePointer,
                          _ out: UnsafeMutablePointer<ChupaValue>) -> Bool {
        chupa_make_bool(out, self)
        return true
    }
}

extension String: CSConvertible {
    /// Всё чтение — внутри `withUnsafePointer`, и это обязательно: правило 2
    /// заголовка говорит, что байты принадлежат той `ChupaValue`, чей адрес
    /// передали, а у строки короче шестнадцати байт они лежат ВНУТРИ неё.
    /// Прежний `var v = value; chupa_value_string(&v, …)` отдавал указатель на
    /// временную переменную, действительный только на время самого вызова, —
    /// и это горячий путь: короткий строковый аргумент как раз то, что меряет
    /// бенчмарк. `String` собирается здесь же, до выхода из замыкания, потому
    /// что дальше указателя уже нет.
    public static func fromChupa(_ value: ChupaValue) -> String? {
        withUnsafePointer(to: value) { v -> String? in
            guard chupa_value_kind(v) == CHUPA_KIND_STRING else { return nil }
            var bytes: UnsafePointer<CChar>?
            var length = 0
            chupa_value_string(v, &bytes, &length)
            // Байты пришли из значения, которое отдал сам движок хосту — тем
            // же путём, что и результат `chupa_eval_string`, так что
            // предусловие `chupaFromValidUTF8`
            // (`Sources/ChupaScript/UTF8.swift`) держится ровно как там.
            return String.chupaFromValidUTF8(bytes, count: length)
        }
    }

    public func intoChupa(_ handle: OpaquePointer,
                          _ out: UnsafeMutablePointer<ChupaValue>) -> Bool {
        withCString { ptr in
            chupa_make_string(handle, ptr, utf8.count, out)
        }
    }
}

extension Optional: CSConvertible where Wrapped: CSConvertible {
    /// `.some(nil)` на `CHUPA_KIND_NULL` — иначе `Double?` в аргументе был бы
    /// неотличим от несовпадения типа: `nil` из `fromChupa` уже значит
    /// «не тот вид», а не «значение отсутствует».
    public static func fromChupa(_ value: ChupaValue) -> Self? {
        var v = value
        if chupa_value_kind(&v) == CHUPA_KIND_NULL { return .some(.none) }
        guard let wrapped = Wrapped.fromChupa(value) else { return nil }
        return .some(wrapped)
    }

    public func intoChupa(_ handle: OpaquePointer,
                          _ out: UnsafeMutablePointer<ChupaValue>) -> Bool {
        switch self {
        case .none:
            chupa_make_null(out)
            return true
        case .some(let wrapped):
            return wrapped.intoChupa(handle, out)
        }
    }
}

/// Что живёт между регистрацией и разрушением контекста.
///
/// LAYOUT:
///   body   замыкание, вызываемое трамплином; закрыто над типизированным
///          телом хоста и над `Context`, к которому функция привязана
///
/// Коробка передаётся в `user_data` через `Unmanaged.passRetained`, а
/// `release` в дескрипторе снимает удержание. Массив коробок полем `Context`
/// не заведён: владение выражено самим дескриптором `ChupaFunction`, а не
/// тем, что кто-то не забыл сложить коробку в поле — ровно на этом уже
/// споткнулось ядро C++ этой же ветки: `setHosts()` завели отдельным
/// действием и не позвали ниоткуда (`core/src/execution.hpp`, коммит
/// c71c22f), проводка переехала в конструктор ровно затем, чтобы забыть
/// её означало не собрать объект вовсе.
final class HostBox {
    let body: (UnsafePointer<ChupaValue>?, Int,
              OpaquePointer, UnsafeMutablePointer<ChupaValue>?) throws -> Void

    init(body: @escaping (UnsafePointer<ChupaValue>?, Int,
                          OpaquePointer, UnsafeMutablePointer<ChupaValue>?) throws -> Void) {
        self.body = body
    }
}

/// Обратное соответствие кодам движка — только те, что хост-функция вправе
/// поднять сама: неопознанный код внутри модуля не рождается, а
/// `.unrecognized` пришёл бы только с чужого движка, чего здесь быть не может.
private func chupaRawErrorCode(for code: ErrorCode) -> ChupaErrorCode {
    switch code {
    // .none в CHUPA_ERR_NONE не переводится: «ошибки нет» — не причина
    // отказа, и `chupa_fail` этот код отвергает (`chupascript.h`), потеряв
    // вместе с ним сообщение. Брошенная ошибка с кодом `.none` — ошибка
    // прикладного кода, и HOST сохраняет хотя бы её текст; отвергнутая
    // альтернатива — пропустить код как есть — не сохраняет ничего.
    case .none:                  return CHUPA_ERR_HOST
    case .syntax:              return CHUPA_ERR_SYNTAX
    case .name:                return CHUPA_ERR_NAME
    case .type:                return CHUPA_ERR_TYPE
    case .range:                return CHUPA_ERR_RANGE
    case .data:                return CHUPA_ERR_DATA
    case .usage:                return CHUPA_ERR_USAGE
    case .memory:                return CHUPA_ERR_MEMORY
    case .host:                  return CHUPA_ERR_HOST
    // У .unrepresentable нет своего кода в C — смысл случая «значение есть,
    // но нужного типа не собрать», то есть ровно про тип, и ближайший по
    // смыслу код честнее общего CHUPA_ERR_HOST, который сказал бы только
    // «хост отказал», не называя причины. Отвергнутая альтернатива — свести
    // всё в HOST — теряет различимость, которая у хоста уже была.
    case .unrepresentable:       return CHUPA_ERR_TYPE
    // raw пришёл из C в первую очередь — обвязка его не узнала и сохранила
    // как есть; подменять его на HOST значило бы терять то, что сам движок
    // прислал. rawValue у ChupaErrorCode совпадает по типу с тем, что несёт
    // .unrecognized (ErrorCode.swift), так что конструктор не может отказать.
    case .unrecognized(let raw): return ChupaErrorCode(rawValue: raw)
    }
}

/// Трамплин — свободная функция с `@convention(c)`: захватывающее замыкание
/// в C-указатель не превращается, поэтому коробка едет через `user_data`, а
/// не через захват.
///
/// Ловит `Swift.Error`, а не `Error`: внутри модуля `Error` — собственный тип
/// обвязки (`Sources/ChupaScript/Error.swift`), затеняющий протокол
/// стандартной библиотеки.
private let chupaHostTrampoline: @convention(c) (OpaquePointer, UnsafePointer<ChupaValue>,
                                                 Int, UnsafeMutablePointer<ChupaValue>?,
                                                 UnsafeMutableRawPointer?) -> Bool = {
    ctx, args, argc, out, userData in
    guard let userData else { return false }
    let box = Unmanaged<HostBox>.fromOpaque(userData).takeUnretainedValue()
    do {
        try box.body(args, argc, ctx, out)
        return true
    } catch let error as ChupaScript.Error {
        error.message.withCString { chupa_fail(ctx, chupaRawErrorCode(for: error.code), $0, error.message.utf8.count) }
        return false
    } catch {
        let message = "\(error)"
        message.withCString { chupa_fail(ctx, CHUPA_ERR_HOST, $0, message.utf8.count) }
        return false
    }
}

/// Снимает удержание, взятое `Unmanaged.passRetained` при регистрации.
/// Движок зовёт это ровно один раз для каждой успешно зарегистрированной
/// функции, из `chupa_context_destroy` (`chupascript.h`, `ChupaFunction.release`).
private func chupaHostRelease(_ userData: UnsafeMutableRawPointer?) {
    guard let userData else { return }
    Unmanaged<HostBox>.fromOpaque(userData).release()
}

extension Context {

    /// Общая часть всех перегрузок `register`: собрать дескриптор, отдать
    /// коробку движку и бросить при отказе регистрации.
    ///
    /// Отдельным методом, а не повторена в каждой перегрузке арности, потому
    /// что арности различаются только разбором аргументов внутри `body`, а не
    /// тем, как дескриптор строится и передаётся `chupa_register`.
    private func registerDescriptor(_ name: String, minArgs: UInt8, maxArgs: UInt8,
                             flags: FunctionFlags,
                             body: @escaping (UnsafePointer<ChupaValue>?, Int,
                                              OpaquePointer, UnsafeMutablePointer<ChupaValue>?) throws -> Void) throws {
        let box = HostBox(body: body)
        let userData = Unmanaged.passRetained(box).toOpaque()
        let ok = name.withCString { namePtr -> Bool in
            var fn = ChupaFunction(
                name: namePtr,
                name_len: name.utf8.count,
                min_args: minArgs,
                max_args: maxArgs,
                flags: flags.rawValue,
                call: chupaHostTrampoline,
                user_data: userData,
                release: chupaHostRelease
            )
            return chupa_register(handle, &fn)
        }
        guard ok else {
            // Отказ не зовёт release (см. докблок `ChupaFunction.release` в
            // `chupascript.h`) — коробка остаётся на хосте, и снять удержание
            // обязана эта ветка, иначе она бы просто текла.
            Unmanaged<HostBox>.fromOpaque(userData).release()
            throw makeError()
        }
    }

    /// Типизированная перегрузка обязана быть объявлена с `.returnsValue`.
    ///
    /// Тип `R` с флагами не связан ничем, поэтому
    /// `register("f", flags: [.pure]) { 1.0 }` компилируется — а движок звал
    /// бы такую функцию с `out == nullptr` (`chupascript.h`,
    /// `ChupaHostFunction`), и класть результат было бы некуда. Отказ
    /// поднимается здесь, на регистрации, рядом со всеми прочими отказами
    /// регистрации; отвергнутая альтернатива — прежнее `out!` в трамплине —
    /// роняла процесс на первом же вызове вместо того, чтобы отказать.
    private func requireReturnsValue(_ name: String,
                                     _ flags: FunctionFlags) throws {
        guard flags.contains(.returnsValue) else {
            throw Error(code: .usage,
                        message: "\(name): типизированной перегрузке register нужен флаг .returnsValue",
                        offset: nil)
        }
    }

    /// Кладёт результат тела в слот движка.
    ///
    /// Оба отказа бросаются, а не глотаются: `intoChupa` возвращает false,
    /// когда `chupa_make_string` не смогла выделить память, и вернуть после
    /// этого `true` с ненаписанным `out` значило бы отдать движку мусор как
    /// результат. Пустой слот сюда уже не доходит — `requireReturnsValue`
    /// выше отсекает его на регистрации, — но проверка остаётся полом: она
    /// стоит одного сравнения и не даёт вернуться `out!`.
    ///
    /// Статический метод, а не метод экземпляра: замыкание, в котором он
    /// зовётся, живёт до разрушения контекста, и ссылка на `self` внутри него
    /// была бы циклом удержания.
    private static func deliver<R: CSConvertible>(
        _ name: String, _ value: R, _ ctx: OpaquePointer,
        _ out: UnsafeMutablePointer<ChupaValue>?
    ) throws {
        guard let out else {
            throw Error(code: .usage,
                        message: "\(name): движок не дал слота под результат — функция объявлена без .returnsValue",
                        offset: nil)
        }
        guard value.intoChupa(ctx, out) else {
            throw Error(code: .memory,
                        message: "\(name): результат не удалось создать",
                        offset: nil)
        }
    }

    /// Сырая регистрация — эскейп-люк для того, что `CSConvertible` не берёт:
    /// массивы, объекты, переменная арность (`maxArgs: 255` — `CHUPA_VARIADIC`
    /// в `chupascript.h`). Здесь, и только здесь, прикладной код видит
    /// `ChupaValue`.
    public func register(_ name: String, minArgs: UInt8, maxArgs: UInt8,
                         flags: FunctionFlags = [.returnsValue, .pure, .deterministic],
                         raw body: @escaping (UnsafeBufferPointer<ChupaValue>,
                                              OpaquePointer,
                                              UnsafeMutablePointer<ChupaValue>?) throws -> Void) throws {
        try registerDescriptor(name, minArgs: minArgs, maxArgs: maxArgs, flags: flags) { args, argc, ctx, out in
            try body(UnsafeBufferPointer(start: args, count: argc), ctx, out)
        }
    }

    /// Зарегистрировать функцию хоста без аргументов.
    public func register<R: CSConvertible>(
        _ name: String,
        flags: FunctionFlags = [.returnsValue, .pure, .deterministic],
        _ body: @escaping () throws -> R
    ) throws {
        try requireReturnsValue(name, flags)
        try registerDescriptor(name, minArgs: 0, maxArgs: 0, flags: flags) { _, _, ctx, out in
            try Context.deliver(name, body(), ctx, out)
        }
    }

    /// Зарегистрировать функцию хоста с одним аргументом.
    public func register<A: CSConvertible, R: CSConvertible>(
        _ name: String,
        flags: FunctionFlags = [.returnsValue, .pure, .deterministic],
        _ body: @escaping (A) throws -> R
    ) throws {
        try requireReturnsValue(name, flags)
        try registerDescriptor(name, minArgs: 1, maxArgs: 1, flags: flags) { args, _, ctx, out in
            guard let a = A.fromChupa(args![0]) else {
                throw Error(code: .type, message: "\(name): аргумент 1 не \(A.self)", offset: nil)
            }
            try Context.deliver(name, body(a), ctx, out)
        }
    }

    /// Зарегистрировать функцию хоста с двумя аргументами.
    public func register<A: CSConvertible, B: CSConvertible, R: CSConvertible>(
        _ name: String,
        flags: FunctionFlags = [.returnsValue, .pure, .deterministic],
        _ body: @escaping (A, B) throws -> R
    ) throws {
        try requireReturnsValue(name, flags)
        try registerDescriptor(name, minArgs: 2, maxArgs: 2, flags: flags) { args, _, ctx, out in
            guard let a = A.fromChupa(args![0]) else {
                throw Error(code: .type, message: "\(name): аргумент 1 не \(A.self)", offset: nil)
            }
            guard let b = B.fromChupa(args![1]) else {
                throw Error(code: .type, message: "\(name): аргумент 2 не \(B.self)", offset: nil)
            }
            try Context.deliver(name, body(a, b), ctx, out)
        }
    }

    /// Зарегистрировать функцию хоста с тремя аргументами.
    public func register<A: CSConvertible, B: CSConvertible, C: CSConvertible, R: CSConvertible>(
        _ name: String,
        flags: FunctionFlags = [.returnsValue, .pure, .deterministic],
        _ body: @escaping (A, B, C) throws -> R
    ) throws {
        try requireReturnsValue(name, flags)
        try registerDescriptor(name, minArgs: 3, maxArgs: 3, flags: flags) { args, _, ctx, out in
            guard let a = A.fromChupa(args![0]) else {
                throw Error(code: .type, message: "\(name): аргумент 1 не \(A.self)", offset: nil)
            }
            guard let b = B.fromChupa(args![1]) else {
                throw Error(code: .type, message: "\(name): аргумент 2 не \(B.self)", offset: nil)
            }
            guard let c = C.fromChupa(args![2]) else {
                throw Error(code: .type, message: "\(name): аргумент 3 не \(C.self)", offset: nil)
            }
            try Context.deliver(name, body(a, b, c), ctx, out)
        }
    }

    /// Зарегистрировать функцию хоста с четырьмя аргументами.
    public func register<A: CSConvertible, B: CSConvertible, C: CSConvertible, D: CSConvertible, R: CSConvertible>(
        _ name: String,
        flags: FunctionFlags = [.returnsValue, .pure, .deterministic],
        _ body: @escaping (A, B, C, D) throws -> R
    ) throws {
        try requireReturnsValue(name, flags)
        try registerDescriptor(name, minArgs: 4, maxArgs: 4, flags: flags) { args, _, ctx, out in
            guard let a = A.fromChupa(args![0]) else {
                throw Error(code: .type, message: "\(name): аргумент 1 не \(A.self)", offset: nil)
            }
            guard let b = B.fromChupa(args![1]) else {
                throw Error(code: .type, message: "\(name): аргумент 2 не \(B.self)", offset: nil)
            }
            guard let c = C.fromChupa(args![2]) else {
                throw Error(code: .type, message: "\(name): аргумент 3 не \(C.self)", offset: nil)
            }
            guard let d = D.fromChupa(args![3]) else {
                throw Error(code: .type, message: "\(name): аргумент 4 не \(D.self)", offset: nil)
            }
            try Context.deliver(name, body(a, b, c, d), ctx, out)
        }
    }
}
