// Unit tests for StepDynamicChain (src/Physics/DynamicChainSolver.h) - the
// chain-level orchestration that advances one dynamic bone chain by exactly
// one fixed timestep of Verlet integration + structural + goal constraints
// (verlet-integration-1 campaign, PHASE2_BONE_CHAIN_PHYSICS_BRIDGE.md). No
// ECS/GPU/Renderer/SkeletonData involved - StepDynamicChain operates purely
// on plain Vec3 positions.

#include "Physics/DynamicChainSolver.h"

#include "Physics/ChainConstraints.h"

#include <gtest/gtest.h>

#include <cmath>

namespace gte {
namespace {

DynamicChainDefinition BuildThreeJointChainDefinition(float stiffness, float damping = 0.1f)
{
    DynamicChainDefinition definition;
    definition.rootBoneIndex = -1; // unused by StepDynamicChain itself - only jointBoneIndices' SIZE matters here.
    definition.jointBoneIndices = { 0, 1, 2 };
    definition.jointSettings = {
        DynamicJointSettings{ damping, stiffness, 1.0f },
        DynamicJointSettings{ damping, stiffness, 1.0f },
        DynamicJointSettings{ damping, stiffness, 1.0f },
    };
    definition.restLengths = { 1.0f, 1.0f, 1.0f };
    definition.gravityScale = 1.0f;
    definition.windScale = 1.0f;
    definition.constraintIterations = 4;
    return definition;
}

} // namespace

// (a) stiffness == 1.0 on every joint, zero gravity/wind: the chain stays
// EXACTLY glued to the animated target every frame, no matter where the
// particles started - proves the goal constraint (applied once, last) fully
// dominates whatever integration/structural relaxation did that same step.
TEST(DynamicChainSolverTests, FullStiffnessStaysGluedToAnimatedTargetEveryFrame)
{
    DynamicChainDefinition definition = BuildThreeJointChainDefinition(/*stiffness=*/1.0f);

    DynamicChainRuntimeState state;
    // Seed particles somewhere else entirely (not on the animated targets,
    // not at the rest lengths from the root either) - lazy init would
    // normally seed them onto the FIRST call's targets, so pre-populate by
    // hand to genuinely start "away".
    state.initialized = true;
    state.particles.resize(3);
    state.particles[0].position = Vec3(5.0f, 5.0f, 5.0f);
    state.particles[0].previousPosition = Vec3(5.0f, 5.0f, 5.0f);
    state.particles[1].position = Vec3(-3.0f, 2.0f, 1.0f);
    state.particles[1].previousPosition = Vec3(-3.0f, 2.0f, 1.0f);
    state.particles[2].position = Vec3(0.0f, -8.0f, 4.0f);
    state.particles[2].previousPosition = Vec3(0.0f, -8.0f, 4.0f);

    const Vec3 root(0.0f, 3.0f, 0.0f);
    const WindSettings noWind{};

    for (int frame = 0; frame < 5; ++frame) {
        // A different animated target every frame (as if the FK pose is
        // itself moving), straight down from root, restLength apart.
        const float sway = std::sin(static_cast<float>(frame) * 0.3f);
        const std::vector<Vec3> targets = {
            Vec3(sway, 2.0f, 0.0f),
            Vec3(sway, 1.0f, 0.0f),
            Vec3(sway, 0.0f, 0.0f),
        };

        StepDynamicChain(definition, root, targets, state, 1.0f / 60.0f, Vec3::Zero(), noWind);

        for (std::size_t i = 0; i < targets.size(); ++i) {
            EXPECT_TRUE(ApproximatelyEqual(state.particles[i].position, targets[i], 1e-4f))
                << "Frame " << frame << ", joint " << i << ": stiffness=1.0 must snap exactly onto the animated target.";
        }
    }
}

// (b) stiffness == 0.0, gravity pointing down: the chain visibly sags below
// its animated target over several steps, but never stretches past
// sum(restLengths) * 1.01 - proves the distance constraint bounds it.
TEST(DynamicChainSolverTests, ZeroStiffnessSagsUnderGravityButNeverStretchesPastRestLength)
{
    DynamicChainDefinition definition = BuildThreeJointChainDefinition(/*stiffness=*/0.0f, /*damping=*/0.05f);

    DynamicChainRuntimeState state; // lazy-initialized on first StepDynamicChain call below.

    const Vec3 root(0.0f, 0.0f, 0.0f);
    // Horizontal animated targets (same height as root) - any downward sag
    // is therefore unambiguous.
    const std::vector<Vec3> targets = { Vec3(1.0f, 0.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f), Vec3(3.0f, 0.0f, 0.0f) };
    const Vec3 gravity(0.0f, -9.8f, 0.0f);
    const WindSettings noWind{};

    const float sumRestLengths = 3.0f;
    for (int step = 0; step < 120; ++step) {
        StepDynamicChain(definition, root, targets, state, 1.0f / 60.0f, gravity, noWind);

        for (const VerletParticle& particle : state.particles) {
            const float distanceFromRoot = Length(particle.position - root);
            EXPECT_LE(distanceFromRoot, sumRestLengths * 1.01f)
                << "A particle stretched past the chain's combined rest length.";
        }
    }

    // After two seconds of free fall (stiffness == 0.0 means the goal
    // constraint is a documented no-op), the tip joint must have sagged
    // measurably below its animated (horizontal) target height.
    EXPECT_LT(state.particles.back().position.y, targets.back().y - 0.05f)
        << "Tip joint did not sag below its animated target under gravity with zero stiffness.";
}

// (c) Calling StepDynamicChain repeatedly with an unchanged animated target
// and moderate damping converges toward a stable rest position rather than
// oscillating/growing without bound forever - numerical sanity check.
TEST(DynamicChainSolverTests, RepeatedStepsConvergeRatherThanDivergeOrOscillateForever)
{
    DynamicChainDefinition definition = BuildThreeJointChainDefinition(/*stiffness=*/0.2f, /*damping=*/0.15f);

    DynamicChainRuntimeState state;
    const Vec3 root(0.0f, 0.0f, 0.0f);
    const std::vector<Vec3> targets = { Vec3(1.0f, 0.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f), Vec3(3.0f, 0.0f, 0.0f) };
    const Vec3 gravity(0.0f, -9.8f, 0.0f);
    const WindSettings noWind{};
    const float dt = 1.0f / 60.0f;
    const float sumRestLengths = 3.0f;

    for (int step = 0; step < 300; ++step) {
        StepDynamicChain(definition, root, targets, state, dt, gravity, noWind);

        for (const VerletParticle& particle : state.particles) {
            ASSERT_TRUE(std::isfinite(particle.position.x));
            ASSERT_TRUE(std::isfinite(particle.position.y));
            ASSERT_TRUE(std::isfinite(particle.position.z));
            // Never blows up past a generous multiple of the chain's own
            // combined rest length - a genuinely diverging/exploding
            // simulation would violate this quickly.
            ASSERT_LE(Length(particle.position - root), sumRestLengths * 1.5f);
        }
    }

    // By the end, the implied velocity of every joint must have settled to
    // a small value - a perpetually oscillating/diverging simulation would
    // still show large per-step motion after 300 steps (5 seconds).
    for (const VerletParticle& particle : state.particles) {
        const Vec3 impliedVelocity = ImpliedVelocity(particle, dt);
        EXPECT_LT(Length(impliedVelocity), 1.0f) << "Chain did not settle - still moving fast after 5 seconds.";
    }
}

// (d) Regression test (PHASE0 Revision Notes, finding #3): changing
// constraintIterations alone must never change how strongly `stiffness`
// pulls toward the animated target - the goal constraint runs exactly ONCE
// per call, after every structural iteration, never inside that loop.
TEST(DynamicChainSolverTests, StiffnessAndConstraintIterationsAreFullyDecoupled)
{
    const float stiffness = 0.5f;
    const float restLength = 1.0f;
    const Vec3 root(0.0f, 0.0f, 0.0f);
    const std::vector<Vec3> targets = { Vec3(0.0f, -1.0f, 0.0f) };
    const WindSettings noWind{};
    const float dt = 1.0f / 60.0f;

    auto buildDefinition = [&](std::uint8_t iterations) {
        DynamicChainDefinition definition;
        definition.rootBoneIndex = -1;
        definition.jointBoneIndices = { 0 };
        definition.jointSettings = { DynamicJointSettings{ 0.0f, stiffness, 1.0f } };
        definition.restLengths = { restLength };
        definition.gravityScale = 1.0f;
        definition.windScale = 1.0f;
        definition.constraintIterations = iterations;
        return definition;
    };

    auto computeReferencePreGoalPosition = [&](int iterations) {
        // Replicates StepDynamicChain's own structural-only loop by hand
        // (IntegrateParticle is a documented no-op here: zero implied
        // velocity, zero acceleration, since gravity/wind are both zero),
        // so the resulting "position just before the goal constraint runs"
        // can be independently verified.
        VerletParticle particle;
        particle.position = Vec3(2.0f, 0.0f, 0.0f); // deliberately away from both the rest length and the target.
        particle.previousPosition = particle.position;
        particle.inverseMass = 1.0f;

        for (int i = 0; i < iterations; ++i) {
            VerletParticle anchor;
            anchor.position = root;
            anchor.previousPosition = root;
            anchor.inverseMass = 0.0f;
            anchor.pinned = true;
            SolveDistanceConstraint(anchor, particle, restLength);
        }
        return particle.position;
    };

    for (const std::uint8_t iterations : { std::uint8_t{ 1 }, std::uint8_t{ 8 } }) {
        DynamicChainDefinition definition = buildDefinition(iterations);

        DynamicChainRuntimeState state;
        state.initialized = true;
        state.particles.resize(1);
        state.particles[0].position = Vec3(2.0f, 0.0f, 0.0f);
        state.particles[0].previousPosition = Vec3(2.0f, 0.0f, 0.0f);
        state.particles[0].inverseMass = 1.0f;

        StepDynamicChain(definition, root, targets, state, dt, Vec3::Zero(), noWind);

        const Vec3 preGoalPosition = computeReferencePreGoalPosition(static_cast<int>(iterations));
        const Vec3 expectedFinalPosition = Lerp(preGoalPosition, targets[0], stiffness);

        EXPECT_TRUE(ApproximatelyEqual(state.particles[0].position, expectedFinalPosition, 1e-4f))
            << "constraintIterations=" << static_cast<int>(iterations)
            << ": goal constraint result did not match Lerp(preGoalPosition, target, stiffness) exactly once.";

        // The FRACTION of the remaining (pre-goal -> target) distance the
        // goal step actually covered must equal `stiffness` regardless of
        // constraintIterations - the coupling bug this test guards against
        // would instead show 1-(1-stiffness)^iterations for iterations > 1.
        const float remainingDistance = Length(targets[0] - preGoalPosition);
        if (remainingDistance > 1e-5f) {
            const float coveredDistance = Length(state.particles[0].position - preGoalPosition);
            const float fraction = coveredDistance / remainingDistance;
            EXPECT_NEAR(fraction, stiffness, 1e-3f)
                << "constraintIterations=" << static_cast<int>(iterations)
                << ": stiffness pull fraction leaked constraintIterations coupling.";
        }
    }
}

} // namespace gte
