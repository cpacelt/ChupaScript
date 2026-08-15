#include <gtest/gtest.h>

#include "chupascript/chupascript.h"

#include <cstring>
#include <string>
#include <string_view>

// Helper: set a global from a ChupaScript literal text
bool setGlobal(ChupaContext* ctx, const std::string& name, const std::string& text) {
    return chupa_context_set(ctx, name.c_str(), name.size(), text.c_str(), text.size());
}

TEST(CApiContext, CreateDestroy) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralString) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "name", "'hello'"));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralNumber) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "count", "42"));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralObject) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "user", "{ 'name': 'John', 'age': 30 }"));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralFailsOnExpression) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // "1 + 2" is an expression, not a literal — should fail
    EXPECT_FALSE(setGlobal(ctx, "x", "1 + 2"));
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_DATA);
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetBool) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_bool(ctx, "flag", 4, true);
    // No return value to check — verify via eval in Task 3
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetNumber) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_number(ctx, "pi", 2, 3.14);
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetString) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_string(ctx, "greeting", 8, "world", 5);
    chupa_context_destroy(ctx);
}

TEST(CApiContext, DestroyNullIsSafe) {
    chupa_context_destroy(nullptr);
}

TEST(CApiContext, VersionIsReported) {
    EXPECT_STREQ("0.1.0", chupa_version());
}

// ─── Compile + Eval ───

TEST(CApiCompile, CompileExpression) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "42"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    EXPECT_NE(e, nullptr);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiCompile, CompileExpressionFailsOnSyntaxError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ChupaExpression* e = chupa_compile_expression(ctx, "1 +", 3);
    EXPECT_EQ(e, nullptr);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_SYNTAX);
    chupa_context_destroy(ctx);
}

TEST(CApiCompile, CompileExpressionFailsOnUnknownGlobal) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ChupaExpression* e = chupa_compile_expression(ctx, "unknown_var", 11);
    EXPECT_EQ(e, nullptr);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_NAME);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNumber) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "42"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 42.0);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNumberFromExpression) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "10"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x + 5", 5);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 15.0);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalBool) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "10"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x > 5", 5);
    ASSERT_NE(e, nullptr);
    bool out = false;
    EXPECT_EQ(chupa_eval_bool(ctx, e, &out), CHUPA_OK);
    EXPECT_TRUE(out);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalString) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "name", "'hello'"));
    ChupaExpression* e = chupa_compile_expression(ctx, "name", 4);
    ASSERT_NE(e, nullptr);
    ChupaString* out = nullptr;
    EXPECT_EQ(chupa_eval_string(ctx, e, &out), CHUPA_OK);
    ASSERT_NE(out, nullptr);
    size_t len = 0;
    const char* bytes = chupa_string_bytes(out, &len);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(std::string(bytes, len), "hello");
    chupa_string_destroy(out);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNullReturnsChupaNull) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "null"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_NULL);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNumberOnStringExpressionReturnsError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "name", "'hello'"));
    ChupaExpression* e = chupa_compile_expression(ctx, "name", 4);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_ERROR);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_TYPE);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalMemberAccess) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "user", "{ 'name': 'John', 'age': 30 }"));
    ChupaExpression* e = chupa_compile_expression(ctx, "user.age", 8);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 30.0);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalTernary) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "5"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x > 3 ? 'big' : 'small'", 23);
    ASSERT_NE(e, nullptr);
    ChupaString* out = nullptr;
    EXPECT_EQ(chupa_eval_string(ctx, e, &out), CHUPA_OK);
    ASSERT_NE(out, nullptr);
    size_t len = 0;
    const char* bytes = chupa_string_bytes(out, &len);
    EXPECT_EQ(std::string(bytes, len), "big");
    chupa_string_destroy(out);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, SetBoolThenEval) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_bool(ctx, "flag", 4, true);
    ChupaExpression* e = chupa_compile_expression(ctx, "flag", 4);
    ASSERT_NE(e, nullptr);
    bool out = false;
    EXPECT_EQ(chupa_eval_bool(ctx, e, &out), CHUPA_OK);
    EXPECT_TRUE(out);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, SetNumberThenEval) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_number(ctx, "pi", 2, 3.14);
    ChupaExpression* e = chupa_compile_expression(ctx, "pi", 2);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 3.14);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, SetStringThenEval) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_string(ctx, "greeting", 8, "world", 5);
    ChupaExpression* e = chupa_compile_expression(ctx, "greeting", 8);
    ASSERT_NE(e, nullptr);
    ChupaString* out = nullptr;
    EXPECT_EQ(chupa_eval_string(ctx, e, &out), CHUPA_OK);
    ASSERT_NE(out, nullptr);
    size_t len = 0;
    const char* bytes = chupa_string_bytes(out, &len);
    EXPECT_EQ(std::string(bytes, len), "world");
    chupa_string_destroy(out);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

