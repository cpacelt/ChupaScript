#include "context.hpp"

#include <gtest/gtest.h>

#include "data.hpp"

namespace {

/// Компилирует выражение в хранилище контекста; требует успеха.
CS::Expression compileIn(CS::Context &ctx, std::string_view source) {
    CS::Expression expr;
    CS::Diagnostic diags[1];
    EXPECT_EQ(CS::Expression::compile(source, ctx.store(), &expr, diags, 1), 0u)
        << diags[0].message;
    return expr;
}

TEST(Context, EvaluatesInTheStoreItHandsOut) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(ctx.store(), "user", "{'name': 'Вася'}", diag));

    const CS::Expression expr = compileIn(ctx, "user.name");
    CS::Value out = CS::Value::null();
    ASSERT_TRUE(ctx.eval(expr, &out, diag)) << diag.message;

    // Существенно не «вычислилось», а «вычислилось в том же хранилище, что
    // отдаёт store()»: значение это индекс в пулы, и из чужого хранилища оно
    // указывало бы не туда.
    EXPECT_EQ(ctx.store().string(out), "Вася");
}

TEST(Context, ScriptChangesAreVisibleThroughTheStore) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(ctx.store(), "state", "{'count': 1}", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Script::compile("state.count = 2;", ctx.store(), &script,
                                  diags, 1),
              0u)
        << diags[0].message;
    ASSERT_TRUE(ctx.run(script, diag)) << diag.message;

    const CS::Value state = ctx.store().global("state");
    EXPECT_DOUBLE_EQ(ctx.store().objectGet(state, "count").numberValue(), 2.0);
}

TEST(Context, TypedEvalsReachTheSameExpression) {
    CS::Context ctx;
    CS::Diagnostic diag;

    double number = 0.0;
    const CS::Expression n = compileIn(ctx, "1 + 2");
    EXPECT_EQ(ctx.evalNumber(n, &number, diag), CS::EvalStatus::Ok);
    EXPECT_DOUBLE_EQ(number, 3.0);

    bool flag = false;
    const CS::Expression b = compileIn(ctx, "1 < 2");
    EXPECT_EQ(ctx.evalBool(b, &flag, diag), CS::EvalStatus::Ok);
    EXPECT_TRUE(flag);

    std::string_view text;
    const CS::Expression s = compileIn(ctx, "'привет'");
    EXPECT_EQ(ctx.evalString(s, &text, diag), CS::EvalStatus::Ok);
    EXPECT_EQ(text, "привет");
}

TEST(Context, ReportsEvaluationFailure) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(ctx.store(), "state", "{'items': [1]}", diag));

    // Отрицательный индекс: разбор и проверка проходят, падает вычисление —
    // то есть diag заполняет именно этот путь. Ни отсутствующий ключ, ни
    // индекс за концом для этого не годятся: оба дают null, а не ошибку.
    const CS::Expression expr = compileIn(ctx, "state.items[0 - 1]");
    CS::Value out = CS::Value::number(42.0);
    EXPECT_FALSE(ctx.eval(expr, &out, diag));
    EXPECT_NE(diag.code, CS::ErrorCode::None);

    // При отказе выходной параметр не трогается — соглашение Expression::eval
    // проходит через Context насквозь.
    EXPECT_DOUBLE_EQ(out.numberValue(), 42.0);
}

}  // namespace
