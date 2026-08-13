# ChupaScript Swift Wrappers Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Implement the C API boundary and Swift wrappers for ChupaScript, replacing the placeholder header with a full API that OKBDUI can consume via CocoaPods.

**Architecture:** C++ engine (`core/`) stays unchanged. A new `c_api.cpp` wraps the engine's C++ types (`CS::Context`, `CS::Ast`, `CS::Value`) into C-compatible opaque structs. Swift wrappers in `swift/` wrap the C opaque pointers into safe Swift classes with delegate-based redraw notification. A podspec compiles everything from source.

**Tech Stack:** C99, C++17, Swift 5, CocoaPods, GoogleTest (C++ tests), CMake

## Global Constraints

- C header is C99-compatible, compiles as both C and C++
- No C++ types leak through the header — only opaque structs, enums, primitives
- `ChupaContext` is the sole owning entity; `ChupaExpression*` and `ChupaScript*` are non-owning handles valid until context destruction
- Source text is copied into context storage (Ast stores string_views into the copy)
- One context = one thread at a time; different contexts are independent
- Spec: `docs/superpowers/specs/2026-08-13-chupascript-swift-wrappers-design.md`
- C API spec: `docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md`
- Engine headers in `core/src/`, public C header in `core/include/chupascript/`

---

### Task 1: Update C header and CMake

**Files:**
- Modify: `core/include/chupascript/chupascript.h`
- Modify: `core/CMakeLists.txt`

**Interfaces:**
- Produces: full C API declarations (all function signatures, enums, opaque types) that Task 2–5 implement and Task 6 wraps in Swift

- [ ] **Step 1: Replace placeholder header with full API**

Replace entire contents of `core/include/chupascript/chupascript.h` with the header from spec §4.2:

```c
#ifndef CHUPASCRIPT_H
#define CHUPASCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#if defined(__clang__)
#  define CHUPA_NONNULL_BEGIN _Pragma("clang assume_nonnull begin")
#  define CHUPA_NONNULL_END   _Pragma("clang assume_nonnull end")
#  define CHUPA_NULLABLE      _Nullable
#else
#  define CHUPA_NONNULL_BEGIN
#  define CHUPA_NONNULL_END
#  define CHUPA_NULLABLE
#endif

#define CHUPA_API __attribute__((visibility("default")))

#if defined(__GNUC__) || defined(__clang__)
#  define CHUPA_MUST_USE __attribute__((warn_unused_result))
#else
#  define CHUPA_MUST_USE
#endif

#ifdef __cplusplus
extern "C" {
#endif

CHUPA_NONNULL_BEGIN

typedef struct ChupaContext    ChupaContext;
typedef struct ChupaExpression ChupaExpression;
typedef struct ChupaScript     ChupaScript;

typedef enum ChupaKind {
    CHUPA_KIND_NULL   = 0,
    CHUPA_KIND_BOOL   = 1,
    CHUPA_KIND_NUMBER = 2,
    CHUPA_KIND_STRING = 3,
    CHUPA_KIND_ARRAY  = 4,
    CHUPA_KIND_OBJECT = 5
} ChupaKind;

typedef enum ChupaStatus {
    CHUPA_OK    = 0,
    CHUPA_NULL  = 1,
    CHUPA_ERROR = 2
} ChupaStatus;

typedef enum ChupaErrorCode {
    CHUPA_ERR_NONE = 0,
    CHUPA_ERR_SYNTAX,
    CHUPA_ERR_NAME,
    CHUPA_ERR_TYPE,
    CHUPA_ERR_RANGE,
    CHUPA_ERR_DATA,
    CHUPA_ERR_USAGE,
    CHUPA_ERR_MEMORY
} ChupaErrorCode;

CHUPA_API const char *chupa_version(void);

CHUPA_API ChupaContext *CHUPA_NULLABLE chupa_context_create(void);
CHUPA_API void chupa_context_destroy(ChupaContext *CHUPA_NULLABLE ctx);

CHUPA_API bool chupa_context_set(ChupaContext *ctx,
                                 const char *name, size_t name_len,
                                 const char *text, size_t text_len);

CHUPA_API void chupa_context_set_bool  (ChupaContext *ctx,
                                        const char *name, size_t name_len,
                                        bool value);
CHUPA_API void chupa_context_set_number(ChupaContext *ctx,
                                        const char *name, size_t name_len,
                                        double value);
CHUPA_API void chupa_context_set_string(ChupaContext *ctx,
                                        const char *name, size_t name_len,
                                        const char *text, size_t text_len);

typedef void (*ChupaRedrawListener)(ChupaContext *ctx,
                                    void *CHUPA_NULLABLE user_data);

CHUPA_API void chupa_context_on_redraw(ChupaContext *ctx,
                                       ChupaRedrawListener listener,
                                       void *CHUPA_NULLABLE user_data);

CHUPA_API ChupaExpression *CHUPA_NULLABLE
chupa_compile_expression(ChupaContext *ctx, const char *source, size_t len);

CHUPA_API ChupaScript *CHUPA_NULLABLE
chupa_compile_script(ChupaContext *ctx, const char *source, size_t len);

CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_number(ChupaContext *ctx, ChupaExpression *e, double *out);

CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_bool(ChupaContext *ctx, ChupaExpression *e, bool *out);

CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_string(ChupaContext *ctx, ChupaExpression *e,
                  const char *CHUPA_NULLABLE *CHUPA_NULLABLE out, size_t *len);

CHUPA_API CHUPA_MUST_USE bool chupa_run(ChupaContext *ctx, ChupaScript *script);

CHUPA_API ChupaErrorCode chupa_context_error_code  (const ChupaContext *ctx);
CHUPA_API size_t         chupa_context_error_offset(const ChupaContext *ctx);
CHUPA_API const char *CHUPA_NULLABLE
chupa_context_error(const ChupaContext *ctx, size_t *CHUPA_NULLABLE len);

CHUPA_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* CHUPASCRIPT_H */
```

