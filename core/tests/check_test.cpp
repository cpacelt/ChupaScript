#include "check.hpp"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdint>
#include <string_view>
#include <vector>

#include "ast.hpp"
#include "compile.hpp"
#include "data.hpp"
#include "diagnostic.hpp"
#include "host.hpp"
#include "host_fixture.hpp"
#include "parser.hpp"
#include "store.hpp"

namespace {

using CS::Ast;
using CS::Store;
using CS::Diagnostic;

/// Кладёт переменную с данными.
void put(Store &store, std::string_view name, std::string_view text) {
    CS::Deferred dead;
    Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, dead, name, text, diag)) << diag.message;
}

/// Компилирует выражение и возвращает найденные ошибки.
std::vector<Diagnostic> checkExpr(Store &store, std::string_view text,
                                  std::uint32_t capacity = 8,
                                  const CS::HostTable *hosts = nullptr) {
    Ast ast;
    std::vector<Diagnostic> found(capacity);
    const std::uint32_t count = CS::compileExpression(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, store,
        found.data(), capacity, hosts);
    found.resize(std::min<std::uint32_t>(count, capacity));
    return found;
}

/// То же для скрипта.
std::vector<Diagnostic> checkScript(Store &store, std::string_view text,
                                    std::uint32_t capacity = 8,
                                    const CS::HostTable *hosts = nullptr) {
    Ast ast;
    std::vector<Diagnostic> found(capacity);
    const std::uint32_t count = CS::compileScript(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, store,
        found.data(), capacity, hosts);
    found.resize(std::min<std::uint32_t>(count, capacity));
    return found;
}

TEST(Check, CleanExpressionPasses) {
    Store store;
    put(store, "items", "[1, 2, 3]");
    EXPECT_TRUE(checkExpr(store, "count(items)").empty());
    EXPECT_TRUE(checkExpr(store, "items[0] + 1").empty());
}

TEST(Check, UnknownFunctionIsACompileError) {
    Store store;
    put(store, "items", "[1]");
    const auto found = checkExpr(store, "cnt(items)");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].code, CS::ErrorCode::Name);
    // Смещение указывает на сам вызов: узел Call несёт смещение своего имени
    // (core/src/ast.cpp, Ast::call), а "cnt" стоит в самом начале строки. Для
    // валидатора в CLI смещение — единственное, что можно показать автору
    // макета, поэтому проход обязан давать его верным, а не только код.
    EXPECT_EQ(found[0].offset, 0u);
}

TEST(Check, WrongArgumentCountIsACompileError) {
    Store store;
    put(store, "items", "[1]");
    // docs/semantics.md §8: min берёт ровно два, count — ровно один.
    EXPECT_EQ(checkExpr(store, "min(1)").size(), 1u);
    EXPECT_EQ(checkExpr(store, "min(1, 2, 3)").size(), 1u);
    EXPECT_EQ(checkExpr(store, "count()").size(), 1u);
    EXPECT_EQ(checkExpr(store, "count(items, 1)").size(), 1u);
    // format вариадичен: и один, и пять аргументов допустимы по числу.
    EXPECT_TRUE(checkExpr(store, "format('нет подстановок')").empty());
}

TEST(Check, ValueReturningCallInStatementPositionIsAnError) {
    Store store;
    put(store, "items", "[1]");
    put(store, "state", "{'n': 0}");
    // docs/grammar.md §6.1: результат не используется.
    EXPECT_EQ(checkScript(store, "count(items);").size(), 1u);
    // А push значения не возвращает — он в позиции стейтмента на месте.
    EXPECT_TRUE(checkScript(store, "push(items, 1);").empty());
    // И использованный результат тоже на месте.
    EXPECT_TRUE(checkScript(store, "state.n = count(items);").empty());
}

