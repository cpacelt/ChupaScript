import Foundation
import ChupaScriptC

/// Compiled ChupaScript expression.
///
/// Owns its C handle and frees it in `deinit`; the context does not.
/// The reference to the context is kept because evaluation needs it, not to
/// keep the context alive for the handle's sake — the handle holds no
/// reference to the context and may outlive it.
public final class Expression {

    internal let handle: OpaquePointer
    internal let context: Context

    init(handle: OpaquePointer, context: Context) {
        self.handle = handle
        self.context = context
    }

    deinit {
        chupa_expression_destroy(handle)
    }

    /// Evaluate as a number. Returns nil if expression is null or wrong type.
    public func evalNumber() -> Double? {
        var out: Double = 0
        let status = chupa_eval_number(context.handle, handle, &out)
        return status == CHUPA_OK ? out : nil
    }

    /// Evaluate as a boolean. Returns nil if expression is null or wrong type.
    public func evalBool() -> Bool? {
        var out: Bool = false
        let status = chupa_eval_bool(context.handle, handle, &out)
        return status == CHUPA_OK ? out : nil
    }

    /// Evaluate as a string. Returns nil if the expression evaluated to null.
    /// Throws on evaluation error, including a wrong result type.
    ///
    /// The engine hands the bytes over in a `ChupaString` this call owns and
    /// must release; the returned `String` is a copy of them.
    public func evalString() throws -> String? {
        var raw: OpaquePointer?
        switch chupa_eval_string(context.handle, handle, &raw) {
        case CHUPA_OK:
            guard let raw else { return nil }
            defer { chupa_string_destroy(raw) }
            var len: Int = 0
            let bytes = chupa_string_bytes(raw, &len)
            return String(decoding: UnsafeRawBufferPointer(start: bytes, count: len),
                          as: UTF8.self)
        case CHUPA_NULL:
            return nil
        default:
            throw context.makeError()
        }
    }
}