- [ ] **Step 2: Add c_api.cpp to CMake**

Modify `core/CMakeLists.txt` — add `src/c_api.cpp` to the source list:

```cmake
add_library(chupascript STATIC
    src/ast.cpp
    src/builtin.cpp
    src/c_api.cpp
    src/check.cpp
    src/compile.cpp
    src/context.cpp
    src/data.cpp
    src/eval.cpp
    src/lexer.cpp
    src/operator.cpp
    src/parser.cpp
    src/text.cpp
    src/version.cpp
)
```

- [ ] **Step 3: Create stub c_api.cpp so build doesn't break**

Create `core/src/c_api.cpp` with stub implementations that link but return failures:

```cpp
#include "chupascript/chupascript.h"
```

This file will be filled in Task 2–5. For now, empty — the header is included so the compiler sees the declarations, and `version.cpp` already provides `chupascript_version`.

- [ ] **Step 4: Build and run existing tests**

Run:
```bash
cd /Users/roman.putincev/ChupaScript && cmake -B build && cmake --build build && cd build && ctest --output-on-failure
```

Expected: All existing tests pass. Smoke test still verifies version string.

- [ ] **Step 5: Commit**

```bash
cd /Users/roman.putincev/ChupaScript
git add core/include/chupascript/chupascript.h core/CMakeLists.txt core/src/c_api.cpp
git commit -m "feat: replace placeholder C header with full API declarations

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 2: Context lifecycle and set functions

**Files:**
- Modify: `core/src/c_api.cpp`
- Create: `core/tests/c_api_test.cpp`
- Modify: `core/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `CS::Context` (`core/src/context.hpp`), `CS::setVariable` (`core/src/data.hpp`), `CS::Value` (`core/src/value.hpp`), `CS::Diagnostic` (`core/src/diagnostic.hpp`)
- Produces: `chupa_context_create`, `chupa_context_destroy`, `chupa_context_set`, `chupa_context_set_bool`, `chupa_context_set_number`, `chupa_context_set_string`

**Key implementation details:**

`ChupaContext` (C++ struct defined in c_api.cpp, not in header):
- Contains `CS::Context engine` — the engine's storage
- Contains `std::vector<std::string> sources` — copied source texts for Asts
- Contains `std::vector<std::unique_ptr<CS::Ast>> asts` — compiled expression/script trees
- Contains `CS::Diagnostic lastError` — last error state
- Contains `ChupaRedrawListener redrawListener` + `void* redrawUserData`
- Has `notifyRedraw()` method that calls the listener if set
- Has `setError(const CS::Diagnostic&)` method

`chupa_context_set` vs `chupa_context_set_string`:
- `chupa_context_set` — text is a ChupaScript **literal** (parsed): `"'hello'"`, `"42"`, `"{ name: 'John' }"`. Calls `CS::setVariable`.
- `chupa_context_set_string` — text is the **raw string value**: `"hello"` stored as string "hello". Creates `CS::Value` via `engine.makeString()` then `engine.setRoot()`.
- `chupa_context_set_bool` — `engine.setRoot(name, CS::Value::boolean(value))`
- `chupa_context_set_number` — `engine.setRoot(name, CS::Value::number(value))`

All `set*` functions call `notifyRedraw()` after mutation.

- [ ] **Step 1: Write failing tests for context lifecycle and set**

Create `core/tests/c_api_test.cpp`:

```cpp
#include <gtest/gtest.h>

#include "chupascript/chupascript.h"

#include <cstring>
#include <string>

// Helper: set a root from a ChupaScript literal text
bool setRoot(ChupaContext* ctx, const std::string& name, const std::string& text) {
    return chupa_context_set(ctx, name.c_str(), name.size(), text.c_str(), text.size());
}

TEST(CApiContext, CreateDestroy) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralString) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "name", "'hello'"));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralNumber) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "count", "42"));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralObject) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "user", "{ name: 'John', age: 30 }"));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralFailsOnExpression) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // "1 + 2" is an expression, not a literal — should fail
    EXPECT_FALSE(setRoot(ctx, "x", "1 + 2"));
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_DATA);
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetBool) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_bool(ctx, "flag", 4, true);
    // No return value to check — verify via eval in Task 3
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetNumber) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_number(ctx, "pi", 2, 3.14);
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetString) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_string(ctx, "greeting", 8, "world", 5);
    chupa_context_destroy(ctx);
}

TEST(CApiContext, DestroyNullIsSafe) {
    chupa_context_destroy(nullptr);
}

TEST(CApiContext, VersionIsReported) {
    EXPECT_STREQ("0.1.0", chupa_version());
}
```

- [ ] **Step 2: Add test to CMake**

Modify `core/tests/CMakeLists.txt` — add `c_api_test.cpp` to the test sources. Read the current file first to match its pattern:

```cmake
# Add c_api_test.cpp alongside existing test files
```

- [ ] **Step 3: Run tests to verify they fail**

Run:
```bash
cd /Users/roman.putincev/ChupaScript && cmake --build build && cd build && ctest -R CApiContext --output-on-failure
```

Expected: FAIL — `chupa_context_create` returns nullptr (stub).

- [ ] **Step 4: Implement context lifecycle and set functions**

Write `core/src/c_api.cpp`:

