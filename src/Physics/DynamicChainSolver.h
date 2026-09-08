#pragma once
#include "Collider.h"
#include "DynamicChainDefinition.h"
#include "DynamicChainRuntimeState.h"
#include "WindField.h"
#include "../Math/Vec3.h"

#include <vector>

namespace gte {

// Advances ONE dynamic bone chain by exactly one FIXED timestep.
// `rootWorldPosition` and `animatedJointWorldPositions` (index-aligned with
// definition.jointBoneIndices) are wherever PLAIN forward-kinematics
// animation (no physics at all) puts the chain's bones THIS frame - the
// "goal"/pin targets - computed by the Phase 3 caller via
// Animation/BoneWorldMatrixQuery.h's ComputeBoneWorldMatrix() BEFORE calling
// this function. `gravity` is the GLOBAL world gravity vector; `wind` is the
// GLOBAL wind description (see PHASE4) - both scaled locally by
// definition.gravityScale/windScale.
// `colliders` (task_manager/verlet-integration-9, PHASE2) is an
// already-resolved, WORLD-space, mixed-shape list, shared across every
// chain belonging to the same model/entity this frame - empty by default;
// the caller (Game/Physics/PhysicsSystem.cpp) is responsible for having
// derived every entry's `center`/`rotation` fresh this frame (via
// Animation/BoneWorldMatrixQuery.h's ComputeBoneWorldMatrix()) - this keeps
// this header's own signature free of a SkeletonData/pose dependency,
// exactly like rootWorldPosition/animatedJointWorldPositions above.
//
// Per-call steps:
//   1. Lazy init / root-teleport guard: re-seeds every particle to its
//      corresponding animatedJointWorldPositions[i] (position AND
//      previousPosition, zero implied velocity, inverseMass = 1.0f /
//      max(jointSettings[i].mass, small epsilon), pinned = false) whenever
//      EITHER (a) this is genuinely the first call for this instance (or the
//      chain's own joint count changed since the last call), OR (b) PHASE5's
//      own numerical-safety guard: `rootWorldPosition` has moved farther than
//      definition.maxPlausibleRootDelta since state.lastRootWorldPosition (a
//      teleporting character, an Editor gizmo drag, ...) - integrating across
//      such a spurious, implausibly large displacement would otherwise whip
//      the chain at effectively infinite velocity on the very next step.
//      state.lastRootWorldPosition is set to THIS call's own
//      rootWorldPosition unconditionally, exactly once, regardless of which
//      branch ran - forgetting this would turn the guard into a permanent,
//      one-shot trip.
//   2. Integrate: for each particle i, acceleration = gravity *
//      definition.gravityScale + ComputeWindAcceleration(wind,
//      particle.position, state.simulationTimeSeconds) * definition.windScale;
//      call IntegrateParticle(particle, fixedDeltaTime, acceleration,
//      jointSettings[i].damping).
//   3. Constrain-structural: repeat definition.constraintIterations times:
//      for each joint i in ascending order, resolve its TREE parent via
//      definition.parentJointIndex[i] (task_manager/verlet-integration-6,
//      Phase 1 - an explicit tree-parent position within jointBoneIndices,
//      -1 meaning "my parent is the root anchor directly") and
//      SolveDistanceConstraint() against that parent particle (or a
//      temporary anchor particle pinned at rootWorldPosition when the
//      parent is the root itself), using restLengths[i]. AFTER every tree
//      edge, every one of definition.extraConstraints (non-hierarchy
//      "web brace" joints - see DynamicChainDefinition.h's own
//      ExtraStructuralConstraint doc comment) is ALSO relaxed the same
//      iteration, via a plain SolveDistanceConstraint() between its own two
//      referenced joint particles. ONLY the structural/distance constraints
//      are repeated here.
//   4. Constrain-goal (this step MUST run exactly ONCE per call, OUTSIDE/
//      AFTER the constraintIterations loop above, never inside it): for
//      every joint i, SolveGoalConstraint(particles[i],
//      animatedJointWorldPositions[i], jointSettings[i].stiffness). Applying
//      this once, after structural relaxation has already converged the rod
//      lengths for this step, is what keeps `stiffness` (a per-joint "how
//      much to keep the animated shape" knob) and `constraintIterations` (a
//      chain-level rod-rigidity/performance knob) fully independent.
//   5. Collision (task_manager/verlet-integration-9, PHASE2) - exactly ONCE
//      per call, AFTER the goal constraint (collision must have the final
//      say, matching PBD convention: structural, then soft/goal, then hard
//      collision): if `definition.collisionEnabled`, every joint particle is
//      tested, in order, against EVERY entry of `colliders` (an
//      already-resolved, WORLD-space, mixed-shape list shared across every
//      chain belonging to the same model/entity this frame - see
//      Game/Physics/PhysicsSystem.cpp, PHASE4) via SolveCollision()
//      (Physics/Collider.h). An empty `colliders` list, or
//      `collisionEnabled == false`, is a complete, documented no-op -
//      exactly like the old `hasHeadCollider == false` contract it
//      replaces. task_manager/verlet-integration-10, PHASE1 - BEFORE
//      SolveCollision() is actually called, a Bullet-style symmetric
//      group/mask AND-test (this joint's own DynamicJointSettings::group/
//      collisionMask vs. the collider's own Collider::group/collisionMask,
//      via a shift-safe GroupBit() helper that masks `group` to its
//      documented 4-bit range before ever using it as a shift amount) must
//      also pass, matching PMX's own authored collision-group/layer rule -
//      a joint/collider pair that isn't mutually "visible" to each other is
//      skipped entirely for that pair, same as if the collider weren't in
//      the list at all.
//   6. NaN/Inf guard (PHASE5, 3.3): after every position update above, any
//      particle whose position fails std::isfinite() on any component is
//      reset (that ONE particle only, never the whole chain) to its
//      corresponding animatedJointWorldPositions[i] with zero implied
//      velocity - and, in a debug/development build only, fires an assert so
//      a real underlying bug is caught loudly rather than silently,
//      permanently corrupting that one joint for the rest of the session.
//   7. state.simulationTimeSeconds += fixedDeltaTime.
//
// Degrades gracefully (does nothing) if any of the four index-aligned
// arrays (jointBoneIndices/jointSettings/restLengths/parentJointIndex,
// animatedJointWorldPositions) disagree in size - a malformed/stale
// definition must never read or write out of bounds.
void StepDynamicChain(const DynamicChainDefinition& definition, const Vec3& rootWorldPosition,
    const std::vector<Vec3>& animatedJointWorldPositions, DynamicChainRuntimeState& state, float fixedDeltaTime,
    const Vec3& gravity, const WindSettings& wind, const std::vector<Collider>& colliders = {});

} // namespace gte
