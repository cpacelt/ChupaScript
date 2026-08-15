#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "diagnostic.hpp"
#include "data.hpp"
#include "expression.hpp"
#include "store.hpp"

namespace {

// CS::Store некопируем и не имеет конструктора перемещения (пользовательский
// деструктор его подавляет), поэтому вернуть готовое хранилище по значению
// нельзя — заполняем на месте через ссылку. Отклонение от буквального текста
// брифа: там storeWithUser() возвращала CS::Store по значению, что не
// компилируется против core/src/store.hpp (see task-2-report.md).
void storeWithUser(CS::Store &store) {
    CS::Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(store, "user", "{'name': 'Вася'}", diag));
}

TEST(Expression, CompilesAndEvaluates) {
    CS::Store store;
    storeWithUser(store);
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("user.name", store, &expr, diags, 1), 0u);

    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.eval(store, &out, diag));
    EXPECT_EQ(store.string(out), "Вася");
}

TEST(Expression, OwnsItsSource) {
    CS::Store store;
    storeWithUser(store);
    CS::Expression expr;
    CS::Diagnostic diags[1];
    {
        // Исходник живёт в буфере, который умрёт прямо сейчас.
        std::string temporary = "user.name";
        ASSERT_EQ(CS::Expression::compile(temporary, store, &expr, diags, 1), 0u);
    }
    // Единица самодостаточна: правила «буфер обязан пережить» больше нет.
    EXPECT_EQ(expr.source(), "user.name");

    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.eval(store, &out, diag));
    EXPECT_EQ(store.string(out), "Вася");
}

TEST(Expression, SurvivesBeingMoved) {
    CS::Store store;
    storeWithUser(store);
    std::vector<CS::Expression> units;
    units.reserve(1);
    units.emplace_back();

    CS::Diagnostic diags[1];
    // Короткий исходник — та самая SSO-строка, на которой ломался UAF-3.
    ASSERT_EQ(CS::Expression::compile("user.name", store, &units[0], diags, 1), 0u);
    for (int i = 0; i < 8; ++i) { units.emplace_back(); }  // вектор переехал

    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    ASSERT_TRUE(units[0].eval(store, &out, diag));
    EXPECT_EQ(store.string(out), "Вася");
}

TEST(Expression, ReportsSyntaxError) {
    CS::Store store;
    storeWithUser(store);
    CS::Expression expr;
    CS::Diagnostic diags[2];
    EXPECT_EQ(CS::Expression::compile("user..name", store, &expr, diags, 2), 1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Syntax);
}

TEST(Expression, ReportsUnknownName) {
    CS::Store store;
    storeWithUser(store);
    CS::Expression expr;
    CS::Diagnostic diags[2];
    EXPECT_EQ(CS::Expression::compile("missing.name", store, &expr, diags, 2), 1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Name);
}

TEST(Expression, RecompileReplacesEverything) {
    CS::Store store;
    storeWithUser(store);
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("user.name", store, &expr, diags, 1), 0u);
    ASSERT_EQ(CS::Expression::compile("1 + 1", store, &expr, diags, 1), 0u);
    EXPECT_EQ(expr.source(), "1 + 1");

    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.eval(store, &out, diag));
    EXPECT_DOUBLE_EQ(out.numberValue(), 2.0);
}

}  // namespace
