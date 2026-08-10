# ChupaScript: лексер — план реализации

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Реализовать лексер ChupaScript, преобразующий байты исходного текста в поток токенов согласно `docs/grammar.md` §3–§4, с полным покрытием тестами и защитой от деградации производительности.

**Architecture:** Потоковый лексер без аллокаций: `Lexer::next()` выдаёт по одному токену, токен не владеет ничем и указывает в исходный буфер парой «смещение + длина». Ошибка возвращается признаком `false` и заполняет `Diagnostic` с кодом, смещением и статическим сообщением — та же схема, что у публичного C API. Ключевые слова распознаются переключателем по длине с `memcmp`, числовой литерал конвертируется `std::from_chars`.

**Tech Stack:** C++17, CMake ≥ 3.20, GoogleTest 1.15.2, Google Benchmark 1.9.1, Apple clang / libc++.

## Порядок работы

Слой делается в три фазы, порядок обязателен:

1. **Интерфейсы и тесты** (задачи 1–3): заголовки, полный набор тестов по спецификации, бенчмарки и механизм сравнения. После фазы проект собирается, тесты падают.
2. **Реализация порциями** (задачи 4–8): каждая порция зеленит свою группу тестов, снимает базу своего бенчмарка и обязана не ухудшить базы предыдущих порций.
3. **Замыкание** (задача 9): полный прогон, реалистичный бенчмарк, фиксация базы.

## Global Constraints

- **Стандарт:** C++17, без расширений (`CMAKE_CXX_EXTENSIONS OFF`).
- **Пространство имён:** `CS` — как в существующем `core/src/value.hpp`.
- **Размещение:** внутренние заголовки в `core/src/`, публичный C-заголовок в `core/include/chupascript/`. Лексер внутренний, в публичный заголовок ничего не добавляется.
- **Предупреждения:** сборка идёт с `-Wall -Wextra -Wpedantic -Wshadow -Wold-style-cast -Wcast-align -Wunused -Wdouble-promotion -Wformat=2`. Приведения только `static_cast`, C-стиль запрещён компилятором.
- **Без аллокаций и без исключений** на пути лексера: все функции `noexcept`, ни одного `new`, `malloc`, `std::string` внутри `core/src/lexer.*`.
- **Числовой литерал** конвертируется только через `std::from_chars` с `std::chars_format::fixed` (`docs/grammar.md` §4.6). `strtod` запрещён: зависит от локали.
- **Байты `>= 0x80`** допустимы только внутри строковых литералов и комментариев и переносятся без интерпретации (`docs/grammar.md` §3). Декодирования UTF-8 нет нигде.
- **Смещения — `std::uint32_t`.** Исходник длиннее 4 ГиБ не поддерживается; проверка входной длины — обязанность вызывающего слоя (появится вместе с компилятором в плане 2).
- **Сборка тестов:** `cmake -B build && cmake --build build -j`, прогон `ctest --test-dir build --output-on-failure`.
- **Сборка бенчмарков:** только Release — `cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON`.

## Решение по расхождению в спецификации

`docs/grammar.md` §4.3 задаёт для `3.foo` токены `3` `.` `foo`, то есть лексер на последовательности `3.` не ошибается. §4.9 перечисляет «числовой литерал с точкой без дробной части (`3.`)» среди ранних ошибок лексера. Это один и тот же префикс, и два правила противоречат друг другу.

**Реализуется §4.3:** лексер выдаёт `Number` и `Dot`, не сообщая об ошибке. Текст `3.` в конце исходника отвергнет парсер (после `.` требуется `Identifier`). §4.9 подлежит правке в отдельном коммите к спецификации — в этом плане она не делается.

## Структура файлов

| Файл | Ответственность |
|---|---|
| `core/src/diagnostic.hpp` | `ErrorCode`, `Diagnostic` — общий канал ошибок для всех внутренних слоёв |
| `core/src/token.hpp` | `TokenKind`, `Token` — результат лексера, без поведения |
| `core/src/lexer.hpp` | Объявление `Lexer` и свободной функции `keywordKind` |
| `core/src/lexer.cpp` | Сканер целиком |
| `core/tests/lexer_test.cpp` | Тесты по `docs/grammar.md` §4 |
| `benchmarks/lexer_benchmark.cpp` | Шесть бенчмарков по подмножествам лексики |
| `tools/bench-compare.py` | Сравнение двух прогонов Google Benchmark, поиск деградации |
| `benchmarks/baseline.json` | Зафиксированная база, обновляется осознанно |

---

## Task 1: Интерфейсы и сборка

**Files:**
- Create: `core/src/diagnostic.hpp`
- Create: `core/src/token.hpp`
- Create: `core/src/lexer.hpp`
- Create: `core/src/lexer.cpp`
- Modify: `core/CMakeLists.txt`

**Interfaces:**
- Consumes: ничего.
- Produces: `CS::ErrorCode`, `CS::Diagnostic`, `CS::TokenKind`, `CS::Token`, `CS::stringContentOffset`, `CS::stringContentLength`, `CS::keywordKind(const char *, std::uint32_t) noexcept -> TokenKind`, `CS::Lexer::Lexer(const char *, std::uint32_t) noexcept`, `CS::Lexer::next(Token &, Diagnostic &) noexcept -> bool`.

