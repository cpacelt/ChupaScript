#include "check.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include "ast.hpp"
#include "compile.hpp"
#include "context.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "parser.hpp"

namespace {

using CS::Ast;
using CS::Context;
using CS::Diagnostic;

/// Кладёт переменную с данными.
void put(Context &ctx, std::string_view name, std::string_view text) {
    Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(ctx, name, text, diag)) << diag.message;
}

/// Компилирует выражение и возвращает найденные ошибки.
std::vector<Diagnostic> checkExpr(Context &ctx, std::string_view text,
                                  std::uint32_t capacity = 8) {
    Ast ast;
    std::vector<Diagnostic> found(capacity);
    const std::uint32_t count = CS::compileExpression(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx,
        found.data(), capacity);
    found.resize(std::min<std::uint32_t>(count, capacity));
    return found;
}

/// То же для скрипта.
std::vector<Diagnostic> checkScript(Context &ctx, std::string_view text,
                                    std::uint32_t capacity = 8) {
    Ast ast;
    std::vector<Diagnostic> found(capacity);
    const std::uint32_t count = CS::compileScript(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx,
        found.data(), capacity);
    found.resize(std::min<std::uint32_t>(count, capacity));
    return found;
}

TEST(Check, CleanExpressionPasses) {
    Context ctx;
    put(ctx, "items", "[1, 2, 3]");
    EXPECT_TRUE(checkExpr(ctx, "count(items)").empty());
    EXPECT_TRUE(checkExpr(ctx, "items[0] + 1").empty());
}

TEST(Check, UnknownFunctionIsACompileError) {
    Context ctx;
    put(ctx, "items", "[1]");
    const auto found = checkExpr(ctx, "cnt(items)");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].code, CS::ErrorCode::Name);
    // Смещение указывает на сам вызов: узел Call несёт смещение своего имени
    // (core/src/ast.cpp, Ast::call), а "cnt" стоит в самом начале строки. Для
    // валидатора в CLI смещение — единственное, что можно показать автору
    // макета, поэтому проход обязан давать его верным, а не только код.
    EXPECT_EQ(found[0].offset, 0u);
}

TEST(Check, WrongArgumentCountIsACompileError) {
    Context ctx;
    put(ctx, "items", "[1]");
    // docs/semantics.md §8: min берёт ровно два, count — ровно один.
    EXPECT_EQ(checkExpr(ctx, "min(1)").size(), 1u);
    EXPECT_EQ(checkExpr(ctx, "min(1, 2, 3)").size(), 1u);
    EXPECT_EQ(checkExpr(ctx, "count()").size(), 1u);
    EXPECT_EQ(checkExpr(ctx, "count(items, 1)").size(), 1u);
    // format вариадичен: и один, и пять аргументов допустимы по числу.
    EXPECT_TRUE(checkExpr(ctx, "format('нет подстановок')").empty());
}

TEST(Check, ValueReturningCallInStatementPositionIsAnError) {
    Context ctx;
    put(ctx, "items", "[1]");
    put(ctx, "state", "{'n': 0}");
    // docs/grammar.md §6.1: результат не используется.
    EXPECT_EQ(checkScript(ctx, "count(items);").size(), 1u);
    // А push значения не возвращает — он в позиции стейтмента на месте.
    EXPECT_TRUE(checkScript(ctx, "push(items, 1);").empty());
    // И использованный результат тоже на месте.
    EXPECT_TRUE(checkScript(ctx, "state.n = count(items);").empty());
}

TEST(Check, UsingTheResultOfAVoidBuiltinIsAnError) {
    Context ctx;
    put(ctx, "items", "[1]");
    put(ctx, "state", "{'n': 0}");
    // docs/grammar.md §6.2.
    EXPECT_EQ(checkScript(ctx, "state.n = push(items, 1);").size(), 1u);
    EXPECT_EQ(checkScript(ctx, "state.n = push(items, 1) + 1;").size(), 1u);
    EXPECT_EQ(checkScript(ctx, "push(items, pop(items));").size(), 1u);
    // Вложенный вызов, возвращающий значение, — не ошибка.
    EXPECT_TRUE(checkScript(ctx, "push(items, count(items));").empty());
}

