#include "context.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using CS::Context;
using CS::Value;

TEST(ContextString, RoundTripsBytes) {
    Context ctx;
    const Value v = ctx.makeString("привет");
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_EQ(ctx.string(v), "привет");
}

TEST(ContextString, EmptyStringIsEmptyView) {
    Context ctx;
    const Value v = ctx.makeString("");
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_TRUE(ctx.string(v).empty());
}

TEST(ContextString, LengthIsCountedInBytes) {
    Context ctx;
    // Шесть кириллических букв — двенадцать байт (semantics.md §2.1).
    EXPECT_EQ(ctx.string(ctx.makeString("привет")).size(), 12u);
}

TEST(ContextString, KeepsEmbeddedNulByte) {
    Context ctx;
    const std::string bytes("a\0b", 3);
    const Value v = ctx.makeString(bytes);
    EXPECT_EQ(ctx.string(v).size(), 3u);
    EXPECT_EQ(ctx.string(v)[1], '\0');
}

TEST(ContextString, EqualStringsAreStoredTwice) {
    Context ctx;
    const Value a = ctx.makeString("одинаково");
    const Value b = ctx.makeString("одинаково");
    EXPECT_EQ(ctx.string(a), ctx.string(b));
    // Дедупликации нет: второй экземпляр занял место (спека §6).
    EXPECT_NE(ctx.string(a).data(), ctx.string(b).data());
}

TEST(ContextString, AcceptsSliceOfItsOwnTextPool) {
    Context ctx;
    // Копирование строки, которая уже лежит в пуле: источник может переехать
    // прямо во время копирования, и наивный insert здесь был бы UB.
    Value seed = ctx.makeString("исходная строка");
    for (int i = 0; i < 64; ++i) {
        seed = ctx.makeString(ctx.string(seed));
    }
    EXPECT_EQ(ctx.string(seed), "исходная строка");
}

TEST(ContextMetrics, FreshContextHoldsOnlyTheRootTable) {
    Context ctx;
    // Единственное, что есть у нового контекста, — пустой объект корней:
    // один заголовок и ни одной пары.
    EXPECT_EQ(ctx.rootCount(), 0u);
    EXPECT_GT(ctx.bytesUsed(), 0u);
    EXPECT_LT(ctx.bytesUsed(), 64u);
}

TEST(ContextMetrics, StringAddsItsBytes) {
    Context ctx;
    const std::size_t before = ctx.bytesUsed();
    ctx.makeString("12345");
    EXPECT_EQ(ctx.bytesUsed(), before + 5u);
}

TEST(ContextMetrics, ReservedCoversUsed) {
    Context ctx;
    const Value a = ctx.makeArray();
    for (int i = 0; i < 100; ++i) {
        ctx.arrayPush(a, Value::number(static_cast<double>(i)));
    }
    ctx.makeString("строка");
    EXPECT_GE(ctx.bytesReserved(), ctx.bytesUsed());
}

TEST(ContextArray, EmptyArrayHasNoElements) {
    Context ctx;
    const Value a = ctx.makeArray();
    EXPECT_EQ(a.kind(), Value::Kind::Array);
    EXPECT_EQ(ctx.arrayCount(a), 0u);
}

TEST(ContextArray, CapacityDoesNotCreateElements) {
    Context ctx;
    EXPECT_EQ(ctx.arrayCount(ctx.makeArray(16)), 0u);
}

TEST(ContextArray, ReadBeyondEndGivesNull) {
    Context ctx;
    const Value a = ctx.makeArray();
    // semantics.md §6.1: чтение за границей — штатная ситуация.
    EXPECT_EQ(ctx.arrayAt(a, 0).kind(), Value::Kind::Null);
    EXPECT_EQ(ctx.arrayAt(a, 1000).kind(), Value::Kind::Null);
}

TEST(ContextArray, WriteBeyondEndIsRefused) {
    Context ctx;
    const Value a = ctx.makeArray(8);
    // semantics.md §7.2: запись за границу — ошибка, ёмкость её не оправдывает.
    EXPECT_FALSE(ctx.arraySet(a, 0, Value::number(1.0)));
}

