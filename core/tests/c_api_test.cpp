#include <gtest/gtest.h>

#include "chupascript/chupascript.h"

#include <cstring>
#include <string>
#include <string_view>

// Helper: set a global from a ChupaScript literal text
bool setGlobal(ChupaContext* ctx, const std::string& name, const std::string& text) {
    return chupa_context_set(ctx, name.c_str(), name.size(), text.c_str(), text.size());
}

TEST(CApiContext, CreateDestroy) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralString) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "name", "'hello'"));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralNumber) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "count", "42"));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralObject) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "user", "{ 'name': 'John', 'age': 30 }"));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetLiteralFailsOnExpression) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // "1 + 2" is an expression, not a literal — should fail
    EXPECT_FALSE(setGlobal(ctx, "x", "1 + 2"));
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_DATA);
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetBool) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(chupa_context_set_bool(ctx, "flag", 4, true));
    // Значение проверяется вычислением ниже; здесь — что имя принято.
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetNumber) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(chupa_context_set_number(ctx, "pi", 2, 3.14));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, SetString) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(chupa_context_set_string(ctx, "greeting", 8, "world", 5));
    chupa_context_destroy(ctx);
}

TEST(CApiContext, ScalarSettersRejectNamesTheProgramCannotReference) {
    // Проверка та же, что у chupa_context_set: имя обязано быть идентификатором
    // и не быть ключевым словом (docs/grammar.md §4.4, §4.5). Без неё запись
    // удавалась бы, а обратиться к глобальной переменной было бы нельзя.
    struct Case {
        const char* name;
        std::size_t length;
        const char* why;
    };
    const Case cases[] = {
        {"my name", 7, "пробел внутри"},
        {"", 0, "пустое имя"},
        {"1abc", 4, "начинается с цифры"},
        {"true", 4, "ключевое слово"},
        {" state", 6, "ведущий пробел"},
        {"state ", 6, "хвостовой пробел"},
    };

    for (const Case& c : cases) {
        ChupaContext* ctx = chupa_context_create();
        ASSERT_NE(ctx, nullptr) << c.why;

        EXPECT_FALSE(chupa_context_set_bool(ctx, c.name, c.length, true)) << c.why;
        EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_NAME) << c.why;

        EXPECT_FALSE(chupa_context_set_number(ctx, c.name, c.length, 1.0)) << c.why;
        EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_NAME) << c.why;

        EXPECT_FALSE(chupa_context_set_string(ctx, c.name, c.length, "v", 1)) << c.why;
        EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_NAME) << c.why;

        chupa_context_destroy(ctx);
    }
}

TEST(CApiContext, RejectedScalarSetterWritesNothing) {
    // Отказ обязан оставить хранилище нетронутым. У set_string это отдельный
    // риск: строка создаётся в пуле, и проверять имя после создания значило бы
    // оставлять там мусор при каждом отказе.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);

    ASSERT_TRUE(chupa_context_set_number(ctx, "kept", 4, 7.0));
    EXPECT_FALSE(chupa_context_set_string(ctx, "bad name", 8, "x", 1));

    // Уцелевшее имя по-прежнему читается — отказ соседа его не задел.
    ChupaExpression* e = chupa_compile_expression(ctx, "kept", 4);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_DOUBLE_EQ(out, 7.0);

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiContext, RejectedScalarSetterDoesNotNotifyRedraw) {
    // Перерисовка означает «данные изменились». Отказ ничего не изменил.
    // Счётчик едет через user_data, а не через глобальную переменную: тест
    // стоит выше её объявления и не должен от него зависеть.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);

    int redraws = 0;
    chupa_context_on_redraw(ctx, [](ChupaContext*, void* user_data) {
        ++*static_cast<int*>(user_data);
    }, &redraws);

    EXPECT_FALSE(chupa_context_set_bool(ctx, "bad name", 8, true));
    EXPECT_EQ(redraws, 0);

    EXPECT_TRUE(chupa_context_set_bool(ctx, "good", 4, true));
    EXPECT_EQ(redraws, 1);

    chupa_context_destroy(ctx);
}

TEST(CApiContext, DestroyNullIsSafe) {
    chupa_context_destroy(nullptr);
}

TEST(CApiContext, VersionIsReported) {
    EXPECT_STREQ("0.1.0", chupa_version());
}

// ─── Compile + Eval ───

