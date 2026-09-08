// Unit tests for ApplyDynamicChainPhysicsToPose
// (src/Physics/BoneChainPhysicsResolver.h) - the position -> rotation/
// translation bridge that turns a chain of already-simulated Verlet
// particle positions back into BoneLocalOffset corrections the existing
// FK pipeline already understands (verlet-integration-1 campaign,
// PHASE2_BONE_CHAIN_PHYSICS_BRIDGE.md; anchor-rigidity fix,
// task_manager/verlet-integration-8, Phase 1).
//
// IMPORTANT: per this file's own header comment ("IMPORTANT DESIGN
// NOTE"), a bone's own rotation can never move its own world position -
// only its DESCENDANTS'. For a joint whose tree-parent is ANOTHER chain
// joint, landing it at its target rewrites that PARENT joint's own
// rotation - these tests assert against the PARENT's pose entry for that
// case. For a joint whose tree-parent is the chain's own rootBoneIndex
// (the ANCHOR) directly, the anchor's pose entry is NEVER written at all
// (see DynamicChainDefinition.h's own "NOT simulated" contract) - instead
// the joint bone's OWN local TRANSLATION is corrected, so these tests
// assert against the JOINT's own pose entry for that case instead.

#include "Physics/BoneChainPhysicsResolver.h"

#include "Animation/BoneWorldMatrixQuery.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

// A minimal 2-bone rig: root (bind position at the origin) -> joint (bind
// position one unit straight up). rootBoneIndex is the root bone itself
// (the anchor - its pose entry must never be written); jointBoneIndices
// holds only the joint bone.
SkeletonData BuildTwoBoneSkeleton()
{
    SkeletonData skeleton;

    Bone root;
    root.name = "root";
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    skeleton.bones.push_back(root); // index 0

    Bone joint;
    joint.name = "joint";
    joint.position = Vec3(0.0f, 1.0f, 0.0f);
    joint.parentBoneIndex = 0;
    skeleton.bones.push_back(joint); // index 1

    return skeleton;
}

DynamicChainDefinition BuildSingleJointDefinition()
{
    DynamicChainDefinition definition;
    definition.rootBoneIndex = 0;
    definition.jointBoneIndices = { 1 };
    definition.jointSettings = { DynamicJointSettings{} };
    definition.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(1);
    definition.restLengths = { 1.0f };
    return definition;
}

} // namespace

// Genuine round-trip correctness test (not just "it compiles"): move the
// joint's simulated target SIDEWAYS by the SAME distance as its own bind
// length, confirm the produced TRANSLATION (written into the JOINT bone
// itself, NEVER the anchor - see this file's own header comment) actually
// lands the joint bone there when fed back through ComputeBoneWorldMatrix().
TEST(BoneChainPhysicsResolverTests, ProducedTranslationLandsRootChildBoneAtRequestedSimulatedPositionWithoutTouchingTheAnchor)
{
    SkeletonData skeleton = BuildTwoBoneSkeleton();
    DynamicChainDefinition definition = BuildSingleJointDefinition();

    std::vector<BoneLocalOffset> pose(skeleton.bones.size()); // all-identity (bind pose).
    // Bind offset from root to joint has length 1 along +Y - request the
    // joint instead land at (1,0,0), same length, so the worked numbers below
    // are exact: localBindOffset = (0,1,0); desiredLocalPoint = (1,0,0)
    // (anchor stays Identity); translation = (1,0,0) - (0,1,0) = (1,-1,0).
    const std::vector<Vec3> simulatedPositions = { Vec3(1.0f, 0.0f, 0.0f) };

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()))
        << "Anchor bone's rotation must NEVER be touched by physics.";
    EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3::Zero()))
        << "Anchor bone's translation must NEVER be touched by physics either.";
    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, Quat::Identity()))
        << "A leaf joint bone's own rotation is never written (nothing needs to swing ITS descendants).";
    EXPECT_TRUE(ApproximatelyEqual(pose[1].translation, Vec3(1.0f, -1.0f, 0.0f), 1e-4f))
        << "Joint bone's own translation must carry the full correction.";

    const Mat4 jointWorld = ComputeBoneWorldMatrix(skeleton, pose, 1);
    const Vec3 landedPosition = jointWorld.TransformPoint(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(landedPosition, simulatedPositions[0], 1e-4f))
        << "Bone did not land at the requested simulated position after ApplyDynamicChainPhysicsToPose().";
}

