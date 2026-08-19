#include "keytable.hpp"

#include <gtest/gtest.h>

#include <string>

namespace {

using CS::KeyTable;

TEST(KeyTable, InternReturnsSameIdForSameKey) {
    KeyTable *t = KeyTable::create();
    const std::uint32_t a = t->intern("name");
    const std::uint32_t b = t->intern("name");
    EXPECT_EQ(a, b);
    EXPECT_EQ(t->count(), 1u);
    KeyTable::release(t);
}

TEST(KeyTable, InternReturnsDifferentIdsForDifferentKeys) {
    KeyTable *t = KeyTable::create();
    EXPECT_NE(t->intern("name"), t->intern("id"));
    EXPECT_EQ(t->count(), 2u);
    KeyTable::release(t);
}

TEST(KeyTable, BytesRoundTrip) {
    KeyTable *t = KeyTable::create();
    const std::uint32_t id = t->intern("привет");
    EXPECT_EQ(t->bytes(id), "привет");
    KeyTable::release(t);
}

TEST(KeyTable, FindDoesNotIntern) {
    KeyTable *t = KeyTable::create();
    EXPECT_EQ(t->find("нет"), CS::kNoKey);
    EXPECT_EQ(t->count(), 0u);
    KeyTable::release(t);
}

TEST(KeyTable, KeepsEmbeddedNulByte) {
    KeyTable *t = KeyTable::create();
    const std::string key("a\0b", 3);
    const std::uint32_t id = t->intern(key);
    EXPECT_EQ(t->bytes(id).size(), 3u);
    EXPECT_EQ(t->find(key), id);
    KeyTable::release(t);
}

TEST(KeyTable, EmptyKeyIsAKey) {
    KeyTable *t = KeyTable::create();
    const std::uint32_t id = t->intern("");
    EXPECT_NE(id, CS::kNoKey);
    EXPECT_TRUE(t->bytes(id).empty());
    EXPECT_EQ(t->find(""), id);
    KeyTable::release(t);
}

TEST(KeyTable, RetainKeepsTableAlivePastFirstRelease) {
    KeyTable *t = KeyTable::create();
    const std::uint32_t id = t->intern("name");
    KeyTable::retain(t);
    KeyTable::release(t);
    // Вторая ссылка ещё держит: читать можно.
    EXPECT_EQ(t->bytes(id), "name");
    KeyTable::release(t);
}

TEST(KeyTable, ManyKeysKeepTheirBytesAfterArenaGrowth) {
    // Арена имён переезжает по мере роста; номера её переезд переживают.
    KeyTable *t = KeyTable::create();
    std::vector<std::uint32_t> ids;
    for (int i = 0; i < 500; ++i) {
        ids.push_back(t->intern("field_" + std::to_string(i)));
    }
    for (int i = 0; i < 500; ++i) {
        EXPECT_EQ(t->bytes(ids[i]), "field_" + std::to_string(i));
    }
    KeyTable::release(t);
}

}  // namespace
