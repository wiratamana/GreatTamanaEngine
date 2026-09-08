#include "SphereCollider.h"
#include "../Math/MathTypes.h" // kEpsilon

#include <algorithm> // std::max

namespace gte {

void SolveSphereCollision(VerletParticle& particle, const SphereCollider& collider) noexcept
{
    // task_manager/verlet-integration-10, PHASE2 - the EFFECTIVE surface
    // distance is the collider's own authored radius PLUS this particle's
    // own physical extent (its own PMX rigid body's shape-derived radius,
    // 0.0f for every particle that never had one seeded - see
    // VerletParticle::collisionRadius's own doc comment). A negative
    // collisionRadius should never occur (DynamicChainDetection.cpp only
    // ever derives a non-negative value - see this phase's own Step 3.5),
    // but is defensively clamped to 0 here anyway, matching this codebase's
    // "never trust an upstream invariant blindly in a leaf math function"
    // convention (see BoxCollider.cpp's own defensive degenerate-shape
    // handling for the established precedent).
    const float effectiveRadius = collider.radius + std::max(0.0f, particle.collisionRadius);

    if (particle.pinned || effectiveRadius <= 0.0f) {
        return;
    }

    const Vec3 delta = particle.position - collider.center;
    const float distance = Length(delta);

    if (distance >= effectiveRadius) {
        return; // Not penetrating.
    }

    if (distance < kEpsilon) {
        // Degenerate - the particle sits exactly on the collider's center,
        // with no well-defined push direction. Push out along a fixed,
        // arbitrary axis rather than dividing by (near) zero.
        particle.position = collider.center + Vec3::Up() * effectiveRadius;
        return;
    }

    particle.position = collider.center + (delta / distance) * effectiveRadius;
}

} // namespace gte
