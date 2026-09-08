// task_manager/verlet-integration-10, PHASE5 - the campaign's own final,
// real, FULL-PIPELINE regression test proving PHASE1 (PMX collision-group/
// layer filtering), PHASE2 (joint's own PMX rigid-body shape as collision
// radius), and PHASE3 (collision enabled ON by default) all work correctly
// TOGETHER, through the REAL PhysicsSystem::RegisterDynamicChains() ->
// AttachDynamicChainRigIfNeeded() -> Update() pipeline - not just isolated
// solver-level unit math the way PHASE1/PHASE2's own test files exercise
// them. Mirrors tests/Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp's
// own overall shape/discipline (a synthetic model, a Static anchor, a
// gravity-neutral "deep initial penetration" fixture, and independently
// re-derived IsOutsideSphere()/IsOutsideBox() verification helpers that never
// call the production Solve*Collision()/GroupsMayCollide()/GroupBit()
// functions directly) - see PHASE0_MASTER_STRATEGY.md's own Revision Notes
// finding #2 for why this file writes its OWN local MakeRigidBody()/
// MakeJoint() fixture helpers rather than reusing another file's private,
// narrower ones.
//
// The ONE deliberate, load-bearing difference from
// PhysicsSystemMultiShapeColliderTests.cpp's own template: every detected
// chain's own `collisionEnabled` is left COMPLETELY UNTOUCHED here (never
// force-set by this file's own fixture helper) - this is what actually
// proves PHASE3's default-true flip end to end, rather than merely
// re-exercising PHASE1/PHASE2 behind an explicit opt-in a real freshly-
// imported model would never need to make.

#include "Game/Physics/PhysicsSystem.h"

#include "Animation/BoneWorldMatrixQuery.h"
#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "Game/Animation/SkeletalRigCache.h"
#include "Math/MathTypes.h"
#include "Math/Quat.h"
#include "Math/Vec3.h"
#include "Physics/DynamicChainDefinition.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <string>
#include <vector>

