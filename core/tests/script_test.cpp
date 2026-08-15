#include <gtest/gtest.h>

#include <string>

#include "data.hpp"
#include "diagnostic.hpp"
#include "script.hpp"
#include "store.hpp"

namespace {

TEST(Script, CompilesAndRuns) {
    CS::Store store;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, "user", "{'name': 'Вася'}", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Script::compile("user.name = 'Петя';", store, &script, diags, 1),
              0u);
    ASSERT_TRUE(script.run(store, diag));

    // Store::global и Store::objectGet возвращают Value, а не пишут в
    // выходной параметр (core/src/store.hpp:107,152).
    const CS::Value user = store.global("user");
    const CS::Value name = store.objectGet(user, "name");
    EXPECT_EQ(store.string(name), "Петя");
}

TEST(Script, OwnsItsSource) {
    // Прямое присваивание имени переменной запрещено языком
    // (core/src/check.cpp: "cannot assign to a variable name",
    // docs/semantics.md §7.2) — целью обязана быть Member/Index. Поэтому
    // вместо "n = n + 1;" из брифа берём поле объекта: та же суть теста
    // (владение исходником, временный буфер умирает сразу), синтаксис —
    // тот, что разрешает check.cpp.
    CS::Store store;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, "obj", "{'n': 1}", diag));

    CS::Script script;
    CS::Diagnostic diags[1];
    {
        std::string temporary = "obj.n = obj.n + 1;";
        ASSERT_EQ(CS::Script::compile(temporary, store, &script, diags, 1), 0u);
    }
    EXPECT_EQ(script.source(), "obj.n = obj.n + 1;");
    EXPECT_TRUE(script.run(store, diag));
}

TEST(Script, ReportsUnknownName) {
    // Цель присваивания — Member, а не голый Identifier, чтобы сработала
    // только проверка "unknown name": "missing = 1;" из брифа даёт вдобавок
    // "cannot assign to a variable name" (check.cpp), то есть 2 ошибки, а не
    // 1, как утверждает бриф.
    CS::Store store;
    CS::Script script;
    CS::Diagnostic diags[2];
    EXPECT_EQ(CS::Script::compile("missing.field = 1;", store, &script, diags, 2),
              1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Name);
}

}  // namespace
