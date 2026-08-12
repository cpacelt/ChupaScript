// Тесты парсера по docs/grammar.md §5.
#include <gtest/gtest.h>

#include <cstdint>
#include <cstdio>
#include <string>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "parser.hpp"

namespace {

using CS::NodeKind;
using CS::TokenKind;

struct Parsed {
    CS::Ast ast;
    bool ok = false;
    CS::Diagnostic diag;
};

/// Исходник обязан пережить Parsed: дерево держит на него срезы.
Parsed parseExpr(const std::string &source) {
    Parsed result;
    result.ok = CS::parseExpression(source.data(),
                                    static_cast<std::uint32_t>(source.size()),
                                    result.ast, result.diag);
    return result;
}

Parsed parseProg(const std::string &source) {
    Parsed result;
    result.ok = CS::parseProgram(source.data(),
                                 static_cast<std::uint32_t>(source.size()),
                                 result.ast, result.diag);
    return result;
}

const char *opName(TokenKind kind) {
    switch (kind) {
        case TokenKind::Plus: return "+";
        case TokenKind::Minus: return "-";
        case TokenKind::Star: return "*";
        case TokenKind::Slash: return "/";
        case TokenKind::Percent: return "%";
        case TokenKind::Bang: return "!";
        case TokenKind::Equal: return "==";
        case TokenKind::NotEqual: return "!=";
        case TokenKind::Less: return "<";
        case TokenKind::Greater: return ">";
        case TokenKind::LessEqual: return "<=";
        case TokenKind::GreaterEqual: return ">=";
        case TokenKind::AndAnd: return "&&";
        case TokenKind::OrOr: return "||";
        case TokenKind::QuestionQuestion: return "??";
        case TokenKind::Assign: return "=";
        case TokenKind::PlusAssign: return "+=";
        case TokenKind::MinusAssign: return "-=";
        case TokenKind::StarAssign: return "*=";
        case TokenKind::SlashAssign: return "/=";
        default: return "?";
    }
}

std::string numberText(double value) {
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, "%g", value);
    return buffer;
}

std::string dump(const CS::Ast &ast, CS::NodeId node);

/// "(head child child …)"
std::string listOf(const CS::Ast &ast, CS::NodeId node, const std::string &head) {
    std::string result = "(" + head;
    for (std::uint32_t i = 0; i < ast.childCount(node); ++i) {
        result += " " + dump(ast, ast.child(node, i));
    }
    return result + ")";
}

/// Печатает поддерево S-выражением. Форма зафиксирована тестами.
std::string dump(const CS::Ast &ast, CS::NodeId node) {
    switch (ast.kind(node)) {
        case NodeKind::Invalid:
            return "<invalid>";
        case NodeKind::Number:
            return numberText(ast.numberValue(node));
        case NodeKind::String:
            return "'" + std::string(ast.text(node)) + "'";
        case NodeKind::Boolean:
            return ast.boolValue(node) ? "true" : "false";
        case NodeKind::Null:
            return "null";
        case NodeKind::Identifier:
            return std::string(ast.text(node));
        case NodeKind::Member:
            return "(. " + dump(ast, ast.child(node, 0)) + " " +
                   std::string(ast.text(node)) + ")";
        case NodeKind::Index:
            return listOf(ast, node, "[]");
        case NodeKind::Call:
            return listOf(ast, node, "call " + std::string(ast.text(node)));
        case NodeKind::Unary:
            return listOf(ast, node, std::string("u") + opName(ast.op(node)));
        case NodeKind::Binary:
            return listOf(ast, node, opName(ast.op(node)));
        case NodeKind::Conditional:
            return listOf(ast, node, "?:");
        case NodeKind::Array:
            return listOf(ast, node, "array");
        case NodeKind::Object:
            return listOf(ast, node, "object");
        case NodeKind::Assign:
            return listOf(ast, node, opName(ast.op(node)));
        case NodeKind::CallStatement:
            return listOf(ast, node, "stmt");
        case NodeKind::Program:
            return listOf(ast, node, "program");
    }
    return "<unknown>";
}

/// Разбирает выражение и печатает дерево. Пустая строка, если разбор не удался.
std::string expr(const std::string &source) {
    const Parsed parsed = parseExpr(source);
    if (!parsed.ok) {
        return "";
    }
    return dump(parsed.ast, parsed.ast.root());
}

