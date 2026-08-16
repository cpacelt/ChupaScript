import Foundation
import ChupaScriptC

/// Compiled ChupaScript script. Used only via `Context.run(_:)`.
///
/// Owns its C handle and frees it in `deinit`; the context does not.
/// The reference to the context is kept because running the script needs it,
/// not to keep the context alive for the handle's sake — the handle holds no
/// reference to the context and may outlive it.
public final class Script {

    internal let handle: OpaquePointer
    internal let context: Context

    init(handle: OpaquePointer, context: Context) {
        self.handle = handle
        self.context = context
    }

    deinit {
        chupa_script_destroy(handle)
    }
}
