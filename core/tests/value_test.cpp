#include "value.hpp"

#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <type_traits>

namespace {

using CS::Value;

TEST(ValueLayout, SizeIsSixteenBytes) {
    EXPECT_EQ(sizeof(Value), 16u);
}

TEST(ValueLayout, IsTriviallyCopyable) {
    EXPECT_TRUE(std::is_trivially_copyable_v<Value>);
}

TEST(ValueScalars, NullHasNullKind) {
    EXPECT_EQ(Value::null().kind(), Value::Kind::Null);
}

TEST(ValueScalars, BooleanRoundTrips) {
    EXPECT_EQ(Value::boolean(true).kind(), Value::Kind::Boolean);
    EXPECT_TRUE(Value::boolean(true).booleanValue());
    EXPECT_FALSE(Value::boolean(false).booleanValue());
}

TEST(ValueScalars, NumberRoundTrips) {
    EXPECT_EQ(Value::number(1.5).kind(), Value::Kind::Number);
    EXPECT_EQ(Value::number(1.5).numberValue(), 1.5);
    EXPECT_EQ(Value::number(-0.0).numberValue(), -0.0);
}

TEST(ValueScalars, NumberKeepsSpecialValues) {
    const double inf = std::numeric_limits<double>::infinity();
    EXPECT_EQ(Value::number(inf).numberValue(), inf);
    EXPECT_TRUE(std::isnan(Value::number(std::numeric_limits<double>::quiet_NaN()).numberValue()));
}

TEST(ValueIdentity, ScalarsAreNeverSameAggregate) {
    EXPECT_FALSE(Value::null().sameAggregate(Value::null()));
    EXPECT_FALSE(Value::number(1.0).sameAggregate(Value::number(1.0)));
    EXPECT_FALSE(Value::boolean(true).sameAggregate(Value::boolean(true)));
}

TEST(ValueIdentity, DifferentKindsAreNotSame) {
    EXPECT_FALSE(Value::null().sameAggregate(Value::number(0.0)));
}

}  // namespace
