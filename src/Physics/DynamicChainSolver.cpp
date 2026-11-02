#include "DynamicChainSolver.h"

#include "ChainConstraints.h"
#include "CollisionFilter.h"
#include "DynamicChainDefinition.h"
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
// A joint left penetrating by less than this still counts as outside after the cone (model units).
constexpr float kRetainTolerance = 1e-4f;
// Collision/cone alternations per step.
constexpr int kMaxCollisionConeRounds = 4;
// Runaway bound from the animated pose: factor * longest root path, never below the minimum (model units).
constexpr float kDivergenceFactor = 10.0f;
constexpr float kMinDivergenceBound = 100.0f;

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

// Pushes one joint out of every collider that applies to it; returns how far it moved.
float CollideJoint(const DynamicChainDefinition& definition, std::size_t jointIndex, const StepOptions& options,
    const std::vector<Collider>& colliders, VerletParticle& particle)
{
    const DynamicJointSettings& js = definition.jointSettings[jointIndex];
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
    return Length(particle.position - before);
}

// Longest rest-length path from the anchor to any joint.
float MaxRootPathLength(const DynamicChainDefinition& definition, std::vector<float>& scratch)
{
    const std::size_t jointCount = definition.restLengths.size();
    scratch.resize(jointCount);
    float longest = 0.0f;
    for (std::size_t i = 0; i < jointCount; ++i) {
        const std::int32_t parent = definition.parentJointIndex[i];
        const float base = (parent >= 0 && static_cast<std::size_t>(parent) < i) ? scratch[static_cast<std::size_t>(parent)] : 0.0f;
        scratch[i] = base + definition.restLengths[i];
        longest = std::max(longest, scratch[i]);
    }
    return longest;
}

// Rebuilds the chain parent-first: each joint keeps its direction, clamped into its cone, at exactly its rest length.
// Lengths stay exact and no joint is pulled back by its child, so the pass adds no energy.
void ApplyBendCone(const DynamicChainDefinition& definition, const Vec3& rootWorldPosition,
    const std::vector<Vec3>& animated, DynamicChainRuntimeState& state, std::uint32_t* clampedJoints)
{
    const std::size_t jointCount = state.particles.size();
    state.scratchPositions.resize(jointCount);
    for (std::size_t i = 0; i < jointCount; ++i) {
        state.scratchPositions[i] = state.particles[i].position;
    }

    for (std::size_t i = 0; i < jointCount; ++i) {
        const std::int32_t parent = definition.parentJointIndex[i];
        const bool isRootChild = parent < 0;
        const std::size_t parentIndex = isRootChild ? 0 : static_cast<std::size_t>(parent);
        const Vec3 parentBefore = isRootChild ? rootWorldPosition : state.scratchPositions[parentIndex];
        const Vec3 parentNow = isRootChild ? rootWorldPosition : state.particles[parentIndex].position;
        const Vec3 parentAnim = isRootChild ? rootWorldPosition : animated[parentIndex];

        const Vec3 offset = state.scratchPositions[i] - parentBefore;
        const float length = Length(offset);
        Vec3 direction = Vec3::Down();
        if (std::isfinite(length) && length > kEpsilon) {
            direction = offset / length;
        } else {
            const Vec3 animatedSegment = animated[i] - parentAnim;
            const float animatedLength = Length(animatedSegment);
            if (animatedLength > kEpsilon) {
                direction = animatedSegment / animatedLength;
            }
        }

        VerletParticle& particle = state.particles[i];
        particle.position = parentNow + direction * definition.restLengths[i];
        if (SolveBendLimit(particle, parentNow, parentAnim, animated[i], state.scratchLimits[i]) && clampedJoints != nullptr) {
            ++*clampedJoints;
        }
    }
}

} // namespace

void ComputeCumulativeBendLimits(const DynamicChainDefinition& definition, std::vector<float>& out)
{
    // Parents always come first.
    const std::size_t jointCount = definition.jointSettings.size();
    out.assign(jointCount, kPi);
    for (std::size_t i = 0; i < jointCount; ++i) {
        const std::int32_t parent = i < definition.parentJointIndex.size() ? definition.parentJointIndex[i] : -1;
        const float parentCumulative
            = (parent >= 0 && static_cast<std::size_t>(parent) < i) ? out[static_cast<std::size_t>(parent)] : 0.0f;
        out[i] = std::min(kPi, definition.jointSettings[i].bendLimitRadians + parentCumulative);
    }
}

