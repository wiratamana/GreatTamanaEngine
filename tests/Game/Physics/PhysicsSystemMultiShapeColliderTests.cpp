// PHASE6 (task_manager/verlet-integration-9,
// PHASE6_END_TO_END_REGRESSION_AND_BUILD_REGISTRATION.md) - the campaign's own
// final, real, FULL-PIPELINE regression test: a single dynamic chain, hanging
// from a Static PMX anchor, genuinely colliding against a Sphere, a Box, AND a
// Capsule collider SIMULTANEOUSLY - all three auto-detected from real
// RigidBodyMotionType::Static PMX rigid bodies (Physics/ModelColliderDetection.h,
// PHASE3), resolved to world space every frame (Game/Physics/PhysicsSystem.cpp,
// PHASE4), and fed into the solver's mixed-shape collider list
// (Physics/DynamicChainSolver.h, PHASE2) - driven end to end through the REAL
// PhysicsSystem::RegisterDynamicChains() -> AttachDynamicChainRigIfNeeded() ->
// Update() pipeline, exactly the scenario the user originally reported as
// broken ("the collision perhaps only works with sphere collider... I want you
// to make the verlet able to do collision check with [sphere, box, capsule]").
//
// This is a companion to tests/Game/Physics/PhysicsSystemModelColliderResolutionTests.cpp
// (PHASE4), which isolates the per-collider WORLD-SPACE RESOLUTION math one
// shape at a time with every other simulation force neutralized; THIS file
// instead proves the OUTCOME of colliding against all three shapes AT ONCE
// under ordinary gravity-driven dynamics, plus a genuine "this fixture
// actually needs collision to pass" regression guard (Step 3.1, item 5 of the
// phase document above): the exact same fixture, stepped the exact same way
// but with every chain's own `collisionEnabled` left false, is independently
// confirmed to end up penetrating at least one collider - proving this test
// is not trivially true regardless of whether collision resolution works at
// all.
//
// Per PHASE0_MASTER_STRATEGY.md's own v2 Revision Notes (finding #2): this
// file writes its OWN local (anonymous-namespace) `MakeRigidBody()`/
// `MakeJoint()` fixture helpers rather than attempting to reuse
// tests/Physics/DynamicChainDetectionTests.cpp's private, narrower
// `MakeRigidBody()` (which has no parameters for shape/shapeSize/translate/
// rotateRadians at all, and is private to that other .cpp's own anonymous
// namespace besides).

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
// for why DynamicChainDetectionTests.cpp's private MakeRigidBody()/MakeJoint()
// cannot be reused (a different file's own private, per-file test helper is
// an established convention in this codebase - see e.g. RotationFromPmxEuler()
// being independently redeclared in several different .cpp files, each
// cross-referencing the others).
RigidBody MakeRigidBody(std::int32_t boneIndex, RigidBodyMotionType motionType, RigidBodyShape shape,
    const Vec3& shapeSize, const Vec3& translate = Vec3::Zero(), const Vec3& rotateRadians = Vec3::Zero())
{
    RigidBody body;
    body.boneIndex = boneIndex;
    body.motionType = motionType;
    body.shape = shape;
    body.shapeSize = shapeSize;
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
// A single linear 3-joint chain hanging from a Static PMX anchor, deliberately
// built so each joint's OWN bind position sits EXACTLY at one of the three
// colliders' own world-space centers (a Sphere, a Box, and a Capsule,
// respectively) - a deep, deliberate initial penetration. Gravity is zero and
// every joint's own goal-constraint stiffness is zero (see
// RegisterAttachAndConfigure() below), and every chain rest length exactly
// matches its own bind-pose bone-to-bone distance (DetectDynamicChains()'s own
// standard rest-length derivation) - so with collision left disabled, NOTHING
// in this fixture ever moves a joint away from its own bind position: it
// would stay exactly at the collider's center, forever. With collision
// enabled, StepDynamicChain()'s own step ordering (integrate -> structural
// relaxation -> goal constraint -> COLLISION, always last - see
// Physics/DynamicChainSolver.h) guarantees every joint is pushed back out to
// its collider's own surface at the end of EVERY completed step, regardless of
// what the earlier steps in that same call did. This is what makes the
// resulting test fully deterministic (no reliance on tuning a
// convergence/settling process to land in the right place after N steps) -
// it isolates PHASE3's real auto-detection + PHASE4's real world-space
// resolution + PHASE2's real solver wiring, all working together through the
// REAL PhysicsSystem::Update() pipeline, from any question about how a
// released chain happens to swing/settle under gravity.
//
// Each of the three colliders is attached to its OWN Static rigid body on its
// OWN bone, and each RigidBody::translate is deliberately OFFSET from its own
// tracked bone's bind position (never coincident) - exercising PHASE3's
// bind-pose-relative-offset math for real, for all three shapes, not just at
// a trivial zero-offset.
constexpr std::int32_t kRootBoneIndex = 0;
constexpr std::int32_t kAnchorBoneIndex = 1;
constexpr std::int32_t kJoint1BoneIndex = 2;
constexpr std::int32_t kJoint2BoneIndex = 3;
constexpr std::int32_t kJoint3BoneIndex = 4;
constexpr std::int32_t kSphereBoneIndex = 5;
constexpr std::int32_t kBoxBoneIndex = 6;
constexpr std::int32_t kCapsuleBoneIndex = 7;

const Vec3 kAnchorBindPosition(0.0f, -0.5f, 0.0f);

const Vec3 kSphereWorldCenter(1.0f, -0.5f, 0.0f); // == joint1's own bind position (deliberate initial penetration).
constexpr float kSphereRadius = 0.6f;

const Vec3 kBoxWorldCenter(2.0f, -0.5f, 0.0f); // == joint2's own bind position.
const Vec3 kBoxHalfExtents(0.6f, 0.6f, 0.6f);

const Vec3 kCapsuleWorldCenter(3.0f, -0.5f, 0.0f); // == joint3's own bind position.
constexpr float kCapsuleRadius = 0.6f;
constexpr float kCapsuleHeight = 1.0f;

constexpr float kPenetrationTolerance = 1e-3f;

SkinnedMeshData BuildFixture()
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
    joint1.position = kSphereWorldCenter; // deliberately coincident - see this section's own header comment.
    joint1.parentBoneIndex = kAnchorBoneIndex;
    data.skeleton.bones.push_back(joint1); // 2

    Bone joint2;
    joint2.position = kBoxWorldCenter;
    joint2.parentBoneIndex = kJoint1BoneIndex;
    data.skeleton.bones.push_back(joint2); // 3

    Bone joint3;
    joint3.position = kCapsuleWorldCenter;
    joint3.parentBoneIndex = kJoint2BoneIndex;
    data.skeleton.bones.push_back(joint3); // 4

    Bone boneSphere;
    boneSphere.position = Vec3(1.0f, 0.0f, 0.0f); // deliberately NOT coincident with kSphereWorldCenter.
    boneSphere.parentBoneIndex = kRootBoneIndex;
    data.skeleton.bones.push_back(boneSphere); // 5

    Bone boneBox;
    boneBox.position = Vec3(2.0f, 0.0f, 0.0f); // deliberately NOT coincident with kBoxWorldCenter.
    boneBox.parentBoneIndex = kRootBoneIndex;
    data.skeleton.bones.push_back(boneBox); // 6

    Bone boneCapsule;
    boneCapsule.position = Vec3(3.0f, 0.0f, 0.0f); // deliberately NOT coincident with kCapsuleWorldCenter.
    boneCapsule.parentBoneIndex = kRootBoneIndex;
    data.skeleton.bones.push_back(boneCapsule); // 7

    PhysicsData physics;

    RigidBody anchorBody = MakeRigidBody(kAnchorBoneIndex, RigidBodyMotionType::Static, RigidBodyShape::Sphere, Vec3::Zero());
    physics.rigidBodies.push_back(anchorBody); // 0

    RigidBody joint1Body = MakeRigidBody(kJoint1BoneIndex, RigidBodyMotionType::Dynamic, RigidBodyShape::Sphere, Vec3::Zero());
    physics.rigidBodies.push_back(joint1Body); // 1

    RigidBody joint2Body = MakeRigidBody(kJoint2BoneIndex, RigidBodyMotionType::Dynamic, RigidBodyShape::Sphere, Vec3::Zero());
    physics.rigidBodies.push_back(joint2Body); // 2

    RigidBody joint3Body = MakeRigidBody(kJoint3BoneIndex, RigidBodyMotionType::Dynamic, RigidBodyShape::Sphere, Vec3::Zero());
    physics.rigidBodies.push_back(joint3Body); // 3

    physics.joints.push_back(MakeJoint(0, 1)); // anchor -> joint1
    physics.joints.push_back(MakeJoint(1, 2)); // joint1 -> joint2
    physics.joints.push_back(MakeJoint(2, 3)); // joint2 -> joint3

    RigidBody sphereBody = MakeRigidBody(
        kSphereBoneIndex, RigidBodyMotionType::Static, RigidBodyShape::Sphere, Vec3(kSphereRadius, 0.0f, 0.0f), kSphereWorldCenter);
    physics.rigidBodies.push_back(sphereBody); // 4 - not referenced by any Joint (a pure collision obstacle).

    RigidBody boxBody
        = MakeRigidBody(kBoxBoneIndex, RigidBodyMotionType::Static, RigidBodyShape::Box, kBoxHalfExtents, kBoxWorldCenter);
    physics.rigidBodies.push_back(boxBody); // 5

    RigidBody capsuleBody = MakeRigidBody(kCapsuleBoneIndex, RigidBodyMotionType::Static, RigidBodyShape::Capsule,
        Vec3(kCapsuleRadius, kCapsuleHeight, 0.0f), kCapsuleWorldCenter);
    physics.rigidBodies.push_back(capsuleBody); // 6

    data.physics = std::move(physics);
    return data;
}

