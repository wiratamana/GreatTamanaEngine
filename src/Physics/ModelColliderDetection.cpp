#include "ModelColliderDetection.h"

#include "../Animation/BoneWorldMatrixQuery.h"
#include "../Math/Mat4.h"
#include "../Math/MathTypes.h" // kEpsilon, RadToDeg

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

// PMX rigid body Euler order (Ry * Rx * Rz).
Quat RotationFromPmxEuler(const Vec3& rotateRadians) noexcept
{
    return Quat::FromEulerDegrees(RadToDeg(rotateRadians.x), RadToDeg(rotateRadians.y), RadToDeg(rotateRadians.z));
}

// A shape with nothing to collide against: sphere/capsule radius <= 0, or a box with every extent <= 0.
bool IsDegenerateColliderShape(RigidBodyShape shape, const Vec3& shapeSize) noexcept
{
    switch (shape) {
    case RigidBodyShape::Sphere:
        return shapeSize.x <= kEpsilon;
    case RigidBodyShape::Box:
        return shapeSize.x <= kEpsilon && shapeSize.y <= kEpsilon && shapeSize.z <= kEpsilon;
    case RigidBodyShape::Capsule:
        return shapeSize.x <= kEpsilon;
    }
    return true;
}

} // namespace

std::vector<ModelColliderDefinition> DetectModelColliders(const SkeletonData& skeleton, const PhysicsData* physics)
{
    std::vector<ModelColliderDefinition> result;
    if (physics == nullptr) {
        return result;
    }

    const std::size_t boneCount = skeleton.bones.size();
    const std::vector<BoneLocalOffset> bindPose; // empty = identity for every bone.

    for (std::size_t rigidBodyIndex = 0; rigidBodyIndex < physics->rigidBodies.size(); ++rigidBodyIndex) {
        const RigidBody& body = physics->rigidBodies[rigidBodyIndex];
        if (body.motionType != RigidBodyMotionType::Static) {
            continue; // Only kinematic, bone-following bodies are obstacles.
        }
        if (body.boneIndex < 0 || static_cast<std::size_t>(body.boneIndex) >= boneCount) {
            continue; // Unattached - nothing to track every frame.
        }
        if (IsDegenerateColliderShape(body.shape, body.shapeSize)) {
            continue; // Nothing meaningful to collide against.
        }

        const Mat4 bindBoneWorld = ComputeBoneWorldMatrix(skeleton, bindPose, body.boneIndex);
        Mat4 bindBoneWorldInverse;
        if (!bindBoneWorld.TryInverse(bindBoneWorldInverse)) {
            continue; // Singular bind transform: cannot happen for a pure TRS, skip defensively.
        }

        const Mat4 rigidBodyBindWorld = Mat4::TRS(body.translate, RotationFromPmxEuler(body.rotateRadians), Vec3::One());
        const Mat4 localOffsetMat = bindBoneWorldInverse * rigidBodyBindWorld;

        ModelColliderDefinition def;
        def.boneIndex = body.boneIndex;
        def.rigidBodyIndex = static_cast<std::int32_t>(rigidBodyIndex);
        def.shape = body.shape;
        def.shapeSize = body.shapeSize;
        def.group = body.group;
        def.collisionMask = body.collisionGroupMask;
        def.localOffsetPosition = localOffsetMat.TransformPoint(Vec3::Zero());
        def.localOffsetRotation = Quat::FromMat4(localOffsetMat);
        result.push_back(def);
    }

    // Ascending bone index; a stable sort keeps rigid body order for equal keys.
    std::stable_sort(result.begin(), result.end(),
        [](const ModelColliderDefinition& a, const ModelColliderDefinition& b) { return a.boneIndex < b.boneIndex; });

    return result;
}

} // namespace gte
