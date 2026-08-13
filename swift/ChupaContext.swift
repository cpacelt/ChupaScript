import Foundation
import ChupaScriptC

/// Owns a ChupaScript engine context.
/// All expressions, scripts, and values live until this context is deallocated.
///
/// Thread safety: one context = one thread at a time. The context MUST be
/// deallocated on the same thread it was last used on — `deinit` calls
/// `chupa_context_destroy` which unregisters the redraw callback, and a
/// callback in flight on another thread during destruction is a race.
/// If the context was used on a background queue, capture it in a
/// `DispatchQueue.async` block on that queue and let it release there.
public final class ChupaContext {

    internal let handle: OpaquePointer

    /// Weak delegate for redraw notifications.
    public weak var delegate: ChupaContextDelegate?

    public init() {
        guard let h = chupa_context_create() else {
            fatalError("ChupaContext: allocation failed")
        }
        handle = h
        registerRedrawCallback()
    }

    deinit {
        chupa_context_destroy(handle)
    }

    // MARK: - Set variables

    /// Set root from a ChupaScript literal text (not JSON).
    /// Examples: "42", "true", "'hello'", "{ name: 'John', age: 30 }"
    @discardableResult
    public func set(_ name: String, text: String) -> Bool {
        name.withCString { namePtr in
            text.withCString { textPtr in
                chupa_context_set(handle, namePtr, name.utf8.count,
                                  textPtr, text.utf8.count)
            }
        }
    }

    /// Set root to a boolean value.
    public func set(_ name: String, _ value: Bool) {
        name.withCString { ptr in
            chupa_context_set_bool(handle, ptr, name.utf8.count, value)
        }
    }

    /// Set root to a number value.
    public func set(_ name: String, _ value: Double) {
        name.withCString { ptr in
            chupa_context_set_number(handle, ptr, name.utf8.count, value)
        }
    }

    /// Set root to a string value (raw, no quoting needed).
    public func set(_ name: String, _ value: String) {
        name.withCString { namePtr in
            value.withCString { valuePtr in
                chupa_context_set_string(handle, namePtr, name.utf8.count,
                                         valuePtr, value.utf8.count)
            }
        }
    }

    // MARK: - Compile

    /// Compile a ChupaScript expression.
    /// Throws on compile error (syntax, unknown root, etc.).
    public func compile(expression source: String) throws -> ChupaExpression {
        let h = source.withCString { ptr in
            chupa_compile_expression(handle, ptr, source.utf8.count)
        }
        guard let h else { throw makeError() }
        return ChupaExpression(handle: h, context: self)
    }

    /// Compile a ChupaScript script (statements).
    /// Throws on compile error.
    public func compile(script source: String) throws -> ChupaScript {
        let h = source.withCString { ptr in
            chupa_compile_script(handle, ptr, source.utf8.count)
        }
        guard let h else { throw makeError() }
        return ChupaScript(handle: h, context: self)
    }

    // MARK: - Run

    /// Execute a compiled script. Returns false on runtime error.
    /// Partial changes may have been applied (see C API spec §7).
    @discardableResult
    public func run(_ script: ChupaScript) -> Bool {
        chupa_run(handle, script.handle)
    }

    // MARK: - Error

    /// Last error on this context, or nil if last operation succeeded.
    public var error: ChupaError? {
        let code = chupa_context_error_code(handle)
        guard code != CHUPA_ERR_NONE else { return nil }
        return makeError()
    }

    private func makeError() -> ChupaError {
        let code = chupa_context_error_code(handle)
        let offset = chupa_context_error_offset(handle)
        var len: Int = 0
        let msgPtr = chupa_context_error(handle, &len)
        let message: String
        if let msgPtr, len > 0 {
            message = String(bytes: UnsafeBufferPointer(start: msgPtr, count: len),
                             encoding: .utf8) ?? ""
        } else {
            message = ""
        }
        return ChupaError(code: code, message: message, offset: offset)
    }

    // MARK: - Redraw trampoline

    private func registerRedrawCallback() {
        let ptr = Unmanaged.passUnretained(self).toOpaque()
        chupa_context_on_redraw(handle, ChupaContext.trampoline, ptr)
    }

    private static let trampoline: @convention(c) (
        OpaquePointer?, UnsafeMutableRawPointer?
    ) -> Void = { _, userData in
        guard let userData else { return }
        let ctx = Unmanaged<ChupaContext>.fromOpaque(userData).takeUnretainedValue()
        ctx.delegate?.contextNeedsRedraw(ctx)
    }
}