- [ ] **Step 1: Создать `core/src/diagnostic.hpp`**

```cpp
#pragma once
#include <cstdint>

namespace CS {

/// Класс ошибки.
///
/// Значения намеренно совпадают с ChupaErrorCode из публичного C-заголовка;
/// соответствие будет закреплено static_assert'ами, когда тот появится.
enum class ErrorCode : std::uint8_t {
    None = 0,
    Syntax,  ///< лексика и синтаксис
    Name,    ///< неизвестный корень, функция, число аргументов
    Type,    ///< не тот тип операнда при выполнении
    Range,   ///< индекс массива, запись за границу
    Data,    ///< некорректный JSON во входных данных
    Usage,   ///< нарушён порядок вызовов
    Memory
};

/// Описание одной неудачи.
///
/// message — статическая строка; Diagnostic ничем не владеет и свободно
/// копируется.
struct Diagnostic {
    ErrorCode code = ErrorCode::None;
    std::uint32_t offset = 0;  ///< смещение в байтах от начала исходника
    const char *message = "";
};

}  // namespace CS
```

- [ ] **Step 2: Создать `core/src/token.hpp`**

```cpp
#pragma once
#include <cstdint>

namespace CS {

/// Вид токена. Соответствует docs/grammar.md §4.
enum class TokenKind : std::uint8_t {
    End,  ///< конец текста

    Identifier,
    Number,
    String,

    True,      ///< активные ключевые слова, §4.5
    False,
    Null,
    Reserved,  ///< зарезервировано на будущее, §4.5

    LParen,    ///< (
    RParen,    ///< )
    LBracket,  ///< [
    RBracket,  ///< ]
    LBrace,    ///< {
    RBrace,    ///< }

    Comma,      ///< ,
    Colon,      ///< :
    Semicolon,  ///< ;
    Dot,        ///< .
    Question,   ///< ?

    Plus,     ///< +
    Minus,    ///< -
    Star,     ///< *
    Slash,    ///< /
    Percent,  ///< %
    Bang,     ///< !

    Assign,       ///< =
    PlusAssign,   ///< +=
    MinusAssign,  ///< -=
    StarAssign,   ///< *=
    SlashAssign,  ///< /=

    Equal,         ///< ==
    NotEqual,      ///< !=
    Less,          ///< <
    Greater,       ///< >
    LessEqual,     ///< <=
    GreaterEqual,  ///< >=

    AndAnd,            ///< &&
    OrOr,              ///< ||
    QuestionQuestion   ///< ??
};

/// Один токен.
///
/// Ничем не владеет: [offset, offset + length) — участок исходного буфера,
/// который обязан пережить токен. Для String участок включает кавычки.
struct Token {
    TokenKind kind = TokenKind::End;
    bool hasEscape = false;    ///< String: содержит escape-последовательность
    std::uint32_t offset = 0;
    std::uint32_t length = 0;
    double number = 0.0;       ///< Number: значение литерала
};

/// Смещение содержимого строкового литерала. Требует kind == String.
inline std::uint32_t stringContentOffset(const Token &token) noexcept {
    return token.offset + 1;
}

/// Длина содержимого строкового литерала. Требует kind == String.
inline std::uint32_t stringContentLength(const Token &token) noexcept {
    return token.length - 2;
}

}  // namespace CS
```

- [ ] **Step 3: Создать `core/src/lexer.hpp`**

```cpp
#pragma once
#include <cstdint>

#include "diagnostic.hpp"
#include "token.hpp"

namespace CS {

/// Классифицирует последовательность как ключевое слово (docs/grammar.md §4.5).
///
/// Возвращает TokenKind::Identifier, если слово не зарезервировано.
TokenKind keywordKind(const char *text, std::uint32_t length) noexcept;

/// Сканер исходного текста.
///
/// Не аллоцирует и не бросает исключений. Буфер source обязан пережить лексер.
class Lexer {
   public:
    Lexer(const char *source, std::uint32_t length) noexcept
        : src_(source), len_(length), pos_(0) {}

    /// Читает очередной токен.
    ///
    /// Возвращает false при ошибке и заполняет diag. В конце текста возвращает
    /// true и out.kind == TokenKind::End; повторные вызовы после этого дают
    /// End снова.
    bool next(Token &out, Diagnostic &diag) noexcept;

   private:
    bool skipTrivia(Diagnostic &diag) noexcept;
    bool lexString(Token &out, Diagnostic &diag) noexcept;
    bool lexNumber(Token &out, Diagnostic &diag) noexcept;
    void lexIdentifier(Token &out) noexcept;

    const char *src_;
    std::uint32_t len_;
    std::uint32_t pos_;
};

}  // namespace CS
```

- [ ] **Step 4: Создать `core/src/lexer.cpp` с заглушкой**

```cpp
#include "lexer.hpp"

namespace CS {

TokenKind keywordKind(const char *, std::uint32_t) noexcept {
    return TokenKind::Identifier;
}

bool Lexer::next(Token &, Diagnostic &diag) noexcept {
    diag = Diagnostic{ErrorCode::Syntax, 0, "lexer not implemented"};
    return false;
}

bool Lexer::skipTrivia(Diagnostic &) noexcept { return true; }
bool Lexer::lexString(Token &, Diagnostic &) noexcept { return false; }
bool Lexer::lexNumber(Token &, Diagnostic &) noexcept { return false; }
void Lexer::lexIdentifier(Token &) noexcept {}

}  // namespace CS
```