TEST(BoneChainPhysicsResolverTests, TargetAtBindPositionLeavesTranslationNearZeroAndAnchorFullyUntouched)
{
    SkeletonData skeleton = BuildTwoBoneSkeleton();
    DynamicChainDefinition definition = BuildSingleJointDefinition();

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    // Simulated target exactly where the bind pose already is - no
    // translation correction should be necessary.
    const std::vector<Vec3> simulatedPositions = { Vec3(0.0f, 1.0f, 0.0f) };

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()));
    EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3::Zero()));
    EXPECT_TRUE(ApproximatelyEqual(pose[1].translation, Vec3::Zero(), 1e-4f))
        << "A target already at the bind position should require no translation correction.";
}

TEST(BoneChainPhysicsResolverTests, ThreeJointChainAppliesRootToTipInDependencyOrder)
{
    SkeletonData skeleton;
    Bone root;
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    skeleton.bones.push_back(root); // 0

    Bone jointA;
    jointA.position = Vec3(0.0f, 1.0f, 0.0f);
    jointA.parentBoneIndex = 0;
    skeleton.bones.push_back(jointA); // 1

    Bone jointB;
    jointB.position = Vec3(0.0f, 2.0f, 0.0f);
    jointB.parentBoneIndex = 1;
    skeleton.bones.push_back(jointB); // 2

    DynamicChainDefinition definition;
    definition.rootBoneIndex = 0;
    definition.jointBoneIndices = { 1, 2 };
    definition.jointSettings = { DynamicJointSettings{}, DynamicJointSettings{} };
    definition.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(2);
    definition.restLengths = { 1.0f, 1.0f };

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    // Bend the whole chain 90 degrees sideways - jointA lands at (1,0,0) via
    // its OWN translation correction (root/anchor untouched), then jointB
    // (one further rest-length past it) requires jointA's OWN rotation to be
    // written next, to swing jointB into its own target at (2,0,0) - two
    // independent BoneLocalOffset channels on the SAME bone (jointA), written
    // by two different loop iterations.
    const std::vector<Vec3> simulatedPositions = {
        Vec3(1.0f, 0.0f, 0.0f), // jointA's target
        Vec3(2.0f, 0.0f, 0.0f), // jointB's target
    };

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

    const Vec3 jointAWorld = ComputeBoneWorldMatrix(skeleton, pose, 1).TransformPoint(Vec3::Zero());
    const Vec3 jointBWorld = ComputeBoneWorldMatrix(skeleton, pose, 2).TransformPoint(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(jointAWorld, simulatedPositions[0], 1e-4f));
    EXPECT_TRUE(ApproximatelyEqual(jointBWorld, simulatedPositions[1], 1e-4f));

    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()))
        << "Root/anchor bone must NEVER be rotated, even when it indirectly carries a whole sub-chain.";
    EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3::Zero()));
    EXPECT_FALSE(ApproximatelyEqual(pose[1].translation, Vec3::Zero()))
        << "JointA (a direct anchor-child) must have received its own translation correction.";
    EXPECT_FALSE(RepresentSameRotation(pose[1].rotation, Quat::Identity()))
        << "JointA must ALSO have been rotated, to aim its own descendant (jointB) - translation and "
           "rotation on the same bone are independent and must both apply.";
}

TEST(BoneChainPhysicsResolverTests, OutOfRangeJointBoneIndexIsSkippedGracefully)
{
    SkeletonData skeleton = BuildTwoBoneSkeleton();
    DynamicChainDefinition definition;
    definition.rootBoneIndex = 0;
    definition.jointBoneIndices = { 99 }; // out of range
    definition.jointSettings = { DynamicJointSettings{} };
    definition.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(1);
    definition.restLengths = { 1.0f };

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    const std::vector<Vec3> simulatedPositions = { Vec3(1.0f, 0.0f, 0.0f) };

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose); // Must not crash.

    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()));
    EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3::Zero()));
    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, Quat::Identity()));
    EXPECT_TRUE(ApproximatelyEqual(pose[1].translation, Vec3::Zero()));
}

