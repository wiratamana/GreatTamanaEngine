#include "CapsuleCollider.h"

#include "SphereCollider.h"
#include "../Math/MathTypes.h" // kEpsilon
#include "../Math/Vec3.h"

#include <algorithm>

namespace gte {

void SolveCapsuleCollision(VerletParticle& particle, const CapsuleCollider& collider) noexcept
{
    if (particle.pinned || collider.radius <= 0.0f) {
        return;
    }

    const float halfHeight = std::max(0.0f, collider.height) * 0.5f;
    const Vec3 axisWorld = collider.rotation.RotateVector(Vec3::Up());
    const Vec3 segStart = collider.center - axisWorld * halfHeight;
    const Vec3 segEnd = collider.center + axisWorld * halfHeight;

    Vec3 closest;
    const Vec3 segment = segEnd - segStart;
    const float segmentLengthSq = LengthSquared(segment);
    if (segmentLengthSq < kEpsilon) {
        // Degenerate (zero-height) segment - both endpoints coincide at
        // `center`; a pure sphere.
        closest = collider.center;
    } else {
        const float t = Clamp(Dot(particle.position - segStart, segment) / segmentLengthSq, 0.0f, 1.0f);
        closest = segStart + segment * t;
    }

    // Delegate to the already-tested sphere logic against a sphere of the
    // same radius centered at the closest segment point - see this file's
    // own header comment for why this is both correct and deliberate.
    SolveSphereCollision(particle, SphereCollider{ closest, collider.radius });
}

} // namespace gte
