// C API boundary — context lifecycle, set functions, version.
//
// The public C header declares the full API surface; this translation unit
// implements the context lifecycle, variable setters, and version query.
// Compile/eval/run/error-reporting functions are filled in by Tasks 3–5.
#include "chupascript/chupascript.h"

#include <cstdlib>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "ast.hpp"
#include "compile.hpp"
#include "context.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "eval.hpp"
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

// ─── Error reporting (partial — full suite in Task 4) ───

ChupaErrorCode chupa_context_error_code(const ChupaContext* ctx) {
    if (!ctx) { return CHUPA_ERR_NONE; }
    const auto* c = reinterpret_cast<const ::ChupaContext*>(ctx);
    return static_cast<ChupaErrorCode>(c->lastError.code);
}

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
    return reinterpret_cast<ChupaExpression*>(expr);
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
    return reinterpret_cast<ChupaScript*>(script);
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
