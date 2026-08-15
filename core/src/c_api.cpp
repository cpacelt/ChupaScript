// C API boundary — context lifecycle, set functions, version.
//
// The public C header declares the full API surface; this translation unit
// implements the context lifecycle, variable setters, and version query.
// Compile/eval/run/error-reporting functions are filled in by Tasks 3–5.
#include "chupascript/chupascript.h"

#include <cstdlib>
#include <cstring>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ast.hpp"
#include "compile.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "eval.hpp"
#include "store.hpp"
#include "value.hpp"

// ─── Opaque struct definitions ───
// Defined here, not in the header: C doesn't see C++ members.

struct ChupaContext {
    CS::Store engine;

    // ╔════════════════════════════════════════════════════════════════════╗
    // ║ UAF-3 — ЗДЕСЬ КОРЕНЬ. std::vector<std::string> под сырым указателем ║
    // ╚════════════════════════════════════════════════════════════════════╝
    //
    // Каждый Ast хранит СЫРОЙ const char* на байты своего исходника
    // (ast.hpp:133, src_). Указатель берётся отсюда: sources.back().c_str().
    //
    // Но vector при росте ПЕРЕМЕЩАЕТ свои элементы в новый буфер.
    //   • Длинная строка (> 22 байт на libc++/x86-64): байты в куче,
    //     перемещение переносит только указатель — данные на месте, всё цело.
    //   • КОРОТКАЯ строка (SSO): байты лежат ВНУТРИ самого std::string,
    //     то есть внутри буфера вектора. Перемещение их ДВИГАЕТ.
    //     Все ранее выданные src_ повисают.
    //
    // Практически это значит: скомпилировал два выражения — первое сломано.
    //
    //     compile("a + b");   // src_ -> внутрь sources[0] (5 байт, SSO)
    //     compile("c * d");   // вектор вырос, sources[0] уехал
    //     eval(первое);       // ЧТЕНИЕ ОСВОБОЖДЁННОЙ ПАМЯТИ
    //
    // "a + b", "count(x)", "user.name" — всё это короче 23 байт. Это не
    // угловой случай, это обычный путь.
    //
    // Тесты молчат, потому что c_api_test.cpp компилирует и сразу вычисляет,
    // а уехавшая SSO-память ещё не отдана ОС и читается как валидная.
    // Видно только под ASan.
    //
    // Починка: см. план, шаг 1 — хранить std::unique_ptr<const char[]>,
    // чей адрес не зависит от роста вектора.
    std::vector<std::string> sources;                    // copied source texts
    std::vector<std::unique_ptr<CS::Ast>> asts;          // compiled trees
    std::vector<std::unique_ptr<ChupaExpression>> expressions;  // wrapper structs
    std::vector<std::unique_ptr<ChupaScript>> scripts;          // wrapper structs
    //
    // ┌─ УТЕЧКА (не UAF, но рядом) ────────────────────────────────────────┐
    // │ В эти четыре вектора только ДОБАВЛЯЮТ. В chupascript.h нет ни      │
    // │ chupa_expression_destroy, ни chupa_script_destroy — освободить     │
    // │ одно выражение нельзя, всё живёт до chupa_context_destroy.         │
    // │ Компиляция в цикле = неограниченный рост по построению.            │
    // │ Хуже: при ошибке компиляции sources.emplace_back уже случился и    │
    // │ назад не откатывается — каждый неудачный разбор оставляет мусор.   │
    // └────────────────────────────────────────────────────────────────────┘
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

// sourceIndex — временный костыль: Ast больше не держит исходник, а
// evalExpression/runScript требуют его параметром. Индекс указывает в
// ChupaContext::sources. Уйдёт вместе с самим sources в задаче 4.
struct ChupaExpression {
    CS::Ast* ast = nullptr;
    std::size_t sourceIndex = 0;
};

struct ChupaScript {
    CS::Ast* ast = nullptr;
    std::size_t sourceIndex = 0;
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
    // ⚠️⚠️⚠️ UAF-3 — ЗДЕСЬ ⚠️⚠️⚠️  (подробно: см. поле sources выше)
    //
    // Комментарий ниже — «Ast stores string_views into this buffer» — верен и
    // именно поэтому опасен: buffer УЕЗЖАЕТ при следующем emplace_back, если
    // строка попала в SSO. Второй compile ломает первый Ast.
    //
    // Copy source — Ast stores string_views into this buffer
    c->sources.emplace_back(source, len);
    const std::size_t sourceIndex = c->sources.size() - 1;
    const char* src = c->sources.back().c_str();   // ← этот адрес НЕ стабилен

