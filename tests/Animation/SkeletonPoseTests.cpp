#include "Animation/SkeletonPose.h"

#include <cmath>
#include <gtest/gtest.h>

using namespace gte;

TEST(SkeletonPoseTests, AllIdentityOffsetsProduceIdentityMatrices)
{
    SkeletonData skeleton;

    Bone root;
    root.name = "root";
    root.position = Vec3{ 0.0f, 0.0f, 0.0f };
    root.parentBoneIndex = -1;
    skeleton.bones.push_back(root);

    Bone child;
    child.name = "child";
    child.position = Vec3{ 0.0f, 1.0f, 0.0f };
    child.parentBoneIndex = 0;
    skeleton.bones.push_back(child);

    const std::vector<BoneLocalOffset> offsets(2); // default-constructed: identity everywhere.
    const std::vector<Mat4> matrices = ComputeSkinningMatrices(skeleton, offsets);

    ASSERT_EQ(matrices.size(), 2u);
    EXPECT_TRUE(ApproximatelyEqual(matrices[0], Mat4::Identity()));
    EXPECT_TRUE(ApproximatelyEqual(matrices[1], Mat4::Identity()));
}

TEST(SkeletonPoseTests, ParentRotationSwingsChildAroundIt)
{
    SkeletonData skeleton;

    Bone root;
    root.position = Vec3::Zero();
    root.parentBoneIndex = -1;
    skeleton.bones.push_back(root);

    Bone child;
    child.position = Vec3{ 0.0f, 1.0f, 0.0f };
    child.parentBoneIndex = 0;
    skeleton.bones.push_back(child);

    std::vector<BoneLocalOffset> offsets(2);
    const Quat rootRotation = Quat::FromAxisAngle(Vec3::Forward(), 1.57079632679f); // 90 degrees around Z.
    offsets[0].rotation = rootRotation;

    const std::vector<Mat4> matrices = ComputeSkinningMatrices(skeleton, offsets);
    ASSERT_EQ(matrices.size(), 2u);

    // skinningMatrix(child) applied to child's own BIND position must equal
    // its real animated world position - see SkeletonPose.h's own doc
    // comment for the derivation: with a zero-translation root and an
    // identity child offset, that's exactly the root's rotation applied to
    // the child's bind-relative offset from the root.
    const Vec3 animatedChildPos = matrices[1].TransformPoint(child.position);
    const Vec3 expected = rootRotation.RotateVector(child.position - root.position);

    EXPECT_TRUE(ApproximatelyEqual(animatedChildPos, expected, 0.001f));
}

TEST(SkeletonPoseTests, TranslationOffsetMovesBoneRelativeToBindPose)
{
    SkeletonData skeleton;
    Bone root;
    root.position = Vec3{ 1.0f, 0.0f, 0.0f };
    root.parentBoneIndex = -1;
    skeleton.bones.push_back(root);

    std::vector<BoneLocalOffset> offsets(1);
    offsets[0].translation = Vec3{ 0.0f, 2.0f, 0.0f };

    const std::vector<Mat4> matrices = ComputeSkinningMatrices(skeleton, offsets);
    ASSERT_EQ(matrices.size(), 1u);

    const Vec3 animatedPos = matrices[0].TransformPoint(root.position);
    EXPECT_TRUE(ApproximatelyEqual(animatedPos, root.position + offsets[0].translation, 0.001f));
}

TEST(SkeletonPoseTests, CyclicParentChainTerminatesAndProducesFiniteMatrices)
{
    // Malformed data (a bone graph with a cycle) must never hang/recurse
    // forever - this test finishing at all is half the assertion; the
    // finite-value checks below cover the rest.
    SkeletonData skeleton;

    Bone a;
    a.position = Vec3{ 1.0f, 0.0f, 0.0f };
    a.parentBoneIndex = 1; // points at b
    skeleton.bones.push_back(a);

    Bone b;
    b.position = Vec3{ 0.0f, 1.0f, 0.0f };
    b.parentBoneIndex = 0; // points at a - cycle
    skeleton.bones.push_back(b);

    const std::vector<BoneLocalOffset> offsets(2);
    const std::vector<Mat4> matrices = ComputeSkinningMatrices(skeleton, offsets);

    ASSERT_EQ(matrices.size(), 2u);
    for (const Mat4& m : matrices) {
        for (int c = 0; c < 4; ++c) {
            for (int r = 0; r < 4; ++r) {
                EXPECT_TRUE(std::isfinite(m(r, c)));
            }
        }
    }
}
