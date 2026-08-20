#include "value.hpp"

#include <gtest/gtest.h>

#include <cstring>
#include <new>
#include <string>

#include "aggregate.hpp"
#include "box.hpp"

namespace {

using CS::Value;

TEST(ValueLayout, StaysSixteenBytesAndTriviallyCopyable) {
    static_assert(sizeof(Value) == 16, "Value must stay sixteen bytes");
    static_assert(std::is_trivially_copyable_v<Value>, "");
    static_assert(alignof(Value) == 8, "the double in the payload sets this");
}

TEST(ValueLayout, HoldsFifteenBytesInline) {
    const std::string bytes(Value::kInlineCapacity, 'x');
    const Value v = Value::inlineString(bytes);
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_TRUE(v.isInlineString());
    EXPECT_EQ(CS::stringBytes(v), bytes);
}

TEST(ValueLayout, EmptyStringIsInline) {
    const Value v = Value::inlineString("");
    EXPECT_TRUE(v.isInlineString());
    EXPECT_TRUE(CS::stringBytes(v).empty());
}

TEST(ValueLayout, InlineStringKeepsEmbeddedNul) {
    const std::string bytes("a\0b", 3);
    const Value v = Value::inlineString(bytes);
    EXPECT_EQ(CS::stringBytes(v).size(), 3u);
    EXPECT_EQ(CS::stringBytes(v)[1], '\0');
}

/// The bytes past the length are zero. Not decoration: it is what makes
/// comparing two inline strings a comparison of the sixteen bytes, without
/// consulting the length and without memcmp over a variable range.
TEST(ValueLayout, PadsWithZeroesSoEqualStringsHaveEqualBytes) {
    // Fix round 1 (review): the previous version built both operands from
    // std::string_view(ptr, 2), which truncates to "ab" before inlineString
    // ever sees the differing tails — memcpy only ever touches the first 2
    // bytes, so a factory that stopped zeroing the padding would still pass.
    // Poison the storage before construction instead, so leftover pad bytes
    // show up as poison rather than by-chance zero from stack reuse, and
    // check them directly against an explicit zero reference.
    alignas(Value) unsigned char storage[sizeof(Value)];
    std::memset(storage, 0xCD, sizeof(storage));
    new (storage) Value(Value::inlineString("ab"));
    const Value &v = *reinterpret_cast<const Value *>(storage);

    unsigned char raw[sizeof(Value)];
    std::memcpy(raw, &v, sizeof(Value));

    // Byte 0 is the tag, bytes 1-2 hold "ab"; everything from byte 3 onward
    // is padding and must be zero. This is what a factory that skipped
    // value-initializing wide_ would get wrong, and poisoned storage makes
    // that failure visible instead of hidden by coincidental zero reuse.
    const unsigned char zero[sizeof(Value) - 3] = {};
    EXPECT_EQ(std::memcmp(raw + 3, zero, sizeof(zero)), 0);
    EXPECT_EQ(CS::stringBytes(v), "ab");

    // The claim the test's name makes: two inline strings built
    // independently from the same content compare equal across the full
    // 16 bytes, not just in the bytes stringBytes reads.
    const Value other = Value::inlineString(std::string_view("ab", 2));
    EXPECT_EQ(std::memcmp(&v, &other, sizeof(Value)), 0);
}

/// Sixteen bytes is one too many: the box path starts here.
TEST(ValueLayout, SixteenBytesGoesToABox) {
    CS::Deferred dead;
    const std::string bytes(Value::kInlineCapacity + 1, 'x');
    const Value v = CS::materialize(bytes, dead);
    EXPECT_EQ(v.kind(), Value::Kind::String);
    EXPECT_FALSE(v.isInlineString());
    EXPECT_EQ(CS::stringBytes(v), bytes);
}

/// A short string costs no box at all — the point of the whole change.
TEST(ValueLayout, ShortStringAllocatesNothing) {
#ifndef NDEBUG
    CS::Deferred dead;
    const std::size_t before = CS::detail::liveBoxCount();
    const Value v = CS::materialize("center", dead);
    EXPECT_EQ(CS::detail::liveBoxCount(), before);
    EXPECT_EQ(CS::stringBytes(v), "center");
#endif
}

}  // namespace
