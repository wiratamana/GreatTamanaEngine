// Unit tests for PhysicsSystem's PHASE4 runtime world-space collider
// resolution (task_manager/verlet-integration-9,
// PHASE4_RUNTIME_WORLD_SPACE_COLLIDER_RESOLUTION_AND_WIRING.md) - proving
// that every one of a model's Static-rigid-body colliders (PHASE3's
// ModelColliderDefinition, cached per-model) is genuinely re-resolved to its
// CURRENT world-space center/rotation every frame (never a stale/cached
// value), correctly composing THREE independent transforms in the process:
// (a) the tracked bone's own CURRENT animated pose, (b) the collider's own
// FIXED bind-pose-relative local offset (position + rotation) from that bone
// (PHASE3), and (c) the owning ECS entity's own resolved world Transform
// (PHASE3 of verlet-integration-7).
//
// Every test below runs the REAL PhysicsSystem::Update() pipeline end to end
// (registration -> per-frame resolution -> DynamicChainSolver::StepDynamicChain())
// - this is a companion to PHASE6's own broader end-to-end multi-shape
// regression test (PhysicsSystemMultiShapeColliderTests.cpp), which proves
// the OUTCOME of colliding against a real Sphere+Box+Capsule trio; THIS file
// focuses specifically on the RESOLUTION MATH itself (does the collider
// track its bone, does it respect its own authored bind-pose offset, is it
// skipped entirely when uninteresting, does an entity-level rotation compose
// into it correctly).
//
// Every test below deliberately neutralizes every OTHER simulation force
// (gravity = 0, DynamicJointSettings::stiffness = 0 - disables the goal
// constraint's pull toward the animated target, DynamicJointSettings::damping
// = 1.0 - fully kills any implied "slide" velocity a prior collision push
// would otherwise carry into the next frame, DynamicChainDefinition::
// constraintIterations = 0 - disables structural/distance-constraint
// relaxation entirely) so the ONLY thing that can ever move a joint particle
// in these tests is the collision-resolution step itself - this isolates
// PHASE4's own resolution math from the rest of the solver's dynamics, which
// is already covered by its own dedicated test files
// (DynamicChainSolverTests.cpp, DynamicChainSolverIdleSettlingTests.cpp).

#include "Game/Physics/PhysicsSystem.h"

