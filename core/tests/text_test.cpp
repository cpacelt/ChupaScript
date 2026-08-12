#include "text.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <string>

namespace {

/// Представление числа в строку — обёртка, скрывающая буфер.
std::string format(double value) {
    char buffer[CS::kNumberBufferSize];
    return std::string(CS::formatNumber(value, buffer, sizeof buffer));
}

TEST(FormatNumber, ExamplesFromTheSpec) {
    // docs/semantics.md §4.3, таблица примеров целиком.
    EXPECT_EQ(format(1.0), "1");
    EXPECT_EQ(format(1.5), "1.5");
    EXPECT_EQ(format(1000000.0), "1000000");
    EXPECT_EQ(format(0.1 + 0.2), "0.30000000000000004");
    EXPECT_EQ(format(1e21), "1e+21");
}

TEST(FormatNumber, SpecialValues) {
    EXPECT_EQ(format(std::numeric_limits<double>::infinity()), "inf");
    EXPECT_EQ(format(-std::numeric_limits<double>::infinity()), "-inf");
    EXPECT_EQ(format(std::numeric_limits<double>::quiet_NaN()), "nan");
}

TEST(FormatNumber, ZeroKeepsItsSign) {
    // Ноль обрабатывается отдельно: общее правило дало бы 0e+00.
    EXPECT_EQ(format(0.0), "0");
    EXPECT_EQ(format(-0.0), "-0");
}

TEST(FormatNumber, UpperThreshold) {
    // Ниже 1e21 — фиксированная запись, начиная с 1e21 — научная.
    EXPECT_EQ(format(1e20), "100000000000000000000");
    EXPECT_EQ(format(1e21), "1e+21");
}

TEST(FormatNumber, LowerThreshold) {
    // 1e-7 ещё фиксированная, 1e-8 уже научная.
    EXPECT_EQ(format(1e-7), "0.0000001");
    EXPECT_EQ(format(1e-8), "1e-08");
}

TEST(FormatNumber, ExponentKeepsTwoDigits) {
    // За порогом отдаём ровно то, что даёт to_chars: расхождение с JavaScript,
    // который написал бы 1e-8, сознательное (спека §7.1).
    EXPECT_EQ(format(1e-8), "1e-08");
    EXPECT_EQ(format(1e300), "1e+300");
}

TEST(FormatNumber, NegativeValues) {
    EXPECT_EQ(format(-1.5), "-1.5");
    EXPECT_EQ(format(-1e21), "-1e+21");
}

TEST(FormatNumber, RoundTripsThroughTheShortestForm) {
    // Кратчайшее представление обязано читаться обратно в то же значение.
    const double values[] = {0.1, 1.0 / 3.0, 1e-5, 12345.6789, 2.2250738585072014e-308};
    for (double value : values) {
        EXPECT_EQ(std::stod(format(value)), value) << format(value);
    }
}

TEST(DecodeEscapes, WithoutEscapesTheTextIsUnchanged) {
    std::string scratch;
    EXPECT_EQ(CS::decodeEscapes("abc", scratch), "abc");
}

TEST(DecodeEscapes, WholeSetIsDecoded) {
    // docs/grammar.md Приложение A: \\ \' \" \n \t и больше ничего.
    std::string scratch;
    EXPECT_EQ(CS::decodeEscapes("a\\nb", scratch), "a\nb");
    EXPECT_EQ(CS::decodeEscapes("a\\tb", scratch), "a\tb");
    EXPECT_EQ(CS::decodeEscapes("a\\\\b", scratch), "a\\b");
    EXPECT_EQ(CS::decodeEscapes("a\\'b", scratch), "a'b");
    EXPECT_EQ(CS::decodeEscapes("a\\\"b", scratch), "a\"b");
}

TEST(DecodeEscapes, ScratchIsReusable) {
    // Один буфер обслуживает несколько вызовов подряд — так им пользуется
    // построение объекта, где ключи раскодируются по очереди.
    std::string scratch;
    EXPECT_EQ(CS::decodeEscapes("\\n", scratch), "\n");
    EXPECT_EQ(CS::decodeEscapes("\\t\\t", scratch), "\t\t");
    EXPECT_EQ(CS::decodeEscapes("", scratch), "");
}

}  // namespace