TEST(ContextArray, TwoEmptyArraysAreDistinct) {
    Context ctx;
    EXPECT_FALSE(ctx.makeArray().sameAggregate(ctx.makeArray()));
}

TEST(ContextArray, CopyOfValueIsTheSameArray) {
    Context ctx;
    const Value a = ctx.makeArray();
    const Value b = a;
    EXPECT_TRUE(a.sameAggregate(b));
}

TEST(ContextArrayMutation, PushAppendsInOrder) {
    Context ctx;
    const Value a = ctx.makeArray();
    ctx.arrayPush(a, Value::number(1.0));
    ctx.arrayPush(a, Value::number(2.0));
    ASSERT_EQ(ctx.arrayCount(a), 2u);
    EXPECT_EQ(ctx.arrayAt(a, 0).numberValue(), 1.0);
    EXPECT_EQ(ctx.arrayAt(a, 1).numberValue(), 2.0);
}

TEST(ContextArrayMutation, SetReplacesExistingElement) {
    Context ctx;
    const Value a = ctx.makeArray();
    ctx.arrayPush(a, Value::number(1.0));
    EXPECT_TRUE(ctx.arraySet(a, 0, Value::boolean(true)));
    EXPECT_TRUE(ctx.arrayAt(a, 0).booleanValue());
}

TEST(ContextArrayMutation, PopReturnsLastAndShrinks) {
    Context ctx;
    const Value a = ctx.makeArray();
    ctx.arrayPush(a, Value::number(1.0));
    ctx.arrayPush(a, Value::number(2.0));

    Value taken = Value::null();
    ASSERT_TRUE(ctx.arrayPop(a, &taken));
    EXPECT_EQ(taken.numberValue(), 2.0);
    EXPECT_EQ(ctx.arrayCount(a), 1u);
}

TEST(ContextArrayMutation, PopOnEmptyIsRefused) {
    Context ctx;
    Value taken = Value::number(7.0);
    EXPECT_FALSE(ctx.arrayPop(ctx.makeArray(), &taken));
    // Отказ не трогает выходной параметр.
    EXPECT_EQ(taken.numberValue(), 7.0);
}

TEST(ContextArrayMutation, AliasSurvivesGrowth) {
    Context ctx;
    const Value a = ctx.makeArray();
    const Value alias = a;

    // Рост через все удвоения: 4, 8, 16, 32 — данные переезжают четырежды.
    for (int i = 0; i < 40; ++i) {
        ctx.arrayPush(a, Value::number(static_cast<double>(i)));
    }

    // semantics.md §2.3: изменение через одно имя видно через второе.
    EXPECT_EQ(ctx.arrayCount(alias), 40u);
    EXPECT_EQ(ctx.arrayAt(alias, 0).numberValue(), 0.0);
    EXPECT_EQ(ctx.arrayAt(alias, 39).numberValue(), 39.0);
    EXPECT_TRUE(a.sameAggregate(alias));
}

TEST(ContextArrayMutation, WriteThroughAliasIsSeenByOriginal) {
    Context ctx;
    const Value a = ctx.makeArray();
    ctx.arrayPush(a, Value::number(1.0));
    const Value alias = a;

    ASSERT_TRUE(ctx.arraySet(alias, 0, Value::number(99.0)));
    EXPECT_EQ(ctx.arrayAt(a, 0).numberValue(), 99.0);
}

TEST(ContextArrayMutation, NestedArrayKeepsIdentity) {
    Context ctx;
    const Value outer = ctx.makeArray();
    const Value inner = ctx.makeArray();
    ctx.arrayPush(outer, inner);
    ctx.arrayPush(inner, Value::number(1.0));

    const Value fetched = ctx.arrayAt(outer, 0);
    EXPECT_TRUE(fetched.sameAggregate(inner));
    EXPECT_EQ(ctx.arrayCount(fetched), 1u);
}

