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

/* ╔══════════════════════════════════════════════════════════════════════╗
 * ║ THREADING — a ChupaContext is the unit of single-threadedness.       ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * At most one thread may touch a given ChupaContext at a time, including
 * every value, expression and script that context produced. Two DIFFERENT
 * contexts may be used from two threads simultaneously; nothing inside the
 * engine is shared between them.
 *
 * Reference counts on values are not atomic, which is why the first rule is a
 * rule and not advice: two threads retaining the same value race, and the
 * value is freed while still in use. A host that hands a ChupaValue to
 * another thread must ensure the handoff is ordered and that only one thread
 * owns it at a time.
 */

typedef struct ChupaContext    ChupaContext;
typedef struct ChupaExpression ChupaExpression;
typedef struct ChupaScript     ChupaScript;

/* Defined here, ahead of the Evaluation section that documents it below: the
 * host-function types just above Evaluation take ChupaValue by pointer, and
 * a second typedef of the same name — one here as a forward declaration, one
 * there as the definition — is only valid from C11 on. This project builds
 * as C99 (see the iOS pod spec), where a repeated typedef of an identical
 * type is a compiler extension, not a language guarantee, and
 * -pedantic-errors turns the resulting warning into a hard failure. One
 * definition, moved up, has no such split. */
typedef struct ChupaValue { uint64_t _bits[2]; } ChupaValue;

/* ╔══════════════════════════════════════════════════════════════════════╗
 * ║ OWNERSHIP — three rules, and this header holds nothing else.         ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 *
 * 1. A VALUE IS BORROWED UNTIL YOU RETAIN IT.
 *    chupa_eval yields a value that stays alive until the next call on that
 *    context. To keep it longer, call chupa_value_retain, and
 *    chupa_value_release when done.
 *
 * 2. BYTES ARE BORROWED FROM THE VALUE WHOSE ADDRESS YOU PASSED.
 *    A string's bytes and an object's key bytes live exactly as long as YOUR
 *    ChupaValue variable does — the one whose address went into
 *    chupa_value_string. They are NOT NUL-terminated; use the length.
 *
 * 3. A NESTED VALUE IS BORROWED FROM ITS PARENT.
 *    chupa_array_at and its neighbours take no reference of their own. A
 *    nested value that must outlive the aggregate holding it needs an explicit
 *    chupa_value_retain.
 *
 * Every function over a value takes it BY ADDRESS — const ChupaValue * — with
 * no exceptions. A short string's bytes live inside the value itself, so a
 * by-copy parameter would hand back a pointer into a copy that dies when the
 * function returns. One way of passing removes that mistake from the set of
 * expressible ones rather than from the set of documented ones.
 */

typedef enum ChupaKind {
    CHUPA_KIND_NULL   = 0,
    CHUPA_KIND_BOOL   = 1,
    CHUPA_KIND_NUMBER = 2,
    CHUPA_KIND_STRING = 3,
    CHUPA_KIND_ARRAY  = 4,
    CHUPA_KIND_OBJECT = 5
} ChupaKind;

typedef enum ChupaErrorCode {
    CHUPA_ERR_NONE = 0,
    CHUPA_ERR_SYNTAX,
    CHUPA_ERR_NAME,
    CHUPA_ERR_TYPE,
    CHUPA_ERR_RANGE,
    CHUPA_ERR_DATA,
    CHUPA_ERR_USAGE,
    CHUPA_ERR_MEMORY,
    /* Appended LAST — the existing codes above must keep their numeric
     * values, because a host may already have them baked into a switch. A
     * host callback refused without calling chupa_fail; there is no reason
     * text to report, so this is the code that says so. */
    CHUPA_ERR_HOST
} ChupaErrorCode;

/* One call, one struct. Three separate accessors used to answer one question
 * in three round trips, and a caller who read the code but not the offset got
 * a half-answer. */
typedef struct ChupaError {
    ChupaErrorCode code;         /* CHUPA_ERR_NONE when the last call succeeded */
    size_t         offset;       /* byte offset into the compiled source        */
    const char    *message;      /* valid until the NEXT call on ctx — see the
                                   * note below, not a process-lifetime literal */
    size_t         message_len;
} ChupaError;

