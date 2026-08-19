#include "value.hpp"

#include <gtest/gtest.h>

#include <cstring>
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
    // Built from two different, longer sources, so anything the factory failed
    // to zero would differ between them.
    const Value a = Value::inlineString(std::string_view("ab_leftover_one", 2));
    const Value b = Value::inlineString(std::string_view("ab?????????????", 2));
    EXPECT_EQ(std::memcmp(&a, &b, sizeof(Value)), 0);
    EXPECT_EQ(CS::stringBytes(a), "ab");
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
