#include "BoneChainPhysicsResolver.h"

#include "../Animation/BoneWorldMatrixQuery.h"
#include "../Math/Mat4.h"
#include "../Math/MathTypes.h"
#include "../Math/Quat.h"

#include <cmath>
#include <cstddef>

namespace gte {

namespace {

// Mirrors IkSolver.cpp's own literal tolerances (see that file's own
// anonymous-namespace constants) - kept as a separate, independently
// redeclared copy here rather than a shared header, per PHASE2's own Step
// 3.5 instruction ("reuse the same literal tolerance constants ... by
// either sharing them via a small shared constants header or simply
// redeclaring the same numeric literals locally with a comment
// cross-referencing IkSolver.cpp").
constexpr float kMinDirectionLengthSq = 1e-10f;
constexpr float kMinAngleRadians = 1e-5f;

} // namespace

void ApplyDynamicChainPhysicsToPose(const SkeletonData& skeleton, const DynamicChainDefinition& definition,
    const std::vector<Vec3>& simulatedJointWorldPositions, std::vector<BoneLocalOffset>& pose)
{
    const std::size_t jointCount = definition.jointBoneIndices.size();
    if (simulatedJointWorldPositions.size() != jointCount) {
        return; // Malformed/stale input - never read/write out of bounds.
    }

    // Match SolveIkChains()'s own convention: a `pose` shorter than the
    // skeleton is grown (missing tail entries treated as BoneLocalOffset{},
    // i.e. bind pose) rather than risking an out-of-bounds write below.
    if (pose.size() < skeleton.bones.size()) {
        pose.resize(skeleton.bones.size());
    }

    for (std::size_t i = 0; i < jointCount; ++i) {
        const std::int32_t boneIndex = definition.jointBoneIndices[i];
        if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= skeleton.bones.size()) {
            continue; // Malformed chain data - skip rather than crash.
        }

        // 1. The bone whose rotation this iteration actually rewrites - see
        // this file's own header comment ("IMPORTANT DESIGN NOTE") for why
        // this must be the PARENT, never `boneIndex` itself: a bone's own
        // rotation can never move its own world position, only its
        // descendants'.
        const std::int32_t parentBoneIndex = (i == 0) ? definition.rootBoneIndex : definition.jointBoneIndices[i - 1];
        if (parentBoneIndex < 0 || static_cast<std::size_t>(parentBoneIndex) >= skeleton.bones.size()) {
            continue; // No real bone to rotate for this segment (e.g. a world-anchored chain with no root bone).
        }

        // 2. Parent's current world matrix/position.
        const Mat4 parentWorld = ComputeBoneWorldMatrix(skeleton, pose, parentBoneIndex);
        const Vec3 parentWorldPos = parentWorld.TransformPoint(Vec3::Zero());

        // 3. The child bone's CURRENT world position, before this
        // iteration's own correction (already reflecting every EARLIER
        // iteration's rewritten ancestor rotation this same call).
        const Mat4 currentChildWorld = ComputeBoneWorldMatrix(skeleton, pose, boneIndex);
        const Vec3 currentChildPos = currentChildWorld.TransformPoint(Vec3::Zero());
        const Vec3 rawCurrentDelta = currentChildPos - parentWorldPos;

        // 4. Wherever the simulated particle actually landed, relative to
        // the same (already-resolved) parent.
        const Vec3 rawTargetDelta = simulatedJointWorldPositions[i] - parentWorldPos;

        // 5. Degenerate-direction guard (near-zero length) - leave this
        // segment's parent rotation untouched rather than normalizing
        // garbage.
        if (LengthSquared(rawCurrentDelta) < kMinDirectionLengthSq || LengthSquared(rawTargetDelta) < kMinDirectionLengthSq) {
            continue;
        }

        const Vec3 currentDir = Normalize(rawCurrentDelta);
        const Vec3 targetDir = Normalize(rawTargetDelta);

        const float dot = Clamp(Dot(currentDir, targetDir), -1.0f, 1.0f);
        const float angle = std::acos(dot);
        if (angle < kMinAngleRadians) {
            continue; // Already (approximately) aimed at the simulated position - nothing to correct.
        }

        Vec3 axis = Cross(currentDir, targetDir);
        if (LengthSquared(axis) < kMinDirectionLengthSq) {
            continue; // Parallel/antiparallel - no well-defined rotation axis.
        }
        axis = Normalize(axis);

        // 6-7. World-space corrective rotation, applied on top of the
        // PARENT bone's existing world rotation (a single corrective step
        // per bone per frame - see this file's own header comment for why
        // this deliberately does NOT mirror IkSolver's local-space CCD
        // accumulation style).
        const Quat delta = Quat::FromAxisAngle(axis, angle);
        const Quat newParentWorldRotation = delta * Quat::FromMat4(parentWorld);

        // 8. Convert back to the parent bone's LOCAL offset rotation by
        // removing ITS OWN parent's (the "grandparent" relative to
        // boneIndex) world rotation. Translation is left unchanged - PMX
        // bones never need a translation channel for a purely-rotated FK
        // bend (see SkeletonPose.h's own bind-pose convention).
        const std::int32_t grandparentBoneIndex = skeleton.bones[static_cast<std::size_t>(parentBoneIndex)].parentBoneIndex;
        const Mat4 grandparentWorld = ComputeBoneWorldMatrix(skeleton, pose, grandparentBoneIndex);
        const Quat grandparentWorldRotationInverse = Quat::FromMat4(grandparentWorld).Inverse();
        pose[static_cast<std::size_t>(parentBoneIndex)].rotation =
            Normalize(grandparentWorldRotationInverse * newParentWorldRotation);
    }
}

} // namespace gte