TEST(CApiCompile, CompileExpression) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "42"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    EXPECT_NE(e, nullptr);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiCompile, CompileExpressionFailsOnSyntaxError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ChupaExpression* e = chupa_compile_expression(ctx, "1 +", 3);
    EXPECT_EQ(e, nullptr);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_SYNTAX);
    chupa_context_destroy(ctx);
}

TEST(CApiCompile, CompileExpressionFailsOnUnknownGlobal) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ChupaExpression* e = chupa_compile_expression(ctx, "unknown_var", 11);
    EXPECT_EQ(e, nullptr);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_NAME);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNumber) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "42"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 42.0);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNumberFromExpression) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "10"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x + 5", 5);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 15.0);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalBool) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "10"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x > 5", 5);
    ASSERT_NE(e, nullptr);
    bool out = false;
    EXPECT_EQ(chupa_eval_bool(ctx, e, &out), CHUPA_OK);
    EXPECT_TRUE(out);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalString) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "name", "'hello'"));
    ChupaExpression* e = chupa_compile_expression(ctx, "name", 4);
    ASSERT_NE(e, nullptr);
    const char* bytes = nullptr;
    size_t len = 0;
    EXPECT_EQ(chupa_eval_string_borrowed(ctx, e, &bytes, &len), CHUPA_OK);
    EXPECT_EQ(len, 5u);
    EXPECT_EQ(std::string(bytes, len), "hello");
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNullReturnsChupaNull) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "null"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x", 1);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_NULL);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalNumberOnStringExpressionReturnsError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "name", "'hello'"));
    ChupaExpression* e = chupa_compile_expression(ctx, "name", 4);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_ERROR);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_TYPE);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalMemberAccess) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "user", "{ 'name': 'John', 'age': 30 }"));
    ChupaExpression* e = chupa_compile_expression(ctx, "user.age", 8);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 30.0);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, EvalTernary) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "5"));
    ChupaExpression* e = chupa_compile_expression(ctx, "x > 3 ? 'big' : 'small'", 23);
    ASSERT_NE(e, nullptr);
    const char* bytes = nullptr;
    size_t len = 0;
    EXPECT_EQ(chupa_eval_string_borrowed(ctx, e, &bytes, &len), CHUPA_OK);
    EXPECT_EQ(std::string(bytes, len), "big");
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, SetBoolThenEval) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(chupa_context_set_bool(ctx, "flag", 4, true));
    ChupaExpression* e = chupa_compile_expression(ctx, "flag", 4);
    ASSERT_NE(e, nullptr);
    bool out = false;
    EXPECT_EQ(chupa_eval_bool(ctx, e, &out), CHUPA_OK);
    EXPECT_TRUE(out);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, SetNumberThenEval) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(chupa_context_set_number(ctx, "pi", 2, 3.14));
    ChupaExpression* e = chupa_compile_expression(ctx, "pi", 2);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 3.14);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiEval, SetStringThenEval) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(chupa_context_set_string(ctx, "greeting", 8, "world", 5));
    ChupaExpression* e = chupa_compile_expression(ctx, "greeting", 8);
    ASSERT_NE(e, nullptr);
    const char* bytes = nullptr;
    size_t len = 0;
    EXPECT_EQ(chupa_eval_string_borrowed(ctx, e, &bytes, &len), CHUPA_OK);
    EXPECT_EQ(std::string(bytes, len), "world");
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

// ─── Строковый результат: срез в пул движка, без владения ───
//
// Владеющей обёртки (ChupaString) больше нет. Она существовала ради одной
// гарантии — свободного порядка разрушения, — и за ней никто не пришёл:
// единственный хост копирует байты немедленно и всегда. Два теста, державшие
// ту гарантию (EvalStringOutlivesTheContext и EvalStringSurvivesStoreMutation),
// удалены вместе с ней: проверять неопределённое поведение нечем. Вместо них
// ниже проверяется положительное — что срез настоящий, что копия успевает
// сняться, и что на неудачных исходах выходные параметры не трогаются.