```cpp
#include "chupascript/chupascript.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "context.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "value.hpp"

// ─── Opaque struct definitions ───
// Defined here, not in the header: C doesn't see C++ members.

struct ChupaContext {
    CS::Context engine;
    std::vector<std::string> sources;                    // copied source texts
    std::vector<std::unique_ptr<CS::Ast>> asts;          // compiled trees
    CS::Diagnostic lastError;
    ChupaRedrawListener redrawListener = nullptr;
    void* redrawUserData = nullptr;

    void notifyRedraw() {
        if (redrawListener) {
            redrawListener(reinterpret_cast<::ChupaContext*>(this), redrawUserData);
        }
    }

    void setError(const CS::Diagnostic& diag) { lastError = diag; }
};

struct ChupaExpression {
    CS::Ast* ast = nullptr;
};

struct ChupaScript {
    CS::Ast* ast = nullptr;
};

// ─── Version ───

#define CHUPA_STR_(x) #x
#define CHUPA_STR(x) CHUPA_STR_(x)

const char* chupa_version(void) {
    return CHUPA_STR(CHUPASCRIPT_VERSION_MAJOR) "."
           CHUPA_STR(CHUPASCRIPT_VERSION_MINOR) "."
           CHUPA_STR(CHUPASCRIPT_VERSION_PATCH);
}

// ─── Context lifecycle ───

ChupaContext* chupa_context_create(void) {
    auto* ctx = new (std::nothrow) ChupaContext;
    return reinterpret_cast<ChupaContext*>(ctx);
}

void chupa_context_destroy(ChupaContext* ctx) {
    if (!ctx) { return; }
    delete reinterpret_cast<::ChupaContext*>(ctx);
}

// ─── Set: text literal ───

bool chupa_context_set(ChupaContext* ctx, const char* name, size_t name_len,
                       const char* text, size_t text_len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    CS::Diagnostic diag;
    bool ok = CS::setVariable(c->engine,
                              std::string_view(name, name_len),
                              std::string_view(text, text_len),
                              diag);
    if (!ok) {
        c->setError(diag);
        return false;
    }
    c->notifyRedraw();
    return true;
}

// ─── Set: scalars ───

void chupa_context_set_bool(ChupaContext* ctx, const char* name, size_t name_len,
                            bool value) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    c->engine.setRoot(std::string_view(name, name_len),
                      CS::Value::boolean(value));
    c->notifyRedraw();
}

void chupa_context_set_number(ChupaContext* ctx, const char* name, size_t name_len,
                              double value) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    c->engine.setRoot(std::string_view(name, name_len),
                      CS::Value::number(value));
    c->notifyRedraw();
}

void chupa_context_set_string(ChupaContext* ctx, const char* name, size_t name_len,
                              const char* text, size_t text_len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    CS::Value str = c->engine.makeString(std::string_view(text, text_len));
    c->engine.setRoot(std::string_view(name, name_len), str);
    c->notifyRedraw();
}
```

Note: `chupa_version` is defined here using the version macros from the header. The old `chupascript_version` in `version.cpp` should delegate to it. Replace `version.cpp` entirely with:

```cpp
#include "chupascript/chupascript.h"

const char* chupascript_version(void) {
    return chupa_version();
}
```

And in `c_api.cpp`, implement `chupa_version` using the macros (not a hardcoded string):

```cpp
#define CHUPA_STR_(x) #x
#define CHUPA_STR(x) CHUPA_STR_(x)

const char* chupa_version(void) {
    return CHUPA_STR(CHUPASCRIPT_VERSION_MAJOR) "."
           CHUPA_STR(CHUPASCRIPT_VERSION_MINOR) "."
           CHUPA_STR(CHUPASCRIPT_VERSION_PATCH);
}
```

- [ ] **Step 5: Run tests to verify they pass**

Run:
```bash
cd /Users/roman.putincev/ChupaScript && cmake --build build && cd build && ctest -R CApiContext --output-on-failure
```

Expected: PASS — all context tests pass.

- [ ] **Step 6: Commit**

```bash
cd /Users/roman.putincev/ChupaScript
git add core/src/c_api.cpp core/src/version.cpp core/tests/c_api_test.cpp core/tests/CMakeLists.txt
git commit -m "feat: implement context lifecycle and set functions in C API

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 3: Compile and eval functions

**Files:**
- Modify: `core/src/c_api.cpp`
- Modify: `core/tests/c_api_test.cpp`

**Interfaces:**
- Consumes: `CS::compileExpression` / `CS::compileScript` (`core/src/compile.hpp`), `CS::evalExpression` (`core/src/eval.hpp`), `CS::Ast` (`core/src/ast.hpp`)
- Produces: `chupa_compile_expression`, `chupa_compile_script`, `chupa_eval_number`, `chupa_eval_bool`, `chupa_eval_string`

**Key implementation details:**

`chupa_compile_expression`:
1. Copy source text into `ctx->sources` (Ast stores string_views into this copy)
2. Create `CS::Ast`, call `reset(src_copy)`
3. Call `CS::compileExpression(src, len, ast, ctx->engine, &diag, 1)`
4. On failure: store error, return nullptr
5. On success: store Ast in `ctx->asts`, create `ChupaExpression` wrapping `ast.get()`, return pointer

`chupa_eval_number/bool/string`:
1. Get `CS::Ast*` from `ChupaExpression`
2. Call `CS::evalExpression(ast, ctx->engine, &value, diag)`
3. On failure: store error, return `CHUPA_ERROR`
4. Check value kind — if null, return `CHUPA_NULL`
5. If matching type, extract and return `CHUPA_OK`
6. If wrong type, return `CHUPA_ERROR`

`chupa_eval_string` returns pointer+length into context's string pool. Valid until next eval on this context (spec §7 rule).

- [ ] **Step 1: Write failing tests for compile and eval**

Add to `core/tests/c_api_test.cpp`:

```cpp
// ─── Compile + Eval ───

TEST(CApiCompile, CompileExpression) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "42"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    EXPECT_NE(e, nullptr);
    chupa_context_destroy(ctx);
}

TEST(CApiCompile, CompileExpressionFailsOnSyntaxError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ChupaExpression* e = chupa_compile_expression(ctx, "1 +", 3);
    EXPECT_EQ(e, nullptr);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_SYNTAX);
    chupa_context_destroy(ctx);
}

