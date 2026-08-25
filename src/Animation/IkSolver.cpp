#include "IkSolver.h"

#include "../Math/Mat4.h"
#include "../Math/Quat.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace gte {

namespace {

// A hard ceiling on iterationCount independent of whatever a (possibly
// malformed) .pmx claims, so a corrupt ikIterationCount can never make one
// frame's animation update take an unbounded amount of time.
constexpr int kMaxIkIterations = 200;

// Used only when a PMX IK bone's own ikAngleLimitRadians is <= 0 (absent/
// malformed data) - a small, conservative per-step correction limit so the
// solver still converges smoothly rather than snapping/oscillating.
constexpr float kFallbackMaxAnglePerStep = 0.0698131701f; // 4 degrees, in radians.

constexpr float kMinDirectionLengthSq = 1e-10f;
constexpr float kMinAngleRadians = 1e-5f;

// Duplicates SkeletonPose.cpp's own bind-relative local-transform formula
// (see SkeletonPose.h's file comment for the derivation) rather than
// sharing it, on purpose: SkeletonPose::ComputeSkinningMatrices() computes
// EVERY bone's world matrix ONCE per call via one memoized pass (O(bone
// count) total) - exactly right for "evaluate the whole skeleton's final
// pose", but wasteful for what THIS file needs, which is a handful of
// single-bone world-position/rotation queries PER CCD ITERATION, where the
// bones actually queried keep changing (any link bone's own rotation may
// have just been updated by the very iteration in progress). Recomputing
// one bone's world matrix from scratch (walking its own ancestor chain,
// with the same cycle guard) is O(chain depth) - trivial for the shallow
// (2-4 bone) chains a real leg/arm IK ever has, and always reflects
// whatever `pose` holds RIGHT NOW, including any change this same solve
// already made to an earlier link/iteration.
Mat4 ComputeBoneWorldMatrixRecursive(const SkeletonData& skeleton, const std::vector<BoneLocalOffset>& pose,
    std::int32_t boneIndex, std::vector<std::uint8_t>& visiting)
{
    const std::size_t count = skeleton.bones.size();
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= count) {
        return Mat4::Identity();
    }

    const std::size_t index = static_cast<std::size_t>(boneIndex);
    if (visiting[index] != 0) {
        // Cyclic parent chain (malformed data) - break the cycle instead of
        // recursing forever, same convention as SkeletonPose.cpp.
        return Mat4::Identity();
    }
    visiting[index] = 1;

    const Bone& bone = skeleton.bones[index];
    const BoneLocalOffset offset = index < pose.size() ? pose[index] : BoneLocalOffset{};

    Vec3 parentBindPosition = Vec3::Zero();
    Mat4 parentWorld = Mat4::Identity();
    if (bone.parentBoneIndex >= 0 && static_cast<std::size_t>(bone.parentBoneIndex) < count
        && static_cast<std::size_t>(bone.parentBoneIndex) != index) {
        const std::size_t parentIndex = static_cast<std::size_t>(bone.parentBoneIndex);
        parentBindPosition = skeleton.bones[parentIndex].position;
        parentWorld = ComputeBoneWorldMatrixRecursive(skeleton, pose, bone.parentBoneIndex, visiting);
    }

    const Vec3 localBindOffset = bone.position - parentBindPosition;
    const Mat4 local = Mat4::TRS(localBindOffset + offset.translation, offset.rotation, Vec3::One());
    const Mat4 world = parentWorld * local;

    visiting[index] = 0; // Allow this bone to be visited again from a sibling/later query.
    return world;
}

Mat4 ComputeBoneWorldMatrix(const SkeletonData& skeleton, const std::vector<BoneLocalOffset>& pose, std::int32_t boneIndex)
{
    std::vector<std::uint8_t> visiting(skeleton.bones.size(), 0);
    return ComputeBoneWorldMatrixRecursive(skeleton, pose, boneIndex, visiting);
}

// Clamps `rotation` (a TOTAL rotation from bind pose - see
// BoneLocalOffset.h) component-wise, in Euler XYZ degrees, against a link's
// own PMX angle limit (already in radians - see Bone::IkLink). A pragmatic
// approximation of PMX's per-axis IK constraint (see this file's own
// header comment for why this isn't a bit-perfect MMD reimplementation).
Quat ClampRotationToAngleLimits(const Quat& rotation, const Vec3& angleLimitMin, const Vec3& angleLimitMax)
{
    const Vec3 eulerDeg = rotation.ToEulerDegrees(); // (pitchX, yawY, rollZ)
    const float clampedPitchX = Clamp(eulerDeg.x, RadToDeg(angleLimitMin.x), RadToDeg(angleLimitMax.x));
    const float clampedYawY = Clamp(eulerDeg.y, RadToDeg(angleLimitMin.y), RadToDeg(angleLimitMax.y));
    const float clampedRollZ = Clamp(eulerDeg.z, RadToDeg(angleLimitMin.z), RadToDeg(angleLimitMax.z));
    return Quat::FromEulerDegrees(clampedPitchX, clampedYawY, clampedRollZ);
}

} // namespace

