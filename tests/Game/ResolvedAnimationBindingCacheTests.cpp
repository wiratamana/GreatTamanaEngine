// Unit tests for ResolvedAnimationBindingCache.h - replaces Game's old
// m_resolvedAnimationBindingCache, keyed by a hand-concatenated
// "meshPath\x1Fanimationpath" string (see
// GameInstantiationRefactorProposal.txt, Step 2.5/3.5) with a real struct
// key (AnimationBindingKey) instead. Pure cache hit/miss behavior - no
// GPU/Renderer/file-I/O involved (the actual bone-name resolution math,
// ResolveBoneTracksToSkeleton(), is already covered by
// tests/Animation/MotionSamplerTests.cpp).

#include "Game/Animation/ResolvedAnimationBindingCache.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

SkeletonData MakeSkeletonWithOneBone(const std::string& name)
{
    SkeletonData skeleton;
    Bone bone;
    bone.name = name;
    skeleton.bones.push_back(bone);
    return skeleton;
}

MotionData MakeMotionWithOneBoneKeyframe(const std::string& boneName, std::uint32_t frame)
{
    MotionData motion;
    BoneKeyframe keyframe;
    keyframe.boneName = boneName;
    keyframe.frame = frame;
    motion.boneKeyframes.push_back(keyframe);
    return motion;
}

TEST(ResolvedAnimationBindingCacheTest, TryGetReturnsNullptrForAnUncomputedKey)
{
    ResolvedAnimationBindingCache cache;
    const AnimationBindingKey key{ "C:/Mesh.gta", "C:/Anim.gta" };
    EXPECT_EQ(cache.TryGet(key), nullptr);
}

TEST(ResolvedAnimationBindingCacheTest, GetOrComputeCachesAndReusesTheSameBinding)
{
    ResolvedAnimationBindingCache cache;
    const AnimationBindingKey key{ "C:/Mesh.gta", "C:/Anim.gta" };
    const SkeletonData skeleton = MakeSkeletonWithOneBone("Hips");
    const MotionData motion = MakeMotionWithOneBoneKeyframe("Hips", 5);

    const ResolvedAnimationBinding& first = cache.GetOrCompute(key, skeleton, motion);
    ASSERT_EQ(first.perBoneKeyframes.size(), 1u);
    EXPECT_EQ(first.lastFrame, 5u);

    // Second call for the SAME key must hit the cache (same object identity),
    // not recompute - checked via TryGet() returning the exact same address.
    const ResolvedAnimationBinding* cached = cache.TryGet(key);
    ASSERT_NE(cached, nullptr);
    EXPECT_EQ(cached, &cache.GetOrCompute(key, skeleton, motion));
}

TEST(ResolvedAnimationBindingCacheTest, DifferentMeshAnimationPairsNeverCollide)
{
    ResolvedAnimationBindingCache cache;

    // Deliberately chosen so that a NAIVE "meshPath + animationPath"
    // concatenation (no separator at all) would produce the exact same
    // joined string for both pairs ("FooBar") - a real struct key with
    // field-by-field equality can never confuse these two, unlike that
    // naive scheme.
    const AnimationBindingKey keyA{ "Foo", "Bar" };
    const AnimationBindingKey keyB{ "FooB", "ar" };

    const SkeletonData skeletonA = MakeSkeletonWithOneBone("BoneA");
    const MotionData motionA = MakeMotionWithOneBoneKeyframe("BoneA", 10);
    const SkeletonData skeletonB = MakeSkeletonWithOneBone("BoneB");
    const MotionData motionB = MakeMotionWithOneBoneKeyframe("BoneB", 20);

    const ResolvedAnimationBinding& bindingA = cache.GetOrCompute(keyA, skeletonA, motionA);
    const ResolvedAnimationBinding& bindingB = cache.GetOrCompute(keyB, skeletonB, motionB);

    EXPECT_EQ(bindingA.lastFrame, 10u);
    EXPECT_EQ(bindingB.lastFrame, 20u);
    EXPECT_NE(&bindingA, &bindingB);

    ASSERT_NE(cache.TryGet(keyA), nullptr);
    ASSERT_NE(cache.TryGet(keyB), nullptr);
    EXPECT_EQ(cache.TryGet(keyA)->lastFrame, 10u);
    EXPECT_EQ(cache.TryGet(keyB)->lastFrame, 20u);
}

TEST(AnimationBindingKeyTest, EqualityComparesBothFields)
{
    const AnimationBindingKey a{ "Mesh1", "Anim1" };
    const AnimationBindingKey b{ "Mesh1", "Anim1" };
    const AnimationBindingKey c{ "Mesh1", "Anim2" };
    const AnimationBindingKey d{ "Mesh2", "Anim1" };

    EXPECT_TRUE(a == b);
    EXPECT_FALSE(a == c);
    EXPECT_FALSE(a == d);
    EXPECT_TRUE(a != c);
}

} // namespace
} // namespace gte
