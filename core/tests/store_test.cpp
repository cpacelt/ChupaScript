#include "store.hpp"

#include "box.hpp"

#include <gtest/gtest.h>

#include <string>
#include "aggregate.hpp"

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

TEST(StoreMetrics, FreshStoreHoldsNothing) {
    Store store;
    // Раньше здесь лежал пустой объект глобальных переменных — один заголовок,
    // и хранилище с рождения было непустым. Теперь у глобальных переменных своя
    // пара массивов (таблица имён и ячейки значений), оба пустые, и новое
    // хранилище не занимает вообще ничего.
    EXPECT_EQ(store.globalCount(), 0u);
    EXPECT_EQ(store.bytesUsed(), 0u);
}

TEST(StoreMetrics, MaterializedStringCostsABoxNotPoolBytes) {
    // Метрика хранилища мерит его собственные арены, а байты долгоживущей
    // строки лежат в коробке — памятью коробки хранилище не владеет и видеть её не
    // может. Раньше здесь стояло before + 5, и звался makeString: тот на
    // постоянном хранилище давал коробку. Больше не даёт — арена умеет одни
    // смещения, и коробку теперь просят у materialize прямо.
    Store store;
    const std::size_t before = store.bytesUsed();
    const std::size_t boxes = CS::detail::liveBoxCount();
    store.materialize("12345");
    EXPECT_EQ(store.bytesUsed(), before);
    EXPECT_EQ(CS::detail::liveBoxCount(), boxes + 1);
}

TEST(StoreMetrics, StringOfScratchStoreAddsItsBytes) {
    // А промежуточная строка по-прежнему смещение в арену операции, и байты
    // её видны там же, где и были.
    Store persistent;
    Store scratch(persistent.deferred());
    const std::size_t before = scratch.bytesUsed();
    scratch.makeString("12345");
    EXPECT_EQ(scratch.bytesUsed(), before + 5u);
}

TEST(StoreMetrics, ReservedCoversUsed) {
    Store store;
    const Value a = CS::makeArray(0, store.deferred());
    for (int i = 0; i < 100; ++i) {
        CS::arrayPush(a, Value::number(static_cast<double>(i)));
    }
    store.makeString("строка");
    EXPECT_GE(store.bytesReserved(), store.bytesUsed());
}

TEST(StoreArray, EmptyArrayHasNoElements) {
    Store store;
    const Value a = CS::makeArray(0, store.deferred());
    EXPECT_EQ(a.kind(), Value::Kind::Array);
    EXPECT_EQ(CS::arrayCount(a), 0u);
}

TEST(StoreArray, CapacityDoesNotCreateElements) {
    Store store;
    EXPECT_EQ(CS::arrayCount(CS::makeArray(16, store.deferred())), 0u);
}

TEST(StoreArray, ReadBeyondEndGivesNull) {
    Store store;
    const Value a = CS::makeArray(0, store.deferred());
    // semantics.md §6.1: чтение за границей — штатная ситуация.
    EXPECT_EQ(CS::arrayAt(a, 0).kind(), Value::Kind::Null);
    EXPECT_EQ(CS::arrayAt(a, 1000).kind(), Value::Kind::Null);
}

TEST(StoreArray, WriteBeyondEndIsRefused) {
    Store store;
    const Value a = CS::makeArray(8, store.deferred());
    // semantics.md §7.2: запись за границу — ошибка, ёмкость её не оправдывает.
    EXPECT_FALSE(CS::arraySet(a, 0, Value::number(1.0), store.deferred()));
}

TEST(StoreArray, TwoEmptyArraysAreDistinct) {
    Store store;
    EXPECT_FALSE(CS::makeArray(0, store.deferred()).sameAggregate(CS::makeArray(0, store.deferred())));
}

TEST(StoreArray, SameIndexInAnotherRegionIsAnotherArray) {
    // Индекс уникален внутри пула, а не между хранилищами: первый массив есть
    // и там, и там, но это разная память. Двух временных хранилищ проверка не
    // различит — регион категория, а не личность (docs/backlog.md [B57]).
    Store persistent;
    Store scratch(persistent.deferred());
    EXPECT_FALSE(CS::makeArray(0, persistent.deferred()).sameAggregate(CS::makeArray(0, scratch.deferred())));
}

