// Тесты строителя и аксессоров Ast. Парсер участвует ровно в одном тесте —
// TextSurvivesSourceRelocation: там нужно настоящее дерево над настоящим
// исходником, а строителем такое не собрать.
#include <gtest/gtest.h>

#include <cstdint>
#include <string>
#include <vector>

#include "ast.hpp"
#include "diagnostic.hpp"
#include "parser.hpp"
#include "token.hpp"

namespace {

using CS::NodeKind;
using CS::TokenKind;

/// Токен-идентификатор, указывающий в source.
CS::Token ident(std::uint32_t offset, std::uint32_t length) {
    CS::Token token;
    token.kind = TokenKind::Identifier;
    token.offset = offset;
    token.length = length;
    return token;
}

/// Токен-число.
CS::Token number(double value, std::uint32_t offset) {
    CS::Token token;
    token.kind = TokenKind::Number;
    token.offset = offset;
    token.length = 1;
    token.number = value;
    return token;
}

/// Токен-строка. offset указывает на открывающую кавычку.
CS::Token string(std::uint32_t offset, std::uint32_t length, bool hasEscape) {
    CS::Token token;
    token.kind = TokenKind::String;
    token.offset = offset;
    token.length = length;
    token.hasEscape = hasEscape;
    return token;
}

TEST(AstShape, EmptyTreeHasNoRoot) {
    const CS::Ast ast;
    EXPECT_EQ(ast.root(), CS::kNoNode);
    EXPECT_EQ(ast.kind(CS::kNoNode), NodeKind::Invalid);
}

TEST(AstShape, NumberKeepsValueAndOffset) {
    CS::Ast ast;
    const CS::NodeId node = ast.number(number(2.5, 7));
    EXPECT_NE(node, CS::kNoNode);
    EXPECT_EQ(ast.kind(node), NodeKind::Number);
    EXPECT_DOUBLE_EQ(ast.numberValue(node), 2.5);
    EXPECT_EQ(ast.offset(node), 7u);
    EXPECT_EQ(ast.childCount(node), 0u);
}

TEST(AstShape, StringStripsQuotesAndKeepsEscapeFlag) {
    const std::string source = "x = 'абв'";
    CS::Ast ast;
    ast.reset(static_cast<std::uint32_t>(source.size()));
    // 'абв' занимает [4, 12): кавычки плюс шесть байт кириллицы.
    const CS::NodeId node = ast.string(string(4, 8, true));
    EXPECT_EQ(ast.kind(node), NodeKind::String);
    EXPECT_EQ(ast.text(node, source), "абв");
    EXPECT_TRUE(ast.hasEscape(node));
    EXPECT_EQ(ast.offset(node), 4u);
}

TEST(AstShape, EmptyStringYieldsEmptyText) {
    const std::string source = "''";
    CS::Ast ast;
    ast.reset(static_cast<std::uint32_t>(source.size()));
    const CS::NodeId node = ast.string(string(0, 2, false));
    EXPECT_EQ(ast.text(node, source), "");
    EXPECT_FALSE(ast.hasEscape(node));
}

TEST(AstShape, BooleanTakesValueFromTokenKind) {
    CS::Ast ast;
    CS::Token yes;
    yes.kind = TokenKind::True;
    CS::Token no;
    no.kind = TokenKind::False;
    EXPECT_TRUE(ast.boolValue(ast.boolean(yes)));
    EXPECT_FALSE(ast.boolValue(ast.boolean(no)));
}

TEST(AstShape, IdentifierTextIsSourceSlice) {
    const std::string source = "user.name";
    CS::Ast ast;
    ast.reset(static_cast<std::uint32_t>(source.size()));
    const CS::NodeId node = ast.identifier(ident(0, 4));
    EXPECT_EQ(ast.kind(node), NodeKind::Identifier);
    EXPECT_EQ(ast.text(node, source), "user");
}

TEST(AstShape, BinaryKeepsChildrenInOrder) {
    CS::Ast ast;
    const CS::NodeId lhs = ast.number(number(1.0, 0));
    const CS::NodeId rhs = ast.number(number(2.0, 4));
    const CS::NodeId node = ast.binary(TokenKind::Plus, lhs, rhs, 2);
    EXPECT_EQ(ast.kind(node), NodeKind::Binary);
    EXPECT_EQ(ast.op(node), TokenKind::Plus);
    EXPECT_EQ(ast.offset(node), 2u);
    ASSERT_EQ(ast.childCount(node), 2u);
    EXPECT_EQ(ast.child(node, 0), lhs);
    EXPECT_EQ(ast.child(node, 1), rhs);
}

TEST(AstShape, CallCopiesArgumentsInOrder) {
    const std::string source = "min(1, 2, 3)";
    CS::Ast ast;
    ast.reset(static_cast<std::uint32_t>(source.size()));
    const CS::NodeId args[3] = {ast.number(number(1.0, 4)),
                                ast.number(number(2.0, 7)),
                                ast.number(number(3.0, 10))};
    const CS::NodeId node = ast.call(ident(0, 3), args, 3);
    EXPECT_EQ(ast.kind(node), NodeKind::Call);
    EXPECT_EQ(ast.text(node, source), "min");
    ASSERT_EQ(ast.childCount(node), 3u);
    EXPECT_EQ(ast.child(node, 0), args[0]);
    EXPECT_EQ(ast.child(node, 2), args[2]);
}

TEST(AstShape, ObjectKeepsPairsInterleaved) {
    const std::string source = "{ 'a': 1 }";
    CS::Ast ast;
    ast.reset(static_cast<std::uint32_t>(source.size()));
    const CS::NodeId pairs[2] = {ast.string(string(2, 3, false)),
                                 ast.number(number(1.0, 7))};
    const CS::NodeId node = ast.object(pairs, 2, 0);
    EXPECT_EQ(ast.kind(node), NodeKind::Object);
    ASSERT_EQ(ast.childCount(node), 2u);
    EXPECT_EQ(ast.kind(ast.child(node, 0)), NodeKind::String);
    EXPECT_EQ(ast.kind(ast.child(node, 1)), NodeKind::Number);
}

TEST(AstShape, NestedChildRangesDoNotOverlap) {
    CS::Ast ast;
    const CS::NodeId inner[2] = {ast.number(number(1.0, 0)),
                                 ast.number(number(2.0, 3))};
    const CS::NodeId innerArray = ast.array(inner, 2, 0);
    const CS::NodeId outer[2] = {innerArray, ast.number(number(3.0, 9))};
    const CS::NodeId outerArray = ast.array(outer, 2, 0);

    ASSERT_EQ(ast.childCount(innerArray), 2u);
    ASSERT_EQ(ast.childCount(outerArray), 2u);
    EXPECT_EQ(ast.child(innerArray, 0), inner[0]);
    EXPECT_EQ(ast.child(innerArray, 1), inner[1]);
    EXPECT_EQ(ast.child(outerArray, 0), innerArray);
    EXPECT_EQ(ast.child(outerArray, 1), outer[1]);
}

TEST(AstShape, ChildOutOfRangeYieldsNoNode) {
    CS::Ast ast;
    const CS::NodeId lhs = ast.number(number(1.0, 0));
    const CS::NodeId rhs = ast.number(number(2.0, 4));
    const CS::NodeId node = ast.binary(TokenKind::Plus, lhs, rhs, 2);
    EXPECT_EQ(ast.child(node, 2), CS::kNoNode);
    EXPECT_EQ(ast.child(node, 1000), CS::kNoNode);
}

TEST(AstShape, EveryBuilderProducesDistinctNodes) {
    CS::Ast ast;
    const CS::NodeId first = ast.number(number(1.0, 0));
    const CS::NodeId second = ast.number(number(1.0, 0));
    EXPECT_NE(first, second);
    EXPECT_EQ(ast.nodeCount(), 3u);  // пустышка плюс два узла
}

TEST(AstShape, RootIsWhatWasSet) {
    CS::Ast ast;
    const CS::NodeId node = ast.number(number(1.0, 0));
    ast.setRoot(node);
    EXPECT_EQ(ast.root(), node);
}

TEST(AstShape, TextSurvivesSourceRelocation) {
    // Короткая строка попадает в SSO: её байты лежат внутри самого
    // std::string, и рост вектора их ФИЗИЧЕСКИ ДВИГАЕТ. Ровно так ломался
    // UAF-3 (docs/backlog.md B39).
    std::vector<std::string> sources;
    sources.reserve(1);
    sources.emplace_back("user.name");

    CS::Ast ast;
    CS::Diagnostic diag;
    ASSERT_TRUE(CS::parseExpression(sources[0].data(),
                                    static_cast<std::uint32_t>(sources[0].size()),
                                    ast, diag));
    EXPECT_EQ(ast.sourceLength(), sources[0].size());

    // Вектор растёт — sources[0] уезжает на новый адрес.
    const char *before = sources[0].data();
    for (int i = 0; i < 8; ++i) { sources.emplace_back("x"); }
    // Тест обязан проверить своё предположение: если reserve(1) когда-нибудь
    // выдаст ёмкость на все девять, роста не случится и проверять станет
    // нечего, а тест продолжит зеленеть.
    ASSERT_NE(sources[0].data(), before);

    // Дерево ничего не заметило: исходник приходит параметром.
    EXPECT_EQ(ast.text(ast.child(ast.root(), 0), sources[0]), "user");
}

TEST(CalleeRef, BuiltinRoundTrips) {
    for (int i = 0; i <= static_cast<int>(CS::Builtin::Str); ++i) {
        const CS::Builtin id = static_cast<CS::Builtin>(i);
        const CS::CalleeRef ref = CS::calleeOfBuiltin(id);
        EXPECT_FALSE(CS::isHostCallee(ref));
        EXPECT_EQ(CS::builtinOfCallee(ref), id);
    }
}

TEST(CalleeRef, HostRoundTrips) {
    for (std::uint8_t i = 0; i < CS::kMaxHostFunctions; ++i) {
        const CS::CalleeRef ref = CS::calleeOfHost(i);
        EXPECT_TRUE(CS::isHostCallee(ref));
        EXPECT_EQ(CS::hostIndexOfCallee(ref), i);
    }
}

/// Ссылка на хост-функцию с наибольшим допустимым номером обязана
/// отличаться от «не разрешено»: иначе последняя зарегистрированная функция
/// выглядела бы неразрешённым именем.
TEST(CalleeRef, LastHostIndexIsNotNoCallee) {
    EXPECT_NE(CS::calleeOfHost(CS::kMaxHostFunctions - 1), CS::kNoCallee);
}

TEST(AstNode, CalleeStartsUnresolved) {
    CS::Ast ast;
    const CS::NodeId node = ast.call(ident(0, 3), nullptr, 0);
    EXPECT_FALSE(ast.hasCallee(node));
}

TEST(AstNode, CalleeSurvivesRoundTrip) {
    CS::Ast ast;
    const CS::NodeId node = ast.call(ident(0, 3), nullptr, 0);
    ast.setCallee(node, CS::calleeOfHost(5));
    EXPECT_TRUE(ast.hasCallee(node));
    EXPECT_TRUE(CS::isHostCallee(ast.callee(node)));
    EXPECT_EQ(CS::hostIndexOfCallee(ast.callee(node)), 5);
}

}  // namespace
