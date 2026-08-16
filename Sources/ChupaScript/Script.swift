import Foundation
import ChupaScriptC

/// Compiled ChupaScript script. Used only via `Context.run(_:)`.
///
/// Owns its C handle and frees it in `deinit`; the context does not.
///
/// Контекст живёт не меньше скрипта: ссылка сильная, поэтому через Swift он
/// раньше не умрёт. C API разрешает и обратный порядок — хэндл скрипта контекст
/// не удерживает, — но обвязка его не предоставляет по той же причине, что и у
/// `Expression`: запускать скрипт без живого контекста всё равно нельзя.
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
