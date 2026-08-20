#include "box.hpp"

#include <gtest/gtest.h>

#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>
#include <thread>

#include "keytable.hpp"
#include "store.hpp"
#include "aggregate.hpp"
#include "context.hpp"

namespace {

using CS::KeyTable;
using CS::Value;
using CS::detail::ArrayBox;
using CS::detail::ObjectBox;
using CS::detail::StringBox;

TEST(Box, StringNodeKeepsBytes) {
    StringBox *s = CS::detail::makeStringBox("привет");
    EXPECT_EQ(s->view(), "привет");
    EXPECT_EQ(s->len, 12u);
    CS::detail::release(s);
}

TEST(Box, StringNodeKeepsEmbeddedNul) {
    const std::string bytes("a\0b", 3);
    StringBox *s = CS::detail::makeStringBox(bytes);
    EXPECT_EQ(s->view().size(), 3u);
    EXPECT_EQ(s->view()[1], '\0');
    CS::detail::release(s);
}

TEST(Box, EmptyStringNodeHasEmptyView) {
    StringBox *s = CS::detail::makeStringBox("");
    EXPECT_TRUE(s->view().empty());
    CS::detail::release(s);
}

TEST(Box, ArrayBoxStartsEmpty) {
    ArrayBox *a = CS::detail::makeArrayBox(4, 1);
    EXPECT_EQ(a->size(), 0u);
    EXPECT_GE(a->cap, 4u);
    CS::detail::release(a);
}

#ifndef NDEBUG
/// A literal aggregate costs one allocation, not two: the elements live in the
/// tail right behind the header, the way StringBox's bytes already do.
TEST(Box, LiteralAggregateIsOneAllocation) {
    ArrayBox *a = CS::detail::makeArrayBox(3, 1);
    const char *header = reinterpret_cast<const char *>(a);
    const char *elements = reinterpret_cast<const char *>(a->data);
    EXPECT_EQ(elements, header + sizeof(ArrayBox));
    CS::detail::release(a);
}

/// Outgrowing the tail moves the ELEMENTS, never the box: every Value pointing
/// at this box keeps pointing at it. Growing the box itself would invalidate
/// all of them at once.
TEST(Box, BoxAddressSurvivesOutgrowingTheTail) {
    ArrayBox *a = CS::detail::makeArrayBox(1, 1);
    const ArrayBox *before = a;
    a->push(Value::number(1));
    a->push(Value::number(2));
    a->push(Value::number(3));
    EXPECT_EQ(a, before);
    EXPECT_EQ(a->size(), 3u);
    EXPECT_EQ(a->at(2).numberValue(), 3.0);
    EXPECT_NE(reinterpret_cast<const char *>(a->data),
              reinterpret_cast<const char *>(a) + sizeof(ArrayBox));
    CS::detail::release(a);
}
#endif

// Assertion about reference counting kept unconditional: this branch's whole
// subject is the refcount, and it must run in a release build too, not only
// when liveBoxCount() (a debug-only counter) is available to check alongside
// it.
TEST(Box, ReleaseOfArrayReleasesElement) {
    StringBox *s = CS::detail::makeStringBox("x");
    ArrayBox *a = CS::detail::makeArrayBox(1, 1);
    a->push(Value::string(s));
    CS::detail::retain(s);   // ссылка ячейки массива
    CS::detail::release(s);  // ссылка создателя ушла, держит массив
    EXPECT_EQ(s->rc, 1u);
    CS::detail::release(a);  // массив отпускает элемент вместе с собой
}

#ifndef NDEBUG
TEST(Box, ReleaseOfArrayReleasesElementsLiveCount) {
    const std::size_t before = CS::detail::liveBoxCount();
    StringBox *s = CS::detail::makeStringBox("x");
    ArrayBox *a = CS::detail::makeArrayBox(1, 1);
    a->push(Value::string(s));
    CS::detail::retain(s);   // ссылка ячейки массива
    CS::detail::release(s);  // ссылка создателя ушла, держит массив
    EXPECT_EQ(CS::detail::liveBoxCount(), before + 2);
    CS::detail::release(a);  // массив отпускает элемент вместе с собой
    EXPECT_EQ(CS::detail::liveBoxCount(), before);
}
#endif

TEST(Box, ObjectBoxHoldsKeyTable) {
    KeyTable *t = KeyTable::create();
    ObjectBox *o = CS::detail::makeObjectBox(t, 2, 1);
    KeyTable::release(t);  // ссылка создателя ушла, держит объект
    EXPECT_EQ(o->keys->intern("name"), 0u);
    CS::detail::release(o);
}

#ifndef NDEBUG
TEST(Box, ReleaseOfObjectReleasesValues) {
    const std::size_t before = CS::detail::liveBoxCount();
    KeyTable *t = KeyTable::create();
    ObjectBox *o = CS::detail::makeObjectBox(t, 1, 1);
    KeyTable::release(t);
    ArrayBox *a = CS::detail::makeArrayBox(0, 1);
    o->insert(0, CS::detail::Entry{o->keys->intern("rows"),
                                   CS::detail::keyPrefix("rows"),
                                   Value::array(a)});
    CS::detail::retain(a);
    CS::detail::release(a);
    EXPECT_EQ(CS::detail::liveBoxCount(), before + 2);
    CS::detail::release(o);
    EXPECT_EQ(CS::detail::liveBoxCount(), before);
}
#endif

#ifndef NDEBUG
TEST(Box, LiveCountReturnsToWhereItStarted) {
    const std::size_t before = CS::detail::liveBoxCount();
    ArrayBox *a = CS::detail::makeArrayBox(0, 1);
    EXPECT_EQ(CS::detail::liveBoxCount(), before + 1);
    CS::detail::release(a);
    EXPECT_EQ(CS::detail::liveBoxCount(), before);
}
#endif

TEST(Box, RetainKeepsNodeAlivePastFirstRelease) {
    ArrayBox *a = CS::detail::makeArrayBox(0, 1);
    CS::detail::retain(a);
    CS::detail::release(a);
    EXPECT_EQ(a->rc, 1u);
    CS::detail::release(a);
}

#ifndef NDEBUG
/// Two Contexts on two threads: the threading contract allows this, and the
/// live-box counter is the one piece of box state that is process-wide rather
/// than per-Context. Without atomicity the two increments race and the final
/// count comes out lower than the number of boxes actually created.
TEST(Box, LiveCountSurvivesTwoContextsOnTwoThreads) {
    constexpr int kPerThread = 2000;
    const std::size_t before = CS::detail::liveBoxCount();

    // Each thread owns its own Context, and each write goes through
    // setGlobalString — the boxed path (materialize sees a string longer
    // than Value::kInlineCapacity) — so the churn exercises two Stores'
    // worth of box creation and release concurrently, the contract
    // chupascript.h states for two threads each with their own Context.
    auto churn = [] {
        CS::Context ctx;
        for (int i = 0; i < kPerThread; ++i) {
            ctx.setGlobalString("s", "payload longer than fifteen bytes");
        }
    };

    std::thread a(churn);
    std::thread b(churn);
    a.join();
    b.join();

    EXPECT_EQ(CS::detail::liveBoxCount(), before);
}
#endif

// См. box.cpp: ArrayBox/ObjectBox формально не standard-layout (Box-база и
// сама коробка обе несут поля), поэтому offsetof здесь честно предупреждает
// -Winvalid-offsetof, хоть раскладка и корректна для обоих компиляторов.
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic push
#  pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
TEST(BoxEpoch, BothAggregatesKeepTheEpochAtTheSameOffset) {
    // Одинаковое смещение — не совпадение, а требование: epochAddressOf
    // отвечает одной строкой на оба вида, и от вида коробки ответ не зависит.
    EXPECT_EQ(offsetof(CS::detail::ArrayBox, epoch),
              offsetof(CS::detail::ObjectBox, epoch));
}
#if defined(__GNUC__) || defined(__clang__)
#  pragma GCC diagnostic pop
#endif

TEST(BoxEpoch, ANewBoxTakesItsNumberFromTheClock) {
    CS::Store store;
    CS::Deferred dead;
    const CS::Epoch before = store.clock().now();

    const Value a = CS::makeArray(0, store.clock(), dead);

    EXPECT_GT(*CS::epochAddressOf(a), before);
}

TEST(BoxEpoch, ABoxBornLaterCarriesAStrictlyGreaterNumber) {
    // Это и закрывает переиспользование адреса (спека §4.4): коробка, севшая
    // на адрес умершей, приносит номер больше всего, что читатель мог видеть.
    CS::Store store;
    CS::Deferred dead;
    const Value first = CS::makeArray(0, store.clock(), dead);
    const Value second = CS::makeObject(store.keys(), 0, store.clock(), dead);

    EXPECT_GT(*CS::epochAddressOf(second), *CS::epochAddressOf(first));
}

TEST(BoxEpoch, ADeadBoxAddressReusedCannotLookUnchanged) {
    CS::Store store;
    CS::Deferred dead;
    CS::Epoch seen = 0;
    const void *address = nullptr;
    {
        CS::Deferred scoped;
        const Value doomed = CS::makeArray(0, store.clock(), scoped);
        seen = *CS::epochAddressOf(doomed);
        address = doomed.box();
    }  // scoped слит — коробка освобождена

    const Value fresh = CS::makeArray(0, store.clock(), dead);
    if (fresh.box() == address) {
        EXPECT_GT(*CS::epochAddressOf(fresh), seen);
    } else {
        GTEST_SKIP() << "аллокатор отдал другой адрес — проверять нечего";
    }
}

}  // namespace

