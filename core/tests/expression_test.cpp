#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "aggregate.hpp"
#include "box.hpp"
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
    CS::Deferred dead;
    CS::Diagnostic diag;
    // ASSERT, а не EXPECT: если setVariable откажет, продолжать тест не
    // имеет смысла — дальше он упал бы непонятным «unknown name» из
    // compile, а не в этой точке (review round 2, M2).
    ASSERT_TRUE(CS::setVariable(store, dead, "user", "{'name': 'Вася'}", diag));
}

TEST(Expression, CompilesAndEvaluates) {
    CS::Store store;
    CS::Execution exec(store);
    storeWithUser(store);
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("user.name", store, &expr, diags, 1), 0u);

    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.eval(exec, &out, diag));
    EXPECT_EQ(CS::stringBytes(out), "Вася");
}

TEST(Expression, OwnsItsSource) {
    CS::Store store;
    CS::Execution exec(store);
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
    ASSERT_TRUE(expr.eval(exec, &out, diag));
    EXPECT_EQ(CS::stringBytes(out), "Вася");
}

TEST(Expression, SurvivesBeingMoved) {
    CS::Store store;
    CS::Execution exec(store);
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
    ASSERT_TRUE(units[0].eval(exec, &out, diag));
    EXPECT_EQ(CS::stringBytes(out), "Вася");
}

TEST(Expression, ReportsSyntaxError) {
    CS::Store store;
    CS::Execution exec(store);
    storeWithUser(store);
    CS::Expression expr;
    CS::Diagnostic diags[2];
    EXPECT_EQ(CS::Expression::compile("user..name", store, &expr, diags, 2), 1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Syntax);
}

TEST(Expression, ReportsUnknownName) {
    CS::Store store;
    CS::Execution exec(store);
    storeWithUser(store);
    CS::Expression expr;
    CS::Diagnostic diags[2];
    EXPECT_EQ(CS::Expression::compile("missing.name", store, &expr, diags, 2), 1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Name);
}

TEST(Expression, RecompileReplacesEverything) {
    CS::Store store;
    CS::Execution exec(store);
    storeWithUser(store);
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("user.name", store, &expr, diags, 1), 0u);
    ASSERT_EQ(CS::Expression::compile("1 + 1", store, &expr, diags, 1), 0u);
    EXPECT_EQ(expr.source(), "1 + 1");

    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.eval(exec, &out, diag));
    EXPECT_DOUBLE_EQ(out.numberValue(), 2.0);
}

TEST(Expression, FailedCompileDoesNotTouchOut) {
    // Контракт «неудачная компиляция не портит *out» (expression.hpp) не
    // покрыт ничем другим: RecompileReplacesEverything гоняет только успехи,
    // а тесты на ошибки всегда работают со свежей единицей (review round 2,
    // I1).
    CS::Store store;
    CS::Execution exec(store);
    storeWithUser(store);
    CS::Expression expr;
    CS::Diagnostic diags[2];
    ASSERT_EQ(CS::Expression::compile("user.name", store, &expr, diags, 2), 0u);

    ASSERT_EQ(CS::Expression::compile("user..name", store, &expr, diags, 2), 1u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Syntax);
    EXPECT_EQ(expr.source(), "user.name");

    CS::Value out = CS::Value::null();
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.eval(exec, &out, diag));
    EXPECT_EQ(CS::stringBytes(out), "Вася");
}

TEST(Expression, EvalNumberReturnsOk) {
    CS::Store store;
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("1 + 1", store, &expr, diags, 1), 0u);

    double out = 0.0;
    CS::Diagnostic diag;
    EXPECT_EQ(expr.evalNumber(exec, &out, diag), CS::EvalStatus::Ok);
    EXPECT_DOUBLE_EQ(out, 2.0);
}

TEST(Expression, EvalNumberReturnsNullSeparately) {
    CS::Store store;
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("null", store, &expr, diags, 1), 0u);

    double out = 42.0;
    CS::Diagnostic diag;
    // Null — не ошибка и не значение: положить его в double* некуда.
    EXPECT_EQ(expr.evalNumber(exec, &out, diag), CS::EvalStatus::Null);
    EXPECT_DOUBLE_EQ(out, 42.0);  // *out не тронут
}

