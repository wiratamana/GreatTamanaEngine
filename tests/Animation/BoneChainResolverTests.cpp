#include "Animation/BoneChainResolver.h"

#include <gtest/gtest.h>

using namespace gte;

namespace {

// A straight parent chain: bone i's parent is bone (i - 1), bone 0 is root.
// Payload type (int/Bone flags) doesn't matter to BoneChainResolver.h at
// all - only SkeletonData::bones' own size and Bone::parentBoneIndex are
// used to shape the chain for these tests.
SkeletonData BuildStraightChain(std::size_t count)
{
    SkeletonData skeleton;
    for (std::size_t i = 0; i < count; ++i) {
        Bone bone;
        bone.parentBoneIndex = (i == 0) ? -1 : static_cast<std::int32_t>(i - 1);
        skeleton.bones.push_back(bone);
    }
    return skeleton;
}

} // namespace

TEST(BoneChainResolverTests, ResolveBoneChainAccumulatesAlongParentChain)
{
    const SkeletonData skeleton = BuildStraightChain(4); // 0 <- 1 <- 2 <- 3
    const std::vector<int> values = ResolveBoneChain<int>(
        skeleton, 0, [&](std::size_t index) -> std::int32_t { return skeleton.bones[index].parentBoneIndex; },
        [&](std::size_t /*index*/, int parentValue) -> int { return parentValue + 1; });

    ASSERT_EQ(values.size(), 4u);
    EXPECT_EQ(values[0], 1);
    EXPECT_EQ(values[1], 2);
    EXPECT_EQ(values[2], 3);
    EXPECT_EQ(values[3], 4);
}

TEST(BoneChainResolverTests, ResolveBoneChainResolvesEachNodeExactlyOnce)
{
    // Memoization is the whole point of this flavor (vs.
    // ResolveSingleBoneChain() below) - a node with several descendants
    // must still only be computed once, not once per descendant that
    // depends on it.
    const SkeletonData skeleton = BuildStraightChain(3);
    int callCount = 0;
    const std::vector<int> values = ResolveBoneChain<int>(
        skeleton, 0, [&](std::size_t index) -> std::int32_t { return skeleton.bones[index].parentBoneIndex; },
        [&](std::size_t /*index*/, int parentValue) -> int {
            ++callCount;
            return parentValue + 1;
        });

    EXPECT_EQ(callCount, 3);
    EXPECT_EQ(values[2], 3);
}

TEST(BoneChainResolverTests, ResolveBoneChainCyclicChainTerminatesWithRootValueFallback)
{
    // Malformed data (a chain with a cycle) must never hang/recurse forever
    // - this test finishing at all is half the assertion, same convention
    // as SkeletonPoseTests::CyclicParentChainTerminatesAndProducesFiniteMatrices.
    SkeletonData skeleton;
    Bone a;
    a.parentBoneIndex = 1;
    skeleton.bones.push_back(a); // 0
    Bone b;
    b.parentBoneIndex = 0;
    skeleton.bones.push_back(b); // 1 - points back at 0, a cycle.

    const std::vector<int> values = ResolveBoneChain<int>(
        skeleton, -100, [&](std::size_t index) -> std::int32_t { return skeleton.bones[index].parentBoneIndex; },
        [&](std::size_t /*index*/, int parentValue) -> int { return parentValue + 1; });

    ASSERT_EQ(values.size(), 2u);
    // Whichever node's recursion closed the cycle got rootValue (-100) as
    // its own predecessor instead of recursing forever.
    EXPECT_TRUE(values[0] == -99 || values[1] == -99);
}

TEST(BoneChainResolverTests, ResolveSingleBoneChainReflectsExternalStateChangesBetweenCalls)
{
    // The exact property IkSolver.h's CCD loop depends on: unlike
    // ResolveBoneChain() above, this flavor must NEVER cache a result
    // across separate calls, since the pose data it reads can change
    // between them (a link bone rotating mid-solve).
    const SkeletonData skeleton = BuildStraightChain(3);
    std::vector<int> externalState = { 10, 20, 30 };

    auto query = [&]() -> int {
        return ResolveSingleBoneChain<int>(
            skeleton, 2, 0, [&](std::size_t index) -> std::int32_t { return skeleton.bones[index].parentBoneIndex; },
            [&](std::size_t index, int parentValue) -> int { return parentValue + externalState[index]; });
    };

    EXPECT_EQ(query(), 10 + 20 + 30);

    externalState[1] = 1000;
    EXPECT_EQ(query(), 10 + 1000 + 30);
}

TEST(BoneChainResolverTests, ResolveSingleBoneChainOutOfRangeIndexReturnsRootValue)
{
    const SkeletonData skeleton = BuildStraightChain(2);
    const int value = ResolveSingleBoneChain<int>(
        skeleton, 99, -5, [&](std::size_t index) -> std::int32_t { return skeleton.bones[index].parentBoneIndex; },
        [&](std::size_t /*index*/, int parentValue) -> int { return parentValue + 1; });
    EXPECT_EQ(value, -5);
}

TEST(BoneChainResolverTests, ResolveSingleBoneChainCyclicChainTerminatesWithRootValueFallback)
{
    SkeletonData skeleton;
    Bone a;
    a.parentBoneIndex = 1;
    skeleton.bones.push_back(a); // 0
    Bone b;
    b.parentBoneIndex = 0;
    skeleton.bones.push_back(b); // 1 - cycle.

    // Must simply terminate and return SOME finite value, not hang.
    const int value = ResolveSingleBoneChain<int>(
        skeleton, 1, -1, [&](std::size_t index) -> std::int32_t { return skeleton.bones[index].parentBoneIndex; },
        [&](std::size_t /*index*/, int parentValue) -> int { return parentValue + 1; });
    EXPECT_GE(value, -1);
}