TEST(StorePromote, KeepsSharingBetweenTwoReferences) {
    // Один массив, положенный в обе ячейки внешнего. Раньше продвижение
    // копировало вглубь и обязано было сделать одну копию, а не две; теперь
    // копии нет вовсе, и разделение сохраняется само собой. Проверка остаётся:
    // на ней стоят равенство по идентичности (semantics.md §5.4) и видимость
    // записи (§2.3).
    Store persistent;
    Store scratch(persistent.deferred());

    const Value inner = CS::makeArray(1, scratch.deferred());
    CS::arrayPush(inner, Value::number(1.0));
    const Value outer = CS::makeArray(2, scratch.deferred());
    CS::arrayPush(outer, inner);
    CS::arrayPush(outer, inner);

    const Value moved = persistent.promote(outer);
    EXPECT_TRUE(CS::arrayAt(moved, 0).sameAggregate(CS::arrayAt(moved, 1)));
}

TEST(StorePromote, WriteThroughOneReferenceIsSeenThroughTheOther) {
    Store persistent;
    Store scratch(persistent.deferred());

    const Value inner = CS::makeArray(1, scratch.deferred());
    CS::arrayPush(inner, Value::number(1.0));
    const Value outer = CS::makeArray(2, scratch.deferred());
    CS::arrayPush(outer, inner);
    CS::arrayPush(outer, inner);

    const Value moved = persistent.promote(outer);
    CS::arraySet(CS::arrayAt(moved, 1), 0, Value::number(3.0), persistent.deferred());
    EXPECT_EQ(CS::arrayAt(CS::arrayAt(moved, 0), 0).numberValue(), 3.0);
}

TEST(StorePromote, ValueOfOwnRegionIsReturnedAsIs) {
    Store persistent;
    Store scratch(persistent.deferred());
    const Value a = CS::makeArray(0, persistent.deferred());
    // Копии не случилось: тот же агрегат, а не равный ему по содержимому.
    EXPECT_TRUE(persistent.promote(a).sameAggregate(a));
}

TEST(StoreClear, EmptiesTheRegionButKeepsItsCapacity) {
    Store persistent;
    Store scratch(persistent.deferred());
    const Value a = CS::makeArray(0, scratch.deferred());
    for (int i = 0; i < 100; ++i) {
        CS::arrayPush(a, Value::number(static_cast<double>(i)));
    }
    scratch.makeString("строка");
    const std::size_t reserved = scratch.bytesReserved();
    ASSERT_GT(scratch.bytesUsed(), 0u);

    scratch.clear();

    EXPECT_EQ(scratch.bytesUsed(), 0u);
    // Ёмкость остаётся — на этом держится «ноль обращений к аллокатору в
    // установившемся режиме» (docs/backlog.md [B57]).
    EXPECT_EQ(scratch.bytesReserved(), reserved);
}

TEST(StoreWriteBarrier, PersistentValueFitsIntoScratchAggregate) {
    // Раньше это разрешал направленный барьер записи. Барьера больше нет, и
    // разрешать нечего: коробка живёт по счётчику, а не по региону, и оказаться
    // короче своего держателя не может. Проверка остаётся — это `[state.header]`.
    Store persistent;
    Store scratch(persistent.deferred());

    const Value header = persistent.makeString("шапка");
    const Value temporary = CS::makeArray(1, scratch.deferred());
    CS::arrayPush(temporary, header);

    // Читается тем хранилищем, которое его выдало: значение осталось собой, а
    // не переехало копией во временный пул.
    EXPECT_EQ(persistent.string(CS::arrayAt(temporary, 0)), "шапка");
}

TEST(StorePromote, LongerLivingValueIsNotCopiedIntoScratch) {
    // Та же направленность со стороны продвижения: во временное хранилище
    // постоянный агрегат обязан пройти как есть. Копия сделала бы его другим
    // объектом, и state.header перестал бы быть тем же, что state.rows[0].
    Store persistent;
    Store scratch(persistent.deferred());

    const Value header = CS::makeArray(0, persistent.deferred());
    EXPECT_TRUE(scratch.promote(header).sameAggregate(header));
}

