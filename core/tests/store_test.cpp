#include "store.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using CS::Store;
using CS::Value;

TEST(StoreString, RoundTripsBytes) {
    Store store;
    const Value v = store.makeString("привет");
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_EQ(store.string(v), "привет");
}

TEST(StoreString, EmptyStringIsEmptyView) {
    Store store;
    const Value v = store.makeString("");
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_TRUE(store.string(v).empty());
}

TEST(StoreString, LengthIsCountedInBytes) {
    Store store;
    // Шесть кириллических букв — двенадцать байт (semantics.md §2.1).
    EXPECT_EQ(store.string(store.makeString("привет")).size(), 12u);
}

TEST(StoreString, KeepsEmbeddedNulByte) {
    Store store;
    const std::string bytes("a\0b", 3);
    const Value v = store.makeString(bytes);
    EXPECT_EQ(store.string(v).size(), 3u);
    EXPECT_EQ(store.string(v)[1], '\0');
}

TEST(StoreString, EqualStringsAreStoredTwice) {
    Store store;
    const Value a = store.makeString("одинаково");
    const Value b = store.makeString("одинаково");
    EXPECT_EQ(store.string(a), store.string(b));
    // Дедупликации нет: второй экземпляр занял место (спека §6).
    EXPECT_NE(store.string(a).data(), store.string(b).data());
}

TEST(StoreString, AcceptsSliceOfItsOwnTextPool) {
    Store store;
    // Копирование строки, которая уже лежит в пуле: источник может переехать
    // прямо во время копирования, и наивный insert здесь был бы UB.
    Value seed = store.makeString("исходная строка");
    for (int i = 0; i < 64; ++i) {
        seed = store.makeString(store.string(seed));
    }
    EXPECT_EQ(store.string(seed), "исходная строка");
}

TEST(StoreMetrics, FreshStoreHoldsOnlyTheGlobalTable) {
    Store store;
    // Единственное, что есть у нового хранилища, — пустой объект глобальных переменных:
    // один заголовок и ни одной пары.
    EXPECT_EQ(store.globalCount(), 0u);
    EXPECT_GT(store.bytesUsed(), 0u);
    EXPECT_LT(store.bytesUsed(), 64u);
}

TEST(StoreMetrics, StringAddsItsBytes) {
    Store store;
    const std::size_t before = store.bytesUsed();
    store.makeString("12345");
    EXPECT_EQ(store.bytesUsed(), before + 5u);
}

TEST(StoreMetrics, ReservedCoversUsed) {
    Store store;
    const Value a = store.makeArray();
    for (int i = 0; i < 100; ++i) {
        store.arrayPush(a, Value::number(static_cast<double>(i)));
    }
    store.makeString("строка");
    EXPECT_GE(store.bytesReserved(), store.bytesUsed());
}

TEST(StoreArray, EmptyArrayHasNoElements) {
    Store store;
    const Value a = store.makeArray();
    EXPECT_EQ(a.kind(), Value::Kind::Array);
    EXPECT_EQ(store.arrayCount(a), 0u);
}

TEST(StoreArray, CapacityDoesNotCreateElements) {
    Store store;
    EXPECT_EQ(store.arrayCount(store.makeArray(16)), 0u);
}

TEST(StoreArray, ReadBeyondEndGivesNull) {
    Store store;
    const Value a = store.makeArray();
    // semantics.md §6.1: чтение за границей — штатная ситуация.
    EXPECT_EQ(store.arrayAt(a, 0).kind(), Value::Kind::Null);
    EXPECT_EQ(store.arrayAt(a, 1000).kind(), Value::Kind::Null);
}

TEST(StoreArray, WriteBeyondEndIsRefused) {
    Store store;
    const Value a = store.makeArray(8);
    // semantics.md §7.2: запись за границу — ошибка, ёмкость её не оправдывает.
    EXPECT_FALSE(store.arraySet(a, 0, Value::number(1.0)));
}

TEST(StoreArray, TwoEmptyArraysAreDistinct) {
    Store store;
    EXPECT_FALSE(store.makeArray().sameAggregate(store.makeArray()));
}

