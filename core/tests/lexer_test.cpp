// Тесты лексера по docs/grammar.md §4.
#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

#include "diagnostic.hpp"
#include "lexer.hpp"
#include "token.hpp"

namespace {

using CS::TokenKind;

struct Lexed {
    std::vector<CS::Token> tokens;
    bool ok = true;
    CS::Diagnostic diag;
};

/// Прогоняет лексер до конца текста или до первой ошибки.
Lexed lexAll(const std::string &source) {
    Lexed result;
    CS::Lexer lexer(source.data(), static_cast<std::uint32_t>(source.size()));
    for (;;) {
        CS::Token token;
        if (!lexer.next(token, result.diag)) {
            result.ok = false;
            return result;
        }
        result.tokens.push_back(token);
        if (token.kind == TokenKind::End) {
            return result;
        }
    }
}

std::vector<TokenKind> kinds(const Lexed &lexed) {
    std::vector<TokenKind> result;
    result.reserve(lexed.tokens.size());
    for (const CS::Token &token : lexed.tokens) {
        result.push_back(token.kind);
    }
    return result;
}

/// Текст токена в исходнике.
std::string text(const std::string &source, const CS::Token &token) {
    return source.substr(token.offset, token.length);
}

// ─── §4.1, §4.2: пробельные символы и комментарии ────────────────────

TEST(LexerTrivia, EmptySourceYieldsEnd) {
    const Lexed lexed = lexAll("");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed), std::vector<TokenKind>{TokenKind::End});
}

TEST(LexerTrivia, WhitespaceOnlyYieldsEnd) {
    const Lexed lexed = lexAll(" \t\n\r ");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed), std::vector<TokenKind>{TokenKind::End});
}

TEST(LexerTrivia, LineCommentRunsToNewline) {
    const Lexed lexed = lexAll("// комментарий\n;");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed),
              (std::vector<TokenKind>{TokenKind::Semicolon, TokenKind::End}));
}

TEST(LexerTrivia, LineCommentMayEndWithSource) {
    const Lexed lexed = lexAll("// до конца");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed), std::vector<TokenKind>{TokenKind::End});
}

TEST(LexerTrivia, BlockCommentIsSkipped) {
    const Lexed lexed = lexAll("/* a */;/* b */");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed),
              (std::vector<TokenKind>{TokenKind::Semicolon, TokenKind::End}));
}

TEST(LexerTrivia, BlockCommentDoesNotNest) {
    // Первое */ закрывает комментарий, остаток — обычный текст.
    const Lexed lexed = lexAll("/* /* */;");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed),
              (std::vector<TokenKind>{TokenKind::Semicolon, TokenKind::End}));
}

TEST(LexerTrivia, UnterminatedBlockCommentIsError) {
    const Lexed lexed = lexAll("/* abc");
    ASSERT_FALSE(lexed.ok);
    EXPECT_EQ(lexed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(lexed.diag.offset, 0u);
}

TEST(LexerTrivia, EndIsStableAcrossRepeatedCalls) {
    const std::string source = "";
    CS::Lexer lexer(source.data(), 0);
    CS::Token token;
    CS::Diagnostic diag;
    ASSERT_TRUE(lexer.next(token, diag));
    EXPECT_EQ(token.kind, TokenKind::End);
    ASSERT_TRUE(lexer.next(token, diag));
    EXPECT_EQ(token.kind, TokenKind::End);
}

// ─── §4.8, §4.3: пунктуаторы и максимальное жевание ──────────────────

TEST(LexerPunctuator, SingleByteTokens) {
    // Пробелы перед '< > =' обязательны: без них максимальное жевание даст '>='.
    const Lexed lexed = lexAll("()[]{},:;.?+-*/%! < > =");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed),
              (std::vector<TokenKind>{
                  TokenKind::LParen, TokenKind::RParen, TokenKind::LBracket,
                  TokenKind::RBracket, TokenKind::LBrace, TokenKind::RBrace,
                  TokenKind::Comma, TokenKind::Colon, TokenKind::Semicolon,
                  TokenKind::Dot, TokenKind::Question, TokenKind::Plus,
                  TokenKind::Minus, TokenKind::Star, TokenKind::Slash,
                  TokenKind::Percent, TokenKind::Bang, TokenKind::Less,
                  TokenKind::Greater, TokenKind::Assign, TokenKind::End}));
}