/// То же для программы.
std::string prog(const std::string &source) {
    const Parsed parsed = parseProg(source);
    if (!parsed.ok) {
        return "";
    }
    return dump(parsed.ast, parsed.ast.root());
}

// ─── §5.3: Primary ───────────────────────────────────────────────────

TEST(ParserPrimary, NumberLiteral) {
    const std::string source = "42";
    EXPECT_EQ(expr(source), "42");
}

TEST(ParserPrimary, FractionalNumberLiteral) {
    const std::string source = "12.75";
    EXPECT_EQ(expr(source), "12.75");
}

TEST(ParserPrimary, StringLiteral) {
    const std::string source = "'привет'";
    EXPECT_EQ(expr(source), "'привет'");
}

TEST(ParserPrimary, StringLiteralKeepsRawEscapes) {
    const std::string source = "'a\\nb'";
    const Parsed parsed = parseExpr(source);
    ASSERT_TRUE(parsed.ok);
    const CS::NodeId root = parsed.ast.root();
    EXPECT_EQ(parsed.ast.kind(root), NodeKind::String);
    EXPECT_EQ(parsed.ast.text(root), "a\\nb");
    EXPECT_TRUE(parsed.ast.hasEscape(root));
}

TEST(ParserPrimary, BooleanLiterals) {
    const std::string yes = "true";
    const std::string no = "false";
    EXPECT_EQ(expr(yes), "true");
    EXPECT_EQ(expr(no), "false");
}

TEST(ParserPrimary, NullLiteral) {
    const std::string source = "null";
    EXPECT_EQ(expr(source), "null");
}

TEST(ParserPrimary, Identifier) {
    const std::string source = "state";
    EXPECT_EQ(expr(source), "state");
}

TEST(ParserPrimary, ParenthesesProduceNoNode) {
    const std::string source = "(((a)))";
    EXPECT_EQ(expr(source), "a");
}

// Зеленеют в задаче 7.
TEST(ParserPrimary, EmptyArrayLiteral) {
    const std::string source = "[]";
    EXPECT_EQ(expr(source), "(array)");
}

TEST(ParserPrimary, ArrayLiteralKeepsOrder) {
    const std::string source = "[1, 'a', null]";
    EXPECT_EQ(expr(source), "(array 1 'a' null)");
}

TEST(ParserPrimary, EmptyObjectLiteral) {
    const std::string source = "{}";
    EXPECT_EQ(expr(source), "(object)");
}

TEST(ParserPrimary, ObjectLiteralInterleavesKeysAndValues) {
    const std::string source = "{ 'a': 1, 'b': 2 }";
    EXPECT_EQ(expr(source), "(object 'a' 1 'b' 2)");
}

TEST(ParserPrimary, AggregatesNest) {
    const std::string source = "{ 'xs': [1, [2]] }";
    EXPECT_EQ(expr(source), "(object 'xs' (array 1 (array 2)))");
}