TEST(ContextArrayMutation, ArrayMayContainItself) {
    Context ctx;
    const Value a = ctx.makeArray();
    ctx.arrayPush(a, a);
    // semantics.md §2.3: цикл допустим, рекурсивного обхода в слое нет.
    EXPECT_TRUE(ctx.arrayAt(a, 0).sameAggregate(a));
}

TEST(ContextArrayMutation, PreallocatedCapacityGrowsNothing) {
    Context ctx;
    const Value a = ctx.makeArray(64);
    const std::size_t afterReserve = ctx.bytesUsed();
    for (int i = 0; i < 64; ++i) {
        ctx.arrayPush(a, Value::number(static_cast<double>(i)));
    }
    // Размер известен заранее — переездов и мусора нет (спека §5).
    EXPECT_EQ(ctx.bytesUsed(), afterReserve);
}

TEST(ContextArrayMutation, GrowthLeavesGarbageBehind) {
    Context ctx;
    const Value a = ctx.makeArray();
    // Заголовок массива уже учтён; дальше растёт только пул элементов, поэтому
    // сравнивается прирост, а не абсолютное число: размер заголовка тесту
    // недоступен — тип неполон вне context.cpp.
    const std::size_t afterHeader = ctx.bytesUsed();
    for (int i = 0; i < 64; ++i) {
        ctx.arrayPush(a, Value::number(static_cast<double>(i)));
    }
    // 4 + 8 + 16 + 32 + 64 = 124 слота выделено под 64 элемента: разница —
    // брошенный мусор, освобождения по одному нет (docs/backlog.md B1).
    EXPECT_EQ(ctx.bytesUsed() - afterHeader, 124u * sizeof(Value));
}

TEST(ContextArrayMutation, RequestedCapacityIsAllocatedExactly) {
    Context ctx;
    const Value a = ctx.makeArray(100);
    const std::size_t afterReserve = ctx.bytesUsed();
    for (int i = 0; i < 100; ++i) {
        ctx.arrayPush(a, Value::number(static_cast<double>(i)));
    }
    // Сто элементов занимают сто слотов, а не ближайшую степень двойки.
    EXPECT_EQ(ctx.bytesUsed(), afterReserve);
}

TEST(ContextObject, EmptyObjectHasNoKeys) {
    Context ctx;
    const Value o = ctx.makeObject();
    EXPECT_EQ(o.kind(), Value::Kind::Object);
    EXPECT_EQ(ctx.objectCount(o), 0u);
}

TEST(ContextObject, MissingKeyReadsAsNull) {
    Context ctx;
    const Value o = ctx.makeObject();
    // semantics.md §6.2: отсутствующий ключ читается как null.
    EXPECT_EQ(ctx.objectGet(o, "нет").kind(), Value::Kind::Null);
    EXPECT_FALSE(ctx.objectHas(o, "нет"));
}

TEST(ContextObject, StoredValueIsFound) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "count", Value::number(3.0));
    EXPECT_TRUE(ctx.objectHas(o, "count"));
    EXPECT_EQ(ctx.objectGet(o, "count").numberValue(), 3.0);
    EXPECT_EQ(ctx.objectCount(o), 1u);
}

TEST(ContextObject, NullValueIsDistinctFromAbsence) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "key", Value::null());
    // semantics.md §6.2: отличить одно от другого можно только через has.
    EXPECT_EQ(ctx.objectGet(o, "key").kind(), Value::Kind::Null);
    EXPECT_TRUE(ctx.objectHas(o, "key"));
}

TEST(ContextObject, FindsKeyAmongMany) {
    Context ctx;
    const Value o = ctx.makeObject();
    const char *keys[] = {"zeta", "alpha", "mu", "beta", "omega", "kappa", "iota"};
    for (int i = 0; i < 7; ++i) {
        ctx.objectSet(o, keys[i], Value::number(static_cast<double>(i)));
    }
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(ctx.objectGet(o, keys[i]).numberValue(), static_cast<double>(i));
    }
    EXPECT_EQ(ctx.objectCount(o), 7u);
}

