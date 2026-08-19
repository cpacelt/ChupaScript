#include "store.hpp"

#include "box.hpp"

#include <gtest/gtest.h>

#include <string>
#include "aggregate.hpp"
#include "execution.hpp"

namespace {

using CS::Deferred;
using CS::Execution;
using CS::Store;
using CS::Value;

TEST(StoreMetrics, FreshStoreHoldsNothing) {
    Store store;
    // Раньше здесь лежал пустой объект глобальных переменных — один заголовок,
    // и хранилище с рождения было непустым. Теперь у глобальных переменных своя
    // пара массивов (таблица имён и ячейки значений), оба пустые, и новое
    // хранилище не занимает вообще ничего.
    EXPECT_EQ(store.globalCount(), 0u);
    EXPECT_EQ(store.bytesUsed(), 0u);
}

#ifndef NDEBUG
TEST(StoreMetrics, MaterializedStringCostsABoxNotPoolBytes) {
    // Метрика хранилища мерит его собственные арены, а байты долгоживущей
    // строки лежат в коробке — памятью коробки хранилище не владеет и видеть её не
    // может. Раньше здесь стояло before + 5, и звался makeString: тот на
    // постоянном хранилище давал коробку. Больше не даёт — арена умеет одни
    // смещения, и коробку теперь просят у materialize прямо.
    Store store;
    Deferred dead;
    const std::size_t before = store.bytesUsed();
    const std::size_t boxes = CS::detail::liveBoxCount();
    CS::materialize("12345", dead);
    EXPECT_EQ(store.bytesUsed(), before);
    EXPECT_EQ(CS::detail::liveBoxCount(), boxes + 1);
}
#endif

TEST(StoreMetrics, ReservedCoversUsed) {
    Store store;
    Deferred dead;
    // A box (CS::makeArray/arrayPush) never touches the store's own pools —
    // it lives by refcount, not by region — so the subject here has to be
    // what actually writes into text_/globalNames_/globalValues_: a global
    // variable. Without this, bytesUsed() and bytesReserved() both read 0 on
    // a fresh Store and the assertion below would hold no matter what
    // bytesReserved() did.
    store.setGlobal("name", CS::materialize("значение", dead), dead);
    EXPECT_GT(store.bytesUsed(), 0u);
    EXPECT_GE(store.bytesReserved(), store.bytesUsed());
}

TEST(StoreArray, EmptyArrayHasNoElements) {
    Store store;
    Deferred dead;
    const Value a = CS::makeArray(0, dead);
    EXPECT_EQ(a.kind(), Value::Kind::Array);
    EXPECT_EQ(CS::arrayCount(a), 0u);
}

TEST(StoreArray, CapacityDoesNotCreateElements) {
    Store store;
    Deferred dead;
    EXPECT_EQ(CS::arrayCount(CS::makeArray(16, dead)), 0u);
}

TEST(StoreArray, ReadBeyondEndGivesNull) {
    Store store;
    Deferred dead;
    const Value a = CS::makeArray(0, dead);
    // semantics.md §6.1: чтение за границей — штатная ситуация.
    EXPECT_EQ(CS::arrayAt(a, 0).kind(), Value::Kind::Null);
    EXPECT_EQ(CS::arrayAt(a, 1000).kind(), Value::Kind::Null);
}

TEST(StoreArray, WriteBeyondEndIsRefused) {
    Store store;
    Deferred dead;
    const Value a = CS::makeArray(8, dead);
    // semantics.md §7.2: запись за границу — ошибка, ёмкость её не оправдывает.
    EXPECT_FALSE(CS::arraySet(a, 0, Value::number(1.0), dead));
}

TEST(StoreArray, TwoEmptyArraysAreDistinct) {
    Store store;
    Deferred dead;
    EXPECT_FALSE(CS::makeArray(0, dead).sameAggregate(CS::makeArray(0, dead)));
}

TEST(StoreArray, SameIndexInAnotherRegionIsAnotherArray) {
    // Индекс уникален внутри пула, а не между хранилищами: первый массив есть
    // и там, и там, но это разная память. Двух временных хранилищ проверка не
    // различит — регион категория, а не личность (docs/backlog.md [B57]).
    Store persistent;
    Execution exec{persistent};
    Store &scratch = exec.scratch;
    EXPECT_FALSE(CS::makeArray(0, exec.deferred()).sameAggregate(CS::makeArray(0, exec.deferred())));
}

TEST(StorePromote, KeepsSharingBetweenTwoReferences) {
    // Один массив, положенный в обе ячейки внешнего. Раньше продвижение
    // копировало вглубь и обязано было сделать одну копию, а не две; теперь
    // копии нет вовсе, и разделение сохраняется само собой. Проверка остаётся:
    // на ней стоят равенство по идентичности (semantics.md §5.4) и видимость
    // записи (§2.3).
    Store persistent;
    Execution exec{persistent};
    Store &scratch = exec.scratch;

    const Value inner = CS::makeArray(1, exec.deferred());
    CS::arrayPush(inner, Value::number(1.0));
    const Value outer = CS::makeArray(2, exec.deferred());
    CS::arrayPush(outer, inner);
    CS::arrayPush(outer, inner);

    const Value moved = exec.promote(outer);
    EXPECT_TRUE(CS::arrayAt(moved, 0).sameAggregate(CS::arrayAt(moved, 1)));
}

TEST(StorePromote, WriteThroughOneReferenceIsSeenThroughTheOther) {
    Store persistent;
    Execution exec{persistent};
    Store &scratch = exec.scratch;

    const Value inner = CS::makeArray(1, exec.deferred());
    CS::arrayPush(inner, Value::number(1.0));
    const Value outer = CS::makeArray(2, exec.deferred());
    CS::arrayPush(outer, inner);
    CS::arrayPush(outer, inner);

    const Value moved = exec.promote(outer);
    CS::arraySet(CS::arrayAt(moved, 1), 0, Value::number(3.0), exec.deferred());
    EXPECT_EQ(CS::arrayAt(CS::arrayAt(moved, 0), 0).numberValue(), 3.0);
}

TEST(StorePromote, ValueOfOwnRegionIsReturnedAsIs) {
    Store persistent;
    Execution exec{persistent};
    Store &scratch = exec.scratch;
    const Value a = CS::makeArray(0, exec.deferred());
    // Копии не случилось: тот же агрегат, а не равный ему по содержимому.
    EXPECT_TRUE(exec.promote(a).sameAggregate(a));
}

TEST(StoreWriteBarrier, PersistentValueFitsIntoScratchAggregate) {
    // Раньше это разрешал направленный барьер записи. Барьера больше нет, и
    // разрешать нечего: коробка живёт по счётчику, а не по региону, и оказаться
    // короче своего держателя не может. Проверка остаётся — это `[state.header]`.
    Store persistent;
    Execution exec{persistent};
    Store &scratch = exec.scratch;

    const Value header = CS::materialize("шапка", exec.deferred());
    const Value temporary = CS::makeArray(1, exec.deferred());
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
    Execution exec{persistent};
    Store &scratch = exec.scratch;

    const Value header = CS::makeArray(0, exec.deferred());
    EXPECT_TRUE(exec.promote(header).sameAggregate(header));
}

TEST(StorePromote, ScalarIntoScratchIsReturnedAsIs) {
    // У скаляра региона нет, и поле у него равно постоянному по умолчанию.
    // Продвижение во временное хранилище не должно принимать это за чужой
    // регион и пытаться скопировать то, чего нет.
    Store persistent;
    Execution exec{persistent};
    Store &scratch = exec.scratch;
    EXPECT_EQ(exec.promote(Value::number(7.0)).numberValue(), 7.0);
}

TEST(StorePromote, AggregateCrossesWithoutACopy) {
    // Раньше здесь проверялось, что продвижение копирует объект вглубь вместе
    // со строками и ключами. Копии больше нет: объект — коробка, и границу он
    // проходит ссылкой. Проверяется теперь ровно это.
    Store persistent;
    Execution exec{persistent};
    Store &scratch = exec.scratch;

    const Value o = CS::makeObject(persistent.keys(), 1, exec.deferred());
    // Строка кладётся в агрегат, значит обязана быть материализована: смещение
    // в арену операции коробка не переживёт.
    CS::objectSet(o, "имя", CS::materialize("Вася", exec.deferred()), exec.deferred());
    const Value moved = exec.promote(o);

    EXPECT_TRUE(moved.sameAggregate(o));
    scratch.clear();
    EXPECT_EQ(CS::objectKeyAt(moved, 0), "имя");
    EXPECT_EQ(persistent.string(CS::objectValueAt(moved, 0)), "Вася");
}

TEST(StoreArray, CopyOfValueIsTheSameArray) {
    Store store;
    Deferred dead;
    const Value a = CS::makeArray(0, dead);
    const Value b = a;
    EXPECT_TRUE(a.sameAggregate(b));
}

TEST(StoreArrayMutation, PushAppendsInOrder) {
    Store store;
    Deferred dead;
    const Value a = CS::makeArray(0, dead);
    CS::arrayPush(a, Value::number(1.0));
    CS::arrayPush(a, Value::number(2.0));
    ASSERT_EQ(CS::arrayCount(a), 2u);
    EXPECT_EQ(CS::arrayAt(a, 0).numberValue(), 1.0);
    EXPECT_EQ(CS::arrayAt(a, 1).numberValue(), 2.0);
}

TEST(StoreArrayMutation, SetReplacesExistingElement) {
    Store store;
    Deferred dead;
    const Value a = CS::makeArray(0, dead);
    CS::arrayPush(a, Value::number(1.0));
    EXPECT_TRUE(CS::arraySet(a, 0, Value::boolean(true), dead));
    EXPECT_TRUE(CS::arrayAt(a, 0).booleanValue());
}

TEST(StoreArrayMutation, PopReturnsLastAndShrinks) {
    Store store;
    Deferred dead;
    const Value a = CS::makeArray(0, dead);
    CS::arrayPush(a, Value::number(1.0));
    CS::arrayPush(a, Value::number(2.0));

    Value taken = Value::null();
    ASSERT_TRUE(CS::arrayPop(a, &taken, dead));
    EXPECT_EQ(taken.numberValue(), 2.0);
    EXPECT_EQ(CS::arrayCount(a), 1u);
}

TEST(StoreArrayMutation, PopOnEmptyIsRefused) {
    Store store;
    Deferred dead;
    Value taken = Value::number(7.0);
    EXPECT_FALSE(CS::arrayPop(CS::makeArray(0, dead), &taken, dead));
    // Отказ не трогает выходной параметр.
    EXPECT_EQ(taken.numberValue(), 7.0);
}

TEST(StoreArrayMutation, AliasSurvivesGrowth) {
    Store store;
    Deferred dead;
    const Value a = CS::makeArray(0, dead);
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
    Deferred dead;
    const Value a = CS::makeArray(0, dead);
    CS::arrayPush(a, Value::number(1.0));
    const Value alias = a;

    ASSERT_TRUE(CS::arraySet(alias, 0, Value::number(99.0), dead));
    EXPECT_EQ(CS::arrayAt(a, 0).numberValue(), 99.0);
}

TEST(StoreArrayMutation, NestedArrayKeepsIdentity) {
    Store store;
    Deferred dead;
    const Value outer = CS::makeArray(0, dead);
    const Value inner = CS::makeArray(0, dead);
    CS::arrayPush(outer, inner);
    CS::arrayPush(inner, Value::number(1.0));

    const Value fetched = CS::arrayAt(outer, 0);
    EXPECT_TRUE(fetched.sameAggregate(inner));
    EXPECT_EQ(CS::arrayCount(fetched), 1u);
}

TEST(StoreArrayMutation, ArrayMayContainItself) {
    Store store;
    Deferred dead;
    const Value a = CS::makeArray(0, dead);
    CS::arrayPush(a, a);
    // semantics.md §2.3: цикл допустим, рекурсивного обхода в слое нет.
    EXPECT_TRUE(CS::arrayAt(a, 0).sameAggregate(a));
}

TEST(StoreArrayMutation, PreallocatedCapacityGrowsNothing) {
    Store store;
    Deferred dead;
    const Value a = CS::makeArray(64, dead);
    const std::size_t afterReserve = store.bytesUsed();
    for (int i = 0; i < 64; ++i) {
        CS::arrayPush(a, Value::number(static_cast<double>(i)));
    }
    // Размер известен заранее — переездов и мусора нет (спека §5).
    EXPECT_EQ(store.bytesUsed(), afterReserve);
}

#ifndef NDEBUG
TEST(StoreArrayMutation, GrowthLeavesNoGarbageBehind) {
    // Раньше здесь проверялось обратное: массив лежал в пуле сплошным
    // диапазоном, дописать в хвост было нельзя, и рост до 64 элементов
    // выделял 4+8+16+32+64 = 124 слота, бросая 60 из них мусором навсегда.
    //
    // Массив теперь владеет элементами сам, и от роста в хранилище не остаётся
    // ничего: его метрика байт про элементы не знает вовсе.
    Store store;
    Deferred dead;
    const Value a = CS::makeArray(0, dead);
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
#endif

TEST(StoreArrayMutation, RequestedCapacityIsAllocatedExactly) {
    Store store;
    Deferred dead;
    const Value a = CS::makeArray(100, dead);
    const std::size_t afterReserve = store.bytesUsed();
    for (int i = 0; i < 100; ++i) {
        CS::arrayPush(a, Value::number(static_cast<double>(i)));
    }
    // Сто элементов занимают сто слотов, а не ближайшую степень двойки.
    EXPECT_EQ(store.bytesUsed(), afterReserve);
}

TEST(StoreObject, EmptyObjectHasNoKeys) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    EXPECT_EQ(o.kind(), Value::Kind::Object);
    EXPECT_EQ(CS::objectCount(o), 0u);
}

TEST(StoreObject, MissingKeyReadsAsNull) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    // semantics.md §6.2: отсутствующий ключ читается как null.
    EXPECT_EQ(CS::objectGet(o, "нет").kind(), Value::Kind::Null);
    EXPECT_FALSE(CS::objectHas(o, "нет"));
}