TEST(StoreArray, CopyOfValueIsTheSameArray) {
    Store store;
    const Value a = store.makeArray();
    const Value b = a;
    EXPECT_TRUE(a.sameAggregate(b));
}

TEST(StoreArrayMutation, PushAppendsInOrder) {
    Store store;
    const Value a = store.makeArray();
    store.arrayPush(a, Value::number(1.0));
    store.arrayPush(a, Value::number(2.0));
    ASSERT_EQ(store.arrayCount(a), 2u);
    EXPECT_EQ(store.arrayAt(a, 0).numberValue(), 1.0);
    EXPECT_EQ(store.arrayAt(a, 1).numberValue(), 2.0);
}

TEST(StoreArrayMutation, SetReplacesExistingElement) {
    Store store;
    const Value a = store.makeArray();
    store.arrayPush(a, Value::number(1.0));
    EXPECT_TRUE(store.arraySet(a, 0, Value::boolean(true)));
    EXPECT_TRUE(store.arrayAt(a, 0).booleanValue());
}

TEST(StoreArrayMutation, PopReturnsLastAndShrinks) {
    Store store;
    const Value a = store.makeArray();
    store.arrayPush(a, Value::number(1.0));
    store.arrayPush(a, Value::number(2.0));

    Value taken = Value::null();
    ASSERT_TRUE(store.arrayPop(a, &taken));
    EXPECT_EQ(taken.numberValue(), 2.0);
    EXPECT_EQ(store.arrayCount(a), 1u);
}

TEST(StoreArrayMutation, PopOnEmptyIsRefused) {
    Store store;
    Value taken = Value::number(7.0);
    EXPECT_FALSE(store.arrayPop(store.makeArray(), &taken));
    // Отказ не трогает выходной параметр.
    EXPECT_EQ(taken.numberValue(), 7.0);
}

TEST(StoreArrayMutation, AliasSurvivesGrowth) {
    Store store;
    const Value a = store.makeArray();
    const Value alias = a;

    // Рост через все удвоения: 4, 8, 16, 32 — данные переезжают четырежды.
    for (int i = 0; i < 40; ++i) {
        store.arrayPush(a, Value::number(static_cast<double>(i)));
    }

    // semantics.md §2.3: изменение через одно имя видно через второе.
    EXPECT_EQ(store.arrayCount(alias), 40u);
    EXPECT_EQ(store.arrayAt(alias, 0).numberValue(), 0.0);
    EXPECT_EQ(store.arrayAt(alias, 39).numberValue(), 39.0);
    EXPECT_TRUE(a.sameAggregate(alias));
}

TEST(StoreArrayMutation, WriteThroughAliasIsSeenByOriginal) {
    Store store;
    const Value a = store.makeArray();
    store.arrayPush(a, Value::number(1.0));
    const Value alias = a;

    ASSERT_TRUE(store.arraySet(alias, 0, Value::number(99.0)));
    EXPECT_EQ(store.arrayAt(a, 0).numberValue(), 99.0);
}

TEST(StoreArrayMutation, NestedArrayKeepsIdentity) {
    Store store;
    const Value outer = store.makeArray();
    const Value inner = store.makeArray();
    store.arrayPush(outer, inner);
    store.arrayPush(inner, Value::number(1.0));

    const Value fetched = store.arrayAt(outer, 0);
    EXPECT_TRUE(fetched.sameAggregate(inner));
    EXPECT_EQ(store.arrayCount(fetched), 1u);
}

TEST(StoreArrayMutation, ArrayMayContainItself) {
    Store store;
    const Value a = store.makeArray();
    store.arrayPush(a, a);
    // semantics.md §2.3: цикл допустим, рекурсивного обхода в слое нет.
    EXPECT_TRUE(store.arrayAt(a, 0).sameAggregate(a));
}