TEST(ContextObject, PrefixKeyIsNotConfusedWithLongerOne) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "item", Value::number(1.0));
    ctx.objectSet(o, "items", Value::number(2.0));
    EXPECT_EQ(ctx.objectGet(o, "item").numberValue(), 1.0);
    EXPECT_EQ(ctx.objectGet(o, "items").numberValue(), 2.0);
}

TEST(ContextObject, NonAsciiKeyIsFound) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "имя", ctx.makeString("Вася"));
    EXPECT_EQ(ctx.string(ctx.objectGet(o, "имя")), "Вася");
}

TEST(ContextObject, EmptyKeyIsAKeyLikeAnyOther) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "", Value::number(1.0));
    EXPECT_TRUE(ctx.objectHas(o, ""));
    EXPECT_EQ(ctx.objectGet(o, "").numberValue(), 1.0);
}

TEST(ContextObject, EmptyKeyIsDistinguishableFromAbsentOne) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "", Value::number(1.0));
    ctx.objectSet(o, "другой", Value::number(2.0));

    // Пустой ключ существует: срез пустой, но не нулевой.
    ASSERT_EQ(ctx.objectCount(o), 2u);
    EXPECT_TRUE(ctx.objectKeyAt(o, 0).empty());
    EXPECT_NE(ctx.objectKeyAt(o, 0).data(), nullptr);
    // За границей — нулевой срез.
    EXPECT_EQ(ctx.objectKeyAt(o, 99).data(), nullptr);
}

TEST(ContextObject, EnumerationYieldsEveryKey) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "b", Value::number(2.0));
    ctx.objectSet(o, "a", Value::number(1.0));

    ASSERT_EQ(ctx.objectCount(o), 2u);
    std::string seen;
    for (std::uint32_t i = 0; i < ctx.objectCount(o); ++i) {
        seen += ctx.objectKeyAt(o, i);
        seen += '=';
        seen += std::to_string(static_cast<int>(ctx.objectValueAt(o, i).numberValue()));
        seen += ';';
    }
    // Порядок наружу не обещан (semantics.md §2.1), но хранение отсортировано.
    EXPECT_EQ(seen, "a=1;b=2;");
}

TEST(ContextObject, EnumerationBeyondEndIsEmpty) {
    Context ctx;
    const Value o = ctx.makeObject();
    EXPECT_TRUE(ctx.objectKeyAt(o, 0).empty());
    EXPECT_EQ(ctx.objectValueAt(o, 0).kind(), Value::Kind::Null);
}

TEST(ContextObject, TwoEmptyObjectsAreDistinct) {
    Context ctx;
    EXPECT_FALSE(ctx.makeObject().sameAggregate(ctx.makeObject()));
}

TEST(ContextObjectMutation, RepeatedKeyReplacesValue) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "k", Value::number(1.0));
    ctx.objectSet(o, "k", Value::number(2.0));
    EXPECT_EQ(ctx.objectCount(o), 1u);
    EXPECT_EQ(ctx.objectGet(o, "k").numberValue(), 2.0);
}

TEST(ContextObjectMutation, ReplacementCopiesNoKeyBytes) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "k", Value::number(1.0));
    const std::size_t before = ctx.bytesUsed();
    ctx.objectSet(o, "k", Value::number(2.0));
    EXPECT_EQ(ctx.bytesUsed(), before);
}

TEST(ContextObjectMutation, InsertionKeepsSortedOrder) {
    Context ctx;
    const Value o = ctx.makeObject();
    const char *keys[] = {"delta", "alpha", "charlie", "bravo", "echo"};
    for (const char *key : keys) { ctx.objectSet(o, key, Value::null()); }

    std::string seen;
    for (std::uint32_t i = 0; i < ctx.objectCount(o); ++i) {
        seen += ctx.objectKeyAt(o, i);
        seen += ' ';
    }
    EXPECT_EQ(seen, "alpha bravo charlie delta echo ");
}

