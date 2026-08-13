import Foundation

/// Error from ChupaScript engine.
public struct ChupaError: Error, CustomStringConvertible {
    public let code: ChupaErrorCode
    public let message: String
    public let offset: Int

    public var description: String {
        "\(code) at \(offset): \(message)"
    }
}
