import Foundation
import ChupaScriptC

/// Compiled ChupaScript script. Holds a strong reference to its context.
/// Used only via ChupaContext.run(_:).
public final class ChupaScript {

    internal let handle: OpaquePointer
    internal let context: ChupaContext

    init(handle: OpaquePointer, context: ChupaContext) {
        self.handle = handle
        self.context = context
    }
}
