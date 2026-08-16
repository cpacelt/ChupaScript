import Foundation

/// Error from the ChupaScript engine.
///
/// One type covers both compilation and evaluation: the engine reports both
/// through the same context slots (`chupa_context_error_code` and friends),
/// and `code` already tells the two apart. Naming it `Error` shadows the
/// standard-library protocol inside this module, so the protocol is spelled
/// `Swift.Error` here; outside the module both names stay unambiguous.
public struct Error: Swift.Error, CustomStringConvertible, Equatable {
    public let code: ErrorCode
    public let message: String

    /// Byte offset into the source the error points at.
    public let offset: Int

    public var description: String {
        "\(code) at \(offset): \(message)"
    }
}
