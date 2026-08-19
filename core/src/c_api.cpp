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

bool chupa_context_set(ChupaContext* ctx, const char* name, size_t name_len,
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

ChupaErrorCode chupa_context_error_code(const ChupaContext* ctx) {
    if (!ctx) { return CHUPA_ERR_NONE; }
    const auto* c = reinterpret_cast<const ::ChupaContext*>(ctx);
    // Map explicitly to guard against future enum drift even though the
    // values currently match (see static_asserts above).
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
    if (!ctx) { return 0; }
    const auto* c = reinterpret_cast<const ::ChupaContext*>(ctx);
    return c->lastError.offset;
}

const char* chupa_context_error(const ChupaContext* ctx, size_t* len) {
    if (!ctx) {
        if (len) { *len = 0; }
        return nullptr;
    }
    const auto* c = reinterpret_cast<const ::ChupaContext*>(ctx);
    const char* msg = c->lastError.message ? c->lastError.message : "";
    if (len) { *len = std::strlen(msg); }
    return msg;
}

// ─── Compile ───

ChupaExpression* chupa_compile_expression(ChupaContext* ctx,
                                          const char* source, size_t len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto e = std::make_unique<::ChupaExpression>();

    CS::Diagnostic diag;
    const std::uint32_t errors = CS::Expression::compile(
        std::string_view(source, len), c->impl.store(), &e->impl, &diag, 1);
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
    const std::uint32_t errors = CS::Script::compile(
        std::string_view(source, len), c->impl.store(), &s->impl, &diag, 1);
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

// ─── Eval ───
//
// Порядок чистки ошибки во всей секции один: clearError() идёт ДО вызова
// ядра. На исходах Ok и Null ядро diag не трогает вовсе (докблок
// evalNumber/evalBool/evalString, core/src/expression.hpp), так что без
// предварительной очистки успешное вычисление оставило бы наружу
// диагностику от прошлого вызова.

namespace {

/// Перевод исхода ядра в исход C. Единственная работа, которая остаётся
/// прокладке после переезда: два перечисления об одном и том же.
ChupaStatus toStatus(CS::EvalStatus status) {
    switch (status) {
        case CS::EvalStatus::Ok:    return CHUPA_OK;
        case CS::EvalStatus::Null:  return CHUPA_NULL;
        case CS::EvalStatus::Error: return CHUPA_ERROR;
    }
    return CHUPA_ERROR;
}

}  // namespace

ChupaStatus chupa_eval_number(ChupaContext* ctx, ChupaExpression* e,
                              double* out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();
    return toStatus(c->impl.evalNumber(expr->impl, out, c->lastError));
}

ChupaStatus chupa_eval_bool(ChupaContext* ctx, ChupaExpression* e,
                            bool* out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();
    return toStatus(c->impl.evalBool(expr->impl, out, c->lastError));
}

ChupaStatus chupa_eval_string_borrowed(ChupaContext* ctx, ChupaExpression* e,
                                       const char** bytes, size_t* len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();

    std::string_view text;
    const CS::EvalStatus status =
        c->impl.evalString(expr->impl, &text, c->lastError);
    if (status != CS::EvalStatus::Ok) { return toStatus(status); }

    // Срез в текстовый пул движка, без владения и без копии. Окно валидности
    // и его обоснование — в заголовке, у объявления.
    *bytes = text.data();
    *len = text.size();
    return CHUPA_OK;
}

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