// ─── ChupaString: строка во владении хоста ───

TEST(CApi, EvalStringHandsOverOwnership) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    std::string_view src = "'привет'";
    ChupaExpression* e = chupa_compile_expression(ctx, src.data(), src.size());
    ASSERT_NE(e, nullptr);

    ChupaString* s = nullptr;
    ASSERT_EQ(chupa_eval_string(ctx, e, &s), CHUPA_OK);
    ASSERT_NE(s, nullptr);

    size_t len = 0;
    const char* bytes = chupa_string_bytes(s, &len);
    EXPECT_EQ(std::string(bytes, len), "привет");

    chupa_string_destroy(s);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, EvalStringSurvivesStoreMutation) {
    // Это и есть UAF-1: раньше указатель смотрел внутрь пула движка, и любая
    // следующая операция над контекстом могла его подвесить.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    std::string_view src = "'привет'";
    ChupaExpression* e = chupa_compile_expression(ctx, src.data(), src.size());
    ASSERT_NE(e, nullptr);

    ChupaString* s = nullptr;
    ASSERT_EQ(chupa_eval_string(ctx, e, &s), CHUPA_OK);
    ASSERT_NE(s, nullptr);

    // Растим пул текста так, чтобы он заведомо переехал.
    std::string_view name = "filler";
    std::string_view filler = "довольно длинная строка для роста пула";
    for (int i = 0; i < 1000; ++i) {
        chupa_context_set_string(ctx, name.data(), name.size(),
                                 filler.data(), filler.size());
    }

    size_t len = 0;
    const char* bytes = chupa_string_bytes(s, &len);
    EXPECT_EQ(std::string(bytes, len), "привет");  // байты наши, не движка

    chupa_string_destroy(s);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, EvalStringOnNullLeavesOutUntouched) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ChupaExpression* e = chupa_compile_expression(ctx, "null", 4);
    ASSERT_NE(e, nullptr);

    ChupaString* s = nullptr;
    EXPECT_EQ(chupa_eval_string(ctx, e, &s), CHUPA_NULL);
    EXPECT_EQ(s, nullptr);  // отдавать нечего — и не отдано

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, EvalStringOnNumberIsTypeError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // Корень намеренно НЕ в первом байте: раньше прокладка ставила в ошибку
    // типа хардкод offset = 0 и на выражении вида "42" разницы было бы не
    // видно. Теперь смещение ставит ядро — и оно настоящее.
    std::string_view src = "1 + 41";
    ChupaExpression* e = chupa_compile_expression(ctx, src.data(), src.size());
    ASSERT_NE(e, nullptr);

    ChupaString* s = nullptr;
    EXPECT_EQ(chupa_eval_string(ctx, e, &s), CHUPA_ERROR);
    EXPECT_EQ(s, nullptr);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_TYPE);
    EXPECT_EQ(chupa_context_error_offset(ctx), 2u);  // '+', корень выражения

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, EvalStringOutlivesTheContext) {
    // Порядок разрушения свободный: строка владеет своими байтами и на
    // контекст не ссылается. Заголовок это обещает — тест это держит.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    std::string_view src = "'привет'";
    ChupaExpression* e = chupa_compile_expression(ctx, src.data(), src.size());
    ASSERT_NE(e, nullptr);

    ChupaString* s = nullptr;
    ASSERT_EQ(chupa_eval_string(ctx, e, &s), CHUPA_OK);
    ASSERT_NE(s, nullptr);

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);  // контекст умер РАНЬШЕ строки

    size_t len = 0;
    const char* bytes = chupa_string_bytes(s, &len);
    EXPECT_EQ(std::string(bytes, len), "привет");
    chupa_string_destroy(s);
}

TEST(CApi, EvalStringOnEmptyStringIsOkAndNotNull) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    std::string_view src = "''";
    ChupaExpression* e = chupa_compile_expression(ctx, src.data(), src.size());
    ASSERT_NE(e, nullptr);

    ChupaString* s = nullptr;
    ASSERT_EQ(chupa_eval_string(ctx, e, &s), CHUPA_OK);  // пустая — не null
    ASSERT_NE(s, nullptr);

    size_t len = 1;
    const char* bytes = chupa_string_bytes(s, &len);
    EXPECT_NE(bytes, nullptr);  // ноль байт — но указатель всё равно есть
    EXPECT_EQ(len, 0u);

    chupa_string_destroy(s);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, StringDestroyAcceptsNull) {
    chupa_string_destroy(nullptr);
}