namespace gte {
namespace {

// This file's OWN local fixture helpers - see this file's own header comment
// for why DynamicChainDetectionTests.cpp's/PhysicsSystemMultiShapeColliderTests.cpp's
// own private MakeRigidBody()/MakeJoint() cannot be reused (neither exposes a
// group/collisionGroupMask parameter, which this file's whole point is to
// exercise).
RigidBody MakeRigidBody(std::int32_t boneIndex, RigidBodyMotionType motionType, RigidBodyShape shape,
    const Vec3& shapeSize, std::uint8_t group, std::uint16_t collisionGroupMask,
    const Vec3& translate = Vec3::Zero(), const Vec3& rotateRadians = Vec3::Zero())
{
    RigidBody body;
    body.boneIndex = boneIndex;
    body.motionType = motionType;
    body.shape = shape;
    body.shapeSize = shapeSize;
    body.group = group;
    body.collisionGroupMask = collisionGroupMask;
    body.translate = translate;
    body.rotateRadians = rotateRadians;
    return body;
}

Joint MakeJoint(std::int32_t rigidBodyAIndex, std::int32_t rigidBodyBIndex)
{
    Joint joint;
    joint.rigidBodyAIndex = rigidBodyAIndex;
    joint.rigidBodyBIndex = rigidBodyBIndex;
    return joint;
}

// --- Fixture geometry -------------------------------------------------------
//
// A 2-joint linear chain (anchor -> joint1 -> joint2) hanging from a Static
// PMX anchor, deliberately built so BOTH joints' own bind positions sit
// exactly (joint1) or nearly (joint2) inside two overlapping Static
// colliders - a Sphere (Collider A) and a Box (Collider B) - a deep,
// deliberate initial penetration for both, mirroring
// PhysicsSystemMultiShapeColliderTests.cpp's own "deep initial penetration,
// zero gravity/stiffness, deterministic outcome" fixture-construction
// technique (see that file's own header comment for exactly why a
// gravity-convergence fixture is unreliable and a deep-initial-penetration
// one is preferred - this is also the exact defect that made this campaign's
// own PHASE1 test file, tests/Physics/DynamicChainSolverCollisionGroupFilterTests.cpp,
// need a fix in this same phase - see this phase's own completion report).
//
// Collision-group/mask setup (task_manager/verlet-integration-10, PHASE1):
// - Joint 1's own PMX Dynamic rigid body is in group 5, with a mask that
//   refuses group 6 specifically (every other bit set) - Collider A is group
//   6, so joint 1 must NEVER collide with Collider A, by PMX's own
//   collision-group/layer rule.
// - Joint 2's own PMX Dynamic rigid body is ALSO group 5, but with an
//   unrestricted (0xFFFF) mask - the deliberate contrast this test needs:
//   joint 2 collides normally against EVERYTHING, including Collider A.
// - Collider B is group 0 with an unrestricted mask - an ordinary,
//   unrestricted collider BOTH joints must avoid normally, this test's own
//   "collision genuinely still works at all, with real PHASE2 radius
//   inflation" sanity control.
//
// Both joints' own PMX Dynamic rigid bodies also carry a non-trivial
// Sphere shapeSize.x = kJointCollisionRadius (task_manager/verlet-integration-10,
// PHASE2), so DeriveJointCollisionRadius() has real, non-zero effect to
// verify - assertion 4 below explicitly confirms the resulting push-out
// distance is inflated by this amount, not merely "outside by some
// unspecified margin".
constexpr std::int32_t kRootBoneIndex = 0;
constexpr std::int32_t kAnchorBoneIndex = 1;
constexpr std::int32_t kJoint1BoneIndex = 2;
constexpr std::int32_t kJoint2BoneIndex = 3;
constexpr std::int32_t kColliderABoneIndex = 4;
constexpr std::int32_t kColliderBBoneIndex = 5;

const Vec3 kAnchorBindPosition(0.0f, -0.2f, 0.0f);
const Vec3 kJoint1BindPosition(0.0f, -0.5f, 0.0f);
const Vec3 kJoint2BindPosition(0.2f, -0.5f, 0.0f);

// Collider A (Sphere) - centered exactly on joint 1's own bind position (deep
// penetration for joint 1), but also well within reach of joint 2's own bind
// position (only 0.2 away) - joint 2's own unrestricted mask means it must
// ALSO end up resolved out of this sphere, proving the group/mask filter
// only blocks joint 1 specifically, never joint 2.
const Vec3 kColliderAWorldCenter = kJoint1BindPosition;
constexpr float kColliderARadius = 1.0f;
constexpr std::uint8_t kColliderAGroup = 6;

// Collider B (Box) - centered at the midpoint between both joints' own bind
// positions, with half-extents comfortably covering both (deep penetration
// for BOTH joints) - the "ordinary, unrestricted collider both joints should
// hit normally" sanity control (see this file's own header comment above).
const Vec3 kColliderBWorldCenter(0.1f, -0.5f, 0.0f);
const Vec3 kColliderBHalfExtents(0.3f, 0.3f, 0.3f);
constexpr std::uint8_t kColliderBGroup = 0;

constexpr std::uint8_t kJointGroup = 5;
constexpr std::uint16_t kJoint1CollisionMask = 0xFFBFu; // every bit set EXCEPT bit 6 (0x40) - refuses Collider A's own group.
constexpr std::uint16_t kJoint2CollisionMask = 0xFFFFu; // unrestricted - the deliberate contrast with joint 1.
constexpr float kJointCollisionRadius = 0.2f; // each joint's own Sphere rigid body shapeSize.x.

constexpr float kPenetrationTolerance = 1e-3f;

// `joint2Group` is parameterized purely for this file's own v2 (see
// PHASE0_MASTER_STRATEGY.md's Revision Notes finding #2) out-of-range-group
// full-pipeline regression test below - every other test in this file calls
// this with the default, legitimate, in-range value.
SkinnedMeshData BuildFixture(std::uint8_t joint2Group = kJointGroup)
{
    SkinnedMeshData data;

    Bone root;
    root.position = Vec3::Zero();
    root.parentBoneIndex = -1;
    data.skeleton.bones.push_back(root); // 0

    Bone anchor;
    anchor.position = kAnchorBindPosition;
    anchor.parentBoneIndex = kRootBoneIndex;
    data.skeleton.bones.push_back(anchor); // 1

    Bone joint1;
    joint1.position = kJoint1BindPosition;
    joint1.parentBoneIndex = kAnchorBoneIndex;
    data.skeleton.bones.push_back(joint1); // 2

    // task_manager/verlet-integration-10, PHASE5 - joint2 is a DIRECT sibling
    // child of the anchor (never joint1's own skeleton child) - this is what
    // keeps joint1/joint2 mutually INDEPENDENT (only an anchor->joint
    // distance constraint each, never a joint1->joint2 one), which is what
    // finally made this fixture converge to a stable, deterministic
    // equilibrium: an earlier revision of this file made joint2 a skeleton
    // CHILD of joint1 (a single-strand chain), which meant every collision
    // push on EITHER joint also tugged the other one via their shared
    // distance constraint, producing an unstable/drifting result over 200
    // steps instead of a clean, predictable one (see this phase's own
    // completion report for the concrete numbers that first exposed this).
    // physics.joints below still connects joint1Body -> joint2Body (never an
    // anchor->joint2Body PMX Joint) - reachability (Step B of
    // DynamicChainDetection.cpp) is graph-based via PhysicsData::joints,
    // completely independent of the tree topology Step D derives purely from
    // walking the real SKELETON ancestor chain - so joint2 stays correctly
    // "reachable" from the Static anchor either way.
    Bone joint2;
    joint2.position = kJoint2BindPosition;
    joint2.parentBoneIndex = kAnchorBoneIndex;
    data.skeleton.bones.push_back(joint2); // 3

    Bone colliderABone;
    colliderABone.position = Vec3::Zero(); // deliberately NOT coincident with kColliderAWorldCenter.
    colliderABone.parentBoneIndex = kRootBoneIndex;
    data.skeleton.bones.push_back(colliderABone); // 4

    Bone colliderBBone;
    colliderBBone.position = Vec3::Zero(); // deliberately NOT coincident with kColliderBWorldCenter.
    colliderBBone.parentBoneIndex = kRootBoneIndex;
    data.skeleton.bones.push_back(colliderBBone); // 5

    PhysicsData physics;

    RigidBody anchorBody = MakeRigidBody(kAnchorBoneIndex, RigidBodyMotionType::Static, RigidBodyShape::Sphere,
        Vec3::Zero(), /*group=*/0, /*collisionGroupMask=*/0xFFFFu); // irrelevant - an anchor is never itself collision-tested.
    physics.rigidBodies.push_back(anchorBody); // 0

    RigidBody joint1Body = MakeRigidBody(kJoint1BoneIndex, RigidBodyMotionType::Dynamic, RigidBodyShape::Sphere,
        Vec3(kJointCollisionRadius, 0.0f, 0.0f), kJointGroup, kJoint1CollisionMask);
    physics.rigidBodies.push_back(joint1Body); // 1

    RigidBody joint2Body = MakeRigidBody(kJoint2BoneIndex, RigidBodyMotionType::Dynamic, RigidBodyShape::Sphere,
        Vec3(kJointCollisionRadius, 0.0f, 0.0f), joint2Group, kJoint2CollisionMask);
    physics.rigidBodies.push_back(joint2Body); // 2

    physics.joints.push_back(MakeJoint(0, 1)); // anchor -> joint1
    physics.joints.push_back(MakeJoint(1, 2)); // joint1 -> joint2

    RigidBody colliderABody = MakeRigidBody(kColliderABoneIndex, RigidBodyMotionType::Static, RigidBodyShape::Sphere,
        Vec3(kColliderARadius, 0.0f, 0.0f), kColliderAGroup, /*collisionGroupMask=*/0xFFFFu, kColliderAWorldCenter);
    physics.rigidBodies.push_back(colliderABody); // 3 - not referenced by any Joint (a pure collision obstacle).

    RigidBody colliderBBody = MakeRigidBody(kColliderBBoneIndex, RigidBodyMotionType::Static, RigidBodyShape::Box,
        kColliderBHalfExtents, kColliderBGroup, /*collisionGroupMask=*/0xFFFFu, kColliderBWorldCenter);
    physics.rigidBodies.push_back(colliderBBody); // 4

    data.physics = std::move(physics);
    return data;
}

// Registers `data` under `path`, forces every joint's own `stiffness` to 0
// (the goal constraint must never pull a joint away from wherever
// collision/structural relaxation left it - see BuildFixture()'s own header
// comment) and `damping` to 1.0 (fully kill any implied velocity every step),
// attaches the rig, and seeds a pure bind-pose ResolvedAnimationPose (the
// chain is never animated; with gravity zero too, ONLY collision can ever
// move a joint away from its own bind position in this fixture).
//
// Deliberately NEVER touches `chain.collisionEnabled` - see this file's own
// header comment for why that is the ONE load-bearing difference from
// PhysicsSystemMultiShapeColliderTests.cpp's own
// RegisterAttachAndConfigure() template.
Entity RegisterAttachAndConfigure(PhysicsSystem& physicsSystem, Registry& registry, const std::string& path, const SkinnedMeshData& data)
{
    physicsSystem.RegisterDynamicChains(path, data);

    DynamicChainRigCache::ModelEntry* entry = physicsSystem.GetDynamicChainRigCache().TryGetMutable(path);
    if (entry != nullptr) {
        for (DynamicChainDefinition& chain : entry->chains) {
            for (DynamicJointSettings& jointSettings : chain.jointSettings) {
                jointSettings.stiffness = 0.0f;
                jointSettings.damping = 1.0f;
            }
        }
    }

    const Entity entity = registry.CreateEntity();
    physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, path);
    registry.AddComponent<Transform>(entity); // identity.