TEST(Check, UsingTheResultOfAVoidBuiltinIsAnError) {
    Store store;
    put(store, "items", "[1]");
    put(store, "state", "{'n': 0}");
    // docs/grammar.md §6.2.
    EXPECT_EQ(checkScript(store, "state.n = push(items, 1);").size(), 1u);
    EXPECT_EQ(checkScript(store, "state.n = push(items, 1) + 1;").size(), 1u);
    EXPECT_EQ(checkScript(store, "push(items, pop(items));").size(), 1u);
    // Вложенный вызов, возвращающий значение, — не ошибка.
    EXPECT_TRUE(checkScript(store, "push(items, count(items));").empty());
}

TEST(Check, FormatPlaceholderMismatchIsCaughtWhenTheTemplateIsLiteral) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    // docs/semantics.md §8.8: при литеральном шаблоне несовпадение — ошибка
    // компиляции.
    EXPECT_EQ(checkExpr(store, "format('${} и ${}', user.name)").size(), 1u);
    EXPECT_EQ(checkExpr(store, "format('нет', user.name)").size(), 1u);
    EXPECT_TRUE(checkExpr(store, "format('${}', user.name)").empty());
    // $${} плейсхолдером не является, поэтому аргументов не требует.
    EXPECT_TRUE(checkExpr(store, "format('цена $${}')").empty());
}

TEST(Check, VoidBuiltinAsAWholeExpressionIsRejected) {
    Store store;
    put(store, "items", "[1, 2]");
    // Корень дерева выражения — вызов, и его результат есть значение props,
    // то есть употреблён. Void-функция здесь запрещена, иначе выражение
    // изменило бы данные, что docs/semantics.md §3.2 обещает невозможным.
    EXPECT_EQ(checkExpr(store, "pop(items)").size(), 1u);
    EXPECT_EQ(checkExpr(store, "push(items, 3)").size(), 1u);
    // Возвращающая значение в той же позиции — на месте.
    EXPECT_TRUE(checkExpr(store, "count(items)").empty());
}

TEST(Check, FormatWithANonLiteralTemplateIsNotCheckedHere) {
    Store store;
    put(store, "user", "{'tpl': '${}'}");
    // Шаблон не литерал — сверять нечего, проверка уходит в выполнение.
    EXPECT_TRUE(checkExpr(store, "format(user.tpl, 1, 2, 3)").empty());
}

TEST(Check, FormatWithEscapesInTheTemplateIsStillChecked) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    // Escape-последовательности лексера не дают ни $, ни { , ни } , поэтому
    // границы плейсхолдеров от декодирования не зависят и шаблон проверяется
    // наравне с прочими.
    EXPECT_EQ(checkExpr(store, "format('строка\\nи ${}')").size(), 1u);
    EXPECT_TRUE(checkExpr(store, "format('строка\\nи ${}', user.name)").empty());
}

TEST(Check, AssigningToANameIsACompileError) {
    Store store;
    put(store, "state", "{'n': 0}");
    // Переезд из вычислителя: docs/semantics.md §7.2, закрывает B27.
    ASSERT_EQ(checkScript(store, "state = 1;").size(), 1u);
    EXPECT_EQ(checkScript(store, "state = 1;")[0].code, CS::ErrorCode::Name);
    EXPECT_TRUE(checkScript(store, "state.n = 1;").empty());
}

TEST(Check, UnknownNameIsACompileError) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    // Переезд из выполнения в компиляцию: спека §5.5.
    ASSERT_EQ(checkExpr(store, "usre.name").size(), 1u);
    EXPECT_EQ(checkExpr(store, "usre.name")[0].code, CS::ErrorCode::Name);
    EXPECT_TRUE(checkExpr(store, "user.name").empty());
}

TEST(Check, UnknownNameInAssignmentTargetIsACompileError) {
    Store store;
    put(store, "state", "{'a': 1}");
    // Переезд из core/tests/eval_test.cpp (EvalAssign.UnknownNameIsAnError):
    // неизвестная глобальная переменная внутри цели присваивания ловится тем же узлом
    // Identifier, что и в обычном выражении — здесь важно лишь то, что
    // стейтменты проверяются наравне с выражениями.
    ASSERT_EQ(checkScript(store, "usre.a = 1;").size(), 1u);
    EXPECT_EQ(checkScript(store, "usre.a = 1;")[0].code, CS::ErrorCode::Name);
    EXPECT_TRUE(checkScript(store, "state.a = 1;").empty());
}

