#include "Animation/AppendBoneSolver.h"

#include <gtest/gtest.h>

using namespace gte;

TEST(AppendBoneSolverTests, NonAppendBoneOffsetsAreLeftUntouched)
{
    SkeletonData skeleton;
    Bone plain;
    plain.name = "plain";
    skeleton.bones.push_back(plain);

    std::vector<BoneLocalOffset> pose(1);
    pose[0].translation = Vec3{ 1.0f, 2.0f, 3.0f };
    pose[0].rotation = Quat::FromAxisAngle(Vec3::Up(), 0.5f);

    const Vec3 expectedTranslation = pose[0].translation;
    const Quat expectedRotation = pose[0].rotation;

    ApplyAppendInheritance(skeleton, pose);

    EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, expectedTranslation));
    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, expectedRotation));
}

TEST(AppendBoneSolverTests, FullWeightAppendRotateCopiesSourceRotation)
{
    // Mirrors the real-world "D-bone" rig pattern this feature exists for
    // (see AppendBoneSolver.h's own file comment): a source bone (e.g.
    // "thigh") with its own animated rotation, and a second, otherwise-
    // never-directly-keyframed bone ("thighD") that fully inherits it via
    // appendRotate + appendWeight == 1.0.
    SkeletonData skeleton;

    Bone source;
    source.name = "thigh";
    skeleton.bones.push_back(source); // index 0

    Bone appended;
    appended.name = "thighD";
    appended.appendRotate = true;
    appended.appendBoneIndex = 0;
    appended.appendWeight = 1.0f;
    skeleton.bones.push_back(appended); // index 1

    std::vector<BoneLocalOffset> pose(2);
    const Quat sourceRotation = Quat::FromAxisAngle(Vec3::Right(), 0.7853981634f); // 45 degrees.
    pose[0].rotation = sourceRotation;
    // pose[1] starts at identity (never directly keyframed, matching a
    // real D-bone).

    ApplyAppendInheritance(skeleton, pose);

    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, sourceRotation))
        << "A full-weight (1.0) append-rotate bone must end up with exactly the source bone's own rotation.";
    // The source bone itself must be completely unaffected by being an
    // append SOURCE.
    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, sourceRotation));
}

TEST(AppendBoneSolverTests, PartialWeightBlendsTowardIdentity)
{
    SkeletonData skeleton;
    Bone source;
    source.name = "source";
    skeleton.bones.push_back(source); // index 0

    Bone appended;
    appended.name = "appended";
    appended.appendRotate = true;
    appended.appendBoneIndex = 0;
    appended.appendWeight = 0.5f;
    skeleton.bones.push_back(appended); // index 1

    std::vector<BoneLocalOffset> pose(2);
    pose[0].rotation = Quat::FromAxisAngle(Vec3::Up(), 1.0f); // Some non-trivial rotation.

    ApplyAppendInheritance(skeleton, pose);

    // Half-weight must be strictly between identity and the full source
    // rotation - i.e. genuinely partial, neither untouched nor a full copy.
    EXPECT_FALSE(RepresentSameRotation(pose[1].rotation, Quat::Identity()));
    EXPECT_FALSE(RepresentSameRotation(pose[1].rotation, pose[0].rotation));
}

TEST(AppendBoneSolverTests, AppendTranslateAddsScaledSourceTranslation)
{
    SkeletonData skeleton;
    Bone source;
    source.name = "source";
    skeleton.bones.push_back(source); // index 0

    Bone appended;
    appended.name = "appended";
    appended.appendTranslate = true;
    appended.appendBoneIndex = 0;
    appended.appendWeight = 0.5f;
    skeleton.bones.push_back(appended); // index 1

    std::vector<BoneLocalOffset> pose(2);
    pose[0].translation = Vec3{ 2.0f, 4.0f, 6.0f };
    pose[1].translation = Vec3{ 1.0f, 1.0f, 1.0f }; // This bone's own authored offset.

    ApplyAppendInheritance(skeleton, pose);

    const Vec3 expected = Vec3{ 1.0f, 1.0f, 1.0f } + Vec3{ 2.0f, 4.0f, 6.0f } * 0.5f;
    EXPECT_TRUE(ApproximatelyEqual(pose[1].translation, expected));
}

