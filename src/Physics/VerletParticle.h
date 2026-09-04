#pragma once
#include "../Math/Vec3.h"

namespace gte {

// One simulated point mass in a Verlet particle chain (see
// VerletIntegration.h). Position-based (Stormer-Verlet): velocity is never
// stored explicitly - it is always `position - previousPosition`, implicitly
// folding in the previous step's timestep. This is what makes Verlet
// integration unconditionally stable for a constrained chain without ever
// needing to store/clamp a separate velocity vector.
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
