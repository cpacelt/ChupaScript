import Foundation
import ChupaScriptC

/// Owns a ChupaScript engine context.
///
/// Compiled units do not belong to the context: an `Expression` or a `Script`
/// owns its own handle and frees it when it is deallocated, in any order
/// relative to this context.
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

    public init() {
        guard let h = chupa_context_create() else {
            fatalError("ChupaScript.Context: allocation failed")
        }
        handle = h
        registerRedrawCallback()
    }

    deinit {
        chupa_context_destroy(handle)
    }

    // MARK: - Set variables

    /// Set global from a ChupaScript literal text (not JSON).
    /// Examples: "42", "true", "'hello'", "{'name': 'John', 'age': 30}"
    ///
    /// Object keys must be quoted: a bare identifier is parsed as a name and
    /// rejected as unknown.
    @discardableResult
    public func set(_ name: String, text: String) -> Bool {
        name.withCString { namePtr in
            text.withCString { textPtr in
                chupa_context_set(handle, namePtr, name.utf8.count,
                                  textPtr, text.utf8.count)
            }
        }
    }

    /// Set global to a boolean value.
    public func set(_ name: String, _ value: Bool) {
        name.withCString { ptr in
            chupa_context_set_bool(handle, ptr, name.utf8.count, value)
        }
    }

    /// Set global to a number value.
    public func set(_ name: String, _ value: Double) {
        name.withCString { ptr in
            chupa_context_set_number(handle, ptr, name.utf8.count, value)
        }
    }

    /// Set global to a string value (raw, no quoting needed).
    public func set(_ name: String, _ value: String) {
        name.withCString { namePtr in
            value.withCString { valuePtr in
                chupa_context_set_string(handle, namePtr, name.utf8.count,
                                         valuePtr, value.utf8.count)
            }
        }
    }

    // MARK: - Compile

    /// Compile a ChupaScript expression promising a result of type `T`.
    ///
    /// `T` обычно выводится из места, куда выражение кладут:
    ///
    ///     struct ButtonProps {
    ///         var title: CSExpression<String>
    ///     }
    ///     props.title = try context.compile(expression: "user.name")
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
        guard chupa_context_error_code(handle) != CHUPA_ERR_NONE else { return nil }
        return makeError()
    }

    internal func makeError() -> Error {
        let code = ErrorCode(chupa_context_error_code(handle))
        let offset = chupa_context_error_offset(handle)
        var len: Int = 0
        let msgPtr = chupa_context_error(handle, &len)
        let message: String
        if let msgPtr, len > 0 {
            message = String(decoding: UnsafeRawBufferPointer(start: msgPtr, count: len),
                             as: UTF8.self)
        } else {
            message = ""
        }
        return Error(code: code, message: message, offset: offset)
    }

    // MARK: - Redraw trampoline

    /// UAF-2 (backlog B38) — not fixed here, and this comment must not read as
    /// if it were. The engine neither retains `user_data` nor unregisters the
    /// listener in `chupa_context_destroy`; unregistering is the host's job
    /// (`chupa_context_on_redraw(ctx, nil, nil)`), and this wrapper does not do
    /// it anywhere, including `deinit`. `passUnretained` is the right half of
    /// the pair — `passRetained` would make the context own itself and `deinit`
    /// would never run — but the other half, the guaranteed unregister, is
    /// missing.
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