TEST(Expression, EvalNumberOnStringIsTypeErrorWithRealOffset) {
    CS::Store store;
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("  'привет'", store, &expr, diags, 1), 0u);

    // Сторожевое значение: случайно получить его нельзя, поэтому если оно
    // выживет — *out на исходе Error действительно не тронут (review round
    // 3, M2).
    double out = 42.0;
    CS::Diagnostic diag;
    EXPECT_EQ(expr.evalNumber(exec, &out, diag), CS::EvalStatus::Error);
    EXPECT_EQ(diag.code, CS::ErrorCode::Type);
    // Смещение настоящее, а не ноль: прокладка ставила 0 и указывала в никуда.
    EXPECT_EQ(diag.offset, 2u);
    EXPECT_DOUBLE_EQ(out, 42.0);
}

TEST(Expression, EvalBoolAndString) {
    CS::Store store;
    CS::Execution exec(store);
    CS::Diagnostic diag;
    CS::Diagnostic diags[1];

    CS::Expression flag;
    ASSERT_EQ(CS::Expression::compile("1 < 2", store, &flag, diags, 1), 0u);
    bool b = false;
    EXPECT_EQ(flag.evalBool(exec, &b, diag), CS::EvalStatus::Ok);
    EXPECT_TRUE(b);

    CS::Expression text;
    ASSERT_EQ(CS::Expression::compile("'привет'", store, &text, diags, 1), 0u);
    CS::Value textValue = CS::Value::null();
    ASSERT_TRUE(text.eval(exec, &textValue, diag));
    EXPECT_EQ(CS::stringBytes(textValue), "привет");
}

TEST(Expression, EvalStringPropagatesEvalError) {
    CS::Deferred dead;
    CS::Store store;
    CS::Execution exec(store);
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, dead, "items", "[1]", diag));

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
    CS::Value value = CS::Value::inlineString("было");
    EXPECT_FALSE(expr.eval(exec, &value, diag));
    EXPECT_EQ(diag.code, CS::ErrorCode::Range);
    // Смещение указывает на сам индекс — байт '[' в "items[-1]" (review
    // round 3, M3): ошибка рождается на operation "[...]", а не на всём
    // выражении.
    EXPECT_EQ(diag.offset, 5u);
    EXPECT_EQ(CS::stringBytes(value), "было");
}

// Байты литерала уложены в пул текста один раз, на компиляции: пул поштучно не
// освобождается, поэтому копия на каждом вычислении означала бы память,
// растущую от числа кадров (docs/backlog.md B51).
TEST(Expression, StringLiteralIsStoredOnceAtCompileTime) {
    CS::Store store;
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[1];
    CS::Diagnostic diag;
    ASSERT_EQ(CS::Expression::compile("'привет'", store, &expr, diags, 1), 0u);

    CS::Value value = CS::Value::null();
    ASSERT_TRUE(expr.eval(exec, &value, diag));
    const std::size_t after = store.bytesUsed();

    for (int i = 0; i < 100; ++i) {
        ASSERT_TRUE(expr.eval(exec, &value, diag));
        EXPECT_EQ(CS::stringBytes(value), "привет");
    }
    EXPECT_EQ(store.bytesUsed(), after);
}

// Экранирование раскодировано тоже один раз: черновик заводился на каждое
// вычисление и на каждый литерал.
TEST(Expression, EscapedLiteralIsDecodedOnceAtCompileTime) {
    CS::Store store;
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[1];
    CS::Diagnostic diag;
    ASSERT_EQ(CS::Expression::compile("'до\\nпосле'", store, &expr, diags, 1), 0u);

    CS::Value value = CS::Value::null();
    ASSERT_TRUE(expr.eval(exec, &value, diag));
    EXPECT_EQ(CS::stringBytes(value), "до\nпосле");
    const std::size_t after = store.bytesUsed();

    ASSERT_TRUE(expr.eval(exec, &value, diag));
    EXPECT_EQ(CS::stringBytes(value), "до\nпосле");
    EXPECT_EQ(store.bytesUsed(), after);
}

// Ключ литерала объекта — тоже уложенный литерал, но копию в объект objectSet
// всё равно делает: ключ обязан пережить выражение, значит лежать в записи
// объекта. Проверяется, что укладка ключа не сбила ни состав, ни порядок.
TEST(Expression, ObjectLiteralKeysComeFromStoredLiterals) {
    CS::Store store;
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[1];
    CS::Diagnostic diag;
    ASSERT_EQ(
        CS::Expression::compile("{'б': 2, 'а\\t': 1}['а\\t']", store, &expr, diags, 1),
        0u);

    double out = 0.0;
    EXPECT_EQ(expr.evalNumber(exec, &out, diag), CS::EvalStatus::Ok);
    EXPECT_DOUBLE_EQ(out, 1.0);
}

