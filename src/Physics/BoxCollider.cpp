#include "BoxCollider.h"

#include "../Math/MathTypes.h" // kEpsilon

#include <algorithm> // std::max
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

    // task_manager/verlet-integration-10, PHASE2 - the EFFECTIVE half-extent
    // on every axis is the box's own authored half-extent PLUS this
    // particle's own physical radius (see SphereCollider.cpp's own
    // identical rationale) - an approximation (a true "rounded box"
    // Minkowski-sum surface is not flat-faced near an edge/corner, unlike
    // this per-axis-inflated approximation), but a deliberately SAFE one:
    // it never UNDER-estimates the true rounded-box surface anywhere,
    // meaning a particle is guaranteed to be pushed AT LEAST as far away
    // as its own true physical radius requires, never less - the same
    // "conservative, never wrong in the unsafe direction" trade-off this
    // codebase already accepts for Box collision's own "push out along the
    // single smallest-escape axis" simplification versus a true SAT-based
    // OBB response.
    const float radius = std::max(0.0f, particle.collisionRadius);
    const Vec3 effectiveHalfExtents = collider.halfExtents + Vec3(radius, radius, radius);

    // Penetration test: STRICTLY inside every axis range. See BoxCollider.h's
    // own doc comment for why a degenerate (<=0) half-extent on any axis
    // makes this always false - no separate early-return needed.
    const bool insideX = std::fabs(local.x) < effectiveHalfExtents.x;
    const bool insideY = std::fabs(local.y) < effectiveHalfExtents.y;
    const bool insideZ = std::fabs(local.z) < effectiveHalfExtents.z;
    if (!(insideX && insideY && insideZ)) {
        return; // Not penetrating (or a degenerate box).
    }

    // Escape distance (>= 0, since we just proved |local[axis]| < effectiveHalfExtents[axis])
    // per axis - the axis with the SMALLEST escape distance is the box's
    // nearest face, the standard shallow-OBB-push-out choice.
    const float escapeX = effectiveHalfExtents.x - std::fabs(local.x);
    const float escapeY = effectiveHalfExtents.y - std::fabs(local.y);
    const float escapeZ = effectiveHalfExtents.z - std::fabs(local.z);

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
    pushedLocal[axis] = sign * effectiveHalfExtents[axis];

    particle.position = collider.center + collider.rotation.RotateVector(pushedLocal);
}

} // namespace gte
