// Unit tests for task_manager/verlet-integration-7's Phase 5
// (PHASE5_IDLE_PHYSICS_TUNING_AND_SETTLING_REGRESSION.md, Culprit E) - proves
// the RE-TUNED default DynamicJointSettings (Physics/DynamicChainDefinition.h)
// and GlobalPhysicsSettings (Physics/GlobalPhysicsSettings.h) produce a
// believable, STABLE idle result for a chain that is NEVER animated: a
// perfectly still (bind-pose target held perfectly constant, no root motion)
// chain must visibly sag under gravity (not look "dead"/perfectly rigid),
// must genuinely settle into a steady shape within a bounded, short amount of
// simulated time (not oscillate/jitter forever), and must never diverge/
// explode/NaN. Pure Tier 1 - calls StepDynamicChain() directly (no ECS/
// AnimationSystem/PhysicsSystem at all), mirroring
// Physics/DynamicChainSolverTests.cpp's own existing convention.

#include "Physics/DynamicChainSolver.h"
#include "Physics/GlobalPhysicsSettings.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte {
namespace {

// A linear chain of `jointCount` joints, every joint using the real,
// UNTOUCHED default DynamicJointSettings{} - never hand-typed/overridden
// literals - so this test can never silently drift out of sync with the
// actual production defaults it is meant to be tuning/guarding.
DynamicChainDefinition BuildIdleChainDefinition(std::size_t jointCount, float restLengthPerJoint)
{
    DynamicChainDefinition definition;
    definition.rootBoneIndex = -1; // unused by StepDynamicChain itself - only jointBoneIndices' SIZE matters here.
    definition.jointBoneIndices.resize(jointCount);
    definition.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(jointCount);
    definition.jointSettings.assign(jointCount, DynamicJointSettings{});
    definition.restLengths.assign(jointCount, restLengthPerJoint);
    for (std::size_t i = 0; i < jointCount; ++i) {
        definition.jointBoneIndices[i] = static_cast<std::int32_t>(i);
    }
    definition.gravityScale = 1.0f;
    definition.windScale = 1.0f;
    // DynamicChainDefinition's own default constraintIterations (4) is left
    // untouched too - this phase never touches that field.
    return definition;
}

} // namespace

// (a) The headline scenario: a perfectly still, NEVER-animated T-pose chain,
// stepped for 10 simulated seconds (600 steps @ 1/60s) against the real,
// unmodified GlobalPhysicsSettings{} defaults. Extends PERPENDICULAR to
// gravity (along +X, gravity along -Y) - a chain colinear WITH gravity would
// only ever compress/stretch, never genuinely swing/sag sideways, mirroring
// PhysicsSystemTests.cpp's own fixture rationale.
TEST(DynamicChainSolverIdleSettlingTests,
    PerfectlyStillTPoseChainSagsSettlesAndNeverDivergesUnderDefaultGlobalPhysicsSettings)
{
    constexpr std::size_t kJointCount = 5;
    constexpr float kRestLengthPerJoint = 1.0f;
    const float totalRestLength = static_cast<float>(kJointCount) * kRestLengthPerJoint;

    DynamicChainDefinition definition = BuildIdleChainDefinition(kJointCount, kRestLengthPerJoint);

    const Vec3 root = Vec3::Zero();
    std::vector<Vec3> bindPositions(kJointCount);
    for (std::size_t i = 0; i < kJointCount; ++i) {
        bindPositions[i] = Vec3(static_cast<float>(i + 1) * kRestLengthPerJoint, 0.0f, 0.0f);
    }

    const GlobalPhysicsSettings globalSettings{}; // real, untouched defaults - never hand-typed literals.
    DynamicChainRuntimeState state;

    constexpr int kTotalSteps = 600;         // 10 simulated seconds at the default 1/60s fixed timestep.
    constexpr int kSettleDeadlineStep = 300; // half the run - "genuinely settles" must happen well before this.
    const float deadEpsilon = 0.01f * totalRestLength;  // "not dead": must sag more than 1% of total rest length.
    const float explodedLimit = 1.5f * totalRestLength; // "not exploded": never farther from root than this.
    constexpr float kConvergenceEpsilon = 0.01f;         // per-step tip delta must drop below and STAY below this.

    float maxSagFromBind = 0.0f;
    std::vector<float> perStepTipDelta(kTotalSteps, 0.0f);
    Vec3 previousTip = bindPositions.back();

    for (int step = 0; step < kTotalSteps; ++step) {
        StepDynamicChain(definition, root, bindPositions, state, globalSettings.fixedTimestepSeconds,
            globalSettings.gravity, globalSettings.wind);

        for (const VerletParticle& particle : state.particles) {
            ASSERT_TRUE(std::isfinite(particle.position.x))
                << "step " << step << ": particle position.x became non-finite.";
            ASSERT_TRUE(std::isfinite(particle.position.y))
                << "step " << step << ": particle position.y became non-finite.";
            ASSERT_TRUE(std::isfinite(particle.position.z))
                << "step " << step << ": particle position.z became non-finite.";

            const float distanceFromRoot = Length(particle.position - root);
            EXPECT_LE(distanceFromRoot, explodedLimit)
                << "step " << step << ": a particle exploded past 1.5x the chain's own total rest length.";
        }

        const Vec3& tip = state.particles.back().position;
        const float sagFromBind = Length(tip - bindPositions.back());
        maxSagFromBind = std::max(maxSagFromBind, sagFromBind);

        perStepTipDelta[step] = Length(tip - previousTip);
        previousTip = tip;
    }

    EXPECT_GT(maxSagFromBind, deadEpsilon)
        << "Chain never sagged more than 1% of its own total rest length under gravity - looks \"dead\"/rigid, "
           "exactly the complaint this campaign exists to fix.";

    // "Genuinely settles": from the deadline step onward, the tip's own
    // per-step motion must never re-exceed the convergence epsilon - proves
    // it reaches and HOLDS a steady-state shape rather than oscillating/
    // jittering forever.
    for (int step = kSettleDeadlineStep; step < kTotalSteps; ++step) {
        EXPECT_LT(perStepTipDelta[step], kConvergenceEpsilon)
            << "step " << step << ": chain was still moving measurably after half the run - never settled.";
    }
}

