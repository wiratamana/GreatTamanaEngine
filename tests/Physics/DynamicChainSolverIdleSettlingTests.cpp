// Idle settling of a never-animated chain under the default physics settings.
// Calls StepDynamicChain() directly (no ECS).

#include "Physics/DynamicChainSolver.h"
#include "Physics/GlobalPhysicsSettings.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace gte {
namespace {

// A linear chain of `jointCount` joints with the untouched default DynamicJointSettings.
DynamicChainDefinition BuildIdleChainDefinition(std::size_t jointCount, float restLengthPerJoint)
{
    DynamicChainDefinition definition;
    definition.rootBoneIndex = -1;
    definition.jointBoneIndices.resize(jointCount);
    definition.parentJointIndex = DynamicChainDefinition::MakeLinearParentIndices(jointCount);
    definition.jointSettings.assign(jointCount, DynamicJointSettings{});
    definition.restLengths.assign(jointCount, restLengthPerJoint);
    for (std::size_t i = 0; i < jointCount; ++i) {
        definition.jointBoneIndices[i] = static_cast<std::int32_t>(i);
    }
    definition.gravityScale = 1.0f;
    definition.windScale = 1.0f;
    return definition;
}

} // namespace

// A still T-pose chain under the default global settings: sags, settles, never diverges.
// The chain lies along +X, perpendicular to gravity, so it must swing down.
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

    const GlobalPhysicsSettings globalSettings{}; // untouched defaults.
    DynamicChainRuntimeState state;

    const int kTotalSteps = static_cast<int>(10.0f / globalSettings.fixedTimestepSeconds + 0.5f); // 10 simulated seconds.
    const int kSettleDeadlineStep = kTotalSteps / 2;
    const float deadEpsilon = 0.01f * totalRestLength;  // must sag more than 1% of the total rest length.
    const float explodedLimit = 1.5f * totalRestLength; // never farther from the root than this.
    constexpr float kConvergenceEpsilon = 0.01f;         // per-step tip delta must drop below and stay below this.

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

    EXPECT_GT(maxSagFromBind, deadEpsilon) << "Chain never sagged more than 1% of its total rest length.";

    // From the deadline on, the tip must stay below the convergence epsilon (steady state, no endless jitter).
    for (int step = kSettleDeadlineStep; step < kTotalSteps; ++step) {
        EXPECT_LT(perStepTipDelta[step], kConvergenceEpsilon)
            << "step " << step << ": chain was still moving measurably after half the run.";
    }
}

// The same idle scenario with another joint count and non-uniform, sub-unit rest lengths.
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

    const int kTotalSteps = static_cast<int>(10.0f / globalSettings.fixedTimestepSeconds + 0.5f); // 10 simulated seconds.
    const int kSettleDeadlineStep = kTotalSteps / 2;
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