TEST(ParserPrimary, ReservedWordIsNotAnExpression) {
    const std::string source = "class";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

// ─── §5.4: приоритет ─────────────────────────────────────────────────

TEST(ParserPrecedence, MultiplicativeBindsTighterThanAdditive) {
    const std::string source = "a + b * c";
    EXPECT_EQ(expr(source), "(+ a (* b c))");
}

TEST(ParserPrecedence, ParenthesesOverridePrecedence) {
    const std::string source = "(a + b) * c";
    EXPECT_EQ(expr(source), "(* (+ a b) c)");
}

TEST(ParserPrecedence, UnaryBindsTighterThanMultiplicative) {
    const std::string source = "-a * b";
    EXPECT_EQ(expr(source), "(* (u- a) b)");
}

TEST(ParserPrecedence, NilCoalesceBindsLooserThanAdditive) {
    const std::string source = "a ?? b + c";
    EXPECT_EQ(expr(source), "(?? a (+ b c))");
}

TEST(ParserPrecedence, NilCoalesceBindsTighterThanComparison) {
    const std::string source = "a ?? b == c";
    EXPECT_EQ(expr(source), "(== (?? a b) c)");
}

TEST(ParserPrecedence, ComparisonBindsTighterThanAnd) {
    const std::string source = "a < b && c > d";
    EXPECT_EQ(expr(source), "(&& (< a b) (> c d))");
}

TEST(ParserPrecedence, AndBindsTighterThanOr) {
    const std::string source = "a || b && c";
    EXPECT_EQ(expr(source), "(|| a (&& b c))");
}

TEST(ParserPrecedence, TernaryIsLowest) {
    const std::string source = "a || b ? c + d : e";
    EXPECT_EQ(expr(source), "(?: (|| a b) (+ c d) e)");
}

TEST(ParserPrecedence, ModuloSitsWithMultiplicative) {
    const std::string source = "a + b % c";
    EXPECT_EQ(expr(source), "(+ a (% b c))");
}

// ─── §5.4: ассоциативность ───────────────────────────────────────────

TEST(ParserAssociativity, AdditiveIsLeft) {
    const std::string source = "a - b - c";
    EXPECT_EQ(expr(source), "(- (- a b) c)");
}

TEST(ParserAssociativity, MultiplicativeIsLeft) {
    const std::string source = "a / b / c";
    EXPECT_EQ(expr(source), "(/ (/ a b) c)");
}

TEST(ParserAssociativity, OrIsLeft) {
    const std::string source = "a || b || c";
    EXPECT_EQ(expr(source), "(|| (|| a b) c)");
}

TEST(ParserAssociativity, AndIsLeft) {
    const std::string source = "a && b && c";
    EXPECT_EQ(expr(source), "(&& (&& a b) c)");
}

TEST(ParserAssociativity, NilCoalesceIsRight) {
    const std::string source = "a ?? b ?? c";
    EXPECT_EQ(expr(source), "(?? a (?? b c))");
}

TEST(ParserAssociativity, TernaryIsRight) {
    const std::string source = "a ? b : c ? d : e";
    EXPECT_EQ(expr(source), "(?: a b (?: c d e))");
}

TEST(ParserAssociativity, UnaryIsRight) {
    const std::string source = "!!a";
    EXPECT_EQ(expr(source), "(u! (u! a))");
}

// ─── §5.3: Postfix и Call ────────────────────────────────────────────

TEST(ParserPostfix, MemberAccess) {
    const std::string source = "user.name";
    EXPECT_EQ(expr(source), "(. user name)");
}

TEST(ParserPostfix, MemberChain) {
    const std::string source = "a.b.c";
    EXPECT_EQ(expr(source), "(. (. a b) c)");
}

TEST(ParserPostfix, Index) {
    const std::string source = "items[0]";
    EXPECT_EQ(expr(source), "([] items 0)");
}

TEST(ParserPostfix, IndexTakesFullExpression) {
    const std::string source = "items[i + 1]";
    EXPECT_EQ(expr(source), "([] items (+ i 1))");
}

TEST(ParserPostfix, MixedChain) {
    const std::string source = "a.b[0].c";
    EXPECT_EQ(expr(source), "(. ([] (. a b) 0) c)");
}

TEST(ParserPostfix, CallWithoutArguments) {
    const std::string source = "keys()";
    EXPECT_EQ(expr(source), "(call keys)");
}

TEST(ParserPostfix, CallWithArguments) {
    const std::string source = "min(a, b + 1)";
    EXPECT_EQ(expr(source), "(call min a (+ b 1))");
}

TEST(ParserPostfix, CallIsPostfixBase) {
    const std::string source = "keys(o)[0]";
    EXPECT_EQ(expr(source), "([] (call keys o) 0)");
}

TEST(ParserPostfix, UnaryAppliesToWholeChain) {
    const std::string source = "-a.b";
    EXPECT_EQ(expr(source), "(u- (. a b))");
}

TEST(ParserPostfix, NestedCallInArgument) {
    const std::string source = "min(count(a), 1)";
    EXPECT_EQ(expr(source), "(call min (call count a) 1)");
}

TEST(ParserPostfix, CallResultIsNotCallable) {
    const std::string source = "f(a)(b)";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 4u);
}

TEST(ParserPostfix, MethodCallSyntaxIsNotSupported) {
    const std::string source = "a.f(b)";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 3u);
}

// ─── §5.2: стейтменты ────────────────────────────────────────────────

TEST(ParserStatement, EmptyProgramHasEmptyRoot) {
    const std::string source = "";
    EXPECT_EQ(prog(source), "(program)");
}

TEST(ParserStatement, WhitespaceOnlyProgramHasEmptyRoot) {
    const std::string source = "  // только комментарий\n";
    EXPECT_EQ(prog(source), "(program)");
}

TEST(ParserStatement, EmptyStatementProducesNoNode) {
    const std::string source = ";;;";
    EXPECT_EQ(prog(source), "(program)");
}

