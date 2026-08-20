// C API boundary — a thin wrapper over the core entities.
//
// The public C header declares the full API surface; this translation unit
// implements it by forwarding to CS::Context.
#include "chupascript/chupascript.h"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

#include "data.hpp"
#include "diagnostic.hpp"
#include "context.hpp"
#include "expression.hpp"
#include "box.hpp"
#include "keytable.hpp"
#include "script.hpp"
#include "store.hpp"
#include "value.hpp"

// ─── Opaque struct definitions ───
// Defined here, not in the header: C doesn't see C++ members.

struct ChupaContext {
    /// Проводит непрозрачный указатель на себя внутрь ядра прямо при
    /// построении, а не отдельным вызовом в теле chupa_context_create: вызов,
    /// который обязан случиться, но стоит отдельно от построения, однажды
    /// забудут — так и вышло с сеттером таблицы в задаче 7, и повторять это
    /// здесь не будем.
    ChupaContext() { impl.setHostHandle(this); }

    /// Хранилище и состояние выполнения вместе с границей операции —
    /// core/src/context.hpp. Здесь остаётся только то, что принадлежит C:
    /// ошибка в стиле errno и колбэк перерисовки.
    CS::Context impl;

    /// Состояние последней ошибки в стиле errno — идиома C, вынужденная тем,
    /// что второе значение из функции здесь вернуть нечем. В C++ ошибка
    /// приходит выходным параметром Diagnostic & (core/src/expression.hpp).
    CS::Diagnostic lastError;

    // ╔════════════════════════════════════════════════════════════════════╗
    // ║ UAF-2 (C-половина) — колбэк переживает того, на кого указывает     ║
    // ╚════════════════════════════════════════════════════════════════════╝
    //
    // redrawUserData — непрозрачный указатель на объект хоста. В Swift это
    // Unmanaged.passUnretained(Context), см. swift/Context.swift.
    // Здесь его никто не удерживает и никто не проверяет на живость.
    //
    // chupa_context_destroy (ниже) просто delete'ит — листенер не обнуляется
    // и не вызывается на прощание. Пока хост сам не снимет колбэк перед
    // разрушением, любой notifyRedraw по мёртвому user_data — это UAF.
    // Swift-обёртка этого не делает: снятия нет нигде.
    ChupaRedrawListener redrawListener = nullptr;
    void* redrawUserData = nullptr;

    void notifyRedraw() {
        if (redrawListener) {
            redrawListener(reinterpret_cast<::ChupaContext*>(this), redrawUserData);
        }
    }

    void setError(const CS::Diagnostic& diag) { lastError = diag; }

    void clearError() {
        lastError.code = CS::ErrorCode::None;
        lastError.offset = 0;
        lastError.message = "";
    }
};

// Обёртки однополевые: вся работа живёт в сущностях ядра, а владеет ими
// хост — контекст о скомпилированных единицах больше не знает (B35).
struct ChupaExpression { CS::Expression impl; };
struct ChupaScript     { CS::Script     impl; };

// ─── Guarding the closed context ───
//
// A host callback runs in the middle of a tree walk that owns the deferred
// list and the argument stack (execution.hpp). Every door that writes,
// compiles or evaluates must refuse for as long as that walk is in flight —
// see refuseWhileEvaluating below, called first thing in each such door.

namespace {

/// Отказ, если на контексте прямо сейчас идёт вычисление.
///
/// Страж работает в релизе, а не только под assert: без него ошибка хоста
/// проявляется не отказом, а сливом списка отложенного освобождения посреди
/// обхода дерева — то есть тихо испорченными данными на чужом устройстве.
///
/// Перечислять, что именно опасно, значило бы поддерживать этот список верным
/// вечно; закрыто всё, что пишет, компилирует или вычисляет.
bool refuseWhileEvaluating(::ChupaContext *c) {
    if (!c->impl.isEvaluating()) { return false; }
    c->setError({CS::ErrorCode::Usage, 0,
                 "the context is closed while a host function is running"});
    return true;
}

}  // namespace

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
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    // Destroying the context out from under the call stack currently walking
    // it would free memory that walk is still reading. Refusing leaks ctx if
    // the host really does this — a host bug either way — and a leak beats
    // destruction out from under yourself.
    if (refuseWhileEvaluating(c)) { return; }
    // ⚠️ UAF-2 — ЗДЕСЬ НИЧЕГО НЕ СНИМАЕТСЯ.
    // Ни redrawListener, ни redrawUserData не обнуляются перед delete.
    // Снятия колбэка нет ни здесь, ни в Swift: swift/Context.swift не зовёт
    // chupa_context_on_redraw(handle, nil, nil) ни в deinit, ни где-либо ещё.
    // (Раньше здесь приводилась цитата из шапки swift/ChupaContext.swift,
    // утверждавшей обратное. Задача 7 ложную фразу удалила — дефект от этого
    // не исчез, см. B38.)
    delete c;
}

