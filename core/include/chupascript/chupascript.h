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

/* ╔══════════════════════════════════════════════════════════════════════╗
 * ║ text MUST be valid UTF-8. This is the host's obligation.             ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * The engine itself does not care: to it a string is a sequence of bytes
 * (docs/semantics.md 2.1), it only ever concatenates them, and the one place
 * that splits — the format template — splits on the ASCII markers '$', '{',
 * '}', so it cannot cut a scalar in half. Whatever goes in comes out
 * unchanged.
 *
 * The obligation exists because of what is on the other side. The Swift
 * wrapper builds its String from these bytes WITHOUT re-validating them
 * (Sources/ChupaScript/UTF8.swift): that validation was three quarters of the
 * cost of reading a long string. Feeding invalid UTF-8 in through this
 * function is therefore not "garbled text on screen" — it is undefined
 * behaviour in the host process.
 *
 * A host that reaches this API from Swift, Kotlin or any other language whose
 * strings are already valid UTF-8 satisfies the obligation for free. A host
 * that assembles bytes itself must check them.
 *
 * The same obligation covers the source text handed to chupa_compile_expression
 * and chupa_compile_script: its string literals become string values by the
 * same route.
 */

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
 * On CHUPA_OK, *bytes points at the string's bytes inside the engine's own
 * text pool and *len is their count. Nothing is allocated and nothing has to
 * be released. On CHUPA_NULL and CHUPA_ERROR neither output is touched.
 *
 * ╔══════════════════════════════════════════════════════════════════════╗
 * ║ The bytes are BORROWED. They stay valid until the next call that     ║
 * ║ touches this context, and not one moment longer. Copy them first.    ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * Any call that writes into the pool — chupa_context_set*, compiling a unit
 * with string literals in it, another evaluation that builds a string — may
 * move the pool and leave the pointer dangling. Destroying the context frees
 * the pool outright.
 *
 * A computed string — anything format() or str() built — lives in the
 * temporary region, which the next evaluation or chupa_run frees WHOLE before
 * it starts. That is not a pool move that might leave the bytes readable by
 * luck: the region is gone. The window has always been "until the next call";
 * this is what now closes it.
 *
 * This used to hand ownership over instead, through a heap-allocated
 * ChupaString the caller destroyed. It bought exactly one guarantee — free
 * destruction order — and no caller ever wanted it: every one of them copies
 * the bytes into its own string immediately, so the owning string existed
 * only to be duplicated and thrown away. Two allocations, a copy and two
 * frees per string read, for a promise nobody collected.
 *
 * The bytes are NOT NUL-terminated: they are a slice of a packed pool, and
 * what follows them is the next string. Use len.
 *
 * An empty result is CHUPA_OK with *len == 0, and *bytes may be NULL — an
 * empty string has nothing to point at. It is still a string, not a null.
 *
 * The _borrowed suffix is in the name and not only in this comment because
 * const char ** and size_t * say nothing about lifetime, so the one thing a
 * caller can get catastrophically wrong is the one thing the signature keeps
 * quiet about. Its C++ counterpart, Expression::evalString, needs no such
 * suffix: it hands back a std::string_view, and non-ownership is what that
 * type means. */
CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_string_borrowed(ChupaContext *ctx, ChupaExpression *e,
                           const char *CHUPA_NULLABLE *CHUPA_NONNULL bytes,
                           size_t *len);

/* ─── Values: aggregates crossing the boundary ───────────────────────────────
 *
 * Everything above returns a scalar by copy. This section returns a value of
 * ANY kind — including an array or an object — and lets the host walk it.
 *
 * ChupaValue is a plain 16-byte struct passed BY VALUE. It is not a handle
 * into a table and it is not an allocation: it is the engine's own value
 * representation, handed over unchanged. Producing one costs nothing.
 *
 * ╔══════════════════════════════════════════════════════════════════════╗
 * ║ None of the walking functions take a ChupaContext *. That is not a   ║
 * ║ saved parameter, it is the whole point: an aggregate carries its own ║
 * ║ storage and its own key names, so it can be read when the context    ║
 * ║ that produced it is already destroyed.                               ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 *
 * LIFETIME. A value returned by chupa_eval_value is BORROWED, exactly like
 * the bytes from chupa_eval_string_borrowed: the only reference to it is the
 * engine's deferred-release list, and the next operation on the context drops
 * that reference. To keep it longer — across frames, into a host object —
 * call chupa_value_retain, and chupa_value_release when done. Retain/release
 * must be balanced; both are no-ops on scalars, which reference nothing.
 *
 * Nested values obtained from chupa_array_at, chupa_object_value_at and
 * chupa_object_get are borrowed from their parent and take no reference of
 * their own: they live as long as the aggregate holding them. Retain one only
 * if it must outlive its parent.
 *
 * Key and string bytes are borrowed from the value that yielded them and stay
 * valid as long as that value does — which, for a retained value, is as long
 * as the host keeps it. They are NOT NUL-terminated. */

typedef struct ChupaValue { uint64_t _bits[2]; } ChupaValue;

/* Evaluates to a value of any kind.
 *
 * A computed string — anything format() or str() built — is materialised here
 * rather than left in the temporary region, because a region offset cannot be
 * retained and chupa_value_retain would otherwise be a silent lie. That costs
 * one allocation; the cheap path for strings is still
 * chupa_eval_string_borrowed, which allocates nothing.
 *
 * On CHUPA_NULL and CHUPA_ERROR *out is not touched. */
CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_value(ChupaContext *ctx, ChupaExpression *e, ChupaValue *out);

CHUPA_API ChupaKind chupa_value_kind(ChupaValue v);

/* Precondition: chupa_value_kind(v) == CHUPA_KIND_BOOL / _NUMBER. */
CHUPA_API bool   chupa_value_bool  (ChupaValue v);
CHUPA_API double chupa_value_number(ChupaValue v);

/* Precondition: chupa_value_kind(v) == CHUPA_KIND_STRING.
 * An empty string yields *len == 0 and *bytes possibly NULL. */
CHUPA_API void chupa_value_string_borrowed(ChupaValue v,
                                           const char *CHUPA_NULLABLE *CHUPA_NONNULL bytes,
                                           size_t *len);

/* Precondition: chupa_value_kind(v) == CHUPA_KIND_ARRAY.
 * chupa_array_at yields a null value past the end. */
CHUPA_API size_t     chupa_array_count(ChupaValue v);
CHUPA_API ChupaValue chupa_array_at   (ChupaValue v, size_t i);

/* Precondition: chupa_value_kind(v) == CHUPA_KIND_OBJECT.
 *
 * Enumeration order is not promised (docs/semantics.md 2.1). chupa_object_get
 * yields a null value when the key is absent — which does not distinguish an
 * absent key from one holding null; that distinction has no C API yet.
 * chupa_object_key_at yields *len == 0 past the end. */
CHUPA_API size_t     chupa_object_count   (ChupaValue v);
CHUPA_API void       chupa_object_key_at  (ChupaValue v, size_t i,
                                           const char *CHUPA_NULLABLE *CHUPA_NONNULL bytes,
                                           size_t *len);
CHUPA_API ChupaValue chupa_object_value_at(ChupaValue v, size_t i);
CHUPA_API ChupaValue chupa_object_get     (ChupaValue v,
                                           const char *key, size_t key_len);

/* Reference counting. Both are no-ops on scalars and on values that reference
 * nothing counted. Releasing more than was retained corrupts the engine's
 * bookkeeping; debug builds trap on it. */
CHUPA_API void chupa_value_retain (ChupaValue v);
CHUPA_API void chupa_value_release(ChupaValue v);

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
