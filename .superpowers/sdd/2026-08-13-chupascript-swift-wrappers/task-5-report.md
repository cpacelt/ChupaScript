# Task 5 Report: Swift Wrappers

## Status: DONE_WITH_CONCERNS

## Commit

- `3da74a6` — feat: add Swift wrappers for ChupaScript C API

## Summary

Created all 5 Swift wrapper files in `swift/` exactly as specified in the task brief:
- `swift/ChupaError.swift` — Error struct with CustomStringConvertible
- `swift/ChupaContextDelegate.swift` — AnyObject protocol with contextNeedsRedraw(_:)
- `swift/ChupaContext.swift` — Final class owning C handle, deinit calls chupa_context_destroy, redraw trampoline via @convention(c)
- `swift/ChupaExpression.swift` — Final class with strong context ref, evalNumber/evalBool/evalString returning optionals
- `swift/ChupaScript.swift` — Final class with strong context ref, no deinit

Total: 212 lines across 5 files.

## Self-Review

Cross-checked all Swift calls against C header signatures in `core/include/chupascript/chupascript.h`:

- `chupa_context_create()` -> `OpaquePointer?` guarded with `guard let h` — correct
- `chupa_context_set` returns `bool` — Swift returns directly from closure — correct
- `chupa_eval_string` double-nullable `const char ** out` — Swift uses `UnsafePointer<CChar>?` with `&ptr` — correct
- `chupa_context_error` nullable return + nullable len param — Swift handles with `if let msgPtr, len > 0` — correct
- Trampoline `@convention(c) (OpaquePointer?, UnsafeMutableRawPointer?) -> Void` matches `ChupaRedrawListener` — correct
- `ChupaExpression`/`ChupaScript` hold strong context ref, no deinit — correct per constraints
- `ChupaContext` deinit calls `chupa_context_destroy` — correct
- `ChupaError.description` = `"\(code) at \(offset): \(message)"` — exact match to spec
- `ChupaContextDelegate` is `AnyObject` protocol with `contextNeedsRedraw(_:)` — correct
- Redraw trampoline uses `Unmanaged.passUnretained(self)` — correct per constraints

## Concerns

1. **ChupaError.swift missing `import ChupaScriptC`**: The file references `ChupaErrorCode` (a C enum from the `ChupaScriptC` module) but only imports `Foundation`. This was written verbatim per the brief. The type may not be visible without the import, depending on how Task 6 configures the modulemap. If compilation fails in Xcode, adding `import ChupaScriptC` to `ChupaError.swift` should fix it. This is a minor issue that can be resolved in Task 6 or during Xcode integration.

## No Build/Test

Per task instructions, no build or test step was performed — Swift wrappers compile only in Xcode/CocoaPods context.

## Fix Log

### Fix 1 (2026-08-14): Add missing `import ChupaScriptC` to ChupaError.swift

**Issue:** `swift/ChupaError.swift` declared `public let code: ChupaErrorCode` but only imported `Foundation`. `ChupaErrorCode` is a C enum imported via the `ChupaScriptC` module, so the file would not compile without that import.

**Change:** Added `import ChupaScriptC` on the line after `import Foundation` in `swift/ChupaError.swift`.

**No test:** Swift files do not build in the CMake-based CI; verification deferred to Xcode integration (Task 6).