TEST(Check, MisspelledKeyIsNotAnErrorAtAnyStage) {
    Store store;
    put(store, "user", "{'name': 'Вася'}");
    // Асимметрия спеки §5.5: имя переменной проверяется, ключ — нет и никогда.
    // Обе половины обязательны, иначе правило вырождается в одностороннее.
    EXPECT_TRUE(checkExpr(store, "user.nmae").empty());
}

TEST(Check, NamesAreCheckedAgainstCompositionNotValues) {
    Store store;
    CS::Deferred dead;
    // Валидатору достаточно состава имён: значения не нужны.
    store.setGlobal("user", CS::Value::null(), dead);
    EXPECT_TRUE(checkExpr(store, "user.profile.city").empty());
    EXPECT_EQ(checkExpr(store, "usre.profile").size(), 1u);
}

TEST(Check, AllErrorsAreReportedNotJustTheFirst) {
    Store store;
    put(store, "items", "[1]");
    // Смысл буфера вместо одного Diagnostic: валидатор показывает всё сразу.
    const auto found = checkScript(store, "cnt(items); min(1); usre.a = 1;");
    EXPECT_GE(found.size(), 3u);
}

TEST(Check, CountExceedsCapacityWhenTheBufferIsSmall) {
    Store store;
    put(store, "items", "[1]");
    Ast ast;
    Diagnostic one;
    const std::string_view text = "cnt(items); min(1); max(2);";
    const std::uint32_t count = CS::compileScript(
        text.data(), static_cast<std::uint32_t>(text.size()), ast, store, &one, 1);
    // Нашлось больше, чем поместилось: вызывающий об этом узнаёт.
    EXPECT_GT(count, 1u);
    EXPECT_EQ(one.code, CS::ErrorCode::Name);
}

TEST(Check, SyntaxErrorGivesExactlyOne) {
    Store store;
    // Парсер останавливается на первой; проверки до негодного дерева не идут.
    const auto found = checkExpr(store, "1 +");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].code, CS::ErrorCode::Syntax);
}

TEST(Check, CleanTreeIsMarkedAndFaultyIsNot) {
    Store store;
    put(store, "items", "[1]");
    Ast clean;
    Diagnostic buffer[4];
    const std::string_view good = "count(items)";
    EXPECT_EQ(CS::compileExpression(good.data(),
                                    static_cast<std::uint32_t>(good.size()),
                                    clean, store, buffer, 4),
              0u);
    EXPECT_TRUE(clean.isChecked());

    Ast faulty;
    const std::string_view bad = "cnt(items)";
    EXPECT_GT(CS::compileExpression(bad.data(),
                                    static_cast<std::uint32_t>(bad.size()),
                                    faulty, store, buffer, 4),
              0u);
    EXPECT_FALSE(faulty.isChecked());
}

TEST(Check, ResetClearsTheMark) {
    Store store;
    put(store, "items", "[1]");
    Ast ast;
    Diagnostic buffer[4];
    const std::string_view good = "count(items)";
    CS::compileExpression(good.data(), static_cast<std::uint32_t>(good.size()),
                          ast, store, buffer, 4);
    ASSERT_TRUE(ast.isChecked());
    // Повторный разбор выбрасывает дерево — отметка обязана уйти с ним.
    const std::string_view other = "1";
    ASSERT_TRUE(CS::parseExpression(
        other.data(), static_cast<std::uint32_t>(other.size()), ast, buffer[0]));
    EXPECT_FALSE(ast.isChecked());
}