TEST(StoreArrayMutation, PreallocatedCapacityGrowsNothing) {
    Store store;
    const Value a = store.makeArray(64);
    const std::size_t afterReserve = store.bytesUsed();
    for (int i = 0; i < 64; ++i) {
        store.arrayPush(a, Value::number(static_cast<double>(i)));
    }
    // Размер известен заранее — переездов и мусора нет (спека §5).
    EXPECT_EQ(store.bytesUsed(), afterReserve);
}

TEST(StoreArrayMutation, GrowthLeavesGarbageBehind) {
    Store store;
    const Value a = store.makeArray();
    // Заголовок массива уже учтён; дальше растёт только пул элементов, поэтому
    // сравнивается прирост, а не абсолютное число: размер заголовка тесту
    // недоступен — тип неполон вне store.cpp.
    const std::size_t afterHeader = store.bytesUsed();
    for (int i = 0; i < 64; ++i) {
        store.arrayPush(a, Value::number(static_cast<double>(i)));
    }
    // 4 + 8 + 16 + 32 + 64 = 124 слота выделено под 64 элемента: разница —
    // брошенный мусор, освобождения по одному нет (docs/backlog.md B1).
    EXPECT_EQ(store.bytesUsed() - afterHeader, 124u * sizeof(Value));
}

TEST(StoreArrayMutation, RequestedCapacityIsAllocatedExactly) {
    Store store;
    const Value a = store.makeArray(100);
    const std::size_t afterReserve = store.bytesUsed();
    for (int i = 0; i < 100; ++i) {
        store.arrayPush(a, Value::number(static_cast<double>(i)));
    }
    // Сто элементов занимают сто слотов, а не ближайшую степень двойки.
    EXPECT_EQ(store.bytesUsed(), afterReserve);
}

TEST(StoreObject, EmptyObjectHasNoKeys) {
    Store store;
    const Value o = store.makeObject();
    EXPECT_EQ(o.kind(), Value::Kind::Object);
    EXPECT_EQ(store.objectCount(o), 0u);
}

TEST(StoreObject, MissingKeyReadsAsNull) {
    Store store;
    const Value o = store.makeObject();
    // semantics.md §6.2: отсутствующий ключ читается как null.
    EXPECT_EQ(store.objectGet(o, "нет").kind(), Value::Kind::Null);
    EXPECT_FALSE(store.objectHas(o, "нет"));
}

TEST(StoreObject, StoredValueIsFound) {
    Store store;
    const Value o = store.makeObject();
    store.objectSet(o, "count", Value::number(3.0));
    EXPECT_TRUE(store.objectHas(o, "count"));
    EXPECT_EQ(store.objectGet(o, "count").numberValue(), 3.0);
    EXPECT_EQ(store.objectCount(o), 1u);
}

TEST(StoreObject, NullValueIsDistinctFromAbsence) {
    Store store;
    const Value o = store.makeObject();
    store.objectSet(o, "key", Value::null());
    // semantics.md §6.2: отличить одно от другого можно только через has.
    EXPECT_EQ(store.objectGet(o, "key").kind(), Value::Kind::Null);
    EXPECT_TRUE(store.objectHas(o, "key"));
}

TEST(StoreObject, FindsKeyAmongMany) {
    Store store;
    const Value o = store.makeObject();
    const char *keys[] = {"zeta", "alpha", "mu", "beta", "omega", "kappa", "iota"};
    for (int i = 0; i < 7; ++i) {
        store.objectSet(o, keys[i], Value::number(static_cast<double>(i)));
    }
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(store.objectGet(o, keys[i]).numberValue(), static_cast<double>(i));
    }
    EXPECT_EQ(store.objectCount(o), 7u);
}

TEST(StoreObject, PrefixKeyIsNotConfusedWithLongerOne) {
    Store store;
    const Value o = store.makeObject();
    store.objectSet(o, "item", Value::number(1.0));
    store.objectSet(o, "items", Value::number(2.0));
    EXPECT_EQ(store.objectGet(o, "item").numberValue(), 1.0);
    EXPECT_EQ(store.objectGet(o, "items").numberValue(), 2.0);
}

TEST(StoreObject, NonAsciiKeyIsFound) {
    Store store;
    const Value o = store.makeObject();
    store.objectSet(o, "имя", store.makeString("Вася"));
    EXPECT_EQ(store.string(store.objectGet(o, "имя")), "Вася");
}