    ResolvedAnimationPose& pose = registry.AddComponent<ResolvedAnimationPose>(entity);
    pose.pose.resize(data.skeleton.bones.size()); // pure bind pose - never animated.

    return entity;
}

// --- Independent (re-derived, NOT calling the production Solve*Collision()/
// GroupsMayCollide()/GroupBit() functions) shape-vs-point tests, mirroring
// PhysicsSystemMultiShapeColliderTests.cpp's own precedent exactly. ---------

bool IsOutsideSphere(const Vec3& point, const Vec3& center, float radius)
{
    return Length(point - center) >= radius - kPenetrationTolerance;
}

bool IsOutsideBox(const Vec3& point, const Vec3& center, const Vec3& halfExtents)
{
    // Neither fixture collider is ever rotated (identity rotateRadians
    // throughout BuildFixture()), so no inverse-rotate step is needed here -
    // the local-space check itself still mirrors BoxCollider.cpp's own logic
    // independently, never calling it directly.
    const Vec3 local = point - center;
    return std::fabs(local.x) >= halfExtents.x - kPenetrationTolerance || std::fabs(local.y) >= halfExtents.y - kPenetrationTolerance
        || std::fabs(local.z) >= halfExtents.z - kPenetrationTolerance;
}

} // namespace

// The campaign's own primary end-to-end proof: PHASE1 (group/mask
// filtering), PHASE2 (joint radius inflation), and PHASE3 (collision-on-by-
// default) all working together, through the REAL PhysicsSystem pipeline.
TEST(PhysicsSystemCollisionGroupAndRadiusEndToEndTests, GroupMaskFilteringAndJointRadiusWorkTogetherWithDefaultOnCollision)
{
    PhysicsSystem physicsSystem;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3::Zero(); // see BuildFixture()'s own determinism argument.

    Registry registry;
    const std::string path = "CollisionGroupAndRadiusModel.gta";
    const SkinnedMeshData data = BuildFixture();
    const Entity entity = RegisterAttachAndConfigure(physicsSystem, registry, path, data);

    const DynamicChainRigCache::ModelEntry* model = physicsSystem.GetDynamicChainRigCache().TryGet(path);
    ASSERT_NE(model, nullptr);
    ASSERT_EQ(model->chains.size(), 1u) << "Expected exactly one detected chain (anchor(1) -> joint1(2) -> joint2(3)).";
    ASSERT_EQ(model->chains[0].jointBoneIndices.size(), 2u);
    ASSERT_EQ(model->colliders.size(), 2u) << "Expected both Static rigid bodies (Sphere A, Box B) to be auto-detected.";

    // Assertion 1 (Step 3.1, point 1) - PHASE3's default-true flip, proven
    // explicitly BEFORE stepping anything, for a chain that never had
    // `collisionEnabled` touched anywhere in this file's own fixture.
    ASSERT_TRUE(model->chains[0].collisionEnabled)
        << "task_manager/verlet-integration-10, PHASE3 - a freshly-detected chain must collide by default, with no "
           "explicit opt-in anywhere in this test's own fixture.";

    for (int step = 0; step < 200; ++step) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(pose, nullptr);

    const Vec3 joint1Position = ComputeBoneWorldMatrix(data.skeleton, pose->pose, kJoint1BoneIndex).TransformPoint(Vec3::Zero());
    const Vec3 joint2Position = ComputeBoneWorldMatrix(data.skeleton, pose->pose, kJoint2BoneIndex).TransformPoint(Vec3::Zero());

    // Assertion 2 (Step 3.1, point 2) - joint 2 (unrestricted mask) ends up
    // outside BOTH Collider A and Collider B.
    EXPECT_TRUE(IsOutsideSphere(joint2Position, kColliderAWorldCenter, kColliderARadius))
        << "Joint 2 (unrestricted collision mask) ended up inside Collider A.";
    EXPECT_TRUE(IsOutsideBox(joint2Position, kColliderBWorldCenter, kColliderBHalfExtents))
        << "Joint 2 (unrestricted collision mask) ended up inside Collider B.";

    // Assertion 3 (Step 3.1, point 3) - joint 1 (refuses Collider A's own
    // group) ends up outside Collider B (an ordinary, unrestricted collider
    // it must still hit normally) but is NOT required to be outside Collider
    // A - and this test explicitly confirms it GENUINELY still penetrates
    // Collider A, mirroring PhysicsSystemMultiShapeColliderTests.cpp's own
    // "SameFixtureWithCollisionDisabledDoesPenetrateProvingTheFixtureNeedsCollision"
    // precedent: a filter that silently failed to block anything would make
    // this EXPECT_FALSE fail, since joint 1 would then have been pushed out
    // of Collider A exactly like joint 2 was.
    EXPECT_TRUE(IsOutsideBox(joint1Position, kColliderBWorldCenter, kColliderBHalfExtents))
        << "Joint 1 must still collide normally against Collider B - an ordinary, unrestricted collider.";
    EXPECT_FALSE(IsOutsideSphere(joint1Position, kColliderAWorldCenter, kColliderARadius))
        << "Joint 1's own PMX collision-group/mask rule must have blocked it from Collider A, so it should still be "
           "genuinely penetrating that sphere - if this assertion fails, either the group/mask filter regressed "
           "(silently stopped blocking this pair) or this fixture's own geometry no longer produces a genuine "
           "penetration to prove the filter actually did something.";

    // Assertion 4 (Step 3.1, point 4) - PHASE2's radius inflation: both
    // joints must be pushed out of Collider B by MORE than a bare
    // zero-radius point would have needed. Since both joints' own bind
    // positions share Collider B's own y/z coordinates, BoxCollider.cpp's own
    // "push along the smallest-escape axis" always lands exactly at
    // (Collider B's own smallest half-extent + the joint's own
    // collisionRadius) away from its center - comparing against the RAW
    // (zero-radius) half-extent directly proves the inflation is genuinely
    // present, not merely "outside by some unspecified margin".
    const float kColliderBSmallestHalfExtent = std::min({ kColliderBHalfExtents.x, kColliderBHalfExtents.y, kColliderBHalfExtents.z });
    const float joint1DistanceFromColliderBCenter = Length(joint1Position - kColliderBWorldCenter);
    const float joint2DistanceFromColliderBCenter = Length(joint2Position - kColliderBWorldCenter);
    EXPECT_GE(joint1DistanceFromColliderBCenter, kColliderBSmallestHalfExtent + kJointCollisionRadius - kPenetrationTolerance)
        << "Joint 1's own PMX-derived collision radius must inflate its push-out distance from Collider B beyond the "
           "bare zero-radius half-extent (" << kColliderBSmallestHalfExtent << ").";
    EXPECT_GE(joint2DistanceFromColliderBCenter, kColliderBSmallestHalfExtent + kJointCollisionRadius - kPenetrationTolerance)
        << "Joint 2's own PMX-derived collision radius must inflate its push-out distance from Collider B beyond the "
           "bare zero-radius half-extent (" << kColliderBSmallestHalfExtent << ").";
}