// (b) The same idle scenario generalizes to a DIFFERENT joint count and
// NON-UNIFORM, sub-unit rest lengths (a 4-joint chain with varied segment
// lengths, closer to a real imported hair/skirt chain's own bind-pose
// geometry than the uniform 1.0-per-joint fixture above) - proves the tuned
// defaults were not accidentally overfit to one specific chain shape.
TEST(DynamicChainSolverIdleSettlingTests, GeneralizesToADifferentJointCountAndNonUniformRestLengths)
{
    const std::vector<float> restLengths = { 0.6f, 0.8f, 0.5f, 0.7f };
    const std::size_t jointCount = restLengths.size();
    float totalRestLength = 0.0f;
    for (float restLength : restLengths) {
        totalRestLength += restLength;
    }

    DynamicChainDefinition definition;
    definition.rootBoneIndex = -1;
    definition.jointBoneIndices.resize(jointCount);
    definition.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(jointCount);
    definition.jointSettings.assign(jointCount, DynamicJointSettings{});
    definition.restLengths = restLengths;
    for (std::size_t i = 0; i < jointCount; ++i) {
        definition.jointBoneIndices[i] = static_cast<std::int32_t>(i);
    }
    definition.gravityScale = 1.0f;
    definition.windScale = 1.0f;

    const Vec3 root = Vec3::Zero();
    std::vector<Vec3> bindPositions(jointCount);
    float cumulative = 0.0f;
    for (std::size_t i = 0; i < jointCount; ++i) {
        cumulative += restLengths[i];
        bindPositions[i] = Vec3(cumulative, 0.0f, 0.0f);
    }

    const GlobalPhysicsSettings globalSettings{};
    DynamicChainRuntimeState state;

    constexpr int kTotalSteps = 600;
    constexpr int kSettleDeadlineStep = 300;
    const float deadEpsilon = 0.01f * totalRestLength;
    const float explodedLimit = 1.5f * totalRestLength;
    constexpr float kConvergenceEpsilon = 0.01f;

    float maxSagFromBind = 0.0f;
    std::vector<float> perStepTipDelta(kTotalSteps, 0.0f);
    Vec3 previousTip = bindPositions.back();

    for (int step = 0; step < kTotalSteps; ++step) {
        StepDynamicChain(definition, root, bindPositions, state, globalSettings.fixedTimestepSeconds,
            globalSettings.gravity, globalSettings.wind);

        for (const VerletParticle& particle : state.particles) {
            ASSERT_TRUE(std::isfinite(particle.position.x));
            ASSERT_TRUE(std::isfinite(particle.position.y));
            ASSERT_TRUE(std::isfinite(particle.position.z));
            EXPECT_LE(Length(particle.position - root), explodedLimit);
        }

        const Vec3& tip = state.particles.back().position;
        maxSagFromBind = std::max(maxSagFromBind, Length(tip - bindPositions.back()));
        perStepTipDelta[step] = Length(tip - previousTip);
        previousTip = tip;
    }

    EXPECT_GT(maxSagFromBind, deadEpsilon);
    for (int step = kSettleDeadlineStep; step < kTotalSteps; ++step) {
        EXPECT_LT(perStepTipDelta[step], kConvergenceEpsilon)
            << "step " << step << ": non-uniform-rest-length chain never settled.";
    }
}

} // namespace gte