/// A unit carries the identity of the Store it was compiled against, and
/// refuses to run on any other one. Without the check the slot number resolved
/// at compile time would index the other Store's values_ and return whichever
/// variable happens to sit there — silently, in release builds.
TEST(Expression, RefusesToEvaluateOnAnotherStore) {
    CS::Store home;
    CS::Store foreign;
    CS::Deferred dead;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(home, dead, "x", "1", diag));
    ASSERT_TRUE(CS::setVariable(foreign, dead, "y", "2", diag));

    CS::Expression expr;
    CS::Diagnostic diags[1];
    ASSERT_EQ(CS::Expression::compile("x", home, &expr, diags, 1), 0u);

    CS::Execution elsewhere(foreign);
    CS::Value out = CS::Value::number(99.0);  // sentinel: not the eval result
    CS::Diagnostic failure;
    EXPECT_FALSE(expr.eval(elsewhere, &out, failure));
    EXPECT_EQ(failure.code, CS::ErrorCode::Usage);
    EXPECT_EQ(out.numberValue(), 99.0);  // *out untouched on refusal
}

#ifndef NDEBUG
/// A string literal belongs to the Ast that parsed it, not to the Store the
/// unit was compiled against: the literal is part of the PROGRAM. The box
/// therefore outlives the Store and dies with the unit.
TEST(Expression, LiteralOutlivesTheStoreItWasCompiledAgainst) {
    const std::size_t before = CS::detail::liveBoxCount();

    CS::Expression expr;
    {
        CS::Store store;
        CS::Diagnostic diags[1];
        ASSERT_EQ(CS::Expression::compile("'literal'", store, &expr, diags, 1), 0u);
        EXPECT_EQ(CS::detail::liveBoxCount(), before + 1);
    }
    // The Store is gone; the literal is not.
    EXPECT_EQ(CS::detail::liveBoxCount(), before + 1);

    expr = CS::Expression{};
    EXPECT_EQ(CS::detail::liveBoxCount(), before);
}
#endif

TEST(EvalTracked, AConstantFillsTheWholeSetWithTheEternalZero) {
    CS::Store store;
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("42", store, &expr, diags, 4), 0u);

    CS::Value out = CS::Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.evalTracked(exec, &out, deps, &n, diag));

    EXPECT_EQ(n, 0u) << "ноль — законный ответ, и это не переполнение";
    for (const CS::Dep &dep : deps) {
        EXPECT_EQ(dep.epoch, &CS::kZeroEpoch);
        EXPECT_EQ(dep.owner.kind(), CS::Value::Kind::Null);
    }
}

TEST(EvalTracked, TheTailPointsAtTheEternalZero) {
    // Читатель складывает ровно kMaxDeps слов, не заглядывая в счётчик, —
    // ради этого хвост обязан быть настоящим нулевым слагаемым, а не nullptr.
    CS::Store store;
    CS::Deferred dead;
    store.setGlobal("a", CS::Value::number(1.0), dead);
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("a", store, &expr, diags, 4), 0u);

    CS::Value out = CS::Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.evalTracked(exec, &out, deps, &n, diag));

    ASSERT_EQ(n, 1u);
    EXPECT_NE(deps[0].epoch, &CS::kZeroEpoch);
    for (std::uint32_t i = 1; i < CS::kMaxDeps; ++i) {
        EXPECT_EQ(deps[i].epoch, &CS::kZeroEpoch);
    }
}

