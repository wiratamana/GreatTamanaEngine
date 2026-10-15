// Tier-1 tests for BoundedOnceSet: report-once keys with a hard memory cap.

#include "Renderer/RenderGraph/BoundedOnceSet.h"

#include <gtest/gtest.h>

namespace gte::rg {
namespace {

using Set = BoundedOnceSet<3>;

TEST(BoundedOnceSetTest, FirstThenRepeat)
{
    Set set;
    EXPECT_EQ(set.Note("a"), Set::Result::First);
    EXPECT_EQ(set.Note("a"), Set::Result::Repeat);
}

TEST(BoundedOnceSetTest, DistinctKeysUpToCapacityAreAllFirst)
{
    Set set;
    EXPECT_EQ(set.Note("a"), Set::Result::First);
    EXPECT_EQ(set.Note("b"), Set::Result::First);
    EXPECT_EQ(set.Note("c"), Set::Result::First);
}

TEST(BoundedOnceSetTest, OverflowIsReportedOnceAndNeverRemembersNewKeys)
{
    Set set;
    set.Note("a");
    set.Note("b");
    set.Note("c");
    EXPECT_EQ(set.Note("d"), Set::Result::Overflow);
    EXPECT_EQ(set.Note("d"), Set::Result::OverflowRepeat);
    EXPECT_EQ(set.Note("e"), Set::Result::OverflowRepeat);
}

TEST(BoundedOnceSetTest, KnownKeyStillRepeatsAfterOverflow)
{
    Set set;
    set.Note("a");
    set.Note("b");
    set.Note("c");
    set.Note("d");
    EXPECT_EQ(set.Note("a"), Set::Result::Repeat);
}

TEST(BoundedOnceSetTest, OnlyFirstAndOverflowDeserveALogLine)
{
    EXPECT_TRUE(Set::ShouldLog(Set::Result::First));
    EXPECT_TRUE(Set::ShouldLog(Set::Result::Overflow));
    EXPECT_FALSE(Set::ShouldLog(Set::Result::Repeat));
    EXPECT_FALSE(Set::ShouldLog(Set::Result::OverflowRepeat));
}

} // namespace
} // namespace gte::rg
