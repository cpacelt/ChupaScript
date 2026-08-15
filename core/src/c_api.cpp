// C API boundary — a thin wrapper over the core entities.
//
// The public C header declares the full API surface; this translation unit
// implements it by forwarding to CS::Store, CS::Expression and CS::Script.
#include "chupascript/chupascript.h"

#include <cstdint>
#include <cstring>
#include <memory>
#include <string_view>

#include "data.hpp"
#include "diagnostic.hpp"
#include "expression.hpp"
#include "script.hpp"
#include "store.hpp"
#include "value.hpp"

// ─── Opaque struct definitions ───
// Defined here, not in the header: C doesn't see C++ members.

struct ChupaContext {
    CS::Store engine;

    /// Состояние последней ошибки в стиле errno — идиома C, вынужденная тем,
    /// что второе значение из функции здесь вернуть нечем. В C++ ошибка
    /// приходит выходным параметром Diagnostic & (core/src/expression.hpp).
    CS::Diagnostic lastError;

    // ╔════════════════════════════════════════════════════════════════════╗
    // ║ UAF-2 (C-половина) — колбэк переживает того, на кого указывает     ║
    // ╚════════════════════════════════════════════════════════════════════╝
    //
    // redrawUserData — непрозрачный указатель на объект хоста. В Swift это
    // Unmanaged.passUnretained(ChupaContext), см. swift/ChupaContext.swift.
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
    // Swift-обёртка (swift/ChupaContext.swift) в своём заголовочном
    // комментарии УТВЕРЖДАЕТ, что «deinit calls chupa_context_destroy which
    // unregisters the redraw callback». Это неправда: смотри код выше и ниже —
    // здесь только delete. Снятия колбэка нет ни здесь, ни в Swift.
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
    c->clearError();
    c->notifyRedraw();
    return true;
}

// ─── Set: scalars ───

void chupa_context_set_bool(ChupaContext* ctx, const char* name, size_t name_len,
                            bool value) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    c->engine.setGlobal(std::string_view(name, name_len),
                        CS::Value::boolean(value));
    c->clearError();
    c->notifyRedraw();
}

void chupa_context_set_number(ChupaContext* ctx, const char* name, size_t name_len,
                              double value) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    c->engine.setGlobal(std::string_view(name, name_len),
                        CS::Value::number(value));
    c->clearError();
    c->notifyRedraw();
}

void chupa_context_set_string(ChupaContext* ctx, const char* name, size_t name_len,
                              const char* text, size_t text_len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    CS::Value str = c->engine.makeString(std::string_view(text, text_len));
    c->engine.setGlobal(std::string_view(name, name_len), str);
    c->clearError();
    c->notifyRedraw();
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
        std::string_view(source, len), c->engine, &e->impl, &diag, 1);
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
        std::string_view(source, len), c->engine, &s->impl, &diag, 1);
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
// диагностику от прошлого вызова. Исключение — chupa_eval_string: она
// собрана вручную и чистит после, потому что ошибку типа ставит сама.

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
    return toStatus(expr->impl.evalNumber(c->engine, out, c->lastError));
}

ChupaStatus chupa_eval_bool(ChupaContext* ctx, ChupaExpression* e,
                            bool* out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);
    c->clearError();
    return toStatus(expr->impl.evalBool(c->engine, out, c->lastError));
}

// На evalString эта функция НЕ переводится: тот отдаёт результат в
// std::string, а наружу здесь уходит сырой указатель — указывать ему было бы
// некуда, временная строка умрёт на выходе. Отсюда ручной разбор вида
// значения и метка UAF-1 ниже; и подпись, и дыру снимает задача 5.
ChupaStatus chupa_eval_string(ChupaContext* ctx, ChupaExpression* e,
                              const char** out, size_t* len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);

    CS::Value value = CS::Value::null();
    CS::Diagnostic diag;
    if (!expr->impl.eval(c->engine, &value, diag)) {
        c->setError(diag);
        return CHUPA_ERROR;
    }
    c->clearError();
    if (value.kind() == CS::Value::Kind::Null) {
        return CHUPA_NULL;
    }
    if (value.kind() != CS::Value::Kind::String) {
        c->setError({CS::ErrorCode::Type, 0, "eval_string: value is not a string"});
        return CHUPA_ERROR;
    }
    std::string_view sv = c->engine.string(value);

    // ╔════════════════════════════════════════════════════════════════════╗
    // ║ UAF-1 — ЗДЕСЬ. Наружу отдаётся указатель ВНУТРЬ пула движка        ║
    // ╚════════════════════════════════════════════════════════════════════╝
    //
    // sv.data() указывает внутрь CS::Store::text_ — это std::vector<char>
    // (store.hpp:201). Сам CS::Store об этом честно предупреждает в своей
    // шапке (store.hpp:24-26): срез живёт «лишь до ближайшей мутации того
    // же хранилища; дольше их хранить нельзя».
    //
    // А мутирует text_ буквально всё:
    //   chupa_context_set_string, chupa_context_set, конкатенация строк
    //   и format ВНУТРИ следующего eval — все они зовут makeString.
    // Любой из них может реаллоцировать вектор и подвесить выданный указатель.
    //
    //     chupa_eval_string(ctx, e, &p, &n);
    //     chupa_context_set_string(ctx, "x", 1, "...", 3);  // text_ переехал
    //     puts(p);                                          // UAF
    //
    // При этом в chupascript.h окно валидности НЕ ОПИСАНО ВООБЩЕ — контракт
    // не нарушается, его просто нет.
    //
    // Ровно здесь обёртка обязана превращать внутреннее время жизни движка в
    // стабильное для хоста — и не делает этого, а пробрасывает насквозь.
    //
    // Swift сегодня цел СЛУЧАЙНО: ChupaExpression.evalString копирует в String
    // сразу же. Любой хост на C/ObjC/Rust, придержавший указатель, — падает.
    //
    // Починка: см. план, шаг 2 — копирование в буфер вызывающего.
    *out = sv.data();
    *len = sv.size();
    return CHUPA_OK;
}

// ─── Run ───

bool chupa_run(ChupaContext* ctx, ChupaScript* script) {
    if (!ctx || !script) { return false; }
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* s = reinterpret_cast<::ChupaScript*>(script);

    CS::Diagnostic diag;
    if (!s->impl.run(c->engine, diag)) {
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