/* Reports the outcome of the last call made on ctx. code is CHUPA_ERR_NONE
 * when that call succeeded; offset and message are meaningful only when it
 * did not.
 *
 * message is valid until the NEXT call on this ctx — the same rule as every
 * borrowed value in this header (rule 1). It used to be documented as a
 * process-lifetime literal that outlived ctx itself; chupa_fail ended that,
 * because a host's reason for refusing is text the host assembles, and
 * demanding a literal would have left a Kotlin host with one static string
 * for every failure. Messages the engine itself produces are still literals;
 * that is no longer something a caller may rely on. */
CHUPA_API void chupa_context_error(const ChupaContext *ctx, ChupaError *out);

CHUPA_API const char *chupa_version(void);

CHUPA_API ChupaContext *CHUPA_NULLABLE chupa_context_create(void);
CHUPA_API void chupa_context_destroy(ChupaContext *CHUPA_NULLABLE ctx);

/* Accepts a literal's SOURCE TEXT, not a value — chupa_context_set_data
 * parses `text` as a literal expression and rejects anything else with
 * CHUPA_ERR_DATA. The old name `chupa_context_set` said nothing about that;
 * the setters below it take an already-parsed value and need no parsing at
 * all. */
CHUPA_API CHUPA_MUST_USE bool
chupa_context_set_data(ChupaContext *ctx,
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

/* Called when an operation CHANGED something, not when one merely finished.
 * A setter that was rejected does not fire it, and neither does a script that
 * wrote nothing — a gesture whose whole body is a host call that navigates
 * away touches no variable, and waking the host for it would repaint a screen
 * that did not move.
 *
 * One call per operation, not per write: a script that assigns four times
 * fires once. Which names moved is not reported — the host is told that
 * something did, and re-reads what it displays.
 */
typedef void (*ChupaRedrawListener)(ChupaContext *ctx,
                                    void *CHUPA_NULLABLE user_data);

/* ╔══════════════════════════════════════════════════════════════════════╗
 * ║ ctx does NOT retain user_data and cannot know when it dies.          ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * chupa_context_destroy does NOT clear the listener and does NOT call it a
 * final time. Clearing it is the host's duty — chupa_context_on_redraw(ctx,
 * NULL, NULL) — BEFORE the object that user_data points at is destroyed.
 *
 * Clear it before chupa_context_destroy, not after and not instead: destroy
 * refuses while an evaluation is on the stack, and that call is reachable —
 * the evaluation mark is already down when the redraw listener runs, so a
 * host that releases its last reference from inside the notification lands
 * in its own teardown mid-operation. On that path the context outlives the
 * host object, and only a clear that happened first keeps user_data honest.
 *
 * The Swift wrapper does this in Context.deinit.
 */
/* listener is nullable, and that is the whole unregister mechanism: passing
 * NULL is how a host takes the listener back. Marking it non-null (which the
 * assume_nonnull region does by default) makes the contract two lines above
 * unreachable from Swift, where a non-optional function pointer cannot be
 * given a nil. */
CHUPA_API void chupa_context_on_redraw(ChupaContext *ctx,
                                       ChupaRedrawListener CHUPA_NULLABLE listener,
                                       void *CHUPA_NULLABLE user_data);

CHUPA_API ChupaExpression *CHUPA_NULLABLE
chupa_compile_expression(ChupaContext *ctx, const char *source, size_t len);

CHUPA_API ChupaScript *CHUPA_NULLABLE
chupa_compile_script(ChupaContext *ctx, const char *source, size_t len);

/* Compiled units are owned by the caller, not by the context. Destroying the
 * context does not free them; destroying a unit does not touch the context.
 * Nothing stops a unit from being destroyed before the context it was
 * compiled against — it holds no reference to it. That is not permission to
 * destroy a unit while a value obtained from evaluating it is still in use
 * unretained: see the LIFETIME note near chupa_eval.
 *
 * A unit MUST be evaluated on the very context it was compiled against.
 * Every evaluation checks this — in release builds too — and a mismatch fails
 * with CHUPA_ERR_USAGE, touching no output. The check exists because
 * compilation resolves every global name to a slot in that context's store,
 * and another store's slots address other variables: without it the call would
 * return a neighbouring variable's value and look successful.
 *
 * Neither destroy takes a ChupaContext *, so neither can refuse while an
 * evaluation is in flight. Destroying a unit from inside a host callback is
 * the host's to avoid — see the block in ChupaHostFunction's docblock. */
CHUPA_API void chupa_expression_destroy(ChupaExpression *CHUPA_NULLABLE e);
CHUPA_API void chupa_script_destroy(ChupaScript *CHUPA_NULLABLE s);

/* ─── Host functions: types ──────────────────────────────────────────────
 *
 * chupa_register does the registering; only the shape of the descriptor
 * lives here, because the core includes this header, and a parallel copy of
 * these types inside the core would hold two truths about one thing. */

typedef enum ChupaFunctionFlags {
    CHUPA_FN_NONE          = 0,
    CHUPA_FN_RETURNS_VALUE = 1u << 0,  /* without it — Void (docs/semantics.md 2.2) */
    CHUPA_FN_EFFECT_FREE   = 1u << 1,  /* the call changes nothing observable outside
                                         * the engine, so the engine may skip it —
                                         * a short-circuited operand is never
                                         * evaluated — and may call it again on the
                                         * next layout pass. Without it — callable
                                         * from a script only, where a call happens
                                         * exactly once. */
    CHUPA_FN_CACHEABLE     = 1u << 2   /* the same arguments give the same answer, so
                                         * a repeat call may be answered from a cache
                                         * instead of being made. Requires
                                         * CHUPA_FN_EFFECT_FREE: answering from a
                                         * cache IS skipping the call. A function
                                         * reading mutable host state — a clock, a
                                         * feature flag — MUST NOT be declared
                                         * cacheable; the engine cannot check this,
                                         * so it is on the host, same as UTF-8 above.
                                         *
                                         * READ, and it decides caching: without this
                                         * flag every expression containing the call
                                         * is marked uncacheable at compile time, and
                                         * chupa_expression_eval_tracked answers
                                         * CHUPA_DEPS_OVERFLOW for it forever.
                                         *
                                         * "The same arguments" means the same box
                                         * IDENTITY, and identity does not change
                                         * when the box is mutated: the array handed
                                         * to total(items) is the same handle before
                                         * and after push(items, x). A cacheable
                                         * function that reads the CONTENTS of an
                                         * aggregate argument therefore is not
                                         * constant in its arguments in the naive
                                         * sense — and the engine covers this for it,
                                         * at any depth the function chooses to read,
                                         * with no rule for the host to remember:
                                         *
                                         *   - a FLAT aggregate bound as a call
                                         *     argument is recorded as a dependency
                                         *     of the expression, so mutating its
                                         *     contents moves the epoch and the
                                         *     reader misses. The price is a
                                         *     dependency slot: total(items) costs two
                                         *     of CHUPA_MAX_DEPS;
                                         *   - an aggregate that holds another
                                         *     aggregate at its top level disables
                                         *     caching for the whole expression, the
                                         *     same outcome as overflow. Epochs do not
                                         *     bubble up a tree — push(state.items, x)
                                         *     moves the array's epoch and nothing
                                         *     else — so no dependency set could stand
                                         *     for what the function reaches through a
                                         *     nested handle, and the engine declines
                                         *     to pretend otherwise.
                                         *
                                         * So read as deep as you like: the answer is
                                         * either watched or recomputed, never stale.
                                         * An expression past the ceiling likewise
                                         * stops being cached — recomputation, never a
                                         * stale answer. */
} ChupaFunctionFlags;

/* No upper bound on argument count — as format has. */
#define CHUPA_VARIADIC 255

/* args are borrowed and valid only for the duration of the call — rule 2 of
 * this header. Nothing outlives the call except what the host retained
 * through chupa_value_retain. args is never NULL, argc == 0 included: a
 * zero-argument call is handed the address of an empty value it must not
 * read, rather than a NULL the non-null region above would be lying about.
 *
 * out == NULL when the function was declared without CHUPA_FN_RETURNS_VALUE.
 *
 * ctx is closed: from inside the call only chupa_make_string, chupa_fail and
 * reading the error are allowed on it. Everything else fails with
 * CHUPA_ERR_USAGE.
 *
 * ╔══════════════════════════════════════════════════════════════════════╗
 * ║ DESTROYING A COMPILED UNIT FROM INSIDE THE CALL IS NOT REFUSED.      ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * chupa_expression_destroy and chupa_script_destroy have no guard and cannot
 * be given one: neither takes a ChupaContext *, so neither can ask whether an
 * evaluation is in flight. A callback that reaches a ChupaExpression * or
 * ChupaScript * — through user_data, say, in a host that caches compiled
 * props — and destroys the very unit being evaluated frees the tree the walk
 * is standing in the middle of, and the walk continues over freed memory.
 * Nothing detects this and nothing reports it.
 *
 * The obligation is therefore the host's, and it is absolute: from the moment
 * a callback is entered until it returns, no unit compiled on this ctx may be
 * destroyed. A host that caches units next to its callbacks must keep the
 * destruction on the other side of that boundary. See docs/backlog.md B67 for
 * why the engine cannot enforce it today.
 *
 * Returning false is a refusal; the callback may call chupa_fail before it.
 * The offset is filled in by the engine — the call node. */
typedef bool (*ChupaHostFunction)(ChupaContext *ctx,
                                  const ChupaValue *args, size_t argc,
                                  ChupaValue *CHUPA_NULLABLE out,
                                  void *CHUPA_NULLABLE user_data);

typedef struct ChupaFunction {
    const char *name;
    size_t      name_len;
    uint8_t     min_args;
    uint8_t     max_args;   /* CHUPA_VARIADIC — no upper bound */
    uint32_t    flags;      /* ChupaFunctionFlags */
    ChupaHostFunction call;
    void       *CHUPA_NULLABLE user_data;
    /* Called exactly once for every SUCCESSFULLY registered function, from
     * chupa_context_destroy. NULL — nothing to release. A refused
     * registration does not call release: the host still holds the box.
     *
     * Once PER REGISTRATION, not per user_data: the same descriptor
     * registered on two contexts is two registrations, and release runs twice
     * on the one user_data they share. A host that hands out one box to
     * several contexts must count references on it itself. */
    void      (*CHUPA_NULLABLE release)(void *CHUPA_NULLABLE user_data);
} ChupaFunction;

/* Registers fn on ctx.
 *
 * MUST be called BEFORE the first compilation on this context: check sees
 * the name set as it stands the moment it runs, and relies on that set not
 * changing afterwards. Refusing everything registered too late is how that
 * is kept true, not a separate rule enforced elsewhere.
 *
 * Refusal is false, with the reason in the context's error. */
CHUPA_API CHUPA_MUST_USE bool
chupa_register(ChupaContext *ctx, const ChupaFunction *fn);

/* Sets the reason for a refusal. Only meaningful from inside a host
 * callback; outside one, it does nothing and sets CHUPA_ERR_USAGE.
 *
 * code MUST NOT be CHUPA_ERR_NONE: "no error" is not a reason for refusing,
 * and the engine uses that same value to mean "the callback never called
 * chupa_fail". Passing it does nothing to the refusal and sets
 * CHUPA_ERR_USAGE, exactly as calling this outside a callback does.
 *
 * msg may be NULL, and then len MUST be 0 — there is no message to measure.
 * A NULL msg with a non-zero len is repaired to the empty message rather than
 * read: the length a host computed before its own string lookup failed
 * describes a string that does not exist.
 *
 * The message bytes are copied immediately, so the caller's own buffer is
 * not needed afterwards. The offset is supplied by the engine — the call
 * site's node — since the host has no way to know it. */
CHUPA_API void chupa_fail(ChupaContext *ctx, ChupaErrorCode code,
                          const char *CHUPA_NULLABLE msg, size_t len);

/* ─── Making values ────────────────────────────────────────────────────────
 *
 * A callback needs these to build its result. The first three allocate
 * nothing at all; a string of at most fifteen bytes is inlined into the
 * value the same way.
 *
 * All four are for a function declared with CHUPA_FN_RETURNS_VALUE. A Void
 * function is called with out == NULL, and passing that NULL on to one of
 * these is a host bug: the first three have no ctx to report it through and
 * trap in debug builds, doing nothing in release; chupa_make_string reports
 * CHUPA_ERR_USAGE and returns false. */

CHUPA_API void chupa_make_null  (ChupaValue *out);
CHUPA_API void chupa_make_bool  (ChupaValue *out, bool value);
CHUPA_API void chupa_make_number(ChupaValue *out, double value);

/* bytes MUST be valid UTF-8 — the same obligation as chupa_context_set_string
 * above. bytes may be NULL, and then len MUST be 0; a NULL with a non-zero
 * len is read as the empty string, not as len bytes.
 *
 * Refusal is false. The reason is in the context's error: CHUPA_ERR_MEMORY
 * when the allocation failed, CHUPA_ERR_USAGE when out was NULL. A NULL ctx
 * is the one refusal with nowhere to report it — there is no context to
 * report through.
 *
 * The value this produces lives until the next operation boundary on ctx,
 * exactly like any value the engine itself produced; keeping it longer needs
 * chupa_value_retain. */
CHUPA_API CHUPA_MUST_USE bool
chupa_make_string(ChupaContext *ctx, const char *CHUPA_NULLABLE bytes,
                  size_t len, ChupaValue *out);

/* ─── Кэш выражений: эпохи ───────────────────────────────────────────────
 *
 * Движок НЕ отдаёт кэшированное значение. Он отвечает на вопрос «менялось ли
 * то, от чего это выражение зависит»; значение, которое хост уже держит, хост
 * переиспользует сам. Причина в String: получить байты дёшево, а собрать из
 * них строку стоит ~45 нс — почти всё чтение целиком.
 *
 * Эпоха — номер на монотонной ленте контекста. Всякая мутация и всякое
 * рождение агрегата берут из неё следующий номер. Номер только растёт, и
 * увеличивает его только движок: состояние «я это видел» живёт у читателя,
 * поэтому читателей можно заводить и убивать когда угодно, а через границу не
 * идёт ни одной записи.
 *
 * Хост читает эпоху по адресу своей родной идиомой, без вызова:
 *
 *   iOS / Swift      UnsafePointer<UInt64>.pointee
 *   Android / JNI    NewDirectByteBuffer однажды, дальше getLong
 *   Web / WASM       адрес есть смещение в линейной памяти; вью поверх
 *                    memory.buffer
 *
 * «Адрес в памяти движка», а не «указатель C»: в wasm32 указатель и есть
 * 32-битное смещение, и один и тот же ABI годится всем троим. Рост памяти
 * WASM отцепляет вью — смещение переживает рост, вью нет; пересоздать вью
 * обязана обёртка. Порядок байтов движковый: для JNI это
 * ByteBuffer.order(nativeOrder()).
 *
 * Сумма эпох как снимок — приём читающей стороны, а не часть контракта.
 * Swift и JVM складывают, потому что 64-битная арифметика им даётся даром;
 * JS сравнивает половины через Uint32Array и ни одного BigInt не заводит.
 * Поэтому движок суммы не возвращает: он отдаёт эпохи. */

typedef uint64_t ChupaEpoch;

/* Сколько зависимостей движок записывает у одного выражения. Больше —
 * выражение не кэшируется вовсе.
 *
 * ╔══════════════════════════════════════════════════════════════════════╗
 * ║ ЭТО ABI. Обёртка, собранная при 4, и движок, пересобранный при 8,    ║
 * ║ разойдутся МОЛЧА: движок запишет больше, чем обёртка прочитает.      ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * Число фиксируется до первого релиза и дальше не двигается. Сверять его в
 * рантайме было бы вторым механизмом там, где хватает одного решения. */
#define CHUPA_MAX_DEPS 4

/* Кэшировать нельзя: мест по пути больше CHUPA_MAX_DEPS, либо в выражении
 * есть вызов, который на тех же входах вправе ответить иначе (см.
 * CHUPA_FN_CACHEABLE), либо вычисление отказало. */
#define CHUPA_DEPS_OVERFLOW 0xffffffffu

/* Одна зависимость выражения: что читать и за что держаться.
 *
 * epoch — адрес эпохи. Читается прямо, без вызова, на каждом кадре.
 * owner — то, внутри чего эта эпоха лежит. Держать его ОБЯЗАТЕЛЬНО:
 *         chupa_value_retain при захвате набора, chupa_value_release при
 *         следующем захвате. Без этого адрес указывает внутрь коробки,
 *         которую счётчик ссылок вправе освободить, и следующий кадр прочтёт
 *         освобождённую память.
 *         CHUPA_KIND_NULL — зависимость это ячейка глобальной переменной;
 *         её эпоха живёт столько же, сколько контекст, и держать нечего.
 *         retain и release на таком значении — no-op, так что цикл у хоста
 *         остаётся без ветвлений. */
typedef struct ChupaDep {
    const ChupaEpoch *epoch;
    ChupaValue        owner;
} ChupaDep;

/* Вычислить и заодно отдать зависимости.
 *
 * deps — буфер вызывающего ровно на CHUPA_MAX_DEPS записей. Заполняется
 *        ЦЕЛИКОМ и на всяком исходе — включая отказ двери (вызов изнутри
 *        колбэка), где буфер заполняется тем же, чем при переполнении.
 *        Незанятый хвост смотрит на вечный ноль движка: читатель складывает
 *        ровно CHUPA_MAX_DEPS слов, не заглядывая в *n и не ветвясь.
 * n    — сколько записано, либо CHUPA_DEPS_OVERFLOW. Ноль — законный ответ и
 *        НЕ то же самое, что переполнение: выражение из одних литералов не
 *        зависит ни от чего и кэшируется навсегда.
 *
 * ╔══════════════════════════════════════════════════════════════════════╗
 * ║ При CHUPA_DEPS_OVERFLOW каждый deps[i].epoch равен NULL.             ║
 * ╚══════════════════════════════════════════════════════════════════════╝
 * Намеренно: читатель, забывший посмотреть на *n, падает на первом прогоне,
 * а не показывает застывший экран через неделю.
 *
 * Отказ сообщается как у chupa_eval: false и chupa_context_error. Значение в
 * *out borrowed по тому же правилу 1 заголовка, что и у chupa_eval. */
CHUPA_API CHUPA_MUST_USE bool
chupa_expression_eval_tracked(ChupaContext *ctx, ChupaExpression *e,
                              ChupaValue *out,
                              ChupaDep deps[CHUPA_NONNULL CHUPA_MAX_DEPS],
                              uint32_t *n);

/* ─── Evaluation ──────────────────────────────────────────────────────────
 *
 * ChupaValue is a plain 16-byte struct. It is not a handle into a table and
 * it is not an allocation: it is the engine's own value representation,
 * handed over unchanged. Producing one costs nothing.
 *
 * chupa_eval yields a value of ANY kind — including an array or an object —
 * and lets the host walk it. None of the walking functions below take a
 * ChupaContext *. That is not a saved parameter, it is the whole point: an
 * aggregate carries its own storage and its own key names, so it can be read
 * when the context that produced it is already destroyed.
 *
 * LIFETIME. The value written to *out is BORROWED — rule 1 in the header's
 * opening block. To keep it longer, chupa_value_retain / _release, which are
 * no-ops on scalars.
 *
 * A bare string-literal result is a narrower case on top of that: its bytes
 * belong to the compiled unit, not to the context, so destroying the
 * ChupaExpression or ChupaScript that produced it ends the life of any
 * un-retained value read from it — even though the context the unit was
 * compiled against is still alive. Recompiling into that same unit does the
 * same, because compiling discards what the unit held before.
 * chupa_value_retain covers this case exactly as it covers every other
 * borrowed value. */

CHUPA_API CHUPA_MUST_USE bool
chupa_eval(ChupaContext *ctx, ChupaExpression *e, ChupaValue *out);

CHUPA_API CHUPA_MUST_USE bool chupa_run(ChupaContext *ctx, ChupaScript *script);

/* Shortcuts. Same semantics as chupa_eval followed by a kind check, in one
 * crossing of the C boundary. They exist because the constant and variable
 * cases are the ones a scrolling frame runs thousands of times.
 *
 * Return false when no value of that kind was produced. The error tells which:
 *   CHUPA_ERR_NONE   the expression evaluated to null
 *   CHUPA_ERR_TYPE   it produced a value of another kind
 *   anything else    evaluation failed
 * On false, *out (and, for the string shortcut, *bytes and *len) is left
 * untouched.
 *
 * chupa_eval_string's bytes are borrowed from the Context, not from the
 * caller's own ChupaValue — there is no host-owned ChupaValue in this call at
 * all — so they stay valid until the next call on ctx (rule 1, applied to a
 * value the Context itself is holding on the caller's behalf). They are NOT
 * NUL-terminated. */
CHUPA_API CHUPA_MUST_USE bool
chupa_eval_number(ChupaContext *ctx, ChupaExpression *e, double *out);

CHUPA_API CHUPA_MUST_USE bool
chupa_eval_bool(ChupaContext *ctx, ChupaExpression *e, bool *out);

CHUPA_API CHUPA_MUST_USE bool
chupa_eval_string(ChupaContext *ctx, ChupaExpression *e,
                  const char *CHUPA_NULLABLE *CHUPA_NONNULL bytes,
                  size_t *len);

/* ─── Values: every one of these takes the value BY ADDRESS ─────────────── */

CHUPA_API ChupaKind chupa_value_kind(const ChupaValue *v);

/* Precondition: chupa_value_kind(v) == CHUPA_KIND_BOOL / _NUMBER. */
CHUPA_API bool   chupa_value_bool  (const ChupaValue *v);
CHUPA_API double chupa_value_number(const ChupaValue *v);

/* Precondition: chupa_value_kind(v) == CHUPA_KIND_STRING.
 * An empty string yields *len == 0 and *bytes possibly NULL. Bytes are
 * borrowed from *v — rule 2. */
CHUPA_API void chupa_value_string(const ChupaValue *v,
                                  const char *CHUPA_NULLABLE *CHUPA_NONNULL bytes,
                                  size_t *len);

/* Precondition: chupa_value_kind(v) == CHUPA_KIND_ARRAY.
 * Past the end, *out is set to null (docs/semantics.md 6.1) rather than
 * failing — chupa_array_at never signals a range error. */
CHUPA_API size_t chupa_array_count(const ChupaValue *v);
CHUPA_API void   chupa_array_at   (const ChupaValue *v, size_t i, ChupaValue *out);

/* Precondition: chupa_value_kind(v) == CHUPA_KIND_OBJECT.
 *
 * Enumeration order is not promised (docs/semantics.md 2.1). chupa_object_get
 * returns false and leaves *out untouched when the key is absent, and true
 * with *out written when it is present — including a key present with the
 * value null, which the old by-value signature could not tell apart from
 * absent. chupa_object_key_at yields *len == 0 past the end. */
CHUPA_API size_t chupa_object_count   (const ChupaValue *v);
CHUPA_API void   chupa_object_key_at  (const ChupaValue *v, size_t i,
                                       const char *CHUPA_NULLABLE *CHUPA_NONNULL bytes,
                                       size_t *len);
CHUPA_API void   chupa_object_value_at(const ChupaValue *v, size_t i, ChupaValue *out);
CHUPA_API CHUPA_MUST_USE bool
chupa_object_get(const ChupaValue *v, const char *key, size_t key_len,
                 ChupaValue *out);

/* Reference counting. Both are no-ops on scalars and on values that reference
 * nothing counted. Releasing more than was retained corrupts the engine's
 * bookkeeping; debug builds trap on it. */
CHUPA_API void chupa_value_retain (const ChupaValue *v);
CHUPA_API void chupa_value_release(const ChupaValue *v);

CHUPA_NONNULL_END

#ifdef __cplusplus
}
#endif

#endif /* CHUPASCRIPT_H */