TEST(CApi, EvalStringGivesTheBytesWithoutOwnership) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    std::string_view src = "'привет'";
    ChupaExpression* e = chupa_compile_expression(ctx, src.data(), src.size());
    ASSERT_NE(e, nullptr);

    const char* bytes = nullptr;
    size_t len = 0;
    ASSERT_EQ(chupa_eval_string_borrowed(ctx, e, &bytes, &len), CHUPA_OK);
    ASSERT_NE(bytes, nullptr);
    EXPECT_EQ(std::string(bytes, len), "привет");

    // Освобождать нечего: парной функции нет, и её отсутствие — часть
    // контракта, а не забывчивость теста.
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, EvalStringReturnsTheSameSliceEveryTime) {
    // Срез, а не копия: повторное вычисление того же литерала обязано отдать
    // тот же самый указатель. Копия давала бы каждый раз новый адрес — это
    // единственный способ увидеть снаружи, что аллокации на пути больше нет.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    std::string_view src = "'привет'";
    ChupaExpression* e = chupa_compile_expression(ctx, src.data(), src.size());
    ASSERT_NE(e, nullptr);

    const char* first = nullptr;
    size_t firstLen = 0;
    ASSERT_EQ(chupa_eval_string_borrowed(ctx, e, &first, &firstLen), CHUPA_OK);

    const char* second = nullptr;
    size_t secondLen = 0;
    ASSERT_EQ(chupa_eval_string_borrowed(ctx, e, &second, &secondLen),
              CHUPA_OK);

    EXPECT_EQ(first, second);
    EXPECT_EQ(firstLen, secondLen);

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, EvalStringBytesAreCopiedBeforeTheNextCall) {
    // Окно валидности — до следующего обращения к контексту. Проверить
    // нарушение нечем (это UB), поэтому тест держит сам порядок работы: снять
    // копию сразу, потом сколько угодно шевелить хранилище, потом вычислить
    // заново и получить годный срез снова.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    std::string_view src = "'привет'";
    ChupaExpression* e = chupa_compile_expression(ctx, src.data(), src.size());
    ASSERT_NE(e, nullptr);

    const char* bytes = nullptr;
    size_t len = 0;
    ASSERT_EQ(chupa_eval_string_borrowed(ctx, e, &bytes, &len), CHUPA_OK);
    const std::string copy(bytes, len);  // копия снята немедленно

    // Растим пул текста так, чтобы он заведомо переехал.
    std::string_view name = "filler";
    std::string_view filler = "довольно длинная строка для роста пула";
    for (int i = 0; i < 1000; ++i) {
        ASSERT_TRUE(chupa_context_set_string(ctx, name.data(), name.size(),
                                             filler.data(), filler.size()));
    }
    EXPECT_EQ(copy, "привет");  // копия переезд пережила

    // А сам срез берётся заново — и он снова годен.
    ASSERT_EQ(chupa_eval_string_borrowed(ctx, e, &bytes, &len), CHUPA_OK);
    EXPECT_EQ(std::string(bytes, len), "привет");

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, EvalStringOnNullLeavesOutputsUntouched) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ChupaExpression* e = chupa_compile_expression(ctx, "null", 4);
    ASSERT_NE(e, nullptr);

    // Сторожевые значения: если исход Null действительно не трогает выходные
    // параметры, они переживут вызов неизменными.
    const char* bytes = reinterpret_cast<const char*>(1);
    size_t len = 42;
    EXPECT_EQ(chupa_eval_string_borrowed(ctx, e, &bytes, &len), CHUPA_NULL);
    EXPECT_EQ(bytes, reinterpret_cast<const char*>(1));
    EXPECT_EQ(len, 42u);

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, EvalStringOnNumberIsTypeError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // Корень намеренно НЕ в первом байте: раньше прокладка ставила в ошибку
    // типа хардкод offset = 0 и на выражении вида "42" разницы было бы не
    // видно. Теперь смещение ставит ядро — и оно настоящее.
    std::string_view src = "1 + 41";
    ChupaExpression* e = chupa_compile_expression(ctx, src.data(), src.size());
    ASSERT_NE(e, nullptr);

    const char* bytes = reinterpret_cast<const char*>(1);
    size_t len = 42;
    EXPECT_EQ(chupa_eval_string_borrowed(ctx, e, &bytes, &len), CHUPA_ERROR);
    EXPECT_EQ(bytes, reinterpret_cast<const char*>(1));
    EXPECT_EQ(len, 42u);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_TYPE);
    EXPECT_EQ(chupa_context_error_offset(ctx), 2u);  // '+', корень выражения

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApi, EvalStringOnEmptyStringIsOkAndNotNull) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    std::string_view src = "''";
    ChupaExpression* e = chupa_compile_expression(ctx, src.data(), src.size());
    ASSERT_NE(e, nullptr);

    const char* bytes = nullptr;
    size_t len = 1;
    // не null
    ASSERT_EQ(chupa_eval_string_borrowed(ctx, e, &bytes, &len), CHUPA_OK);
    EXPECT_EQ(len, 0u);
    // Указатель при этом не обещан: у пустой строки в пуле нечего показывать,
    // и раньше непустым он был лишь потому, что std::string::c_str() всегда
    // возвращает адрес своего нуля. Годен любой — читать по нему нечего.

    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