TEST(CApiCompile, CompileExpressionFailsOnUnknownRoot) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ChupaExpression* e = chupa_compile_expression(ctx, "unknown_var", 11);
    EXPECT_EQ(e, nullptr);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_NAME);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNumber) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "42"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 42.0);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNumberFromExpression) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "10"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x + 5", 5);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 15.0);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalBool) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "10"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x > 5", 5);
    ASSERT_NE(e, nullptr);
    bool out = false;
    EXPECT_EQ(chupa_eval_bool(ctx, e, &out), CHUPA_OK);
    EXPECT_TRUE(out);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalString) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "name", "'hello'"));
    ChupaExpression* e = chupa_compile_expression(ctx, "name", 4);
    ASSERT_NE(e, nullptr);
    const char* out = nullptr;
    size_t len = 0;
    EXPECT_EQ(chupa_eval_string(ctx, e, &out, &len), CHUPA_OK);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(std::string(out, len), "hello");
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNullReturnsChupaNull) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "null"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_NULL);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNumberOnStringExpressionReturnsError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "name", "'hello'"));
    ChupaExpression* e = chupa_compile_expression(ctx, "name", 4);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_ERROR);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalMemberAccess) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "user", "{ name: 'John', age: 30 }"));
    ChupaExpression* e = chupa_compile_expression(ctx, "user.age", 8);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 30.0);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalTernary) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "5"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x > 3 ? 'big' : 'small'", 22);
    ASSERT_NE(e, nullptr);
    const char* out = nullptr;
    size_t len = 0;
    EXPECT_EQ(chupa_eval_string(ctx, e, &out, &len), CHUPA_OK);
    EXPECT_EQ(std::string(out, len), "big");
    chupa_context_destroy(ctx);
}

TEST(CApiEval, SetBoolThenEval) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_bool(ctx, "flag", 4, true);
    ChupaExpression* e = chupa_compile_expression(ctx, "flag", 4);
    ASSERT_NE(e, nullptr);
    bool out = false;
    EXPECT_EQ(chupa_eval_bool(ctx, e, &out), CHUPA_OK);
    EXPECT_TRUE(out);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, SetNumberThenEval) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_number(ctx, "pi", 2, 3.14);
    ChupaExpression* e = chupa_compile_expression(ctx, "pi", 2);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 3.14);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, SetStringThenEval) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_string(ctx, "greeting", 8, "world", 5);
    ChupaExpression* e = chupa_compile_expression(ctx, "greeting", 8);
    ASSERT_NE(e, nullptr);
    const char* out = nullptr;
    size_t len = 0;
    EXPECT_EQ(chupa_eval_string(ctx, e, &out, &len), CHUPA_OK);
    EXPECT_EQ(std::string(out, len), "world");
    chupa_context_destroy(ctx);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:
```bash
cd /Users/roman.putincev/ChupaScript && cmake --build build && cd build && ctest -R "CApiCompile|CApiEval" --output-on-failure
```

Expected: FAIL — functions not implemented.

- [ ] **Step 3: Implement compile and eval functions**

Add to `core/src/c_api.cpp` (after existing implementations). Include `compile.hpp`, `eval.hpp`, `ast.hpp` at top:

```cpp
#include "ast.hpp"
#include "compile.hpp"
#include "eval.hpp"
```

Add implementations:

```cpp
// ─── Compile ───

ChupaExpression* chupa_compile_expression(ChupaContext* ctx,
                                          const char* source, size_t len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    // Copy source — Ast stores string_views into this buffer
    c->sources.emplace_back(source, len);
    const char* src = c->sources.back().c_str();

    auto ast = std::make_unique<CS::Ast>();
    ast->reset(src);

    CS::Diagnostic diag;
    const std::uint32_t errors = CS::compileExpression(
        src, static_cast<std::uint32_t>(len), *ast, c->engine, &diag, 1);

    if (errors != 0) {
        c->setError(diag);
        return nullptr;
    }

    ChupaExpression* expr = new ChupaExpression{ast.get()};
    c->asts.push_back(std::move(ast));
    return expr;
}

ChupaScript* chupa_compile_script(ChupaContext* ctx,
                                  const char* source, size_t len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    c->sources.emplace_back(source, len);
    const char* src = c->sources.back().c_str();

    auto ast = std::make_unique<CS::Ast>();
    ast->reset(src);

    CS::Diagnostic diag;
    const std::uint32_t errors = CS::compileScript(
        src, static_cast<std::uint32_t>(len), *ast, c->engine, &diag, 1);

    if (errors != 0) {
        c->setError(diag);
        return nullptr;
    }

    ChupaScript* script = new ChupaScript{ast.get()};
    c->asts.push_back(std::move(ast));
    return script;
}

// ─── Eval ───

ChupaStatus chupa_eval_number(ChupaContext* ctx, ChupaExpression* e,
                              double* out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);

    CS::Value value = CS::Value::null();
    CS::Diagnostic diag;
    if (!CS::evalExpression(*expr->ast, c->engine, &value, diag)) {
        c->setError(diag);
        return CHUPA_ERROR;
    }
    if (value.kind() == CS::Value::Kind::Null) {
        return CHUPA_NULL;
    }
    if (value.kind() != CS::Value::Kind::Number) {
        return CHUPA_ERROR;
    }
    *out = value.numberValue();
    return CHUPA_OK;
}

ChupaStatus chupa_eval_bool(ChupaContext* ctx, ChupaExpression* e,
                            bool* out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);

    CS::Value value = CS::Value::null();
    CS::Diagnostic diag;
    if (!CS::evalExpression(*expr->ast, c->engine, &value, diag)) {
        c->setError(diag);
        return CHUPA_ERROR;
    }
    if (value.kind() == CS::Value::Kind::Null) {
        return CHUPA_NULL;
    }
    if (value.kind() != CS::Value::Kind::Boolean) {
        return CHUPA_ERROR;
    }
    *out = value.booleanValue();
    return CHUPA_OK;
}

ChupaStatus chupa_eval_string(ChupaContext* ctx, ChupaExpression* e,
                              const char** out, size_t* len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);

    CS::Value value = CS::Value::null();
    CS::Diagnostic diag;
    if (!CS::evalExpression(*expr->ast, c->engine, &value, diag)) {
        c->setError(diag);
        return CHUPA_ERROR;
    }
    if (value.kind() == CS::Value::Kind::Null) {
        return CHUPA_NULL;
    }
    if (value.kind() != CS::Value::Kind::String) {
        return CHUPA_ERROR;
    }
    std::string_view sv = c->engine.string(value);
    *out = sv.data();
    *len = sv.size();
    return CHUPA_OK;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run:
```bash
cd /Users/roman.putincev/ChupaScript && cmake --build build && cd build && ctest -R "CApiCompile|CApiEval" --output-on-failure
```

Expected: PASS — all compile and eval tests pass.

- [ ] **Step 5: Commit**

```bash
cd /Users/roman.putincev/ChupaScript
git add core/src/c_api.cpp core/tests/c_api_test.cpp
git commit -m "feat: implement compile and eval functions in C API

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 4: Run, error, and redraw functions

