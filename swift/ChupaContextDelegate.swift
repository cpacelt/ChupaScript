import Foundation

/// Delegate protocol for redraw notifications.
/// Called when the engine has finished mutations and the host can recalculate.
public protocol ChupaContextDelegate: AnyObject {
    func contextNeedsRedraw(_ context: ChupaContext)
}
