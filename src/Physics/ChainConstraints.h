#pragma once
#include "VerletParticle.h"

namespace gte {

// Keeps two particles `restLength` apart (mass-weighted position-based projection).
// `correctionStrength` in 0..1 is the per-iteration relaxation factor.
// No-op when both are pinned, their inverse masses sum to ~0, or they coincide.
void SolveDistanceConstraint(VerletParticle& a, VerletParticle& b, float restLength, float correctionStrength = 1.0f) noexcept;

// Blends the particle toward `animatedTargetPosition` by `stiffness01` (0 = free, 1 = snap). No-op when pinned.
void SolveGoalConstraint(VerletParticle& particle, const Vec3& animatedTargetPosition, float stiffness01) noexcept;

// Keeps `child` inside a cone of half angle `limitRadians` around the animated segment direction
// (parentAnim -> childAnim), keeping the length |child - parentSim|. Moves the child only.
// Limit 0 puts the child exactly on parentSim + animatedDir * length.
// No-op for a pinned child, limit >= pi, or a zero-length animated segment. Returns true when the child moved.
bool SolveBendLimit(VerletParticle& child, const Vec3& parentSimPosition, const Vec3& parentAnimPosition,
    const Vec3& childAnimPosition, float limitRadians) noexcept;

} // namespace gte