// ─── Set: text literal ───

bool chupa_context_set_data(ChupaContext* ctx, const char* name, size_t name_len,
                            const char* text, size_t text_len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return false; }
    CS::Diagnostic diag;
    bool ok = c->impl.setVariableText(std::string_view(name, name_len),
                                      std::string_view(text, text_len), diag);
    if (!ok) {
        c->setError(diag);
        return false;
    }
    c->clearError();
    c->notifyRedraw();
    return true;
}

// ─── Set: scalars ───
//
// Имя проверяется до любой работы: у set_string иначе строка успела бы попасть
// в пул и осталась бы там мусором после отказа.

namespace {

/// Общая часть трёх сеттеров: проверка имени с записью отказа в контекст.
bool acceptName(::ChupaContext* c, std::string_view name) {
    if (CS::isGlobalName(name)) { return true; }
    c->setError({CS::ErrorCode::Name, 0, "global name must be an identifier"});
    return false;
}

}  // namespace

bool chupa_context_set_bool(ChupaContext* ctx, const char* name, size_t name_len,
                            bool value) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return false; }
    const std::string_view key(name, name_len);
    if (!acceptName(c, key)) { return false; }

    c->impl.setGlobal(key, CS::Value::boolean(value));
    c->clearError();
    c->notifyRedraw();
    return true;
}

bool chupa_context_set_number(ChupaContext* ctx, const char* name, size_t name_len,
                              double value) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return false; }
    const std::string_view key(name, name_len);
    if (!acceptName(c, key)) { return false; }

    c->impl.setGlobal(key, CS::Value::number(value));
    c->clearError();
    c->notifyRedraw();
    return true;
}

bool chupa_context_set_string(ChupaContext* ctx, const char* name, size_t name_len,
                              const char* text, size_t text_len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return false; }
    const std::string_view key(name, name_len);
    if (!acceptName(c, key)) { return false; }

    c->impl.setGlobalString(key, std::string_view(text, text_len));
    c->clearError();
    c->notifyRedraw();
    return true;
}

// ─── Host functions: register ───

namespace {

/// Translates a refusal into the pair the context's error reports.
///
/// A switch without a default: RegisterOutcome grew two members between
/// task 3 and task 4 (TooLate, Reentrant) with no compiler complaint at the
/// call site that consumed it — adding a tenth must not repeat that.
///
/// Ok never reaches here (chupa_register branches on it above); the case is
/// still spelled out because the switch has no default to fall back on.
CS::Diagnostic errorFor(CS::RegisterOutcome outcome) {
    switch (outcome) {
        case CS::RegisterOutcome::Ok:
            return {CS::ErrorCode::None, 0, ""};
        case CS::RegisterOutcome::BadName:
            return {CS::ErrorCode::Name, 0,
                   "function name must be an identifier, not a reserved word"};
        case CS::RegisterOutcome::NameTaken:
            return {CS::ErrorCode::Name, 0,
                   "a builtin or another registered function already has this name"};
        case CS::RegisterOutcome::NoCallback:
            return {CS::ErrorCode::Usage, 0, "fn->call must not be NULL"};
        case CS::RegisterOutcome::BadArity:
            return {CS::ErrorCode::Usage, 0,
                   "fn->min_args must not exceed fn->max_args"};
        case CS::RegisterOutcome::BadFlags:
            return {CS::ErrorCode::Usage, 0,
                   "CHUPA_FN_DETERMINISTIC requires CHUPA_FN_PURE"};
        case CS::RegisterOutcome::TableFull:
            // Usage, not Range: Range in this engine belongs to the language's
            // own value space (an array index past the end), and every other
            // API-misuse outcome here already answers Usage. Registering a
            // 128th function is a host-side misuse of chupa_register, not a
            // value out of range.
            return {CS::ErrorCode::Usage, 0,
                   "no more host functions may be registered on this context"};
        case CS::RegisterOutcome::TooLate:
            return {CS::ErrorCode::Usage, 0,
                   "chupa_register must run before the first compilation on this context"};
        case CS::RegisterOutcome::Reentrant:
            return {CS::ErrorCode::Usage, 0,
                   "chupa_register was called from inside a host callback"};
    }
    return {CS::ErrorCode::None, 0, ""};
}

}  // namespace

