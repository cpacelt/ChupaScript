#include "data.hpp"

#include <cmath>
#include <gtest/gtest.h>
#include <string>

#include "box.hpp"
#include "diagnostic.hpp"
#include "store.hpp"
#include "aggregate.hpp"

namespace {

using CS::Store;
using CS::Diagnostic;
using CS::ErrorCode;
using CS::Value;

/// Кладёт значение и требует успеха; возвращает то, что легло.
Value put(Store &store, std::string_view name, std::string_view text) {
    CS::Deferred dead;
    Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(store, dead, name, text, diag)) << diag.message;
    return store.global(name);
}

TEST(DataScalars, NumberIsStored) {
    Store store;
    EXPECT_EQ(put(store, "count", "3").numberValue(), 3.0);
    EXPECT_EQ(put(store, "ratio", "0.5").numberValue(), 0.5);
}

TEST(DataScalars, BooleanIsStored) {
    Store store;
    EXPECT_TRUE(put(store, "on", "true").booleanValue());
    EXPECT_FALSE(put(store, "off", "false").booleanValue());
}

TEST(DataScalars, NullIsStored) {
    Store store;
    EXPECT_EQ(put(store, "nothing", "null").kind(), Value::Kind::Null);
    EXPECT_TRUE(store.hasGlobal("nothing"));
}

TEST(DataScalars, VeryLongIntegerBecomesInfinity) {
    Store store;
    // Четыреста цифр — не ошибка данных, а следствие поведения лексера:
    // величина переполняет double и округляется до бесконечности. Это
    // осознанное свойство модели значений (double без отдельной проверки
    // диапазона), а не недосмотр разбора недоверенных данных, — и оно теперь
    // достижимо напрямую с бэкенда.
    const std::string text(400, '9');
    const double value = put(store, "huge", text).numberValue();
    EXPECT_TRUE(std::isinf(value));
}

TEST(DataNames, IdentifierIsAccepted) {
    CS::Deferred dead;
    Store store;
    Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(store, dead, "user_2", "1", diag));
    EXPECT_TRUE(CS::setVariable(store, dead, "_private", "1", diag));
}

TEST(DataNames, NonIdentifierIsRejected) {
    CS::Deferred dead;
    Store store;
    Diagnostic diag;
    // Корень, который программа не может написать, бесполезен.
    EXPECT_FALSE(CS::setVariable(store, dead, "content-type", "1", diag));
    EXPECT_EQ(diag.code, ErrorCode::Name);
    EXPECT_FALSE(CS::setVariable(store, dead, "2fa", "1", diag));
    EXPECT_FALSE(CS::setVariable(store, dead, "", "1", diag));
    EXPECT_FALSE(CS::setVariable(store, dead, " state", "1", diag));
    EXPECT_FALSE(CS::setVariable(store, dead, "state ", "1", diag));
    EXPECT_FALSE(CS::setVariable(store, dead, "имя", "1", diag));
    EXPECT_EQ(store.globalCount(), 0u);
}

TEST(DataNames, ReservedWordIsRejected) {
    CS::Deferred dead;
    Store store;
    Diagnostic diag;
    // docs/grammar.md §4.5: ключевое слово идентификатором не является.
    EXPECT_FALSE(CS::setVariable(store, dead, "null", "1", diag));
    EXPECT_FALSE(CS::setVariable(store, dead, "true", "1", diag));
    EXPECT_FALSE(CS::setVariable(store, dead, "while", "1", diag));
    EXPECT_EQ(store.globalCount(), 0u);
}

TEST(DataFailure, SyntaxErrorLeavesNoGlobal) {
    CS::Deferred dead;
    Store store;
    Diagnostic diag;
    EXPECT_FALSE(CS::setVariable(store, dead, "broken", "3 3", diag));
    EXPECT_EQ(diag.code, ErrorCode::Syntax);
    EXPECT_FALSE(store.hasGlobal("broken"));
    EXPECT_EQ(store.globalCount(), 0u);
}

TEST(DataStrings, BothQuoteFormsAreAccepted) {
    Store store;
    // docs/grammar.md §A: обе формы равноправны и дают одинаковые значения.
    const Value a = put(store, "a", "'Вася'");
    EXPECT_EQ(CS::stringBytes(a), "Вася");
    const Value b = put(store, "b", "\"Вася\"");
    EXPECT_EQ(CS::stringBytes(b), "Вася");
}

