#include "data.hpp"

#include <gtest/gtest.h>

#include "context.hpp"
#include "diagnostic.hpp"

namespace {

using CS::Context;
using CS::Diagnostic;
using CS::ErrorCode;
using CS::Value;

/// Кладёт значение и требует успеха; возвращает то, что легло.
Value put(Context &ctx, std::string_view name, std::string_view text) {
    Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(ctx, name, text, diag)) << diag.message;
    return ctx.root(name);
}

TEST(DataScalars, NumberIsStored) {
    Context ctx;
    EXPECT_EQ(put(ctx, "count", "3").numberValue(), 3.0);
    EXPECT_EQ(put(ctx, "ratio", "0.5").numberValue(), 0.5);
}

TEST(DataScalars, BooleanIsStored) {
    Context ctx;
    EXPECT_TRUE(put(ctx, "on", "true").booleanValue());
    EXPECT_FALSE(put(ctx, "off", "false").booleanValue());
}

TEST(DataScalars, NullIsStored) {
    Context ctx;
    EXPECT_EQ(put(ctx, "nothing", "null").kind(), Value::Kind::Null);
    EXPECT_TRUE(ctx.hasRoot("nothing"));
}

TEST(DataNames, IdentifierIsAccepted) {
    Context ctx;
    Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(ctx, "user_2", "1", diag));
    EXPECT_TRUE(CS::setVariable(ctx, "_private", "1", diag));
}

TEST(DataNames, NonIdentifierIsRejected) {
    Context ctx;
    Diagnostic diag;
    // Корень, который программа не может написать, бесполезен.
    EXPECT_FALSE(CS::setVariable(ctx, "content-type", "1", diag));
    EXPECT_EQ(diag.code, ErrorCode::Name);
    EXPECT_FALSE(CS::setVariable(ctx, "2fa", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, "", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, " state", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, "state ", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, "имя", "1", diag));
    EXPECT_EQ(ctx.rootCount(), 0u);
}

TEST(DataNames, ReservedWordIsRejected) {
    Context ctx;
    Diagnostic diag;
    // docs/grammar.md §4.5: ключевое слово идентификатором не является.
    EXPECT_FALSE(CS::setVariable(ctx, "null", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, "true", "1", diag));
    EXPECT_FALSE(CS::setVariable(ctx, "while", "1", diag));
    EXPECT_EQ(ctx.rootCount(), 0u);
}

TEST(DataFailure, SyntaxErrorLeavesNoRoot) {
    Context ctx;
    Diagnostic diag;
    EXPECT_FALSE(CS::setVariable(ctx, "broken", "3 3", diag));
    EXPECT_EQ(diag.code, ErrorCode::Syntax);
    EXPECT_FALSE(ctx.hasRoot("broken"));
    EXPECT_EQ(ctx.rootCount(), 0u);
}

}  // namespace