TEST(StorePromote, ScalarIntoScratchIsReturnedAsIs) {
    // У скаляра региона нет, и поле у него равно постоянному по умолчанию.
    // Продвижение во временное хранилище не должно принимать это за чужой
    // регион и пытаться скопировать то, чего нет.
    Store persistent;
    Store scratch(persistent.deferred());
    EXPECT_EQ(scratch.promote(Value::number(7.0)).numberValue(), 7.0);
}

TEST(StorePromote, AggregateCrossesWithoutACopy) {
    // Раньше здесь проверялось, что продвижение копирует объект вглубь вместе
    // со строками и ключами. Копии больше нет: объект — коробка, и границу он
    // проходит ссылкой. Проверяется теперь ровно это.
    Store persistent;
    Store scratch(persistent.deferred());

    const Value o = CS::makeObject(persistent.keys(), 1, scratch.deferred());
    // Строка кладётся в агрегат, значит обязана быть материализована: смещение
    // в арену операции коробка не переживёт.
    CS::objectSet(o, "имя", scratch.materialize("Вася"), scratch.deferred());
    const Value moved = persistent.promote(o);

    EXPECT_TRUE(moved.sameAggregate(o));
    scratch.clear();
    EXPECT_EQ(CS::objectKeyAt(moved, 0), "имя");
    EXPECT_EQ(persistent.string(CS::objectValueAt(moved, 0)), "Вася");
}

TEST(StorePromote, ScratchStringIsMaterializedOnItsWayIn) {
    Store persistent;
    Store scratch(persistent.deferred());
    const Value temp = scratch.makeString("Вася");
    ASSERT_EQ(temp.region(), Value::Region::Scratch);

    const Value o = CS::makeObject(persistent.keys(), 1, persistent.deferred());
    CS::objectSet(o, "имя", scratch.promote(temp), persistent.deferred());
    scratch.clear();
    EXPECT_EQ(persistent.string(CS::objectValueAt(o, 0)), "Вася");
}

TEST(StoreArray, CopyOfValueIsTheSameArray) {
    Store store;
    const Value a = CS::makeArray(0, store.deferred());
    const Value b = a;
    EXPECT_TRUE(a.sameAggregate(b));
}

TEST(StoreArrayMutation, PushAppendsInOrder) {
    Store store;
    const Value a = CS::makeArray(0, store.deferred());
    CS::arrayPush(a, Value::number(1.0));
    CS::arrayPush(a, Value::number(2.0));
    ASSERT_EQ(CS::arrayCount(a), 2u);
    EXPECT_EQ(CS::arrayAt(a, 0).numberValue(), 1.0);
    EXPECT_EQ(CS::arrayAt(a, 1).numberValue(), 2.0);
}

TEST(StoreArrayMutation, SetReplacesExistingElement) {
    Store store;
    const Value a = CS::makeArray(0, store.deferred());
    CS::arrayPush(a, Value::number(1.0));
    EXPECT_TRUE(CS::arraySet(a, 0, Value::boolean(true), store.deferred()));
    EXPECT_TRUE(CS::arrayAt(a, 0).booleanValue());
}

TEST(StoreArrayMutation, PopReturnsLastAndShrinks) {
    Store store;
    const Value a = CS::makeArray(0, store.deferred());
    CS::arrayPush(a, Value::number(1.0));
    CS::arrayPush(a, Value::number(2.0));

    Value taken = Value::null();
    ASSERT_TRUE(CS::arrayPop(a, &taken, store.deferred()));
    EXPECT_EQ(taken.numberValue(), 2.0);
    EXPECT_EQ(CS::arrayCount(a), 1u);
}

TEST(StoreArrayMutation, PopOnEmptyIsRefused) {
    Store store;
    Value taken = Value::number(7.0);
    EXPECT_FALSE(CS::arrayPop(CS::makeArray(0, store.deferred()), &taken, store.deferred()));
    // Отказ не трогает выходной параметр.
    EXPECT_EQ(taken.numberValue(), 7.0);
}

