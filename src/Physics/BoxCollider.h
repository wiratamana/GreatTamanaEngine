#pragma once
#include "VerletParticle.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

namespace gte {

// A single oriented collision BOX in WORLD space - the Box-shaped sibling of
// SphereCollider.h (see that file's own doc comment for the shared
// "bare-bone, no broad-phase" philosophy this campaign preserves). The
// caller is responsible for re-deriving `center`/`rotation` every step from
// the character's own current animated collider-bone world transform - see
// task_manager/verlet-integration-9, PHASE4.
//
// `rotation` follows this engine's ordinary Quat convention (Math/Quat.h) -
// `halfExtents` are measured along the box's own LOCAL axes, i.e. the box's
// world-space corners are `center + rotation.RotateVector({+-hx, +-hy,
// +-hz})`. This exactly matches Assets/PhysicsData.h's own RigidBody::
// shapeSize convention for RigidBodyShape::Box (x/y/z = half-extents), and
// src/Editor/RigidBodyWireframe.cpp's BuildBoxWireframe() (same corner
// construction, used there only for drawing).
struct BoxCollider {
    Vec3 center = Vec3::Zero();
    Quat rotation = Quat::Identity();
    Vec3 halfExtents = Vec3::Zero();
};

// Pushes `particle.position` back out to the BOX's nearest face if it has
// penetrated (i.e. its position, expressed in the box's own local space,
// lies STRICTLY inside every one of the three [-halfExtent, +halfExtent]
// ranges) - a Position-Based-Dynamics-style shallow-projection along the
// single axis with the SMALLEST penetration depth (the standard "push out
// through the nearest face" OBB response), run with the exact same
// ordering/contract as SolveSphereCollision() (see SphereCollider.h): a
// no-op for a pinned particle; `previousPosition` is never modified.
//
// Degenerate/no-op cases (never NaN/Inf, matches SphereCollider.h's own
// "non-positive radius is a no-op" convention): if ANY of halfExtents.x/y/z
// is <= 0, the STRICT "< halfExtent" penetration test can never be
// satisfied on that axis for any real coordinate, so the box is
// automatically, correctly treated as a no-op collider on that (or any)
// degenerate axis - no separate early-return branch is needed for this,
// it falls out of the inequality itself. task_manager/verlet-integration-10,
// PHASE2 - every halfExtents.x/y/z used above is really an EFFECTIVE
// half-extent, inflated by max(0, particle.collisionRadius) on every axis
// (see BoxCollider.cpp for the full conservative-approximation rationale)
// - a particle that never had a radius seeded reproduces the exact
// pre-PHASE2 behavior, term for term.
void SolveBoxCollision(VerletParticle& particle, const BoxCollider& collider) noexcept;

} // namespace gte