bool chupa_register(ChupaContext* ctx, const ChupaFunction* fn) {
    if (ctx == nullptr || fn == nullptr) { return false; }
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return false; }
    const CS::RegisterOutcome outcome = c->impl.registerFunction(*fn);
    if (outcome == CS::RegisterOutcome::Ok) {
        c->clearError();
        return true;
    }
    // Утверждение вместе с отказом: код регистрации статичен и выполняется до
    // всего остального, поэтому разработчик увидит его на первом же запуске у
    // себя, а не пользователь на устройстве. В релизе утверждение исчезает, и
    // настоящим контрактом остаётся возвращаемое false.
    assert(false && "chupa_register отказал — см. код ошибки контекста");
    c->setError(errorFor(outcome));
    return false;
}

// ─── Error reporting ───

namespace {

// CS::ErrorCode values intentionally match ChupaErrorCode (diagnostic.hpp).
// The static_asserts below guard against accidental drift.
static_assert(static_cast<int>(CS::ErrorCode::None)   == CHUPA_ERR_NONE,   "ErrorCode::None must match CHUPA_ERR_NONE");
static_assert(static_cast<int>(CS::ErrorCode::Syntax) == CHUPA_ERR_SYNTAX, "ErrorCode::Syntax must match CHUPA_ERR_SYNTAX");
static_assert(static_cast<int>(CS::ErrorCode::Name)   == CHUPA_ERR_NAME,   "ErrorCode::Name must match CHUPA_ERR_NAME");
static_assert(static_cast<int>(CS::ErrorCode::Type)   == CHUPA_ERR_TYPE,   "ErrorCode::Type must match CHUPA_ERR_TYPE");
static_assert(static_cast<int>(CS::ErrorCode::Range)  == CHUPA_ERR_RANGE,  "ErrorCode::Range must match CHUPA_ERR_RANGE");
static_assert(static_cast<int>(CS::ErrorCode::Data)   == CHUPA_ERR_DATA,   "ErrorCode::Data must match CHUPA_ERR_DATA");
static_assert(static_cast<int>(CS::ErrorCode::Usage)  == CHUPA_ERR_USAGE,  "ErrorCode::Usage must match CHUPA_ERR_USAGE");
static_assert(static_cast<int>(CS::ErrorCode::Memory) == CHUPA_ERR_MEMORY, "ErrorCode::Memory must match CHUPA_ERR_MEMORY");
static_assert(static_cast<int>(CS::ErrorCode::Host)   == CHUPA_ERR_HOST,   "ErrorCode::Host must match CHUPA_ERR_HOST");

/// Translates the core's error code to the public one. Mapped explicitly, not
/// cast, to guard against future enum drift even though the values currently
/// match (see the static_asserts above).
ChupaErrorCode toCode(CS::ErrorCode code) {
    switch (code) {
        case CS::ErrorCode::None:    return CHUPA_ERR_NONE;
        case CS::ErrorCode::Syntax:  return CHUPA_ERR_SYNTAX;
        case CS::ErrorCode::Name:    return CHUPA_ERR_NAME;
        case CS::ErrorCode::Type:    return CHUPA_ERR_TYPE;
        case CS::ErrorCode::Range:   return CHUPA_ERR_RANGE;
        case CS::ErrorCode::Data:    return CHUPA_ERR_DATA;
        case CS::ErrorCode::Usage:   return CHUPA_ERR_USAGE;
        case CS::ErrorCode::Memory:  return CHUPA_ERR_MEMORY;
        case CS::ErrorCode::Host:    return CHUPA_ERR_HOST;
    }
    return CHUPA_ERR_NONE;
}

/// The inverse of toCode, for chupa_fail: a host reports its refusal in the
/// public vocabulary, and setHostFailure stores it in the core's. Mapped
/// explicitly for the same reason toCode is — a switch with no default so a
/// code ChupaErrorCode grows tomorrow fails to compile here instead of
/// silently falling through.
CS::ErrorCode fromCode(ChupaErrorCode code) {
    switch (code) {
        case CHUPA_ERR_NONE:   return CS::ErrorCode::None;
        case CHUPA_ERR_SYNTAX: return CS::ErrorCode::Syntax;
        case CHUPA_ERR_NAME:   return CS::ErrorCode::Name;
        case CHUPA_ERR_TYPE:   return CS::ErrorCode::Type;
        case CHUPA_ERR_RANGE:  return CS::ErrorCode::Range;
        case CHUPA_ERR_DATA:   return CS::ErrorCode::Data;
        case CHUPA_ERR_USAGE:  return CS::ErrorCode::Usage;
        case CHUPA_ERR_MEMORY: return CS::ErrorCode::Memory;
        case CHUPA_ERR_HOST:   return CS::ErrorCode::Host;
    }
    return CS::ErrorCode::Host;
}

}  // namespace