TEST(StoreArrayMutation, AliasSurvivesGrowth) {
    Store store;
    const Value a = CS::makeArray(0, store.deferred());
    const Value alias = a;

    // Рост через все удвоения: 4, 8, 16, 32 — данные переезжают четырежды.
    for (int i = 0; i < 40; ++i) {
        CS::arrayPush(a, Value::number(static_cast<double>(i)));
    }

    // semantics.md §2.3: изменение через одно имя видно через второе.
    EXPECT_EQ(CS::arrayCount(alias), 40u);
    EXPECT_EQ(CS::arrayAt(alias, 0).numberValue(), 0.0);
    EXPECT_EQ(CS::arrayAt(alias, 39).numberValue(), 39.0);
    EXPECT_TRUE(a.sameAggregate(alias));
}

TEST(StoreArrayMutation, WriteThroughAliasIsSeenByOriginal) {
    Store store;
    const Value a = CS::makeArray(0, store.deferred());
    CS::arrayPush(a, Value::number(1.0));
    const Value alias = a;

    ASSERT_TRUE(CS::arraySet(alias, 0, Value::number(99.0), store.deferred()));
    EXPECT_EQ(CS::arrayAt(a, 0).numberValue(), 99.0);
}

TEST(StoreArrayMutation, NestedArrayKeepsIdentity) {
    Store store;
    const Value outer = CS::makeArray(0, store.deferred());
    const Value inner = CS::makeArray(0, store.deferred());
    CS::arrayPush(outer, inner);
    CS::arrayPush(inner, Value::number(1.0));

    const Value fetched = CS::arrayAt(outer, 0);
    EXPECT_TRUE(fetched.sameAggregate(inner));
    EXPECT_EQ(CS::arrayCount(fetched), 1u);
}

TEST(StoreArrayMutation, ArrayMayContainItself) {
    Store store;
    const Value a = CS::makeArray(0, store.deferred());
    CS::arrayPush(a, a);
    // semantics.md §2.3: цикл допустим, рекурсивного обхода в слое нет.
    EXPECT_TRUE(CS::arrayAt(a, 0).sameAggregate(a));
}

TEST(StoreArrayMutation, PreallocatedCapacityGrowsNothing) {
    Store store;
    const Value a = CS::makeArray(64, store.deferred());
    const std::size_t afterReserve = store.bytesUsed();
    for (int i = 0; i < 64; ++i) {
        CS::arrayPush(a, Value::number(static_cast<double>(i)));
    }
    // Размер известен заранее — переездов и мусора нет (спека §5).
    EXPECT_EQ(store.bytesUsed(), afterReserve);
}

TEST(StoreArrayMutation, GrowthLeavesNoGarbageBehind) {
    // Раньше здесь проверялось обратное: массив лежал в пуле сплошным
    // диапазоном, дописать в хвост было нельзя, и рост до 64 элементов
    // выделял 4+8+16+32+64 = 124 слота, бросая 60 из них мусором навсегда.
    //
    // Массив теперь владеет элементами сам, и от роста в хранилище не остаётся
    // ничего: его метрика байт про элементы не знает вовсе.
    Store store;
    const Value a = CS::makeArray(0, store.deferred());
    const std::size_t before = store.bytesUsed();
    const std::size_t nodes = CS::detail::liveBoxCount();
    for (int i = 0; i < 64; ++i) {
        CS::arrayPush(a, Value::number(static_cast<double>(i)));
    }
    EXPECT_EQ(CS::arrayCount(a), 64u);
    EXPECT_EQ(store.bytesUsed(), before);
    // Ни одного лишнего коробки: рост вектора внутри коробки коробок не заводит.
    EXPECT_EQ(CS::detail::liveBoxCount(), nodes);
}

TEST(StoreArrayMutation, RequestedCapacityIsAllocatedExactly) {
    Store store;
    const Value a = CS::makeArray(100, store.deferred());
    const std::size_t afterReserve = store.bytesUsed();
    for (int i = 0; i < 100; ++i) {
        CS::arrayPush(a, Value::number(static_cast<double>(i)));
    }
    // Сто элементов занимают сто слотов, а не ближайшую степень двойки.
    EXPECT_EQ(store.bytesUsed(), afterReserve);
}