- [ ] **Step 5: Подключить исходник к библиотеке**

В `core/CMakeLists.txt` заменить

```cmake
add_library(chupascript STATIC
    src/version.cpp
)
```

на

```cmake
add_library(chupascript STATIC
    src/lexer.cpp
    src/version.cpp
)
```

- [ ] **Step 6: Проверить, что всё собирается**

Run: `cmake -B build && cmake --build build -j`
Expected: сборка проходит без предупреждений.

- [ ] **Step 7: Коммит**

```bash
git add core/src/diagnostic.hpp core/src/token.hpp core/src/lexer.hpp core/src/lexer.cpp core/CMakeLists.txt
git commit -m "Add lexer interfaces"
```

---

## Task 2: Тестовый набор по §4

Тесты пишутся целиком до реализации. После задачи почти все падают — это ожидаемо и является предметом приёмки.

**Files:**
- Create: `core/tests/lexer_test.cpp`
- Modify: `core/tests/CMakeLists.txt`

**Interfaces:**
- Consumes: всё из задачи 1.
- Produces: вспомогательные `lexAll(const std::string &) -> Lexed` и `kinds(const Lexed &) -> std::vector<CS::TokenKind>` внутри анонимного пространства имён теста; наружу ничего.

- [ ] **Step 1: Создать `core/tests/lexer_test.cpp`**

```cpp
// Тесты лексера по docs/grammar.md §4.
#include <gtest/gtest.h>

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
    EXPECT_FALSE(lexed.ok);
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

TEST(LexerNumber, OutOfRangeLiteralIsError) {
    const std::string source(400, '9');
    const Lexed lexed = lexAll(source);
    ASSERT_FALSE(lexed.ok);
    EXPECT_EQ(lexed.diag.code, CS::ErrorCode::Syntax);
    EXPECT_EQ(lexed.diag.offset, 0u);
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
}

TEST(LexerString, TrailingBackslashIsError) {
    const Lexed lexed = lexAll("'abc\\");
    ASSERT_FALSE(lexed.ok);
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

}  // namespace
```

- [ ] **Step 2: Подключить тест к сборке**

В `core/tests/CMakeLists.txt` заменить

```cmake
add_executable(chupascript_tests
    smoke_test.cpp
)
```

на

```cmake
add_executable(chupascript_tests
    lexer_test.cpp
    smoke_test.cpp
)
```

- [ ] **Step 3: Убедиться, что тесты собираются и падают**

Run: `cmake --build build -j && ./build/core/tests/chupascript_tests`
Expected: сборка проходит; тесты `Lexer*` падают (заглушка возвращает ошибку), `Smoke.VersionIsReported` проходит.

- [ ] **Step 4: Коммит**

```bash
git add core/tests/lexer_test.cpp core/tests/CMakeLists.txt
git commit -m "Add lexer test suite"
```

---

## Task 3: Бенчмарки и сравнение с базой

**Files:**
- Create: `benchmarks/lexer_benchmark.cpp`
- Create: `tools/bench-compare.py`
- Modify: `benchmarks/CMakeLists.txt`

**Interfaces:**
- Consumes: `CS::Lexer`, `CS::Token`, `CS::Diagnostic` из задачи 1.
- Produces: цели бенчмарков `BM_Lex_Trivia`, `BM_Lex_Punctuators`, `BM_Lex_Identifiers`, `BM_Lex_Numbers`, `BM_Lex_Strings`, `BM_Lex_Realistic`; скрипт `tools/bench-compare.py <baseline.json> <current.json> [--threshold N]`, возвращающий 1 при деградации.

- [ ] **Step 1: Создать `benchmarks/lexer_benchmark.cpp`**

```cpp
// Бенчмарки лексера, по одному на подмножество лексики.
//
// Каждый становится осмысленным, когда садится его порция реализации, и с
// этого момента обязан не деградировать от последующих порций.
#include <benchmark/benchmark.h>

#include <cstdint>
#include <string>

#include "diagnostic.hpp"
#include "lexer.hpp"
#include "token.hpp"

namespace {

void lexAll(const std::string &source) {
    CS::Lexer lexer(source.data(), static_cast<std::uint32_t>(source.size()));
    CS::Token token;
    CS::Diagnostic diag;
    while (lexer.next(token, diag) && token.kind != CS::TokenKind::End) {
        benchmark::DoNotOptimize(token);
    }
}

std::string repeat(const std::string &unit, int times) {
    std::string result;
    result.reserve(unit.size() * static_cast<std::size_t>(times));
    for (int i = 0; i < times; ++i) {
        result += unit;
    }
    return result;
}

void run(benchmark::State &state, const std::string &source) {
    for (auto _ : state) {
        lexAll(source);
    }
    state.SetBytesProcessed(static_cast<std::int64_t>(state.iterations()) *
                            static_cast<std::int64_t>(source.size()));
}

}  // namespace

static void BM_Lex_Trivia(benchmark::State &state) {
    run(state, repeat("   /* комментарий */\n// строка\n", 200));
}
BENCHMARK(BM_Lex_Trivia);

static void BM_Lex_Punctuators(benchmark::State &state) {
    run(state, repeat("+= -= == != <= >= && || ?? ( ) [ ] { } , : ; . ? ", 200));
}
BENCHMARK(BM_Lex_Punctuators);

static void BM_Lex_Identifiers(benchmark::State &state) {
    run(state, repeat("user state items product price count true false null ", 200));
}
BENCHMARK(BM_Lex_Identifiers);

static void BM_Lex_Numbers(benchmark::State &state) {
    run(state, repeat("0 1 42 3.0 0.5 1000000 12.75 ", 200));
}
BENCHMARK(BM_Lex_Numbers);

static void BM_Lex_Strings(benchmark::State &state) {
    run(state, repeat("'простая' 'с \\n экранированием' 'юникод 😀' ", 200));
}
BENCHMARK(BM_Lex_Strings);

static void BM_Lex_Realistic(benchmark::State &state) {
    run(state, repeat(
                   "push(state.items, product);"
                   "state.badge = count(state.items);"
                   "state.total += product.price;"
                   "state.label = format('добавлено ${} на ${}',"
                   " product.name, product.price);",
                   50));
}
BENCHMARK(BM_Lex_Realistic);
```