// Registers `data` under `path`, sets every detected chain's own
// `collisionEnabled` per the caller's request, and forces every joint's own
// `stiffness` to 0 (the goal constraint must never pull a joint away from
// wherever collision/structural relaxation left it, per this file's own
// determinism argument above) and `damping` to 1.0 (fully kill any implied
// velocity every step, so nothing carries residual motion across frames
// either) - attaches the rig, and seeds a pure bind-pose ResolvedAnimationPose
// (the chain is never animated; with gravity zero too, ONLY collision can
// ever move a joint away from its own bind position in this fixture).
Entity RegisterAttachAndConfigure(PhysicsSystem& physicsSystem, Registry& registry, const std::string& path, bool collisionEnabled)
{
    const SkinnedMeshData data = BuildFixture();
    physicsSystem.RegisterDynamicChains(path, data);

    DynamicChainRigCache::ModelEntry* entry = physicsSystem.GetDynamicChainRigCache().TryGetMutable(path);
    if (entry != nullptr) {
        for (DynamicChainDefinition& chain : entry->chains) {
            chain.collisionEnabled = collisionEnabled;
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

// --- Independent (re-derived, NOT calling the production Solve*Collision()
// functions) shape-vs-point tests, per PHASE6's own Step 3.1 instructions ---

bool IsOutsideSphere(const Vec3& point, const Vec3& center, float radius)
{
    return Length(point - center) >= radius - kPenetrationTolerance;
}

bool IsOutsideBox(const Vec3& point, const Vec3& center, const Vec3& halfExtents)
{
    // The fixture's own box collider is never rotated (see BuildFixture()'s
    // own comment - identity rotateRadians throughout), so no inverse-rotate
    // step is needed here; the local-space check itself still mirrors
    // BoxCollider.cpp's own logic independently, never calling it directly.
    const Vec3 local = point - center;
    return std::fabs(local.x) >= halfExtents.x - kPenetrationTolerance || std::fabs(local.y) >= halfExtents.y - kPenetrationTolerance
        || std::fabs(local.z) >= halfExtents.z - kPenetrationTolerance;
}

bool IsOutsideCapsule(const Vec3& point, const Vec3& center, float radius, float height)
{
    // The fixture's own capsule collider is never rotated either - its
    // central segment is simply vertical (local +Y, matching
    // CapsuleCollider.h's own documented axis convention), independently
    // re-derived here rather than calling SolveCapsuleCollision() directly.
    const float halfHeight = height * 0.5f;
    const Vec3 segStart = center - Vec3(0.0f, halfHeight, 0.0f);
    const Vec3 segEnd = center + Vec3(0.0f, halfHeight, 0.0f);
    const Vec3 segment = segEnd - segStart;
    const float segmentLengthSq = LengthSquared(segment);

    Vec3 closest;
    if (segmentLengthSq < kEpsilon) {
        closest = center;
    } else {
        const float t = Clamp(Dot(point - segStart, segment) / segmentLengthSq, 0.0f, 1.0f);
        closest = segStart + segment * t;
    }
    return Length(point - closest) >= radius - kPenetrationTolerance;
}

bool IsOutsideEveryCollider(const Vec3& point)
{
    return IsOutsideSphere(point, kSphereWorldCenter, kSphereRadius) && IsOutsideBox(point, kBoxWorldCenter, kBoxHalfExtents)
        && IsOutsideCapsule(point, kCapsuleWorldCenter, kCapsuleRadius, kCapsuleHeight);
}

} // namespace

TEST(PhysicsSystemMultiShapeColliderTests, ChainCollidesCorrectlyAgainstAllThreeAutoDetectedShapesSimultaneously)
{
    PhysicsSystem physicsSystem;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3::Zero(); // see this file's own determinism argument above.

    Registry registry;
    const std::string path = "MultiShapeColliderModel.gta";
    const Entity entity = RegisterAttachAndConfigure(physicsSystem, registry, path, /*collisionEnabled=*/true);

    // Step 1 of the phase document: confirm detection genuinely ran against
    // this fixture BEFORE trusting anything the stepping loop below produces.
    const DynamicChainRigCache::ModelEntry* model = physicsSystem.GetDynamicChainRigCache().TryGet(path);
    ASSERT_NE(model, nullptr);
    ASSERT_EQ(model->chains.size(), 1u) << "Expected exactly one detected chain (anchor(1) -> joint1(2) -> joint2(3) -> joint3(4)).";
    ASSERT_EQ(model->chains[0].jointBoneIndices.size(), 3u);
    ASSERT_EQ(model->colliders.size(), 3u) << "Expected all three Static rigid bodies (sphere/box/capsule) to be auto-detected.";

    const SkinnedMeshData data = BuildFixture(); // Local copy purely for reading bind-topology below - never re-registered.

    for (int step = 0; step < 200; ++step) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(pose, nullptr);

    for (std::int32_t boneIndex : { kJoint1BoneIndex, kJoint2BoneIndex, kJoint3BoneIndex }) {
        const Vec3 jointWorldPosition = ComputeBoneWorldMatrix(data.skeleton, pose->pose, boneIndex).TransformPoint(Vec3::Zero());

        EXPECT_TRUE(IsOutsideSphere(jointWorldPosition, kSphereWorldCenter, kSphereRadius))
            << "Bone " << boneIndex << " ended up inside the auto-detected Sphere collider.";
        EXPECT_TRUE(IsOutsideBox(jointWorldPosition, kBoxWorldCenter, kBoxHalfExtents))
            << "Bone " << boneIndex << " ended up inside the auto-detected Box collider.";
        EXPECT_TRUE(IsOutsideCapsule(jointWorldPosition, kCapsuleWorldCenter, kCapsuleRadius, kCapsuleHeight))
            << "Bone " << boneIndex << " ended up inside the auto-detected Capsule collider.";
    }
}

// Step 5 of the phase document's own instructions: a genuine regression
// guard, proving this fixture's own geometry actually NEEDS collision to
// avoid penetration - never a trivially-true test. The EXACT same fixture,
// stepped the EXACT same number of times, but with every chain's
// `collisionEnabled` left false (DetectDynamicChains()'s own default) must
// end up with AT LEAST ONE joint penetrating AT LEAST ONE collider.
TEST(PhysicsSystemMultiShapeColliderTests, SameFixtureWithCollisionDisabledDoesPenetrateProvingTheFixtureNeedsCollision)
{
    PhysicsSystem physicsSystem;
    physicsSystem.GetGlobalPhysicsSettings().gravity = Vec3::Zero(); // see this file's own determinism argument above.

    Registry registry;
    const std::string path = "MultiShapeColliderModelNoCollision.gta";
    const Entity entity = RegisterAttachAndConfigure(physicsSystem, registry, path, /*collisionEnabled=*/false);

    const DynamicChainRigCache::ModelEntry* model = physicsSystem.GetDynamicChainRigCache().TryGet(path);
    ASSERT_NE(model, nullptr);
    ASSERT_EQ(model->chains.size(), 1u);
    ASSERT_FALSE(model->chains[0].collisionEnabled);
    ASSERT_EQ(model->colliders.size(), 3u);

    const SkinnedMeshData data = BuildFixture();

    for (int step = 0; step < 200; ++step) {
        physicsSystem.Update(registry, 1.0 / 60.0);
    }

    const ResolvedAnimationPose* pose = registry.TryGetComponent<ResolvedAnimationPose>(entity);
    ASSERT_NE(pose, nullptr);

    bool anyJointPenetratedAnyCollider = false;
    for (std::int32_t boneIndex : { kJoint1BoneIndex, kJoint2BoneIndex, kJoint3BoneIndex }) {
        const Vec3 jointWorldPosition = ComputeBoneWorldMatrix(data.skeleton, pose->pose, boneIndex).TransformPoint(Vec3::Zero());
        if (!IsOutsideEveryCollider(jointWorldPosition)) {
            anyJointPenetratedAnyCollider = true;
            break;
        }
    }

    EXPECT_TRUE(anyJointPenetratedAnyCollider)
        << "With collision disabled, this fixture's own geometry was expected to let at least one joint fall into at least one "
           "collider - if it didn't, the OTHER test in this file (collision enabled) would be trivially true regardless of whether "
           "collision resolution actually works.";
}

} // namespace gte