TEST(StoreObject, EmptyObjectHasNoKeys) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    EXPECT_EQ(o.kind(), Value::Kind::Object);
    EXPECT_EQ(CS::objectCount(o), 0u);
}

TEST(StoreObject, MissingKeyReadsAsNull) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    // semantics.md §6.2: отсутствующий ключ читается как null.
    EXPECT_EQ(CS::objectGet(o, "нет").kind(), Value::Kind::Null);
    EXPECT_FALSE(CS::objectHas(o, "нет"));
}

TEST(StoreObject, StoredValueIsFound) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    CS::objectSet(o, "count", Value::number(3.0), store.deferred());
    EXPECT_TRUE(CS::objectHas(o, "count"));
    EXPECT_EQ(CS::objectGet(o, "count").numberValue(), 3.0);
    EXPECT_EQ(CS::objectCount(o), 1u);
}

TEST(StoreObject, NullValueIsDistinctFromAbsence) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    CS::objectSet(o, "key", Value::null(), store.deferred());
    // semantics.md §6.2: отличить одно от другого можно только через has.
    EXPECT_EQ(CS::objectGet(o, "key").kind(), Value::Kind::Null);
    EXPECT_TRUE(CS::objectHas(o, "key"));
}

TEST(StoreObject, FindsKeyAmongMany) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    const char *keys[] = {"zeta", "alpha", "mu", "beta", "omega", "kappa", "iota"};
    for (int i = 0; i < 7; ++i) {
        CS::objectSet(o, keys[i], Value::number(static_cast<double>(i)), store.deferred());
    }
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(CS::objectGet(o, keys[i]).numberValue(), static_cast<double>(i));
    }
    EXPECT_EQ(CS::objectCount(o), 7u);
}

TEST(StoreObject, PrefixKeyIsNotConfusedWithLongerOne) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    CS::objectSet(o, "item", Value::number(1.0), store.deferred());
    CS::objectSet(o, "items", Value::number(2.0), store.deferred());
    EXPECT_EQ(CS::objectGet(o, "item").numberValue(), 1.0);
    EXPECT_EQ(CS::objectGet(o, "items").numberValue(), 2.0);
}

TEST(StoreObject, NonAsciiKeyIsFound) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    CS::objectSet(o, "имя", store.makeString("Вася"), store.deferred());
    EXPECT_EQ(store.string(CS::objectGet(o, "имя")), "Вася");
}

TEST(StoreObject, EmptyKeyIsAKeyLikeAnyOther) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    CS::objectSet(o, "", Value::number(1.0), store.deferred());
    EXPECT_TRUE(CS::objectHas(o, ""));
    EXPECT_EQ(CS::objectGet(o, "").numberValue(), 1.0);
}

TEST(StoreObject, EmptyKeyIsDistinguishableFromAbsentOne) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    CS::objectSet(o, "", Value::number(1.0), store.deferred());
    CS::objectSet(o, "другой", Value::number(2.0), store.deferred());

    // Пустой ключ существует: срез пустой, но не нулевой.
    ASSERT_EQ(CS::objectCount(o), 2u);
    EXPECT_TRUE(CS::objectKeyAt(o, 0).empty());
    EXPECT_NE(CS::objectKeyAt(o, 0).data(), nullptr);
    // За границей — нулевой срез.
    EXPECT_EQ(CS::objectKeyAt(o, 99).data(), nullptr);
}

TEST(StoreObject, EnumerationYieldsEveryKey) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    CS::objectSet(o, "b", Value::number(2.0), store.deferred());
    CS::objectSet(o, "a", Value::number(1.0), store.deferred());

    ASSERT_EQ(CS::objectCount(o), 2u);
    std::string seen;
    for (std::uint32_t i = 0; i < CS::objectCount(o); ++i) {
        seen += CS::objectKeyAt(o, i);
        seen += '=';
        seen += std::to_string(static_cast<int>(CS::objectValueAt(o, i).numberValue()));
        seen += ';';
    }
    // Порядок наружу не обещан (semantics.md §2.1), но хранение отсортировано.
    EXPECT_EQ(seen, "a=1;b=2;");
}

TEST(StoreObject, EnumerationBeyondEndIsEmpty) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    EXPECT_TRUE(CS::objectKeyAt(o, 0).empty());
    EXPECT_EQ(CS::objectValueAt(o, 0).kind(), Value::Kind::Null);
}

