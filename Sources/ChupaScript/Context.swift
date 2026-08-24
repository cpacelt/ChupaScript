import Foundation
import ChupaScriptC

/// Owns a ChupaScript engine context.
///
/// Compiled units do not belong to the context: an `Expression` or a `Script`
/// owns its own handle and frees it when it is deallocated. Обратное неверно:
/// каждая единица держит на контекст сильную ссылку, поэтому контекст всегда
/// переживает свои выражения и скрипты.
///
/// Thread safety: one context = one thread at a time. The context MUST be
/// deallocated on the same thread it was last used on — `deinit` calls
/// `chupa_context_destroy` which tears down the engine while the redraw
/// callback is still registered, and a callback in flight on another thread
/// during destruction is a race.
/// If the context was used on a background queue, capture it in a
/// `DispatchQueue.async` block on that queue and let it release there.
public final class Context {

    internal let handle: OpaquePointer

    /// Weak delegate for redraw notifications.
    public weak var delegate: ContextDelegate?

    /// Нехватка памяти здесь завершает процесс, и это решение, а не недосмотр
    /// (`docs/backlog.md` B46).
    ///
    /// Выделяется несколько сотен байт: два пустых вектора, часы, слоты эпох.
    /// Если их взять неоткуда, приложение уже убивают, и наш отказ — симптом,
    /// а не причина.
    ///
    /// Отказуемый конструктор эту беду не пережил бы, а переложил: обработать
    /// `nil` хосту нечем, и на месте вызова появился бы восклицательный знак —
    /// то же падение, только без имени и без сообщения. Хуже того, он обещал
    /// бы безопасность по памяти, которой у остального API нет: любая
    /// операция при `std::bad_alloc` завершает процесс на границе C
    /// (`docs/backlog.md` B21). Либо весь API переживает нехватку памяти, либо
    /// ни одна его часть; сегодня честно второе.
    ///
    /// Восстановимая нехватка памяти существует и живёт не здесь: разобрать
    /// большой экран и не смочь — обычное дело, и `compile` на это бросает
    /// `Error` с кодом `.memory`, отличимым от ошибки в тексте выражения.
    public init() {
        guard let h = chupa_context_create() else {
            fatalError("ChupaScript.Context: allocation failed")
        }
        handle = h
        registerRedrawCallback()
    }

    /// Снятие слушателя — до уничтожения, и порядок здесь не косметический.
    ///
    /// `chupa_context_destroy` отказывает, если его позвали посреди вычисления
    /// (`refuseWhileEvaluating`), и такой вызов достижим: флаг вычисления к
    /// моменту уведомления о перерисовке уже снят, поэтому хост, отпустивший
    /// последнюю ссылку прямо из своего `contextNeedsRedraw`, приходит сюда
    /// изнутри кадра той самой операции. На этом пути ядро остаётся жить — и
    /// без снятия в нём остался бы указатель на объект, который вот-вот
    /// перестанет существовать.
    ///
    /// Отменить `deinit` нельзя, а отказать в уничтожении ядро может. Значит
    /// единственное место, где снятие гарантированно происходит, — здесь, и
    /// до вызова, который вправе не сработать.
    deinit {
        chupa_context_on_redraw(handle, nil, nil)
        chupa_context_destroy(handle)
    }

    // MARK: - Set variables
    //
    // Все четыре сеттера бросают — тот же контракт, что у compile и run.
    // Отказ бывает двух природ, и хосту надо их различать: негодное имя
    // (`.name`) — ошибка в коде, неразбираемый текст значения (`.data`)
    // приходит с бэкенда и чинится контентом. Bool склеивал бы их в одно
    // «не получилось».

    /// Set global from a ChupaScript literal text (not JSON).
    /// Examples: "42", "true", "'hello'", "{'name': 'John', 'age': 30}"
    ///
    /// Object keys must be quoted: a bare identifier is parsed as a name and
    /// rejected as unknown.
    public func set(_ name: String, text: String) throws {
        let ok = name.withCString { namePtr in
            text.withCString { textPtr in
                chupa_context_set_data(handle, namePtr, name.utf8.count,
                                       textPtr, text.utf8.count)
            }
        }
        guard ok else { throw makeError() }
    }

    /// Set global to a boolean value.
    public func set(_ name: String, _ value: Bool) throws {
        let ok = name.withCString { ptr in
            chupa_context_set_bool(handle, ptr, name.utf8.count, value)
        }
        guard ok else { throw makeError() }
    }

    /// Set global to a number value.
    public func set(_ name: String, _ value: Double) throws {
        let ok = name.withCString { ptr in
            chupa_context_set_number(handle, ptr, name.utf8.count, value)
        }
        guard ok else { throw makeError() }
    }

    /// Set global to a string value (raw, no quoting needed).
    public func set(_ name: String, _ value: String) throws {
        let ok = name.withCString { namePtr in
            value.withCString { valuePtr in
                chupa_context_set_string(handle, namePtr, name.utf8.count,
                                         valuePtr, value.utf8.count)
            }
        }
        guard ok else { throw makeError() }
    }

    // MARK: - Compile

