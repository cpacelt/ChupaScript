import Foundation
import ChupaScriptC

/// Compiled ChupaScript expression. Holds a strong reference to its context.
public final class ChupaExpression {

    internal let handle: OpaquePointer
    internal let context: ChupaContext

    init(handle: OpaquePointer, context: ChupaContext) {
        self.handle = handle
        self.context = context
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

    /// Evaluate as a string. Returns nil if expression is null or wrong type.
    /// The returned String is a copy — safe to retain beyond the next eval.
    public func evalString() -> String? {
        var ptr: UnsafePointer<CChar>?
        var len: Int = 0
        let status = chupa_eval_string(context.handle, handle, &ptr, &len)
        guard status == CHUPA_OK, let ptr, len > 0 else { return nil }
        return String(bytes: UnsafeBufferPointer(start: ptr, count: len),
                       encoding: .utf8)
    }
}