**Files:**
- Modify: `core/src/c_api.cpp`
- Modify: `core/tests/c_api_test.cpp`

**Interfaces:**
- Consumes: `CS::runScript` (`core/src/eval.hpp`)
- Produces: `chupa_run`, `chupa_context_error_code`, `chupa_context_error_offset`, `chupa_context_error`, `chupa_context_on_redraw`

**Key implementation details:**

`chupa_run`:
1. Get `CS::Ast*` from `ChupaScript`
2. Call `CS::runScript(ast, ctx->engine, diag)`
3. On failure: store error, return false
4. On success: call `notifyRedraw()`, return true

Error accessors read `ctx->lastError`. The message pointer is valid until the next call on the context (matches spec §8).

`chupa_context_on_redraw` stores the listener + user_data. `notifyRedraw()` calls it. Currently fires after every `chupa_run` and every `chupa_context_set*` call. B29 (batch/dirty tracking) will refine this — for now, simple is correct.

- [ ] **Step 1: Write failing tests for run, error, and redraw**

Add to `core/tests/c_api_test.cpp`:

```cpp
// ─── Run ───

TEST(CApiRun, RunScriptSetsVariable) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "0"));
    ChupaScript* s = chupa_compile_script(ctx, "x = 42", 6);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(chupa_run(ctx, s));
    // Verify via eval
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 42.0);
    chupa_context_destroy(ctx);
}

TEST(CApiRun, RunScriptWithMemberAccess) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "user", "{ name: 'old' }"));
    ChupaScript* s = chupa_compile_script(ctx, "user.name = 'new'", 18);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(chupa_run(ctx, s));
    ChupaExpression* e = chupa_compile_expression(ctx, "user.name", 10);
    ASSERT_NE(e, nullptr);
    const char* out = nullptr;
    size_t len = 0;
    EXPECT_EQ(chupa_eval_string(ctx, e, &out, &len), CHUPA_OK);
    EXPECT_EQ(std::string(out, len), "new");
    chupa_context_destroy(ctx);
}

TEST(CApiRun, RunScriptFailsOnTypeError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "0"));
    // x is a number, can't assign a string member to it
    ChupaScript* s = chupa_compile_script(ctx, "x.name = 'bad'", 14);
    ASSERT_NE(s, nullptr);
    EXPECT_FALSE(chupa_run(ctx, s));
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_TYPE);
    chupa_context_destroy(ctx);
}

// ─── Error accessors ───

TEST(CApiError, NoErrorAfterSuccess) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "42"));
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_NONE);
    chupa_context_destroy(ctx);
}

TEST(CApiError, SyntaxErrorDetails) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_compile_expression(ctx, "1 +", 3);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_SYNTAX);
    size_t len = 0;
    const char* msg = chupa_context_error(ctx, &len);
    ASSERT_NE(msg, nullptr);
    EXPECT_GT(len, 0u);
    // Offset should point somewhere in the source
    EXPECT_GE(chupa_context_error_offset(ctx), 0u);
    chupa_context_destroy(ctx);
}

TEST(CApiError, DataErrorOnExpressionAsData) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // "1 + 2" is an expression, not a literal — setVariable rejects it
    chupa_context_set(ctx, "x", 1, "1 + 2", 5);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_DATA);
    chupa_context_destroy(ctx);
}

// ─── Redraw ───

namespace {
int g_redrawCount = 0;
ChupaContext* g_lastRedrawCtx = nullptr;

void testRedrawListener(ChupaContext* ctx, void* /*user_data*/) {
    g_redrawCount++;
    g_lastRedrawCtx = ctx;
}
}

TEST(CApiRedraw, FiresAfterSet) {
    g_redrawCount = 0;
    g_lastRedrawCtx = nullptr;
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_on_redraw(ctx, testRedrawListener, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "42"));
    EXPECT_EQ(g_redrawCount, 1);
    EXPECT_EQ(g_lastRedrawCtx, ctx);
    chupa_context_destroy(ctx);
}

TEST(CApiRedraw, FiresAfterRun) {
    g_redrawCount = 0;
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "0"));
    // set already fired redraw; reset counter
    g_redrawCount = 0;
    chupa_context_on_redraw(ctx, testRedrawListener, nullptr);
    ChupaScript* s = chupa_compile_script(ctx, "x = 1", 5);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(chupa_run(ctx, s));
    EXPECT_EQ(g_redrawCount, 1);
    chupa_context_destroy(ctx);
}

TEST(CApiRedraw, NoFireWithoutListener) {
    g_redrawCount = 0;
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "42"));
    EXPECT_EQ(g_redrawCount, 0);
    chupa_context_destroy(ctx);
}

TEST(CApiRedraw, UserDataPassedThrough) {
    g_redrawCount = 0;
    int marker = 42;
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_on_redraw(ctx, [](ChupaContext* ctx, void* user_data) {
        g_redrawCount++;
        EXPECT_EQ(*static_cast<int*>(user_data), 42);
    }, &marker);
    chupa_context_set_bool(ctx, "flag", 4, true);
    EXPECT_EQ(g_redrawCount, 1);
    chupa_context_destroy(ctx);
}
```