TEST(StoreObject, StoredValueIsFound) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    CS::objectSet(o, "count", Value::number(3.0), dead);
    EXPECT_TRUE(CS::objectHas(o, "count"));
    EXPECT_EQ(CS::objectGet(o, "count").numberValue(), 3.0);
    EXPECT_EQ(CS::objectCount(o), 1u);
}

TEST(StoreObject, NullValueIsDistinctFromAbsence) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    CS::objectSet(o, "key", Value::null(), dead);
    // semantics.md §6.2: отличить одно от другого можно только через has.
    EXPECT_EQ(CS::objectGet(o, "key").kind(), Value::Kind::Null);
    EXPECT_TRUE(CS::objectHas(o, "key"));
}

TEST(StoreObject, FindsKeyAmongMany) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    const char *keys[] = {"zeta", "alpha", "mu", "beta", "omega", "kappa", "iota"};
    for (int i = 0; i < 7; ++i) {
        CS::objectSet(o, keys[i], Value::number(static_cast<double>(i)), dead);
    }
    for (int i = 0; i < 7; ++i) {
        EXPECT_EQ(CS::objectGet(o, keys[i]).numberValue(), static_cast<double>(i));
    }
    EXPECT_EQ(CS::objectCount(o), 7u);
}

TEST(StoreObject, PrefixKeyIsNotConfusedWithLongerOne) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    CS::objectSet(o, "item", Value::number(1.0), dead);
    CS::objectSet(o, "items", Value::number(2.0), dead);
    EXPECT_EQ(CS::objectGet(o, "item").numberValue(), 1.0);
    EXPECT_EQ(CS::objectGet(o, "items").numberValue(), 2.0);
}