void SolveIkChains(const SkeletonData& skeleton, std::vector<BoneLocalOffset>& pose)
{
    const std::size_t count = skeleton.bones.size();
    if (pose.size() < count) {
        pose.resize(count);
    }

    for (std::size_t ikBoneIndex = 0; ikBoneIndex < count; ++ikBoneIndex) {
        const Bone& ikBone = skeleton.bones[ikBoneIndex];
        if (!ikBone.isIk || ikBone.ikLinks.empty()) {
            continue;
        }
        if (ikBone.ikTargetBoneIndex < 0 || static_cast<std::size_t>(ikBone.ikTargetBoneIndex) >= count) {
            continue; // No valid effector bone to solve toward.
        }
        const std::size_t effectorIndex = static_cast<std::size_t>(ikBone.ikTargetBoneIndex);

        const int iterationCount =
            ikBone.ikIterationCount > 0 ? std::min(ikBone.ikIterationCount, kMaxIkIterations) : kMaxIkIterations;
        const float maxAnglePerStep =
            ikBone.ikAngleLimitRadians > 0.0f ? ikBone.ikAngleLimitRadians : kFallbackMaxAnglePerStep;

        for (int iteration = 0; iteration < iterationCount; ++iteration) {
            // Wherever this motion's own keyframes actually moved the
            // (invisible) IK bone to - the fixed point this whole pass
            // tries to bring the effector onto. Re-derived every iteration
            // (rather than cached once outside the loop) so this stays
            // correct even in the unusual case where the IK bone's own
            // position is itself affected by one of its links (e.g. a
            // shared parent) - negligible extra cost either way given how
            // shallow a real IK chain's ancestor walk is.
            const Vec3 targetPos =
                ComputeBoneWorldMatrix(skeleton, pose, static_cast<std::int32_t>(ikBoneIndex))
                    .TransformPoint(Vec3::Zero());

            bool changedThisPass = false;

            for (const Bone::IkLink& link : ikBone.ikLinks) {
                if (link.boneIndex < 0 || static_cast<std::size_t>(link.boneIndex) >= count) {
                    continue;
                }
                const std::size_t linkIndex = static_cast<std::size_t>(link.boneIndex);

                const Mat4 linkWorld = ComputeBoneWorldMatrix(skeleton, pose, static_cast<std::int32_t>(linkIndex));
                const Vec3 linkWorldPos = linkWorld.TransformPoint(Vec3::Zero());
                const Quat linkWorldRotInv = Quat::FromMat4(linkWorld).Inverse();

                const Mat4 effectorWorld =
                    ComputeBoneWorldMatrix(skeleton, pose, static_cast<std::int32_t>(effectorIndex));
                const Vec3 effectorWorldPos = effectorWorld.TransformPoint(Vec3::Zero());

                const Vec3 toEffector = effectorWorldPos - linkWorldPos;
                const Vec3 toTarget = targetPos - linkWorldPos;
                if (LengthSquared(toEffector) < kMinDirectionLengthSq || LengthSquared(toTarget) < kMinDirectionLengthSq) {
                    continue; // Effector or target sits right on top of this link bone - no well-defined direction.
                }

                // Express both directions in the link bone's own local
                // space (cancels out whatever it/its ancestors are
                // currently rotated by), so the axis/angle computed below
                // is a pure LOCAL rotation delta for this bone alone.
                const Vec3 localEffectorDir = Normalize(linkWorldRotInv.RotateVector(toEffector));
                const Vec3 localTargetDir = Normalize(linkWorldRotInv.RotateVector(toTarget));

                const float dot = Clamp(Dot(localEffectorDir, localTargetDir), -1.0f, 1.0f);
                float angle = std::acos(dot);
                if (angle < kMinAngleRadians) {
                    continue; // Already close enough - nothing to correct.
                }

                Vec3 axis = Cross(localEffectorDir, localTargetDir);
                if (LengthSquared(axis) < kMinDirectionLengthSq) {
                    continue; // Directions are parallel/antiparallel - no well-defined rotation axis.
                }
                axis = Normalize(axis);
                angle = std::min(angle, maxAnglePerStep);

                const Quat delta = Quat::FromAxisAngle(axis, angle);
                // Post-multiply: `delta` was derived in this bone's own
                // CURRENT total-orientation frame, so appending it "before"
                // (Hamilton-product-wise, on the right of) the bone's
                // existing bind-relative rotation yields the correct new
                // total rotation - see Quat.h's own composition-order
                // comment ("p * q means apply q first, then p").
                Quat newRotation = Normalize(pose[linkIndex].rotation * delta);

                if (link.hasAngleLimit) {
                    newRotation = ClampRotationToAngleLimits(newRotation, link.angleLimitMin, link.angleLimitMax);
                }

                pose[linkIndex].rotation = newRotation;
                changedThisPass = true;
            }

            if (!changedThisPass) {
                break; // Converged (or stuck) - no link moved this pass, further iterations won't help.
            }
        }
    }
}

} // namespace gte
