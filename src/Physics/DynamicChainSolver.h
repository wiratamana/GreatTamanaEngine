#pragma once
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
//
// Per-call steps:
//   1. Lazy init (state.initialized == false, or the chain's own joint
//      count changed since the last call): resize state.particles to
//      definition.jointBoneIndices.size(), set every particle's position AND
//      previousPosition to its corresponding animatedJointWorldPositions[i]
//      (zero implied velocity, so frame 1 never "snaps"/free-falls from the
//      origin), inverseMass = 1.0f / max(jointSettings[i].mass, small
//      epsilon), pinned = false. Set state.initialized = true.
//   2. Integrate: for each particle i, acceleration = gravity *
//      definition.gravityScale + ComputeWindAcceleration(wind,
//      particle.position, state.simulationTimeSeconds) * definition.windScale;
//      call IntegrateParticle(particle, fixedDeltaTime, acceleration,
//      jointSettings[i].damping).
//   3. Constrain-structural: repeat definition.constraintIterations times:
//      SolveDistanceConstraint against a temporary anchor particle pinned at
//      rootWorldPosition (inverseMass 0, pinned = true) for the first joint
//      (restLengths[0]), then between consecutive joint particles for every
//      later one (restLengths[i]). ONLY the structural/distance constraint
//      is repeated here.
//   4. Constrain-goal (this step MUST run exactly ONCE per call, OUTSIDE/
//      AFTER the constraintIterations loop above, never inside it): for
//      every joint i, SolveGoalConstraint(particles[i],
//      animatedJointWorldPositions[i], jointSettings[i].stiffness). Applying
//      this once, after structural relaxation has already converged the rod
//      lengths for this step, is what keeps `stiffness` (a per-joint "how
//      much to keep the animated shape" knob) and `constraintIterations` (a
//      chain-level rod-rigidity/performance knob) fully independent.
//   5. state.simulationTimeSeconds += fixedDeltaTime.
//
// Degrades gracefully (does nothing) if any of the three index-aligned
// arrays (jointBoneIndices/jointSettings/restLengths,
// animatedJointWorldPositions) disagree in size - a malformed/stale
// definition must never read or write out of bounds.
void StepDynamicChain(const DynamicChainDefinition& definition, const Vec3& rootWorldPosition,
    const std::vector<Vec3>& animatedJointWorldPositions, DynamicChainRuntimeState& state, float fixedDeltaTime,
    const Vec3& gravity, const WindSettings& wind);

} // namespace gte
