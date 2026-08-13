#include <gtest/gtest.h>

#include "chupascript/chupascript.h"

#include <cstring>
#include <string>

// Helper: set a root from a ChupaScript literal text
bool setRoot(ChupaContext* ctx, const std::string& name, const std::string& text) {
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
    EXPECT_TRUE(setRoot(ctx, "name", "'hello'"));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralNumber) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "count", "42"));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralObject) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "user", "{ 'name': 'John', 'age': 30 }"));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralFailsOnExpression) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // "1 + 2" is an expression, not a literal — should fail
    EXPECT_FALSE(setRoot(ctx, "x", "1 + 2"));
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
    EXPECT_TRUE(setRoot(ctx, "x", "42"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    EXPECT_NE(e, nullptr);
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

TEST(CApiCompile, CompileExpressionFailsOnUnknownRoot) {
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
    EXPECT_TRUE(setRoot(ctx, "x", "42"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 42.0);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNumberFromExpression) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "10"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x + 5", 5);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 15.0);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalBool) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "10"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x > 5", 5);
    ASSERT_NE(e, nullptr);
    bool out = false;
    EXPECT_EQ(chupa_eval_bool(ctx, e, &out), CHUPA_OK);
    EXPECT_TRUE(out);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalString) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "name", "'hello'"));
    ChupaExpression* e = chupa_compile_expression(ctx, "name", 4);
    ASSERT_NE(e, nullptr);
    const char* out = nullptr;
    size_t len = 0;
    EXPECT_EQ(chupa_eval_string(ctx, e, &out, &len), CHUPA_OK);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(std::string(out, len), "hello");
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNullReturnsChupaNull) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "null"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_NULL);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNumberOnStringExpressionReturnsError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "name", "'hello'"));
    ChupaExpression* e = chupa_compile_expression(ctx, "name", 4);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_ERROR);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalMemberAccess) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "user", "{ 'name': 'John', 'age': 30 }"));
    ChupaExpression* e = chupa_compile_expression(ctx, "user.age", 8);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 30.0);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalTernary) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setRoot(ctx, "x", "5"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x > 3 ? 'big' : 'small'", 23);
    ASSERT_NE(e, nullptr);
    const char* out = nullptr;
    size_t len = 0;
    EXPECT_EQ(chupa_eval_string(ctx, e, &out, &len), CHUPA_OK);
    EXPECT_EQ(std::string(out, len), "big");
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
    chupa_context_destroy(ctx);
}

TEST(CApiEval, SetStringThenEval) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_set_string(ctx, "greeting", 8, "world", 5);
    ChupaExpression* e = chupa_compile_expression(ctx, "greeting", 8);
    ASSERT_NE(e, nullptr);
    const char* out = nullptr;
    size_t len = 0;
    EXPECT_EQ(chupa_eval_string(ctx, e, &out, &len), CHUPA_OK);
    EXPECT_EQ(std::string(out, len), "world");
    chupa_context_destroy(ctx);
}
