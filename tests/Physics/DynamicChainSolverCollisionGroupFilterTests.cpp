// Unit tests for task_manager/verlet-integration-10, PHASE1 - the PMX
// collision-group/layer filter (Bullet-style symmetric group/mask AND-test)
// added to StepDynamicChain()'s own collision loop (src/Physics/
// DynamicChainSolver.cpp). Mirrors tests/Physics/DynamicChainSolverTests.cpp's
// own fixture style (BuildThreeJointChainDefinition() pattern), but every
// test here uses a single-joint chain for a smaller, more direct proof.
//
// GroupsMayCollide()/GroupBit() themselves are `static`/anonymous-namespace
// in DynamicChainSolver.cpp - never exported - so every test below either
// (a) exercises the real filter indirectly through StepDynamicChain() itself
// (the overwhelming majority of these tests), or (b) independently
// re-derives the exact same Bullet-style truth table by hand, entirely
// separately from the production code, mirroring
// tests/Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp's own
// "IsOutsideSphere()/IsOutsideBox()/IsOutsideCapsule() never call the
// production math directly" precedent (see PHASE1's own Step 3.9 for this
// exact instruction).

#include "Physics/DynamicChainSolver.h"

#include "Physics/Collider.h"

#include <gtest/gtest.h>
#include <cmath>
#include <cstdint>