TEST(DataStrings, EmptyStringIsAccepted) {
    Store store;
    const Value v = put(store, "empty", "''");
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_TRUE(CS::stringBytes(v).empty());
}

TEST(DataStrings, BytesArriveAlreadyUnescapedByTheHost) {
    Store store;
    // Внешний JSON снимает хост, до нас доезжают настоящие байты UTF-8.
    // Шесть кириллических букв — двенадцать байт.
    const Value v = put(store, "greet", "'привет'");
    EXPECT_EQ(CS::stringBytes(v).size(), 12u);
}

TEST(DataMinus, NegativeNumberIsAccepted) {
    Store store;
    // Знака в NumericLiteral нет, -3 приезжает узлом Unary над Number.
    EXPECT_EQ(put(store, "below", "-3").numberValue(), -3.0);
    EXPECT_EQ(put(store, "half", "-0.5").numberValue(), -0.5);
}

TEST(DataMinus, NegativeZeroKeepsItsSign) {
    Store store;
    // docs/semantics.md §2.1 включает отрицательный ноль в модель значений.
    EXPECT_TRUE(std::signbit(put(store, "zero", "-0").numberValue()));
}

TEST(DataMinus, MinusOverNonNumberIsRejected) {
    CS::Deferred dead;
    Store store;
    Diagnostic diag;
    EXPECT_FALSE(CS::setVariable(store, dead, "bad", "-'abc'", diag));
    EXPECT_EQ(diag.code, ErrorCode::Data);
    EXPECT_EQ(store.globalCount(), 0u);
}

TEST(DataMinus, BangIsRejected) {
    CS::Deferred dead;
    Store store;
    Diagnostic diag;
    EXPECT_FALSE(CS::setVariable(store, dead, "worse", "!true", diag));
    EXPECT_EQ(diag.code, ErrorCode::Data);
    EXPECT_EQ(store.globalCount(), 0u);
}

TEST(DataMinus, DoubleMinusIsRejected) {
    CS::Deferred dead;
    Store store;
    Diagnostic diag;
    // "--3" — минус над узлом Unary, а не над Number: материализация
    // принимает минус только непосредственно над числом (§5), поэтому
    // второй минус упирается в общее правило "выражение — не данные", а не
    // в частный случай "минус не над числом". Поведение разумное, но нигде
    // не было зафиксировано тестом.
    EXPECT_FALSE(CS::setVariable(store, dead, "v", "--3", diag));
    EXPECT_EQ(diag.code, ErrorCode::Data);
    EXPECT_EQ(store.globalCount(), 0u);
}

TEST(DataEscapes, NewlineIsDecoded) {
    Store store;
    const Value v = put(store, "s", "'a\\nb'");
    const std::string_view text = CS::stringBytes(v);
    ASSERT_EQ(text.size(), 3u);
    EXPECT_EQ(text[1], '\n');
}

TEST(DataEscapes, TabIsDecoded) {
    Store store;
    const Value v = put(store, "s", "'a\\tb'");
    const std::string_view text = CS::stringBytes(v);
    ASSERT_EQ(text.size(), 3u);
    EXPECT_EQ(text[1], '\t');
}

TEST(DataEscapes, BackslashIsDecoded) {
    Store store;
    const Value v = put(store, "s", "'a\\\\b'");
    const std::string_view text = CS::stringBytes(v);
    ASSERT_EQ(text.size(), 3u);
    EXPECT_EQ(text[1], '\\');
}

TEST(DataEscapes, BothQuotesAreDecoded) {
    Store store;
    const Value a = put(store, "a", "'a\\'b'");
    EXPECT_EQ(CS::stringBytes(a), "a'b");
    const Value b = put(store, "b", "\"a\\\"b\"");
    EXPECT_EQ(CS::stringBytes(b), "a\"b");
}

TEST(DataEscapes, EscapeAtBothEndsIsDecoded) {
    Store store;
    const Value v = put(store, "s", "'\\n\\t'");
    EXPECT_EQ(CS::stringBytes(v), "\n\t");
}

TEST(DataEscapes, UnicodeEscapeIsRejectedByTheLexer) {
    CS::Deferred dead;
    Store store;
    Diagnostic diag;
    // docs/grammar.md §8 и §4.7: юникодных escape в языке нет — внешний
    // уровень снимает хост, и до нас доезжают готовые байты.
    EXPECT_FALSE(CS::setVariable(store, dead, "s", "'\\u0041'", diag));
    EXPECT_EQ(diag.code, ErrorCode::Syntax);
}

