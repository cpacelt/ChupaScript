#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "data.hpp"
#include "diagnostic.hpp"
#include "script.hpp"
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
    EXPECT_EQ(store.string(name), "Петя");
}

TEST(Script, OwnsItsSource) {
    CS::Deferred dead;
    // Прямое присваивание имени переменной запрещено языком
    // (core/src/check.cpp: "cannot assign to a variable name",
    // docs/semantics.md §7.2) — целью обязана быть Member/Index. Поэтому
    // вместо "n = n + 1;" из брифа берём поле объекта: та же суть теста
    // (владение исходником, временный буфер умирает сразу), синтаксис —
    // тот, что разрешает check.cpp.
    CS::Store store;
    CS::Execution exec(store);
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, dead, "obj", "{'n': 1}", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    {
        std::string temporary = "obj.n = obj.n + 1;";
        ASSERT_EQ(CS::Script::compile(temporary, store, &script, diags, 1), 0u);
    }
    EXPECT_EQ(script.source(), "obj.n = obj.n + 1;");
    EXPECT_TRUE(script.run(exec, diag));
}

TEST(Script, ReportsUnknownName) {
    // Цель присваивания — Member, а не голый Identifier, чтобы сработала
    // только проверка "unknown name": "missing = 1;" из брифа даёт вдобавок
    // "cannot assign to a variable name" (check.cpp), то есть 2 ошибки, а не
    // 1, как утверждает бриф.
    //
    // diags[0].message пришпилен к точному тексту (review round 2, M8):
    // код ErrorCode::Name общий у обеих проверок check.cpp — "unknown name"
    // и "cannot assign to a variable name" — тест был бы зелёным, поймав
    // не ту.
    CS::Store store;
    CS::Execution exec(store);
    CS::Script script;
    CS::Diagnostic diags[2];
    EXPECT_EQ(CS::Script::compile("missing.field = 1;", store, &script, diags, 2),
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
    // Direct assignment to a variable name is rejected by the language
    // (check.cpp: "cannot assign to a variable name"), so the target is an
    // object field, same as OwnsItsSource above.
    ASSERT_TRUE(CS::setVariable(home, dead, "x", "{'n': 1}", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Script::compile("x.n = 1;", home, &script, diags, 1), 0u);

    CS::Execution elsewhere(foreign);
    CS::Diagnostic failure;
    EXPECT_FALSE(script.run(elsewhere, failure));
    EXPECT_EQ(failure.code, CS::ErrorCode::Usage);
}

}  // namespace
