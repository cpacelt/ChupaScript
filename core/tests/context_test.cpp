#include "context.hpp"

#include "box.hpp"

#include <gtest/gtest.h>

#include <cstddef>
#include <type_traits>

#include "data.hpp"
#include "aggregate.hpp"
#include "execution.hpp"
#include "host_fixture.hpp"

namespace {

/// Компилирует выражение в хранилище контекста; требует успеха.
CS::Expression compileIn(CS::Context &ctx, std::string_view source) {
    CS::Expression expr;
    CS::Diagnostic diags[1];
    EXPECT_EQ(ctx.compileExpression(source, &expr, diags, 1), 0u)
        << diags[0].message;
    return expr;
}

// Context::store() must hand out a read-only view: nothing outside the door
// methods (setVariableText, compileExpression, compileScript) may mutate the
// Store a Context owns.
TEST(Context, HandsOutItsStoreForReadingOnly) {
    static_assert(
        std::is_const_v<std::remove_reference_t<decltype(std::declval<CS::Context &>().store())>>,
        "Context::store() must not hand out a mutable Store");
}

// A Context compiles against its own Store without exposing a mutable
// reference to it: the door is compileExpression, not store().
TEST(Context, CompilesAgainstItsOwnStore) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("x", "41", diag));
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(ctx.compileExpression("x + 1", &expr, diags, 1), 0u) << diags[0].message;
    double out = 0.0;
    ASSERT_EQ(ctx.evalNumber(expr, &out, diag), CS::EvalStatus::Ok);
    EXPECT_EQ(out, 42.0);
}

TEST(Context, EvaluatesInTheStoreItHandsOut) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("user", "{'name': 'Вася'}", diag));

    const CS::Expression expr = compileIn(ctx, "user.name");
    CS::Value out = CS::Value::null();
    ASSERT_TRUE(ctx.eval(expr, &out, diag)) << diag.message;

    // Существенно не «вычислилось», а «вычислилось в том же хранилище, что
    // отдаёт store()»: значение это индекс в пулы, и из чужого хранилища оно
    // указывало бы не туда.
    EXPECT_EQ(CS::stringBytes(out), "Вася");
}

TEST(Context, ScriptChangesAreVisibleThroughTheStore) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("state", "{'count': 1}", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(ctx.compileScript("state.count = 2;", &script, diags, 1), 0u)
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

    CS::Value text = CS::Value::null();
    const CS::Expression s = compileIn(ctx, "'привет'");
    EXPECT_TRUE(ctx.eval(s, &text, diag));
    EXPECT_EQ(CS::stringBytes(text), "привет");
}

TEST(Context, ReportsEvaluationFailure) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("state", "{'items': [1]}", diag));

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

// Context::evalTracked проходит границу операции так же, как Context::eval:
// та же охрана реентрантности (EvaluationGuard) и тот же beginOperation
// (drain отложенного списка, сброс lastResult_) — оба door-метода зовут один
// и тот же приватный beginOperation, и это не совпадение реализации, а
// требование (LAYOUT: "операция, а не голая запись").
TEST(Context, EvalTrackedReachesTheSameExpressionAndDeps) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("a", "1", diag));

    const CS::Expression expr = compileIn(ctx, "a");

    CS::Value out = CS::Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    ASSERT_TRUE(ctx.evalTracked(expr, &out, deps, &n, diag));

    EXPECT_DOUBLE_EQ(out.numberValue(), 1.0);
    ASSERT_EQ(n, 1u);
    EXPECT_NE(deps[0].epoch, &CS::kZeroEpoch);
    for (std::uint32_t i = 1; i < CS::kMaxDeps; ++i) {
        EXPECT_EQ(deps[i].epoch, &CS::kZeroEpoch);
    }
}

/// Признак реентрантности снимается и на отказе evalTracked — тот же путь,
/// что EvaluatingFlagClearsAfterFailure проверяет у Context::eval. При отказе
/// набор отравлен nullptr'ами насквозь через дверь Context, а не только у
/// Expression::evalTracked напрямую.
TEST(Context, EvalTrackedClearsTheEvaluatingFlagAfterFailure) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("n", "1", diag));

    const CS::Expression expr = compileIn(ctx, "n.field");

    CS::Value out = CS::Value::boolean(true);
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    EXPECT_FALSE(ctx.evalTracked(expr, &out, deps, &n, diag));

    EXPECT_FALSE(ctx.isEvaluating());
    EXPECT_EQ(n, CS::kDepsOverflow);
    for (const CS::Dep &dep : deps) { EXPECT_EQ(dep.epoch, nullptr); }
    EXPECT_EQ(out.kind(), CS::Value::Kind::Boolean) << "при отказе *out не трогается";
}

