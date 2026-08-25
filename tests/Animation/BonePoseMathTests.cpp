#include "Animation/BonePoseMath.h"

#include <gtest/gtest.h>

using namespace gte;

TEST(BonePoseMathTests, RootBoneLocalMatrixIsBindPositionPlusOffset)
{
    SkeletonData skeleton;
    Bone root;
    root.position = Vec3{ 1.0f, 2.0f, 3.0f };
    root.parentBoneIndex = -1;
    skeleton.bones.push_back(root);

    BoneLocalOffset offset;
    offset.translation = Vec3{ 0.0f, 1.0f, 0.0f };
    offset.rotation = Quat::Identity();

    const Mat4 local = ComputeBoneLocalMatrix(skeleton, 0, offset);
    const Vec3 result = local.TransformPoint(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(result, root.position + offset.translation));
}

TEST(BonePoseMathTests, ChildBoneLocalMatrixIsRelativeToParentBindPosition)
{
    SkeletonData skeleton;
    Bone root;
    root.position = Vec3{ 1.0f, 0.0f, 0.0f };
    root.parentBoneIndex = -1;
    skeleton.bones.push_back(root);

    Bone child;
    child.position = Vec3{ 1.0f, 2.0f, 0.0f };
    child.parentBoneIndex = 0;
    skeleton.bones.push_back(child);

    const Mat4 local = ComputeBoneLocalMatrix(skeleton, 1, BoneLocalOffset{});
    const Vec3 result = local.TransformPoint(Vec3::Zero());
    // Identity offset - just the bind-relative offset from the parent.
    EXPECT_TRUE(ApproximatelyEqual(result, child.position - root.position));
}

TEST(BonePoseMathTests, OutOfRangeOrSelfReferencingParentIsTreatedAsRoot)
{
    SkeletonData skeleton;
    Bone selfReferencing;
    selfReferencing.position = Vec3{ 5.0f, 0.0f, 0.0f };
    selfReferencing.parentBoneIndex = 0; // Points at itself.
    skeleton.bones.push_back(selfReferencing);

    const Mat4 local = ComputeBoneLocalMatrix(skeleton, 0, BoneLocalOffset{});
    const Vec3 result = local.TransformPoint(Vec3::Zero());
    // Self-reference must be ignored (treated as "no parent"), not fed back
    // into the bind-offset formula.
    EXPECT_TRUE(ApproximatelyEqual(result, selfReferencing.position));
}
