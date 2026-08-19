#include "context.hpp"

#include "box.hpp"

#include <gtest/gtest.h>

#include <cstddef>

#include "data.hpp"
#include "aggregate.hpp"

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
    CS::Deferred dead;
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(ctx.store(), dead, "user", "{'name': 'Вася'}", diag));

    const CS::Expression expr = compileIn(ctx, "user.name");
    CS::Value out = CS::Value::null();
    ASSERT_TRUE(ctx.eval(expr, &out, diag)) << diag.message;

    // Существенно не «вычислилось», а «вычислилось в том же хранилище, что
    // отдаёт store()»: значение это индекс в пулы, и из чужого хранилища оно
    // указывало бы не туда.
    EXPECT_EQ(ctx.store().string(out), "Вася");
}

TEST(Context, ScriptChangesAreVisibleThroughTheStore) {
    CS::Deferred dead;
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(ctx.store(), dead, "state", "{'count': 1}", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Script::compile("state.count = 2;", ctx.store(), &script,
                                  diags, 1),
              0u)
        << diags[0].message;
    ASSERT_TRUE(ctx.run(script, diag)) << diag.message;

    const CS::Value state = ctx.store().global("state");
    EXPECT_DOUBLE_EQ(CS::objectGet(state, "count").numberValue(), 2.0);
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
    CS::Deferred dead;
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(ctx.store(), dead, "state", "{'items': [1]}", diag));

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

TEST(Context, TemporaryRegionDoesNotGrowAcrossOperations) {
    CS::Context ctx;
    CS::Diagnostic diag;

    // Склейка строит новые байты на каждом вычислении, поэтому без сброса на
    // границе операции временный регион рос бы линейно от числа вычислений —
    // ровно то, что [B57] и убирает.
    //
    // Раньше здесь стоял литерал массива. Он больше не годится: агрегат теперь
    // коробка со счётчиком, во временном регионе её не бывает, и байт она туда не
    // кладёт. Мерить рост арены надо тем, что в ней правда живёт, — строкой.
    const CS::Expression expr = compileIn(ctx, "format('${}${}', 1, 2)");

    CS::Value out = CS::Value::null();
    ASSERT_TRUE(ctx.eval(expr, &out, diag)) << diag.message;
    const std::size_t afterFirst = ctx.temporaryBytesUsed();
    ASSERT_GT(afterFirst, 0u) << "вычисление ничего не положило во временный "
                                 "регион — тест перестал что-либо проверять";

    for (int i = 0; i < 16; ++i) {
        ASSERT_TRUE(ctx.eval(expr, &out, diag)) << diag.message;
        EXPECT_EQ(ctx.temporaryBytesUsed(), afterFirst);
    }
}

TEST(Context, ScriptAlsoOpensAnOperation) {
    CS::Deferred dead;
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(ctx.store(), dead, "state", "{'n': 0}", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    // Строка, а не агрегат: во временном регионе теперь живут только байты.
    ASSERT_EQ(CS::Script::compile("state.n = format('${}${}', 1, 2);", ctx.store(),
                                  &script, diags, 1),
              0u)
        << diags[0].message;

    ASSERT_TRUE(ctx.run(script, diag)) << diag.message;
    const std::size_t afterFirst = ctx.temporaryBytesUsed();
    ASSERT_GT(afterFirst, 0u) << "скрипт ничего не положил во временный регион "
                                 "— тест перестал что-либо проверять";
    for (int i = 0; i < 16; ++i) {
        ASSERT_TRUE(ctx.run(script, diag)) << diag.message;
        EXPECT_EQ(ctx.temporaryBytesUsed(), afterFirst);
    }
}

#ifndef NDEBUG
TEST(ContextMemory, RewrittenGlobalDoesNotGrowForever) {
    // Присваивать переменную целиком язык не даёт (check.cpp: «cannot assign
    // to a variable name»), так что переписывает её только хост — и это
    // основной случай: бэкенд шлёт новые данные на каждое обновление экрана.
    //
    // Раньше прежний массив оставался в пуле навсегда, и двести обновлений
    // держали двести массивов. Меряется счётчиком живых коробок: память коробки
    // хранилищу не принадлежит, и его метрика байт её не видит.
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("rows", "[1, 2, 3]", diag)) << diag.message;

    const std::size_t afterFirst = CS::detail::liveBoxCount();
    for (int i = 0; i < 200; ++i) {
        ASSERT_TRUE(ctx.setVariableText("rows", "[1, 2, 3]", diag)) << diag.message;
    }
    // Живым остаётся ровно последний массив; допуск — на тот, чью ссылку ещё
    // держит список отложенного освобождения до ближайшей границы.
    EXPECT_LE(CS::detail::liveBoxCount(), afterFirst + 1);
}
#endif