/// A computed string handed straight back into a global variable keeps its
/// bytes. This is defect Б1 from the design document: setGlobal opened the
/// operation boundary, which cleared the arena, and only then promoted the
/// value out of that same arena — yielding an empty slice that no assert
/// caught, because the promoted box was a genuine box, merely an empty one.
TEST(Context, ComputedStringSurvivesBeingStoredInAGlobal) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("who", "'Вася'", diag));

    const CS::Expression expr = compileIn(ctx, "format('привет, ${}', who)");

    CS::Value computed = CS::Value::null();
    ASSERT_TRUE(ctx.eval(expr, &computed, diag)) << diag.message;
    ASSERT_EQ(computed.kind(), CS::Value::Kind::String);

    ctx.setGlobal("saved", computed);
    const CS::Value saved = ctx.store().global("saved");
    EXPECT_EQ(CS::stringBytes(saved), "привет, Вася");
}

/// And it stays readable across any number of later operations: a boxed string
/// is owned by the global's slot, not by the operation that produced it.
TEST(Context, StoredComputedStringSurvivesLaterOperations) {
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("who", "'Вася'", diag));

    const CS::Expression build = compileIn(ctx, "format('привет, ${}', who)");
    CS::Value computed = CS::Value::null();
    ASSERT_TRUE(ctx.eval(build, &computed, diag)) << diag.message;
    ctx.setGlobal("saved", computed);

    const CS::Expression noise = compileIn(ctx, "format('${} ${}', 1, 2)");
    for (int i = 0; i < 8; ++i) {
        CS::Value ignored = CS::Value::null();
        ASSERT_TRUE(ctx.eval(noise, &ignored, diag)) << diag.message;
    }

    const CS::Value saved = ctx.store().global("saved");
    EXPECT_EQ(CS::stringBytes(saved), "привет, Вася");
}

#ifndef NDEBUG
TEST(ContextMemory, RewrittenGlobalDoesNotGrowForever) {
    // Здесь меряется путь хоста — setVariableText: бэкенд шлёт новые данные на
    // каждое обновление экрана, и это основной случай перезаписи. Путь
    // программы (присваивание имени, docs/semantics.md §7.2) отпускает
    // вытесненное той же дверью Store и меряется в store_test.cpp
    // (StoreGlobals.SetGlobalAtReleasesTheDisplacedBox).
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
    // Единственный способ вырастить массив в языке. Раньше каждый push
    // переносил его в хвост пула, бросая прежний диапазон мусором.
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("rows", "[]", diag)) << diag.message;

    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(ctx.compileScript("push(rows, 1);", &script, diags, 1), 0u)
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
    // То, ради чего всё это: значение, отданное наружу, не зависит от того,
    // жив контекст или нет. Хост берёт ссылку и отпускает её сам.
    CS::Value escaped = CS::Value::null();
    {
        CS::Context ctx;
        CS::Diagnostic diag;
        ASSERT_TRUE(ctx.setVariableText("rows", "[1, 2, 3]", diag))
            << diag.message;

        const CS::Expression expr = compileIn(ctx, "rows");
        ASSERT_TRUE(ctx.eval(expr, &escaped, diag)) << diag.message;
        ASSERT_EQ(escaped.kind(), CS::Value::Kind::Array);
        CS::detail::retain(escaped.box());   // так делает обёртка хоста
    }
    // Контекста нет, хранилища нет, таблицы имён у него нет. Массив есть.
    const CS::detail::ArrayBox *node =
        static_cast<const CS::detail::ArrayBox *>(escaped.box());
    ASSERT_EQ(node->size(), 3u);
    EXPECT_EQ(node->at(2).numberValue(), 3.0);
    CS::detail::release(escaped.box());
}

TEST(ContextMemory, ObjectHandedOutKeepsItsKeysPastTheContext) {
    CS::Value escaped = CS::Value::null();
    {
        CS::Context ctx;
        CS::Diagnostic diag;
        ASSERT_TRUE(ctx.setVariableText("user", "{'name': 'Вася'}", diag))
            << diag.message;
        const CS::Expression expr = compileIn(ctx, "user");
        ASSERT_TRUE(ctx.eval(expr, &escaped, diag)) << diag.message;
        CS::detail::retain(escaped.box());
    }
    const CS::detail::ObjectBox *node =
        static_cast<const CS::detail::ObjectBox *>(escaped.box());
    ASSERT_EQ(node->size(), 1u);
    // Таблица имён пережила своё хранилище, потому что её держит коробка.
    EXPECT_EQ(node->keys->bytes(node->at(0).key), "name");
    CS::detail::release(escaped.box());
}