TEST(Check, FormatPlaceholderMismatchIsCaughtWhenTheTemplateIsLiteral) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // docs/semantics.md §8.8: при литеральном шаблоне несовпадение — ошибка
    // компиляции.
    EXPECT_EQ(checkExpr(ctx, "format('${} и ${}', user.name)").size(), 1u);
    EXPECT_EQ(checkExpr(ctx, "format('нет', user.name)").size(), 1u);
    EXPECT_TRUE(checkExpr(ctx, "format('${}', user.name)").empty());
    // $${} плейсхолдером не является, поэтому аргументов не требует.
    EXPECT_TRUE(checkExpr(ctx, "format('цена $${}')").empty());
}

TEST(Check, VoidBuiltinAsAWholeExpressionIsRejected) {
    Context ctx;
    put(ctx, "items", "[1, 2]");
    // Корень дерева выражения — вызов, и его результат есть значение props,
    // то есть употреблён. Void-функция здесь запрещена, иначе выражение
    // изменило бы данные, что docs/semantics.md §3.2 обещает невозможным.
    EXPECT_EQ(checkExpr(ctx, "pop(items)").size(), 1u);
    EXPECT_EQ(checkExpr(ctx, "push(items, 3)").size(), 1u);
    // Возвращающая значение в той же позиции — на месте.
    EXPECT_TRUE(checkExpr(ctx, "count(items)").empty());
}

TEST(Check, FormatWithANonLiteralTemplateIsNotCheckedHere) {
    Context ctx;
    put(ctx, "user", "{'tpl': '${}'}");
    // Шаблон не литерал — сверять нечего, проверка уходит в выполнение.
    EXPECT_TRUE(checkExpr(ctx, "format(user.tpl, 1, 2, 3)").empty());
}

TEST(Check, FormatWithEscapesInTheTemplateIsStillChecked) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // Escape-последовательности лексера не дают ни $, ни { , ни } , поэтому
    // границы плейсхолдеров от декодирования не зависят и шаблон проверяется
    // наравне с прочими.
    EXPECT_EQ(checkExpr(ctx, "format('строка\\nи ${}')").size(), 1u);
    EXPECT_TRUE(checkExpr(ctx, "format('строка\\nи ${}', user.name)").empty());
}

TEST(Check, AssigningToANameIsACompileError) {
    Context ctx;
    put(ctx, "state", "{'n': 0}");
    // Переезд из вычислителя: docs/semantics.md §7.2, закрывает B27.
    ASSERT_EQ(checkScript(ctx, "state = 1;").size(), 1u);
    EXPECT_EQ(checkScript(ctx, "state = 1;")[0].code, CS::ErrorCode::Name);
    EXPECT_TRUE(checkScript(ctx, "state.n = 1;").empty());
}

TEST(Check, UnknownNameIsACompileError) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // Переезд из выполнения в компиляцию: спека §5.5.
    ASSERT_EQ(checkExpr(ctx, "usre.name").size(), 1u);
    EXPECT_EQ(checkExpr(ctx, "usre.name")[0].code, CS::ErrorCode::Name);
    EXPECT_TRUE(checkExpr(ctx, "user.name").empty());
}

TEST(Check, UnknownNameInAssignmentTargetIsACompileError) {
    Context ctx;
    put(ctx, "state", "{'a': 1}");
    // Переезд из core/tests/eval_test.cpp (EvalAssign.UnknownNameIsAnError):
    // неизвестный корень внутри цели присваивания ловится тем же узлом
    // Identifier, что и в обычном выражении — здесь важно лишь то, что
    // стейтменты проверяются наравне с выражениями.
    ASSERT_EQ(checkScript(ctx, "usre.a = 1;").size(), 1u);
    EXPECT_EQ(checkScript(ctx, "usre.a = 1;")[0].code, CS::ErrorCode::Name);
    EXPECT_TRUE(checkScript(ctx, "state.a = 1;").empty());
}