    auto ast = std::make_unique<CS::Ast>();

    CS::Diagnostic diag;
    const std::uint32_t errors = CS::compileExpression(
        src, static_cast<std::uint32_t>(len), *ast, c->engine, &diag, 1);

    if (errors != 0) {
        c->setError(diag);
        return nullptr;
    }

    c->clearError();
    auto expr =
        std::make_unique<ChupaExpression>(ChupaExpression{ast.get(), sourceIndex});
    ChupaExpression* raw = expr.get();
    c->expressions.push_back(std::move(expr));
    c->asts.push_back(std::move(ast));
    return reinterpret_cast<ChupaExpression*>(raw);
}

ChupaScript* chupa_compile_script(ChupaContext* ctx,
                                  const char* source, size_t len) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    // ⚠️⚠️⚠️ UAF-3 — ЗДЕСЬ ТОЖЕ ⚠️⚠️⚠️  (то же самое, что в compile_expression)
    c->sources.emplace_back(source, len);
    const std::size_t sourceIndex = c->sources.size() - 1;
    const char* src = c->sources.back().c_str();   // ← этот адрес НЕ стабилен

    auto ast = std::make_unique<CS::Ast>();

    CS::Diagnostic diag;
    const std::uint32_t errors = CS::compileScript(
        src, static_cast<std::uint32_t>(len), *ast, c->engine, &diag, 1);

    if (errors != 0) {
        c->setError(diag);
        return nullptr;
    }

    c->clearError();
    auto script =
        std::make_unique<ChupaScript>(ChupaScript{ast.get(), sourceIndex});
    ChupaScript* raw = script.get();
    c->scripts.push_back(std::move(script));
    c->asts.push_back(std::move(ast));
    return reinterpret_cast<ChupaScript*>(raw);
}

// ─── Eval ───

ChupaStatus chupa_eval_number(ChupaContext* ctx, ChupaExpression* e,
                              double* out) {
    auto* c = reinterpret_cast<::ChupaContext*>(ctx);
    auto* expr = reinterpret_cast<::ChupaExpression*>(e);

    CS::Value value = CS::Value::null();
    CS::Diagnostic diag;
    if (!CS::evalExpression(*expr->ast, c->sources[expr->sourceIndex], c->engine,
                            &value, diag)) {
        c->setError(diag);
        return CHUPA_ERROR;
    }
    c->clearError();
    if (value.kind() == CS::Value::Kind::Null) {
        return CHUPA_NULL;
    }
    if (value.kind() != CS::Value::Kind::Number) {
        c->setError({CS::ErrorCode::Type, 0, "eval_number: value is not a number"});
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
    if (!CS::evalExpression(*expr->ast, c->sources[expr->sourceIndex], c->engine,
                            &value, diag)) {
        c->setError(diag);
        return CHUPA_ERROR;
    }
    c->clearError();
    if (value.kind() == CS::Value::Kind::Null) {
        return CHUPA_NULL;
    }
    if (value.kind() != CS::Value::Kind::Boolean) {
        c->setError({CS::ErrorCode::Type, 0, "eval_bool: value is not a boolean"});
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
    if (!CS::evalExpression(*expr->ast, c->sources[expr->sourceIndex], c->engine,
                            &value, diag)) {
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
    if (!CS::runScript(*s->ast, c->sources[s->sourceIndex], c->engine, diag)) {
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