TEST(StoreObject, NonAsciiKeyIsFound) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    CS::objectSet(o, "имя", CS::materialize("Вася", dead), dead);
    EXPECT_EQ(store.string(CS::objectGet(o, "имя")), "Вася");
}

TEST(StoreObject, EmptyKeyIsAKeyLikeAnyOther) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    CS::objectSet(o, "", Value::number(1.0), dead);
    EXPECT_TRUE(CS::objectHas(o, ""));
    EXPECT_EQ(CS::objectGet(o, "").numberValue(), 1.0);
}

TEST(StoreObject, EmptyKeyIsDistinguishableFromAbsentOne) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    CS::objectSet(o, "", Value::number(1.0), dead);
    CS::objectSet(o, "другой", Value::number(2.0), dead);

    // Пустой ключ существует: срез пустой, но не нулевой.
    ASSERT_EQ(CS::objectCount(o), 2u);
    EXPECT_TRUE(CS::objectKeyAt(o, 0).empty());
    EXPECT_NE(CS::objectKeyAt(o, 0).data(), nullptr);
    // За границей — нулевой срез.
    EXPECT_EQ(CS::objectKeyAt(o, 99).data(), nullptr);
}

TEST(StoreObject, EnumerationYieldsEveryKey) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    CS::objectSet(o, "b", Value::number(2.0), dead);
    CS::objectSet(o, "a", Value::number(1.0), dead);

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
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    EXPECT_TRUE(CS::objectKeyAt(o, 0).empty());
    EXPECT_EQ(CS::objectValueAt(o, 0).kind(), Value::Kind::Null);
}

