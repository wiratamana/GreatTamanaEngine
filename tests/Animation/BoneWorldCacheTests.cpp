// Unit tests for BoneWorldCache (src/Animation/BoneWorldCache.h) and the allocation-free single-bone walk.
// Every result must match ComputeBoneWorldMatrix() bit for bit.

#include "Animation/BoneWorldCache.h"
#include "Animation/BoneChainResolver.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <cstring>
#include <random>

namespace gte {
namespace {

bool SameBits(const Mat4& a, const Mat4& b)
{
    return std::memcmp(&a, &b, sizeof(Mat4)) == 0;
}

// Parents are NOT always before children, to cover the unordered case.
SkeletonData BuildRandomSkeleton(std::mt19937& rng, std::size_t count)
{
    SkeletonData skeleton;
    std::uniform_real_distribution<float> pos(-5.0f, 5.0f);
    skeleton.bones.resize(count);
    for (std::size_t i = 0; i < count; ++i) {
        skeleton.bones[i].position = Vec3(pos(rng), pos(rng), pos(rng));
        skeleton.bones[i].parentBoneIndex = -1;
    }
    for (std::size_t i = 1; i < count; ++i) {
        skeleton.bones[i].parentBoneIndex = static_cast<std::int32_t>(rng() % i);
    }
    return skeleton;
}

BoneLocalOffset RandomOffset(std::mt19937& rng)
{
    std::uniform_real_distribution<float> t(-1.0f, 1.0f);
    BoneLocalOffset offset;
    offset.translation = Vec3(t(rng), t(rng), t(rng));
    offset.rotation = Normalize(Quat(t(rng), t(rng), t(rng), t(rng) + 2.0f));
    return offset;
}

} // namespace

TEST(BoneWorldCacheTests, MatchesComputeBoneWorldMatrixBitForBitAfterRandomPoseWrites)
{
    std::mt19937 rng(1234);
    const SkeletonData skeleton = BuildRandomSkeleton(rng, 60);
    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    for (BoneLocalOffset& offset : pose) {
        offset = RandomOffset(rng);
    }

    BoneWorldCache cache;
    cache.Reset(skeleton.bones.size());
    for (int round = 0; round < 400; ++round) {
        const std::int32_t bone = static_cast<std::int32_t>(rng() % skeleton.bones.size());
        ASSERT_TRUE(SameBits(cache.Get(skeleton, pose, bone), ComputeBoneWorldMatrix(skeleton, pose, bone))) << "round " << round;
        if (round % 3 == 0) {
            const std::int32_t written = static_cast<std::int32_t>(rng() % skeleton.bones.size());
            pose[static_cast<std::size_t>(written)] = RandomOffset(rng);
            cache.Invalidate(written);
        }
    }
}

TEST(BoneWorldCacheTests, ResetDropsEverythingAfterAWholesalePoseChange)
{
    std::mt19937 rng(7);
    const SkeletonData skeleton = BuildRandomSkeleton(rng, 20);
    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    BoneWorldCache cache;
    cache.Reset(skeleton.bones.size());
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        cache.Get(skeleton, pose, static_cast<std::int32_t>(i));
    }
    for (BoneLocalOffset& offset : pose) {
        offset = RandomOffset(rng);
    }
    cache.Reset(skeleton.bones.size());
    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        EXPECT_TRUE(SameBits(cache.Get(skeleton, pose, static_cast<std::int32_t>(i)),
            ComputeBoneWorldMatrix(skeleton, pose, static_cast<std::int32_t>(i))));
    }
}

TEST(BoneWorldCacheTests, OutOfRangeBoneGivesIdentityLikeTheUncachedQuery)
{
    SkeletonData skeleton;
    skeleton.bones.resize(2);
    skeleton.bones[1].parentBoneIndex = 0;
    std::vector<BoneLocalOffset> pose(2);
    BoneWorldCache cache;
    EXPECT_TRUE(SameBits(cache.Get(skeleton, pose, -1), Mat4::Identity()));
    EXPECT_TRUE(SameBits(cache.Get(skeleton, pose, 9), Mat4::Identity()));
}