TEST(LexerPunctuator, TwoByteTokensWinOverOneByte) {
    const Lexed lexed = lexAll("+= -= *= /= == != <= >= && || ??");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed),
              (std::vector<TokenKind>{
                  TokenKind::PlusAssign, TokenKind::MinusAssign,
                  TokenKind::StarAssign, TokenKind::SlashAssign,
                  TokenKind::Equal, TokenKind::NotEqual, TokenKind::LessEqual,
                  TokenKind::GreaterEqual, TokenKind::AndAnd, TokenKind::OrOr,
                  TokenKind::QuestionQuestion, TokenKind::End}));
}

TEST(LexerPunctuator, WhitespaceSplitsTwoByteToken) {
    const Lexed lexed = lexAll("+ =");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed), (std::vector<TokenKind>{TokenKind::Plus,
                                                    TokenKind::Assign,
                                                    TokenKind::End}));
}

TEST(LexerPunctuator, SingleAmpersandIsError) {
    // '&&' съедается целиком, третий '&' остаётся одиночным и недопустим.
    const Lexed lexed = lexAll("&&&");
    ASSERT_FALSE(lexed.ok);
    EXPECT_EQ(lexed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(lexed.diag.offset, 2u);
}

TEST(LexerPunctuator, SinglePipeIsError) {
    const Lexed lexed = lexAll("|");
    ASSERT_FALSE(lexed.ok);
    EXPECT_EQ(lexed.diag.code, CS::ErrorCode::Syntax);
}

TEST(LexerPunctuator, ForbiddenBytesAreErrors) {
    for (const std::string source : {"@", "#", "$", "^", "~", "\\"}) {
        const Lexed lexed = lexAll(source);
        EXPECT_FALSE(lexed.ok) << "должно быть ошибкой: " << source;
        EXPECT_EQ(lexed.diag.code, CS::ErrorCode::Syntax) << source;
        EXPECT_EQ(lexed.diag.offset, 0u) << source;
    }
}

TEST(LexerPunctuator, ByteOrderMarkIsError) {
    const Lexed lexed = lexAll("\xEF\xBB\xBF;");
    ASSERT_FALSE(lexed.ok);
    EXPECT_EQ(lexed.diag.offset, 0u);
}

TEST(LexerPunctuator, SlashAssignBeatsComment) {
    const Lexed lexed = lexAll("/=");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed),
              (std::vector<TokenKind>{TokenKind::SlashAssign, TokenKind::End}));
}

// ─── §4.4, §4.5: идентификаторы и ключевые слова ─────────────────────

TEST(LexerIdentifier, SimpleIdentifiers) {
    const std::string source = "user _x a1 __";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 5u);
    EXPECT_EQ(lexed.tokens[0].kind, TokenKind::Identifier);
    EXPECT_EQ(text(source, lexed.tokens[0]), "user");
    EXPECT_EQ(text(source, lexed.tokens[1]), "_x");
    EXPECT_EQ(text(source, lexed.tokens[2]), "a1");
    EXPECT_EQ(text(source, lexed.tokens[3]), "__");
}

TEST(LexerIdentifier, IdentifierCannotStartWithDigit) {
    const std::string source = "1abc";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 3u);
    EXPECT_EQ(lexed.tokens[0].kind, TokenKind::Number);
    EXPECT_EQ(lexed.tokens[1].kind, TokenKind::Identifier);
    EXPECT_EQ(text(source, lexed.tokens[1]), "abc");
}

TEST(LexerIdentifier, NonAsciiIsNotAnIdentifier) {
    const Lexed lexed = lexAll("привет");
    ASSERT_FALSE(lexed.ok);
    EXPECT_EQ(lexed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(lexed.diag.offset, 0u);
}

TEST(LexerIdentifier, ActiveKeywords) {
    const Lexed lexed = lexAll("true false null");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed), (std::vector<TokenKind>{TokenKind::True,
                                                    TokenKind::False,
                                                    TokenKind::Null,
                                                    TokenKind::End}));
}