// ─── Run ───

TEST(CApiRun, RunScriptSetsVariable) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "state", "{ 'count': 0 }"));
    ChupaScript* s = chupa_compile_script(ctx, "state.count = 42;", 17);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(chupa_run(ctx, s));
    // Verify via eval
    ChupaExpression* e = chupa_compile_expression(ctx, "state.count", 11);
    ASSERT_NE(e, nullptr);
    double out = 0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_EQ(out, 42.0);
    chupa_expression_destroy(e);
    chupa_script_destroy(s);
    chupa_context_destroy(ctx);
}

TEST(CApiRun, RunScriptWithMemberAccess) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "user", "{ 'name': 'old' }"));
    ChupaScript* s = chupa_compile_script(ctx, "user.name = 'new';", 18);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(chupa_run(ctx, s));
    ChupaExpression* e = chupa_compile_expression(ctx, "user.name", 9);
    ASSERT_NE(e, nullptr);
    const char* bytes = nullptr;
    size_t len = 0;
    EXPECT_EQ(chupa_eval_string_borrowed(ctx, e, &bytes, &len), CHUPA_OK);
    EXPECT_EQ(std::string(bytes, len), "new");
    chupa_expression_destroy(e);
    chupa_script_destroy(s);
    chupa_context_destroy(ctx);
}

TEST(CApiRun, RunScriptFailsOnTypeError) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "0"));
    // x is a number, can't assign a string member to it
    ChupaScript* s = chupa_compile_script(ctx, "x.name = 'bad';", 15);
    ASSERT_NE(s, nullptr);
    EXPECT_FALSE(chupa_run(ctx, s));
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_TYPE);
    chupa_script_destroy(s);
    chupa_context_destroy(ctx);
}

// ─── Error accessors ───

TEST(CApiError, NoErrorAfterSuccess) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "42"));
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_NONE);
    chupa_context_destroy(ctx);
}

TEST(CApiError, SyntaxErrorDetails) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_compile_expression(ctx, "1 +", 3);
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_SYNTAX);
    size_t len = 0;
    const char* msg = chupa_context_error(ctx, &len);
    ASSERT_NE(msg, nullptr);
    EXPECT_GT(len, 0u);
    // Offset should point somewhere in the source
    EXPECT_GE(chupa_context_error_offset(ctx), 0u);
    chupa_context_destroy(ctx);
}

TEST(CApiError, DataErrorOnExpressionAsData) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // "1 + 2" is an expression, not a literal — setVariable rejects it
    EXPECT_FALSE(chupa_context_set(ctx, "x", 1, "1 + 2", 5));
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_DATA);
    chupa_context_destroy(ctx);
}

// ─── Redraw ───

namespace {
int g_redrawCount = 0;
ChupaContext* g_lastRedrawCtx = nullptr;

void testRedrawListener(ChupaContext* ctx, void* /*user_data*/) {
    g_redrawCount++;
    g_lastRedrawCtx = ctx;
}
}

TEST(CApiRedraw, FiresAfterSet) {
    g_redrawCount = 0;
    g_lastRedrawCtx = nullptr;
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_on_redraw(ctx, testRedrawListener, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "42"));
    EXPECT_EQ(g_redrawCount, 1);
    EXPECT_EQ(g_lastRedrawCtx, ctx);
    chupa_context_destroy(ctx);
}

TEST(CApiRedraw, FiresAfterRun) {
    g_redrawCount = 0;
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "state", "{ 'count': 0 }"));
    // set already fired redraw; reset counter
    g_redrawCount = 0;
    chupa_context_on_redraw(ctx, testRedrawListener, nullptr);
    ChupaScript* s = chupa_compile_script(ctx, "state.count = 1;", 16);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(chupa_run(ctx, s));
    EXPECT_EQ(g_redrawCount, 1);
    chupa_script_destroy(s);
    chupa_context_destroy(ctx);
}

