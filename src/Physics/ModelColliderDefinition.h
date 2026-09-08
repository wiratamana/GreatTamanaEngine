#pragma once
#include "../Assets/PhysicsData.h" // RigidBodyShape
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

#include <cstdint>

namespace gte {

// One collider volume tracked from the model's own PMX Static rigid-body
// data (Assets/PhysicsData.h) - LOCAL, bind-pose-relative data, precomputed
// ONCE per model by Physics/ModelColliderDetection.h's DetectModelColliders()
// (task_manager/verlet-integration-9, PHASE3) and re-resolved to WORLD space
// EVERY FRAME by the caller (Game/Physics/PhysicsSystem.cpp, PHASE4) from
// `boneIndex`'s own current animated world transform - this exactly mirrors
// how the now-removed single SphereCollider used to be re-derived every
// step (see DynamicChainSolver.h's own now-superseded doc comment history),
// generalized here to the full Sphere/Box/Capsule shape set
// Assets::RigidBodyShape actually supports, and to a real per-shape
// ORIENTATION (meaningless for a Sphere, but required for a Box/Capsule to
// be positioned/aimed correctly).
//
// This struct deliberately lives in Physics/'s "data-driven" tier (like
// DynamicChainDefinition.h), not its "pure primitive" tier (like
// Physics/Collider.h) - see task_manager/verlet-integration-9,
// PHASE0_MASTER_STRATEGY.md's "Architectural tiering" note for why this
// split is intentional and must be preserved.
struct ModelColliderDefinition {
    // Bone this collider tracks every frame (RigidBody::boneIndex).
    std::int32_t boneIndex = -1;

    RigidBodyShape shape = RigidBodyShape::Sphere;
    // Same per-shape convention as RigidBody::shapeSize (Assets/PhysicsData.h)
    // and Physics/Collider.h's own `size` field - unaffected by any
    // transform.
    Vec3 shapeSize = Vec3::Zero();

    // The FIXED, bind-pose-relative offset of this collider from
    // `boneIndex`'s own bind-pose world transform - i.e. the unique
    // rotation+translation that, composed on top of `boneIndex`'s CURRENT
    // (animated) world matrix, reproduces exactly where this rigid body's
    // own authored (Assets::RigidBody::translate/rotateRadians, an absolute
    // MODEL-SPACE bind-pose transform - see PhysicsData.h's own doc
    // comment) shape sits at bind pose, and then correctly "rides along"
    // as the bone animates - the same "bind pose offset" principle vertex
    // skinning itself relies on. Precomputed exactly once by
    // DetectModelColliders() (PHASE3) using only bind-pose data (an EMPTY
    // pose vector - see Animation/BoneWorldMatrixQuery.h) - NEVER
    // recomputed per frame.
    Vec3 localOffsetPosition = Vec3::Zero();
    Quat localOffsetRotation = Quat::Identity();
};

} // namespace gte
