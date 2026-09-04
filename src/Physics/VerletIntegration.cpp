#include "VerletIntegration.h"
#include "../Math/MathTypes.h" // Clamp

namespace gte {

void IntegrateParticle(VerletParticle& particle, float deltaTime, const Vec3& acceleration, float damping) noexcept
{
    if (particle.pinned) {
        // Pinned particles are driven entirely by the caller (the animated
        // FK target) every step - keep previousPosition in lock-step too, so
        // ImpliedVelocity() never reports spurious motion for an anchor that
        // hasn't actually accelerated, and so a particle that stops being
        // pinned later never "teleports" using a stale previousPosition.
        particle.previousPosition = particle.position;
        return;
    }

    const float clampedDamping = Clamp(damping, 0.0f, 1.0f);
    const Vec3 velocity = (particle.position - particle.previousPosition) * (1.0f - clampedDamping);
    const Vec3 newPosition = particle.position + velocity + acceleration * (deltaTime * deltaTime);

    particle.previousPosition = particle.position;
    particle.position = newPosition;
}

} // namespace gte
