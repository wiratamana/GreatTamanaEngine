#include "DynamicChainSolver.h"

#include "ChainConstraints.h"
#include "VerletIntegration.h"
#include "../Math/MathTypes.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace gte {

namespace {

// Guards against a caller-supplied (or malformed data-driven) mass <= 0
// producing an infinite/negative inverseMass - mirrors ChainConstraints.cpp's
// own "degrade gracefully instead of NaN/Inf" convention.
constexpr float kMinMass = 1e-4f;

void SeedParticlesFromAnimatedPose(
    const DynamicChainDefinition& definition, const std::vector<Vec3>& animatedJointWorldPositions,
    DynamicChainRuntimeState& state, std::size_t jointCount)
{
    state.particles.resize(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        VerletParticle& particle = state.particles[i];
        particle.position = animatedJointWorldPositions[i];
        particle.previousPosition = animatedJointWorldPositions[i];
        particle.inverseMass = 1.0f / std::max(definition.jointSettings[i].mass, kMinMass);
        particle.pinned = false;
    }
}

bool IsFinite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

void StepDynamicChain(const DynamicChainDefinition& definition, const Vec3& rootWorldPosition,
    const std::vector<Vec3>& animatedJointWorldPositions, DynamicChainRuntimeState& state, float fixedDeltaTime,
    const Vec3& gravity, const WindSettings& wind, const SphereCollider* collider)
{
    const std::size_t jointCount = definition.jointBoneIndices.size();
    if (definition.jointSettings.size() != jointCount || definition.restLengths.size() != jointCount
        || animatedJointWorldPositions.size() != jointCount) {
        return; // Malformed/stale definition - never read/write out of bounds.
    }
    if (jointCount == 0) {
        return;
    }

    // 1. Lazy init - also re-seeds if the chain's own joint count ever
    // changes after the first call (shouldn't normally happen for a fixed,
    // authoring-time chain, but never read/write out of bounds either way) -
    // OR (PHASE5, 3.3) the root bone has moved an implausible distance since
    // the last call, e.g. a teleporting character/Editor gizmo drag.
    bool needsSeed = !state.initialized || state.particles.size() != jointCount;
    if (!needsSeed) {
        const float rootDelta = Length(rootWorldPosition - state.lastRootWorldPosition);
        if (rootDelta > definition.maxPlausibleRootDelta) {
            needsSeed = true;
        }
    }
    if (needsSeed) {
        SeedParticlesFromAnimatedPose(definition, animatedJointWorldPositions, state, jointCount);
        state.initialized = true;
    }
    // Set UNCONDITIONALLY, exactly once per call, regardless of which branch
    // above ran - forgetting this turns the teleport guard into a permanent,
    // one-shot trip (see this file's own header comment, step 1).
    state.lastRootWorldPosition = rootWorldPosition;

    // 2. Integrate every joint particle under gravity + wind.
    for (std::size_t i = 0; i < jointCount; ++i) {
        VerletParticle& particle = state.particles[i];
        const Vec3 acceleration = gravity * definition.gravityScale
            + ComputeWindAcceleration(wind, particle.position, state.simulationTimeSeconds) * definition.windScale;
        IntegrateParticle(particle, fixedDeltaTime, acceleration, definition.jointSettings[i].damping);
    }

    // 3. Constrain-structural: repeated `constraintIterations` times, root
    // -> tip. The root anchor is a local, stack-allocated particle each
    // iteration - it is never itself simulated/stored in `state`.
    const int iterations = static_cast<int>(definition.constraintIterations);
    for (int iter = 0; iter < iterations; ++iter) {
        VerletParticle anchor;
        anchor.position = rootWorldPosition;
        anchor.previousPosition = rootWorldPosition;
        anchor.inverseMass = 0.0f;
        anchor.pinned = true;

        SolveDistanceConstraint(anchor, state.particles[0], definition.restLengths[0]);
        for (std::size_t i = 1; i < jointCount; ++i) {
            SolveDistanceConstraint(state.particles[i - 1], state.particles[i], definition.restLengths[i]);
        }
    }

    // 4. Constrain-goal - exactly ONCE per call, AFTER structural relaxation
    // has already converged the rod lengths this step (never inside the
    // loop above) - this is what keeps `stiffness` and `constraintIterations`
    // fully decoupled (see this file's own header comment, step 4).
    for (std::size_t i = 0; i < jointCount; ++i) {
        SolveGoalConstraint(state.particles[i], animatedJointWorldPositions[i], definition.jointSettings[i].stiffness);
    }

    // 5. Collision (PHASE5, 3.2) - exactly ONCE per call, AFTER the goal
    // constraint, so collision has the final say (structural, then soft/
    // goal, then hard collision).
    if (definition.hasHeadCollider && collider != nullptr) {
        for (std::size_t i = 0; i < jointCount; ++i) {
            SolveSphereCollision(state.particles[i], *collider);
        }
    }

    // 6. NaN/Inf guard (PHASE5, 3.3) - reset only the AFFECTED particle, not
    // the whole chain, so one bad joint never permanently corrupts its
    // siblings.
    for (std::size_t i = 0; i < jointCount; ++i) {
        VerletParticle& particle = state.particles[i];
        if (!IsFinite(particle.position)) {
            assert(false && "DynamicChainSolver: a particle's position became non-finite (NaN/Inf) - resetting to its animated target.");
            particle.position = animatedJointWorldPositions[i];
            particle.previousPosition = animatedJointWorldPositions[i];
        }
    }

    // 7. Advance the simulation clock.
    state.simulationTimeSeconds += fixedDeltaTime;
}

} // namespace gte