TEST(StoreObject, TwoEmptyObjectsAreDistinct) {
    Store store;
    Deferred dead;
    EXPECT_FALSE(CS::makeObject(store.keys(), 0, dead).sameAggregate(CS::makeObject(store.keys(), 0, dead)));
}

TEST(StoreObjectMutation, RepeatedKeyReplacesValue) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    CS::objectSet(o, "k", Value::number(1.0), dead);
    CS::objectSet(o, "k", Value::number(2.0), dead);
    EXPECT_EQ(CS::objectCount(o), 1u);
    EXPECT_EQ(CS::objectGet(o, "k").numberValue(), 2.0);
}

TEST(StoreObjectMutation, ReplacementCopiesNoKeyBytes) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    CS::objectSet(o, "k", Value::number(1.0), dead);
    const std::size_t before = store.bytesUsed();
    CS::objectSet(o, "k", Value::number(2.0), dead);
    EXPECT_EQ(store.bytesUsed(), before);
}

TEST(StoreObjectMutation, InsertionKeepsSortedOrder) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    const char *keys[] = {"delta", "alpha", "charlie", "bravo", "echo"};
    for (const char *key : keys) { CS::objectSet(o, key, Value::null(), dead); }

    std::string seen;
    for (std::uint32_t i = 0; i < CS::objectCount(o); ++i) {
        seen += CS::objectKeyAt(o, i);
        seen += ' ';
    }
    EXPECT_EQ(seen, "alpha bravo charlie delta echo ");
}