void chupa_context_error(const ChupaContext* ctx, ChupaError* out) {
    if (!ctx || !out) { return; }
    const auto* c = reinterpret_cast<const ::ChupaContext*>(ctx);
    const char* message = c->lastError.message ? c->lastError.message : "";
    out->code = toCode(c->lastError.code);
    out->offset = c->lastError.offset;
    out->message = message;
    out->message_len = std::strlen(message);
}

// ─── Host functions: fail ───
//
// The opposite guard from refuseWhileEvaluating's family above: every other
// door refuses WHILE a call is in flight, this one only WORKS while one is —
// it has no meaning once the callback that would call it has returned.

void chupa_fail(ChupaContext* ctx, ChupaErrorCode code, const char* msg,
                size_t len) {
    if (ctx == nullptr) { return; }
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (!c->impl.isEvaluating()) {
        c->setError({CS::ErrorCode::Usage, 0,
                    "chupa_fail was called outside a host callback"});
        return;
    }
    c->impl.setHostFailure(fromCode(code),
                           std::string_view(msg == nullptr ? "" : msg, len));
}

// ─── Compile ───

ChupaExpression* chupa_compile_expression(ChupaContext* ctx,
                                          const char* source, size_t len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return nullptr; }
    auto e = std::make_unique<::ChupaExpression>();

    CS::Diagnostic diag;
    const std::uint32_t errors =
        c->impl.compileExpression(std::string_view(source, len), &e->impl, &diag, 1);
    if (errors != 0) {
        c->setError(diag);
        return nullptr;   // unique_ptr уносит с собой всё, что успело завестись
    }
    c->clearError();
    return reinterpret_cast<ChupaExpression*>(e.release());
}

ChupaScript* chupa_compile_script(ChupaContext* ctx,
                                  const char* source, size_t len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return nullptr; }
    auto s = std::make_unique<::ChupaScript>();

    CS::Diagnostic diag;
    const std::uint32_t errors =
        c->impl.compileScript(std::string_view(source, len), &s->impl, &diag, 1);
    if (errors != 0) {
        c->setError(diag);
        return nullptr;   // unique_ptr уносит с собой всё, что успело завестись
    }
    c->clearError();
    return reinterpret_cast<ChupaScript*>(s.release());
}

// ─── Destroy ───

void chupa_expression_destroy(ChupaExpression* e) {
    delete reinterpret_cast<::ChupaExpression*>(e);
}

void chupa_script_destroy(ChupaScript* s) {
    delete reinterpret_cast<::ChupaScript*>(s);
}

// ─── Values: address in, address out ───
//
// ChupaValue is the same sixteen bytes as CS::Value, and fromC reinterprets a
// host pointer as a CS::Value IN PLACE — no copy, no table, no allocation.
// That is what makes chupa_value_string safe: the slice it hands back points
// into the host's own ChupaValue variable, not into a CS::Value that lived
// only inside this translation unit and died on return (defect В3).

