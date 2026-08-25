#include "Animation/AnimationPoseEvaluator.h"

#include "Animation/AppendBoneSolver.h"
#include "Animation/IkSolver.h"
#include "Animation/SkeletonPose.h"

#include <gtest/gtest.h>

using namespace gte;

namespace {

// A straight-line 2-bone "leg" (thigh -> knee -> ankle) plus an IK bone
// targeting the ankle (same shape as IkSolverTests.cpp's
// BuildStraightLegSkeleton()), PLUS a "thighD" bone that fully (weight 1.0)
// append-inherits the thigh's rotation - the real-world rig pattern
// Animation/AppendBoneSolver.h's own file comment describes. This is the
// minimal skeleton needed to prove SolveIkChains() and
// ApplyAppendInheritance() must run in THAT order for a correct result.
SkeletonData BuildLegWithAppendBone()
{
    SkeletonData skeleton;

    Bone thigh;
    thigh.name = "thigh";
    thigh.position = Vec3{ 0.0f, 2.0f, 0.0f };
    thigh.parentBoneIndex = -1;
    skeleton.bones.push_back(thigh); // 0

    Bone knee;
    knee.name = "knee";
    knee.position = Vec3{ 0.0f, 1.0f, 0.0f };
    knee.parentBoneIndex = 0;
    skeleton.bones.push_back(knee); // 1

    Bone ankle;
    ankle.name = "ankle";
    ankle.position = Vec3{ 0.0f, 0.0f, 0.0f };
    ankle.parentBoneIndex = 1;
    skeleton.bones.push_back(ankle); // 2

    Bone footIk;
    footIk.name = "footIK";
    footIk.position = Vec3{ 0.0f, 0.0f, 0.0f };
    footIk.parentBoneIndex = -1;
    footIk.isIk = true;
    footIk.ikTargetBoneIndex = 2; // ankle
    footIk.ikIterationCount = 60;
    footIk.ikAngleLimitRadians = 0.3490658504f; // ~20 degrees per step.
    Bone::IkLink kneeLink;
    kneeLink.boneIndex = 1;
    footIk.ikLinks.push_back(kneeLink);
    Bone::IkLink thighLink;
    thighLink.boneIndex = 0;
    footIk.ikLinks.push_back(thighLink);
    skeleton.bones.push_back(footIk); // 3

    Bone thighD;
    thighD.name = "thighD";
    thighD.position = thigh.position; // Bind-coincident with its source, like a real D-bone.
    thighD.parentBoneIndex = -1;
    thighD.appendRotate = true;
    thighD.appendBoneIndex = 0; // thigh
    thighD.appendWeight = 1.0f;
    skeleton.bones.push_back(thighD); // 4

    return skeleton;
}

ResolvedAnimationBinding BuildFootIkOnlyBinding(const SkeletonData& skeleton)
{
    ResolvedAnimationBinding binding;
    binding.perBoneKeyframes.resize(skeleton.bones.size());

    BoneKeyframe kf;
    kf.frame = 0;
    kf.translation = Vec3{ 0.0f, 0.3f, 0.8f }; // Moves the IK target forward, same as IkSolverTests.cpp.
    kf.rotation = Quat::Identity();
    binding.perBoneKeyframes[3].push_back(kf); // footIK is index 3 - the ONLY bone directly keyframed.
    binding.lastFrame = 0;
    return binding;
}

} // namespace

TEST(AnimationPoseEvaluatorTests, MatchesManualSampleIkAppendFkSequence)
{
    // EvaluateAnimatedSkinningPose() must produce EXACTLY the same result as
    // calling the four underlying steps by hand, in the documented order -
    // this is what makes it a safe drop-in replacement for
    // Game::UpdateSkeletalAnimators()'s previous hand-inlined sequence.
    const SkeletonData skeleton = BuildLegWithAppendBone();
    const ResolvedAnimationBinding binding = BuildFootIkOnlyBinding(skeleton);

    const std::vector<Mat4> evaluated = EvaluateAnimatedSkinningPose(skeleton, binding, 0.0f);

    std::vector<BoneLocalOffset> reference = SampleAnimationPose(binding, 0.0f);
    SolveIkChains(skeleton, reference);
    ApplyAppendInheritance(skeleton, reference);
    const std::vector<Mat4> expected = ComputeSkinningMatrices(skeleton, reference);

    ASSERT_EQ(evaluated.size(), expected.size());
    for (std::size_t i = 0; i < evaluated.size(); ++i) {
        EXPECT_TRUE(ApproximatelyEqual(evaluated[i], expected[i])) << "Mismatch at bone index " << i;
    }
}

TEST(AnimationPoseEvaluatorTests, AppendedBoneInheritsIkSolvedRotationNotRawBindPose)
{
    // A genuine ORDERING regression test: if a future edit swapped
    // SolveIkChains()/ApplyAppendInheritance()'s order inside
    // EvaluateAnimatedSkinningPose(), thighD (index 4) would inherit the
    // thigh's RAW (still bind-pose/identity) rotation instead of its
    // IK-solved one, and this test would fail.
    const SkeletonData skeleton = BuildLegWithAppendBone();
    const ResolvedAnimationBinding binding = BuildFootIkOnlyBinding(skeleton);

    const std::vector<Mat4> evaluated = EvaluateAnimatedSkinningPose(skeleton, binding, 0.0f);

    EXPECT_FALSE(ApproximatelyEqual(evaluated[4], Mat4::Identity()))
        << "Appended bone did not inherit the IK-solved source rotation - append likely ran before IK.";

    // thighD is bind-coincident with thigh and fully (weight 1.0) inherits
    // its rotation with no translation of its own - its final skinning
    // matrix should therefore match thigh's own closely.
    EXPECT_TRUE(ApproximatelyEqual(evaluated[4], evaluated[0], 0.01f));
}

TEST(AnimationPoseEvaluatorTests, NoIkOrAppendBonesStillProducesPlainForwardKinematics)
{
    // A skeleton with neither IK nor append bones must behave exactly like
    // calling ComputeSkinningMatrices() directly - the pipeline shouldn't
    // introduce any behavior change for the common (non-IK, non-append)
    // case.
    SkeletonData skeleton;
    Bone root;
    root.name = "root";
    root.position = Vec3::Zero();
    skeleton.bones.push_back(root);

    ResolvedAnimationBinding binding;
    binding.perBoneKeyframes.resize(1);
    BoneKeyframe kf;
    kf.frame = 0;
    kf.translation = Vec3{ 1.0f, 2.0f, 3.0f };
    kf.rotation = Quat::Identity();
    binding.perBoneKeyframes[0].push_back(kf);

    const std::vector<Mat4> evaluated = EvaluateAnimatedSkinningPose(skeleton, binding, 0.0f);
    ASSERT_EQ(evaluated.size(), 1u);

    const Vec3 animatedPos = evaluated[0].TransformPoint(root.position);
    EXPECT_TRUE(ApproximatelyEqual(animatedPos, root.position + kf.translation, 0.001f));
}