TEST(StoreObjectMutation, EveryKeySurvivesGrowth) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    // Тридцать ключей — рост через 4, 8, 16, 32: пары переезжают четырежды.
    for (int i = 0; i < 30; ++i) {
        CS::objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)), dead);
    }
    ASSERT_EQ(CS::objectCount(o), 30u);
    for (int i = 0; i < 30; ++i) {
        EXPECT_EQ(CS::objectGet(o, "key" + std::to_string(i)).numberValue(),
                  static_cast<double>(i));
    }
}

TEST(StoreObjectMutation, AliasSeesNewKey) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    const Value alias = o;
    for (int i = 0; i < 30; ++i) {
        CS::objectSet(o, "key" + std::to_string(i), Value::number(static_cast<double>(i)), dead);
    }
    // semantics.md §2.3: изменение через одно имя видно через второе.
    EXPECT_EQ(CS::objectCount(alias), 30u);
    EXPECT_EQ(CS::objectGet(alias, "key29").numberValue(), 29.0);
    EXPECT_TRUE(o.sameAggregate(alias));
}

TEST(StoreObjectMutation, KeyTakenFromTheSameStoreWorks) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    const Value keyValue = CS::materialize("динамический", dead);
    // The key is a string Value, not a literal spelled inline: the same shape
    // obj[k] uses.
    CS::objectSet(o, store.string(keyValue), Value::number(5.0), dead);
    EXPECT_EQ(CS::objectGet(o, "динамический").numberValue(), 5.0);
}

TEST(StoreObjectMutation, ObjectMayContainItself) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    CS::objectSet(o, "self", o, dead);
    // semantics.md §2.3: obj['self'] = obj — корректная программа.
    EXPECT_TRUE(CS::objectGet(o, "self").sameAggregate(o));
    EXPECT_EQ(CS::objectCount(o), 1u);
}

TEST(StoreObjectMutation, ObjectHoldsArrayAndArrayHoldsObject) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    const Value a = CS::makeArray(0, dead);
    CS::objectSet(o, "items", a, dead);
    CS::arrayPush(a, o);

    EXPECT_TRUE(CS::objectGet(o, "items").sameAggregate(a));
    EXPECT_TRUE(CS::arrayAt(a, 0).sameAggregate(o));
}