TEST(BoneWorldCacheTests, CyclicAndSelfParentedChainsMatchTheUncachedQuery)
{
    SkeletonData skeleton;
    skeleton.bones.resize(4);
    for (std::size_t i = 0; i < 4; ++i) {
        skeleton.bones[i].position = Vec3(static_cast<float>(i), 1.0f, 0.5f * static_cast<float>(i));
    }
    skeleton.bones[0].parentBoneIndex = 2; // 0 -> 2 -> 1 -> 0 is a cycle
    skeleton.bones[1].parentBoneIndex = 0;
    skeleton.bones[2].parentBoneIndex = 1;
    skeleton.bones[3].parentBoneIndex = 3; // self parent
    std::mt19937 rng(5);
    std::vector<BoneLocalOffset> pose(4);
    for (BoneLocalOffset& offset : pose) {
        offset = RandomOffset(rng);
    }
    BoneWorldCache cache;
    cache.Reset(4);
    for (std::int32_t bone = 0; bone < 4; ++bone) {
        EXPECT_TRUE(SameBits(cache.Get(skeleton, pose, bone), ComputeBoneWorldMatrix(skeleton, pose, bone))) << "bone " << bone;
    }
}

TEST(BoneWorldCacheTests, ChainDeeperThanTheInlineArrayStillMatches)
{
    SkeletonData skeleton;
    constexpr std::size_t kDepth = 300;
    skeleton.bones.resize(kDepth);
    std::mt19937 rng(99);
    std::vector<BoneLocalOffset> pose(kDepth);
    for (std::size_t i = 0; i < kDepth; ++i) {
        skeleton.bones[i].position = Vec3(0.0f, static_cast<float>(i) * 0.1f, 0.0f);
        skeleton.bones[i].parentBoneIndex = i == 0 ? -1 : static_cast<std::int32_t>(i - 1);
        pose[i] = RandomOffset(rng);
    }
    BoneWorldCache cache;
    cache.Reset(kDepth);
    const std::int32_t tip = static_cast<std::int32_t>(kDepth - 1);
    EXPECT_TRUE(SameBits(cache.Get(skeleton, pose, tip), ComputeBoneWorldMatrix(skeleton, pose, tip)));
}

// Reference copy of the recursive single-bone walk with its per-call visiting array.
Mat4 ReferenceWorld(const SkeletonData& skeleton, const std::vector<BoneLocalOffset>& pose, std::int32_t boneIndex)
{
    const std::size_t count = skeleton.bones.size();
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= count) {
        return Mat4::Identity();
    }
    std::vector<std::uint8_t> visiting(count, 0);
    struct Recur {
        const SkeletonData& skeleton;
        const std::vector<BoneLocalOffset>& pose;
        std::vector<std::uint8_t>& visiting;
        Mat4 Resolve(std::size_t index)
        {
            if (visiting[index] != 0) {
                return Mat4::Identity();
            }
            visiting[index] = 1;
            const std::int32_t next = skeleton.bones[index].parentBoneIndex;
            Mat4 predecessor = Mat4::Identity();
            if (next >= 0 && static_cast<std::size_t>(next) < skeleton.bones.size() && static_cast<std::size_t>(next) != index) {
                predecessor = Resolve(static_cast<std::size_t>(next));
            }
            const BoneLocalOffset offset = index < pose.size() ? pose[index] : BoneLocalOffset{};
            const Mat4 value = predecessor * ComputeBoneLocalMatrix(skeleton, index, offset);
            visiting[index] = 0;
            return value;
        }
    };
    Recur recur{ skeleton, pose, visiting };
    return recur.Resolve(static_cast<std::size_t>(boneIndex));
}

TEST(BoneChainResolverFastPathTests, ComputeBoneWorldMatrixMatchesTheRecursiveWalkOnRandomCyclicAndDeepSkeletons)
{
    std::mt19937 rng(2024);
    for (int trial = 0; trial < 30; ++trial) {
        const std::size_t count = trial < 25 ? 40 : 260;
        SkeletonData skeleton = BuildRandomSkeleton(rng, count);
        if (trial >= 25) { // a straight deep chain
            for (std::size_t i = 1; i < count; ++i) {
                skeleton.bones[i].parentBoneIndex = static_cast<std::int32_t>(i - 1);
            }
        }
        if (trial % 5 == 4) { // inject a cycle
            skeleton.bones[0].parentBoneIndex = static_cast<std::int32_t>(count - 1);
        }
        std::vector<BoneLocalOffset> pose(count);
        for (BoneLocalOffset& offset : pose) {
            offset = RandomOffset(rng);
        }
        for (std::size_t bone = 0; bone < count; ++bone) {
            ASSERT_TRUE(SameBits(ComputeBoneWorldMatrix(skeleton, pose, static_cast<std::int32_t>(bone)),
                ReferenceWorld(skeleton, pose, static_cast<std::int32_t>(bone))))
                << "trial " << trial << " bone " << bone;
        }
    }
}

} // namespace gte
