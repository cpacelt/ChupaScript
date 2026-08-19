// C API boundary — a thin wrapper over the core entities.
//
// The public C header declares the full API surface; this translation unit
// implements it by forwarding to CS::Store, CS::Expression and CS::Script.
#include "chupascript/chupascript.h"

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
    // ⚠️ UAF-2 — ЗДЕСЬ НИЧЕГО НЕ СНИМАЕТСЯ.
    // Ни redrawListener, ни redrawUserData не обнуляются перед delete.
    // Снятия колбэка нет ни здесь, ни в Swift: swift/Context.swift не зовёт
    // chupa_context_on_redraw(handle, nil, nil) ни в deinit, ни где-либо ещё.
    // (Раньше здесь приводилась цитата из шапки swift/ChupaContext.swift,
    // утверждавшей обратное. Задача 7 ложную фразу удалила — дефект от этого
    // не исчез, см. B38.)
    delete reinterpret_cast<::ChupaContext*>(ctx);
}

// ─── Set: text literal ───

bool chupa_context_set_data(ChupaContext* ctx, const char* name, size_t name_len,
                            const char* text, size_t text_len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
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
    const std::string_view key(name, name_len);
    if (!acceptName(c, key)) { return false; }

    c->impl.setGlobalString(key, std::string_view(text, text_len));
    c->clearError();
    c->notifyRedraw();
    return true;
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
    }
    return CHUPA_ERR_NONE;
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

// ─── Compile ───

ChupaExpression* chupa_compile_expression(ChupaContext* ctx,
                                          const char* source, size_t len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
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

static_assert(sizeof(ChupaValue) == sizeof(CS::Value),
              "ChupaValue обязан совпадать с CS::Value байт в байт");
static_assert(alignof(ChupaValue) >= alignof(CS::Value),
              "выравнивание ChupaValue не должно быть слабее");

/// Reinterprets the host's sixteen bytes as a CS::Value IN PLACE.
///
/// A reference, not a copy: a short string's bytes live inside the value, and
/// every slice handed back to the host points into the host's own variable.
const CS::Value& fromC(const ChupaValue* v) {
    return *reinterpret_cast<const CS::Value*>(v);
}

/// Copies a CS::Value into a host-owned output slot. The reverse of fromC:
/// here the host's storage is the destination, so a copy is unavoidable and
/// harmless — the sixteen bytes just landed in the caller's own variable,
/// which is exactly where the by-address contract wants them.
void toC(CS::Value v, ChupaValue* out) {
    std::memcpy(out, &v, sizeof(*out));
}

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

// ─── Eval ───
//
// One clearing order for the whole section: clearError() runs BEFORE the
// core call. On Ok the core leaves diag untouched entirely (docblock on
// Expression::eval, core/src/expression.hpp), so without clearing first a
// successful evaluation would leak the previous call's diagnostic.

bool chupa_eval(ChupaContext* ctx, ChupaExpression* e, ChupaValue* out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
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
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();
    return c->impl.evalBool(expr->impl, out, c->lastError) == CS::EvalStatus::Ok;
}

bool chupa_eval_string(ChupaContext* ctx, ChupaExpression* e,
                       const char** bytes, size_t* len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
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
    return asArray(fromC(v))->items.size();
}

void chupa_array_at(const ChupaValue* v, size_t i, ChupaValue* out) {
    const CS::detail::ArrayBox* box = asArray(fromC(v));
    if (i >= box->items.size()) {
        toC(CS::Value::null(), out);
        return;
    }
    // Ссылка не берётся: элемент держит сам массив, а массив держит хост.
    toC(box->items[i], out);
}

size_t chupa_object_count(const ChupaValue* v) {
    return asObject(fromC(v))->entries.size();
}

void chupa_object_key_at(const ChupaValue* v, size_t i, const char** bytes,
                         size_t* len) {
    const CS::detail::ObjectBox* box = asObject(fromC(v));
    if (i >= box->entries.size()) {
        *bytes = nullptr;
        *len = 0;
        return;
    }
    // Имя берётся из таблицы коробки, а не из чьего-то хранилища: она и есть то,
    // что делает объект читаемым после смерти контекста.
    const std::string_view key = box->keys->bytes(box->entries[i].key);
    *bytes = key.data();
    *len = key.size();
}

void chupa_object_value_at(const ChupaValue* v, size_t i, ChupaValue* out) {
    const CS::detail::ObjectBox* box = asObject(fromC(v));
    if (i >= box->entries.size()) {
        toC(CS::Value::null(), out);
        return;
    }
    toC(box->entries[i].value, out);
}

bool chupa_object_get(const ChupaValue* v, const char* key, size_t key_len,
                      ChupaValue* out) {
    const CS::detail::ObjectBox* box = asObject(fromC(v));
    bool found = false;
    const std::uint32_t at =
        CS::detail::findEntry(*box, std::string_view(key, key_len), &found);
    if (!found) { return false; }
    toC(box->entries[at].value, out);
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
    c->redrawListener = listener;
    c->redrawUserData = user_data;
}
