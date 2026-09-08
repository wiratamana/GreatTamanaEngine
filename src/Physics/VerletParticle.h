#pragma once
#include "../Math/Vec3.h"

namespace gte {

// One simulated point mass in a Verlet particle chain (see
// VerletIntegration.h). Position-based (Stormer-Verlet): velocity is never
// stored explicitly - it is always `position - previousPosition`, implicitly
// folding in the previous step's timestep. This is what makes Verlet
// integration unconditionally stable for a constrained chain without ever
// needing to store/clamp a separate velocity vector.
//
// task_manager/verlet-integration-7, Phase 3 - `position`/`previousPosition`
// are genuine WORLD-space (rotation + translation only, scale excluded -
// see PHASE0_MASTER_STRATEGY.md's Revision Notes (v2), Finding #1) quantities
// as of this phase: PhysicsSystem::Update() composes the owning ECS entity's
// own resolved Transform with the bone-local pose before feeding a position
// here, and converts a simulated position back into bone-local space before
// writing it into ResolvedAnimationPose::pose. Before this phase, these were
// only ever bone-local "model space" values.
struct VerletParticle {
    Vec3 position = Vec3::Zero();
    Vec3 previousPosition = Vec3::Zero();

    // 1/mass - "Weight: controls how heavy the simulated chain feels" (see
    // ChainConstraints.h's distance-constraint mass-weighted correction). A
    // pinned particle (see below) uses inverseMass == 0.0f by convention,
    // even though `pinned` is the flag actually checked - this mirrors
    // Position-Based-Dynamics' own "an infinite-mass point never receives a
    // correction" convention, so a caller that forgets to check `pinned`
    // and instead just weights by inverseMass still gets the correct (zero)
    // result.
    float inverseMass = 1.0f;

    // True for a chain's root anchor particle (see DynamicChainSolver.h,
    // Phase 2) - its position is written EVERY STEP directly from the
    // character's own animated FK pose, never touched by
    // IntegrateParticle()/constraint solving. Kept on the particle itself
    // (rather than only in the chain definition) so ChainConstraints.h's
    // constraint functions stay pure/self-contained - they never need a
    // side-channel "is this the anchor" flag.
    bool pinned = false;
    // task_manager/verlet-integration-10, PHASE2 - this particle's own
    // physical collision extent, derived from its own PMX Dynamic/
    // DynamicAndBoneMerge RigidBody's real shape/shapeSize (see
    // Physics/DynamicChainDefinition.h's own DynamicJointSettings::
    // collisionRadius doc comment for exactly how this is derived per
    // shape) - seeded once per (re)seed by
    // DynamicChainSolver.cpp's SeedParticlesFromAnimatedPose(), exactly
    // like inverseMass already is. DEFAULT IS 0.0f - a zero-radius
    // mathematical point, i.e. EXACTLY today's pre-PHASE2 behavior - so
    // every existing hand-built VerletParticle in every existing test
    // (SphereColliderTests.cpp/BoxColliderTests.cpp/CapsuleColliderTests.cpp/
    // DynamicChainSolverTests.cpp, none of which ever mention this field)
    // continues to produce byte-identical results. Every
    // Solve*Collision() function inflates its own effective surface
    // distance by this value (see SphereCollider.cpp/BoxCollider.cpp) -
    // CapsuleCollider.cpp needs NO change at all, since
    // SolveCapsuleCollision() already delegates to SolveSphereCollision()
    // with the SAME `particle` reference, inheriting the inflation for
    // free.
    float collisionRadius = 0.0f;
};

// Implicit velocity this step, in world units per second - NOT stored, always
// derived. `deltaTime` must be the SAME fixed timestep IntegrateParticle()
// was last called with, or this value is meaningless (mixing timesteps mid-
// simulation is a caller error - Phase 3's fixed-timestep accumulator is
// what guarantees this never happens in practice).
inline Vec3 ImpliedVelocity(const VerletParticle& particle, float deltaTime) noexcept
{
    return deltaTime > 0.0f ? (particle.position - particle.previousPosition) / deltaTime : Vec3::Zero();
}

} // namespace gte
