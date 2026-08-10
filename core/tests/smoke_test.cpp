// Placeholder test: proves the test harness is wired end to end.
#include <gtest/gtest.h>

#include "chupascript/chupascript.h"

TEST(Smoke, VersionIsReported) {
    EXPECT_STREQ("0.1.0", chupascript_version());
}
