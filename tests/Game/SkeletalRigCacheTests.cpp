// Unit tests for SkeletalRigCache.h - the animation-owned replacement for
// Game's old private m_meshSkinningCache member (see
// GameInstantiationRefactorProposal.txt, Step 3.5). Pure register/lookup/
// miss behavior over plain data - no GPU/Renderer/file-I/O involved.

#include "Game/Animation/SkeletalRigCache.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(SkeletalRigCacheTest, TryGetReturnsNullptrForAnUnregisteredPath)
{
    SkeletalRigCache cache;
    EXPECT_EQ(cache.TryGet("C:/Project/Nope.gta"), nullptr);
}

TEST(SkeletalRigCacheTest, RegisterThenTryGetReturnsTheSameData)
{
    SkeletalRigCache cache;

    SkinnedMeshData data;
    data.bindPositions = { Vec3{ 1.0f, 2.0f, 3.0f } };
    data.skeleton.bones.resize(2);

    cache.Register("C:/Project/Furina.gta", data);

    const SkinnedMeshData* found = cache.TryGet("C:/Project/Furina.gta");
    ASSERT_NE(found, nullptr);
    ASSERT_EQ(found->bindPositions.size(), 1u);
    EXPECT_TRUE(ApproximatelyEqual(found->bindPositions[0], Vec3{ 1.0f, 2.0f, 3.0f }));
    EXPECT_EQ(found->skeleton.bones.size(), 2u);
}

TEST(SkeletalRigCacheTest, RegisteringTheSamePathTwiceOverwritesRatherThanDuplicating)
{
    SkeletalRigCache cache;

    SkinnedMeshData first;
    first.skeleton.bones.resize(1);
    cache.Register("C:/Project/Model.gta", first);

    SkinnedMeshData second;
    second.skeleton.bones.resize(5);
    cache.Register("C:/Project/Model.gta", second);

    const SkinnedMeshData* found = cache.TryGet("C:/Project/Model.gta");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->skeleton.bones.size(), 5u);
}

TEST(SkeletalRigCacheTest, DifferentPathsAreTrackedIndependently)
{
    SkeletalRigCache cache;

    SkinnedMeshData a;
    a.skeleton.bones.resize(1);
    SkinnedMeshData b;
    b.skeleton.bones.resize(2);

    cache.Register("C:/Project/A.gta", a);
    cache.Register("C:/Project/B.gta", b);

    ASSERT_NE(cache.TryGet("C:/Project/A.gta"), nullptr);
    ASSERT_NE(cache.TryGet("C:/Project/B.gta"), nullptr);
    EXPECT_EQ(cache.TryGet("C:/Project/A.gta")->skeleton.bones.size(), 1u);
    EXPECT_EQ(cache.TryGet("C:/Project/B.gta")->skeleton.bones.size(), 2u);
}

} // namespace
} // namespace gte