TEST(StoreObjectMutation, PushIntoStoredArrayIsSeenThroughTheObject) {
    Store store;
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 0, dead);
    const Value a = CS::makeArray(0, dead);
    CS::objectSet(o, "items", a, dead);

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
    Deferred dead;
    const Value o = CS::makeObject(store.keys(), 8, dead);
    const std::size_t afterReserve = store.bytesUsed();
    for (int i = 0; i < 8; ++i) {
        CS::objectSet(o, "k" + std::to_string(i), Value::null(), dead);
    }
    EXPECT_EQ(store.bytesUsed(), afterReserve);
    EXPECT_EQ(store.keys()->count(), 8u);
}

TEST(StoreObjectMutation, RepeatedKeyIsInternedOnce) {
    // Ради чего таблица и заводилась: тысяча объектов с одним именем поля
    // хранит одно имя, а не тысячу.
    Store store;
    Deferred dead;
    for (int i = 0; i < 1000; ++i) {
        const Value o = CS::makeObject(store.keys(), 1, dead);
        CS::objectSet(o, "name", Value::number(i), dead);
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
    CS::Deferred dead;
    store.setGlobal("count", Value::number(3.0), dead);
    EXPECT_TRUE(store.hasGlobal("count"));
    EXPECT_EQ(store.global("count").numberValue(), 3.0);
    EXPECT_EQ(store.globalCount(), 1u);
}

TEST(StoreGlobals, NullGlobalIsDistinctFromAbsence) {
    Store store;
    CS::Deferred dead;
    store.setGlobal("maybe", Value::null(), dead);
    // Тот же довод, что для ключей объекта (docs/semantics.md §6.2).
    EXPECT_EQ(store.global("maybe").kind(), Value::Kind::Null);
    EXPECT_TRUE(store.hasGlobal("maybe"));
}

TEST(StoreGlobals, RepeatedSetReplacesValueWithoutAddingName) {
    Store store;
    CS::Deferred dead;
    store.setGlobal("state", Value::number(1.0), dead);
    store.setGlobal("state", Value::number(2.0), dead);
    EXPECT_EQ(store.globalCount(), 1u);
    EXPECT_EQ(store.global("state").numberValue(), 2.0);
}

TEST(StoreGlobals, GlobalHoldsAggregate) {
    Store store;
    Deferred dead;
    const Value items = CS::makeArray(0, dead);
    CS::arrayPush(items, Value::number(1.0));
    store.setGlobal("items", items, dead);

    EXPECT_TRUE(store.global("items").sameAggregate(items));
    EXPECT_EQ(CS::arrayCount(store.global("items")), 1u);
}

TEST(StoreGlobals, MutationThroughGlobalIsSeenThroughTheOriginal) {
    Store store;
    Deferred dead;
    const Value items = CS::makeArray(0, dead);
    store.setGlobal("items", items, dead);
    for (int i = 0; i < 30; ++i) {
        CS::arrayPush(store.global("items"), Value::number(static_cast<double>(i)));
    }
    // docs/semantics.md §2.3: ссылочность наблюдаема и через глобальную переменную.
    EXPECT_EQ(CS::arrayCount(items), 30u);
}

TEST(StoreGlobals, EnumerationYieldsEveryName) {
    Store store;
    CS::Deferred dead;
    store.setGlobal("user", Value::null(), dead);
    store.setGlobal("state", Value::null(), dead);

    ASSERT_EQ(store.globalCount(), 2u);
    std::string seen;
    for (std::uint32_t i = 0; i < store.globalCount(); ++i) {
        seen += store.globalNameAt(i);
        seen += ' ';
    }
    // Хранение отсортировано, как у любого объекта.
    EXPECT_EQ(seen, "state user ");
}

TEST(StoreString, MaterializeMakesANodeEvenInScratch) {
    Store persistent;
    Execution exec{persistent};
    Store &scratch = exec.scratch;
    const Value v = CS::materialize("a", exec.deferred());
    EXPECT_EQ(v.region(), Value::Region::Boxed);
    scratch.clear();
    // Узел арену не заметил. Ссылку держит список отложенного освобождения
    // хранилища, поэтому отпускать её здесь не надо и нельзя.
    EXPECT_EQ(scratch.string(v), "a");
}

}  // namespace