- [ ] **Step 2: Подключить бенчмарк к сборке**

В `benchmarks/CMakeLists.txt` заменить

```cmake
add_executable(chupascript_benchmarks eval_benchmark.cpp)
```

на

```cmake
add_executable(chupascript_benchmarks
    eval_benchmark.cpp
    lexer_benchmark.cpp
)
```

- [ ] **Step 3: Создать `tools/bench-compare.py`**

```python
#!/usr/bin/env python3
"""Сравнивает два прогона Google Benchmark и находит деградацию.

    python3 tools/bench-compare.py baseline.json current.json [--threshold 10]

Возвращает 1, если cpu_time хотя бы одного бенчмарка вырос больше порога.
Сравнивать имеет смысл только прогоны на одной машине: абсолютные числа
между машинами несопоставимы.
"""
import argparse
import json
import sys


def load(path):
    with open(path, encoding="utf-8") as handle:
        data = json.load(handle)
    return {
        entry["name"]: float(entry["cpu_time"])
        for entry in data["benchmarks"]
        if entry.get("run_type", "iteration") == "iteration"
    }


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline")
    parser.add_argument("current")
    parser.add_argument(
        "--threshold",
        type=float,
        default=10.0,
        help="допустимый рост cpu_time в процентах",
    )
    args = parser.parse_args()

    base = load(args.baseline)
    current = load(args.current)

    print(f"{'benchmark':<24}{'base':>12}{'current':>12}{'change':>10}")
    regressed = []
    for name in sorted(current):
        if name not in base:
            print(f"{name:<24}{'—':>12}{current[name]:>12.1f}{'новый':>10}")
            continue
        change = (current[name] - base[name]) / base[name] * 100.0
        print(f"{name:<24}{base[name]:>12.1f}{current[name]:>12.1f}{change:>9.1f}%")
        if change > args.threshold:
            regressed.append((name, change))

    for name in sorted(set(base) - set(current)):
        print(f"{name:<24}{base[name]:>12.1f}{'—':>12}{'исчез':>10}")

    if regressed:
        print("\nДеградация:")
        for name, change in regressed:
            print(f"  {name}: +{change:.1f}%")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
```

- [ ] **Step 4: Сделать скрипт исполняемым и проверить сборку бенчмарков**

```bash
chmod +x tools/bench-compare.py
cmake -B build-rel -DCMAKE_BUILD_TYPE=Release -DCHUPASCRIPT_BUILD_BENCHMARKS=ON
cmake --build build-rel -j
```

Expected: сборка проходит.

- [ ] **Step 5: Проверить скрипт на самом себе**

```bash
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_out=/tmp/bench-a.json --benchmark_out_format=json
python3 tools/bench-compare.py /tmp/bench-a.json /tmp/bench-a.json
echo "exit=$?"
```

Expected: таблица со всеми изменениями `0.0%`, `exit=0`.

- [ ] **Step 6: Коммит**

```bash
git add benchmarks/lexer_benchmark.cpp benchmarks/CMakeLists.txt tools/bench-compare.py
git commit -m "Add lexer benchmarks and regression comparison"
```

---

## Порядок для задач 4–8

Каждая из следующих задач устроена одинаково:

1. Снять текущий прогон бенчмарков в `/tmp/bench-before.json`.
2. Реализовать порцию.
3. Прогнать тесты — зеленеет ровно указанная группа, ранее зелёные не краснеют.
4. Снять `/tmp/bench-after.json`, сравнить с `before`, убедиться, что ранее измеренные бенчмарки не выросли больше 10%.
5. Коммит.

---

## Task 4: Пропуск незначимого

**Files:**
- Modify: `core/src/lexer.cpp`

**Interfaces:**
- Consumes: `CS::Lexer`, `CS::Diagnostic` из задачи 1.
- Produces: рабочие `Lexer::skipTrivia` и `Lexer::next` в части конца текста; `next` на любом значимом байте пока сообщает `"unexpected byte"`.

