#include "Renderer/ComputeDispatch.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(ComputeDispatchTests, ExactMultipleDividesEvenly)
{
    EXPECT_EQ(ComputeGroupCount(128, 64), 2u);
    EXPECT_EQ(ComputeGroupCount(64, 64), 1u);
}

// The single most important case in this whole file - a naive
// `totalItems / localGroupSize` (plain truncating integer division) would
// silently yield 1 here, dropping the last 36 items entirely. Ceiling
// division must yield 2 (128 total slots dispatched, the last 28 doing
// nothing useful but every one of the 100 real items still processed).
TEST(ComputeDispatchTests, NonExactMultipleRoundsUpNeverTruncates)
{
    EXPECT_EQ(ComputeGroupCount(100, 64), 2u);
}

TEST(ComputeDispatchTests, SingleItemStillNeedsOneGroup)
{
    EXPECT_EQ(ComputeGroupCount(1, 64), 1u);
}

TEST(ComputeDispatchTests, ZeroItemsNeedsZeroGroups)
{
    EXPECT_EQ(ComputeGroupCount(0, 64), 0u);
}

TEST(ComputeDispatchTests, LocalGroupSizeOfOneMatchesItemCountExactly)
{
    EXPECT_EQ(ComputeGroupCount(37, 1), 37u);
}

TEST(ComputeDispatchTests, LargeItemCountDoesNotOverflowForReasonableGroupSizes)
{
    // 1,000,000 items at a group size of 256 -> ceil(1000000 / 256) = 3907
    // (3907 * 256 = 1,000,192 >= 1,000,000; 3906 * 256 = 999,936 < 1,000,000).
    EXPECT_EQ(ComputeGroupCount(1'000'000, 256), 3907u);
}

TEST(ComputeDispatchTests, GroupCount3DAppliesPerAxisIndependently)
{
    // A 130x70x1 image dispatched with an 8x8x1 local size:
    //   width:  ceil(130/8) = 17
    //   height: ceil(70/8)  = 9
    //   depth:  ceil(1/1)   = 1
    const Extent3D totalItems{ 130, 70, 1 };
    const Extent3D localGroupSize{ 8, 8, 1 };
    const Extent3D groupCount = ComputeGroupCount3D(totalItems, localGroupSize);
    EXPECT_EQ(groupCount.width, 17u);
    EXPECT_EQ(groupCount.height, 9u);
    EXPECT_EQ(groupCount.depth, 1u);
}

TEST(ComputeDispatchTests, GroupCount3DExactMultipleOnEveryAxis)
{
    const Extent3D totalItems{ 64, 32, 4 };
    const Extent3D localGroupSize{ 16, 16, 4 };
    const Extent3D groupCount = ComputeGroupCount3D(totalItems, localGroupSize);
    EXPECT_EQ(groupCount.width, 4u);
    EXPECT_EQ(groupCount.height, 2u);
    EXPECT_EQ(groupCount.depth, 1u);
}

} // namespace
} // namespace gte
