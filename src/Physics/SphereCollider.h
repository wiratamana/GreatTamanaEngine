#pragma once
#include "VerletParticle.h"
#include "../Math/Vec3.h"

namespace gte {

// A single collision sphere in WORLD space (PHASE5,
// task_manager/verlet-integration-1/PHASE5_COLLISION_STABILITY_AND_PERFORMANCE_HARDENING.md,
// 3.1) - bare-bone, no broad-phase, no generality, exactly enough to keep a
// simulated dynamic bone chain off a character's own head/shoulders. The
// caller (Physics/DynamicChainSolver.h, 3.2; Game/Physics/PhysicsSystem.cpp)
// is responsible for re-deriving `center` every step from the character's
// current animated collider-bone world position - this struct itself
// carries no bone reference at all, keeping it exactly as pure/
// engine-data-free as every other Physics/ primitive (VerletParticle,
// WindSettings, ...).
struct SphereCollider {
    Vec3 center = Vec3::Zero();
    float radius = 0.0f;
};

// Pushes `particle.position` back out to `collider`'s surface along the
// center->particle direction if it has penetrated - a single, cheap
// Position-Based-Dynamics-style projection, run AFTER distance/goal
// constraints each step (see DynamicChainSolver.cpp's own ordering note) so
// collision has the final say. A no-op for a pinned particle (matches every
// other constraint's own convention) or a non-positive radius. Does NOT
// modify particle.previousPosition - exactly like SolveDistanceConstraint/
// SolveGoalConstraint, so the position correction here contributes to
// (rather than erases) the particle's own implied velocity next step,
// giving the chain a visible "slide off the surface" response instead of
// simply freezing at the boundary. task_manager/verlet-integration-10,
// PHASE2 - the effective test/push-out radius is `collider.radius PLUS
// particle.collisionRadius` (this particle's own PMX-rigid-body-derived
// physical extent, 0.0f by default - see VerletParticle::collisionRadius's
// own doc comment) - a particle that never had a radius seeded reproduces
// the exact pre-PHASE2 behavior, term for term.
void SolveSphereCollision(VerletParticle& particle, const SphereCollider& collider) noexcept;

} // namespace gte