TEST(LexerIdentifier, EveryReservedWordIsReserved) {
    const char *const reserved[] = {
        "let",   "var",     "val",      "const",  "void",   "typeof",
        "if",    "else",    "while",    "for",    "in",     "break",
        "continue", "do",   "function", "func",   "return", "this",
        "self",  "new",     "delete",   "switch", "case",   "default",
        "try",   "catch",   "throw",    "import", "export", "class"};
    for (const char *word : reserved) {
        const Lexed lexed = lexAll(word);
        ASSERT_TRUE(lexed.ok) << word;
        ASSERT_EQ(lexed.tokens.size(), 2u) << word;
        EXPECT_EQ(lexed.tokens[0].kind, TokenKind::Reserved) << word;
    }
}

TEST(LexerIdentifier, KeywordPrefixesAreIdentifiers) {
    const char *const words[] = {"i",     "ifx",    "lets",  "returns",
                                 "class1", "trues", "nullish"};
    for (const char *word : words) {
        const Lexed lexed = lexAll(word);
        ASSERT_TRUE(lexed.ok) << word;
        ASSERT_EQ(lexed.tokens.size(), 2u) << word;
        EXPECT_EQ(lexed.tokens[0].kind, TokenKind::Identifier) << word;
    }
}

TEST(LexerIdentifier, KeywordKindIsPure) {
    EXPECT_EQ(CS::keywordKind("true", 4), TokenKind::True);
    EXPECT_EQ(CS::keywordKind("false", 5), TokenKind::False);
    EXPECT_EQ(CS::keywordKind("null", 4), TokenKind::Null);
    EXPECT_EQ(CS::keywordKind("let", 3), TokenKind::Reserved);
    EXPECT_EQ(CS::keywordKind("user", 4), TokenKind::Identifier);
    EXPECT_EQ(CS::keywordKind("", 0), TokenKind::Identifier);
}

// ─── §4.6: числовые литералы ─────────────────────────────────────────

TEST(LexerNumber, IntegerAndFraction) {
    const Lexed lexed = lexAll("3 3.0 0.5 1000000");
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 5u);
    EXPECT_EQ(lexed.tokens[0].kind, TokenKind::Number);
    EXPECT_DOUBLE_EQ(lexed.tokens[0].number, 3.0);
    EXPECT_DOUBLE_EQ(lexed.tokens[1].number, 3.0);
    EXPECT_DOUBLE_EQ(lexed.tokens[2].number, 0.5);
    EXPECT_DOUBLE_EQ(lexed.tokens[3].number, 1000000.0);
}

TEST(LexerNumber, LeadingDotIsNotANumber) {
    const Lexed lexed = lexAll(".5");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed), (std::vector<TokenKind>{TokenKind::Dot,
                                                    TokenKind::Number,
                                                    TokenKind::End}));
}

TEST(LexerNumber, TrailingDotSplitsIntoTwoTokens) {
    // docs/grammar.md §4.3: 3.foo → 3 . foo
    const std::string source = "3.foo";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 4u);
    EXPECT_EQ(lexed.tokens[0].kind, TokenKind::Number);
    EXPECT_DOUBLE_EQ(lexed.tokens[0].number, 3.0);
    EXPECT_EQ(lexed.tokens[1].kind, TokenKind::Dot);
    EXPECT_EQ(lexed.tokens[2].kind, TokenKind::Identifier);
}

TEST(LexerNumber, TrailingDotAtEndOfSource) {
    const Lexed lexed = lexAll("3.");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed), (std::vector<TokenKind>{TokenKind::Number,
                                                    TokenKind::Dot,
                                                    TokenKind::End}));
}

TEST(LexerNumber, ExponentIsNotPartOfLiteral) {
    const std::string source = "1e3";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 3u);
    EXPECT_EQ(lexed.tokens[0].kind, TokenKind::Number);
    EXPECT_DOUBLE_EQ(lexed.tokens[0].number, 1.0);
    EXPECT_EQ(lexed.tokens[1].kind, TokenKind::Identifier);
    EXPECT_EQ(text(source, lexed.tokens[1]), "e3");
}