- [ ] **Step 1: Снять прогон до изменений**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_out=/tmp/bench-before.json --benchmark_out_format=json
```

- [ ] **Step 2: Заменить `skipTrivia` и `next` в `core/src/lexer.cpp`**

```cpp
bool Lexer::skipTrivia(Diagnostic &diag) noexcept {
    for (;;) {
        while (pos_ < len_) {
            const char c = src_[pos_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++pos_;
                continue;
            }
            break;
        }

        if (pos_ + 1 < len_ && src_[pos_] == '/' && src_[pos_ + 1] == '/') {
            pos_ += 2;
            while (pos_ < len_ && src_[pos_] != '\n') {
                ++pos_;
            }
            continue;
        }

        if (pos_ + 1 < len_ && src_[pos_] == '/' && src_[pos_ + 1] == '*') {
            const std::uint32_t start = pos_;
            pos_ += 2;
            for (;;) {
                if (pos_ + 1 >= len_) {
                    pos_ = len_;
                    diag = Diagnostic{ErrorCode::Syntax, start,
                                      "unterminated block comment"};
                    return false;
                }
                if (src_[pos_] == '*' && src_[pos_ + 1] == '/') {
                    pos_ += 2;
                    break;
                }
                ++pos_;
            }
            continue;
        }

        return true;
    }
}

bool Lexer::next(Token &out, Diagnostic &diag) noexcept {
    if (!skipTrivia(diag)) {
        return false;
    }

    out = Token{};
    out.offset = pos_;

    if (pos_ >= len_) {
        out.kind = TokenKind::End;
        return true;
    }

    diag = Diagnostic{ErrorCode::Syntax, pos_, "unexpected byte"};
    return false;
}
```

- [ ] **Step 3: Прогнать тесты**

Run: `cmake --build build -j && ./build/core/tests/chupascript_tests --gtest_filter=LexerTrivia.*`
Expected: проходят пять тестов — `EmptySourceYieldsEnd`, `WhitespaceOnlyYieldsEnd`, `LineCommentMayEndWithSource`, `UnterminatedBlockCommentIsError`, `EndIsStableAcrossRepeatedCalls`.

Остальные три (`LineCommentRunsToNewline`, `BlockCommentIsSkipped`, `BlockCommentDoesNotNest`) содержат `;` и позеленеют в задаче 5 — пунктуаторов пока нет.

- [ ] **Step 4: Сравнить производительность**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_out=/tmp/bench-after.json --benchmark_out_format=json
python3 tools/bench-compare.py /tmp/bench-before.json /tmp/bench-after.json
```

Expected: `BM_Lex_Trivia` теперь измеряет настоящую работу — это первая его база, рост здесь ожидаем и не является деградацией. Остальные бенчмарки упираются в ошибку на первом же значимом байте.

- [ ] **Step 5: Коммит**

```bash
git add core/src/lexer.cpp
git commit -m "Implement whitespace and comment skipping"
```

---

## Task 5: Пунктуаторы

**Files:**
- Modify: `core/src/lexer.cpp`

**Interfaces:**
- Consumes: `Lexer::skipTrivia` из задачи 4.
- Produces: распознавание всех токенов `Punctuator` из `docs/grammar.md` §4.8 с правилом максимального жевания.

- [ ] **Step 1: Снять прогон до изменений**

```bash
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_out=/tmp/bench-before.json --benchmark_out_format=json
```

- [ ] **Step 2: Заменить хвост `Lexer::next` в `core/src/lexer.cpp`**

Заменить

```cpp
    diag = Diagnostic{ErrorCode::Syntax, pos_, "unexpected byte"};
    return false;
}
```

на

```cpp
    const char c = src_[pos_];

    // Двухбайтовые токены выигрывают у однобайтовых: правило максимального
    // жевания, docs/grammar.md §4.3.
    const bool hasNext = pos_ + 1 < len_;
    const char n = hasNext ? src_[pos_ + 1] : '\0';

    const auto emit = [&out, this](TokenKind kind, std::uint32_t length) noexcept {
        out.kind = kind;
        out.length = length;
        pos_ += length;
        return true;
    };

    switch (c) {
        case '(': return emit(TokenKind::LParen, 1);
        case ')': return emit(TokenKind::RParen, 1);
        case '[': return emit(TokenKind::LBracket, 1);
        case ']': return emit(TokenKind::RBracket, 1);
        case '{': return emit(TokenKind::LBrace, 1);
        case '}': return emit(TokenKind::RBrace, 1);
        case ',': return emit(TokenKind::Comma, 1);
        case ':': return emit(TokenKind::Colon, 1);
        case ';': return emit(TokenKind::Semicolon, 1);
        case '.': return emit(TokenKind::Dot, 1);

        case '+': return emit(n == '=' ? TokenKind::PlusAssign : TokenKind::Plus,
                              n == '=' ? 2 : 1);
        case '-': return emit(n == '=' ? TokenKind::MinusAssign : TokenKind::Minus,
                              n == '=' ? 2 : 1);
        case '*': return emit(n == '=' ? TokenKind::StarAssign : TokenKind::Star,
                              n == '=' ? 2 : 1);
        // '//' и '/*' уже съедены skipTrivia, здесь остаются только '/=' и '/'.
        case '/': return emit(n == '=' ? TokenKind::SlashAssign : TokenKind::Slash,
                              n == '=' ? 2 : 1);
        case '%': return emit(TokenKind::Percent, 1);

        case '=': return emit(n == '=' ? TokenKind::Equal : TokenKind::Assign,
                              n == '=' ? 2 : 1);
        case '!': return emit(n == '=' ? TokenKind::NotEqual : TokenKind::Bang,
                              n == '=' ? 2 : 1);
        case '<': return emit(n == '=' ? TokenKind::LessEqual : TokenKind::Less,
                              n == '=' ? 2 : 1);
        case '>': return emit(n == '=' ? TokenKind::GreaterEqual : TokenKind::Greater,
                              n == '=' ? 2 : 1);
        case '?': return emit(n == '?' ? TokenKind::QuestionQuestion : TokenKind::Question,
                              n == '?' ? 2 : 1);

        // Одиночные '&' и '|' в языке отсутствуют, docs/grammar.md §4.8.
        case '&':
            if (n == '&') {
                return emit(TokenKind::AndAnd, 2);
            }
            break;
        case '|':
            if (n == '|') {
                return emit(TokenKind::OrOr, 2);
            }
            break;

        default:
            break;
    }

    diag = Diagnostic{ErrorCode::Syntax, pos_, "unexpected byte"};
    return false;
}
```