TEST(BoneChainPhysicsResolverTests, OutOfRangeRootBoneIndexIsSkippedGracefully)
{
    SkeletonData skeleton = BuildTwoBoneSkeleton();
    DynamicChainDefinition definition;
    definition.rootBoneIndex = -1; // No real root bone (e.g. a world-anchored chain).
    definition.jointBoneIndices = { 1 };
    definition.jointSettings = { DynamicJointSettings{} };
    definition.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(1);
    definition.restLengths = { 1.0f };

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    const std::vector<Vec3> simulatedPositions = { Vec3(1.0f, 0.0f, 0.0f) };

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose); // Must not crash.

    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()));
    EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3::Zero()));
    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, Quat::Identity()));
    EXPECT_TRUE(ApproximatelyEqual(pose[1].translation, Vec3::Zero()));
}

// task_manager/verlet-integration-8, Phase 1 - this is no longer a "known,
// accepted limitation": it is the isolated, minimal reproduction of the
// exact reported bug (many joints sharing one real anchor bone as their
// direct tree-parent, each independently trying to move a SHARED mutable
// pose entry). A 3-bone skeleton: root=0, jointA=1 child of 0, jointB=2 ALSO
// child of 0, both resolved via parentJointIndex = -1 (their parent is
// rootBoneIndex directly, matching the skeleton).
TEST(BoneChainPhysicsResolverTests, RootLevelHubTranslatesEachChildIndependentlyWithoutOverwritingSiblingsOrTouchingTheSharedAnchor)
{
    SkeletonData skeleton;
    Bone root;
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    skeleton.bones.push_back(root); // 0

    Bone jointA;
    jointA.position = Vec3(0.0f, 1.0f, 0.0f);
    jointA.parentBoneIndex = 0;
    skeleton.bones.push_back(jointA); // 1

    Bone jointB;
    jointB.position = Vec3(0.0f, 1.0f, 0.0f); // Also a direct child of root.
    jointB.parentBoneIndex = 0;
    skeleton.bones.push_back(jointB); // 2

    DynamicChainDefinition definition;
    definition.rootBoneIndex = 0;
    definition.jointBoneIndices = { 1, 2 };
    definition.jointSettings = { DynamicJointSettings{}, DynamicJointSettings{} };
    definition.parentJointIndex = { -1, -1 }; // Both joints' parent is rootBoneIndex directly.
    definition.restLengths = { 1.0f, 1.0f };

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    const std::vector<Vec3> simulatedPositions = {
        Vec3(1.0f, 0.0f, 0.0f), // jointA's target
        Vec3(0.0f, -1.0f, 0.0f), // jointB's DIFFERENT target.
    };

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

    const Vec3 jointAWorld = ComputeBoneWorldMatrix(skeleton, pose, 1).TransformPoint(Vec3::Zero());
    const Vec3 jointBWorld = ComputeBoneWorldMatrix(skeleton, pose, 2).TransformPoint(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(jointAWorld, simulatedPositions[0], 1e-4f))
        << "jointA must land at its OWN target, undisturbed by jointB being processed afterward.";
    EXPECT_TRUE(ApproximatelyEqual(jointBWorld, simulatedPositions[1], 1e-4f))
        << "jointB must land at its OWN (different) target.";
    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()))
        << "The shared anchor's rotation must NEVER be written, regardless of how many children hub off it.";
    EXPECT_TRUE(ApproximatelyEqual(pose[0].translation, Vec3::Zero()));
}

