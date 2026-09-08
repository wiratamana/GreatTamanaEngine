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
// cross-referencing IkSolver.cpp"). Used only by the parentJoint >= 0
// (rotate-parent) branch below.
constexpr float kMinDirectionLengthSq = 1e-10f;
constexpr float kMinAngleRadians = 1e-5f;

} // namespace

void ApplyDynamicChainPhysicsToPose(const SkeletonData& skeleton, const DynamicChainDefinition& definition,
    const std::vector<Vec3>& simulatedJointWorldPositions, std::vector<BoneLocalOffset>& pose)
{
    const std::size_t jointCount = definition.jointBoneIndices.size();
    if (simulatedJointWorldPositions.size() != jointCount || definition.parentJointIndex.size() != jointCount) {
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

        const std::int32_t parentJoint = definition.parentJointIndex[i];

        if (parentJoint < 0) {
            // This joint's tree-parent is the chain's own ANCHOR bone
            // (definition.rootBoneIndex) directly - the Bone Viewer's "(root
            // child)" case. The anchor is frequently a REAL, SHARED, load-
            // bearing skeleton bone (e.g. MMD's own 下半身), which may be the
            // tree-parent of many dozens of unrelated accessory joints AND the
            // real FK ancestor of non-participating body bones (legs). Per
            // DynamicChainDefinition.h's own documented contract, the anchor's
            // pose entry must NEVER be written by physics - so instead of
            // rotating it (which would move every other child sharing it, and
            // every real body bone descending from it), this branch corrects
            // ONLY this joint's OWN local TRANSLATION, which moves nothing
            // except this one bone's own origin within the anchor's (always
            // untouched) frame. See this file's own header comment for the
            // full derivation. task_manager/verlet-integration-8, Phase 1.
            const std::int32_t rootBoneIndex = definition.rootBoneIndex;
            if (rootBoneIndex < 0 || static_cast<std::size_t>(rootBoneIndex) >= skeleton.bones.size()) {
                continue; // No real anchor bone (e.g. a world-anchored chain) - nothing to translate relative to.
            }

            const Mat4 anchorWorld = ComputeBoneWorldMatrix(skeleton, pose, rootBoneIndex);
            Mat4 anchorWorldInverse;
            if (!anchorWorld.TryInverse(anchorWorldInverse)) {
                continue; // Algebraically should never happen (anchorWorld is a unit-scale TRS) - defensive only.
            }

            const Vec3 desiredLocalPoint = anchorWorldInverse.TransformPoint(simulatedJointWorldPositions[i]);
            const Vec3 localBindOffset = skeleton.bones[static_cast<std::size_t>(boneIndex)].position
                - skeleton.bones[static_cast<std::size_t>(rootBoneIndex)].position;
            pose[static_cast<std::size_t>(boneIndex)].translation = desiredLocalPoint - localBindOffset;
            // pose[boneIndex].rotation is intentionally left untouched here - if
            // this SAME bone is itself some LATER joint's own tree-parent, the
            // `parentJoint >= 0` branch below (a later iteration, per this
            // array's own root-to-tip ordering invariant) will still correctly
            // rewrite its rotation to aim that descendant - translation and
            // rotation are independent BoneLocalOffset channels
            // (Animation/BonePoseMath.h's ComputeBoneLocalMatrix()), so writing
            // both across two different iterations composes correctly.
            continue;
        }

        // UNCHANGED below this point: parentJoint >= 0, i.e. this joint's
        // tree-parent is ANOTHER chain joint (never the anchor) - rotate that
        // joint's own bone to aim this joint's descendant at its target, exactly
        // as before. See this file's own header comment for why this must be
        // the PARENT's rotation, never boneIndex's own.
        const std::int32_t parentBoneIndex = definition.jointBoneIndices[static_cast<std::size_t>(parentJoint)];
        if (parentBoneIndex < 0 || static_cast<std::size_t>(parentBoneIndex) >= skeleton.bones.size()) {
            continue;
        }

        const Mat4 parentWorld = ComputeBoneWorldMatrix(skeleton, pose, parentBoneIndex);
        const Vec3 parentWorldPos = parentWorld.TransformPoint(Vec3::Zero());

        const Mat4 currentChildWorld = ComputeBoneWorldMatrix(skeleton, pose, boneIndex);
        const Vec3 currentChildPos = currentChildWorld.TransformPoint(Vec3::Zero());
        const Vec3 rawCurrentDelta = currentChildPos - parentWorldPos;

        const Vec3 rawTargetDelta = simulatedJointWorldPositions[i] - parentWorldPos;

        if (LengthSquared(rawCurrentDelta) < kMinDirectionLengthSq || LengthSquared(rawTargetDelta) < kMinDirectionLengthSq) {
            continue;
        }

        const Vec3 currentDir = Normalize(rawCurrentDelta);
        const Vec3 targetDir = Normalize(rawTargetDelta);

        const float dot = Clamp(Dot(currentDir, targetDir), -1.0f, 1.0f);
        const float angle = std::acos(dot);
        if (angle < kMinAngleRadians) {
            continue;
        }

        Vec3 axis = Cross(currentDir, targetDir);
        if (LengthSquared(axis) < kMinDirectionLengthSq) {
            continue;
        }
        axis = Normalize(axis);

        const Quat delta = Quat::FromAxisAngle(axis, angle);
        const Quat newParentWorldRotation = delta * Quat::FromMat4(parentWorld);

        const std::int32_t grandparentBoneIndex = skeleton.bones[static_cast<std::size_t>(parentBoneIndex)].parentBoneIndex;
        const Mat4 grandparentWorld = ComputeBoneWorldMatrix(skeleton, pose, grandparentBoneIndex);
        const Quat grandparentWorldRotationInverse = Quat::FromMat4(grandparentWorld).Inverse();
        pose[static_cast<std::size_t>(parentBoneIndex)].rotation =
            Normalize(grandparentWorldRotationInverse * newParentWorldRotation);
    }
}

} // namespace gte
