import Foundation

/// Delegate protocol for redraw notifications.
/// Called when the engine has finished mutations and the host can recalculate.
public protocol ContextDelegate: AnyObject {
    func contextNeedsRedraw(_ context: Context)
}