TEST(ContextMemory, StringPushedIntoGlobalArraySurvivesTheOperation) {
    // A computed string is built self-contained and survives the operation
    // boundary as a box — exactly the rule that lets a value get pushed into
    // an aggregate straight after being formatted.
    CS::Context ctx;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.setVariableText("rows", "[]", diag)) << diag.message;

    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(ctx.compileScript("push(rows, format('${}${}', 1, 2));",
                               &script, diags, 1),
              0u)
        << diags[0].message;
    ASSERT_TRUE(ctx.run(script, diag)) << diag.message;

    const CS::Expression expr = compileIn(ctx, "rows[0]");
    CS::Value got = CS::Value::null();
    ASSERT_TRUE(ctx.eval(expr, &got, diag)) << diag.message;
    EXPECT_EQ(CS::stringBytes(got), "12");
}

/// The builder hands back a box, and a box is readable without asking any
/// Store: that is what lets a format result be stored, pushed and returned.
TEST(Execution, BuildsAStringIntoABox) {
    CS::Store store;
    CS::Execution exec(store);

    const std::uint32_t mark = exec.beginString();
    exec.appendToString("при");
    exec.appendToString("вет");
    const CS::Value built = exec.endString(mark);

    EXPECT_EQ(built.kind(), CS::Value::Kind::String);
    EXPECT_EQ(CS::stringBytes(built), "привет");
}

/// A nested build finishes before the outer one continues, because the inner
/// mark sits above the outer mark in the same buffer.
TEST(Execution, NestedBuildTakesOnlyItsOwnTail) {
    CS::Store store;
    CS::Execution exec(store);

    const std::uint32_t outer = exec.beginString();
    exec.appendToString("a");
    const std::uint32_t inner = exec.beginString();
    exec.appendToString("bc");
    const CS::Value innerResult = exec.endString(inner);
    exec.appendToString("d");
    const CS::Value outerResult = exec.endString(outer);

    EXPECT_EQ(CS::stringBytes(innerResult), "bc");
    EXPECT_EQ(CS::stringBytes(outerResult), "ad");
}

/// An abandoned build leaves nothing behind for the next one to pick up.
TEST(Execution, AbortedBuildLeavesNoTail) {
    CS::Store store;
    CS::Execution exec(store);

    const std::uint32_t first = exec.beginString();
    exec.appendToString("discarded");
    exec.abortString(first);

    const std::uint32_t second = exec.beginString();
    exec.appendToString("kept");
    const CS::Value keptResult = exec.endString(second);
    EXPECT_EQ(CS::stringBytes(keptResult), "kept");
}

/// Aborting an inner build must not disturb bytes an outer, still-open build
/// already appended: the mark is a position in the shared buffer, and an
/// inner abort truncates only back to its own mark.
TEST(Execution, NestedAbortLeavesTheOuterAssemblyIntact) {
    CS::Store store;
    CS::Execution exec(store);

    const std::uint32_t outer = exec.beginString();
    exec.appendToString("внешнее ");
    const std::uint32_t inner = exec.beginString();
    exec.appendToString("выброшенное");
    exec.abortString(inner);
    exec.appendToString("продолжение");
    const CS::Value outerResult = exec.endString(outer);
    EXPECT_EQ(CS::stringBytes(outerResult), "внешнее продолжение");
}

/// The mark is a POSITION, not a pointer: growing builder_ well past its
/// initial capacity must not corrupt a build already in progress.
TEST(Execution, SurvivesBufferGrowth) {
    CS::Store store;
    CS::Execution exec(store);

    const std::uint32_t mark = exec.beginString();
    std::string expected;
    for (int i = 0; i < 500; ++i) {
        exec.appendToString("кусок");
        expected += "кусок";
    }
    const CS::Value markResult = exec.endString(mark);
    EXPECT_EQ(CS::stringBytes(markResult), expected);
}

/// Порядок «регистрация → компиляция» обеспечивается, а не оставляется на
/// совести хоста: check сверяет имена в момент своего вызова и полагается,
/// что состав имён после этого не меняется (docs/backlog.md B23).
TEST(ContextHostFunctions, RefusesRegistrationAfterFirstCompile) {
    CS::Context ctx;
    ChupaFunction fn = healthyFunction("formatDate");
    EXPECT_EQ(ctx.registerFunction(fn), CS::RegisterOutcome::Ok);

    CS::Expression expr;
    CS::Diagnostic diag{};
    ASSERT_EQ(ctx.compileExpression("42", &expr, &diag, 1), 0u);

    ChupaFunction late = healthyFunction("pluralForm");
    EXPECT_EQ(ctx.registerFunction(late), CS::RegisterOutcome::TooLate);
}