TEST(StoreObject, TwoEmptyObjectsAreDistinct) {
    Store store;
    EXPECT_FALSE(CS::makeObject(store.keys(), 0, store.deferred()).sameAggregate(CS::makeObject(store.keys(), 0, store.deferred())));
}

TEST(StoreObjectMutation, RepeatedKeyReplacesValue) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    CS::objectSet(o, "k", Value::number(1.0), store.deferred());
    CS::objectSet(o, "k", Value::number(2.0), store.deferred());
    EXPECT_EQ(CS::objectCount(o), 1u);
    EXPECT_EQ(CS::objectGet(o, "k").numberValue(), 2.0);
}

TEST(StoreObjectMutation, ReplacementCopiesNoKeyBytes) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    CS::objectSet(o, "k", Value::number(1.0), store.deferred());
    const std::size_t before = store.bytesUsed();
    CS::objectSet(o, "k", Value::number(2.0), store.deferred());
    EXPECT_EQ(store.bytesUsed(), before);
}

TEST(StoreObjectMutation, InsertionKeepsSortedOrder) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    const char *keys[] = {"delta", "alpha", "charlie", "bravo", "echo"};
    for (const char *key : keys) { CS::objectSet(o, key, Value::null(), store.deferred()); }

    std::string seen;
    for (std::uint32_t i = 0; i < CS::objectCount(o); ++i) {
        seen += CS::objectKeyAt(o, i);
        seen += ' ';
    }
    EXPECT_EQ(seen, "alpha bravo charlie delta echo ");
}

TEST(StoreObjectMutation, EveryKeySurvivesGrowth) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    // Тридцать ключей — рост через 4, 8, 16, 32: пары переезжают четырежды.
    for (int i = 0; i < 30; ++i) {
        CS::objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)), store.deferred());
    }
    ASSERT_EQ(CS::objectCount(o), 30u);
    for (int i = 0; i < 30; ++i) {
        EXPECT_EQ(CS::objectGet(o, "key" + std::to_string(i)).numberValue(),
                  static_cast<double>(i));
    }
}

TEST(StoreObjectMutation, AliasSeesNewKey) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    const Value alias = o;
    for (int i = 0; i < 30; ++i) {
        CS::objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)), store.deferred());
    }
    // semantics.md §2.3: изменение через одно имя видно через второе.
    EXPECT_EQ(CS::objectCount(alias), 30u);
    EXPECT_EQ(CS::objectGet(alias, "key29").numberValue(), 29.0);
    EXPECT_TRUE(o.sameAggregate(alias));
}

TEST(StoreObjectMutation, KeyTakenFromTheSameStoreWorks) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    const Value keyValue = store.makeString("динамический");
    // Ключ — срез собственного пула текста: приём, которым пользуется obj[k].
    CS::objectSet(o, store.string(keyValue), Value::number(5.0), store.deferred());
    EXPECT_EQ(CS::objectGet(o, "динамический").numberValue(), 5.0);
}

TEST(StoreObjectMutation, ObjectMayContainItself) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    CS::objectSet(o, "self", o, store.deferred());
    // semantics.md §2.3: obj['self'] = obj — корректная программа.
    EXPECT_TRUE(CS::objectGet(o, "self").sameAggregate(o));
    EXPECT_EQ(CS::objectCount(o), 1u);
}

TEST(StoreObjectMutation, ObjectHoldsArrayAndArrayHoldsObject) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    const Value a = CS::makeArray(0, store.deferred());
    CS::objectSet(o, "items", a, store.deferred());
    CS::arrayPush(a, o);

    EXPECT_TRUE(CS::objectGet(o, "items").sameAggregate(a));
    EXPECT_TRUE(CS::arrayAt(a, 0).sameAggregate(o));
}

TEST(StoreObjectMutation, PushIntoStoredArrayIsSeenThroughTheObject) {
    Store store;
    const Value o = CS::makeObject(store.keys(), 0, store.deferred());
    const Value a = CS::makeArray(0, store.deferred());
    CS::objectSet(o, "items", a, store.deferred());

    for (int i = 0; i < 20; ++i) {
        CS::arrayPush(CS::objectGet(o, "items"), Value::number(static_cast<double>(i)));
    }
    EXPECT_EQ(CS::arrayCount(a), 20u);
}

