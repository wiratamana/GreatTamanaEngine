#include "ModelColliderDetection.h"

#include "../Animation/BoneWorldMatrixQuery.h"
#include "../Math/Mat4.h"
#include "../Math/MathTypes.h" // kEpsilon, RadToDeg

#include <algorithm>
#include <cmath>

namespace gte {

namespace {

// Same PMX Euler-rotation convention as src/Editor/RigidBodyWireframe.cpp's
// own (private, Editor-only) RotationFromPmxEuler() - redeclared locally
// rather than shared/exported, matching this campaign's own
// PHASE0_MASTER_STRATEGY.md convention (mirrors the established precedent
// in Physics/BoneChainPhysicsResolver.cpp, which redeclares Animation/
// IkSolver.cpp's own tolerance constants for the identical reason: the
// original file is not always compiled/linked, e.g. RigidBodyWireframe.cpp
// only exists under GTE_ENABLE_PROJECT_PANEL). Verified against
// saba::MMDPhysics.cpp's own `rotMat = ry * rx * rz` - do not change this
// to a different Euler order.
Quat RotationFromPmxEuler(const Vec3& rotateRadians) noexcept
{
    return Quat::FromEulerDegrees(RadToDeg(rotateRadians.x), RadToDeg(rotateRadians.y), RadToDeg(rotateRadians.z));
}

// Mirrors src/Editor/RigidBodyWireframe.h's own documented per-shape
// "nothing meaningful to draw/collide against" degenerate thresholds
// EXACTLY (that file's own doc comment: Sphere - radius <= 0; Box - ALL of
// x/y/z <= 0; Capsule - radius <= 0, non-positive height is still valid -
// a "pure sphere" capsule). Redeclared here (rather than shared) for the
// same reason as RotationFromPmxEuler() above - RigidBodyWireframe.cpp is
// an Editor-only file, not always compiled/linked.
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
    const std::vector<BoneLocalOffset> bindPose; // empty - ComputeBoneWorldMatrix() defaults every entry to identity.

    for (std::size_t rigidBodyIndex = 0; rigidBodyIndex < physics->rigidBodies.size(); ++rigidBodyIndex) {
        const RigidBody& body = physics->rigidBodies[rigidBodyIndex];
        if (body.motionType != RigidBodyMotionType::Static) {
            continue; // Only a kinematic, bone-following body is a valid collision obstacle - see this file's own header comment.
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
            continue; // Algebraically should never happen (pure bind-pose TRS) - defensive only, matches
                      // BoneChainPhysicsResolver.cpp's own identical precedent.
        }

        const Mat4 rigidBodyBindWorld = Mat4::TRS(body.translate, RotationFromPmxEuler(body.rotateRadians), Vec3::One());
        const Mat4 localOffsetMat = bindBoneWorldInverse * rigidBodyBindWorld;

        ModelColliderDefinition def;
        def.boneIndex = body.boneIndex;
        def.shape = body.shape;
        def.shapeSize = body.shapeSize;
        def.localOffsetPosition = localOffsetMat.TransformPoint(Vec3::Zero());
        def.localOffsetRotation = Quat::FromMat4(localOffsetMat);
        result.push_back(def);
    }

    // Full determinism - ascending boneIndex, ties broken by original
    // rigid-body index (stable_sort preserves the ascending-rigidBodyIndex
    // insertion order for equal boneIndex keys, matching this codebase's
    // own established "ascending index" tie-break convention elsewhere -
    // see e.g. DynamicChainDetection.cpp's Step C, pass 3).
    std::stable_sort(result.begin(), result.end(),
        [](const ModelColliderDefinition& a, const ModelColliderDefinition& b) { return a.boneIndex < b.boneIndex; });

    return result;
}

} // namespace gte