// task_manager/verlet-integration-8, Phase 1 - the most direct possible
// encoding of the reported bug: an anchor bone with a REAL, non-participating
// descendant (a stand-in for "leg"), plus several hub children sharing that
// same anchor, mirroring the reported model's actual shape (~23-25 children
// hanging off a shared "lower body" anchor bone that is also the real FK
// ancestor of the legs). A handful (5) is enough to prove the pattern scales.
TEST(BoneChainPhysicsResolverTests, AnchorThatIsAlsoARealBodyAncestorNeverMovesRegardlessOfHubSize)
{
    // Skeleton: root(0) -> anchor(1, e.g. "lower_body") -> leg(2, a REAL,
    // NON-PARTICIPATING body bone descending from the anchor - never listed
    // in any DynamicChainDefinition::jointBoneIndices) ; anchor(1) is ALSO
    // the direct parent of five independent accessory joints (3..7).
    SkeletonData skeleton;
    Bone root;
    root.position = Vec3(0.0f, 0.0f, 0.0f);
    root.parentBoneIndex = -1;
    skeleton.bones.push_back(root); // 0

    Bone anchor;
    anchor.position = Vec3(0.0f, 1.0f, 0.0f);
    anchor.parentBoneIndex = 0;
    skeleton.bones.push_back(anchor); // 1

    Bone leg;
    leg.position = Vec3(0.0f, -1.0f, 0.0f); // hangs BELOW the anchor, like a real leg would.
    leg.parentBoneIndex = 1;
    skeleton.bones.push_back(leg); // 2

    std::vector<std::int32_t> jointBoneIndices;
    std::vector<Vec3> simulatedPositions;
    for (int k = 0; k < 5; ++k) {
        Bone accessory;
        accessory.position = Vec3(0.0f, 1.0f, 0.0f); // bind-identical to the anchor's own position, like a real hair/skirt root.
        accessory.parentBoneIndex = 1;
        skeleton.bones.push_back(accessory); // 3, 4, 5, 6, 7
        jointBoneIndices.push_back(3 + k);
        // Each accessory swings to its OWN distinct, arbitrary target.
        simulatedPositions.push_back(Vec3(0.1f * static_cast<float>(k), -0.1f * static_cast<float>(k), 1.0f));
    }

    DynamicChainDefinition definition;
    definition.rootBoneIndex = 1;
    definition.jointBoneIndices = jointBoneIndices;
    definition.jointSettings.assign(jointBoneIndices.size(), DynamicJointSettings{});
    definition.parentJointIndex.assign(jointBoneIndices.size(), -1); // every accessory is a direct anchor-child.
    definition.restLengths.assign(jointBoneIndices.size(), 1.0f);

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    const Vec3 legWorldBefore = ComputeBoneWorldMatrix(skeleton, pose, 2).TransformPoint(Vec3::Zero());

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose);

    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, Quat::Identity()))
        << "The shared anchor must never be rotated, no matter how many accessories hub off it.";
    EXPECT_TRUE(ApproximatelyEqual(pose[1].translation, Vec3::Zero()));

    const Vec3 legWorldAfter = ComputeBoneWorldMatrix(skeleton, pose, 2).TransformPoint(Vec3::Zero());
    EXPECT_TRUE(ApproximatelyEqual(legWorldAfter, legWorldBefore, 1e-5f))
        << "A real, non-participating body bone descending from the anchor must not move AT ALL - "
           "this is the exact reported 'whole body looks ragdoll-simulated' bug.";

    for (std::size_t k = 0; k < jointBoneIndices.size(); ++k) {
        const Vec3 landed = ComputeBoneWorldMatrix(skeleton, pose, jointBoneIndices[k]).TransformPoint(Vec3::Zero());
        EXPECT_TRUE(ApproximatelyEqual(landed, simulatedPositions[k], 1e-4f))
            << "Accessory joint " << k << " did not land at its own independent target.";
    }
}

// task_manager/verlet-integration-6, Phase 1 - regression test proving the
// corrected top-of-function guard (BoneChainPhysicsResolver.cpp) actually
// prevents an out-of-bounds parentJointIndex read for stale/malformed data,
// rather than the rejected per-iteration `continue` guard an earlier draft
// of this campaign's own strategy document would have produced instead (see
// PHASE0_MASTER_STRATEGY.md's own Revision Notes (v2), finding #7).
TEST(BoneChainPhysicsResolverTests, MismatchedParentJointIndexSizeIsIgnoredGracefully)
{
    SkeletonData skeleton = BuildTwoBoneSkeleton();
    DynamicChainDefinition definition = BuildSingleJointDefinition();
    definition.parentJointIndex.clear(); // Simulate stale/malformed data.

    std::vector<BoneLocalOffset> pose(skeleton.bones.size());
    const std::vector<Vec3> simulatedPositions = { Vec3(1.0f, 0.0f, 0.0f) };

    ApplyDynamicChainPhysicsToPose(skeleton, definition, simulatedPositions, pose); // Must not crash.

    EXPECT_TRUE(RepresentSameRotation(pose[0].rotation, Quat::Identity()))
        << "A mismatched parentJointIndex size must leave the pose completely untouched.";
    EXPECT_TRUE(RepresentSameRotation(pose[1].rotation, Quat::Identity()));
}

} // namespace gte