// task_manager/verlet-integration-10, PHASE0_MASTER_STRATEGY.md's own v2
// Revision Notes finding #2 - a direct, FULL-PIPELINE regression guard for
// the shift-safety fix (PHASE1's own GroupBit()): a deliberately
// out-of-documented-range `group` value on a real PMX rigid body, flowing all
// the way from PhysicsData::rigidBodies through DetectDynamicChains() and
// PhysicsSystem::Update()'s own resolved Colliders, must produce a result
// IDENTICAL to the same fixture authored with the legitimate, in-range value
// its own low 4 bits alias - never merely "does not crash", the stronger,
// more useful proof PHASE1's own isolated solver-level unit test already
// established (tests/Physics/DynamicChainSolverCollisionGroupFilterTests.cpp's
// own OutOfRangeGroupValueNeverCrashesAndStillMasksToTheCorrectLowFourBits),
// now re-confirmed through the FULL pipeline.
TEST(PhysicsSystemCollisionGroupAndRadiusEndToEndTests, OutOfRangeGroupValueProducesIdenticalOutcomeThroughFullPipeline)
{
    const auto runFixtureAndReturnJoint2FinalPosition = [](std::uint8_t joint2Group, const char* path) -> Vec3 {
        PhysicsSystem physicsSystem;
        physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3::Zero();

        Registry registry;
        const SkinnedMeshData data = BuildFixture(joint2Group);
        const Entity entity = RegisterAttachAndConfigure(physicsSystem, registry, path, data);

        for (int step = 0; step < 200; ++step) {
            physicsSystem.Update(registry, 1.0 / 60.0);
        }

        const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        return ComputeBoneWorldMatrix(data.skeleton, pose->pose, kJoint2BoneIndex).TransformPoint(Vec3::Zero());
    };

    // group = 21 (0b10101) & 0x0F == 5, the exact same low 4 bits as the
    // legitimate group-5 value every other test in this file already uses.
    const Vec3 legitimateResult = runFixtureAndReturnJoint2FinalPosition(/*joint2Group=*/5, "CollisionGroupMaskingRegressionModel_Legit.gta");
    const Vec3 outOfRangeResult
        = runFixtureAndReturnJoint2FinalPosition(/*joint2Group=*/21, "CollisionGroupMaskingRegressionModel_OutOfRange.gta");

    EXPECT_TRUE(std::isfinite(outOfRangeResult.x));
    EXPECT_TRUE(std::isfinite(outOfRangeResult.y));
    EXPECT_TRUE(std::isfinite(outOfRangeResult.z));

    constexpr float kEqualityTolerance = 1e-4f;
    EXPECT_NEAR(legitimateResult.x, outOfRangeResult.x, kEqualityTolerance)
        << "An out-of-range group value sharing the same low 4 bits must produce an IDENTICAL outcome through the "
           "FULL pipeline, not just PHASE1's own isolated solver-level unit test.";
    EXPECT_NEAR(legitimateResult.y, outOfRangeResult.y, kEqualityTolerance);
    EXPECT_NEAR(legitimateResult.z, outOfRangeResult.z, kEqualityTolerance);
}

} // namespace gte
