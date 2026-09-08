#include "BoxCollider.h"

#include "../Math/MathTypes.h" // kEpsilon

#include <cmath>

namespace gte {

void SolveBoxCollision(VerletParticle& particle, const BoxCollider& collider) noexcept
{
    if (particle.pinned) {
        return;
    }

    // Transform the particle into the box's own local space (inverse
    // rotate, translate) - a unit quaternion's Inverse() is its Conjugate()
    // scaled by 1/|q|^2 (see Math/Quat.h), safe even for a slightly
    // non-normalized input.
    const Vec3 worldOffset = particle.position - collider.center;
    const Vec3 local = collider.rotation.Inverse().RotateVector(worldOffset);

    // Penetration test: STRICTLY inside every axis range. See BoxCollider.h's
    // own doc comment for why a degenerate (<=0) half-extent on any axis
    // makes this always false - no separate early-return needed.
    const bool insideX = std::fabs(local.x) < collider.halfExtents.x;
    const bool insideY = std::fabs(local.y) < collider.halfExtents.y;
    const bool insideZ = std::fabs(local.z) < collider.halfExtents.z;
    if (!(insideX && insideY && insideZ)) {
        return; // Not penetrating (or a degenerate box).
    }

    // Escape distance (>= 0, since we just proved |local[axis]| < halfExtents[axis])
    // per axis - the axis with the SMALLEST escape distance is the box's
    // nearest face, the standard shallow-OBB-push-out choice.
    const float escapeX = collider.halfExtents.x - std::fabs(local.x);
    const float escapeY = collider.halfExtents.y - std::fabs(local.y);
    const float escapeZ = collider.halfExtents.z - std::fabs(local.z);

    int axis = 0;
    float smallestEscape = escapeX;
    if (escapeY < smallestEscape) { axis = 1; smallestEscape = escapeY; }
    if (escapeZ < smallestEscape) { axis = 2; smallestEscape = escapeZ; }

    // Push along the chosen axis's own sign, out to that face. Treat
    // exactly-zero as positive (a fixed, deterministic tie-break for the
    // particle sitting exactly on the box's own central plane along this
    // axis - mirrors SphereCollider.cpp's own "push along a fixed arbitrary
    // axis" convention for its degenerate at-center case).
    Vec3 pushedLocal = local;
    const float sign = pushedLocal[axis] >= 0.0f ? 1.0f : -1.0f;
    pushedLocal[axis] = sign * collider.halfExtents[axis];

    particle.position = collider.center + collider.rotation.RotateVector(pushedLocal);
}

} // namespace gte