TEST(CApi, StringBytesAcceptsNullLength) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    std::string_view src = "'ok'";
    ChupaExpression* e = chupa_compile_expression(ctx, src.data(), src.size());
    ASSERT_NE(e, nullptr);
    ChupaString* s = nullptr;
    ASSERT_EQ(chupa_eval_string(ctx, e, &s), CHUPA_OK);
    ASSERT_NE(s, nullptr);
    size_t len = 0;
    const char* with_len = chupa_string_bytes(s, &len);
    EXPECT_EQ(std::string_view(with_len, len), "ok");
    // len == nullptr принимается, и байты те же самые.
    EXPECT_EQ(std::string_view(chupa_string_bytes(s, nullptr), len), "ok");
    chupa_string_destroy(s);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

// ─── Run ───

TEST(CApiRun, RunScriptSetsVariable) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "state", "{ 'count': 0 }"));
    ChupaScript* s = chupa_compile_script(ctx, "state.count = 42;", 17);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(chupa_run(ctx, s));
    // Verify via eval
    ChupaExpression* e = chupa_compile_expression(ctx, "state.count", 11);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 42.0);
    chupa_expression_destroy(e);
    chupa_script_destroy(s);
    chupa_context_destroy(ctx);
}

TEST(CApiRun, RunScriptWithMemberAccess) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "user", "{ 'name': 'old' }"));
    ChupaScript* s = chupa_compile_script(ctx, "user.name = 'new';", 18);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(chupa_run(ctx, s));
    ChupaExpression* e = chupa_compile_expression(ctx, "user.name", 9);
    ASSERT_NE(e, nullptr);
    ChupaString* out = nullptr;
    EXPECT_EQ(chupa_eval_string(ctx, e, &out), CHUPA_OK);
    ASSERT_NE(out, nullptr);
    size_t len = 0;
    const char* bytes = chupa_string_bytes(out, &len);
    EXPECT_EQ(std::string(bytes, len), "new");
    chupa_string_destroy(out);
    chupa_expression_destroy(e);
    chupa_script_destroy(s);
    chupa_context_destroy(ctx);
}

TEST(CApiRun, RunScriptFailsOnTypeError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "0"));
    // x is a number, can't assign a string member to it
    ChupaScript* s = chupa_compile_script(ctx, "x.name = 'bad';", 15);
    ASSERT_NE(s, nullptr);
    EXPECT_FALSE(chupa_run(ctx, s));
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_TYPE);
    chupa_script_destroy(s);
    chupa_context_destroy(ctx);
}

// ─── Error accessors ───

TEST(CApiError, NoErrorAfterSuccess) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "42"));
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_NONE);
    chupa_context_destroy(ctx);
}

TEST(CApiError, SyntaxErrorDetails) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_compile_expression(ctx, "1 +", 3);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_SYNTAX);
    size_t len = 0;
    const char* msg = chupa_context_error(ctx, &len);
    ASSERT_NE(msg, nullptr);
    EXPECT_GT(len, 0u);
    // Offset should point somewhere in the source
    EXPECT_GE(chupa_context_error_offset(ctx), 0u);
    chupa_context_destroy(ctx);
}

TEST(CApiError, DataErrorOnExpressionAsData) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // "1 + 2" is an expression, not a literal — setVariable rejects it
    chupa_context_set(ctx, "x", 1, "1 + 2", 5);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_DATA);
    chupa_context_destroy(ctx);
}

// ─── Redraw ───

namespace {
int g_redrawCount = 0;
ChupaContext* g_lastRedrawCtx = nullptr;

void testRedrawListener(ChupaContext* ctx, void* /*user_data*/) {
    g_redrawCount++;
    g_lastRedrawCtx = ctx;
}
}

TEST(CApiRedraw, FiresAfterSet) {
    g_redrawCount = 0;
    g_lastRedrawCtx = nullptr;
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_on_redraw(ctx, testRedrawListener, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "42"));
    EXPECT_EQ(g_redrawCount, 1);
    EXPECT_EQ(g_lastRedrawCtx, ctx);
    chupa_context_destroy(ctx);
}