TEST(ParserStatement, SimpleAssignment) {
    const std::string source = "a = 1;";
    EXPECT_EQ(prog(source), "(program (= a 1))");
}

TEST(ParserStatement, CompoundAssignments) {
    const std::string plus = "a += 1;";
    const std::string minus = "a -= 1;";
    const std::string star = "a *= 1;";
    const std::string slash = "a /= 1;";
    EXPECT_EQ(prog(plus), "(program (+= a 1))");
    EXPECT_EQ(prog(minus), "(program (-= a 1))");
    EXPECT_EQ(prog(star), "(program (*= a 1))");
    EXPECT_EQ(prog(slash), "(program (/= a 1))");
}

TEST(ParserStatement, AssignmentToMember) {
    const std::string source = "state.total = 1;";
    EXPECT_EQ(prog(source), "(program (= (. state total) 1))");
}

TEST(ParserStatement, AssignmentToIndex) {
    const std::string source = "items[0] = 1;";
    EXPECT_EQ(prog(source), "(program (= ([] items 0) 1))");
}

TEST(ParserStatement, AssignmentTargetMayHaveCallInSubscript) {
    const std::string source = "arr[idx()] += 1;";
    EXPECT_EQ(prog(source), "(program (+= ([] arr (call idx)) 1))");
}

TEST(ParserStatement, CallStatement) {
    const std::string source = "push(items, 1);";
    EXPECT_EQ(prog(source), "(program (stmt (call push items 1)))");
}

TEST(ParserStatement, SeveralStatementsKeepOrder) {
    const std::string source = "a = 1; push(b, 2); c += 3;";
    EXPECT_EQ(prog(source),
              "(program (= a 1) (stmt (call push b 2)) (+= c 3))");
}

TEST(ParserStatement, StatementsSpanLines) {
    const std::string source = "a = 1;\n\nb = 2;\n";
    EXPECT_EQ(prog(source), "(program (= a 1) (= b 2))");
}

// ─── §5.1: два стартовых символа ─────────────────────────────────────

TEST(ParserModes, ExpressionModeRejectsAssignment) {
    const std::string source = "a = 1";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 2u);
}

TEST(ParserModes, ExpressionModeRejectsSemicolon) {
    const std::string source = "a + b;";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 5u);
}

TEST(ParserModes, ExpressionModeRejectsEmptySource) {
    const std::string source = "";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

TEST(ParserModes, ProgramModeRejectsBareExpression) {
    const std::string source = "a + 1;";
    const Parsed parsed = parseProg(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

TEST(ParserModes, ExpressionModeAcceptsWhatProgramModeRejects) {
    const std::string source = "a + 1";
    EXPECT_EQ(expr(source), "(+ a 1)");
}

TEST(ParserModes, FailedParseAfterSuccessLeavesNoRoot) {
    const std::string good = "a + 1";
    const std::string bad = "1 +";
    Parsed first;
    first.ok = CS::parseExpression(good.data(),
                                   static_cast<std::uint32_t>(good.size()),
                                   first.ast, first.diag);
    ASSERT_TRUE(first.ok);
    first.ok = CS::parseExpression(bad.data(),
                                   static_cast<std::uint32_t>(bad.size()),
                                   first.ast, first.diag);
    EXPECT_FALSE(first.ok);
    EXPECT_EQ(first.ast.root(), CS::kNoNode);
}

// ─── §5.5: ранние ошибки парсера ─────────────────────────────────────

TEST(ParserEarlyErrors, ChainedComparison) {
    const std::string source = "a < b < c";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 6u);
}

TEST(ParserEarlyErrors, TrailingTextAfterExpression) {
    const std::string source = "1 + 1 2";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 6u);
}

TEST(ParserEarlyErrors, MissingOperand) {
    const std::string source = "1 +";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 3u);
}

TEST(ParserEarlyErrors, UnclosedParenthesis) {
    const std::string source = "(1 + 2";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 6u);
}

TEST(ParserEarlyErrors, TernaryWithoutColon) {
    const std::string source = "a ? b";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 5u);
}

TEST(ParserEarlyErrors, DotWithoutName) {
    const std::string source = "a.1";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 2u);
}

TEST(ParserEarlyErrors, UnclosedBracket) {
    const std::string source = "a[0";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 3u);
}

TEST(ParserEarlyErrors, TrailingCommaInCall) {
    const std::string source = "f(a, b,)";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 7u);
}