#include "Animation/BoneWorldMatrixQuery.h"
#include "ECS/Components/DynamicChainRig.h"
#include "ECS/Components/ResolvedAnimationPose.h"
#include "ECS/Components/Transform.h"
#include "ECS/Registry.h"
#include "ECS/TransformHierarchy.h"
#include "Game/Animation/SkeletalRigCache.h"
#include "Math/Mat4.h"
#include "Math/MathTypes.h"
#include "Math/Quat.h"
#include "Math/Vec3.h"
#include "Physics/DynamicChainDefinition.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gte {
namespace {

constexpr std::int32_t kRootBoneIndex = 0;
constexpr std::int32_t kChainRootBoneIndex = 1; // the chain's own Static anchor.
constexpr std::int32_t kJointBoneIndex = 2;      // the chain's first Dynamic joint - every test's own point of interest.
constexpr std::int32_t kSecondJointBoneIndex = 3; // a second, deliberately inert Dynamic joint - see BuildFixture()'s own comment.
constexpr std::int32_t kColliderBoneIndex = 4;   // an UNRELATED Static rigid body - a collider candidate.

// Deliberately far from anything else in every fixture below (no collider
// this file builds ever reaches out this far), and never touched by
// StepDynamicChain() in any way that matters here since every chain's own
// constraintIterations is forced to 0 (see
// RegisterAttachSeedPoseAndConfigureChain() below) - it exists PURELY to
// satisfy DynamicChainDetectionDefaults::minimumChainLength (2), so
// DetectDynamicChains() doesn't discard this file's single-joint-of-interest
// chain as "too short" (see DynamicChainDetection.cpp, Step G).
constexpr Vec3 kSecondJointBonePositionOffset(1000.0f, 1000.0f, 1000.0f);

// Builds a minimal synthetic model: root(0) -> chainRoot(1, Static, this
// chain's anchor) -> joint1(2, Dynamic) -> joint2(3, Dynamic, deliberately
// inert - see kSecondJointBonePositionOffset's own comment) forms a
// two-joint dynamic chain (satisfies minimumChainLength - see
// DynamicChainDetection.h), plus an entirely independent colliderBone(4,
// parented directly under root) carrying its own Static rigid body of
// `shape` - never referenced by any Joint, so DetectDynamicChains() correctly
// ignores it as inert while DetectModelColliders() (PHASE3) correctly picks
// it up as a collision obstacle candidate, exactly like a real model's
// unrelated hit-box rigid body sitting alongside its jiggle-bone rigid
// bodies.
SkinnedMeshData BuildFixture(const Vec3& jointBonePosition, const Vec3& colliderBonePosition, RigidBodyShape shape,
    const Vec3& shapeSize, const Vec3& bodyTranslate, const Vec3& bodyRotateRadians)
{
    SkinnedMeshData data;

    Bone root;
    root.position = Vec3::Zero();
    root.parentBoneIndex = -1;
    data.skeleton.bones.push_back(root); // 0

    Bone chainRoot;
    chainRoot.position = Vec3::Zero();
    chainRoot.parentBoneIndex = kRootBoneIndex;
    data.skeleton.bones.push_back(chainRoot); // 1

    Bone joint1;
    joint1.position = jointBonePosition;
    joint1.parentBoneIndex = kChainRootBoneIndex;
    data.skeleton.bones.push_back(joint1); // 2

    Bone joint2;
    joint2.position = jointBonePosition + kSecondJointBonePositionOffset;
    joint2.parentBoneIndex = kJointBoneIndex;
    data.skeleton.bones.push_back(joint2); // 3

    Bone colliderBone;
    colliderBone.position = colliderBonePosition;
    colliderBone.parentBoneIndex = kRootBoneIndex;
    data.skeleton.bones.push_back(colliderBone); // 4

    PhysicsData physics;

    RigidBody anchorBody;
    anchorBody.boneIndex = kChainRootBoneIndex;
    anchorBody.motionType = RigidBodyMotionType::Static;
    physics.rigidBodies.push_back(anchorBody); // 0

    RigidBody jointBody;
    jointBody.boneIndex = kJointBoneIndex;
    jointBody.motionType = RigidBodyMotionType::Dynamic;
    physics.rigidBodies.push_back(jointBody); // 1

    RigidBody secondJointBody;
    secondJointBody.boneIndex = kSecondJointBoneIndex;
    secondJointBody.motionType = RigidBodyMotionType::Dynamic;
    physics.rigidBodies.push_back(secondJointBody); // 2

    Joint anchorToJoint;
    anchorToJoint.rigidBodyAIndex = 0;
    anchorToJoint.rigidBodyBIndex = 1;
    physics.joints.push_back(anchorToJoint);

    Joint joint1ToJoint2;
    joint1ToJoint2.rigidBodyAIndex = 1;
    joint1ToJoint2.rigidBodyBIndex = 2;
    physics.joints.push_back(joint1ToJoint2);

    RigidBody colliderBody;
    colliderBody.boneIndex = kColliderBoneIndex;
    colliderBody.motionType = RigidBodyMotionType::Static;
    colliderBody.shape = shape;
    colliderBody.shapeSize = shapeSize;
    colliderBody.translate = bodyTranslate;
    colliderBody.rotateRadians = bodyRotateRadians;
    physics.rigidBodies.push_back(colliderBody); // 3 - not referenced by any Joint.

    data.physics = std::move(physics);
    return data;
}

// Registers `data`, mutates every detected chain's tuning so ONLY collision
// can ever move a joint particle (see this file's own header comment), sets
// `collisionEnabled` per the caller's request, attaches the rig, and seeds a
// pure bind-pose ResolvedAnimationPose - mirrors
// PhysicsSystemWorldSpaceRootMotionTests.cpp's own
// RegisterAttachAndSeedPose() helper, extended with this file's own
// collision-isolating chain mutation.
Entity RegisterAttachSeedPoseAndConfigureChain(
    PhysicsSystem& physicsSystem, Registry& registry, const std::string& path, const SkinnedMeshData& data, bool collisionEnabled)
{
    physicsSystem.RegisterDynamicChains(path, data);

    DynamicChainRigCache::ModelEntry* entry = physicsSystem.GetDynamicChainRigCache().TryGetMutable(path);
    if (entry != nullptr) {
        for (DynamicChainDefinition& chain : entry->chains) {
            chain.collisionEnabled = collisionEnabled;
            chain.constraintIterations = 0; // isolate collision from structural relaxation.
            for (DynamicJointSettings& jointSettings : chain.jointSettings) {
                jointSettings.stiffness = 0.0f; // disable the goal constraint's pull toward the animated target.
                jointSettings.damping = 1.0f;   // fully kill any implied velocity every step.
            }
        }
    }

    const Entity entity = registry.CreateEntity();
    physicsSystem.AttachDynamicChainRigIfNeeded(registry, entity, path);

    ResolvedAnimationPose& pose = registry.AddComponent<ResolvedAnimationPose>(entity);
    pose.pose.resize(data.skeleton.bones.size()); // pure bind pose.

    return entity;
}

// Reconstructs a bone's CURRENT world position exactly the way
// PhysicsSystem::Update() itself does - the entity's own resolved
// (scale-free) world transform composed with the bone-local pose.
Vec3 ReconstructBoneWorldPosition(
    Registry& registry, Entity entity, const SkeletonData& skeleton, const std::vector<BoneLocalOffset>& pose, std::int32_t boneIndex)
{
    const Transform worldTransform = ComputeWorldTransform(registry, entity);
    const Mat4 entityWorldMatrix = Mat4::TRS(worldTransform.position, worldTransform.rotation, Vec3::One());
    const Mat4 boneWorld = entityWorldMatrix * ComputeBoneWorldMatrix(skeleton, pose, boneIndex);
    return boneWorld.TransformPoint(Vec3::Zero());
}

TEST(PhysicsSystemModelColliderResolutionTests, ColliderTracksItsBoneAsTheBoneAnimates)
{
    PhysicsSystem physicsSystem;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3::Zero();

    const Vec3 jointBonePosition(2.0f, 0.0f, 0.0f);
    const Vec3 colliderBonePosition = jointBonePosition; // coincides with the joint's own bind position at bind pose.
    constexpr float kRadius = 1.0f;
    // bodyTranslate == colliderBonePosition -> this rigid body's own
    // authored ABSOLUTE bind-pose position (Assets/PhysicsData.h's own
    // "translate is model-local, independent of which bone tracks it"
    // convention) exactly coincides with colliderBone's own bind position,
    // so this collider's localOffsetPosition works out to exactly zero -
    // its resolved world center is simply "wherever colliderBone currently is".
    const SkinnedMeshData data = BuildFixture(
        jointBonePosition, colliderBonePosition, RigidBodyShape::Sphere, Vec3(kRadius, 0.0f, 0.0f), colliderBonePosition, Vec3::Zero());

    Registry registry;
    const std::string path = "ColliderTracksBoneModel.gta";
    const Entity entity = RegisterAttachSeedPoseAndConfigureChain(physicsSystem, registry, path, data, /*collisionEnabled=*/true);
    registry.AddComponent<Transform>(entity); // identity.

    // Frame 1: lazy init seeds the joint particle exactly AT the collider's
    // (coincident) bind-pose center - the degenerate zero-distance case
    // (SphereCollider.cpp) deterministically pushes it straight up by
    // exactly the radius.
    physicsSystem.Update(registry, 1.0 / 60.0);

    {
        const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        ASSERT_NE(pose, nullptr);
        const Vec3 jointAfterFrame1 = ReconstructBoneWorldPosition(registry, entity, data.skeleton, pose->pose, kJointBoneIndex);
        ASSERT_TRUE(ApproximatelyEqual(jointAfterFrame1, jointBonePosition + Vec3::Up() * kRadius, 1e-3f))
            << "Frame 1 sanity check failed - the joint was not pushed to the expected degenerate-collision position.";
    }

    // Move the collider's OWN bone (never the joint's) by mutating its pose
    // entry directly - simulating that bone animating this frame.
    {
        ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
        ASSERT_NE(pose, nullptr);
        ASSERT_GT(pose->pose.size(), static_cast<std::size_t>(kColliderBoneIndex));
        pose->pose[static_cast<std::size_t>(kColliderBoneIndex)].translation += Vec3::Up() * 1.5f;
    }

    physicsSystem.Update(registry, 1.0 / 60.0);

    const ResolvedAnimationPose* poseAfterFrame2 = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(poseAfterFrame2, nullptr);
    const Vec3 jointAfterFrame2 = ReconstructBoneWorldPosition(registry, entity, data.skeleton, poseAfterFrame2->pose, kJointBoneIndex);

    // If the collider genuinely tracked its bone's NEW world position, the
    // joint (sitting exactly at the collider's OLD surface) is now
    // penetrating the MOVED sphere and gets pushed to its NEW surface,
    // landing at Y = 0.5 (see this test's own file-level derivation in the
    // strategy document). If the collider were stale (still resolved at its
    // bind-pose position, or never resolved into the solver at all), the
    // joint would instead stay at Y = 1.0 - a clearly distinguishable
    // difference.
    const float expectedY = jointBonePosition.y + 0.5f;
    EXPECT_NEAR(jointAfterFrame2.y, expectedY, 0.05f) << "The collider did not track its bone's new world-space position this frame.";
    EXPECT_LT(jointAfterFrame2.y, jointBonePosition.y + 0.8f)
        << "The joint stayed near its OLD (pre-move) pushed-out position - the collider looks stale/un-resolved.";
}

// Shared fixture for the two tests below - a Box collider whose bind-pose
// authored transform (RigidBody::translate/rotateRadians) is DELIBERATELY
// non-coincident with its own tracked bone's bind position/rotation, so
// resolving it correctly genuinely exercises the full
// localOffsetPosition/localOffsetRotation composition, not a trivial
// zero-offset case.
struct BoxColliderFixture {
    SkinnedMeshData data;
    Quat expectedRotationAtBindPose = Quat::Identity();
    Vec3 expectedCenterAtBindPose = Vec3::Zero();
    // Where the joint (lazy-init-seeded exactly at a known point INSIDE the
    // box) should land after exactly one collision resolve, with the entity
    // itself left at identity - the expected face-push result along the
    // box's own local +X axis (see BuildBoxColliderFixture()'s own body).
    Vec3 expectedPushedWorldPositionAtBindPose = Vec3::Zero();
};

BoxColliderFixture BuildBoxColliderFixture()
{
    BoxColliderFixture result;

    const Vec3 colliderBonePosition(5.0f, 0.0f, 0.0f); // deliberately far from the rigid body's own authored bind position below.
    const Vec3 bodyTranslate(0.0f, 0.0f, 2.0f);         // this rigid body's own absolute model-space bind-pose position.
    const Vec3 bodyRotateRadians(0.0f, kHalfPi, 0.0f);  // 90 degrees about Y (PMX Euler convention, radians).
    const Vec3 boxHalfExtents(1.0f, 1.0f, 1.0f);

    result.expectedRotationAtBindPose = Quat::FromEulerDegrees(0.0f, 90.0f, 0.0f);
    result.expectedCenterAtBindPose = bodyTranslate;

    const Vec3 localPointInsideBox(0.5f, 0.0f, 0.0f); // comfortably inside every half-extent (1.0f).
    const Vec3 jointBonePosition = result.expectedCenterAtBindPose + result.expectedRotationAtBindPose.RotateVector(localPointInsideBox);

    // The X axis has the smallest escape distance (1.0f - 0.5f = 0.5f, vs.
    // 1.0f for both Y and Z) - see BoxCollider.cpp's own "smallest escape"
    // selection - so the particle is pushed out to the box's own local +X
    // face.
    result.expectedPushedWorldPositionAtBindPose
        = result.expectedCenterAtBindPose + result.expectedRotationAtBindPose.RotateVector(Vec3(1.0f, 0.0f, 0.0f));

    result.data
        = BuildFixture(jointBonePosition, colliderBonePosition, RigidBodyShape::Box, boxHalfExtents, bodyTranslate, bodyRotateRadians);
    return result;
}

TEST(PhysicsSystemModelColliderResolutionTests, ColliderRespectsItsOwnBindPoseLocalOffsetAndRotation)
{
    PhysicsSystem physicsSystem;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3::Zero();
    const BoxColliderFixture fixture = BuildBoxColliderFixture();

    Registry registry;
    const std::string path = "ColliderOffsetRotationModel.gta";
    const Entity entity = RegisterAttachSeedPoseAndConfigureChain(physicsSystem, registry, path, fixture.data, /*collisionEnabled=*/true);
    registry.AddComponent<Transform>(entity); // identity - the bone stays at bind pose throughout, unmoved.

    physicsSystem.Update(registry, 1.0 / 60.0);

    const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(pose, nullptr);
    const Vec3 jointWorldPosition = ReconstructBoneWorldPosition(registry, entity, fixture.data.skeleton, pose->pose, kJointBoneIndex);

    EXPECT_TRUE(ApproximatelyEqual(jointWorldPosition, fixture.expectedPushedWorldPositionAtBindPose, 1e-3f))
        << "The resolved collider's bind-pose center/rotation did not match the RigidBody's own authored absolute "
           "bind-pose transform (translate/rotateRadians) composed through its bone's own bind-pose local offset.";
}

TEST(PhysicsSystemModelColliderResolutionTests, NoChainWantingCollisionResolvesAnEmptyListEveryFrame)
{
    PhysicsSystem physicsSystem;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3::Zero();

    const Vec3 jointBonePosition(2.0f, 0.0f, 0.0f);
    const Vec3 colliderBonePosition = jointBonePosition;
    constexpr float kRadius = 1.0f;
    // The EXACT same overlapping-collider setup as
    // ColliderTracksItsBoneAsTheBoneAnimates above, which WOULD trigger a
    // collision push if collision were enabled - proving this test's own
    // "nothing moved" result is a genuine no-op, not merely a fixture that
    // never overlaps anything.
    const SkinnedMeshData data = BuildFixture(
        jointBonePosition, colliderBonePosition, RigidBodyShape::Sphere, Vec3(kRadius, 0.0f, 0.0f), colliderBonePosition, Vec3::Zero());

    Registry registry;
    const std::string path = "NoCollisionWantedModel.gta";
    // collisionEnabled = false for every chain - the default, matching a
    // freshly-detected chain that never had the Editor's opt-in checkbox
    // switched on.
    const Entity entity = RegisterAttachSeedPoseAndConfigureChain(physicsSystem, registry, path, data, /*collisionEnabled=*/false);
    registry.AddComponent<Transform>(entity);

    physicsSystem.Update(registry, 1.0 / 60.0);

    const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(pose, nullptr);
    const Vec3 jointAfterUpdate = ReconstructBoneWorldPosition(registry, entity, data.skeleton, pose->pose, kJointBoneIndex);

    EXPECT_TRUE(ApproximatelyEqual(jointAfterUpdate, jointBonePosition, 1e-3f))
        << "The joint moved even though no chain opted into collision - collision must be a complete no-op "
           "(both the perf-guard's empty resolvedColliders list AND StepDynamicChain()'s own collisionEnabled "
           "guard) whenever every chain on this model has collisionEnabled == false.";
}

TEST(PhysicsSystemModelColliderResolutionTests, EntityWorldTransformRotationIsComposedIntoColliderOrientation)
{
    PhysicsSystem physicsSystem;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3::Zero();
    const BoxColliderFixture fixture = BuildBoxColliderFixture();

    Registry registry;
    const std::string path = "ColliderEntityRotationModel.gta";
    const Entity entity = RegisterAttachSeedPoseAndConfigureChain(physicsSystem, registry, path, fixture.data, /*collisionEnabled=*/true);

    // Rotate the OWNING ENTITY itself (never a bone) - deliberately about a
    // DIFFERENT axis (Z) than the box's own authored bind-pose rotation
    // (Y), so this is genuinely testing entity-level composition, not
    // accidentally re-testing the bind-pose-offset math above.
    const Quat entityRotation = Quat::FromEulerDegrees(0.0f, 0.0f, 90.0f);
    Transform& transform = registry.AddComponent<Transform>(entity);
    transform.rotation = entityRotation;

    physicsSystem.Update(registry, 1.0 / 60.0);

    const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(pose, nullptr);
    const Vec3 jointWorldPosition = ReconstructBoneWorldPosition(registry, entity, fixture.data.skeleton, pose->pose, kJointBoneIndex);

    // Rotating the ENTITY rotates the joint AND the collider by the exact
    // same amount (both are composed through the SAME entityWorldMatrix),
    // so their RELATIVE geometry - and therefore the expected push-out
    // result - is simply the identity (unrotated) result, itself rotated by
    // entityRotation. If PHASE4's own resolution code forgot to compose
    // entityWorldMatrix into the collider (a real, plausible bug: computing
    // the collider from the bone's own world matrix alone), the collider
    // would stay un-rotated while the joint still correctly rotates - a
    // large, easily-detected mismatch from this expectation.
    const Vec3 expectedWorldPosition = entityRotation.RotateVector(fixture.expectedPushedWorldPositionAtBindPose);
    EXPECT_TRUE(ApproximatelyEqual(jointWorldPosition, expectedWorldPosition, 1e-3f))
        << "The owning entity's own Transform rotation was not correctly composed into the resolved collider's "
           "orientation/position - a Box collider must rotate along with its model exactly like everything else.";
}

} // namespace
} // namespace gte
