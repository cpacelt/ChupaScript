#include "builtin.hpp"

#include <gtest/gtest.h>

namespace {

using CS::Builtin;

TEST(BuiltinTable, FindsEveryName) {
    // Двенадцать функций docs/semantics.md §8. typeof и type зарезервированы
    // грамматикой и функциями не являются (B32, docs/backlog.md).
    const std::pair<const char *, Builtin> all[] = {
        {"count", Builtin::Count},   {"keys", Builtin::Keys},
        {"has", Builtin::Has},       {"last", Builtin::Last},
        {"push", Builtin::Push},     {"pop", Builtin::Pop},
        {"str", Builtin::Str},
        {"format", Builtin::Format}, {"min", Builtin::Min},
        {"max", Builtin::Max},       {"abs", Builtin::Abs},
        {"round", Builtin::Round},
    };
    for (const auto &pair : all) {
        Builtin id = Builtin::Count;
        EXPECT_TRUE(CS::findBuiltin(pair.first, &id)) << pair.first;
        EXPECT_EQ(id, pair.second) << pair.first;
    }
}

TEST(BuiltinTable, RejectsUnknownNames) {
    Builtin id = Builtin::Count;
    EXPECT_FALSE(CS::findBuiltin("cnt", &id));
    EXPECT_FALSE(CS::findBuiltin("", &id));
    EXPECT_FALSE(CS::findBuiltin("Count", &id));  // регистр значим
    EXPECT_FALSE(CS::findBuiltin("counts", &id));
}

TEST(BuiltinTable, ArityMatchesTheSpecification) {
    // docs/semantics.md §8: один аргумент.
    for (Builtin id : {Builtin::Count, Builtin::Keys, Builtin::Last,
                       Builtin::Pop, Builtin::Str,
                       Builtin::Abs, Builtin::Round}) {
        EXPECT_EQ(CS::builtinInfo(id).minArgs, 1);
        EXPECT_EQ(CS::builtinInfo(id).maxArgs, 1);
    }
    // Два аргумента.
    for (Builtin id : {Builtin::Has, Builtin::Push, Builtin::Min,
                       Builtin::Max}) {
        EXPECT_EQ(CS::builtinInfo(id).minArgs, 2);
        EXPECT_EQ(CS::builtinInfo(id).maxArgs, 2);
    }
    // format — от одного шаблона и сколько угодно аргументов (§8.9).
    EXPECT_EQ(CS::builtinInfo(Builtin::Format).minArgs, 1);
    EXPECT_EQ(CS::builtinInfo(Builtin::Format).maxArgs, CS::kVariadic);
}

TEST(BuiltinTable, OnlyPushAndPopAreVoid) {
    // Команды отделены от запросов (§8): либо меняет данные и не возвращает
    // значения, либо возвращает и не меняет.
    EXPECT_FALSE(CS::builtinInfo(Builtin::Push).returnsValue);
    EXPECT_FALSE(CS::builtinInfo(Builtin::Pop).returnsValue);
    for (Builtin id : {Builtin::Count, Builtin::Keys, Builtin::Has,
                       Builtin::Last, Builtin::Str,
                       Builtin::Format, Builtin::Min, Builtin::Max,
                       Builtin::Abs, Builtin::Round}) {
        EXPECT_TRUE(CS::builtinInfo(id).returnsValue);
    }
}

TEST(BuiltinTable, IsSortedByName) {
    // Поиск двоичный, поэтому порядок таблицы — инвариант, а не оформление.
    std::string_view previous;
    // Str — последний по алфавиту, значит и последний в enum.
    for (int i = 0; i <= static_cast<int>(Builtin::Str); ++i) {
        const std::string_view name =
            CS::builtinInfo(static_cast<Builtin>(i)).name;
        EXPECT_LT(previous, name) << i;
        previous = name;
    }
}

TEST(BuiltinTable, TypeofIsReservedAndThereforeNotBuiltin) {
    // typeof зарезервирован грамматикой (docs/grammar.md §4.5): лексер отдаёт
    // на него Reserved, поэтому вызов с таким именем не разбирается вовсе.
    // Функции под этим именем нет и не будет — ограничение принято осознанно,
    // см. B32. Тест упадёт, если typeof вернут в таблицу, не заметив резерва.
    Builtin id = Builtin::Abs;
    EXPECT_FALSE(CS::findBuiltin("typeof", &id));
}

TEST(PlaceholderCount, CountsAndRespectsEscaping) {
    // docs/semantics.md §8.9: плейсхолдер ${}, последовательность $${} даёт
    // литеральное ${} и плейсхолдером не является.
    EXPECT_EQ(CS::countPlaceholders(""), 0u);
    EXPECT_EQ(CS::countPlaceholders("без подстановок"), 0u);
    EXPECT_EQ(CS::countPlaceholders("${}"), 1u);
    EXPECT_EQ(CS::countPlaceholders("Привет, ${}!"), 1u);
    EXPECT_EQ(CS::countPlaceholders("${} из ${}"), 2u);
    EXPECT_EQ(CS::countPlaceholders("цена $${}"), 0u);
    EXPECT_EQ(CS::countPlaceholders("$${} и ${}"), 1u);
    // Одинокие символы плейсхолдера не образуют: $ без {} и { без $.
    EXPECT_EQ(CS::countPlaceholders("$"), 0u);
    EXPECT_EQ(CS::countPlaceholders("${"), 0u);
    EXPECT_EQ(CS::countPlaceholders("{}"), 0u);
    EXPECT_EQ(CS::countPlaceholders("$$"), 0u);
    // $$${} — это литеральный $, за которым следует экранированное $${}:
    // разбор берёт четырёхсимвольное совпадение с позиции 1, а не с позиции 0
    // (там оно не совпадает — четвёртый символ там "{", а не "}"). Ноль
    // плейсхолдеров.
    EXPECT_EQ(CS::countPlaceholders("$$${}"), 0u);
}

}  // namespace
