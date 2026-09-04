#pragma once
#include "VerletParticle.h"

namespace gte {

// Advances one particle by exactly one FIXED timestep of Position-Verlet
// integration with velocity damping - the "Damping: controls how fast the
// simulated chain stops swinging" parameter. A no-op for a pinned particle
// (its position is authoritative from the animated pose, set by the caller
// every step - see DynamicChainSolver.h, Phase 2).
//
// Formula (Stormer-Verlet with a damping term applied to the IMPLICIT
// velocity, the standard "cheap air-drag" extension - see e.g. Jakobsen's
// "Advanced Character Physics", the reference algorithm every from-scratch
// constrained-particle-chain Verlet implementation is built on):
//
//   velocity    = (position - previousPosition) * (1 - damping)
//   newPosition = position + velocity + acceleration * deltaTime^2
//   previousPosition = position
//   position    = newPosition
//
// `damping` is clamped to [0, 1] internally - 0 means "no energy loss, keeps
// swinging forever" (a real damping of exactly 0 is legal and intentional
// for a test asserting pure energy conservation), 1 means "fully damped,
// stops dead every step, effectively rigid/keyframed-looking".
// `acceleration` is the SUM of every force-as-acceleration this particle
// feels this step (gravity*gravityScale + wind*windScale - see WindField.h -
// summed by the Phase 2 caller, never computed inside this function, which
// stays a pure integrator with no knowledge of what a "chain" or "gravity"
// even is).
void IntegrateParticle(VerletParticle& particle, float deltaTime, const Vec3& acceleration, float damping) noexcept;

} // namespace gte