TEST(CApiRedraw, NoFireWithoutListener) {
    g_redrawCount = 0;
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    EXPECT_TRUE(setGlobal(ctx, "x", "42"));
    EXPECT_EQ(g_redrawCount, 0);
    chupa_context_destroy(ctx);
}

TEST(CApiRedraw, UserDataPassedThrough) {
    g_redrawCount = 0;
    int marker = 42;
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    chupa_context_on_redraw(ctx, [](ChupaContext* /*ctx*/, void* user_data) {
        g_redrawCount++;
        EXPECT_EQ(*static_cast<int*>(user_data), 42);
    }, &marker);
    EXPECT_TRUE(chupa_context_set_bool(ctx, "flag", 4, true));
    EXPECT_EQ(g_redrawCount, 1);
    chupa_context_destroy(ctx);
}

// ─── Ownership of compiled units ───

TEST(CApi, SecondCompileDoesNotBreakTheFirst) {
    // Ровно тот сценарий, на котором ломался UAF-3 (B39): оба исходника
    // короче 23 байт, то есть оба попадали в SSO.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(chupa_context_set(ctx, "a", 1, "2", 1));
    ASSERT_TRUE(chupa_context_set(ctx, "b", 1, "3", 1));

    ChupaExpression* first = chupa_compile_expression(ctx, "a + b", 5);
    ASSERT_NE(first, nullptr);
    ChupaExpression* second = chupa_compile_expression(ctx, "a * b", 5);
    ASSERT_NE(second, nullptr);

    double out = 0.0;
    EXPECT_EQ(chupa_eval_number(ctx, first, &out), CHUPA_OK);
    EXPECT_DOUBLE_EQ(out, 5.0);

    chupa_expression_destroy(first);
    chupa_expression_destroy(second);
    chupa_context_destroy(ctx);
}

TEST(CApi, ExpressionOutlivesNothingAndIsFreedByHost) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // Компиляция в цикле больше не растит контекст по построению.
    for (int i = 0; i < 1000; ++i) {
        ChupaExpression* e = chupa_compile_expression(ctx, "1 + 1", 5);
        ASSERT_NE(e, nullptr);
        chupa_expression_destroy(e);
    }
    chupa_context_destroy(ctx);
}

TEST(CApi, FailedCompileLeavesNothingBehind) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    for (int i = 0; i < 1000; ++i) {
        EXPECT_EQ(chupa_compile_expression(ctx, "a..b", 4), nullptr);
    }
    EXPECT_EQ(chupa_context_error_code(ctx), CHUPA_ERR_SYNTAX);
    chupa_context_destroy(ctx);
}

TEST(CApi, DestroyAcceptsNull) {
    chupa_expression_destroy(nullptr);
    chupa_script_destroy(nullptr);
}

TEST(CApi, UnitOutlivesTheContextItWasCompiledAgainst) {
    // chupascript.h обещает наружу: «A unit may be destroyed in any order
    // relative to the context it was compiled against». Здесь порядок
    // обратный обычному — сначала умирает контекст.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(chupa_context_set(ctx, "obj", 3, "{ 'n': 1 }", 10));

    ChupaExpression* e = chupa_compile_expression(ctx, "obj.n + 1", 9);
    ASSERT_NE(e, nullptr);
    ChupaScript* s = chupa_compile_script(ctx, "obj.n = 7;", 10);
    ASSERT_NE(s, nullptr);

    chupa_context_destroy(ctx);
    chupa_expression_destroy(e);
    chupa_script_destroy(s);
}

TEST(CApi, ScriptIsOwnedByHost) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    // Цель присваивания обязана быть Member или Index (docs/semantics.md
    // §7.2), голый идентификатор язык отвергает — отсюда объект.
    ASSERT_TRUE(chupa_context_set(ctx, "obj", 3, "{ 'n': 1 }", 10));

    ChupaScript* s = chupa_compile_script(ctx, "obj.n = obj.n + 1;", 18);
    ASSERT_NE(s, nullptr);
    EXPECT_TRUE(chupa_run(ctx, s));
    chupa_script_destroy(s);

    ChupaExpression* e = chupa_compile_expression(ctx, "obj.n", 5);
    ASSERT_NE(e, nullptr);
    double out = 0.0;
    EXPECT_EQ(chupa_eval_number(ctx, e, &out), CHUPA_OK);
    EXPECT_DOUBLE_EQ(out, 2.0);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