void ComputeEffectiveBendLimits(const DynamicChainDefinition& definition, const StepOptions& options, std::vector<float>& out)
{
    if (!options.bendLimitsEnabled) {
        out.assign(definition.jointSettings.size(), kPi);
        return;
    }
    ComputeCumulativeBendLimits(definition, out);
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

    // The cone pass only runs when at least one joint has a finite limit.
    bool coneActive = false;
    if (options.bendLimitsEnabled) {
        ComputeEffectiveBendLimits(definition, options, state.scratchLimits);
        coneActive = std::any_of(state.scratchLimits.begin(), state.scratchLimits.end(), [](float limit) { return limit < kPi; });
    }

    // Rods and braces, repeated.
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
    }

    // Soft pull toward the animated pose, once per step.
    for (std::size_t i = 0; i < jointCount; ++i) {
        SolveGoalConstraint(state.particles[i], animatedJointWorldPositions[i],
            ScaleStepFraction(definition.jointSettings[i].stiffness, fixedDeltaTime, options.stiffnessMultiplier));
    }

    // Cone first, so collision sees a legal pose.
    if (coneActive) {
        ApplyBendCone(definition, rootWorldPosition, animatedJointWorldPositions, state,
            diagnostics != nullptr ? &diagnostics->bendClampedJoints : nullptr);
    }

    // Collision; welded joints cannot move, so they skip it.
    if (definition.collisionEnabled && !colliders.empty()) {
        state.scratchPushDistance.assign(jointCount, 0.0f);
        const auto collideAll = [&](bool record) {
            bool moved = false;
            for (std::size_t i = 0; i < jointCount; ++i) {
                if (coneActive && IsWeldedLimit(state.scratchLimits[i])) {
                    continue;
                }
                const float pushed = CollideJoint(definition, i, options, colliders, state.particles[i]);
                if (pushed > kPushEpsilon) {
                    moved = true;
                    if (record) {
                        state.scratchPushDistance[i] = pushed;
                    }
                }
            }
            return moved;
        };

        // Alternate collision and cone until the pose is outside the colliders and inside the cones.
        // The cone always runs last, so it keeps the final say when no pose satisfies both.
        bool moved = collideAll(true);
        int rounds = 0;
        while (coneActive && moved) {
            ApplyBendCone(definition, rootWorldPosition, animatedJointWorldPositions, state, nullptr);
            if (++rounds >= kMaxCollisionConeRounds) {
                break;
            }
            moved = collideAll(false);
        }

        // A push counts only if the joint is still outside after the cone; the raw count has every push.
        for (std::size_t i = 0; i < jointCount; ++i) {
            const float pushed = state.scratchPushDistance[i];
            if (!(pushed > kPushEpsilon) || diagnostics == nullptr) {
                continue;
            }
            ++diagnostics->collisionRawPushedParticles;
            VerletParticle probe = state.particles[i];
            if (!coneActive || CollideJoint(definition, i, options, colliders, probe) <= kRetainTolerance) {
                ++diagnostics->collisionPushedParticles;
                diagnostics->collisionPushDistanceSum += pushed;
            }
        }
    }

    // A non-finite or runaway particle reseeds the whole chain.
    const float maxPath = MaxRootPathLength(definition, state.scratchPathLength);
    const float divergenceBound = std::max(maxPath * kDivergenceFactor, kMinDivergenceBound);
    std::uint32_t diverged = 0;
    for (std::size_t i = 0; i < jointCount; ++i) {
        const Vec3& position = state.particles[i].position;
        if (!IsFinite(position) || !(Length(position - animatedJointWorldPositions[i]) <= divergenceBound)) {
            ++diverged;
        }
    }
    if (diverged > 0) {
        SeedParticlesFromAnimatedPose(definition, animatedJointWorldPositions, state, jointCount);
        ++state.reseedCount;
        if (diagnostics != nullptr) {
            ++diagnostics->reseeds;
            diagnostics->nanGuardHits += diverged;
        }
    }

    state.simulationTimeSeconds += fixedDeltaTime;
}

} // namespace gte