TEST(DataAggregates, ArrayKeepsOrder) {
    Store store;
    const Value a = put(store, "items", "[1, 2, 3]");
    ASSERT_EQ(CS::arrayCount(a), 3u);
    EXPECT_EQ(CS::arrayAt(a, 0).numberValue(), 1.0);
    EXPECT_EQ(CS::arrayAt(a, 2).numberValue(), 3.0);
}

TEST(DataAggregates, EmptyArrayAndObject) {
    Store store;
    EXPECT_EQ(CS::arrayCount(put(store, "a", "[]")), 0u);
    EXPECT_EQ(CS::objectCount(put(store, "o", "{}")), 0u);
}

TEST(DataAggregates, ObjectStoresKeys) {
    Store store;
    const Value o = put(store, "user", "{\"name\": \"Вася\", \"age\": 30}");
    ASSERT_EQ(CS::objectCount(o), 2u);
    const Value name = CS::objectGet(o, "name");
    EXPECT_EQ(CS::stringBytes(name), "Вася");
    EXPECT_EQ(CS::objectGet(o, "age").numberValue(), 30.0);
}

TEST(DataAggregates, KeyWithEscapeIsDecoded) {
    Store store;
    const Value o = put(store, "o", "{'a\\nb': 1}");
    EXPECT_TRUE(CS::objectHas(o, "a\nb"));
}

TEST(DataAggregates, LastDuplicateKeyWins) {
    Store store;
    // Бэкенд отсутствия дубликатов не гарантирует; поведение определено.
    const Value o = put(store, "o", "{'k': 1, 'k': 2}");
    EXPECT_EQ(CS::objectCount(o), 1u);
    EXPECT_EQ(CS::objectGet(o, "k").numberValue(), 2.0);
}

TEST(DataAggregates, NestingWorks) {
    Store store;
    const Value o = put(store, "state", "{'items': [{'id': 1}, {'id': 2}]}");
    const Value items = CS::objectGet(o, "items");
    ASSERT_EQ(CS::arrayCount(items), 2u);
    EXPECT_EQ(CS::objectGet(CS::arrayAt(items, 1), "id").numberValue(), 2.0);
}

TEST(DataAggregates, NegativeNumbersInsideAggregates) {
    Store store;
    const Value a = put(store, "a", "[-1, -2.5]");
    EXPECT_EQ(CS::arrayAt(a, 0).numberValue(), -1.0);
    EXPECT_EQ(CS::arrayAt(a, 1).numberValue(), -2.5);
}

TEST(DataAggregates, ExactCapacityLeavesNoGarbage) {
    Store store;
    std::string text = "[";
    for (int i = 0; i < 100; ++i) {
        if (i > 0) { text += ", "; }
        text += std::to_string(i);
    }
    text += "]";

    const std::size_t before = store.bytesUsed();
    const Value a = put(store, "hundred", text);
    ASSERT_EQ(CS::arrayCount(a), 100u);
    // Прирост — сто слотов плюс заголовок массива, пара глобальной переменной и байты его
    // имени; запас до ста десяти слотов это покрывает. При удвоении вместо
    // точного размера ушло бы сто двадцать восемь слотов, и порог не прошёл бы:
    // размер известен заранее, поэтому переездов при построении нет.
    EXPECT_LT(store.bytesUsed() - before, 110u * sizeof(Value));
}

TEST(DataAggregates, NestingLimit) {
    CS::Deferred dead;
    Store store;
    // docs/superpowers/specs/2026-08-11-chupascript-data-design.md §4:
    // предел вложенности агрегатов — тот же, что у парсера, 169 уровней.
    // Текст строится программно, а не руками, чтобы граница была видна.
    std::string nested169(169, '[');
    nested169 += "1";
    nested169 += std::string(169, ']');
    Diagnostic diag;
    EXPECT_TRUE(CS::setVariable(store, dead, "ok", nested169, diag)) << diag.message;

    std::string nested170(170, '[');
    nested170 += "1";
    nested170 += std::string(170, ']');
    EXPECT_FALSE(CS::setVariable(store, dead, "bad", nested170, diag));
    EXPECT_EQ(diag.code, ErrorCode::Syntax);
}

TEST(DataAggregates, ExpressionInsideAggregateIsRejected) {
    CS::Deferred dead;
    Store store;
    Diagnostic diag;
    EXPECT_FALSE(CS::setVariable(store, dead, "a", "[1, count(x), 3]", diag));
    EXPECT_EQ(diag.code, ErrorCode::Data);
    EXPECT_FALSE(store.hasGlobal("a"));
}