TEST(Check, ResolvesTheBuiltinIntoTheCallNode) {
    Store store;
    put(store, "items", "[1]");
    Ast ast;
    Diagnostic buffer[4];
    const std::string_view text = "count(items)";
    ASSERT_EQ(CS::compileExpression(text.data(),
                                    static_cast<std::uint32_t>(text.size()),
                                    ast, store, buffer, 4),
              0u);
    // Имя разрешено на компиляции: вычислению искать его в таблице незачем.
    const CS::NodeId call = ast.root();
    ASSERT_EQ(ast.kind(call), CS::NodeKind::Call);
    EXPECT_TRUE(ast.hasCallee(call));
    EXPECT_EQ(CS::builtinOfCallee(ast.callee(call)), CS::Builtin::Count);
}

TEST(Check, LeavesTheCallNodeUnresolvedForAnUnknownName) {
    Store store;
    put(store, "items", "[1]");
    Ast ast;
    Diagnostic buffer[4];
    const std::string_view text = "cnt(items)";
    ASSERT_GT(CS::compileExpression(text.data(),
                                    static_cast<std::uint32_t>(text.size()),
                                    ast, store, buffer, 4),
              0u);
    // Дерево с ошибкой до вычисления не доходит, но узел обязан честно
    // сообщать, что имя не разрешено: на этом стоит проверка употребления
    // результата, которая иначе прочла бы чужую функцию.
    const CS::NodeId call = ast.root();
    ASSERT_EQ(ast.kind(call), CS::NodeKind::Call);
    EXPECT_FALSE(ast.hasCallee(call));
}

TEST(Check, ResetClearsTheResolvedBuiltin) {
    Store store;
    put(store, "items", "[1]");
    Ast ast;
    Diagnostic buffer[4];
    const std::string_view good = "count(items)";
    ASSERT_EQ(CS::compileExpression(good.data(),
                                    static_cast<std::uint32_t>(good.size()),
                                    ast, store, buffer, 4),
              0u);
    // Повторный разбор выбрасывает дерево — разрешение уходит вместе с узлами,
    // и новый узел на том же индексе не наследует чужую функцию.
    const std::string_view other = "min(1, 2)";
    ASSERT_EQ(CS::compileExpression(other.data(),
                                    static_cast<std::uint32_t>(other.size()),
                                    ast, store, buffer, 4),
              0u);
    const CS::NodeId call = ast.root();
    ASSERT_EQ(ast.kind(call), CS::NodeKind::Call);
    EXPECT_EQ(CS::builtinOfCallee(ast.callee(call)), CS::Builtin::Min);
}

TEST(Check, CountsWithoutABufferAtAll) {
    Store store;
    put(store, "items", "[1]");
    Ast ast;
    const std::string_view text = "cnt(items); min(1);";
    // Ни ёмкости, ни буфера: счёт всё равно ведётся, записи не происходит.
    EXPECT_GT(CS::compileScript(text.data(),
                                static_cast<std::uint32_t>(text.size()), ast,
                                store, nullptr, 0),
              0u);
    Diagnostic unused;
    EXPECT_GT(CS::compileScript(text.data(),
                                static_cast<std::uint32_t>(text.size()), ast,
                                store, &unused, 0),
              0u);
    EXPECT_EQ(unused.code, CS::ErrorCode::None);
}

/// Таблица и режим обязаны доехать до прохода. Пользоваться ими проход
/// начнёт в следующей задаче, но доехать они должны уже сейчас — иначе
/// следующая задача обнаружит, что чинить надо две вещи вместо одной.
TEST(CheckCompileMode, ExpressionDoorCompilesWithoutAHostTable) {
    Store store;
    Ast ast;
    Diagnostic diags[4]{};
    const std::string_view source = "1 + 2";
    EXPECT_EQ(CS::compileExpression(source.data(),
                                    static_cast<std::uint32_t>(source.size()),
                                    ast, store, diags, 4),
              0u);
}

TEST(CheckCompileMode, ExpressionDoorAcceptsAHostTable) {
    Store store;
    CS::HostTable hosts;
    Ast ast;
    Diagnostic diags[4]{};
    const std::string_view source = "1 + 2";
    EXPECT_EQ(CS::compileExpression(source.data(),
                                    static_cast<std::uint32_t>(source.size()),
                                    ast, store, diags, 4, &hosts),
              0u);
}