TEST(ContextHostFunctions, IsNotEvaluatingOutsideEvaluation) {
    CS::Context ctx;
    EXPECT_FALSE(ctx.isEvaluating());
}

/// Признак снимается и тогда, когда вычисление завершилось ошибкой: иначе
/// один неудачный кадр закрыл бы контекст навсегда.
TEST(ContextHostFunctions, EvaluatingFlagClearsAfterFailure) {
    CS::Context ctx;
    CS::Expression expr;
    CS::Diagnostic diag{};
    ASSERT_EQ(ctx.compileExpression("count(1)", &expr, &diag, 1), 0u);

    CS::Value out = CS::Value::null();
    EXPECT_FALSE(ctx.eval(expr, &out, diag));   // count требует агрегат
    EXPECT_FALSE(ctx.isEvaluating());
}

// ─── Двери C++-слоя, закрытые на время вызова ───

namespace {

/// Что колбэк успел выяснить о закрытых дверях. Файловые переменные, а не
/// поля: коллбэк — свободная функция с C-соглашением, поля ей взять негде,
/// кроме user_data, а туда уже уехал сам CS::Context.
bool g_textRefused = false;
bool g_expressionRefused = false;
bool g_scriptRefused = false;
CS::ErrorCode g_textCode = CS::ErrorCode::None;

/// Колбэк, добравшийся до CS::Context напрямую.
///
/// Так выглядит хост, зарегистрировавший функцию через
/// CS::Context::registerFunction, минуя C API (cli/echo.cpp — ровно этот
/// путь), и положивший контекст себе в user_data. Стража C API на этом пути
/// нет вовсе: она стоит на дверях c_api.cpp, а сюда никто из них не заходил.
bool knocksOnTheCppDoors(ChupaContext *, const ChupaValue *, std::size_t,
                         ChupaValue *out, void *userData) {
    auto *ctx = static_cast<CS::Context *>(userData);

    CS::Diagnostic diag{};
    g_textRefused = !ctx->setVariableText("y", "1", diag);
    g_textCode = diag.code;

    CS::Expression expr;
    CS::Diagnostic exprDiag{};
    g_expressionRefused =
        ctx->compileExpression("1 + 1", &expr, &exprDiag, 1) != 0u &&
        exprDiag.code == CS::ErrorCode::Usage;

    CS::Script script;
    CS::Diagnostic scriptDiag{};
    g_scriptRefused =
        ctx->compileScript("y = 1;", &script, &scriptDiag, 1) != 0u &&
        scriptDiag.code == CS::ErrorCode::Usage;

    chupa_make_number(out, 1.0);
    return true;
}

}  // namespace

/// Оба конца: признак поднят входом в Context::eval и снят его выходом, а
/// внутри — между этими двумя точками — все три двери, чья сигнатура
/// позволяет сказать «нет», говорят его и в релизе.
///
/// setGlobal и setGlobalString сюда не входят: они возвращают void, ловятся
/// только утверждением отладочной сборки, и тест, проверяющий assert,
/// пришлось бы писать смертью процесса (docs/backlog.md B34).
TEST(ContextHostFunctions, CppDoorsRefuseFromInsideACallback) {
    CS::Context ctx;
    g_textRefused = false;
    g_expressionRefused = false;
    g_scriptRefused = false;
    g_textCode = CS::ErrorCode::None;

    ChupaFunction fn = described("knocks", 0, 0, knocksOnTheCppDoors, &ctx);
    ASSERT_EQ(ctx.registerFunction(fn), CS::RegisterOutcome::Ok);

    CS::Expression expr;
    CS::Diagnostic diag{};
    ASSERT_EQ(ctx.compileExpression("knocks()", &expr, &diag, 1), 0u)
        << diag.message;

    CS::Value out = CS::Value::null();
    ASSERT_TRUE(ctx.eval(expr, &out, diag)) << diag.message;

    EXPECT_TRUE(g_textRefused);
    EXPECT_EQ(g_textCode, CS::ErrorCode::Usage);
    EXPECT_TRUE(g_expressionRefused);
    EXPECT_TRUE(g_scriptRefused);
    EXPECT_FALSE(ctx.isEvaluating());

    // Дверь снова открыта, и отказ ничего в контексте не испортил.
    CS::Diagnostic after{};
    EXPECT_TRUE(ctx.setVariableText("y", "1", after)) << after.message;
}

}  // namespace