- [ ] **Step 2: Run tests to verify they fail**

Run:
```bash
cd /Users/roman.putincev/ChupaScript && cmake --build build && cd build && ctest -R "CApiRun|CApiError|CApiRedraw" --output-on-failure
```

Expected: FAIL — `chupa_run` and error accessors not implemented.

- [ ] **Step 3: Implement run, error, and redraw functions**

Add to `core/src/c_api.cpp`:

```cpp
// ─── Run ───

bool chupa_run(ChupaContext* ctx, ChupaScript* script) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* s = reinterpret_cast<::ChupaScript*>(script);

    CS::Diagnostic diag;
    if (!CS::runScript(*s->ast, c->engine, diag)) {
        c->setError(diag);
        return false;
    }
    c->notifyRedraw();
    return true;
}

// ─── Redraw ───

void chupa_context_on_redraw(ChupaContext* ctx, ChupaRedrawListener listener,
                             void* user_data) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    c->redrawListener = listener;
    c->redrawUserData = user_data;
}

// ─── Error accessors ───

ChupaErrorCode chupa_context_error_code(const ChupaContext* ctx) {
    const auto* c = reinterpret_cast<const ::ChupaContext*>(ctx);
    // Map CS::ErrorCode to ChupaErrorCode.
    // Values are intentionally identical (diagnostic.hpp comment), but
    // map explicitly to guard against drift.
    switch (c->lastError.code) {
        case CS::ErrorCode::None:    return CHUPA_ERR_NONE;
        case CS::ErrorCode::Syntax:  return CHUPA_ERR_SYNTAX;
        case CS::ErrorCode::Name:    return CHUPA_ERR_NAME;
        case CS::ErrorCode::Type:    return CHUPA_ERR_TYPE;
        case CS::ErrorCode::Range:   return CHUPA_ERR_RANGE;
        case CS::ErrorCode::Data:    return CHUPA_ERR_DATA;
        case CS::ErrorCode::Usage:   return CHUPA_ERR_USAGE;
        case CS::ErrorCode::Memory:  return CHUPA_ERR_MEMORY;
    }
    return CHUPA_ERR_NONE;
}

size_t chupa_context_error_offset(const ChupaContext* ctx) {
    const auto* c = reinterpret_cast<const ::ChupaContext*>(ctx);
    return c->lastError.offset;
}

const char* chupa_context_error(const ChupaContext* ctx, size_t* len) {
    const auto* c = reinterpret_cast<const ::ChupaContext*>(ctx);
    if (len) { *len = std::strlen(c->lastError.message); }
    return c->lastError.message;
}
```

- [ ] **Step 4: Run tests to verify they pass**

Run:
```bash
cd /Users/roman.putincev/ChupaScript && cmake --build build && cd build && ctest --output-on-failure
```

Expected: PASS — all C API tests pass, all existing engine tests still pass.

- [ ] **Step 5: Commit**

```bash
cd /Users/roman.putincev/ChupaScript
git add core/src/c_api.cpp core/tests/c_api_test.cpp
git commit -m "feat: implement run, error, and redraw functions in C API

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 5: Swift wrappers

**Files:**
- Create: `swift/ChupaContext.swift`
- Create: `swift/ChupaExpression.swift`
- Create: `swift/ChupaScript.swift`
- Create: `swift/ChupaError.swift`
- Create: `swift/ChupaContextDelegate.swift`

**Interfaces:**
- Consumes: C API functions from `chupascript.h` (imported as `OpaquePointer` and C functions)
- Produces: `ChupaContext` (final class), `ChupaExpression` (final class), `ChupaScript` (final class), `ChupaError` (struct), `ChupaContextDelegate` (protocol)

**Key design decisions:**

- Swift imports opaque C types as `OpaquePointer` — wrapper classes hold `OpaquePointer` and manage lifetime
- `ChupaExpression` and `ChupaScript` hold a strong reference to `ChupaContext` to prevent dangling pointers (no retain cycle — context doesn't hold expressions)
- Compile methods `throw ChupaError` on failure
- Eval methods return optionals — `nil` for null or error
- `onRedraw` implemented via delegate protocol + `@convention(c)` trampoline
- `Unmanaged.passUnretained(self)` in trampoline is safe — context owns the C handle, `chupa_context_destroy` in `deinit` prevents callback after death

**Note on testing:** Swift wrappers cannot be tested in isolation within the CMake/GoogleTest build. They compile as part of the pod when integrated into an Xcode project. Verification happens when OKBDUI integrates ChupaScript (separate task). The C API tests in Tasks 2–4 verify the underlying behavior.

- [ ] **Step 1: Create ChupaError.swift**

Create `swift/ChupaError.swift`:

```swift
import Foundation

/// Error from ChupaScript engine.
public struct ChupaError: Error, CustomStringConvertible {
    public let code: ChupaErrorCode
    public let message: String
    public let offset: Int

    public var description: String {
        "ChupaError(\(code)): \(message) at offset \(offset)"
    }
}
```

- [ ] **Step 2: Create ChupaContextDelegate.swift**

Create `swift/ChupaContextDelegate.swift`:

```swift
import Foundation

/// Delegate protocol for redraw notifications.
/// Called when the engine has finished mutations and the host can recalculate.
public protocol ChupaContextDelegate: AnyObject {
    func contextNeedsRedraw(_ context: ChupaContext)
}
```

- [ ] **Step 3: Create ChupaContext.swift**

Create `swift/ChupaContext.swift`:

```swift
import Foundation
import ChupaScriptC