TEST(StoreObject, EmptyKeyIsAKeyLikeAnyOther) {
    Store store;
    const Value o = store.makeObject();
    store.objectSet(o, "", Value::number(1.0));
    EXPECT_TRUE(store.objectHas(o, ""));
    EXPECT_EQ(store.objectGet(o, "").numberValue(), 1.0);
}

TEST(StoreObject, EmptyKeyIsDistinguishableFromAbsentOne) {
    Store store;
    const Value o = store.makeObject();
    store.objectSet(o, "", Value::number(1.0));
    store.objectSet(o, "другой", Value::number(2.0));

    // Пустой ключ существует: срез пустой, но не нулевой.
    ASSERT_EQ(store.objectCount(o), 2u);
    EXPECT_TRUE(store.objectKeyAt(o, 0).empty());
    EXPECT_NE(store.objectKeyAt(o, 0).data(), nullptr);
    // За границей — нулевой срез.
    EXPECT_EQ(store.objectKeyAt(o, 99).data(), nullptr);
}

TEST(StoreObject, EnumerationYieldsEveryKey) {
    Store store;
    const Value o = store.makeObject();
    store.objectSet(o, "b", Value::number(2.0));
    store.objectSet(o, "a", Value::number(1.0));

    ASSERT_EQ(store.objectCount(o), 2u);
    std::string seen;
    for (std::uint32_t i = 0; i < store.objectCount(o); ++i) {
        seen += store.objectKeyAt(o, i);
        seen += '=';
        seen += std::to_string(static_cast<int>(store.objectValueAt(o, i).numberValue()));
        seen += ';';
    }
    // Порядок наружу не обещан (semantics.md §2.1), но хранение отсортировано.
    EXPECT_EQ(seen, "a=1;b=2;");
}

TEST(StoreObject, EnumerationBeyondEndIsEmpty) {
    Store store;
    const Value o = store.makeObject();
    EXPECT_TRUE(store.objectKeyAt(o, 0).empty());
    EXPECT_EQ(store.objectValueAt(o, 0).kind(), Value::Kind::Null);
}

TEST(StoreObject, TwoEmptyObjectsAreDistinct) {
    Store store;
    EXPECT_FALSE(store.makeObject().sameAggregate(store.makeObject()));
}

TEST(StoreObjectMutation, RepeatedKeyReplacesValue) {
    Store store;
    const Value o = store.makeObject();
    store.objectSet(o, "k", Value::number(1.0));
    store.objectSet(o, "k", Value::number(2.0));
    EXPECT_EQ(store.objectCount(o), 1u);
    EXPECT_EQ(store.objectGet(o, "k").numberValue(), 2.0);
}

TEST(StoreObjectMutation, ReplacementCopiesNoKeyBytes) {
    Store store;
    const Value o = store.makeObject();
    store.objectSet(o, "k", Value::number(1.0));
    const std::size_t before = store.bytesUsed();
    store.objectSet(o, "k", Value::number(2.0));
    EXPECT_EQ(store.bytesUsed(), before);
}

TEST(StoreObjectMutation, InsertionKeepsSortedOrder) {
    Store store;
    const Value o = store.makeObject();
    const char *keys[] = {"delta", "alpha", "charlie", "bravo", "echo"};
    for (const char *key : keys) { store.objectSet(o, key, Value::null()); }

    std::string seen;
    for (std::uint32_t i = 0; i < store.objectCount(o); ++i) {
        seen += store.objectKeyAt(o, i);
        seen += ' ';
    }
    EXPECT_EQ(seen, "alpha bravo charlie delta echo ");
}

TEST(StoreObjectMutation, EveryKeySurvivesGrowth) {
    Store store;
    const Value o = store.makeObject();
    // Тридцать ключей — рост через 4, 8, 16, 32: пары переезжают четырежды.
    for (int i = 0; i < 30; ++i) {
        store.objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)));
    }
    ASSERT_EQ(store.objectCount(o), 30u);
    for (int i = 0; i < 30; ++i) {
        EXPECT_EQ(store.objectGet(o, "key" + std::to_string(i)).numberValue(),
                  static_cast<double>(i));
    }
}

