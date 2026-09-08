#include "DynamicChainSolver.h"

#include "ChainConstraints.h"
#include "VerletIntegration.h"
#include "../Math/MathTypes.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace gte {

namespace {

// Guards against a caller-supplied (or malformed data-driven) mass <= 0
// producing an infinite/negative inverseMass - mirrors ChainConstraints.cpp's
// own "degrade gracefully instead of NaN/Inf" convention.
constexpr float kMinMass = 1e-4f;

// task_manager/verlet-integration-10, PHASE1 (v2) - a single "which layer am
// I on" bit, SAFELY derived from a raw group value that this engine never
// range-validates anywhere in its own load pipeline (PmxLoader.cpp's own
// ConvertRigidBody(): `out.group = body.m_group;`, a straight byte copy off
// an untrusted .pmx file, with no range check - see
// PHASE0_MASTER_STRATEGY.md's Step 2 point 13). PMX authoring tools always
// emit 0-15, but this engine cannot assume the FILE itself is well-formed.
// Masking to the documented 4-bit range BEFORE shifting is what guarantees
// `1u << group` can never become undefined behavior (shifting by an amount
// >= the promoted-to unsigned int's own bit width, 32, is UB in C++ - a real
// risk for an unmasked `group` up to 255) - matching this codebase's own
// "degrade gracefully, never crash" convention (see BoxCollider.cpp's own
// degenerate half-extent handling, SphereCollider.cpp's own degenerate-
// center fallback, IsDegenerateColliderShape()'s own sibling precedent in
// Physics/ModelColliderDetection.cpp).
constexpr std::uint16_t GroupBit(std::uint8_t group) noexcept
{
    return static_cast<std::uint16_t>(1u << (group & 0x0Fu));
}

// task_manager/verlet-integration-10, PHASE1 - Bullet's own broad-phase
// collision-filter convention (confirmed against
// third_party/saba/src/Saba/Model/MMD/MMDPhysics.cpp's own
// `m_world->addRigidBody(rb, 1 << mmdRB->GetGroup(), mmdRB->GetGroupMask())`
// call, lines 149-154, and its own `GetGroup()`/`GetGroupMask()` accessors,
// lines 632-637) - a SYMMETRIC AND-test: A and B may collide only if A's own
// group bit is set in B's mask, AND B's own group bit is set in A's mask.
bool GroupsMayCollide(std::uint8_t groupA, std::uint16_t maskA, std::uint8_t groupB, std::uint16_t maskB) noexcept
{
    const std::uint16_t bitA = GroupBit(groupA);
    const std::uint16_t bitB = GroupBit(groupB);
    return (bitA & maskB) != 0 && (bitB & maskA) != 0;
}

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
        // task_manager/verlet-integration-10, PHASE2 - this joint's own
        // PMX-rigid-body-derived collision radius (see DynamicJointSettings::
        // collisionRadius's own doc comment).
        particle.collisionRadius = definition.jointSettings[i].collisionRadius;
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
    const Vec3& gravity, const WindSettings& wind, const std::vector<Collider>& colliders)
{
    const std::size_t jointCount = definition.jointBoneIndices.size();
    if (definition.jointSettings.size() != jointCount || definition.restLengths.size() != jointCount
        || definition.parentJointIndex.size() != jointCount || animatedJointWorldPositions.size() != jointCount) {
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

    // 3. Constrain-structural: repeated `constraintIterations` times. Every
    // iteration walks jointBoneIndices in ascending order (root -> tip),
    // resolving each joint's TREE parent via definition.parentJointIndex[i]
    // (task_manager/verlet-integration-6, Phase 1) - the root anchor is a
    // local, stack-allocated particle each iteration, never itself
    // simulated/stored in `state`. Tree edges are solved BEFORE
    // extraConstraints every iteration (parent-before-child, guaranteed by
    // parentJointIndex's own invariant - see DynamicChainDefinition.h),
    // mirroring standard PBD practice of resolving the "primary" structure
    // before "secondary" bracing constraints within the same relaxation
    // pass; extraConstraints are fully order-independent among themselves
    // (each is a simple, symmetric pairwise correction with no ordering
    // dependency on any other extra constraint).
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
                continue; // Malformed/stale - never read/write out of bounds.
            }
            SolveDistanceConstraint(state.particles[static_cast<std::size_t>(extra.jointIndexA)],
                state.particles[static_cast<std::size_t>(extra.jointIndexB)], extra.restLength);
        }
    }

    // 4. Constrain-goal - exactly ONCE per call, AFTER structural relaxation
    // has already converged the rod lengths this step (never inside the
    // loop above) - this is what keeps `stiffness` and `constraintIterations`
    // fully decoupled (see this file's own header comment, step 4).
    for (std::size_t i = 0; i < jointCount; ++i) {
        SolveGoalConstraint(state.particles[i], animatedJointWorldPositions[i], definition.jointSettings[i].stiffness);
    }

    // 5. Collision (task_manager/verlet-integration-9, PHASE2) - exactly
    // ONCE per call, AFTER the goal constraint, so collision has the final
    // say (structural, then soft/goal, then hard collision). Every joint
    // particle is tested against EVERY collider in the shared,
    // already-resolved list - order among colliders never matters (each
    // SolveCollision() call is an independent, idempotent-if-already-outside
    // projection), so a particle penetrating more than one collider
    // simultaneously still ends up outside ALL of them by the end of this
    // loop (each subsequent call only ever pushes it further from whichever
    // surface it is CURRENTLY penetrating). task_manager/verlet-integration-10,
    // PHASE1 - before actually solving, a Bullet-style symmetric group/mask
    // AND-test (GroupsMayCollide(), above) filters out any joint/collider
    // pair that isn't mutually "visible" to each other via PMX's own
    // collision-group/layer rule ("use pmx defined rigid body layer rule
    // with collision map").
    if (definition.collisionEnabled) {
        for (std::size_t i = 0; i < jointCount; ++i) {
            const std::uint8_t jointGroup = definition.jointSettings[i].group;
            const std::uint16_t jointMask = definition.jointSettings[i].collisionMask;
            for (const Collider& collider : colliders) {
                if (!GroupsMayCollide(jointGroup, jointMask, collider.group, collider.collisionMask)) {
                    continue; // task_manager/verlet-integration-10, PHASE1 - PMX collision-group/layer rule.
                }
                SolveCollision(state.particles[i], collider);
            }
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
