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

/* ╔══════════════════════════════════════════════════════════════════════╗
 * ║ UAF-2 — ctx НЕ удерживает user_data и не знает, когда тот умер.      ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * chupa_context_destroy НЕ снимает слушателя и не зовёт его на прощание.
 * Снять обязан хост — chupa_context_on_redraw(ctx, NULL, NULL) — ДО того,
 * как умрёт объект, на который смотрит user_data.
 * Swift-обёртка этого сейчас не делает: см. swift/ChupaContext.swift, UAF-2.
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
 * compiled against — it holds no reference to it. */
CHUPA_API void chupa_expression_destroy(ChupaExpression *CHUPA_NULLABLE e);
CHUPA_API void chupa_script_destroy(ChupaScript *CHUPA_NULLABLE s);

CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_number(ChupaContext *ctx, ChupaExpression *e, double *out);

CHUPA_API CHUPA_MUST_USE ChupaStatus
chupa_eval_bool(ChupaContext *ctx, ChupaExpression *e, bool *out);

/* ╔══════════════════════════════════════════════════════════════════════╗
 * ║ UAF-1 — out получает указатель ВНУТРЬ хранилища контекста.           ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * Окно валидности здесь НЕ ОПИСАНО, и это сама по себе дыра в контракте.
 * Фактически указатель мёртв после ЛЮБОЙ следующей операции над ctx —
 * chupa_context_set*, chupa_run или даже другого chupa_eval_string,
 * потому что вычисление строки само пишет в тот же пул.
 * Пользоваться можно только до следующего вызова: копируй сразу.
 * Реализация и план починки: core/src/c_api.cpp, метка UAF-1.
 */
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