TEST(CheckHostFunctions, ResolvesRegisteredName) {
    Store store;
    CS::HostTable hosts;
    ASSERT_EQ(hosts.add(healthyFunction("formatDate")), CS::RegisterOutcome::Ok);
    EXPECT_TRUE(checkExpr(store, "formatDate(1)", 8, &hosts).empty());
}

TEST(CheckHostFunctions, UnknownNameStillReportsUnknownFunction) {
    Store store;
    CS::HostTable hosts;
    const auto found = checkExpr(store, "noSuchFunction(1)", 8, &hosts);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_STREQ(found[0].message, "unknown function");
}

/// Без таблицы вовсе — та же диагностика: hosts == nullptr значит «хост-функций
/// нет», а не «искать негде».
TEST(CheckHostFunctions, UnknownNameWithoutATableReportsTheSameThing) {
    Store store;
    const auto found = checkExpr(store, "formatDate(1)");
    ASSERT_EQ(found.size(), 1u);
    EXPECT_STREQ(found[0].message, "unknown function");
}

TEST(CheckHostFunctions, ArityIsCheckedForHostFunctions) {
    Store store;
    CS::HostTable hosts;
    ASSERT_EQ(hosts.add(healthyFunction("formatDate")), CS::RegisterOutcome::Ok);
    const auto found = checkExpr(store, "formatDate(1, 2)", 8, &hosts);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_STREQ(found[0].message, "wrong number of arguments");
}

TEST(CheckHostFunctions, VariadicHostFunctionAcceptsAnyCountAboveMinimum) {
    Store store;
    CS::HostTable hosts;
    ChupaFunction fn = healthyFunction("joinAll");
    fn.min_args = 1;
    fn.max_args = CHUPA_VARIADIC;
    ASSERT_EQ(hosts.add(fn), CS::RegisterOutcome::Ok);

    EXPECT_TRUE(checkExpr(store, "joinAll(1, 2, 3, 4, 5)", 8, &hosts).empty());
    EXPECT_EQ(checkExpr(store, "joinAll()", 8, &hosts).size(), 1u);
}

/// Грязная функция в выражении — ошибка компиляции. Это и есть та проверка,
/// которой docs/grammar.md §6.3 раньше не требовал: он ВЫВОДИЛ чистоту из
/// «грязное не возвращает значения», а хост-функция вправе эту посылку
/// нарушить — объявить и RETURNS_VALUE, и отсутствие PURE.
TEST(CheckHostFunctions, ImpureFunctionIsRefusedInAnExpression) {
    Store store;
    CS::HostTable hosts;
    ChupaFunction fn = healthyFunction("track");
    fn.flags = CHUPA_FN_RETURNS_VALUE;   // возвращает значение и грязная
    ASSERT_EQ(hosts.add(fn), CS::RegisterOutcome::Ok);

    const auto found = checkExpr(store, "track(1)", 8, &hosts);
    ASSERT_EQ(found.size(), 1u);
    EXPECT_EQ(found[0].code, CS::ErrorCode::Usage);
}

TEST(CheckHostFunctions, ImpureFunctionIsAllowedInAScript) {
    Store store;
    put(store, "x", "{'n': 0}");
    CS::HostTable hosts;
    ChupaFunction fn = healthyFunction("track");
    fn.flags = CHUPA_FN_RETURNS_VALUE;
    ASSERT_EQ(hosts.add(fn), CS::RegisterOutcome::Ok);

    EXPECT_TRUE(checkScript(store, "x.n = track(1);", 8, &hosts).empty());
}

/// push и pop грязные, но новая диагностика их не касается: их случай уже
/// закрыт правилом «результат Void употреблять нельзя», и вторая жалоба на тот
/// же факт удвоила бы вывод компилятора.
TEST(CheckHostFunctions, ImpureBuiltinKeepsItsOldSingleDiagnostic) {
    Store store;
    put(store, "items", "[1, 2, 3]");
    EXPECT_EQ(checkExpr(store, "push(items, 1)").size(), 1u);
}

}  // namespace