TEST(LexerNumber, HexIsNotPartOfLiteral) {
    const std::string source = "0x1F";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 3u);
    EXPECT_DOUBLE_EQ(lexed.tokens[0].number, 0.0);
    EXPECT_EQ(text(source, lexed.tokens[1]), "x1F");
}

TEST(LexerNumber, UnderscoreIsNotADigitSeparator) {
    const std::string source = "1_000";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 3u);
    EXPECT_DOUBLE_EQ(lexed.tokens[0].number, 1.0);
    EXPECT_EQ(text(source, lexed.tokens[1]), "_000");
}

TEST(LexerNumber, SignIsNotPartOfLiteral) {
    const Lexed lexed = lexAll("-1");
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed), (std::vector<TokenKind>{TokenKind::Minus,
                                                    TokenKind::Number,
                                                    TokenKind::End}));
}

TEST(LexerNumber, FractionIsExact) {
    const Lexed lexed = lexAll("0.1");
    ASSERT_TRUE(lexed.ok);
    EXPECT_DOUBLE_EQ(lexed.tokens[0].number, 0.1);
}

TEST(LexerNumber, OverflowYieldsInfinity) {
    const std::string source(400, '9');
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 2u);
    EXPECT_EQ(lexed.tokens[0].kind, TokenKind::Number);
    EXPECT_TRUE(std::isinf(lexed.tokens[0].number));
    EXPECT_GT(lexed.tokens[0].number, 0.0);
}

TEST(LexerNumber, UnderflowYieldsZero) {
    const std::string source = "0." + std::string(400, '0') + "1";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 2u);
    EXPECT_EQ(lexed.tokens[0].kind, TokenKind::Number);
    EXPECT_DOUBLE_EQ(lexed.tokens[0].number, 0.0);
}

// ─── §4.7: строковые литералы ────────────────────────────────────────

TEST(LexerString, BothQuoteFormsAreEquivalent) {
    const std::string source = "'abc' \"abc\"";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 3u);
    EXPECT_EQ(lexed.tokens[0].kind, TokenKind::String);
    EXPECT_EQ(lexed.tokens[1].kind, TokenKind::String);
    EXPECT_EQ(CS::stringContentLength(lexed.tokens[0]), 3u);
    EXPECT_EQ(CS::stringContentLength(lexed.tokens[1]), 3u);
}

TEST(LexerString, OtherQuoteNeedsNoEscape) {
    const std::string source = "'он сказал \"да\"'";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 2u);
    EXPECT_EQ(lexed.tokens[0].kind, TokenKind::String);
    EXPECT_FALSE(lexed.tokens[0].hasEscape);
}

TEST(LexerString, EmptyString) {
    const Lexed lexed = lexAll("''");
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 2u);
    EXPECT_EQ(CS::stringContentLength(lexed.tokens[0]), 0u);
}

TEST(LexerString, EscapeIsFlagged) {
    const Lexed lexed = lexAll("'a\\nb'");
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 2u);
    EXPECT_TRUE(lexed.tokens[0].hasEscape);
}

TEST(LexerString, AllKnownEscapes) {
    const Lexed lexed = lexAll("'\\\\ \\' \\\" \\n \\t'");
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 2u);
    EXPECT_EQ(lexed.tokens[0].kind, TokenKind::String);
    EXPECT_TRUE(lexed.tokens[0].hasEscape);
}

TEST(LexerString, EscapedQuoteDoesNotCloseLiteral) {
    const std::string source = "'a\\'b'";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 2u);
    EXPECT_EQ(lexed.tokens[0].length, 6u);
}

TEST(LexerString, HighBytesArePreserved) {
    const std::string source = "'Привет \xF0\x9F\x98\x80'";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 2u);
    EXPECT_EQ(lexed.tokens[0].kind, TokenKind::String);
    EXPECT_FALSE(lexed.tokens[0].hasEscape);
    EXPECT_EQ(CS::stringContentLength(lexed.tokens[0]),
              static_cast<std::uint32_t>(source.size() - 2));
}