#ifndef NDEBUG
TEST(ContextMemory, PushInALoopDoesNotLeaveGarbage) {
    CS::Deferred dead;
    // Единственный способ вырастить массив в языке. Раньше каждый push
    // переносил его в хвост пула, бросая прежний диапазон мусором.
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(ctx.store(), dead, "rows", "[]", diag)) << diag.message;

    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Script::compile("push(rows, 1);", ctx.store(), &script, diags, 1), 0u)
        << diags[0].message;

    const std::size_t before = CS::detail::liveBoxCount();
    for (int i = 0; i < 200; ++i) { ASSERT_TRUE(ctx.run(script, diag)) << diag.message; }

    const CS::Expression expr = compileIn(ctx, "count(rows)");
    double got = 0.0;
    ASSERT_EQ(ctx.evalNumber(expr, &got, diag), CS::EvalStatus::Ok) << diag.message;
    EXPECT_EQ(got, 200.0);
    // Двести чисел не завели ни одного коробки: скаляр живёт в самом Value.
    EXPECT_EQ(CS::detail::liveBoxCount(), before);
}
#endif

TEST(ContextMemory, ArrayHandedOutOutlivesTheContext) {
    CS::Deferred dead;
    // То, ради чего всё это: значение, отданное наружу, не зависит от того,
    // жив контекст или нет. Хост берёт ссылку и отпускает её сам.
    CS::Value escaped = CS::Value::null();
    {
        CS::Context ctx;
        CS::Diagnostic diag;
        ASSERT_TRUE(CS::setVariable(ctx.store(), dead, "rows", "[1, 2, 3]", diag))
            << diag.message;

        const CS::Expression expr = compileIn(ctx, "rows");
        ASSERT_TRUE(ctx.eval(expr, &escaped, diag)) << diag.message;
        ASSERT_EQ(escaped.kind(), CS::Value::Kind::Array);
        CS::detail::retain(escaped.box());   // так делает обёртка хоста
    }
    // Контекста нет, хранилища нет, таблицы имён у него нет. Массив есть.
    const CS::detail::ArrayBox *node =
        static_cast<const CS::detail::ArrayBox *>(escaped.box());
    ASSERT_EQ(node->items.size(), 3u);
    EXPECT_EQ(node->items[2].numberValue(), 3.0);
    CS::detail::release(escaped.box());
}

TEST(ContextMemory, ObjectHandedOutKeepsItsKeysPastTheContext) {
    CS::Deferred dead;
    CS::Value escaped = CS::Value::null();
    {
        CS::Context ctx;
        CS::Diagnostic diag;
        ASSERT_TRUE(CS::setVariable(ctx.store(), dead, "user", "{'name': 'Вася'}", diag))
            << diag.message;
        const CS::Expression expr = compileIn(ctx, "user");
        ASSERT_TRUE(ctx.eval(expr, &escaped, diag)) << diag.message;
        CS::detail::retain(escaped.box());
    }
    const CS::detail::ObjectBox *node =
        static_cast<const CS::detail::ObjectBox *>(escaped.box());
    ASSERT_EQ(node->entries.size(), 1u);
    // Таблица имён пережила своё хранилище, потому что её держит коробка.
    EXPECT_EQ(node->keys->bytes(node->entries[0].key), "name");
    CS::detail::release(escaped.box());
}

TEST(ContextMemory, StringPushedIntoGlobalArraySurvivesTheOperation) {
    CS::Deferred dead;
    // Строка собирается в арене операции, а границу переживает коробкой — ровно
    // то правило, ради которого укладка в агрегат материализует строку.
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(ctx.store(), dead, "rows", "[]", diag)) << diag.message;

    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Script::compile("push(rows, format('${}${}', 1, 2));", ctx.store(),
                                  &script, diags, 1),
              0u)
        << diags[0].message;
    ASSERT_TRUE(ctx.run(script, diag)) << diag.message;

    const CS::Expression expr = compileIn(ctx, "rows[0]");
    std::string_view got;
    ASSERT_EQ(ctx.evalString(expr, &got, diag), CS::EvalStatus::Ok) << diag.message;
    EXPECT_EQ(got, "12");
}

}  // namespace