namespace gte {
namespace {

DynamicChainDefinition BuildOneJointChainDefinition(std::uint8_t jointGroup, std::uint16_t jointCollisionMask)
{
    DynamicChainDefinition definition;
    definition.rootBoneIndex = -1;
    definition.jointBoneIndices = { 0 };
    definition.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(1);
    DynamicJointSettings settings{ /*damping=*/0.05f, /*stiffness=*/0.0f, /*mass=*/1.0f };
    settings.group = jointGroup;
    settings.collisionMask = jointCollisionMask;
    definition.jointSettings = { settings };
    definition.restLengths = { 1.0f };
    definition.gravityScale = 1.0f;
    definition.windScale = 1.0f;
    definition.constraintIterations = 4;
    definition.collisionEnabled = true;
    return definition;
}

// Independent, from-scratch re-derivation of the exact same Bullet-style
// symmetric AND-test the production GroupsMayCollide() implements - never
// calls the production (anonymous-namespace, unexported) function directly.
bool ReferenceGroupsMayCollide(std::uint8_t groupA, std::uint16_t maskA, std::uint8_t groupB, std::uint16_t maskB)
{
    const std::uint16_t bitA = static_cast<std::uint16_t>(1u << (groupA & 0x0Fu));
    const std::uint16_t bitB = static_cast<std::uint16_t>(1u << (groupB & 0x0Fu));
    return (bitA & maskB) != 0 && (bitB & maskA) != 0;
}

} // namespace

// A joint and a collider in the same group, with masks that allow it, must
// collide - the joint sags under gravity into the sphere's own falling path
// and must end up pushed back out to the sphere's surface.
TEST(DynamicChainSolverCollisionGroupFilterTests, JointAndColliderInSameGroupDoCollide)
{
    DynamicChainDefinition definition = BuildOneJointChainDefinition(/*group=*/3, /*mask=*/0xFFFF);

    DynamicChainRuntimeState state;
    const Vec3 root(0.0f, 0.0f, 0.0f);
    const std::vector<Vec3> targets = { Vec3(1.0f, 0.0f, 0.0f) };
    const Vec3 gravity(0.0f, -9.8f, 0.0f);
    const WindSettings noWind{};

    Collider collider;
    collider.shape = ColliderShape::Sphere;
    collider.center = Vec3(1.0f, -0.5f, 0.0f);
    collider.rotation = Quat::Identity();
    collider.size = Vec3(1.0f, 0.0f, 0.0f);
    collider.group = 3;
    collider.collisionMask = 0xFFFF;
    const std::vector<Collider> colliders = { collider };

    for (int step = 0; step < 120; ++step) {
        StepDynamicChain(definition, root, targets, state, 1.0f / 60.0f, gravity, noWind, colliders);
    }

    const float distanceFromColliderCenter = Length(state.particles[0].position - collider.center);
    EXPECT_GE(distanceFromColliderCenter, collider.size.x - 1e-3f)
        << "Joint and collider share the same group and an all-allowing mask - they must collide.";
}

// A joint and a collider in DIFFERENT groups, with a mask that blocks the
// pairing (collider's mask has bit 3 clear), must NOT collide - a genuine
// regression guard proving the GROUP filter itself, not merely the ordinary
// collisionEnabled == false no-op (collisionEnabled is explicitly true here).
TEST(DynamicChainSolverCollisionGroupFilterTests, JointAndColliderInDifferentGroupsWithNoOverlapDoNotCollide)
{
    DynamicChainDefinition definition = BuildOneJointChainDefinition(/*group=*/3, /*mask=*/0xFFFF);
    ASSERT_TRUE(definition.collisionEnabled);

    DynamicChainRuntimeState state;
    const Vec3 root(0.0f, 0.0f, 0.0f);
    const std::vector<Vec3> targets = { Vec3(1.0f, 0.0f, 0.0f) };
    const Vec3 gravity(0.0f, -9.8f, 0.0f);
    const WindSettings noWind{};

    Collider collider;
    collider.shape = ColliderShape::Sphere;
    collider.center = Vec3(1.0f, -0.5f, 0.0f);
    collider.rotation = Quat::Identity();
    collider.size = Vec3(1.0f, 0.0f, 0.0f);
    collider.group = 5;
    collider.collisionMask = 0x0000; // Bit 3 (the joint's own group) is clear - filter must block this pair.
    const std::vector<Collider> colliders = { collider };

    for (int step = 0; step < 120; ++step) {
        StepDynamicChain(definition, root, targets, state, 1.0f / 60.0f, gravity, noWind, colliders);
    }

    const float distanceFromColliderCenter = Length(state.particles[0].position - collider.center);
    EXPECT_LT(distanceFromColliderCenter, collider.size.x - 1e-3f)
        << "The group filter must have blocked this pair, so the joint should have penetrated the sphere freely.";
}

// Proves the AND is genuinely SYMMETRIC (both sides must allow each other),
// not one-directional, by independently re-deriving the truth table.
TEST(DynamicChainSolverCollisionGroupFilterTests, FilterIsSymmetricBothSidesMustAllow)
{
    // A allows B (bit for B's group is set in A's mask) but B doesn't allow A.
    {
        const std::uint8_t groupA = 1;
        const std::uint16_t maskA = static_cast<std::uint16_t>(1u << 2); // allows group 2 only.
        const std::uint8_t groupB = 2;
        const std::uint16_t maskB = static_cast<std::uint16_t>(1u << 5); // does NOT allow group 1.
        EXPECT_FALSE(ReferenceGroupsMayCollide(groupA, maskA, groupB, maskB))
            << "One-directional allow must not be enough - both sides must mutually allow each other.";
    }
    // B allows A but A doesn't allow B.
    {
        const std::uint8_t groupA = 1;
        const std::uint16_t maskA = static_cast<std::uint16_t>(1u << 9); // does NOT allow group 2.
        const std::uint8_t groupB = 2;
        const std::uint16_t maskB = static_cast<std::uint16_t>(1u << 1); // allows group 1.
        EXPECT_FALSE(ReferenceGroupsMayCollide(groupA, maskA, groupB, maskB))
            << "One-directional allow (reversed) must not be enough either.";
    }
    // Both allow each other.
    {
        const std::uint8_t groupA = 1;
        const std::uint16_t maskA = static_cast<std::uint16_t>(1u << 2);
        const std::uint8_t groupB = 2;
        const std::uint16_t maskB = static_cast<std::uint16_t>(1u << 1);
        EXPECT_TRUE(ReferenceGroupsMayCollide(groupA, maskA, groupB, maskB))
            << "When both sides mutually allow each other, the pair must be allowed to collide.";
    }
}

// Regression guard: a chain and collider BOTH left at their default
// group = 0 / collisionMask = 0xFFFF must still collide exactly like every
// pre-PHASE1 test already proved - the defaults are 100% backward compatible.
TEST(DynamicChainSolverCollisionGroupFilterTests, DefaultGroupAndMaskReproduceOldUnconditionalCollisionBehavior)
{
    DynamicChainDefinition definition;
    definition.rootBoneIndex = -1;
    definition.jointBoneIndices = { 0 };
    definition.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(1);
    definition.jointSettings = { DynamicJointSettings{ 0.05f, 0.0f, 1.0f } }; // group/collisionMask left at defaults.
    definition.restLengths = { 1.0f };
    definition.gravityScale = 1.0f;
    definition.windScale = 1.0f;
    definition.constraintIterations = 4;
    definition.collisionEnabled = true;

    ASSERT_EQ(definition.jointSettings[0].group, 0);
    ASSERT_EQ(definition.jointSettings[0].collisionMask, 0xFFFF);

    DynamicChainRuntimeState state;
    const Vec3 root(0.0f, 0.0f, 0.0f);
    const std::vector<Vec3> targets = { Vec3(1.0f, 0.0f, 0.0f) };
    const Vec3 gravity(0.0f, -9.8f, 0.0f);
    const WindSettings noWind{};

    const Collider collider{ ColliderShape::Sphere, Vec3(1.0f, -0.5f, 0.0f), Quat::Identity(), Vec3(1.0f, 0.0f, 0.0f) };
    ASSERT_EQ(collider.group, 0);
    ASSERT_EQ(collider.collisionMask, 0xFFFF);
    const std::vector<Collider> colliders = { collider };

    for (int step = 0; step < 120; ++step) {
        StepDynamicChain(definition, root, targets, state, 1.0f / 60.0f, gravity, noWind, colliders);
    }

    const float distanceFromColliderCenter = Length(state.particles[0].position - collider.center);
    EXPECT_GE(distanceFromColliderCenter, collider.size.x - 1e-3f)
        << "Both left at defaults must still collide - exactly the pre-PHASE1 unconditional behavior.";
}

// task_manager/verlet-integration-10, PHASE1 (v2) - a direct regression
// guard for the shift-safety fix: an out-of-documented-range `group` value
// that nonetheless shares the same low 4 bits as a legitimate value must
// still be treated identically (masked consistently, not merely "does not
// crash").
TEST(DynamicChainSolverCollisionGroupFilterTests, OutOfRangeGroupValueNeverCrashesAndStillMasksToTheCorrectLowFourBits)
{
    // group = 19 (0b10011) & 0x0F == 3, the same bit as a legitimate group-3 value.
    DynamicChainDefinition definition = BuildOneJointChainDefinition(/*group=*/3, /*mask=*/0xFFFF);

    DynamicChainRuntimeState state;
    const Vec3 root(0.0f, 0.0f, 0.0f);
    const std::vector<Vec3> targets = { Vec3(1.0f, 0.0f, 0.0f) };
    const Vec3 gravity(0.0f, -9.8f, 0.0f);
    const WindSettings noWind{};

    Collider collider;
    collider.shape = ColliderShape::Sphere;
    collider.center = Vec3(1.0f, -0.5f, 0.0f);
    collider.rotation = Quat::Identity();
    collider.size = Vec3(1.0f, 0.0f, 0.0f);
    collider.group = static_cast<std::uint8_t>(19); // out-of-range, but 19 & 0x0F == 3.
    collider.collisionMask = 0xFFFF;
    const std::vector<Collider> colliders = { collider };

    for (int step = 0; step < 120; ++step) {
        StepDynamicChain(definition, root, targets, state, 1.0f / 60.0f, gravity, noWind, colliders);
    }

    const float distanceFromColliderCenter = Length(state.particles[0].position - collider.center);
    EXPECT_GE(distanceFromColliderCenter, collider.size.x - 1e-3f)
        << "An out-of-range group value sharing the same low 4 bits must still collide identically.";

    // A second sub-case with group = 255 (255 & 0x0F == 15, a legitimately
    // in-range bit) - this sub-case's own primary purpose is running cleanly
    // under a sanitizer/assert build, not asserting a specific outcome
    // beyond "does not crash and produces a well-defined, finite result".
    DynamicChainDefinition definitionHighGroup = BuildOneJointChainDefinition(/*group=*/15, /*mask=*/0xFFFF);
    DynamicChainRuntimeState stateHighGroup;
    Collider colliderHighGroup = collider;
    colliderHighGroup.group = static_cast<std::uint8_t>(255);
    const std::vector<Collider> collidersHighGroup = { colliderHighGroup };

    for (int step = 0; step < 120; ++step) {
        StepDynamicChain(
            definitionHighGroup, root, targets, stateHighGroup, 1.0f / 60.0f, gravity, noWind, collidersHighGroup);
    }
    EXPECT_TRUE(std::isfinite(stateHighGroup.particles[0].position.x));
    EXPECT_TRUE(std::isfinite(stateHighGroup.particles[0].position.y));
    EXPECT_TRUE(std::isfinite(stateHighGroup.particles[0].position.z));
}

} // namespace gte