TEST(ContextObjectMutation, EveryKeySurvivesGrowth) {
    Context ctx;
    const Value o = ctx.makeObject();
    // Тридцать ключей — рост через 4, 8, 16, 32: пары переезжают четырежды.
    for (int i = 0; i < 30; ++i) {
        ctx.objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)));
    }
    ASSERT_EQ(ctx.objectCount(o), 30u);
    for (int i = 0; i < 30; ++i) {
        EXPECT_EQ(ctx.objectGet(o, "key" + std::to_string(i)).numberValue(),
                  static_cast<double>(i));
    }
}

TEST(ContextObjectMutation, AliasSeesNewKey) {
    Context ctx;
    const Value o = ctx.makeObject();
    const Value alias = o;
    for (int i = 0; i < 30; ++i) {
        ctx.objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)));
    }
    // semantics.md §2.3: изменение через одно имя видно через второе.
    EXPECT_EQ(ctx.objectCount(alias), 30u);
    EXPECT_EQ(ctx.objectGet(alias, "key29").numberValue(), 29.0);
    EXPECT_TRUE(o.sameAggregate(alias));
}

TEST(ContextObjectMutation, KeyTakenFromTheSameContextWorks) {
    Context ctx;
    const Value o = ctx.makeObject();
    const Value keyValue = ctx.makeString("динамический");
    // Ключ — срез собственного пула текста: приём, которым пользуется obj[k].
    ctx.objectSet(o, ctx.string(keyValue), Value::number(5.0));
    EXPECT_EQ(ctx.objectGet(o, "динамический").numberValue(), 5.0);
}

TEST(ContextObjectMutation, ObjectMayContainItself) {
    Context ctx;
    const Value o = ctx.makeObject();
    ctx.objectSet(o, "self", o);
    // semantics.md §2.3: obj['self'] = obj — корректная программа.
    EXPECT_TRUE(ctx.objectGet(o, "self").sameAggregate(o));
    EXPECT_EQ(ctx.objectCount(o), 1u);
}

TEST(ContextObjectMutation, ObjectHoldsArrayAndArrayHoldsObject) {
    Context ctx;
    const Value o = ctx.makeObject();
    const Value a = ctx.makeArray();
    ctx.objectSet(o, "items", a);
    ctx.arrayPush(a, o);

    EXPECT_TRUE(ctx.objectGet(o, "items").sameAggregate(a));
    EXPECT_TRUE(ctx.arrayAt(a, 0).sameAggregate(o));
}

TEST(ContextObjectMutation, PushIntoStoredArrayIsSeenThroughTheObject) {
    Context ctx;
    const Value o = ctx.makeObject();
    const Value a = ctx.makeArray();
    ctx.objectSet(o, "items", a);

    for (int i = 0; i < 20; ++i) {
        ctx.arrayPush(ctx.objectGet(o, "items"), Value::number(static_cast<double>(i)));
    }
    EXPECT_EQ(ctx.arrayCount(a), 20u);
}

TEST(ContextObjectMutation, PreallocatedCapacityGrowsNothing) {
    Context ctx;
    const Value o = ctx.makeObject(8);
    const std::size_t afterReserve = ctx.bytesUsed();
    for (int i = 0; i < 8; ++i) {
        ctx.objectSet(o, "k" + std::to_string(i), Value::null());
    }
    // Пары не переезжали; выросло ровно на байты восьми двухсимвольных ключей.
    EXPECT_EQ(ctx.bytesUsed(), afterReserve + 16u);
}

TEST(ContextRoots, FreshContextHasNoRoots) {
    Context ctx;
    EXPECT_EQ(ctx.rootCount(), 0u);
}

TEST(ContextRoots, MissingRootReadsAsNull) {
    Context ctx;
    EXPECT_EQ(ctx.root("state").kind(), Value::Kind::Null);
    EXPECT_FALSE(ctx.hasRoot("state"));
}

TEST(ContextRoots, StoredRootIsFound) {
    Context ctx;
    ctx.setRoot("count", Value::number(3.0));
    EXPECT_TRUE(ctx.hasRoot("count"));
    EXPECT_EQ(ctx.root("count").numberValue(), 3.0);
    EXPECT_EQ(ctx.rootCount(), 1u);
}