namespace {

// fromC/toC and their layout static_asserts live in host.hpp now — eval.cpp
// needs them too, and host.hpp is already on both translation units'
// include path (see the block comment there). This file lives in the global
// namespace, so the two are pulled in by name rather than qualified at every
// call site below.
using CS::fromC;
using CS::toC;

// CS::Value::Kind and ChupaKind deliberately do NOT match for Object/Array:
// CS::Value::Kind puts Object before Array (value.hpp), ChupaKind puts Array
// before Object (chupascript.h) for its own historical reasons. toKind below
// maps explicitly, never casts, and these asserts exist so a future
// "simplification" into a cast fails to compile instead of silently
// swapping every array and object crossing the C boundary.
static_assert(static_cast<int>(CS::Value::Kind::Array) != CHUPA_KIND_ARRAY,
              "CS::Value::Kind::Array and CHUPA_KIND_ARRAY are meant to differ; toKind must map explicitly");
static_assert(static_cast<int>(CS::Value::Kind::Object) != CHUPA_KIND_OBJECT,
              "CS::Value::Kind::Object and CHUPA_KIND_OBJECT are meant to differ; toKind must map explicitly");

ChupaKind toKind(CS::Value::Kind kind) {
    switch (kind) {
        case CS::Value::Kind::Null:    return CHUPA_KIND_NULL;
        case CS::Value::Kind::Boolean: return CHUPA_KIND_BOOL;
        case CS::Value::Kind::Number:  return CHUPA_KIND_NUMBER;
        case CS::Value::Kind::String:  return CHUPA_KIND_STRING;
        case CS::Value::Kind::Array:   return CHUPA_KIND_ARRAY;
        case CS::Value::Kind::Object:  return CHUPA_KIND_OBJECT;
    }
    return CHUPA_KIND_NULL;
}

const CS::detail::ArrayBox* asArray(const CS::Value& v) {
    return static_cast<const CS::detail::ArrayBox*>(v.box());
}

const CS::detail::ObjectBox* asObject(const CS::Value& v) {
    return static_cast<const CS::detail::ObjectBox*>(v.box());
}

}  // namespace

// ─── Making values ───
//
// out == nullptr is refused, not dereferenced: a Void host function that
// forgot CHUPA_FN_RETURNS_VALUE is called with slot == nullptr
// (core/src/eval.cpp evalHostCall) and may still reach for one of these —
// the whole family refuses the same way chupa_make_string already did,
// rather than three of four crashing on the one path chupa_make_string was
// written to survive.

void chupa_make_null(ChupaValue* out) {
    if (out == nullptr) { return; }
    toC(CS::Value::null(), out);
}

void chupa_make_bool(ChupaValue* out, bool value) {
    if (out == nullptr) { return; }
    toC(CS::Value::boolean(value), out);
}

void chupa_make_number(ChupaValue* out, double value) {
    if (out == nullptr) { return; }
    toC(CS::Value::number(value), out);
}

bool chupa_make_string(ChupaContext* ctx, const char* bytes, size_t len,
                       ChupaValue* out) {
    if (ctx == nullptr || out == nullptr) { return false; }
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    const std::string_view text(bytes == nullptr ? "" : bytes, len);
    toC(c->impl.makeString(text), out);
    return true;
}

// ─── Eval ───
//
// One clearing order for the whole section: clearError() runs BEFORE the
// core call. On Ok the core leaves diag untouched entirely (docblock on
// Expression::eval, core/src/expression.hpp), so without clearing first a
// successful evaluation would leak the previous call's diagnostic.

bool chupa_eval(ChupaContext* ctx, ChupaExpression* e, ChupaValue* out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return false; }
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();

    CS::Value value = CS::Value::null();
    if (!c->impl.eval(expr->impl, &value, c->lastError)) { return false; }
    // Null is a kind, not an outcome: the value says so itself, and a separate
    // return code for it existed only because a double * had nowhere to put
    // "it came out null".
    toC(value, out);
    return true;
}

bool chupa_eval_number(ChupaContext* ctx, ChupaExpression* e, double* out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return false; }
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();
    // Expression::evalNumber already answers Null with CHUPA_ERR_NONE (diag
    // untouched) and a wrong kind with CHUPA_ERR_TYPE — exactly the two-way
    // split the shortcut promises, so there is nothing left to translate but
    // the outcome itself.
    return c->impl.evalNumber(expr->impl, out, c->lastError) == CS::EvalStatus::Ok;
}

bool chupa_eval_bool(ChupaContext* ctx, ChupaExpression* e, bool* out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return false; }
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();
    return c->impl.evalBool(expr->impl, out, c->lastError) == CS::EvalStatus::Ok;
}