TEST(AppendBoneSolverTests, NegativeWeightProducesAnInverseLikeRotation)
{
    // Mirrors a real "shoulder cancel" bone (e.g. this engine's own test
    // model's "肩C" bones, appendWeight == -1.0) - counters its source's
    // rotation rather than copying it.
    SkeletonData skeleton;
    Bone source;
    source.name = "source";
    skeleton.bones.push_back(source); // index 0

    Bone cancel;
    cancel.name = "cancel";
    cancel.appendRotate = true;
    cancel.appendBoneIndex = 0;
    cancel.appendWeight = -1.0f;
    skeleton.bones.push_back(cancel); // index 1

    std::vector<BoneLocalOffset> pose(2);
    const Quat sourceRotation = Quat::FromAxisAngle(Vec3::Forward(), 0.6f);
    pose[0].rotation = sourceRotation;

    ApplyAppendInheritance(skeleton, pose);

    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, sourceRotation.Inverse()))
        << "A -1.0 weight append-rotate bone should end up rotated opposite to its source.";
}

TEST(AppendBoneSolverTests, CascadingAppendChainResolvesInDependencyOrder)
{
    // A -> B (appends from A) -> C (appends from B, non-local so it should
    // see B's OWN appended/total rotation, not just B's raw pre-append
    // value) - proves resolution isn't a single flat pass blind to order.
    SkeletonData skeleton;

    Bone a;
    a.name = "a";
    skeleton.bones.push_back(a); // index 0

    Bone b;
    b.name = "b";
    b.appendRotate = true;
    b.appendBoneIndex = 0;
    b.appendWeight = 1.0f;
    b.appendLocal = false;
    skeleton.bones.push_back(b); // index 1

    Bone c;
    c.name = "c";
    c.appendRotate = true;
    c.appendBoneIndex = 1;
    c.appendWeight = 1.0f;
    c.appendLocal = false; // Wants B's TOTAL (already-appended) rotation.
    skeleton.bones.push_back(c); // index 2

    std::vector<BoneLocalOffset> pose(3);
    const Quat rotationA = Quat::FromAxisAngle(Vec3::Up(), 0.4f);
    pose[0].rotation = rotationA;

    ApplyAppendInheritance(skeleton, pose);

    // B fully inherits A's rotation, and C (non-local) fully inherits B's
    // ALREADY-APPENDED rotation - so C should end up equal to A's rotation
    // too, proving the cascade resolved correctly rather than C only ever
    // seeing B's raw (pre-append, identity) value.
    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, rotationA));
    EXPECT_TRUE(RepresentSameRotation(pose[2].rotation, rotationA));
}

TEST(AppendBoneSolverTests, MalformedAppendDataDoesNotCrashOrHang)
{
    SkeletonData skeleton;

    Bone selfReferencing;
    selfReferencing.name = "self";
    selfReferencing.appendRotate = true;
    selfReferencing.appendBoneIndex = 0; // Points at itself.
    selfReferencing.appendWeight = 1.0f;
    skeleton.bones.push_back(selfReferencing);

    Bone outOfRange;
    outOfRange.name = "bad";
    outOfRange.appendRotate = true;
    outOfRange.appendBoneIndex = 99; // Out of range.
    outOfRange.appendWeight = 1.0f;
    skeleton.bones.push_back(outOfRange);

    std::vector<BoneLocalOffset> pose(2);
    ApplyAppendInheritance(skeleton, pose); // Must simply return, not crash/hang.

    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()));
    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, Quat::Identity()));
}
