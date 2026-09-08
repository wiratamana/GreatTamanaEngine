#pragma once
#include "VerletParticle.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

namespace gte {

// A single oriented collision CAPSULE in WORLD space - the Capsule-shaped
// sibling of SphereCollider.h (see that file's own doc comment). The
// capsule's cylindrical axis is the shape's own LOCAL +Y BEFORE `rotation`
// is applied - i.e. its world-space end-cap centers are
// `center +/- rotation.RotateVector(Vec3::Up()) * (height * 0.5f)` - this
// EXACTLY matches src/Editor/RigidBodyWireframe.cpp's own
// BuildCapsuleWireframe() convention ("Capsule axis = local +Y") and
// Assets/PhysicsData.h's RigidBody::shapeSize documented convention for
// RigidBodyShape::Capsule (x = radius, y = FULL height, not half-height).
struct CapsuleCollider {
    Vec3 center = Vec3::Zero();
    Quat rotation = Quat::Identity();
    float radius = 0.0f;
    float height = 0.0f; // full height of the cylindrical portion (see above).
};

// Pushes `particle.position` back out to the CAPSULE's surface if
// penetrated - computed as the closest point on the capsule's own central
// line segment to the particle, then delegating to the EXACT SAME
// SolveSphereCollision() logic (SphereCollider.h) against a sphere of that
// same `radius` centered at that closest point - this is both the
// textbook-correct capsule collision formula (a capsule is the Minkowski
// sum of a segment and a sphere) AND guarantees this function inherits
// SolveSphereCollision()'s own already-tested degenerate-at-center
// handling for free, with zero duplicated logic. Same contract as every
// other Solve*Collision() in this campaign: a no-op for a pinned particle
// or a non-positive radius; `previousPosition` is never modified. A
// non-positive `height` degrades to a pure sphere at `center` (both segment
// endpoints coincide) rather than being treated as invalid - matches
// RigidBodyWireframe.h's own "a non-positive shapeSize.y is a 'pure sphere'
// capsule" documented convention exactly. task_manager/verlet-integration-10,
// PHASE2 - this function inherits `particle.collisionRadius` inflation for
// free through its own existing delegation to SolveSphereCollision()
// immediately below - no code in this file needed to change for that.
void SolveCapsuleCollision(VerletParticle& particle, const CapsuleCollider& collider) noexcept;

} // namespace gte
