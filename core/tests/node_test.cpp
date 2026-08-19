#include "node.hpp"

#include <gtest/gtest.h>

#include <string>

#include "keytable.hpp"

namespace {

using CS::KeyTable;
using CS::Value;
using CS::detail::ArrayNode;
using CS::detail::ObjectNode;
using CS::detail::StrNode;

TEST(Node, StringNodeKeepsBytes) {
    StrNode *s = CS::detail::makeStrNode("привет");
    EXPECT_EQ(s->view(), "привет");
    EXPECT_EQ(s->len, 12u);
    CS::detail::release(s);
}

TEST(Node, StringNodeKeepsEmbeddedNul) {
    const std::string bytes("a\0b", 3);
    StrNode *s = CS::detail::makeStrNode(bytes);
    EXPECT_EQ(s->view().size(), 3u);
    EXPECT_EQ(s->view()[1], '\0');
    CS::detail::release(s);
}

TEST(Node, EmptyStringNodeHasEmptyView) {
    StrNode *s = CS::detail::makeStrNode("");
    EXPECT_TRUE(s->view().empty());
    CS::detail::release(s);
}

TEST(Node, ArrayNodeStartsEmpty) {
    ArrayNode *a = CS::detail::makeArrayNode(4);
    EXPECT_TRUE(a->items.empty());
    EXPECT_GE(a->items.capacity(), 4u);
    CS::detail::release(a);
}

TEST(Node, ReleaseOfArrayReleasesElements) {
    const std::size_t before = CS::detail::liveNodeCount();
    StrNode *s = CS::detail::makeStrNode("x");
    ArrayNode *a = CS::detail::makeArrayNode(1);
    a->items.push_back(Value::string(s, s->len));
    CS::detail::retain(s);   // ссылка ячейки массива
    CS::detail::release(s);  // ссылка создателя ушла, держит массив
    EXPECT_EQ(s->rc, 1u);
    EXPECT_EQ(CS::detail::liveNodeCount(), before + 2);
    CS::detail::release(a);  // массив отпускает элемент вместе с собой
    EXPECT_EQ(CS::detail::liveNodeCount(), before);
}

TEST(Node, ObjectNodeHoldsKeyTable) {
    KeyTable *t = KeyTable::create();
    ObjectNode *o = CS::detail::makeObjectNode(t, 2);
    KeyTable::release(t);  // ссылка создателя ушла, держит объект
    EXPECT_EQ(o->keys->intern("name"), 0u);
    CS::detail::release(o);
}

TEST(Node, ReleaseOfObjectReleasesValues) {
    const std::size_t before = CS::detail::liveNodeCount();
    KeyTable *t = KeyTable::create();
    ObjectNode *o = CS::detail::makeObjectNode(t, 1);
    KeyTable::release(t);
    ArrayNode *a = CS::detail::makeArrayNode(0);
    o->entries.push_back(CS::detail::Entry{o->keys->intern("rows"), Value::array(a)});
    CS::detail::retain(a);
    CS::detail::release(a);
    EXPECT_EQ(CS::detail::liveNodeCount(), before + 2);
    CS::detail::release(o);
    EXPECT_EQ(CS::detail::liveNodeCount(), before);
}

TEST(Node, LiveCountReturnsToWhereItStarted) {
    const std::size_t before = CS::detail::liveNodeCount();
    ArrayNode *a = CS::detail::makeArrayNode(0);
    EXPECT_EQ(CS::detail::liveNodeCount(), before + 1);
    CS::detail::release(a);
    EXPECT_EQ(CS::detail::liveNodeCount(), before);
}

TEST(Node, RetainKeepsNodeAlivePastFirstRelease) {
    ArrayNode *a = CS::detail::makeArrayNode(0);
    CS::detail::retain(a);
    CS::detail::release(a);
    EXPECT_EQ(a->rc, 1u);
    CS::detail::release(a);
}

}  // namespace