TEST(Check, MisspelledKeyIsNotAnErrorAtAnyStage) {
    Context ctx;
    put(ctx, "user", "{'name': 'Вася'}");
    // Асимметрия спеки §5.5: имя переменной проверяется, ключ — нет и никогда.
    // Обе половины обязательны, иначе правило вырождается в одностороннее.
    EXPECT_TRUE(checkExpr(ctx, "user.nmae").empty());
}

TEST(Check, NamesAreCheckedAgainstCompositionNotValues) {
    Context ctx;
    // Валидатору достаточно состава имён: значения не нужны.
    ctx.setRoot("user", CS::Value::null());
    EXPECT_TRUE(checkExpr(ctx, "user.profile.city").empty());
    EXPECT_EQ(checkExpr(ctx, "usre.profile").size(), 1u);
}

TEST(Check, AllErrorsAreReportedNotJustTheFirst) {
    Context ctx;
    put(ctx, "items", "[1]");
    // Смысл буфера вместо одного Diagnostic: валидатор показывает всё сразу.
    const auto found = checkScript(ctx, "cnt(items); min(1); usre.a = 1;");
    EXPECT_GE(found.size(), 3u);
}

TEST(Check, CountExceedsCapacityWhenTheBufferIsSmall) {
    Context ctx;
    put(ctx, "items", "[1]");
    Ast ast;
    Diagnostic one;
    const std::string_view text = "cnt(items); min(1); max(2);";
    const std::uint32_t count = CS::compileScript(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, ctx, &one, 1);
    // Нашлось больше, чем поместилось: вызывающий об этом узнаёт.
    EXPECT_GT(count, 1u);
    EXPECT_EQ(one.code, CS::ErrorCode::Name);
}

TEST(Check, SyntaxErrorGivesExactlyOne) {
    Context ctx;
    // Парсер останавливается на первой; проверки до негодного дерева не идут.
    const auto found = checkExpr(ctx, "1 +");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].code, CS::ErrorCode::Syntax);
}

TEST(Check, CleanTreeIsMarkedAndFaultyIsNot) {
    Context ctx;
    put(ctx, "items", "[1]");
    Ast clean;
    Diagnostic buffer[4];
    const std::string_view good = "count(items)";
    EXPECT_EQ(CS::compileExpression(good.data(),
                                    static_cast<std::uint32_t>(good.size()),
                                    clean, ctx, buffer, 4),
              0u);
    EXPECT_TRUE(clean.isChecked());

    Ast faulty;
    const std::string_view bad = "cnt(items)";
    EXPECT_GT(CS::compileExpression(bad.data(),
                                    static_cast<std::uint32_t>(bad.size()),
                                    faulty, ctx, buffer, 4),
              0u);
    EXPECT_FALSE(faulty.isChecked());
}

TEST(Check, ResetClearsTheMark) {
    Context ctx;
    put(ctx, "items", "[1]");
    Ast ast;
    Diagnostic buffer[4];
    const std::string_view good = "count(items)";
    CS::compileExpression(good.data(), static_cast<std::uint32_t>(good.size()),
                          ast, ctx, buffer, 4);
    ASSERT_TRUE(ast.isChecked());
    // Повторный разбор выбрасывает дерево — отметка обязана уйти с ним.
    const std::string_view other = "1";
    ASSERT_TRUE(CS::parseExpression(
        other.data(), static_cast<std::uint32_t>(other.size()), ast, buffer[0]));
    EXPECT_FALSE(ast.isChecked());
}

TEST(Check, CountsWithoutABufferAtAll) {
    Context ctx;
    put(ctx, "items", "[1]");
    Ast ast;
    const std::string_view text = "cnt(items); min(1);";
    // Ни ёмкости, ни буфера: счёт всё равно ведётся, записи не происходит.
    EXPECT_GT(CS::compileScript(text.data(),
                                static_cast<std::uint32_t>(text.size()), ast,
                                ctx, nullptr, 0),
              0u);
    Diagnostic unused;
    EXPECT_GT(CS::compileScript(text.data(),
                                static_cast<std::uint32_t>(text.size()), ast,
                                ctx, &unused, 0),
              0u);
    EXPECT_EQ(unused.code, CS::ErrorCode::None);
}

}  // namespace