/// Owns a ChupaScript engine context.
/// All expressions, scripts, and values live until this context is deallocated.
public final class ChupaContext {

    internal let handle: OpaquePointer

    /// Weak delegate for redraw notifications.
    public weak var delegate: ChupaContextDelegate?

    public init() {
        guard let h = chupa_context_create() else {
            fatalError("ChupaContext: allocation failed")
        }
        handle = h
        registerRedrawCallback()
    }

    deinit {
        chupa_context_destroy(handle)
    }

    // MARK: - Set variables

    /// Set root from a ChupaScript literal text (not JSON).
    /// Examples: "42", "true", "'hello'", "{ name: 'John', age: 30 }"
    @discardableResult
    public func set(_ name: String, text: String) -> Bool {
        name.withCString { namePtr in
            text.withCString { textPtr in
                chupa_context_set(handle, namePtr, name.utf8.count,
                                  textPtr, text.utf8.count)
            }
        }
    }

    /// Set root to a boolean value.
    public func set(_ name: String, _ value: Bool) {
        name.withCString { ptr in
            chupa_context_set_bool(handle, ptr, name.utf8.count, value)
        }
    }

    /// Set root to a number value.
    public func set(_ name: String, _ value: Double) {
        name.withCString { ptr in
            chupa_context_set_number(handle, ptr, name.utf8.count, value)
        }
    }

    /// Set root to a string value (raw, no quoting needed).
    public func set(_ name: String, _ value: String) {
        name.withCString { namePtr in
            value.withCString { valuePtr in
                chupa_context_set_string(handle, namePtr, name.utf8.count,
                                         valuePtr, value.utf8.count)
            }
        }
    }

    // MARK: - Compile

    /// Compile a ChupaScript expression.
    /// Throws on compile error (syntax, unknown root, etc.).
    public func compile(expression source: String) throws -> ChupaExpression {
        let h = source.withCString { ptr in
            chupa_compile_expression(handle, ptr, source.utf8.count)
        }
        guard let h else { throw makeError() }
        return ChupaExpression(handle: h, context: self)
    }

    /// Compile a ChupaScript script (statements).
    /// Throws on compile error.
    public func compile(script source: String) throws -> ChupaScript {
        let h = source.withCString { ptr in
            chupa_compile_script(handle, ptr, source.utf8.count)
        }
        guard let h else { throw makeError() }
        return ChupaScript(handle: h, context: self)
    }

    // MARK: - Run

    /// Execute a compiled script. Returns false on runtime error.
    /// Partial changes may have been applied (see C API spec §7).
    @discardableResult
    public func run(_ script: ChupaScript) -> Bool {
        chupa_run(handle, script.handle)
    }

    // MARK: - Error

    /// Last error on this context, or nil if last operation succeeded.
    public var error: ChupaError? {
        let code = chupa_context_error_code(handle)
        guard code != CHUPA_ERR_NONE else { return nil }
        return makeError()
    }

    private func makeError() -> ChupaError {
        let code = chupa_context_error_code(handle)
        let offset = chupa_context_error_offset(handle)
        var len: Int = 0
        let msgPtr = chupa_context_error(handle, &len)
        let message: String
        if let msgPtr, len > 0 {
            message = String(bytes: UnsafeBufferPointer(start: msgPtr, count: len),
                             encoding: .utf8) ?? ""
        } else {
            message = ""
        }
        return ChupaError(code: code, message: message, offset: offset)
    }

    // MARK: - Redraw trampoline

    private func registerRedrawCallback() {
        let ptr = Unmanaged.passUnretained(self).toOpaque()
        chupa_context_on_redraw(handle, ChupaContext.trampoline, ptr)
    }

    private static let trampoline: @convention(c) (
        OpaquePointer?, UnsafeMutableRawPointer?
    ) -> Void = { _, userData in
        guard let userData else { return }
        let ctx = Unmanaged<ChupaContext>.fromOpaque(userData).takeUnretainedValue()
        ctx.delegate?.contextNeedsRedraw(ctx)
    }
}
```

- [ ] **Step 4: Create ChupaExpression.swift**

Create `swift/ChupaExpression.swift`:

```swift
import Foundation
import ChupaScriptC

/// Compiled ChupaScript expression. Holds a strong reference to its context.
public final class ChupaExpression {

    internal let handle: OpaquePointer
    internal let context: ChupaContext

    init(handle: OpaquePointer, context: ChupaContext) {
        self.handle = handle
        self.context = context
    }

    /// Evaluate as a number. Returns nil if expression is null or wrong type.
    public func evalNumber() -> Double? {
        var out: Double = 0
        let status = chupa_eval_number(context.handle, handle, &out)
        return status == CHUPA_OK ? out : nil
    }

    /// Evaluate as a boolean. Returns nil if expression is null or wrong type.
    public func evalBool() -> Bool? {
        var out: Bool = false
        let status = chupa_eval_bool(context.handle, handle, &out)
        return status == CHUPA_OK ? out : nil
    }

    /// Evaluate as a string. Returns nil if expression is null or wrong type.
    /// The returned String is a copy — safe to retain beyond the next eval.
    public func evalString() -> String? {
        var ptr: UnsafePointer<CChar>?
        var len: Int = 0
        let status = chupa_eval_string(context.handle, handle, &ptr, &len)
        guard status == CHUPA_OK, let ptr, len > 0 else { return nil }
        return String(bytes: UnsafeBufferPointer(start: ptr, count: len),
                       encoding: .utf8)
    }
}
```

- [ ] **Step 5: Create ChupaScript.swift**

Create `swift/ChupaScript.swift`:

```swift
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
```

- [ ] **Step 6: Commit**

```bash
cd /Users/roman.putincev/ChupaScript
mkdir -p swift
git add swift/
git commit -m "feat: add Swift wrappers for ChupaScript C API