TEST(EvalTracked, TheSumMovesExactlyWhenTheAnswerCanChange) {
    CS::Store store;
    CS::Deferred dead;
    CS::Diagnostic setup;
    ASSERT_TRUE(CS::setVariable(store, dead, "users",
                                "[{'name': 'Вася'}, {'name': 'Петя'}]", setup));
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("users[0].name", store, &expr, diags, 4), 0u);

    CS::Value out = CS::Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.evalTracked(exec, &out, deps, &n, diag));
    ASSERT_EQ(n, 3u);

    const auto sum = [&deps]() {
        CS::Epoch total = 0;
        for (const CS::Dep &dep : deps) { total += *dep.epoch; }
        return total;
    };
    const CS::Epoch snapshot = sum();

    // Правка соседнего элемента читателя нулевого не задевает — то, ради чего
    // схема с коробками стоит своих денег (спека §3.2).
    const CS::Value users = store.global("users");
    CS::objectSet(CS::arrayAt(users, 1), "name", CS::Value::null(), store.clock(), dead);
    EXPECT_EQ(sum(), snapshot);

    // Правка своего — задевает.
    CS::objectSet(CS::arrayAt(users, 0), "name", CS::Value::null(), store.clock(), dead);
    EXPECT_GT(sum(), snapshot);
}

TEST(EvalTracked, OverflowPoisonsTheSetSoAForgetfulReaderCrashesLoudly) {
    CS::Store store;
    CS::Deferred dead;
    CS::Diagnostic setup;
    ASSERT_TRUE(CS::setVariable(store, dead, "u",
                                "{'a': {'b': {'c': {'d': {'e': 1}}}}}", setup));
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("u.a.b.c.d.e", store, &expr, diags, 4), 0u);

    CS::Value out = CS::Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.evalTracked(exec, &out, deps, &n, diag));

    EXPECT_EQ(n, CS::kDepsOverflow);
    for (const CS::Dep &dep : deps) { EXPECT_EQ(dep.epoch, nullptr); }
}

TEST(EvalTracked, AnAggregateResultIsNotCached) {
    // Спека §2.8: кэшировать нечего — возврат ручки это копия шестнадцати
    // байт. И граница ответственности: за содержимое агрегата движок не
    // ручается, читает его хост своими вызовами.
    CS::Store store;
    CS::Deferred dead;
    CS::Diagnostic setup;
    ASSERT_TRUE(CS::setVariable(store, dead, "items", "[1, 2, 3]", setup));
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("items", store, &expr, diags, 4), 0u);

    CS::Value out = CS::Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.evalTracked(exec, &out, deps, &n, diag));

    EXPECT_EQ(out.kind(), CS::Value::Kind::Array);
    EXPECT_EQ(n, CS::kDepsOverflow);
}

TEST(EvalTracked, ALongStringResultIsStillCached) {
    // Строка под правило §2.8 не попадает: мутирующих операций над строками в
    // языке нет, ручка на строку вечно свежая. Обобщив правило на строки, кэш
    // выключили бы ровно там, ради чего он затевался.
    CS::Store store;
    CS::Deferred dead;
    CS::Diagnostic setup;
    ASSERT_TRUE(CS::setVariable(store, dead, "title",
                                "'строка заведомо длиннее пятнадцати байт'",
                                setup));
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("title", store, &expr, diags, 4), 0u);

    CS::Value out = CS::Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    ASSERT_TRUE(expr.evalTracked(exec, &out, deps, &n, diag));

    EXPECT_EQ(out.kind(), CS::Value::Kind::String);
    EXPECT_EQ(n, 1u);
}

TEST(EvalTracked, AFailedEvaluationLeavesNothingToCache) {
    CS::Store store;
    CS::Deferred dead;
    store.setGlobal("n", CS::Value::number(1.0), dead);
    CS::Execution exec(store);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("n.field", store, &expr, diags, 4), 0u);

    CS::Value out = CS::Value::boolean(true);
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    EXPECT_FALSE(expr.evalTracked(exec, &out, deps, &n, diag));

    EXPECT_EQ(n, CS::kDepsOverflow);
    EXPECT_EQ(out.kind(), CS::Value::Kind::Boolean) << "при отказе *out не трогается";
}

TEST(EvalTracked, AUnitFromAnotherStoreIsRefused) {
    CS::Store mine;
    CS::Store other;
    CS::Execution exec(mine);
    CS::Expression expr;
    CS::Diagnostic diags[4];
    ASSERT_EQ(CS::Expression::compile("42", other, &expr, diags, 4), 0u);

    CS::Value out = CS::Value::null();
    CS::Dep deps[CS::kMaxDeps];
    std::uint32_t n = 0;
    CS::Diagnostic diag;
    EXPECT_FALSE(expr.evalTracked(exec, &out, deps, &n, diag));
    EXPECT_EQ(diag.code, CS::ErrorCode::Usage);
}

}  // namespace
