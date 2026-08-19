#include "box.hpp"

#include <gtest/gtest.h>

#include <initializer_list>
#include <iterator>
#include <string>
#include <string_view>

#include "keytable.hpp"
#include "store.hpp"
#include "aggregate.hpp"

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
    ArrayBox *a = CS::detail::makeArrayBox(4);
    EXPECT_TRUE(a->items.empty());
    EXPECT_GE(a->items.capacity(), 4u);
    CS::detail::release(a);
}

TEST(Box, ReleaseOfArrayReleasesElements) {
    const std::size_t before = CS::detail::liveBoxCount();
    StringBox *s = CS::detail::makeStringBox("x");
    ArrayBox *a = CS::detail::makeArrayBox(1);
    a->items.push_back(Value::string(s, s->len));
    CS::detail::retain(s);   // ссылка ячейки массива
    CS::detail::release(s);  // ссылка создателя ушла, держит массив
    EXPECT_EQ(s->rc, 1u);
    EXPECT_EQ(CS::detail::liveBoxCount(), before + 2);
    CS::detail::release(a);  // массив отпускает элемент вместе с собой
    EXPECT_EQ(CS::detail::liveBoxCount(), before);
}

TEST(Box, ObjectBoxHoldsKeyTable) {
    KeyTable *t = KeyTable::create();
    ObjectBox *o = CS::detail::makeObjectBox(t, 2);
    KeyTable::release(t);  // ссылка создателя ушла, держит объект
    EXPECT_EQ(o->keys->intern("name"), 0u);
    CS::detail::release(o);
}

TEST(Box, ReleaseOfObjectReleasesValues) {
    const std::size_t before = CS::detail::liveBoxCount();
    KeyTable *t = KeyTable::create();
    ObjectBox *o = CS::detail::makeObjectBox(t, 1);
    KeyTable::release(t);
    ArrayBox *a = CS::detail::makeArrayBox(0);
    o->entries.push_back(CS::detail::Entry{o->keys->intern("rows"),
                                                CS::detail::keyPrefix("rows"),
                                                Value::array(a)});
    CS::detail::retain(a);
    CS::detail::release(a);
    EXPECT_EQ(CS::detail::liveBoxCount(), before + 2);
    CS::detail::release(o);
    EXPECT_EQ(CS::detail::liveBoxCount(), before);
}

TEST(Box, LiveCountReturnsToWhereItStarted) {
    const std::size_t before = CS::detail::liveBoxCount();
    ArrayBox *a = CS::detail::makeArrayBox(0);
    EXPECT_EQ(CS::detail::liveBoxCount(), before + 1);
    CS::detail::release(a);
    EXPECT_EQ(CS::detail::liveBoxCount(), before);
}

TEST(Box, RetainKeepsNodeAlivePastFirstRelease) {
    ArrayBox *a = CS::detail::makeArrayBox(0);
    CS::detail::retain(a);
    CS::detail::release(a);
    EXPECT_EQ(a->rc, 1u);
    CS::detail::release(a);
}

}  // namespace

namespace {

/// Объект с этими ключами, собранный через хранилище, — чтобы порядок пар
/// заводил сам objectSet, а не тест.
CS::Value objectWith(CS::Store &store, std::initializer_list<std::string_view> keys) {
    CS::Value o = CS::makeObject(store.keys(), static_cast<std::uint32_t>(keys.size()), store.deferred());
    double n = 0.0;
    for (std::string_view key : keys) { CS::objectSet(o, key, CS::Value::number(n++), store.deferred()); }
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
    // Имена, совпадающие в первых четырёх байтах, префиксом не различаются
    // вовсе — и поиск обязан находить каждое.
    CS::Store store;
    const CS::Value o = objectWith(store, {"labelA", "labelB", "label"});
    EXPECT_EQ(CS::detail::keyPrefix("labelA"), CS::detail::keyPrefix("labelB"));
    EXPECT_DOUBLE_EQ(CS::objectGet(o, "labelA").numberValue(), 0.0);
    EXPECT_DOUBLE_EQ(CS::objectGet(o, "labelB").numberValue(), 1.0);
    EXPECT_DOUBLE_EQ(CS::objectGet(o, "label").numberValue(), 2.0);
    EXPECT_EQ(CS::objectGet(o, "labelC").kind(), CS::Value::Kind::Null);
}

TEST(KeyPrefix, ShortKeysStayDistinct) {
    // Пустой ключ, однобайтовый и трёхбайтовый живут в одном объекте: у всех
    // префикс добит нулями, и спутать их нельзя.
    CS::Store store;
    const CS::Value o = objectWith(store, {"", "a", "abc", "abcd"});
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