- [ ] **Step 3: Прогнать тесты**

Run: `cmake --build build -j && ./build/core/tests/chupascript_tests --gtest_filter='LexerTrivia.*:LexerPunctuator.*'`
Expected: все восемь `LexerTrivia.*` (включая три, ждавшие `;`) и все семь `LexerPunctuator.*` проходят.

- [ ] **Step 4: Сравнить производительность**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_out=/tmp/bench-after.json --benchmark_out_format=json
python3 tools/bench-compare.py /tmp/bench-before.json /tmp/bench-after.json
```

Expected: `BM_Lex_Trivia` не вырос больше 10%; `BM_Lex_Punctuators` получил первую базу.

- [ ] **Step 5: Коммит**

```bash
git add core/src/lexer.cpp
git commit -m "Implement punctuators with maximal munch"
```

---

## Task 6: Идентификаторы и ключевые слова

**Files:**
- Modify: `core/src/lexer.cpp`

**Interfaces:**
- Consumes: `Lexer::next` из задачи 5.
- Produces: рабочие `CS::keywordKind` и `Lexer::lexIdentifier`; `next` распознаёт `Identifier`, `True`, `False`, `Null`, `Reserved`.

- [ ] **Step 1: Снять прогон до изменений**

```bash
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_out=/tmp/bench-before.json --benchmark_out_format=json
```

- [ ] **Step 2: Добавить `<cstring>` к включениям `core/src/lexer.cpp`**

```cpp
#include <cstring>

#include "lexer.hpp"
```

- [ ] **Step 3: Заменить заглушку `keywordKind`**

```cpp
namespace {

bool sameAs(const char *text, const char *word, std::uint32_t length) noexcept {
    return std::memcmp(text, word, length) == 0;
}

}  // namespace

TokenKind keywordKind(const char *text, std::uint32_t length) noexcept {
    // Переключатель по длине отсекает почти всё до единого memcmp.
    switch (length) {
        case 2:
            if (sameAs(text, "if", 2) || sameAs(text, "do", 2) ||
                sameAs(text, "in", 2)) {
                return TokenKind::Reserved;
            }
            break;
        case 3:
            if (sameAs(text, "let", 3) || sameAs(text, "var", 3) ||
                sameAs(text, "val", 3) || sameAs(text, "for", 3) ||
                sameAs(text, "new", 3) || sameAs(text, "try", 3)) {
                return TokenKind::Reserved;
            }
            break;
        case 4:
            if (sameAs(text, "true", 4)) {
                return TokenKind::True;
            }
            if (sameAs(text, "null", 4)) {
                return TokenKind::Null;
            }
            if (sameAs(text, "else", 4) || sameAs(text, "func", 4) ||
                sameAs(text, "this", 4) || sameAs(text, "self", 4) ||
                sameAs(text, "case", 4) || sameAs(text, "void", 4)) {
                return TokenKind::Reserved;
            }
            break;
        case 5:
            if (sameAs(text, "false", 5)) {
                return TokenKind::False;
            }
            if (sameAs(text, "while", 5) || sameAs(text, "break", 5) ||
                sameAs(text, "const", 5) || sameAs(text, "catch", 5) ||
                sameAs(text, "throw", 5) || sameAs(text, "class", 5)) {
                return TokenKind::Reserved;
            }
            break;
        case 6:
            if (sameAs(text, "return", 6) || sameAs(text, "typeof", 6) ||
                sameAs(text, "delete", 6) || sameAs(text, "switch", 6) ||
                sameAs(text, "import", 6) || sameAs(text, "export", 6)) {
                return TokenKind::Reserved;
            }
            break;
        case 7:
            if (sameAs(text, "default", 7)) {
                return TokenKind::Reserved;
            }
            break;
        case 8:
            if (sameAs(text, "continue", 8) || sameAs(text, "function", 8)) {
                return TokenKind::Reserved;
            }
            break;
        default:
            break;
    }
    return TokenKind::Identifier;
}
```

- [ ] **Step 4: Заменить заглушку `lexIdentifier`**

```cpp
void Lexer::lexIdentifier(Token &out) noexcept {
    std::uint32_t end = pos_ + 1;
    while (end < len_) {
        const char c = src_[end];
        const bool isPart = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '_';
        if (!isPart) {
            break;
        }
        ++end;
    }
    out.length = end - pos_;
    out.kind = keywordKind(src_ + pos_, out.length);
    pos_ = end;
}
```

- [ ] **Step 5: Вызвать `lexIdentifier` из `next`**

Вставить непосредственно перед объявлением `const bool hasNext = ...`:

```cpp
    if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_') {
        lexIdentifier(out);
        return true;
    }