TEST(StoreObjectMutation, AliasSeesNewKey) {
    Store store;
    const Value o = store.makeObject();
    const Value alias = o;
    for (int i = 0; i < 30; ++i) {
        store.objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)));
    }
    // semantics.md §2.3: изменение через одно имя видно через второе.
    EXPECT_EQ(store.objectCount(alias), 30u);
    EXPECT_EQ(store.objectGet(alias, "key29").numberValue(), 29.0);
    EXPECT_TRUE(o.sameAggregate(alias));
}

TEST(StoreObjectMutation, KeyTakenFromTheSameStoreWorks) {
    Store store;
    const Value o = store.makeObject();
    const Value keyValue = store.makeString("динамический");
    // Ключ — срез собственного пула текста: приём, которым пользуется obj[k].
    store.objectSet(o, store.string(keyValue), Value::number(5.0));
    EXPECT_EQ(store.objectGet(o, "динамический").numberValue(), 5.0);
}

TEST(StoreObjectMutation, ObjectMayContainItself) {
    Store store;
    const Value o = store.makeObject();
    store.objectSet(o, "self", o);
    // semantics.md §2.3: obj['self'] = obj — корректная программа.
    EXPECT_TRUE(store.objectGet(o, "self").sameAggregate(o));
    EXPECT_EQ(store.objectCount(o), 1u);
}

TEST(StoreObjectMutation, ObjectHoldsArrayAndArrayHoldsObject) {
    Store store;
    const Value o = store.makeObject();
    const Value a = store.makeArray();
    store.objectSet(o, "items", a);
    store.arrayPush(a, o);

    EXPECT_TRUE(store.objectGet(o, "items").sameAggregate(a));
    EXPECT_TRUE(store.arrayAt(a, 0).sameAggregate(o));
}

TEST(StoreObjectMutation, PushIntoStoredArrayIsSeenThroughTheObject) {
    Store store;
    const Value o = store.makeObject();
    const Value a = store.makeArray();
    store.objectSet(o, "items", a);

    for (int i = 0; i < 20; ++i) {
        store.arrayPush(store.objectGet(o, "items"), Value::number(static_cast<double>(i)));
    }
    EXPECT_EQ(store.arrayCount(a), 20u);
}

TEST(StoreObjectMutation, PreallocatedCapacityGrowsNothing) {
    Store store;
    const Value o = store.makeObject(8);
    const std::size_t afterReserve = store.bytesUsed();
    for (int i = 0; i < 8; ++i) {
        store.objectSet(o, "k" + std::to_string(i), Value::null());
    }
    // Пары не переезжали; выросло ровно на байты восьми двухсимвольных ключей.
    EXPECT_EQ(store.bytesUsed(), afterReserve + 16u);
}

TEST(StoreGlobals, FreshStoreHasNoGlobals) {
    Store store;
    EXPECT_EQ(store.globalCount(), 0u);
}

TEST(StoreGlobals, MissingGlobalReadsAsNull) {
    Store store;
    EXPECT_EQ(store.global("state").kind(), Value::Kind::Null);
    EXPECT_FALSE(store.hasGlobal("state"));
}

TEST(StoreGlobals, StoredGlobalIsFound) {
    Store store;
    store.setGlobal("count", Value::number(3.0));
    EXPECT_TRUE(store.hasGlobal("count"));
    EXPECT_EQ(store.global("count").numberValue(), 3.0);
    EXPECT_EQ(store.globalCount(), 1u);
}

TEST(StoreGlobals, NullGlobalIsDistinctFromAbsence) {
    Store store;
    store.setGlobal("maybe", Value::null());
    // Тот же довод, что для ключей объекта (docs/semantics.md §6.2).
    EXPECT_EQ(store.global("maybe").kind(), Value::Kind::Null);
    EXPECT_TRUE(store.hasGlobal("maybe"));
}