/// Требует отказа и возвращает диагностику.
Diagnostic reject(Store &store, std::string_view text) {
    CS::Deferred dead;
    Diagnostic diag;
    EXPECT_FALSE(CS::setVariable(store, dead, "v", text, diag));
    return diag;
}

TEST(DataRejects, IdentifierIsNotData) {
    Store store;
    EXPECT_EQ(reject(store, "user").code, ErrorCode::Data);
}

TEST(DataRejects, CallIsNotData) {
    Store store;
    EXPECT_EQ(reject(store, "count(items)").code, ErrorCode::Data);
}

TEST(DataRejects, BinaryIsNotData) {
    Store store;
    EXPECT_EQ(reject(store, "1 + 1").code, ErrorCode::Data);
}

TEST(DataRejects, MemberIsNotData) {
    Store store;
    EXPECT_EQ(reject(store, "user.name").code, ErrorCode::Data);
}

TEST(DataRejects, IndexIsNotData) {
    Store store;
    EXPECT_EQ(reject(store, "items[0]").code, ErrorCode::Data);
}

TEST(DataRejects, ConditionalIsNotData) {
    Store store;
    EXPECT_EQ(reject(store, "a ? 1 : 2").code, ErrorCode::Data);
}

TEST(DataRejects, OffsetPointsAtTheOffendingNode) {
    Store store;
    // Ошибка внутри массива обязана указывать на место выражения, а не на
    // начало текста и не на соседний элемент: иначе хост не покажет, где
    // именно чинить. В "[1, user.name, 3]" выражение занимает байты с 4 по 12,
    // а последний элемент стоит на 15 — верхняя граница отсекает его.
    const Diagnostic diag = reject(store, "[1, user.name, 3]");
    EXPECT_EQ(diag.code, ErrorCode::Data);
    EXPECT_GE(diag.offset, 4u);
    EXPECT_LT(diag.offset, 13u);
}

TEST(DataRejects, OffsetPointsAtCallInsideObject) {
    Store store;
    // То же самое, но для другого вида узла (Call, а не Member) и другого
    // агрегата (Object, а не Array): смещение обязано попадать в границы
    // "count(x)", а не указывать на начало текста. В "{'k': count(x)}"
    // вызов занимает байты с 6 по 13.
    const Diagnostic diag = reject(store, "{'k': count(x)}");
    EXPECT_EQ(diag.code, ErrorCode::Data);
    EXPECT_GE(diag.offset, 6u);
    EXPECT_LT(diag.offset, 14u);
}

TEST(DataRejects, EmptyTextIsRejected) {
    Store store;
    // docs/superpowers/specs/2026-08-11-chupascript-data-design.md §6:
    // пустой текст значения назван в таблице ошибок, но не проверялся.
    EXPECT_EQ(reject(store, "").code, ErrorCode::Syntax);
}

TEST(DataRejects, TrailingBytesAreRejected) {
    Store store;
    // Текст обязан быть значением целиком.
    EXPECT_EQ(reject(store, "1 2").code, ErrorCode::Syntax);
    EXPECT_EQ(reject(store, "[1] 2").code, ErrorCode::Syntax);
}

TEST(DataRejects, IndexingALiteralIsNotData) {
    Store store;
    // [1] [2] разбирается: это массив, проиндексированный двойкой. Выражение,
    // а не запись значения.
    EXPECT_EQ(reject(store, "[1] [2]").code, ErrorCode::Data);
}

TEST(DataRejects, ExponentIsNotANumber) {
    Store store;
    // docs/grammar.md §8 и §4.6: экспоненты в языке нет, 1e3 — два токена.
    // Единственное расхождение с JSON, переживающее границу.
    EXPECT_EQ(reject(store, "1e3").code, ErrorCode::Syntax);
}

TEST(DataRejects, FailedSetLeavesPreviousValueIntact) {
    CS::Deferred dead;
    Store store;
    Diagnostic diag;
    ASSERT_TRUE(CS::setVariable(store, dead, "v", "1", diag));
    EXPECT_FALSE(CS::setVariable(store, dead, "v", "user.name", diag));
    // Отказ не трогает того, что уже лежало.
    EXPECT_EQ(store.global("v").numberValue(), 1.0);
    EXPECT_EQ(store.globalCount(), 1u);
}

}  // namespace
