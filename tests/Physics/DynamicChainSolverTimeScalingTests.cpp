// Step-length independence of damping and goal pull in StepDynamicChain().

#include "Physics/DynamicChainSolver.h"
#include "Physics/GlobalPhysicsSettings.h"
#include "Physics/TimeScaling.h"

#include <gtest/gtest.h>

#include <cmath>
#include <cstdint>
#include <vector>

namespace gte {
namespace {

// One joint hanging from the anchor with no rods (zero iterations) and no collision.
DynamicChainDefinition BuildFreeParticle(const DynamicJointSettings& settings)
{
    DynamicChainDefinition definition;
    definition.rootBoneIndex = -1;
    definition.jointBoneIndices = { 0 };
    definition.jointSettings = { settings };
    definition.parentJointIndex = { -1 };
    definition.restLengths = { 1.0f };
    definition.constraintIterations = 0;
    definition.collisionEnabled = false;
    return definition;
}

// A seeded state with the particle at the origin moving `unitsPerSecond` along +X.
DynamicChainRuntimeState MakeMovingState(float unitsPerSecond, float stepSeconds)
{
    DynamicChainRuntimeState state;
    state.initialized = true;
    state.particles.resize(1);
    state.particles[0].position = Vec3::Zero();
    state.particles[0].previousPosition = Vec3(-unitsPerSecond * stepSeconds, 0.0f, 0.0f);
    return state;
}

DynamicJointSettings DampedSettings(float damping)
{
    DynamicJointSettings settings;
    settings.damping = damping;
    settings.stiffness = 0.0f;
    return settings;
}

// Runs `steps` steps of `stepSeconds` with no gravity and returns the final state.
DynamicChainRuntimeState RunFree(const DynamicChainDefinition& definition, DynamicChainRuntimeState state,
    int steps, float stepSeconds, const StepOptions& options = {})
{
    const std::vector<Vec3> targets = { Vec3::Zero() };
    for (int i = 0; i < steps; ++i) {
        StepDynamicChain(definition, Vec3::Zero(), targets, state, stepSeconds, Vec3::Zero(), WindSettings{}, {}, options);
    }
    return state;
}

float Speed(const DynamicChainRuntimeState& state)
{
    return Length(state.particles[0].position - state.particles[0].previousPosition);
}

// Root (0,0,0), one joint on the unit circle pulled toward (1,0,0); velocity is killed every step.
DynamicChainDefinition BuildGoalPull()
{
    DynamicJointSettings settings;
    settings.damping = 1.0f;
    settings.stiffness = 0.02f;
    DynamicChainDefinition definition = BuildFreeParticle(settings);
    definition.constraintIterations = 4;
    return definition;
}

Vec3 RunGoalPull(float stepSeconds, float stiffnessMultiplier)
{
    const DynamicChainDefinition definition = BuildGoalPull();
    DynamicChainRuntimeState state;
    state.initialized = true;
    state.particles.resize(1);
    state.particles[0].position = Vec3(0.0f, 1.0f, 0.0f);
    state.particles[0].previousPosition = Vec3(0.0f, 1.0f, 0.0f);

    StepOptions options;
    options.stiffnessMultiplier = stiffnessMultiplier;
    const std::vector<Vec3> targets = { Vec3(1.0f, 0.0f, 0.0f) };
    const int steps = static_cast<int>(1.0f / stepSeconds + 0.5f);
    for (int i = 0; i < steps; ++i) {
        StepDynamicChain(definition, Vec3::Zero(), targets, state, stepSeconds, Vec3::Zero(), WindSettings{}, {}, options);
    }
    return state.particles[0].position;
}

} // namespace

TEST(DynamicChainSolverTimeScalingTests, PmxDampingKeepsExpMinusOneOfTheSpeedAfterOneSecond)
{
    const float f = PmxDampingToStepFraction(1.0f, 1.0f);
    const DynamicChainDefinition definition = BuildFreeParticle(DampedSettings(f));
    const float dt = 1.0f / 60.0f;

    const DynamicChainRuntimeState result = RunFree(definition, MakeMovingState(60.0f, dt), 60, dt);
    EXPECT_NEAR(Speed(result), std::exp(-1.0f), std::exp(-1.0f) * 0.01f);
}

TEST(DynamicChainSolverTimeScalingTests, RawPmxDampingActsAsPerStepFraction)
{
    DynamicJointSettings settings = DampedSettings(0.0f);
    settings.pmxLinearDamping = 1.0f;
    const DynamicChainDefinition definition = BuildFreeParticle(settings);
    const float dt = 1.0f / 60.0f;

    StepOptions options;
    options.useRawPmxDamping = true;
    const DynamicChainRuntimeState result = RunFree(definition, MakeMovingState(60.0f, dt), 1, dt, options);
    EXPECT_EQ(Speed(result), 0.0f);
}

TEST(DynamicChainSolverTimeScalingTests, DampingGivesTheSameDistanceAt60And120Hz)
{
    const float f = PmxDampingToStepFraction(1.0f, 1.0f);
    const DynamicChainDefinition definition = BuildFreeParticle(DampedSettings(f));

    const DynamicChainRuntimeState at60 = RunFree(definition, MakeMovingState(60.0f, 1.0f / 60.0f), 60, 1.0f / 60.0f);
    const DynamicChainRuntimeState at120 = RunFree(definition, MakeMovingState(60.0f, 1.0f / 120.0f), 120, 1.0f / 120.0f);

    const float x60 = at60.particles[0].position.x;
    const float x120 = at120.particles[0].position.x;
    EXPECT_NEAR(x60, x120, 0.01f * x120);
}

TEST(DynamicChainSolverTimeScalingTests, GoalPullGivesTheSameResultAt60And120Hz)
{
    const Vec3 at60 = RunGoalPull(1.0f / 60.0f, 1.0f);
    const Vec3 at120 = RunGoalPull(1.0f / 120.0f, 1.0f);

    EXPECT_GT(Length(at60 - Vec3(0.0f, 1.0f, 0.0f)), 0.3f); // the pull really moved the joint.
    EXPECT_LT(Length(at60 - at120), 0.03f);
}

TEST(DynamicChainSolverTimeScalingTests, DefaultGlobalSettingsUseMmdGravityAndStepRate)
{
    const GlobalPhysicsSettings settings{};
    EXPECT_FLOAT_EQ(settings.gravity.y, -98.0f);
    EXPECT_FLOAT_EQ(settings.gravity.x, 0.0f);
    EXPECT_FLOAT_EQ(settings.gravity.z, 0.0f);
    EXPECT_FLOAT_EQ(settings.fixedTimestepSeconds, 1.0f / 120.0f);
    EXPECT_EQ(settings.maxStepsPerFrame, 10);
}

TEST(DynamicChainSolverTimeScalingTests, DampingMultiplierZeroKeepsAllSpeed)
{
    const float f = PmxDampingToStepFraction(1.0f, 1.0f);
    const DynamicChainDefinition definition = BuildFreeParticle(DampedSettings(f));
    const float dt = 1.0f / 60.0f;

    StepOptions options;
    options.dampingMultiplier = 0.0f;
    const DynamicChainRuntimeState result = RunFree(definition, MakeMovingState(60.0f, dt), 60, dt, options);
    EXPECT_NEAR(Speed(result), 1.0f, 1e-4f);
}

TEST(DynamicChainSolverTimeScalingTests, StiffnessMultiplierZeroSwitchesTheGoalPullOff)
{
    const Vec3 result = RunGoalPull(1.0f / 60.0f, 0.0f);
    EXPECT_LT(Length(result - Vec3(0.0f, 1.0f, 0.0f)), 1e-4f);
}

} // namespace gte