// ─── выдача агрегата ───
//
// То, ради чего переделывалась модель памяти. До неё такого API быть не могло:
// значение было индексом в пулы хранилища, и ни прочитать его без контекста,
// ни пережить контекст оно не умело.

namespace {

/// Компилирует выражение; требует успеха.
ChupaExpression* compileIn(ChupaContext* ctx, std::string_view source) {
    ChupaExpression* e =
        chupa_compile_expression(ctx, source.data(), source.size());
    EXPECT_NE(e, nullptr) << chupa_context_error(ctx, nullptr);
    return e;
}

std::string_view stringOf(ChupaValue v) {
    const char* bytes = nullptr;
    size_t len = 0;
    chupa_value_string_borrowed(v, &bytes, &len);
    return std::string_view(bytes, len);
}

ChupaValue evalValue(ChupaContext* ctx, std::string_view source) {
    ChupaExpression* e = compileIn(ctx, source);
    ChupaValue out{};
    EXPECT_EQ(chupa_eval_value(ctx, e, &out), CHUPA_OK)
        << chupa_context_error(ctx, nullptr);
    chupa_expression_destroy(e);
    return out;
}

}  // namespace

TEST(CApiValue, ScalarsComeThroughUntouched) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);

    const ChupaValue n = evalValue(ctx, "42");
    EXPECT_EQ(chupa_value_kind(n), CHUPA_KIND_NUMBER);
    EXPECT_DOUBLE_EQ(chupa_value_number(n), 42.0);

    const ChupaValue b = evalValue(ctx, "1 < 2");
    EXPECT_EQ(chupa_value_kind(b), CHUPA_KIND_BOOL);
    EXPECT_TRUE(chupa_value_bool(b));

    chupa_context_destroy(ctx);
}

TEST(CApiValue, NullIsItsOwnOutcome) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(setGlobal(ctx, "user", "{'name': 'Вася'}"));

    ChupaExpression* e = compileIn(ctx, "user.missing");
    ChupaValue out{};
    // Отдельный исход, как у типизированных вычислений: иначе два пути к
    // одному выражению отвечали бы на отсутствие по-разному.
    EXPECT_EQ(chupa_eval_value(ctx, e, &out), CHUPA_NULL);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiValue, ArrayIsWalkedWithoutTheContext) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(setGlobal(ctx, "items", "[10, 'два', 30]"));

    const ChupaValue items = evalValue(ctx, "items");
    ASSERT_EQ(chupa_value_kind(items), CHUPA_KIND_ARRAY);
    ASSERT_EQ(chupa_array_count(items), 3u);
    EXPECT_DOUBLE_EQ(chupa_value_number(chupa_array_at(items, 0)), 10.0);
    EXPECT_EQ(stringOf(chupa_array_at(items, 1)), "два");
    // За концом — null, а не отказ: правило docs/semantics.md §6.1 доходит и
    // до границы.
    EXPECT_EQ(chupa_value_kind(chupa_array_at(items, 3)), CHUPA_KIND_NULL);

    chupa_context_destroy(ctx);
}

TEST(CApiValue, ObjectIsReadByKeyAndByPosition) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    ASSERT_TRUE(setGlobal(ctx, "user", "{'age': 30, 'name': 'Вася'}"));

    const ChupaValue user = evalValue(ctx, "user");
    ASSERT_EQ(chupa_value_kind(user), CHUPA_KIND_OBJECT);
    ASSERT_EQ(chupa_object_count(user), 2u);
    EXPECT_EQ(stringOf(chupa_object_get(user, "name", 4)), "Вася");
    EXPECT_DOUBLE_EQ(chupa_value_number(chupa_object_get(user, "age", 3)), 30.0);
    EXPECT_EQ(chupa_value_kind(chupa_object_get(user, "нет", 6)),
              CHUPA_KIND_NULL);

    const char* key = nullptr;
    size_t len = 0;
    chupa_object_key_at(user, 0, &key, &len);
    EXPECT_EQ(std::string_view(key, len), "age");
    chupa_object_key_at(user, 9, &key, &len);
    EXPECT_EQ(len, 0u);

    chupa_context_destroy(ctx);
}

