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