TEST(StoreGlobals, RepeatedSetReplacesValueWithoutAddingName) {
    Store store;
    store.setGlobal("state", Value::number(1.0));
    store.setGlobal("state", Value::number(2.0));
    EXPECT_EQ(store.globalCount(), 1u);
    EXPECT_EQ(store.global("state").numberValue(), 2.0);
}

TEST(StoreGlobals, GlobalHoldsAggregate) {
    Store store;
    const Value items = store.makeArray();
    store.arrayPush(items, Value::number(1.0));
    store.setGlobal("items", items);

    EXPECT_TRUE(store.global("items").sameAggregate(items));
    EXPECT_EQ(store.arrayCount(store.global("items")), 1u);
}

TEST(StoreGlobals, MutationThroughGlobalIsSeenThroughTheOriginal) {
    Store store;
    const Value items = store.makeArray();
    store.setGlobal("items", items);
    for (int i = 0; i < 30; ++i) {
        store.arrayPush(store.global("items"), Value::number(static_cast<double>(i)));
    }
    // docs/semantics.md §2.3: ссылочность наблюдаема и через глобальную переменную.
    EXPECT_EQ(store.arrayCount(items), 30u);
}

TEST(StoreGlobals, EnumerationYieldsEveryName) {
    Store store;
    store.setGlobal("user", Value::null());
    store.setGlobal("state", Value::null());

    ASSERT_EQ(store.globalCount(), 2u);
    std::string seen;
    for (std::uint32_t i = 0; i < store.globalCount(); ++i) {
        seen += store.globalNameAt(i);
        seen += ' ';
    }
    // Хранение отсортировано, как у любого объекта.
    EXPECT_EQ(seen, "state user ");
}

TEST(StoreStringBuilder, AssemblesFromParts) {
    Store store;
    const std::uint32_t mark = store.beginString();
    store.appendToString("Привет");
    store.appendToString(", ");
    store.appendToString("мир");
    const Value built = store.endString(mark);
    EXPECT_EQ(store.string(built), "Привет, мир");
}

TEST(StoreStringBuilder, EmptyBuildGivesEmptyString) {
    Store store;
    const std::uint32_t mark = store.beginString();
    const Value built = store.endString(mark);
    EXPECT_EQ(store.string(built), "");
}

TEST(StoreStringBuilder, AbortLeavesNothingBehind) {
    Store store;
    // Сборка идёт в собственном буфере, отдельном от пула текста (§ store.hpp
    // «сборка строки по частям»), поэтому bytesUsed() её не видит вовсе — до
    // endString пул текста не трогается. Наблюдаем через то, что действительно
    // меняется: прежняя строка цела, а следующая сборка не видит отменённого
    // хвоста.
    const Value before = store.makeString("уже в пуле");

    const std::uint32_t mark = store.beginString();
    store.appendToString("это будет выброшено");
    store.abortString(mark);

    EXPECT_EQ(store.string(before), "уже в пуле");

    const std::uint32_t nextMark = store.beginString();
    store.appendToString("новая сборка");
    EXPECT_EQ(store.string(store.endString(nextMark)), "новая сборка");
}

TEST(StoreStringBuilder, SurvivesPoolGrowth) {
    Store store;
    // Кусков заведомо больше, чем влезет без переезда пула: сборка обязана
    // держаться на смещениях, а не на указателях.
    const std::uint32_t mark = store.beginString();
    std::string expected;
    for (int i = 0; i < 500; ++i) {
        store.appendToString("кусок");
        expected += "кусок";
    }
    EXPECT_EQ(store.string(store.endString(mark)), expected);
}

TEST(StoreStringBuilder, NestedAbortLeavesTheOuterAssemblyIntact) {
    Store store;
    const std::uint32_t outer = store.beginString();
    store.appendToString("внешнее ");
    const std::uint32_t inner = store.beginString();
    store.appendToString("выброшенное");
    store.abortString(inner);
    store.appendToString("продолжение");
    EXPECT_EQ(store.string(store.endString(outer)), "внешнее продолжение");
}

}  // namespace