TEST(ParserEarlyErrors, UnclosedCall) {
    const std::string source = "f(a";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 3u);
}

TEST(ParserEarlyErrors, TrailingCommaInArray) {
    const std::string source = "[1, 2,]";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 6u);
}

TEST(ParserEarlyErrors, TrailingCommaInObject) {
    const std::string source = "{ 'a': 1, }";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 10u);
}

TEST(ParserEarlyErrors, ObjectKeyMustBeStringLiteral) {
    const std::string source = "{ x: 1 }";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 2u);
}

TEST(ParserEarlyErrors, ObjectPairNeedsColon) {
    const std::string source = "{ 'a' 1 }";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 6u);
}

TEST(ParserEarlyErrors, StatementReducesToNoProduction) {
    const std::string source = "user.name;";
    const Parsed parsed = parseProg(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

TEST(ParserEarlyErrors, StatementCannotStartWithLiteral) {
    const std::string source = "1 + 1;";
    const Parsed parsed = parseProg(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

TEST(ParserEarlyErrors, AssignmentTargetCannotContainCall) {
    const std::string source = "f(a).b = 1;";
    const Parsed parsed = parseProg(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

TEST(ParserEarlyErrors, StatementNeedsSemicolon) {
    const std::string source = "a = 1";
    const Parsed parsed = parseProg(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 5u);
}

// ─── ошибки лексера проходят насквозь ────────────────────────────────

TEST(ParserLexerErrors, UnterminatedStringKeepsLexerDiagnostic) {
    const std::string source = "'abc";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(parsed.diag.offset, 0u);
}

// ─── предел глубины рекурсии ──────────────────────────────────────────

TEST(ParserLimits, DeepButAcceptableNestingParses) {
    std::string source(20, '(');
    source += "1";
    source.append(20, ')');
    const Parsed parsed = parseExpr(source);
    EXPECT_TRUE(parsed.ok);
}

TEST(ParserLimits, ExcessiveNestingIsRejectedNotCrashing) {
    std::string source(5000, '(');
    source += "1";
    source.append(5000, ')');
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
}

TEST(ParserLimits, DeepUnaryChainIsRejectedNotCrashing) {
    std::string source(5000, '!');
    source += "a";
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
}

TEST(ParserLimits, DeepNilCoalesceChainIsRejectedNotCrashing) {
    std::string source = "a";
    for (int i = 0; i < 5000; ++i) {
        source += " ?? a";
    }
    const Parsed parsed = parseExpr(source);
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
}

/// Цепочка из n звеньев вида link, приложенных к имени.
std::string chainOf(const char *link, int count) {
    std::string source = "a";
    for (int i = 0; i < count; ++i) {
        source += link;
    }
    return source;
}

// Цепочка '.' и '[]' строится циклом, но дерево наращивает так же, как
// рекурсия, а по этому дереву потом рекурсивно спускается вычислитель. Поэтому
// звено тратит тот же бюджет, что и вложенность, и границы ниже — измеренные
// (docs/grammar.md Приложение C.1), а не выведенные из kMaxDepth: до postfix()
// управление доходит через ternary, nilCoalesce и unary, каждое из которых уже
// взяло по единице.

TEST(ParserLimits, LongMemberChainIsAcceptedUpToTheLimit) {
    EXPECT_TRUE(parseExpr(chainOf(".b", 509)).ok);
}

TEST(ParserLimits, MemberChainPastTheLimitIsRejected) {
    const Parsed parsed = parseExpr(chainOf(".b", 510));
    ASSERT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_STREQ(parsed.diag.message, "expression nesting too deep");
}

TEST(ParserLimits, LongIndexChainIsAcceptedUpToTheLimit) {
    // На три звена короче, чем цепочка '.': подвыражение внутри последнего
    // '[...]' проходит ternary, nilCoalesce и unary поверх накопленной глубины.
    EXPECT_TRUE(parseExpr(chainOf("[0]", 506)).ok);
}

TEST(ParserLimits, IndexChainPastTheLimitIsRejected) {
    const Parsed parsed = parseExpr(chainOf("[0]", 507));
    ASSERT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_STREQ(parsed.diag.message, "expression nesting too deep");
}

TEST(ParserLimits, VeryLongMemberChainIsRejectedNotCrashing) {
    // Ровно тот вход, который раньше разбирался целиком и ронял вычислитель.
    const Parsed parsed = parseExpr(chainOf(".b", 50000));
    EXPECT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
}

TEST(ParserLimits, NestingAndChainsShareOneBudget) {
    // Главное свойство правки: цепочки и вложенность не перемножаются. Каждый
    // уровень скобок стоит трёх единиц, поэтому доступная длина цепочки внутри
    // убывает на три за уровень, а высота дерева остаётся ограниченной.
    EXPECT_TRUE(parseExpr("(" + chainOf(".b", 506) + ")").ok);
    EXPECT_FALSE(parseExpr("(" + chainOf(".b", 507) + ")").ok);
    EXPECT_TRUE(parseExpr("((((" + chainOf(".b", 497) + "))))").ok);
    EXPECT_FALSE(parseExpr("((((" + chainOf(".b", 498) + "))))").ok);
}

/// Цепочка из count операторов op над count+1 копиями operand.
std::string operatorChain(const char *operand, const char *op, int count) {
    std::string source = operand;
    for (int i = 0; i < count; ++i) {
        source += op;
        source += operand;
    }
    return source;
}

// Левоассоциативные уровни — '||', '&&', '+'/'-', '*'/'/'/'%' — разбирают
// цепочку циклом, но каждый оператор даёт уровень левоглубокого дерева Binary,
// по которому вычислитель спускается рекурсивно. Поэтому оператор тратит ту же
// единицу бюджета, что и вложенность, а границы ниже — измеренные
// (docs/grammar.md Приложение C.1). До этой правки цепочка любой длины
// разбиралась успешно и роняла процесс в вычислителе.

TEST(ParserLimits, LongAdditiveChainIsAcceptedUpToTheLimit) {
    EXPECT_TRUE(parseExpr(operatorChain("1", " + ", 509)).ok);
}

TEST(ParserLimits, AdditiveChainPastTheLimitIsRejected) {
    const Parsed parsed = parseExpr(operatorChain("1", " + ", 510));
    ASSERT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_STREQ(parsed.diag.message, "expression nesting too deep");
}

TEST(ParserLimits, LongLogicalChainIsAcceptedUpToTheLimit) {
    EXPECT_TRUE(parseExpr(operatorChain("true", " && ", 509)).ok);
}

TEST(ParserLimits, LogicalChainPastTheLimitIsRejected) {
    const Parsed parsed = parseExpr(operatorChain("true", " && ", 510));
    ASSERT_FALSE(parsed.ok);
    EXPECT_EQ(parsed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_STREQ(parsed.diag.message, "expression nesting too deep");
}

TEST(ParserLimits, VeryLongOperatorChainIsRejectedNotCrashing) {
    // Ровно тот вход, который раньше разбирался целиком (100 002 узла) и ронял
    // вычислитель по SIGSEGV.
    EXPECT_FALSE(parseExpr(operatorChain("1", " + ", 50000)).ok);
    EXPECT_FALSE(parseExpr(operatorChain("true", " && ", 50000)).ok);
}

TEST(ParserLimits, OperatorChainsShareTheBudgetWithNesting) {
    // Цепочка внутри скобок короче на три единицы за уровень — ровно как
    // цепочка '.': бюджет один на всё дерево, и перемножения не происходит.
    EXPECT_TRUE(parseExpr("(" + operatorChain("1", " + ", 506) + ")").ok);
    EXPECT_FALSE(parseExpr("(" + operatorChain("1", " + ", 507) + ")").ok);
    EXPECT_TRUE(parseExpr("((((" + operatorChain("1", " + ", 497) + "))))").ok);
    EXPECT_FALSE(parseExpr("((((" + operatorChain("1", " + ", 498) + "))))").ok);
}

TEST(ParserLimits, ChainsInsideSubscriptsAreCountedToo) {
    // Выражение внутри '[...]' видит глубину, накопленную предыдущими звеньями:
    // иначе каждый уровень вложенности нёс бы полную цепочку и высота дерева
    // росла бы произведением.
    std::string nested;
    for (int i = 0; i < 40; ++i) {
        nested += chainOf(".b", 10) + "[";
    }
    nested += "0";
    for (int i = 0; i < 40; ++i) {
        nested += "]";
    }
    const Parsed parsed = parseExpr(nested);
    ASSERT_FALSE(parsed.ok);
    EXPECT_STREQ(parsed.diag.message, "expression nesting too deep");
}

}  // namespace
