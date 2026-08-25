#include "IkSolver.h"

#include "BoneChainResolver.h"
#include "BonePoseMath.h"
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

// A single bone's CURRENT world matrix, re-derived fresh from `pose` every
// call by walking only that bone's own ancestor chain
// (BoneChainResolver.h's ResolveSingleBoneChain()) - deliberately NOT
// memoized across calls, unlike SkeletonPose.cpp's whole-skeleton pass:
// `pose` is mutated by THIS solver's own CCD loop between successive
// queries (rotating an earlier link bone changes every later query's answer
// for the effector/next link), so caching a bone's world matrix across
// calls would return a stale answer - see SolveIkChains()'s own call site
// comments below for exactly when a fresh query is needed. Shares the exact
// same bind-relative local-transform formula SkeletonPose.cpp uses
// (BonePoseMath.h's ComputeBoneLocalMatrix()), so the two can never
// silently drift out of sync with each other.
Mat4 ComputeBoneWorldMatrix(
    const SkeletonData& skeleton, const std::vector<BoneLocalOffset>& pose, std::int32_t boneIndex)
{
    return ResolveSingleBoneChain<Mat4>(skeleton, boneIndex, Mat4::Identity(),
        [&](std::size_t index) -> std::int32_t { return skeleton.bones[index].parentBoneIndex; },
        [&](std::size_t index, const Mat4& parentWorld) -> Mat4 {
            const BoneLocalOffset offset = index < pose.size() ? pose[index] : BoneLocalOffset{};
            return parentWorld * ComputeBoneLocalMatrix(skeleton, index, offset);
        });
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
