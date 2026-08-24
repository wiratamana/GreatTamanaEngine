#pragma once

#include "../Math/Vec3.h"

#include <cstdint>
#include <string>
#include <vector>

namespace gte {

// Plain, engine-native ragdoll/cloth-jiggle physics setup - the "Physics:
// Rigid bodies and Joints" half of this engine's MMD import support (see
// SkeletonData.h's own file comment for the other three: bone weights/
// skinning, bones, morphs). Produced by PmxLoader.h's LoadPmxModel() today
// (from saba::PMXFile's m_rigidbodies/m_joints - see
// third_party/saba/src/Saba/Model/MMD/PMXFile.h), same "engine-native,
// saba/glm-free copy of the third-party format's data" shape as
// SkeletonData/MorphData/MeshData.
//
// This is DATA only - no actual physics simulation happens anywhere in this
// engine yet (no Bullet or any other physics backend is vendored - see
// TODO.md's "Real MMD skinning/animation" entry). A future physics system
// would consume RigidBody/Joint read-only to construct real rigid bodies/
// constraints; nothing here simulates or steps anything.
enum class RigidBodyShape : std::uint8_t {
    Sphere,
    Box,
    Capsule,
};

// How a rigid body's transform relates to its associated bone (see
// RigidBody::boneIndex below):
//   Static             - the rigid body strictly follows the bone (a
//                         collider for e.g. hit-testing, not simulated).
//   Dynamic            - the rigid body is fully physics-driven; the bone
//                         should instead follow the SIMULATED rigid body
//                         (e.g. jiggle hair/skirt bones).
//   DynamicAndBoneMerge- physics-driven like Dynamic, but additionally
//                         constrained back to the bone's own position
//                         (a hybrid "physics with a leash" mode).
enum class RigidBodyMotionType : std::uint8_t {
    Static,
    Dynamic,
    DynamicAndBoneMerge,
};

struct RigidBody {
    std::string name;
    std::string englishName;

    // Index into SkeletonData::bones this rigid body is attached to/follows
    // (see RigidBodyMotionType above) - -1 if unattached.
    std::int32_t boneIndex = -1;

    // Collision filtering: `group` is this body's own single-bit group
    // membership (0-15), `collisionGroupMask` is the bitmask of groups it IS
    // ALLOWED to collide with - matches Bullet's own btCollisionObject
    // group/mask convention exactly (this data is shaped for eventual direct
    // hand-off to Bullet or an equivalent backend).
    std::uint8_t group = 0;
    std::uint16_t collisionGroupMask = 0;

    RigidBodyShape shape = RigidBodyShape::Sphere;
    // Meaning depends on `shape`: Sphere uses only x (radius); Box uses
    // x/y/z as half-extents; Capsule uses x (radius) and y (height) - same
    // per-shape field reuse as saba::PMXRigidbody::m_shapeSize.
    Vec3 shapeSize = Vec3::Zero();

    // Rest-pose transform, in model-local space (same space as
    // MeshData::positions/Bone::position).
    Vec3 translate = Vec3::Zero();
    Vec3 rotateRadians = Vec3::Zero(); // Euler angles, radians, matching PMX's own convention.

    float mass = 1.0f;
    float linearDamping = 0.0f;
    float angularDamping = 0.0f;
    float restitution = 0.0f;
    float friction = 0.0f;

    RigidBodyMotionType motionType = RigidBodyMotionType::Static;
};

enum class JointType : std::uint8_t {
    SpringDof6, // 6-degree-of-freedom constraint with spring forces on some axes.
    Dof6, // Plain 6-degree-of-freedom constraint (translation + rotation limits per axis).
    P2P, // Point-to-point (ball socket, no rotation limits).
    ConeTwist,
    Slider,
    Hinge,
};

struct Joint {
    std::string name;
    std::string englishName;

    JointType type = JointType::SpringDof6;

    // Indices into PhysicsData::rigidBodies this joint connects.
    std::int32_t rigidBodyAIndex = -1;
    std::int32_t rigidBodyBIndex = -1;

    // Joint frame's rest transform, in model-local space.
    Vec3 translate = Vec3::Zero();
    Vec3 rotateRadians = Vec3::Zero();

    // Only meaningful for constraint types that honor per-axis limits
    // (Dof6/SpringDof6 primarily) - a lower limit greater than its upper
    // limit on a given axis conventionally means "locked" on that axis,
    // matching PMX/Bullet's own convention.
    Vec3 translateLowerLimit = Vec3::Zero();
    Vec3 translateUpperLimit = Vec3::Zero();
    Vec3 rotateLowerLimit = Vec3::Zero();
    Vec3 rotateUpperLimit = Vec3::Zero();

    // Only meaningful for JointType::SpringDof6 - per-axis spring stiffness
    // factor; zero means "no spring" on that axis.
    Vec3 springTranslateFactor = Vec3::Zero();
    Vec3 springRotateFactor = Vec3::Zero();
};

struct PhysicsData {
    std::vector<RigidBody> rigidBodies;
    std::vector<Joint> joints;
};

} // namespace gte
