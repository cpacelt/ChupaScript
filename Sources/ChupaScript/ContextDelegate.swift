import Foundation

/// Delegate protocol for redraw notifications.
///
/// Called when an operation changed something — not when one merely finished.
/// A rejected setter does not call it, and neither does a script that wrote
/// nothing. One call per operation, whatever the number of writes; which names
/// moved is not reported.
public protocol ContextDelegate: AnyObject {
    func contextNeedsRedraw(_ context: Context)
}
