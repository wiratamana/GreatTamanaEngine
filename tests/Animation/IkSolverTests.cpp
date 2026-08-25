#include "Animation/IkSolver.h"

#include "Animation/SkeletonPose.h"

#include <cmath>
#include <gtest/gtest.h>

using namespace gte;

namespace {

// Builds a minimal, straight-line 2-bone "leg" (thigh -> knee -> ankle)
// plus a separate, non-hierarchy-connected "footIK" bone whose ikLinks
// name [knee, thigh] (nearest-effector-first, PMX's own storage order) and
// whose ikTargetBoneIndex is the ankle - the exact shape a real MMD leg rig
// uses (see Assets/SkeletonData.h). All bones start straight down the -Y
// axis, bind-pose (thigh at y=2, knee at y=1, ankle at y=0), matching a
// simple standing leg.
SkeletonData BuildStraightLegSkeleton()
{
    SkeletonData skeleton;

    Bone thigh;
    thigh.name = "thigh";
    thigh.position = Vec3{ 0.0f, 2.0f, 0.0f };
    thigh.parentBoneIndex = -1;
    skeleton.bones.push_back(thigh); // index 0

    Bone knee;
    knee.name = "knee";
    knee.position = Vec3{ 0.0f, 1.0f, 0.0f };
    knee.parentBoneIndex = 0;
    skeleton.bones.push_back(knee); // index 1

    Bone ankle;
    ankle.name = "ankle";
    ankle.position = Vec3{ 0.0f, 0.0f, 0.0f };
    ankle.parentBoneIndex = 1;
    skeleton.bones.push_back(ankle); // index 2

    Bone footIk;
    footIk.name = "footIK";
    footIk.position = Vec3{ 0.0f, 0.0f, 0.0f }; // Bind-coincides with the ankle, like a real rig.
    footIk.parentBoneIndex = -1;
    footIk.isIk = true;
    footIk.ikTargetBoneIndex = 2; // ankle
    footIk.ikIterationCount = 60;
    footIk.ikAngleLimitRadians = 0.3490658504f; // ~20 degrees per step - generous for a 2-bone test chain.
    Bone::IkLink kneeLink;
    kneeLink.boneIndex = 1; // knee - nearest to the effector.
    footIk.ikLinks.push_back(kneeLink);
    Bone::IkLink thighLink;
    thighLink.boneIndex = 0; // thigh - nearest to the root.
    footIk.ikLinks.push_back(thighLink);
    skeleton.bones.push_back(footIk); // index 3

    return skeleton;
}

} // namespace

TEST(IkSolverTests, NonIkBoneOffsetsAreLeftUntouched)
{
    SkeletonData skeleton = BuildStraightLegSkeleton();
    // No IK bone at all - a plain 2-bone chain.
    skeleton.bones.pop_back();

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    pose[0].translation = Vec3{ 1.0f, 2.0f, 3.0f };

    SolveIkChains(skeleton, pose);

    EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3{ 1.0f, 2.0f, 3.0f }));
    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()));
    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, Quat::Identity()));
}

TEST(IkSolverTests, MovingIkTargetForwardBendsTheKnee)
{
    SkeletonData skeleton = BuildStraightLegSkeleton();
    std::vector<BoneLocalOffset> pose(skeleton.bones.size());

    // Move the (invisible) IK bone forward and slightly up from its bind
    // position - a real dance motion moving the foot forward mid-stride -
    // never touching the thigh/knee bones' own offsets directly (they stay
    // BoneLocalOffset{} here, exactly like a real VMD bone track that only
    // targets the IK bone).
    pose[3].translation = Vec3{ 0.0f, 0.3f, 0.8f };

    SolveIkChains(skeleton, pose);

    // The knee/thigh bones (previously untouched - the exact bug this
    // solver fixes) must now carry a genuinely non-identity rotation.
    EXPECT_FALSE(RepresentSameRotation(pose[1].rotation, Quat::Identity()))
        << "Knee bone was not rotated by the IK solve - the leg would stay frozen in bind pose.";
    EXPECT_FALSE(RepresentSameRotation(pose[0].rotation, Quat::Identity()))
        << "Thigh bone was not rotated by the IK solve - the leg would stay frozen in bind pose.";

    // The ankle (the effector) must actually have reached (or come very
    // close to) the IK bone's own animated target position.
    const std::vector<Mat4> worldMatrices = ComputeSkinningMatrices(skeleton, pose);
    // ComputeSkinningMatrices() returns SKINNING matrices (world * inverse
    // bind) - re-apply each bone's own bind position to recover its real
    // animated world position, exactly like SkeletonPoseTests.cpp does.
    const Vec3 animatedAnklePos = worldMatrices[2].TransformPoint(skeleton.bones[2].position);
    const Vec3 animatedTargetPos = worldMatrices[3].TransformPoint(skeleton.bones[3].position);

    EXPECT_TRUE(ApproximatelyEqual(animatedAnklePos, animatedTargetPos, 0.01f))
        << "Effector (ankle) did not converge onto the IK target position.";
}

TEST(IkSolverTests, KneeAngleLimitIsRespected)
{
    SkeletonData skeleton = BuildStraightLegSkeleton();
    // Restrict the knee link to bending only around X, within a plausible
    // one-directional knee range (0 to ~150 degrees) - MMD's own convention
    // for a knee joint.
    Bone::IkLink& kneeLink = skeleton.bones[3].ikLinks[0];
    kneeLink.hasAngleLimit = true;
    kneeLink.angleLimitMin = Vec3{ 0.0f, 0.0f, 0.0f };
    kneeLink.angleLimitMax = Vec3{ 2.6179938780f, 0.0f, 0.0f }; // 150 degrees.

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    // An unreasonable target that would need the knee to bend backwards
    // past its allowed range if left unclamped.
    pose[3].translation = Vec3{ 0.0f, 1.9f, -0.05f };

    SolveIkChains(skeleton, pose);

    const Vec3 kneeEulerDeg = pose[1].rotation.ToEulerDegrees();
    EXPECT_GE(kneeEulerDeg.x, -0.01f) << "Knee rotated outside its configured angle limit (min).";
    EXPECT_LE(kneeEulerDeg.x, 150.01f) << "Knee rotated outside its configured angle limit (max).";
}

TEST(IkSolverTests, MalformedIkDataDoesNotCrashOrHang)
{
    SkeletonData skeleton;

    Bone bad;
    bad.name = "bad";
    bad.isIk = true;
    bad.ikTargetBoneIndex = 99; // Out of range.
    bad.ikIterationCount = 40;
    Bone::IkLink link;
    link.boneIndex = 123; // Out of range.
    bad.ikLinks.push_back(link);
    skeleton.bones.push_back(bad);

    std::vector<BoneLocalOffset> pose(1);
    SolveIkChains(skeleton, pose); // Must simply return, not crash/hang.

    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()));
}
