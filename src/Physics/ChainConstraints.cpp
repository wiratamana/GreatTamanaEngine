#include "ChainConstraints.h"
#include "../Math/MathTypes.h" // Clamp, kEpsilon

namespace gte {

void SolveDistanceConstraint(VerletParticle& a, VerletParticle& b, float restLength, float correctionStrength) noexcept
{
    if (a.pinned && b.pinned) {
        return;
    }

    const float invMassSum = a.inverseMass + b.inverseMass;
    if (invMassSum <= kEpsilon) {
        // Both particles are effectively infinite mass - nowhere for a
        // correction to go, regardless of whether either is actually
        // flagged `pinned` (see this function's own doc comment, v2 fix).
        return;
    }

    const Vec3 delta = b.position - a.position;
    const float currentLength = Length(delta);
    if (currentLength <= kEpsilon) {
        // Degenerate - no well-defined direction to push along.
        return;
    }

    const float diff = (currentLength - restLength) / currentLength;
    const Vec3 correction = delta * diff * correctionStrength;

    if (!a.pinned) {
        a.position += correction * (a.inverseMass / invMassSum);
    }
    if (!b.pinned) {
        b.position -= correction * (b.inverseMass / invMassSum);
    }
}

void SolveGoalConstraint(VerletParticle& particle, const Vec3& animatedTargetPosition, float stiffness01) noexcept
{
    if (particle.pinned) {
        return;
    }

    particle.position = Lerp(particle.position, animatedTargetPosition, Clamp(stiffness01, 0.0f, 1.0f));
}

} // namespace gte