TEST(StoreObjectMutation, KeyBytesLiveInTheTableNotInTheStore) {
    // Раньше байты ключа дописывались в пул текста хранилища, и восемь
    // двухсимвольных имён давали ровно 16 байт прироста. Теперь имя живёт в
    // таблице интернирования, а хранилище о нём не знает.
    Store store;
    const Value o = CS::makeObject(store.keys(), 8, store.deferred());
    const std::size_t afterReserve = store.bytesUsed();
    for (int i = 0; i < 8; ++i) {
        CS::objectSet(o, "k" + std::to_string(i), Value::null(), store.deferred());
    }
    EXPECT_EQ(store.bytesUsed(), afterReserve);
    EXPECT_EQ(store.keys()->count(), 8u);
}

TEST(StoreObjectMutation, RepeatedKeyIsInternedOnce) {
    // Ради чего таблица и заводилась: тысяча объектов с одним именем поля
    // хранит одно имя, а не тысячу.
    Store store;
    for (int i = 0; i < 1000; ++i) {
        const Value o = CS::makeObject(store.keys(), 1, store.deferred());
        CS::objectSet(o, "name", Value::number(i), store.deferred());
    }
    EXPECT_EQ(store.keys()->count(), 1u);
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
    const Value items = CS::makeArray(0, store.deferred());
    CS::arrayPush(items, Value::number(1.0));
    store.setGlobal("items", items);

    EXPECT_TRUE(store.global("items").sameAggregate(items));
    EXPECT_EQ(CS::arrayCount(store.global("items")), 1u);
}

TEST(StoreGlobals, MutationThroughGlobalIsSeenThroughTheOriginal) {
    Store store;
    const Value items = CS::makeArray(0, store.deferred());
    store.setGlobal("items", items);
    for (int i = 0; i < 30; ++i) {
        CS::arrayPush(store.global("items"), Value::number(static_cast<double>(i)));
    }
    // docs/semantics.md §2.3: ссылочность наблюдаема и через глобальную переменную.
    EXPECT_EQ(CS::arrayCount(items), 30u);
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

TEST(StoreString, ScratchStoreMakesOffsets) {
    Store persistent;
    Store scratch(persistent.deferred());
    const Value v = scratch.makeString("a");
    EXPECT_EQ(v.region(), Value::Region::Scratch);
    EXPECT_EQ(scratch.string(v), "a");
}

TEST(StoreString, MaterializeMakesANodeEvenInScratch) {
    Store persistent;
    Store scratch(persistent.deferred());
    const Value v = scratch.materialize("a");
    EXPECT_EQ(v.region(), Value::Region::Boxed);
    scratch.clear();
    // Узел арену не заметил. Ссылку держит список отложенного освобождения
    // хранилища, поэтому отпускать её здесь не надо и нельзя.
    EXPECT_EQ(scratch.string(v), "a");
}

TEST(StorePromote, ScratchStringBecomesABoxAndOutlivesTheArena) {
    Store persistent;
    Store scratch(persistent.deferred());
    const Value temp = scratch.makeString("привет");
    ASSERT_EQ(temp.region(), Value::Region::Scratch);

    // Продвигает та арена, что выдала смещение, — другой прочитать его негде.
    const Value kept = scratch.promote(temp);
    EXPECT_EQ(kept.region(), Value::Region::Boxed);
    scratch.clear();
    // А вот прочитать коробку вправе любое хранилище: она самодостаточна.
    EXPECT_EQ(persistent.string(kept), "привет");
}

TEST(StoreLiteral, InternedLiteralLivesAsLongAsTheStore) {
    const std::size_t before = CS::detail::liveBoxCount();
    {
        Store store;
        CS::detail::StringBox *literal = store.internLiteral("abc");
        EXPECT_EQ(literal->view(), "abc");
        EXPECT_EQ(CS::detail::liveBoxCount(), before + 1);
    }
    EXPECT_EQ(CS::detail::liveBoxCount(), before);
}

}  // namespace
