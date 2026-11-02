#pragma once
#include "../Assets/PhysicsData.h" // RigidBodyShape
#include "../Math/Quat.h"
#include "../Math/Vec3.h"

#include <cstdint>

namespace gte {

// One collider volume from a Static PMX rigid body, in bind-pose-relative form.
// Resolved to world space every frame from the animated pose of `boneIndex`.
struct ModelColliderDefinition {
    // Bone this collider follows.
    std::int32_t boneIndex = -1;

    // Index into PhysicsData::rigidBodies (-1 = unknown).
    std::int32_t rigidBodyIndex = -1;

    RigidBodyShape shape = RigidBodyShape::Sphere;
    // Same per-shape convention as RigidBody::shapeSize and Collider::size.
    Vec3 shapeSize = Vec3::Zero();

    // Fixed offset from the bind-pose bone transform to the shape.
    Vec3 localOffsetPosition = Vec3::Zero();
    Quat localOffsetRotation = Quat::Identity();

    // Verbatim PMX collision group and mask. Always mask `group` with 0x0F before shifting.
    std::uint8_t group = 0;
    std::uint16_t collisionMask = 0xFFFF;
};

} // namespace gte