namespace {

/// Объект с этими ключами, собранный через хранилище, — чтобы порядок пар
/// заводил сам objectSet, а не тест.
/// dead принадлежит вызывающему намеренно: единственная ссылка на новый объект
/// — ссылка создателя, и слить список здесь значило бы убить его на возврате.
CS::Value objectWith(CS::Store &store, CS::Deferred &dead,
                     std::initializer_list<std::string_view> keys) {
    CS::Value o = CS::makeObject(store.keys(), static_cast<std::uint32_t>(keys.size()), store.clock(), dead);
    double n = 0.0;
    for (std::string_view key : keys) { CS::objectSet(o, key, CS::Value::number(n++), dead); }
    return o;
}

}  // namespace

TEST(KeyPrefix, OrdersLikeTheBytesItPacks) {
    // На этом свойстве стоит быстрый путь findEntry: если префиксы различны,
    // проба решается ими одними, и таблица имён не читается. Свойство обязано
    // держаться и там, где имя короче четырёх байт, — там префикс добит
    // нулями, а более короткое имя обязано остаться меньшим.
    const std::string_view ordered[] = {"", "a", "ab", "abc", "abcd", "abce",
                                        "b", "id", "label", "name", "price"};
    for (std::size_t i = 0; i + 1 < std::size(ordered); ++i) {
        const std::string_view lo = ordered[i];
        const std::string_view hi = ordered[i + 1];
        ASSERT_LT(lo, hi) << "сам набор перестал быть упорядоченным";
        const std::uint32_t a = CS::detail::keyPrefix(lo);
        const std::uint32_t b = CS::detail::keyPrefix(hi);
        // Либо префикс уже решает, либо он совпал и решать будут байты. Чего
        // быть не должно — это префикса, решающего НАОБОРОТ.
        EXPECT_LE(a, b) << lo << " / " << hi;
    }
}