bool chupa_eval_string(ChupaContext* ctx, ChupaExpression* e,
                       const char** bytes, size_t* len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return false; }
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();

    CS::Value value = CS::Value::null();
    if (!c->impl.eval(expr->impl, &value, c->lastError)) { return false; }
    if (value.kind() == CS::Value::Kind::Null) { return false; }  // error stays NONE
    if (value.kind() != CS::Value::Kind::String) {
        c->setError({CS::ErrorCode::Type, 0, "expression is not a string"});
        return false;
    }

    // Borrowed from the Context's own slot, not from a local: a short string's
    // bytes live inside the value, and a local would die on return.
    const CS::Value& kept = c->impl.keepResult(value);
    const std::string_view text = CS::stringBytes(kept);
    *bytes = text.data();
    *len = text.size();
    return true;
}

// ─── Values: reading an aggregate crossing the boundary ───
//
// Ни одна функция обхода не берёт ChupaContext *, и это не экономия: коробка
// самодостаточна, объект носит свою таблицу имён, поэтому читать его можно и
// тогда, когда контекста уже нет. В прежней модели такой сигнатуры быть не
// могло — значение там было индексом в пулы конкретного хранилища.

ChupaKind chupa_value_kind(const ChupaValue* v) { return toKind(fromC(v).kind()); }

bool chupa_value_bool(const ChupaValue* v) { return fromC(v).booleanValue(); }

double chupa_value_number(const ChupaValue* v) { return fromC(v).numberValue(); }

void chupa_value_string(const ChupaValue* v, const char** bytes, size_t* len) {
    const std::string_view text = CS::stringBytes(fromC(v));
    *bytes = text.data();
    *len = text.size();
}

size_t chupa_array_count(const ChupaValue* v) {
    return asArray(fromC(v))->size();
}

void chupa_array_at(const ChupaValue* v, size_t i, ChupaValue* out) {
    const CS::detail::ArrayBox* box = asArray(fromC(v));
    if (i >= box->size()) {
        toC(CS::Value::null(), out);
        return;
    }
    // Ссылка не берётся: элемент держит сам массив, а массив держит хост.
    toC(box->at(static_cast<std::uint32_t>(i)), out);
}

size_t chupa_object_count(const ChupaValue* v) {
    return asObject(fromC(v))->size();
}

void chupa_object_key_at(const ChupaValue* v, size_t i, const char** bytes,
                         size_t* len) {
    const CS::detail::ObjectBox* box = asObject(fromC(v));
    if (i >= box->size()) {
        *bytes = nullptr;
        *len = 0;
        return;
    }
    // Имя берётся из таблицы коробки, а не из чьего-то хранилища: она и есть то,
    // что делает объект читаемым после смерти контекста.
    const std::string_view key = box->keys->bytes(box->at(static_cast<std::uint32_t>(i)).key);
    *bytes = key.data();
    *len = key.size();
}

void chupa_object_value_at(const ChupaValue* v, size_t i, ChupaValue* out) {
    const CS::detail::ObjectBox* box = asObject(fromC(v));
    if (i >= box->size()) {
        toC(CS::Value::null(), out);
        return;
    }
    toC(box->at(static_cast<std::uint32_t>(i)).value, out);
}

bool chupa_object_get(const ChupaValue* v, const char* key, size_t key_len,
                      ChupaValue* out) {
    const CS::detail::ObjectBox* box = asObject(fromC(v));
    bool found = false;
    const std::uint32_t at =
        CS::detail::findEntry(*box, std::string_view(key, key_len), &found);
    if (!found) { return false; }
    toC(box->at(at).value, out);
    return true;
}

// Скаляр обе функции пропускают молча, и это не снисхождение: хост не обязан
// разбирать, что ему вернули, — он держит ChupaValue и отпускает его так же,
// каким бы тот ни оказался.

void chupa_value_retain(const ChupaValue* v) { CS::detail::retainValue(fromC(v)); }

void chupa_value_release(const ChupaValue* v) { CS::detail::releaseValue(fromC(v)); }

// ─── Run ───

bool chupa_run(ChupaContext* ctx, ChupaScript* script) {
    if (!ctx || !script) { return false; }
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return false; }
    auto* s = reinterpret_cast<::ChupaScript*>(script);

    CS::Diagnostic diag;
    if (!c->impl.run(s->impl, diag)) {
        c->setError(diag);
        return false;
    }
    c->clearError();
    c->notifyRedraw();
    return true;
}

// ─── Redraw ───

void chupa_context_on_redraw(ChupaContext* ctx,
                             ChupaRedrawListener listener,
                             void* user_data) {
    if (!ctx) { return; }
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    if (refuseWhileEvaluating(c)) { return; }
    c->redrawListener = listener;
    c->redrawUserData = user_data;
}