ChupaContext, ChupaExpression, ChupaScript, ChupaError,
ChupaContextDelegate.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 6: Podspec

**Files:**
- Create: `ChupaScript.podspec`

**Interfaces:**
- Produces: a CocoaPods podspec that compiles C++ sources + C header + Swift wrappers from source

**Key design decisions:**

- Pod name: `ChupaScript`
- Module name for C header: `ChupaScriptC` (Swift imports `import ChupaScriptC`)
- Swift wrappers use the pod module name `ChupaScript` (Swift files don't need `import ChupaScript` — they're in the same module)
- `s.source_files` includes C++ sources, C header, and Swift files
- `s.public_header_files` = only `chupascript.h`
- `s.pod_target_xcconfig` sets `-fvisibility=hidden` (matches spec §6) and C++ language standard
- No xcframework, no vendored binaries

**Module naming concern:** The Swift files use `import ChupaScriptC` for the C header. In CocoaPods, when a pod has both C headers and Swift files, the C header module is typically named `<PodName>`. To avoid conflict, we use a subspec or modulemap approach:

- Main module `ChupaScript` contains Swift files
- Submodule `ChupaScript.ChupaScriptC` or separate module `ChupaScriptC` for the C header

The simplest approach with CocoaPods: use a modulemap that defines `ChupaScriptC` as a module wrapping the C header. The podspec uses `s.module_map` to point to a custom modulemap.

- [ ] **Step 1: Create modulemap**

Create `ChupaScript.modulemap`:

```
module ChupaScriptC {
    umbrella header "chupascript.h"
    export *
    module * { export * }
}

module ChupaScript {
    export *
    module * { export * }
}
```

- [ ] **Step 2: Create podspec**

Create `ChupaScript.podspec`:

```ruby
Pod::Spec.new do |s|
  s.name             = 'ChupaScript'
  s.version          = '0.1.0'
  s.summary          = 'ChupaScript expression engine for backend-driven UI'
  s.description      = <<-DESC
A compact expression language engine with C API, designed for backend-driven UI
systems. Replaces verbose JSON expression DSLs with a clean, composable syntax.
                       DESC
  s.homepage         = 'https://github.com/nickolaus-od/ChupaScript'
  s.license          = { :type => 'Proprietary' }
  s.author           = { 'Roman Putintsev' => 'roman.putincev@ok.ru' }
  s.source           = { :git => 'https://github.com/nickolaus-od/ChupaScript.git', :tag => s.version.to_s }

  s.ios.deployment_target = '15.6'
  s.swift_version = '5.0'

  # C header — compiled as module ChupaScriptC
  s.public_header_files = 'core/include/chupascript/chupascript.h'

  # C++ engine sources
  s.source_files = [
    'core/src/*.{cpp,hpp}',
    'core/include/chupascript/*.h',
    'swift/*.swift'
  ]

  # Preserve the header directory structure
  s.preserve_paths = 'core/include/**/*'

  # C++17, hidden visibility for non-API symbols
  s.pod_target_xcconfig = {
    'CLANG_CXX_LANGUAGE_STANDARD' => 'c++17',
    'GCC_C_LANGUAGE_STANDARD' => 'c99',
    'CLANG_CXX_LIBRARY' => 'libc++',
    'OTHER_CFLAGS' => '-fvisibility=hidden',
    'OTHER_CPLUSPLUSFLAGS' => '-fvisibility=hidden',
    'DEFINES_MODULE' => 'YES',
    'SWIFT_OBJC_INTERFACE_HEADER_NAME' => 'ChupaScript-Swift.h'
  }

  # Custom modulemap: ChupaScriptC wraps the C header, ChupaScript wraps Swift
  s.module_map = 'ChupaScript.modulemap'
end
```

- [ ] **Step 3: Verify podspec syntax**

Run:
```bash
cd /Users/roman.putincev/ChupaScript && pod spec lint ChupaScript.podspec --quick 2>&1 | head -30
```

Expected: No syntax errors. May warn about missing repo/source — that's fine, we're checking syntax only.

- [ ] **Step 4: Commit**

```bash
cd /Users/roman.putincev/ChupaScript
git add ChupaScript.podspec ChupaScript.modulemap
git commit -m "feat: add CocoaPods podspec and modulemap

Compiles C++ engine, C header, and Swift wrappers from source.
No xcframework, no vendored binaries.

Co-Authored-By: Claude <noreply@anthropic.com>"
```

---

### Task 7: Final verification and cleanup

**Files:**
- Modify: `core/src/version.cpp` (verify delegation)
- Verify: all tests pass, all files in place

- [ ] **Step 1: Run full test suite**

Run:
```bash
cd /Users/roman.putincev/ChupaScript && cmake -B build && cmake --build build && cd build && ctest --output-on-failure
```

Expected: All tests pass — existing engine tests + new C API tests.

- [ ] **Step 2: Verify file structure**

Run:
```bash
cd /Users/roman.putincev/ChupaScript && find . -name '*.swift' -o -name 'c_api*' -o -name '*.podspec' -o -name '*.modulemap' | sort
```

Expected:
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

- [ ] **Step 3: Update C API spec document**

Update `docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md` §7 to match the implemented API:
- Remove `chupa_context_set_value` and `ChupaValue` references
- Add `chupa_context_set_bool/number/string`
- Add `chupa_context_on_redraw`
- Add note: `ChupaValue` removed — scalars set via typed functions, complex data via text literal

- [ ] **Step 4: Commit**

```bash
cd /Users/roman.putincev/ChupaScript
git add docs/superpowers/specs/2026-08-10-chupascript-c-api-design.md
git commit -m "docs: update C API spec to match implementation

Remove ChupaValue, add typed scalar setters, add on_redraw.

Co-Authored-By: Claude <noreply@anthropic.com>"
```