TEST(KeyPrefix, EqualPrefixesFallThroughToTheBytes) {
    CS::Deferred dead;
    // Имена, совпадающие в первых четырёх байтах, префиксом не различаются
    // вовсе — и поиск обязан находить каждое.
    CS::Store store;
    const CS::Value o = objectWith(store, dead, {"labelA", "labelB", "label"});
    EXPECT_EQ(CS::detail::keyPrefix("labelA"), CS::detail::keyPrefix("labelB"));
    EXPECT_DOUBLE_EQ(CS::objectGet(o, "labelA").numberValue(), 0.0);
    EXPECT_DOUBLE_EQ(CS::objectGet(o, "labelB").numberValue(), 1.0);
    EXPECT_DOUBLE_EQ(CS::objectGet(o, "label").numberValue(), 2.0);
    EXPECT_EQ(CS::objectGet(o, "labelC").kind(), CS::Value::Kind::Null);
}

TEST(KeyPrefix, ShortKeysStayDistinct) {
    CS::Deferred dead;
    // Пустой ключ, однобайтовый и трёхбайтовый живут в одном объекте: у всех
    // префикс добит нулями, и спутать их нельзя.
    CS::Store store;
    const CS::Value o = objectWith(store, dead, {"", "a", "abc", "abcd"});
    EXPECT_EQ(CS::objectCount(o), 4u);
    EXPECT_DOUBLE_EQ(CS::objectGet(o, "").numberValue(), 0.0);
    EXPECT_DOUBLE_EQ(CS::objectGet(o, "a").numberValue(), 1.0);
    EXPECT_DOUBLE_EQ(CS::objectGet(o, "abc").numberValue(), 2.0);
    EXPECT_DOUBLE_EQ(CS::objectGet(o, "abcd").numberValue(), 3.0);
    EXPECT_FALSE(CS::objectHas(o, "ab"));
}

TEST(KeyPrefix, EntryStaysTwentyFourBytes) {
    // Префикс лёг в набивку, которая была и так. Вырасти запись не вправе: её
    // размер — это память всякого объекта в системе.
    EXPECT_EQ(sizeof(CS::detail::Entry), 24u);
}
