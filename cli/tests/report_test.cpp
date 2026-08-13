#include "report.hpp"

#include <sstream>

#include <gtest/gtest.h>

#include "diagnostic.hpp"

namespace {

TEST(ColumnOf, CountsAsciiBytesAsColumns) {
    EXPECT_EQ(chupa::columnOf("abcdef", 0), 0u);
    EXPECT_EQ(chupa::columnOf("abcdef", 3), 3u);
    EXPECT_EQ(chupa::columnOf("abcdef", 6), 6u);
}

TEST(ColumnOf, CountsCharactersNotBytes) {
    // «имя» — шесть байт и три символа. Смещение шесть байт указывает на
    // точку, то есть на четвёртый символ.
    const std::string_view source = "имя.поле";
    EXPECT_EQ(chupa::columnOf(source, 6), 3u);
    // Смещение на «п» — семь байт от начала, четвёртый символ позади точки.
    EXPECT_EQ(chupa::columnOf(source, 7), 4u);
}

TEST(ColumnOf, CountsFourByteCharacters) {
    // Эмодзи занимает четыре байта и один символ.
    EXPECT_EQ(chupa::columnOf("😀x", 4), 1u);
    EXPECT_EQ(chupa::columnOf("😀x", 5), 2u);
}

TEST(ColumnOf, ClampsOffsetPastTheEnd) {
    // Диагностика вправе указывать на конец строки; за него — не вправе, но
    // оболочка не должна выходить за буфер, если это случится.
    EXPECT_EQ(chupa::columnOf("abc", 3), 3u);
    EXPECT_EQ(chupa::columnOf("abc", 99), 3u);
    EXPECT_EQ(chupa::columnOf("", 0), 0u);
    EXPECT_EQ(chupa::columnOf("", 5), 0u);
}

TEST(ColumnOf, OffsetInsideACharacterRoundsDown) {
    // Смещение внутрь многобайтового символа диагностика не порождает, но
    // округление вниз лучше выхода за границу.
    EXPECT_EQ(chupa::columnOf("имя", 1), 0u);
    EXPECT_EQ(chupa::columnOf("имя", 3), 1u);
}

TEST(CaretLine, PutsTheCaretUnderTheColumn) {
    EXPECT_EQ(chupa::caretLine(0, 0), "^");
    EXPECT_EQ(chupa::caretLine(3, 0), "   ^");
    // Отступ — ширина того, что оболочка напечатала до исходника: приглашение
    // «> » плюс префикс режима.
    EXPECT_EQ(chupa::caretLine(0, 8), "        ^");
    EXPECT_EQ(chupa::caretLine(2, 8), "          ^");
}

TEST(ReportDiagnostic, PrintsCaretThenMessage) {
    std::ostringstream out;
    const CS::Diagnostic diag{CS::ErrorCode::Name, 0, "unknown name"};
    chupa::reportDiagnostic(out, "usre.name", 8, diag);
    EXPECT_EQ(out.str(), "        ^\nerror: unknown name\n");
}

TEST(ReportDiagnostic, PlacesTheCaretByCharactersNotBytes) {
    std::ostringstream out;
    // В «'привет' + 1» оператор стоит на 15-м байте: кавычка, шесть кириллических
    // букв по два байта, кавычка, пробел. А символов до него девять.
    const CS::Diagnostic diag{CS::ErrorCode::Type, 15,
                              "arithmetic requires numbers"};
    chupa::reportDiagnostic(out, "'привет' + 1", 0, diag);
    EXPECT_EQ(out.str(), "         ^\nerror: arithmetic requires numbers\n");
}

}  // namespace