TEST(ContextRoots, NullRootIsDistinctFromAbsence) {
    Context ctx;
    ctx.setRoot("maybe", Value::null());
    // Тот же довод, что для ключей объекта (docs/semantics.md §6.2).
    EXPECT_EQ(ctx.root("maybe").kind(), Value::Kind::Null);
    EXPECT_TRUE(ctx.hasRoot("maybe"));
}

TEST(ContextRoots, RepeatedSetReplacesValueWithoutAddingName) {
    Context ctx;
    ctx.setRoot("state", Value::number(1.0));
    ctx.setRoot("state", Value::number(2.0));
    EXPECT_EQ(ctx.rootCount(), 1u);
    EXPECT_EQ(ctx.root("state").numberValue(), 2.0);
}

TEST(ContextRoots, RootHoldsAggregate) {
    Context ctx;
    const Value items = ctx.makeArray();
    ctx.arrayPush(items, Value::number(1.0));
    ctx.setRoot("items", items);

    EXPECT_TRUE(ctx.root("items").sameAggregate(items));
    EXPECT_EQ(ctx.arrayCount(ctx.root("items")), 1u);
}

TEST(ContextRoots, MutationThroughRootIsSeenThroughTheOriginal) {
    Context ctx;
    const Value items = ctx.makeArray();
    ctx.setRoot("items", items);
    for (int i = 0; i < 30; ++i) {
        ctx.arrayPush(ctx.root("items"), Value::number(static_cast<double>(i)));
    }
    // docs/semantics.md §2.3: ссылочность наблюдаема и через корень.
    EXPECT_EQ(ctx.arrayCount(items), 30u);
}

TEST(ContextRoots, EnumerationYieldsEveryName) {
    Context ctx;
    ctx.setRoot("user", Value::null());
    ctx.setRoot("state", Value::null());

    ASSERT_EQ(ctx.rootCount(), 2u);
    std::string seen;
    for (std::uint32_t i = 0; i < ctx.rootCount(); ++i) {
        seen += ctx.rootNameAt(i);
        seen += ' ';
    }
    // Хранение отсортировано, как у любого объекта.
    EXPECT_EQ(seen, "state user ");
}

TEST(ContextStringBuilder, AssemblesFromParts) {
    Context ctx;
    const std::uint32_t mark = ctx.beginString();
    ctx.appendToString("Привет");
    ctx.appendToString(", ");
    ctx.appendToString("мир");
    const Value built = ctx.endString(mark);
    EXPECT_EQ(ctx.string(built), "Привет, мир");
}

TEST(ContextStringBuilder, EmptyBuildGivesEmptyString) {
    Context ctx;
    const std::uint32_t mark = ctx.beginString();
    const Value built = ctx.endString(mark);
    EXPECT_EQ(ctx.string(built), "");
}

TEST(ContextStringBuilder, AbortLeavesNothingBehind) {
    Context ctx;
    const Value before = ctx.makeString("уже в пуле");
    const std::uint32_t used = ctx.bytesUsed();

    const std::uint32_t mark = ctx.beginString();
    ctx.appendToString("это будет выброшено");
    ctx.abortString(mark);

    // Пул усечён к метке, а прежняя строка цела.
    EXPECT_EQ(ctx.bytesUsed(), used);
    EXPECT_EQ(ctx.string(before), "уже в пуле");
}

TEST(ContextStringBuilder, SurvivesPoolGrowth) {
    Context ctx;
    // Кусков заведомо больше, чем влезет без переезда пула: сборка обязана
    // держаться на смещениях, а не на указателях.
    const std::uint32_t mark = ctx.beginString();
    std::string expected;
    for (int i = 0; i < 500; ++i) {
        ctx.appendToString("кусок");
        expected += "кусок";
    }
    EXPECT_EQ(ctx.string(ctx.endString(mark)), expected);
}

}  // namespace
