#ifndef CHUPASCRIPT_H
#define CHUPASCRIPT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Version macros — used by chupa_version() and available to hosts. */
#define CHUPASCRIPT_VERSION_MAJOR 0
#define CHUPASCRIPT_VERSION_MINOR 1
#define CHUPASCRIPT_VERSION_PATCH 0

#if defined(__clang__)
#  define CHUPA_NONNULL_BEGIN _Pragma("clang assume_nonnull begin")
#  define CHUPA_NONNULL_END   _Pragma("clang assume_nonnull end")
#  define CHUPA_NULLABLE      _Nullable
/* Explicit non-null marker. Inside a CHUPA_NONNULL_BEGIN region the outer
 * level of a multi-level pointer is NOT inferred once an inner level carries
 * an explicit specifier, so it has to be spelled out. */
#  define CHUPA_NONNULL       _Nonnull
#else
#  define CHUPA_NONNULL_BEGIN
#  define CHUPA_NONNULL_END
#  define CHUPA_NULLABLE
#  define CHUPA_NONNULL
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
typedef struct ChupaString     ChupaString;

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

CHUPA_API CHUPA_MUST_USE bool
chupa_context_set(ChupaContext *ctx,
                  const char *name, size_t name_len,
                  const char *text, size_t text_len);

/* Typed setters for scalars — no parsing, the value is passed as is.
 *
 * Every setter validates the name: it must be an identifier and not a reserved
 * word, otherwise no program could ever reference the global. On rejection the
 * setter returns false, sets the context error to CHUPA_ERR_NAME and writes
 * nothing — the store is left exactly as it was. */
CHUPA_API CHUPA_MUST_USE bool
chupa_context_set_bool  (ChupaContext *ctx,
                         const char *name, size_t name_len,
                         bool value);
CHUPA_API CHUPA_MUST_USE bool
chupa_context_set_number(ChupaContext *ctx,
                         const char *name, size_t name_len,
                         double value);
CHUPA_API CHUPA_MUST_USE bool
chupa_context_set_string(ChupaContext *ctx,
                         const char *name, size_t name_len,
                         const char *text, size_t text_len);

typedef void (*ChupaRedrawListener)(ChupaContext *ctx,
                                    void *CHUPA_NULLABLE user_data);

/* ╔══════════════════════════════════════════════════════════════════════╗
 * ║ UAF-2 — ctx does NOT retain user_data and cannot know when it dies.  ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * chupa_context_destroy does NOT clear the listener and does NOT call it a
 * final time. Clearing it is the host's duty — chupa_context_on_redraw(ctx,
 * NULL, NULL) — BEFORE the object that user_data points at is destroyed.
 * The Swift wrapper does not do this today: see swift/Context.swift, UAF-2.
 */
CHUPA_API void chupa_context_on_redraw(ChupaContext *ctx,
                                       ChupaRedrawListener listener,
                                       void *CHUPA_NULLABLE user_data);

CHUPA_API ChupaExpression *CHUPA_NULLABLE
chupa_compile_expression(ChupaContext *ctx, const char *source, size_t len);

CHUPA_API ChupaScript *CHUPA_NULLABLE
chupa_compile_script(ChupaContext *ctx, const char *source, size_t len);

/* Compiled units are owned by the caller, not by the context. Destroying the
 * context does not free them; destroying a unit does not touch the context.
 * A unit may be destroyed in any order relative to the context it was
 * compiled against — it holds no reference to it.
 *
 * A unit MUST be evaluated on the very context it was compiled against.
 * Passing it to another context is undefined behaviour: compilation resolves
 * every global name to a slot in that context's store, and another store's
 * slots address other variables. Debug builds trap on it; release builds do
 * not check. */
CHUPA_API void chupa_expression_destroy(ChupaExpression *CHUPA_NULLABLE e);
CHUPA_API void chupa_script_destroy(ChupaScript *CHUPA_NULLABLE s);

CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_number(ChupaContext *ctx, ChupaExpression *e, double *out);

CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_bool(ChupaContext *ctx, ChupaExpression *e, bool *out);

/* Evaluates the expression as a string.
 *
 * On CHUPA_OK, *out receives a ChupaString the CALLER now owns and must
 * release with chupa_string_destroy. On CHUPA_NULL and CHUPA_ERROR, *out is
 * left untouched and there is nothing to destroy.
 *
 * A ChupaString may be destroyed in any order relative to the context it was
 * evaluated against — it holds no reference to it. It owns its bytes outright,
 * so destroying the context neither frees it nor invalidates its bytes. */
CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_string(ChupaContext *ctx, ChupaExpression *e,
                  ChupaString *CHUPA_NULLABLE *CHUPA_NONNULL out);

/* The bytes are valid until chupa_string_destroy and not one moment longer.
 * They are not NUL-terminated by contract; pass len if you need the length.
 * Never freed by the caller — the ChupaString owns them. */
CHUPA_API const char *chupa_string_bytes(const ChupaString *s,
                                         size_t *CHUPA_NULLABLE len);

CHUPA_API void chupa_string_destroy(ChupaString *CHUPA_NULLABLE s);

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
