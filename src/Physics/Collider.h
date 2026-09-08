#pragma once
#include "VerletParticle.h"
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

#include <cstdint>

namespace gte {

// Which of the three PMX-derived collision shapes a Collider (below)
// represents - deliberately its own small enum (rather than reusing
// Assets/PhysicsData.h's RigidBodyShape directly) so this file - and every
// other Physics/ primitive it sits alongside (VerletParticle, SphereCollider,
// WindSettings, ...) - stays completely engine-data-free, with zero
// dependency on Assets/ (see task_manager/verlet-integration-9,
// PHASE0_MASTER_STRATEGY.md's "Architectural tiering" note). The one place
// that DOES need to convert a real Assets::RigidBodyShape into this enum is
// Physics/ModelColliderDetection.h (task_manager/verlet-integration-9,
// PHASE3) - that data-driven tier file is exactly where such a conversion
// belongs.
enum class ColliderShape : std::uint8_t {
    Sphere,
    Box,
    Capsule,
};

// A single WORLD-space collision volume of ANY of the three supported
// shapes, tagged by `shape` - lets a caller (Physics/DynamicChainSolver.h,
// task_manager/verlet-integration-9 PHASE2) hold one homogeneous list
// mixing Sphere/Box/Capsule colliders and resolve each one generically via
// SolveCollision() below, without a switch at every call site. `size`
// reuses the EXACT SAME per-shape field convention as Assets/
// PhysicsData.h's own RigidBody::shapeSize (documented there and mirrored
// by BoxCollider.h/CapsuleCollider.h): Sphere -> size.x = radius; Box ->
// size.xyz = half-extents; Capsule -> size.x = radius, size.y = FULL
// height. `rotation` is meaningless for Sphere (a sphere has no
// orientation) but always present so this struct's own size/shape stays
// uniform regardless of which shape it holds.
struct Collider {
    ColliderShape shape = ColliderShape::Sphere;
    Vec3 center = Vec3::Zero();
    Quat rotation = Quat::Identity();
    Vec3 size = Vec3::Zero();
};

// Dispatches to SolveSphereCollision()/SolveBoxCollision()/
// SolveCapsuleCollision() (SphereCollider.h/BoxCollider.h/CapsuleCollider.h)
// based on `collider.shape` - each underlying function ALREADY checks
// `particle.pinned` and its own shape-specific degenerate-size case
// internally, so this dispatcher adds no extra logic of its own beyond the
// plain field-repacking needed to call the right one.
void SolveCollision(VerletParticle& particle, const Collider& collider) noexcept;

} // namespace gte
