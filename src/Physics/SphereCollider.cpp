#include "SphereCollider.h"
#include "../Math/MathTypes.h" // kEpsilon

namespace gte {

void SolveSphereCollision(VerletParticle& particle, const SphereCollider& collider) noexcept
{
    if (particle.pinned || collider.radius <= 0.0f) {
        return;
    }

    const Vec3 delta = particle.position - collider.center;
    const float distance = Length(delta);

    if (distance >= collider.radius) {
        return; // Not penetrating.
    }

    if (distance < kEpsilon) {
        // Degenerate - the particle sits exactly on the collider's center,
        // with no well-defined push direction. Push out along a fixed,
        // arbitrary axis rather than dividing by (near) zero.
        particle.position = collider.center + Vec3::Up() * collider.radius;
        return;
    }

    particle.position = collider.center + (delta / distance) * collider.radius;
}

} // namespace gte