    /// Compile a ChupaScript expression promising a result of type `T`.
    ///
    /// `T` обычно выводится из места, куда выражение кладут:
    ///
    ///     struct ButtonProps {
    ///         var title: ChupaScript.Expression<String>
    ///     }
    ///     props.title = try context.compile(expression: "user.name")
    ///
    /// Имя квалифицировано не для красоты: с iOS 18 `Expression` объявляет и
    /// Foundation, поэтому без модуля запись неоднозначна везде, где импортирован
    /// он. Хосту, которому это многословно, ничего не мешает завести у себя
    /// `typealias` — какие имена у него заняты, знает только он сам.
    ///
    /// Где вывода нет, тип называется явно — см. перегрузку с `as:`.
    ///
    /// Throws on compile error (syntax, unknown global, etc.).
    public func compile<T>(expression source: String) throws -> Expression<T> {
        let h = source.withCString { ptr in
            chupa_compile_expression(handle, ptr, source.utf8.count)
        }
        guard let h else { throw makeError() }
        return Expression(handle: h, context: self)
    }

    /// То же, но с явно названным типом результата.
    ///
    ///     let align = try context.compile(expression: "card.align", as: Align.self)
    public func compile<T>(expression source: String,
                           as type: T.Type) throws -> Expression<T> {
        try compile(expression: source)
    }

    /// Compile a ChupaScript script (statements).
    /// Throws on compile error.
    public func compile(script source: String) throws -> Script {
        let h = source.withCString { ptr in
            chupa_compile_script(handle, ptr, source.utf8.count)
        }
        guard let h else { throw makeError() }
        return Script(handle: h, context: self)
    }

    // MARK: - Run

    /// Execute a compiled script.
    ///
    /// Бросает при ошибке выполнения. Часть изменений при этом могла успеть
    /// примениться — скрипт не транзакция (C API спека §7).
    public func run(_ script: Script) throws {
        guard chupa_run(handle, script.handle) else { throw makeError() }
    }

    // MARK: - Error

    /// Last error on this context, or nil if last operation succeeded.
    public var error: Error? {
        let raw = readError()
        guard raw.code != CHUPA_ERR_NONE else { return nil }
        return makeError(from: raw)
    }

    /// Reads the context's last error in one crossing of the C boundary.
    /// Callers who already know the last call failed (compile/run/set
    /// throwers) go through this instead of the `error` property, so they
    /// do not read the struct twice.
    internal func makeError() -> Error {
        makeError(from: readError())
    }

    /// `ChupaError.message` imports as a non-optional pointer (nothing in
    /// the header marks it nullable), so a Swift local of this type has no
    /// zero-argument initializer to pre-fill it with — the struct is read
    /// through an out-param instead, exactly as the C API declares it.
    ///
    /// Stack-allocated, not heap-allocated: `CSValue.chupaEval` calls
    /// `makeError()` — which routes through this method — on every null
    /// result, and a null-valued expression is an ordinary outcome on a
    /// BDUI screen, not a cold path. `withUnsafeTemporaryAllocation` gives
    /// the out-param a home for the duration of this call without a
    /// malloc/free pair on that warm path.
    private func readError() -> ChupaError {
        withUnsafeTemporaryAllocation(of: ChupaError.self, capacity: 1) { buffer in
            let ptr = buffer.baseAddress!
            chupa_context_error(handle, ptr)
            return ptr.pointee
        }
    }

    private func makeError(from raw: ChupaError) -> Error {
        let code = ErrorCode(raw.code)
        let offset = raw.offset
        // Сообщения — литералы из C++, ASCII целиком, так что проверять их
        // кодировку нечего (String.chupaFromValidUTF8).
        let message = String.chupaFromValidUTF8(raw.message, count: raw.message_len)
        return Error(code: code, message: message, offset: offset)
    }

    // MARK: - Redraw trampoline

    /// `passUnretained`, и вторая половина пары — снятие в `deinit`.
    ///
    /// Ядро `user_data` не удерживает и о смерти того, на кого он указывает,
    /// узнать не может; снятие — обязанность хоста (`chupascript.h`, блок про
    /// владение). `passRetained` эту обязанность не заменил бы, а сделал бы
    /// невыполнимой: контекст стал бы владельцем самого себя, `deinit` не
    /// случился бы никогда, а вместе с ним и снятие, которое в нём живёт.
    ///
    /// Сторожит `RedrawTests.testTheContextDoesNotRetainItself`: подмена
    /// одного слова здесь ломает освобождение тихо, отказом она не выглядит.
    private func registerRedrawCallback() {
        let ptr = Unmanaged.passUnretained(self).toOpaque()
        // The closure captures nothing, so it converts to a C function pointer.
        // Written inline rather than as a typed constant: the header is inside
        // an `assume_nonnull` region, and letting the parameter type drive
        // inference avoids restating the imported signature by hand.
        chupa_context_on_redraw(handle, { _, userData in
            guard let userData else { return }
            let ctx = Unmanaged<Context>.fromOpaque(userData).takeUnretainedValue()
            ctx.delegate?.contextNeedsRedraw(ctx)
        }, ptr)
    }
}
