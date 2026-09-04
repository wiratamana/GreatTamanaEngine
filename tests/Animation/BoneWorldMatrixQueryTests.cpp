// Unit tests for ComputeBoneWorldMatrix (src/Animation/BoneWorldMatrixQuery.h)
// - the shared bone-world-matrix query promoted out of IkSolver.cpp's own
// anonymous namespace (verlet-integration-1 campaign,
// PHASE2_BONE_CHAIN_PHYSICS_BRIDGE.md, Culprit E) so
// Physics/DynamicChainSolver.h/BoneChainPhysicsResolver.h can reuse it
// rather than hand-rolling a second, independent copy of this exact
// cycle-guarded ancestor walk. No ECS/GPU/Renderer involved.

#include "Animation/BoneWorldMatrixQuery.h"

#include "Animation/SkeletonPose.h"

#include <gtest/gtest.h>

#include <cstdint>

namespace gte {
namespace {

// A simple 3-bone chain: root -> mid -> tip, straight up the +Y axis in
// bind pose (root at y=0, mid at y=1, tip at y=2).
SkeletonData BuildStraightChainSkeleton()
{
    SkeletonData skeleton;

    Bone root;
    root.name = "root";
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    skeleton.bones.push_back(root); // index 0

    Bone mid;
    mid.name = "mid";
    mid.position = Vec3(0.0f, 1.0f, 0.0f);
    mid.parentBoneIndex = 0;
    skeleton.bones.push_back(mid); // index 1

    Bone tip;
    tip.name = "tip";
    tip.position = Vec3(0.0f, 2.0f, 0.0f);
    tip.parentBoneIndex = 1;
    skeleton.bones.push_back(tip); // index 2

    return skeleton;
}

} // namespace

TEST(BoneWorldMatrixQueryTests, BindPoseWorldMatrixMatchesBoneBindPosition)
{
    SkeletonData skeleton = BuildStraightChainSkeleton();
    std::vector<BoneLocalOffset> pose(skeleton.bones.size()); // all-identity offsets

    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const Mat4 world = ComputeBoneWorldMatrix(skeleton, pose, static_cast<std::int32_t>(i));
        EXPECT_TRUE(ApproximatelyEqual(world.TransformPoint(Vec3::Zero()), skeleton.bones[i].position));
    }
}

TEST(BoneWorldMatrixQueryTests, RootBoneTranslationOffsetPropagatesToEveryDescendant)
{
    SkeletonData skeleton = BuildStraightChainSkeleton();
    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    pose[0].translation = Vec3(5.0f, 0.0f, 0.0f); // shift the whole chain sideways

    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const Mat4 world = ComputeBoneWorldMatrix(skeleton, pose, static_cast<std::int32_t>(i));
        const Vec3 expected = skeleton.bones[i].position + Vec3(5.0f, 0.0f, 0.0f);
        EXPECT_TRUE(ApproximatelyEqual(world.TransformPoint(Vec3::Zero()), expected));
    }
}

// Cross-checks the promoted ComputeBoneWorldMatrix() (this file) against
// ComputeSkinningMatrices() (SkeletonPose.h, already independently tested -
// see tests/Animation/SkeletonPoseTests.cpp/IkSolverTests.cpp) for the exact
// SAME resolved pose - both must agree on every bone's world position,
// proving the IkSolver.cpp extraction changed nothing behaviorally.
TEST(BoneWorldMatrixQueryTests, MatchesSkinningMatricesForTheSameResolvedPose)
{
    SkeletonData skeleton = BuildStraightChainSkeleton();

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    pose[0].translation = Vec3(0.3f, 0.1f, -0.2f);
    pose[1].rotation = Quat::FromAxisAngle(Vec3::Right(), 0.4f);
    pose[2].rotation = Quat::FromAxisAngle(Vec3::Up(), 0.7f);

    const std::vector<Mat4> skinningMatrices = ComputeSkinningMatrices(skeleton, pose);
    ASSERT_EQ(skinningMatrices.size(), skeleton.bones.size());

    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        const Mat4 world = ComputeBoneWorldMatrix(skeleton, pose, static_cast<std::int32_t>(i));
        const Vec3 fromWorld = world.TransformPoint(Vec3::Zero());
        const Vec3 fromSkinning = skinningMatrices[i].TransformPoint(skeleton.bones[i].position);
        EXPECT_TRUE(ApproximatelyEqual(fromWorld, fromSkinning, 1e-4f))
            << "Bone " << i << " world position disagreement between ComputeBoneWorldMatrix and ComputeSkinningMatrices.";
    }
}

TEST(BoneWorldMatrixQueryTests, OutOfRangeBoneIndexReturnsIdentity)
{
    SkeletonData skeleton = BuildStraightChainSkeleton();
    std::vector<BoneLocalOffset> pose(skeleton.bones.size());

    const Mat4 world = ComputeBoneWorldMatrix(skeleton, pose, 99);
    EXPECT_TRUE(ApproximatelyEqual(world, Mat4::Identity()));

    const Mat4 worldNegative = ComputeBoneWorldMatrix(skeleton, pose, -1);
    EXPECT_TRUE(ApproximatelyEqual(worldNegative, Mat4::Identity()));
}

} // namespace gte
