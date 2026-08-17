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
    // ASSERT, а не EXPECT: если setVariable откажет, продолжать тест не
    // имеет смысла — дальше он упал бы непонятным «unknown name» из
    // compile, а не в этой точке (review round 2, M2).
    ASSERT_TRUE(CS::setVariable(store, "user", "{'name': 'Вася'}", diag));
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

TEST(Expression, FailedCompileDoesNotTouchOut) {
    // Контракт «неудачная компиляция не портит *out» (expression.hpp) не
    // покрыт ничем другим: RecompileReplacesEverything гоняет только успехи,
    // а тесты на ошибки всегда работают со свежей единицей (review round 2,
    // I1).
    CS::Store store;
    storeWithUser(store);
    CS::Expression expr;
    CS::Diagnostic diags[2];
    ASSERT_EQ(CS::Expression::compile("user.name", store, &expr, diags, 2), 0u);

    ASSERT_EQ(CS::Expression::compile("user..name", store, &expr, diags, 2), 1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Syntax);
    EXPECT_EQ(expr.source(), "user.name");

    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.eval(store, &out, diag));
    EXPECT_EQ(store.string(out), "Вася");
}

TEST(Expression, EvalNumberReturnsOk) {
    CS::Store store;
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("1 + 1", store, &expr, diags, 1), 0u);

    double out = 0.0;
    CS::Diagnostic diag;
    EXPECT_EQ(expr.evalNumber(store, &out, diag), CS::EvalStatus::Ok);
    EXPECT_DOUBLE_EQ(out, 2.0);
}

TEST(Expression, EvalNumberReturnsNullSeparately) {
    CS::Store store;
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("null", store, &expr, diags, 1), 0u);

    double out = 42.0;
    CS::Diagnostic diag;
    // Null — не ошибка и не значение: положить его в double* некуда.
    EXPECT_EQ(expr.evalNumber(store, &out, diag), CS::EvalStatus::Null);
    EXPECT_DOUBLE_EQ(out, 42.0);  // *out не тронут
}

TEST(Expression, EvalNumberOnStringIsTypeErrorWithRealOffset) {
    CS::Store store;
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("  'привет'", store, &expr, diags, 1), 0u);

    // Сторожевое значение: случайно получить его нельзя, поэтому если оно
    // выживет — *out на исходе Error действительно не тронут (review round
    // 3, M2).
    double out = 42.0;
    CS::Diagnostic diag;
    EXPECT_EQ(expr.evalNumber(store, &out, diag), CS::EvalStatus::Error);
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    // Смещение настоящее, а не ноль: прокладка ставила 0 и указывала в никуда.
    EXPECT_EQ(diag.offset, 2u);
    EXPECT_DOUBLE_EQ(out, 42.0);
}

TEST(Expression, EvalBoolAndString) {
    CS::Store store;
    CS::Diagnostic diag;
    CS::Diagnostic diags[1];

    CS::Expression flag;
    ASSERT_EQ(CS::Expression::compile("1 < 2", store, &flag, diags, 1), 0u);
    bool b = false;
    EXPECT_EQ(flag.evalBool(store, &b, diag), CS::EvalStatus::Ok);
    EXPECT_TRUE(b);

    CS::Expression text;
    ASSERT_EQ(CS::Expression::compile("'привет'", store, &text, diags, 1), 0u);
    std::string_view s;
    EXPECT_EQ(text.evalString(store, &s, diag), CS::EvalStatus::Ok);
    EXPECT_EQ(s, "привет");
}

TEST(Expression, EvalStringPropagatesEvalError) {
    CS::Store store;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, "items", "[1]", diag));

    CS::Expression expr;
    CS::Diagnostic diags[1];
    // Бриф предлагал items[5], но положительный индекс за концом массива при
    // чтении штатно даёт null (EvalCompound.BeyondTheEndGivesTypeNotRange,
    // core/tests/eval_test.cpp) — не ошибку. Range при чтении даёт только
    // дробный/отрицательный/переполняющий индекс (EvalIndex.
    // FractionalAndNegativeIndicesAreErrors, там же); берём отрицательный.
    ASSERT_EQ(CS::Expression::compile("items[-1]", store, &expr, diags, 1), 0u);

    // Сторожевое значение по той же причине, что и у out=42.0 выше
    // (review round 3, M2): если *out на исходе Error действительно не
    // тронут, "было" переживёт вызов неизменным.
    std::string_view s = "было";
    EXPECT_EQ(expr.evalString(store, &s, diag), CS::EvalStatus::Error);
    EXPECT_EQ(diag.code, CS::ErrorCode::Range);
    // Смещение указывает на сам индекс — байт '[' в "items[-1]" (review
    // round 3, M3): ошибка рождается на operation "[...]", а не на всём
    // выражении.
    EXPECT_EQ(diag.offset, 5u);
    EXPECT_EQ(s, "было");
}

// Байты литерала уложены в пул текста один раз, на компиляции: пул поштучно не
// освобождается, поэтому копия на каждом вычислении означала бы память,
// растущую от числа кадров (docs/backlog.md B51).
TEST(Expression, StringLiteralIsStoredOnceAtCompileTime) {
    CS::Store store;
    CS::Expression expr;
    CS::Diagnostic diags[1];
    CS::Diagnostic diag;
    ASSERT_EQ(CS::Expression::compile("'привет'", store, &expr, diags, 1), 0u);

    std::string_view s;
    ASSERT_EQ(expr.evalString(store, &s, diag), CS::EvalStatus::Ok);
    const std::size_t after = store.bytesUsed();

    for (int i = 0; i < 100; ++i) {
        ASSERT_EQ(expr.evalString(store, &s, diag), CS::EvalStatus::Ok);
        EXPECT_EQ(s, "привет");
    }
    EXPECT_EQ(store.bytesUsed(), after);
}

// Экранирование раскодировано тоже один раз: черновик заводился на каждое
// вычисление и на каждый литерал.
TEST(Expression, EscapedLiteralIsDecodedOnceAtCompileTime) {
    CS::Store store;
    CS::Expression expr;
    CS::Diagnostic diags[1];
    CS::Diagnostic diag;
    ASSERT_EQ(CS::Expression::compile("'до\\nпосле'", store, &expr, diags, 1), 0u);

    std::string_view s;
    ASSERT_EQ(expr.evalString(store, &s, diag), CS::EvalStatus::Ok);
    EXPECT_EQ(s, "до\nпосле");
    const std::size_t after = store.bytesUsed();

    ASSERT_EQ(expr.evalString(store, &s, diag), CS::EvalStatus::Ok);
    EXPECT_EQ(s, "до\nпосле");
    EXPECT_EQ(store.bytesUsed(), after);
}

// Ключ литерала объекта — тоже уложенный литерал, но копию в объект objectSet
// всё равно делает: ключ обязан пережить выражение, значит лежать в записи
// объекта. Проверяется, что укладка ключа не сбила ни состав, ни порядок.
TEST(Expression, ObjectLiteralKeysComeFromStoredLiterals) {
    CS::Store store;
    CS::Expression expr;
    CS::Diagnostic diags[1];
    CS::Diagnostic diag;
    ASSERT_EQ(
        CS::Expression::compile("{'б': 2, 'а\\t': 1}['а\\t']", store, &expr, diags, 1),
        0u);

    double out = 0.0;
    EXPECT_EQ(expr.evalNumber(store, &out, diag), CS::EvalStatus::Ok);
    EXPECT_DOUBLE_EQ(out, 1.0);
}

}  // namespace
