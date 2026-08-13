# Task 7: Final verification and cleanup — Report

## Step 1: Full test suite

```
cmake -B build && cmake --build build && cd build && ctest --output-on-failure
```

Result: **536/536 tests passed** (0 failures), total time 3.15s.

Build configured and compiled cleanly with no warnings or errors.

## Step 2: File structure verification

```
./ChupaScript.modulemap
./ChupaScript.podspec
./core/src/c_api.cpp
./core/tests/c_api_test.cpp
./swift/ChupaContext.swift
./swift/ChupaContextDelegate.swift
./swift/ChupaError.swift
./swift/ChupaExpression.swift
./swift/ChupaScript.swift
```

All 9 expected files present. No extra or missing files.

## Step 3: version.cpp delegation

The task brief referenced `core/src/version.cpp`. This file does not exist.
The `chupa_version()` function is implemented directly in `core/src/c_api.cpp`
(lines 57–66), delegating to the `CHUPASCRIPT_VERSION_MAJOR/MINOR/PATCH` macros
defined in the header. This is correct — the version is stringified from the
header macros, no separate `version.cpp` is needed.

## Step 4: C API spec update

Updated `docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md` to match
the implemented header (`core/include/chupascript/chupascript.h`):

1. **Header status line** — added revision note referencing the 2026-08-13
   swift-wrappers spec §4.1.
2. **§6 (Соглашения заголовка)** — removed `ChupaValue` from the typedef list;
   updated the `CHUPA_MUST_USE` example from `chupa_value_number` to
   `chupa_eval_number`; updated the Swift importer limitation note from
   `chupa_eval(ctx, script)` to `chupa_run(ctx, expr)`.
3. **§7 (Функции)** — added `chupa_context_set_bool/number/string` with
   docstrings; added `ChupaRedrawListener` typedef and
   `chupa_context_on_redraw`; added version rename note on `chupa_version`;
   removed `chupa_eval` (generic), `chupa_value_kind`, `chupa_value_bool`,
   `chupa_value_number`, `chupa_value_string`, `chupa_array_count`,
   `chupa_array_at`, `chupa_object_count`, `chupa_object_key_at`,
   `chupa_object_value_at`, `chupa_object_get`; updated `chupa_eval_string`
   signature to match header (`const char *CHUPA_NULLABLE *CHUPA_NULLABLE`).
4. **§7 ChupaValue removal note** — added a blockquote explaining what was
   removed and why: scalars via typed setters, complex data via text literal,
   array/object reading deferred.
5. **§7 Время жизни результата** — updated to reference only `chupa_eval_string`
   (was `chupa_eval_string` or `chupa_eval`).
6. **§9 (Принятые решения)** — replaced `ChupaValue`-related rows with rows for
   typed setters, `on_redraw`, and the removal of `ChupaValue`; updated the
   "два соглашения о провале" row from "четырёх функций вместо десяти" to
   "трёх функций".

## Step 5: Commit

```
1ff0cde docs: update C API spec to match implementation
```

## Step 6: Self-review

- All 536 tests pass.
- File structure matches the brief exactly.
- C API spec matches the implemented header verbatim (function signatures,
  typedefs, enum values).
- No stale `ChupaValue`, `chupa_value_*`, `chupa_array_*`, `chupa_object_*`,
  `chupa_context_set_value`, or `chupascript_version` references remain in the
  spec (except in the intentional removal/rename notes).
- `core/src/version.cpp` from the brief does not exist; version is implemented
  in `c_api.cpp` via header macros — this is correct and consistent.

No concerns.

---

## Fix: document future API functions in C API spec

**Issue:** The global constraint required keeping `chupa_eval`,
`chupa_value_kind`, `chupa_array_*`, and `chupa_object_*` as future
(not implemented) functions. The implementer removed all signatures
entirely, leaving only a blockquote note acknowledging deferral.

**Fix:** Added a "Future API" subsection at the end of §7 in
`docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md`
(before §8 "Ошибки"). The subsection:

1. Lists the planned functions by name (`chupa_eval`,
   `chupa_value_kind`, `chupa_array_count`, `chupa_array_at`,
   `chupa_object_count`, `chupa_object_key_at`,
   `chupa_object_value_at`, `chupa_object_get`) with brief
   descriptions — no full signatures, since `ChupaValue` type is
   removed.
2. States they are not yet implemented — deferred until array/object
   reading from eval is needed.
3. Notes that `ChupaValue` will be re-introduced or replaced when
   this layer lands.
4. References `docs/backlog.md`.

This preserves the signal for downstream wrapper authors that these
functions are planned, without referencing the removed `ChupaValue`
type.