TEST(LexerString, UnknownEscapeIsError) {
    const Lexed lexed = lexAll("'\\q'");
    ASSERT_FALSE(lexed.ok);
    EXPECT_EQ(lexed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(lexed.diag.offset, 1u);
}

TEST(LexerString, UnicodeEscapeIsUnknown) {
    const Lexed lexed = lexAll("'\\u0041'");
    ASSERT_FALSE(lexed.ok);
    EXPECT_EQ(lexed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(lexed.diag.offset, 1u);
}

TEST(LexerString, UnterminatedLiteralIsError) {
    const Lexed lexed = lexAll("'abc");
    ASSERT_FALSE(lexed.ok);
    EXPECT_EQ(lexed.diag.offset, 0u);
}

TEST(LexerString, LineBreakInsideLiteralIsError) {
    const Lexed lexed = lexAll("'abc\ndef'");
    ASSERT_FALSE(lexed.ok);
    EXPECT_EQ(lexed.diag.offset, 4u);
}

TEST(LexerString, CarriageReturnInsideLiteralIsError) {
    const Lexed lexed = lexAll("'abc\rdef'");
    ASSERT_FALSE(lexed.ok);
    EXPECT_EQ(lexed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(lexed.diag.offset, 4u);
}

TEST(LexerString, TrailingBackslashIsError) {
    const Lexed lexed = lexAll("'abc\\");
    ASSERT_FALSE(lexed.ok);
    EXPECT_EQ(lexed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(lexed.diag.offset, 0u);
}

// ─── Совместный разбор ───────────────────────────────────────────────

TEST(LexerProgram, RealisticScript) {
    const std::string source =
        "push(state.items, product);\n"
        "state.badge = count(state.items);\n"
        "state.total += product.price;";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(lexed.tokens.back().kind, TokenKind::End);
    // 9 + 11 + 8 токенов плюс End.
    EXPECT_EQ(lexed.tokens.size(), 29u);
}

TEST(LexerProgram, RealisticExpression) {
    const std::string source = "user.name ?? 'Гость'";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    EXPECT_EQ(kinds(lexed),
              (std::vector<TokenKind>{TokenKind::Identifier, TokenKind::Dot,
                                      TokenKind::Identifier,
                                      TokenKind::QuestionQuestion,
                                      TokenKind::String, TokenKind::End}));
}

TEST(LexerProgram, OffsetsPointAtSource) {
    const std::string source = "  user.name";
    const Lexed lexed = lexAll(source);
    ASSERT_TRUE(lexed.ok);
    ASSERT_EQ(lexed.tokens.size(), 4u);
    EXPECT_EQ(lexed.tokens[0].offset, 2u);
    EXPECT_EQ(lexed.tokens[0].length, 4u);
    EXPECT_EQ(lexed.tokens[1].offset, 6u);
    EXPECT_EQ(lexed.tokens[2].offset, 7u);
    EXPECT_EQ(lexed.tokens[3].offset, 11u);
}

// ─── Состояние после ошибки ──────────────────────────────────────────

TEST(LexerFailure, ErrorIsLatchedAndRepeats) {
    const std::string source = "@;";
    CS::Lexer lexer(source.data(), static_cast<std::uint32_t>(source.size()));
    CS::Token token;
    CS::Diagnostic first;
    ASSERT_FALSE(lexer.next(token, first));
    for (int attempt = 0; attempt < 3; ++attempt) {
        CS::Diagnostic again;
        ASSERT_FALSE(lexer.next(token, again)) << "попытка " << attempt;
        EXPECT_EQ(again.code, first.code);
        EXPECT_EQ(again.offset, first.offset);
    }
}

TEST(LexerFailure, UnterminatedBlockCommentDoesNotResumeAsEnd) {
    // Раньше этот путь двигал pos_ в конец, и следующий вызов возвращал End.
    const std::string source = "/* abc";
    CS::Lexer lexer(source.data(), static_cast<std::uint32_t>(source.size()));
    CS::Token token;
    CS::Diagnostic diag;
    ASSERT_FALSE(lexer.next(token, diag));
    CS::Diagnostic again;
    ASSERT_FALSE(lexer.next(token, again));
    EXPECT_EQ(again.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(again.offset, 0u);
}

}  // namespace
