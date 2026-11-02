// Collision fit radius, ignored colliders and step diagnostics of StepDynamicChain().

#include "Physics/DynamicChainSolver.h"

#include <gtest/gtest.h>

#include <cstdint>
#include <utility>
#include <vector>

namespace gte {
namespace {

constexpr float kDt = 1.0f / 120.0f;

Collider MakeSphere(const Vec3& center, float radius)
{
    Collider collider;
    collider.shape = ColliderShape::Sphere;
    collider.center = center;
    collider.size = Vec3(radius, 0.0f, 0.0f);
    return collider;
}

// One free joint (no rods, no goal pull) with collision on.
DynamicChainDefinition BuildCollidingJoint(float collisionRadius, float fitRadius, std::vector<std::uint16_t> ignored)
{
    DynamicChainDefinition definition;
    definition.rootBoneIndex = -1;
    definition.jointBoneIndices = { 0 };
    definition.parentJointIndex = { -1 };
    definition.restLengths = { 1.0f };
    definition.constraintIterations = 0;
    definition.collisionEnabled = true;

    DynamicJointSettings settings;
    settings.stiffness = 0.0f;
    settings.collisionRadius = collisionRadius;
    settings.collisionFitRadius = fitRadius;
    settings.ignoredColliderIndices = std::move(ignored);
    definition.jointSettings = { settings };
    return definition;
}

// Seeds the joint at `position`, steps once and returns the final particle position.
Vec3 StepOnce(const DynamicChainDefinition& definition, const Vec3& position, const std::vector<Collider>& colliders,
    const StepOptions& options)
{
    DynamicChainRuntimeState state;
    StepDynamicChain(definition, Vec3::Zero(), { position }, state, kDt, Vec3::Zero(), WindSettings{}, colliders, options);
    return state.particles[0].position;
}

} // namespace

TEST(DynamicChainSolverCollisionFitTests, FitRadiusKeepsAJointThatStartsNearTheSurfaceFromBeingPushed)
{
    const DynamicChainDefinition definition = BuildCollidingJoint(0.2f, 0.05f, {});
    const std::vector<Collider> colliders = { MakeSphere(Vec3::Zero(), 1.0f) };
    const Vec3 start(1.05f, 0.0f, 0.0f);

    StepDiagnostics diagnostics;
    StepOptions fitOn;
    fitOn.diagnostics = &diagnostics;
    fitOn.useCollisionFit = true;
    const Vec3 fitted = StepOnce(definition, start, colliders, fitOn);
    EXPECT_NEAR(fitted.x, 1.05f, 1e-5f);
    EXPECT_EQ(diagnostics.collisionPushedParticles, 0u);

    diagnostics = StepDiagnostics{};
    StepOptions fitOff;
    fitOff.diagnostics = &diagnostics;
    fitOff.useCollisionFit = false;
    const Vec3 raw = StepOnce(definition, start, colliders, fitOff);
    EXPECT_NEAR(raw.x, 1.2f, 1e-5f);
    EXPECT_EQ(diagnostics.collisionPushedParticles, 1u);
    EXPECT_NEAR(diagnostics.collisionPushDistanceSum, 0.15f, 1e-4f);
}

TEST(DynamicChainSolverCollisionFitTests, NegativeFitRadiusMeansUseTheRawRadius)
{
    const DynamicChainDefinition definition = BuildCollidingJoint(0.2f, -1.0f, {});
    const std::vector<Collider> colliders = { MakeSphere(Vec3::Zero(), 1.0f) };

    StepOptions options;
    options.useCollisionFit = true;
    EXPECT_NEAR(StepOnce(definition, Vec3(1.05f, 0.0f, 0.0f), colliders, options).x, 1.2f, 1e-5f);
}

TEST(DynamicChainSolverCollisionFitTests, IgnoredColliderIsSkippedOnlyWhileFitIsOn)
{
    // Collider 0 contains the joint origin; collider 1 is far away and irrelevant.
    const DynamicChainDefinition definition = BuildCollidingJoint(0.0f, -1.0f, { 0 });
    const std::vector<Collider> colliders = { MakeSphere(Vec3::Zero(), 1.0f), MakeSphere(Vec3(10.0f, 0.0f, 0.0f), 0.1f) };
    const Vec3 start(0.5f, 0.0f, 0.0f);

    StepOptions fitOn;
    fitOn.useCollisionFit = true;
    EXPECT_NEAR(StepOnce(definition, start, colliders, fitOn).x, 0.5f, 1e-5f);

    StepOptions fitOff;
    fitOff.useCollisionFit = false;
    EXPECT_NEAR(StepOnce(definition, start, colliders, fitOff).x, 1.0f, 1e-5f);
}

TEST(DynamicChainSolverCollisionFitTests, OnlyTheListedColliderIsIgnored)
{
    const DynamicChainDefinition definition = BuildCollidingJoint(0.0f, -1.0f, { 1 });
    const std::vector<Collider> colliders = { MakeSphere(Vec3::Zero(), 1.0f), MakeSphere(Vec3(0.0f, 0.0f, 0.0f), 2.0f) };

    StepOptions options;
    options.useCollisionFit = true;
    // Collider 1 is ignored, collider 0 still pushes the joint to its own surface.
    EXPECT_NEAR(StepOnce(definition, Vec3(0.5f, 0.0f, 0.0f), colliders, options).x, 1.0f, 1e-5f);
}

TEST(DynamicChainSolverCollisionFitTests, DiagnosticsCountReseedsAfterTheFirstSeedOnly)
{
    const DynamicChainDefinition definition = BuildCollidingJoint(0.0f, -1.0f, {});
    DynamicChainRuntimeState state;
    StepDiagnostics diagnostics;
    StepOptions options;
    options.diagnostics = &diagnostics;
    const std::vector<Vec3> targets = { Vec3(1.0f, 0.0f, 0.0f) };

    StepDynamicChain(definition, Vec3::Zero(), targets, state, kDt, Vec3::Zero(), WindSettings{}, {}, options);
    EXPECT_EQ(diagnostics.reseeds, 0u);
    EXPECT_EQ(state.reseedCount, 0u);

    // A root jump past maxPlausibleRootDelta re-seeds the chain.
    const Vec3 teleported(definition.maxPlausibleRootDelta * 10.0f, 0.0f, 0.0f);
    StepDynamicChain(definition, teleported, targets, state, kDt, Vec3::Zero(), WindSettings{}, {}, options);
    EXPECT_EQ(diagnostics.reseeds, 1u);
    EXPECT_EQ(state.reseedCount, 1u);

    // A normal step afterwards does not.
    StepDynamicChain(definition, teleported, targets, state, kDt, Vec3::Zero(), WindSettings{}, {}, options);
    EXPECT_EQ(diagnostics.reseeds, 1u);
}

} // namespace gte
