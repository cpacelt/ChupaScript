#include "echo.hpp"

#include <sstream>

#include <gtest/gtest.h>

#include "context.hpp"
#include "diagnostic.hpp"
#include "script.hpp"

namespace {

/// Compiles and runs source, asserting no diagnostic on either step — the
/// same round trip cli/main.cpp's runScriptSource drives.
void runScript(CS::Context &ctx, std::string_view source) {
    CS::Script script;
    CS::Diagnostic diags[1];
    ASSERT_EQ(ctx.compileScript(source, &script, diags, 1), 0u)
        << diags[0].message;
    CS::Diagnostic diag;
    ASSERT_TRUE(ctx.run(script, diag)) << diag.message;
}

TEST(RegisterEcho, PrintsItsArgument) {
    CS::Context ctx;
    std::ostringstream out;
    chupa::registerEcho(ctx, out);

    runScript(ctx, "echo('привет');");
    EXPECT_EQ(out.str(), "привет\n");
}

/// Каждый вызов дописывает свою строку: echo зовётся ради эффекта, а не
/// возвращает значение, которое можно было бы проверить как-то ещё.
TEST(RegisterEcho, EachCallAppendsALine) {
    CS::Context ctx;
    std::ostringstream out;
    chupa::registerEcho(ctx, out);

    runScript(ctx, "echo('a'); echo('b');");
    EXPECT_EQ(out.str(), "a\nb\n");
}

/// echo объявлена без CHUPA_FN_PURE — вызвать её выражением нельзя, только
/// стейтментом скрипта (docs §2.2). Отказ приходит НЕ от того, что имя не
/// нашлось: `echo` находится там же, где и в скрипте (resolveCallee одна на
/// оба режима), и отвергается двумя правилами разом — §6.3 «грязную функцию
/// из выражения звать нельзя» и §6.2 «результат Void употреблять нельзя»,
/// потому что корень выражения и есть его значение. Поэтому сверяются коды и
/// тексты обоих: проверка на «ошибок не ноль» прошла бы одинаково и при
/// ненайденном имени, а это другой механизм.
TEST(RegisterEcho, NotCallableAsAnExpression) {
    CS::Context ctx;
    std::ostringstream out;
    chupa::registerEcho(ctx, out);

    CS::Expression expr;
    CS::Diagnostic diags[2];
    ASSERT_EQ(ctx.compileExpression("echo('привет')", &expr, diags, 2), 2u);
    EXPECT_EQ(diags[0].code, CS::ErrorCode::Usage);
    EXPECT_STREQ(diags[0].message,
                 "impure function cannot be called from an expression");
    EXPECT_EQ(diags[1].code, CS::ErrorCode::Name);
    EXPECT_STREQ(diags[1].message, "function does not return a value");
}

}  // namespace
