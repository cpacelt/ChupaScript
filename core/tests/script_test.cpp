#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "data.hpp"
#include "diagnostic.hpp"
#include "script.hpp"
#include "box.hpp"
#include "store.hpp"
#include "aggregate.hpp"

namespace {

TEST(Script, CompilesAndRuns) {
    CS::Deferred dead;
    CS::Store store;
    CS::Execution exec(store);
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, dead, "user", "{'name': 'Вася'}", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Script::compile("user.name = 'Петя';", store, &script, diags, 1),
              0u);
    ASSERT_TRUE(script.run(exec, diag));

    // Store::global и Store::objectGet возвращают Value, а не пишут в
    // выходной параметр (core/src/store.hpp:107,152).
    const CS::Value user = store.global("user");
    const CS::Value name = CS::objectGet(user, "name");
    EXPECT_EQ(CS::stringBytes(name), "Петя");
}

TEST(Script, OwnsItsSource) {
    CS::Deferred dead;
    CS::Store store;
    CS::Execution exec(store);
    CS::Diagnostic diag;
    store.setGlobal("n", CS::Value::number(1), dead);

    CS::Script script;
    CS::Diagnostic diags[1];
    {
        std::string temporary = "n = n + 1;";
        ASSERT_EQ(CS::Script::compile(temporary, store, &script, diags, 1), 0u);
    }
    // Временный буфер умер вместе с областью видимости: единица обязана
    // держать собственную копию исходника.
    EXPECT_EQ(script.source(), "n = n + 1;");
    EXPECT_TRUE(script.run(exec, diag));
    EXPECT_EQ(store.global("n").numberValue(), 2.0);
}

TEST(Script, ReportsUnknownName) {
    // diags[0].message пришпилен к точному тексту (review round 2, M8):
    // код ErrorCode::Name общий у нескольких проверок check.cpp — тест был
    // бы зелёным, поймав не ту.
    CS::Store store;
    CS::Execution exec(store);
    CS::Script script;
    CS::Diagnostic diags[2];
    EXPECT_EQ(CS::Script::compile("missing = 1;", store, &script, diags, 2),
              1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Name);
    EXPECT_STREQ(diags[0].message, "unknown name");
}

TEST(Script, SurvivesBeingMoved) {
    CS::Deferred dead;
    // Тот же манёвр, что Expression.SurvivesBeingMoved (review round 2, M3):
    // *out = std::move(built) в script.cpp — отдельная строка кода, зелень
    // соседнего теста про Expression о ней ничего не говорит.
    CS::Store store;
    CS::Execution exec(store);
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, dead, "obj", "{'n': 1}", diag));

    std::vector<CS::Script> units;
    units.reserve(1);
    units.emplace_back();

    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Script::compile("obj.n = obj.n + 1;", store, &units[0], diags, 1),
              0u);
    for (int i = 0; i < 8; ++i) { units.emplace_back(); }  // вектор переехал

    ASSERT_TRUE(units[0].run(exec, diag));
    const CS::Value obj = store.global("obj");
    const CS::Value n = CS::objectGet(obj, "n");
    EXPECT_DOUBLE_EQ(n.numberValue(), 2.0);
}

TEST(Script, RecompileReplacesEverything) {
    CS::Deferred dead;
    CS::Store store;
    CS::Execution exec(store);
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, dead, "obj", "{'n': 1}", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Script::compile("obj.n = obj.n + 1;", store, &script, diags, 1),
              0u);
    ASSERT_EQ(CS::Script::compile("obj.n = obj.n + 10;", store, &script, diags, 1),
              0u);
    EXPECT_EQ(script.source(), "obj.n = obj.n + 10;");

    ASSERT_TRUE(script.run(exec, diag));
    const CS::Value obj = store.global("obj");
    const CS::Value n = CS::objectGet(obj, "n");
    EXPECT_DOUBLE_EQ(n.numberValue(), 11.0);
}

TEST(Script, FailedCompileDoesNotTouchOut) {
    CS::Deferred dead;
    // Контракт «неудачная компиляция не портит *out» (script.hpp) не
    // покрыт ничем другим (review round 2, I1).
    CS::Store store;
    CS::Execution exec(store);
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, dead, "obj", "{'n': 1}", diag));

    CS::Script script;
    CS::Diagnostic diags[2];
    ASSERT_EQ(CS::Script::compile("obj.n = obj.n + 1;", store, &script, diags, 2),
              0u);

    ASSERT_EQ(CS::Script::compile("missing.field = 1;", store, &script, diags, 2),
              1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Name);
    EXPECT_EQ(script.source(), "obj.n = obj.n + 1;");

    ASSERT_TRUE(script.run(exec, diag));
    const CS::Value obj = store.global("obj");
    const CS::Value n = CS::objectGet(obj, "n");
    EXPECT_DOUBLE_EQ(n.numberValue(), 2.0);
}

/// A unit carries the identity of the Store it was compiled against, and
/// refuses to run on any other one. Without the check the slot number resolved
/// at compile time would index the other Store's values_ and return whichever
/// variable happens to sit there — silently, in release builds.
TEST(Script, RefusesToEvaluateOnAnotherStore) {
    CS::Store home;
    CS::Store foreign;
    CS::Deferred dead;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(home, dead, "x", "1", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    // Assigns a value different from the variable's current one, so a run
    // that slipped through despite the refusal would be visible below.
    ASSERT_EQ(CS::Script::compile("x = 2;", home, &script, diags, 1), 0u);

    CS::Execution elsewhere(foreign);
    CS::Diagnostic failure;
    EXPECT_FALSE(script.run(elsewhere, failure));
    EXPECT_EQ(failure.code, CS::ErrorCode::Usage);

    // The refused run must not have written through to home's variable.
    EXPECT_DOUBLE_EQ(home.global("x").numberValue(), 1.0);
}

}  // namespace