TEST(CApiRedraw, FiresAfterRun) {
    g_redrawCount = 0;
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "state", "{ 'count': 0 }"));
    // set already fired redraw; reset counter
    g_redrawCount = 0;
    chupa_context_on_redraw(ctx, testRedrawListener, nullptr);
    ChupaScript* s = chupa_compile_script(ctx, "state.count = 1;", 16);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(chupa_run(ctx, s));
    EXPECT_EQ(g_redrawCount, 1);
    chupa_script_destroy(s);
    chupa_context_destroy(ctx);
}

TEST(CApiRedraw, NoFireWithoutListener) {
    g_redrawCount = 0;
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "42"));
    EXPECT_EQ(g_redrawCount, 0);
    chupa_context_destroy(ctx);
}

TEST(CApiRedraw, UserDataPassedThrough) {
    g_redrawCount = 0;
    int marker = 42;
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_on_redraw(ctx, [](ChupaContext* /*ctx*/, void* user_data) {
        g_redrawCount++;
        EXPECT_EQ(*static_cast<int*>(user_data), 42);
    }, &marker);
    chupa_context_set_bool(ctx, "flag", 4, true);
    EXPECT_EQ(g_redrawCount, 1);
    chupa_context_destroy(ctx);
}

// ─── Ownership of compiled units ───

TEST(CApi, SecondCompileDoesNotBreakTheFirst) {
    // Ровно тот сценарий, на котором ломался UAF-3 (B39): оба исходника
    // короче 23 байт, то есть оба попадали в SSO.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(chupa_context_set(ctx, "a", 1, "2", 1));
    ASSERT_TRUE(chupa_context_set(ctx, "b", 1, "3", 1));

    ChupaExpression* first = chupa_compile_expression(ctx, "a + b", 5);
    ASSERT_NE(first, nullptr);
    ChupaExpression* second = chupa_compile_expression(ctx, "a * b", 5);
    ASSERT_NE(second, nullptr);

    double out = 0.0;
    EXPECT_EQ(chupa_eval_number(ctx, first, &out), CHUPA_OK);
    EXPECT_DOUBLE_EQ(out, 5.0);

    chupa_expression_destroy(first);
    chupa_expression_destroy(second);
    chupa_context_destroy(ctx);
}

TEST(CApi, ExpressionOutlivesNothingAndIsFreedByHost) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // Компиляция в цикле больше не растит контекст по построению.
    for (int i = 0; i < 1000; ++i) {
        ChupaExpression* e = chupa_compile_expression(ctx, "1 + 1", 5);
        ASSERT_NE(e, nullptr);
        chupa_expression_destroy(e);
    }
    chupa_context_destroy(ctx);
}

TEST(CApi, FailedCompileLeavesNothingBehind) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(chupa_compile_expression(ctx, "a..b", 4), nullptr);
    }
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_SYNTAX);
    chupa_context_destroy(ctx);
}

TEST(CApi, DestroyAcceptsNull) {
    chupa_expression_destroy(nullptr);
    chupa_script_destroy(nullptr);
}

TEST(CApi, UnitOutlivesTheContextItWasCompiledAgainst) {
    // chupascript.h обещает наружу: «A unit may be destroyed in any order
    // relative to the context it was compiled against». Здесь порядок
    // обратный обычному — сначала умирает контекст.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(chupa_context_set(ctx, "obj", 3, "{ 'n': 1 }", 10));

    ChupaExpression* e = chupa_compile_expression(ctx, "obj.n + 1", 9);
    ASSERT_NE(e, nullptr);
    ChupaScript* s = chupa_compile_script(ctx, "obj.n = 7;", 10);
    ASSERT_NE(s, nullptr);

    chupa_context_destroy(ctx);
    chupa_expression_destroy(e);
    chupa_script_destroy(s);
}

TEST(CApi, ScriptIsOwnedByHost) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // Цель присваивания обязана быть Member или Index (docs/semantics.md
    // §7.2), голый идентификатор язык отвергает — отсюда объект.
    ASSERT_TRUE(chupa_context_set(ctx, "obj", 3, "{ 'n': 1 }", 10));

    ChupaScript* s = chupa_compile_script(ctx, "obj.n = obj.n + 1;", 18);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(chupa_run(ctx, s));
    chupa_script_destroy(s);

    ChupaExpression* e = chupa_compile_expression(ctx, "obj.n", 5);
    ASSERT_NE(e, nullptr);
    double out = 0.0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_DOUBLE_EQ(out, 2.0);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}