TEST(CApiValue, ComputedStringIsMaterialisedSoItCanBeRetained) {
    // format строит строку в арене операции. Не материализуй её выдача — retain
    // оказался бы молчаливой ложью, и хост прочитал бы освобождённую арену.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);

    ChupaExpression* e = compileIn(ctx, "format('${}-${}', 1, 2)");
    ChupaValue text{};
    ASSERT_EQ(chupa_eval_value(ctx, e, &text), CHUPA_OK);
    chupa_value_retain(text);
    chupa_expression_destroy(e);

    // Любая следующая операция сбрасывает арену целиком.
    ChupaExpression* other = compileIn(ctx, "1 + 1");
    double ignored = 0.0;
    ASSERT_EQ(chupa_eval_number(ctx, other, &ignored), CHUPA_OK);
    chupa_expression_destroy(other);

    EXPECT_EQ(stringOf(text), "1-2");
    chupa_value_release(text);
    chupa_context_destroy(ctx);
}

TEST(CApiValue, UnretainedValueIsGoneAfterTheNextOperation) {
    // Обратная сторона того же правила: без retain выдача одалживается, и
    // ближайшая операция её отпускает. Проверяется счётом живых узлов —
    // читать освобождённое здесь и было бы той самой ошибкой.
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);

    ChupaExpression* e = compileIn(ctx, "[1, 2, 3]");
    ChupaValue kept{};
    ASSERT_EQ(chupa_eval_value(ctx, e, &kept), CHUPA_OK);
    chupa_value_retain(kept);

    ChupaValue lent{};
    ASSERT_EQ(chupa_eval_value(ctx, e, &lent), CHUPA_OK);
    // Вторая выдача не удержана; третья операция её отпустит, а удержанная
    // останется.
    ASSERT_EQ(chupa_eval_value(ctx, e, &lent), CHUPA_OK);

    EXPECT_EQ(chupa_array_count(kept), 3u);
    chupa_value_release(kept);
    chupa_expression_destroy(e);
    chupa_context_destroy(ctx);
}

TEST(CApiValue, RetainedObjectOutlivesTheContext) {
    // Главное обещание модели: удержанный агрегат читается, когда контекста,
    // хранилища и таблицы имён у него уже нет.
    ChupaValue user{};
    {
        ChupaContext* ctx = chupa_context_create();
        ASSERT_NE(ctx, nullptr);
        ASSERT_TRUE(setGlobal(ctx, "user",
                              "{'name': 'Вася', 'tags': ['a', 'b']}"));
        user = evalValue(ctx, "user");
        chupa_value_retain(user);
        chupa_context_destroy(ctx);
    }

    ASSERT_EQ(chupa_value_kind(user), CHUPA_KIND_OBJECT);
    EXPECT_EQ(stringOf(chupa_object_get(user, "name", 4)), "Вася");
    const ChupaValue tags = chupa_object_get(user, "tags", 4);
    ASSERT_EQ(chupa_value_kind(tags), CHUPA_KIND_ARRAY);
    ASSERT_EQ(chupa_array_count(tags), 2u);
    EXPECT_EQ(stringOf(chupa_array_at(tags, 1)), "b");

    chupa_value_release(user);
}

TEST(CApiValue, NestedValueCanBeKeptWithoutItsParent) {
    // Вложенное значение одалживается у родителя, но своя ссылка делает его
    // самостоятельным — иначе хост не мог бы удержать одну ячейку списка.
    ChupaValue tags{};
    {
        ChupaContext* ctx = chupa_context_create();
        ASSERT_NE(ctx, nullptr);
        ASSERT_TRUE(setGlobal(ctx, "user", "{'tags': ['a', 'b']}"));
        const ChupaValue user = evalValue(ctx, "user");
        tags = chupa_object_get(user, "tags", 4);
        chupa_value_retain(tags);
        chupa_context_destroy(ctx);
    }
    ASSERT_EQ(chupa_array_count(tags), 2u);
    EXPECT_EQ(stringOf(chupa_array_at(tags, 0)), "a");
    chupa_value_release(tags);
}

TEST(CApiValue, RetainAndReleaseAreNoOpsOnScalars) {
    ChupaContext* ctx = chupa_context_create();
    ASSERT_NE(ctx, nullptr);
    const ChupaValue n = evalValue(ctx, "42");
    chupa_value_retain(n);
    chupa_value_release(n);
    chupa_value_release(n);   // ссылок нет, отпускать нечего
    EXPECT_DOUBLE_EQ(chupa_value_number(n), 42.0);
    chupa_context_destroy(ctx);
}
