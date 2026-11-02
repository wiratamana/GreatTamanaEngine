#include "DynamicChainSolver.h"

#include "ChainConstraints.h"
#include "CollisionFilter.h"
#include "TimeScaling.h"
#include "VerletIntegration.h"
#include "../Math/MathTypes.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gte {

namespace {

// Lower bound of a joint mass, keeps inverseMass finite.
constexpr float kMinMass = 1e-4f;
// A collision move longer than this counts as a push.
constexpr float kPushEpsilon = 1e-6f;

void SeedParticlesFromAnimatedPose(const DynamicChainDefinition& definition,
    const std::vector<Vec3>& animatedJointWorldPositions, DynamicChainRuntimeState& state, std::size_t jointCount)
{
    state.particles.resize(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        VerletParticle& particle = state.particles[i];
        particle.position = animatedJointWorldPositions[i];
        particle.previousPosition = animatedJointWorldPositions[i];
        particle.inverseMass = 1.0f / std::max(definition.jointSettings[i].mass, kMinMass);
        particle.collisionRadius = definition.jointSettings[i].collisionRadius;
        particle.pinned = false;
    }
}

bool IsFinite(const Vec3& v) noexcept
{
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

bool IsIgnoredCollider(const std::vector<std::uint16_t>& sortedIgnored, std::size_t colliderIndex) noexcept
{
    return !sortedIgnored.empty() && colliderIndex <= 0xFFFFu
        && std::binary_search(sortedIgnored.begin(), sortedIgnored.end(), static_cast<std::uint16_t>(colliderIndex));
}

} // namespace

void ComputeEffectiveBendLimits(const DynamicChainDefinition& definition, const StepOptions& options, std::vector<float>& out)
{
    const std::size_t jointCount = definition.jointSettings.size();
    out.assign(jointCount, kPi);
    if (!options.bendLimitsEnabled) {
        return;
    }

    // Cumulative own limits from the anchor down; parents always come first.
    for (std::size_t i = 0; i < jointCount; ++i) {
        const std::int32_t parent = i < definition.parentJointIndex.size() ? definition.parentJointIndex[i] : -1;
        const float parentCumulative
            = (parent >= 0 && static_cast<std::size_t>(parent) < i) ? out[static_cast<std::size_t>(parent)] : 0.0f;
        out[i] = std::min(kPi, definition.jointSettings[i].bendLimitRadians + parentCumulative);
    }
    for (float& limit : out) {
        limit = std::clamp(limit * options.bendLimitScale + options.bendLimitMinRadians, 0.0f, kPi);
    }
}

std::vector<float> ComputeEffectiveBendLimits(const DynamicChainDefinition& definition, const StepOptions& options)
{
    std::vector<float> limits;
    ComputeEffectiveBendLimits(definition, options, limits);
    return limits;
}

void StepDynamicChain(const DynamicChainDefinition& definition, const Vec3& rootWorldPosition,
    const std::vector<Vec3>& animatedJointWorldPositions, DynamicChainRuntimeState& state, float fixedDeltaTime,
    const Vec3& gravity, const WindSettings& wind, const std::vector<Collider>& colliders, const StepOptions& options)
{
    const std::size_t jointCount = definition.jointBoneIndices.size();
    if (definition.jointSettings.size() != jointCount || definition.restLengths.size() != jointCount
        || definition.parentJointIndex.size() != jointCount || animatedJointWorldPositions.size() != jointCount) {
        return; // Malformed definition: never read or write out of bounds.
    }
    if (jointCount == 0) {
        return;
    }

    StepDiagnostics* const diagnostics = options.diagnostics;

    // Seed on the first call, on a joint count change, or after an implausible root jump.
    bool needsSeed = !state.initialized || state.particles.size() != jointCount;
    if (!needsSeed && Length(rootWorldPosition - state.lastRootWorldPosition) > definition.maxPlausibleRootDelta) {
        needsSeed = true;
    }
    if (needsSeed) {
        if (state.initialized) {
            ++state.reseedCount;
            if (diagnostics != nullptr) {
                ++diagnostics->reseeds;
            }
        }
        SeedParticlesFromAnimatedPose(definition, animatedJointWorldPositions, state, jointCount);
        state.initialized = true;
    }
    state.lastRootWorldPosition = rootWorldPosition;

    // Integrate under gravity and wind with step-length damping.
    for (std::size_t i = 0; i < jointCount; ++i) {
        VerletParticle& particle = state.particles[i];
        const DynamicJointSettings& js = definition.jointSettings[i];
        const Vec3 acceleration = gravity * definition.gravityScale
            + ComputeWindAcceleration(wind, particle.position, state.simulationTimeSeconds) * definition.windScale;
        const float baseDamping = (options.useRawPmxDamping && js.pmxLinearDamping >= 0.0f) ? js.pmxLinearDamping : js.damping;
        IntegrateParticle(
            particle, fixedDeltaTime, acceleration, ScaleStepFraction(baseDamping, fixedDeltaTime, options.dampingMultiplier));
    }

    const bool bendLimitsActive = options.bendLimitsEnabled;
    if (bendLimitsActive) {
        ComputeEffectiveBendLimits(definition, options, state.scratchLimits);
    }

    // Rods, braces, then the bend cone, repeated.
    const int iterations = static_cast<int>(definition.constraintIterations);
    for (int iter = 0; iter < iterations; ++iter) {
        VerletParticle anchor;
        anchor.position = rootWorldPosition;
        anchor.previousPosition = rootWorldPosition;
        anchor.inverseMass = 0.0f;
        anchor.pinned = true;

        for (std::size_t i = 0; i < jointCount; ++i) {
            const std::int32_t parentJoint = definition.parentJointIndex[i];
            if (parentJoint < 0) {
                SolveDistanceConstraint(anchor, state.particles[i], definition.restLengths[i]);
            } else {
                SolveDistanceConstraint(state.particles[static_cast<std::size_t>(parentJoint)], state.particles[i],
                    definition.restLengths[i]);
            }
        }
        for (const ExtraStructuralConstraint& extra : definition.extraConstraints) {
            if (extra.jointIndexA < 0 || extra.jointIndexB < 0
                || static_cast<std::size_t>(extra.jointIndexA) >= jointCount
                || static_cast<std::size_t>(extra.jointIndexB) >= jointCount) {
                continue;
            }
            SolveDistanceConstraint(state.particles[static_cast<std::size_t>(extra.jointIndexA)],
                state.particles[static_cast<std::size_t>(extra.jointIndexB)], extra.restLength);
        }

        if (bendLimitsActive) {
            const bool countClamps = (iter == 0); // each joint counts at most once per call.
            for (std::size_t i = 0; i < jointCount; ++i) {
                const std::int32_t parentJoint = definition.parentJointIndex[i];
                const bool isRootChild = parentJoint < 0;
                const Vec3& parentSim = isRootChild ? rootWorldPosition : state.particles[static_cast<std::size_t>(parentJoint)].position;
                const Vec3& parentAnim = isRootChild ? rootWorldPosition : animatedJointWorldPositions[static_cast<std::size_t>(parentJoint)];
                const bool moved = SolveBendLimit(state.particles[i], parentSim, parentAnim, animatedJointWorldPositions[i],
                    state.scratchLimits[i]);
                if (moved && countClamps && diagnostics != nullptr) {
                    ++diagnostics->bendClampedJoints;
                }
            }
        }
    }

    // Soft pull toward the animated pose, once per step.
    for (std::size_t i = 0; i < jointCount; ++i) {
        SolveGoalConstraint(state.particles[i], animatedJointWorldPositions[i],
            ScaleStepFraction(definition.jointSettings[i].stiffness, fixedDeltaTime, options.stiffnessMultiplier));
    }

    // Collision has the final say.
    if (definition.collisionEnabled && !colliders.empty()) {
        for (std::size_t i = 0; i < jointCount; ++i) {
            const DynamicJointSettings& js = definition.jointSettings[i];
            VerletParticle& particle = state.particles[i];
            particle.collisionRadius
                = (options.useCollisionFit && js.collisionFitRadius >= 0.0f) ? js.collisionFitRadius : js.collisionRadius;
            const Vec3 before = particle.position;
            for (std::size_t k = 0; k < colliders.size(); ++k) {
                const Collider& collider = colliders[k];
                if (options.useCollisionFit && IsIgnoredCollider(js.ignoredColliderIndices, k)) {
                    continue;
                }
                if (!GroupsMayCollide(js.group, js.collisionMask, collider.group, collider.collisionMask)) {
                    continue;
                }
                SolveCollision(particle, collider);
            }
            if (diagnostics != nullptr) {
                const float pushed = Length(particle.position - before);
                if (pushed > kPushEpsilon) {
                    ++diagnostics->collisionPushedParticles;
                    diagnostics->collisionPushDistanceSum += pushed;
                }
            }
        }
    }

    // Reset only the broken particle to its animated target.
    for (std::size_t i = 0; i < jointCount; ++i) {
        VerletParticle& particle = state.particles[i];
        if (!IsFinite(particle.position)) {
            particle.position = animatedJointWorldPositions[i];
            particle.previousPosition = animatedJointWorldPositions[i];
            if (diagnostics != nullptr) {
                ++diagnostics->nanGuardHits;
            }
        }
    }

    state.simulationTimeSeconds += fixedDeltaTime;
}

} // namespace gte
