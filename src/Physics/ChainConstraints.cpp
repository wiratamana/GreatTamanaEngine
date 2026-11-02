#include "ChainConstraints.h"
#include "../Math/MathTypes.h" // Clamp, kEpsilon, kPi

#include <algorithm>
#include <cmath>

namespace gte {

void SolveDistanceConstraint(VerletParticle& a, VerletParticle& b, float restLength, float correctionStrength) noexcept
{
    if (a.pinned && b.pinned) {
        return;
    }

    const float invMassSum = a.inverseMass + b.inverseMass;
    if (invMassSum <= kEpsilon) {
        return; // Both effectively infinite mass: nowhere for a correction to go.
    }

    const Vec3 delta = b.position - a.position;
    const float currentLength = Length(delta);
    if (currentLength <= kEpsilon) {
        return; // No well-defined direction.
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

bool SolveBendLimit(VerletParticle& child, const Vec3& parentSimPosition, const Vec3& parentAnimPosition,
    const Vec3& childAnimPosition, float limitRadians) noexcept
{
    if (child.pinned || !(limitRadians < kPi)) {
        return false;
    }
    const float limit = std::max(limitRadians, 0.0f);

    const Vec3 animSegment = childAnimPosition - parentAnimPosition;
    const float animLength = Length(animSegment);
    if (!(animLength > kEpsilon)) {
        return false;
    }
    const Vec3 animDir = animSegment / animLength;

    const Vec3 offset = child.position - parentSimPosition;
    const float length = Length(offset);
    if (!(length >= kEpsilon)) {
        child.position = parentSimPosition + animDir * animLength; // child sits on its parent.
        return true;
    }
    const Vec3 simDir = offset / length;

    const float angle = std::acos(Clamp(Dot(animDir, simDir), -1.0f, 1.0f));
    if (angle <= limit) {
        return false;
    }

    const float sinAngle = std::sin(angle);
    Vec3 dir;
    if (sinAngle < 1e-4f) {
        // Opposite (or equal) directions: any perpendicular works.
        const Vec3 absDir(std::fabs(animDir.x), std::fabs(animDir.y), std::fabs(animDir.z));
        Vec3 axis = Vec3::Right();
        if (absDir.y <= absDir.x && absDir.y <= absDir.z) {
            axis = Vec3::Up();
        } else if (absDir.z <= absDir.x && absDir.z <= absDir.y) {
            axis = Vec3::Forward();
        }
        const Vec3 perp = Normalize(Cross(animDir, axis));
        dir = animDir * std::cos(limit) + perp * std::sin(limit);
    } else {
        dir = (animDir * std::sin(angle - limit) + simDir * std::sin(limit)) / sinAngle;
    }
    child.position = parentSimPosition + Normalize(dir) * length;
    return true;
}

} // namespace gte