```

- [ ] **Step 6: Прогнать тесты**

Run: `cmake --build build -j && ./build/core/tests/chupascript_tests --gtest_filter='LexerTrivia.*:LexerPunctuator.*:LexerIdentifier.*'`
Expected: проходит всё, кроме `LexerIdentifier.IdentifierCannotStartWithDigit` — он требует числовых литералов и позеленеет в задаче 7.

- [ ] **Step 7: Сравнить производительность**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_out=/tmp/bench-after.json --benchmark_out_format=json
python3 tools/bench-compare.py /tmp/bench-before.json /tmp/bench-after.json
```

Expected: `BM_Lex_Trivia` и `BM_Lex_Punctuators` не выросли больше 10%; `BM_Lex_Identifiers` получил первую базу.

- [ ] **Step 8: Коммит**

```bash
git add core/src/lexer.cpp
git commit -m "Implement identifiers and keyword recognition"
```

---

## Task 7: Числовые литералы

**Files:**
- Modify: `core/src/lexer.cpp`

**Interfaces:**
- Consumes: `Lexer::next` из задачи 6.
- Produces: рабочий `Lexer::lexNumber`, заполняющий `Token::number`.

- [ ] **Step 1: Снять прогон до изменений**

```bash
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_out=/tmp/bench-before.json --benchmark_out_format=json
```

- [ ] **Step 2: Добавить `<charconv>` и `<system_error>` к включениям `core/src/lexer.cpp`**

```cpp
#include <charconv>
#include <cstring>
#include <system_error>

#include "lexer.hpp"
```

- [ ] **Step 3: Заменить заглушку `lexNumber`**

```cpp
bool Lexer::lexNumber(Token &out, Diagnostic &diag) noexcept {
    const std::uint32_t start = pos_;
    std::uint32_t end = pos_;
    while (end < len_ && src_[end] >= '0' && src_[end] <= '9') {
        ++end;
    }

    // Дробная часть берётся, только если за точкой стоит цифра: '3.' числом не
    // является и распадается на Number и Dot (docs/grammar.md §4.3).
    if (end + 1 < len_ && src_[end] == '.' && src_[end + 1] >= '0' &&
        src_[end + 1] <= '9') {
        end += 2;
        while (end < len_ && src_[end] >= '0' && src_[end] <= '9') {
            ++end;
        }
    }

    const std::from_chars_result parsed = std::from_chars(
        src_ + start, src_ + end, out.number, std::chars_format::fixed);
    if (parsed.ec != std::errc() || parsed.ptr != src_ + end) {
        diag = Diagnostic{ErrorCode::Syntax, start, "numeric literal out of range"};
        return false;
    }

    out.kind = TokenKind::Number;
    out.length = end - start;
    pos_ = end;
    return true;
}
```

- [ ] **Step 4: Вызвать `lexNumber` из `next`**

Вставить непосредственно после блока идентификаторов:

```cpp
    if (c >= '0' && c <= '9') {
        return lexNumber(out, diag);
    }
```

- [ ] **Step 5: Прогнать тесты**

Run: `cmake --build build -j && ./build/core/tests/chupascript_tests --gtest_filter='LexerTrivia.*:LexerPunctuator.*:LexerIdentifier.*:LexerNumber.*'`
Expected: проходит всё, включая `LexerIdentifier.IdentifierCannotStartWithDigit`.

- [ ] **Step 6: Сравнить производительность**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_out=/tmp/bench-after.json --benchmark_out_format=json
python3 tools/bench-compare.py /tmp/bench-before.json /tmp/bench-after.json
```

Expected: ранее измеренные бенчмарки не выросли больше 10%; `BM_Lex_Numbers` получил первую базу.

- [ ] **Step 7: Коммит**

```bash
git add core/src/lexer.cpp
git commit -m "Implement numeric literals"
```

---

## Task 8: Строковые литералы

**Files:**
- Modify: `core/src/lexer.cpp`

**Interfaces:**
- Consumes: `Lexer::next` из задачи 7.
- Produces: рабочий `Lexer::lexString`, выставляющий `Token::hasEscape` и участок вместе с кавычками.

- [ ] **Step 1: Снять прогон до изменений**

```bash
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_out=/tmp/bench-before.json --benchmark_out_format=json
```

- [ ] **Step 2: Заменить заглушку `lexString`**

```cpp
bool Lexer::lexString(Token &out, Diagnostic &diag) noexcept {
    const char quote = src_[pos_];
    const std::uint32_t start = pos_;
    std::uint32_t end = pos_ + 1;
    bool hasEscape = false;

    for (;;) {
        if (end >= len_) {
            diag = Diagnostic{ErrorCode::Syntax, start,
                              "unterminated string literal"};
            return false;
        }

        const char c = src_[end];

        if (c == quote) {
            ++end;
            break;
        }

        // Сырой перевод строки обрывает литерал, чтобы забытая кавычка не
        // поглотила остаток программы (docs/grammar.md §4.9).
        if (c == '\n' || c == '\r') {
            diag = Diagnostic{ErrorCode::Syntax, end,
                              "line break in string literal"};
            return false;
        }

        if (c == '\\') {
            if (end + 1 >= len_) {
                diag = Diagnostic{ErrorCode::Syntax, start,
                                  "unterminated string literal"};
                return false;
            }
            const char escaped = src_[end + 1];
            if (escaped != '\\' && escaped != '\'' && escaped != '"' &&
                escaped != 'n' && escaped != 't') {
                diag = Diagnostic{ErrorCode::Syntax, end,
                                  "unknown escape sequence"};
                return false;
            }
            hasEscape = true;
            end += 2;
            continue;
        }

        // Байты >= 0x80 переносятся без интерпретации: UTF-8 не декодируется.
        ++end;
    }

    out.kind = TokenKind::String;
    out.length = end - start;
    out.hasEscape = hasEscape;
    pos_ = end;
    return true;
}
```

- [ ] **Step 3: Вызвать `lexString` из `next`**

Вставить непосредственно после блока числовых литералов:

```cpp
    if (c == '\'' || c == '"') {
        return lexString(out, diag);
    }
```

- [ ] **Step 4: Прогнать весь набор**

Run: `cmake --build build -j && ctest --test-dir build --output-on-failure`
Expected: все тесты проходят.

- [ ] **Step 5: Сравнить производительность**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_out=/tmp/bench-after.json --benchmark_out_format=json
python3 tools/bench-compare.py /tmp/bench-before.json /tmp/bench-after.json
```

Expected: ранее измеренные бенчмарки не выросли больше 10%; `BM_Lex_Strings` и `BM_Lex_Realistic` получили первые базы.

- [ ] **Step 6: Коммит**

```bash
git add core/src/lexer.cpp
git commit -m "Implement string literals"
```

---

## Task 9: Замыкание слоя

**Files:**
- Create: `benchmarks/baseline.json`
- Modify: `core/src/lexer.cpp` (только если санитайзеры что-то найдут)

**Interfaces:**
- Consumes: всё предыдущее.
- Produces: зафиксированная база производительности для последующих слоёв.

- [ ] **Step 1: Прогнать тесты под санитайзерами**

```bash
cmake -B build-san -DCHUPASCRIPT_SANITIZE_ADDRESS=ON \
      -DCHUPASCRIPT_SANITIZE_UNDEFINED=ON
cmake --build build-san -j
ctest --test-dir build-san --output-on-failure
```

Expected: все тесты проходят, ни одного отчёта санитайзера. Лексер читает только внутри `[src_, src_ + len_)`, поэтому находка здесь означает выход за границу — исправить до продолжения.

- [ ] **Step 2: Проверить сборку с предупреждениями как ошибками**

```bash
cmake -B build-werror -DCHUPASCRIPT_WERROR=ON
cmake --build build-werror -j
```

Expected: сборка проходит.

- [ ] **Step 3: Зафиксировать базу**

```bash
cmake --build build-rel -j
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_repetitions=5 --benchmark_report_aggregates_only=true \
    --benchmark_out=benchmarks/baseline.json --benchmark_out_format=json
```

- [ ] **Step 4: Записать в базу пометку о машине**

Добавить в `benchmarks/baseline.json` в объект `context` поле с описанием машины, чтобы сравнение не проводилось вслепую между разными компьютерами:

```bash
python3 - <<'PY'
import json, platform
path = "benchmarks/baseline.json"
with open(path, encoding="utf-8") as handle:
    data = json.load(handle)
data["context"]["chupascript_machine"] = f"{platform.machine()} {platform.platform()}"
with open(path, "w", encoding="utf-8") as handle:
    json.dump(data, handle, indent=2, ensure_ascii=False)
PY
```

- [ ] **Step 5: Проверить, что сравнение с базой проходит**

```bash
./build-rel/benchmarks/chupascript_benchmarks \
    --benchmark_out=/tmp/bench-check.json --benchmark_out_format=json
python3 tools/bench-compare.py benchmarks/baseline.json /tmp/bench-check.json
echo "exit=$?"
```

Expected: `exit=0`. Если база снималась с агрегатами, а проверка без них, имена не совпадут и все бенчмарки будут показаны как «новые» — в этом случае снять базу без `--benchmark_repetitions`.

- [ ] **Step 6: Коммит**

```bash
git add benchmarks/baseline.json
git commit -m "Record lexer performance baseline"
```

---

## Что этот слой не делает

Перечислено, чтобы отсутствие не принималось за упущение.

- **Раскодирование escape-последовательностей.** Лексер только помечает литерал флагом `hasEscape`. Раскодирование требует памяти под результат и произойдёт в слое значений (план 3), где появится арена. Литерал без флага — участок исходника побайтово, что и даёт zero-copy из `docs/grammar.md` §7.
- **Проверка длины исходника.** Смещения 32-битные; отказ от исходников длиннее 4 ГиБ ложится на вызывающий слой в плане 2.
- **Любой синтаксис.** `3.` в конце текста, `a < b < c`, висячая запятая — всё это ошибки парсера, а не лексера.
- **Публичный C-заголовок.** Лексер внутренний; `core/include/chupascript/chupascript.h` в этом плане не меняется.
