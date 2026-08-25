#include "AppendBoneSolver.h"

#include "BoneChainResolver.h"
#include "../Math/Quat.h"

#include <cstdint>

namespace gte {

void ApplyAppendInheritance(const SkeletonData& skeleton, std::vector<BoneLocalOffset>& pose)
{
    const std::size_t count = skeleton.bones.size();
    if (pose.size() < count) {
        pose.resize(count);
    }

    // `pose` holds each bone's own RAW (FK/IK-solved, pre-append) offset -
    // captured here and read from throughout (`raw`) whenever a bone's own
    // appendLocal flag asks for "that bone's own local offset only" (see
    // this function's own doc comment, AppendBoneSolver.h) rather than its
    // source's fully-resolved (post-append) value.
    const std::vector<BoneLocalOffset> raw = pose;

    // One memoized pass over the whole skeleton (BoneChainResolver.h's
    // ResolveBoneChain() - see its own file comment), walking
    // Bone::appendBoneIndex (NOT parentBoneIndex - this resolves the
    // APPEND-SOURCE chain, a completely separate graph from the bone
    // hierarchy SkeletonPose.cpp's ComputeSkinningMatrices() walks, even
    // though the underlying cycle-guarded memoization shape is identical).
    const std::vector<BoneLocalOffset> resolved = ResolveBoneChain<BoneLocalOffset>(skeleton, BoneLocalOffset{},
        [&](std::size_t index) -> std::int32_t {
            const Bone& bone = skeleton.bones[index];
            // appendLocal wants the source's RAW (pre-append) offset only -
            // never chase a recursive resolve for it. Returning -1 here
            // means the walk never even attempts to resolve the source bone
            // as part of resolving THIS bone; it's read directly from `raw`
            // in the resolveNode callback below instead. Any bone with no
            // append behavior at all also returns -1 (nothing to chase).
            if ((!bone.appendRotate && !bone.appendTranslate) || bone.appendLocal) {
                return -1;
            }
            return bone.appendBoneIndex;
        },
        [&](std::size_t index, const BoneLocalOffset& resolvedSource) -> BoneLocalOffset {
            const Bone& bone = skeleton.bones[index];
            BoneLocalOffset own = raw[index];

            if ((bone.appendRotate || bone.appendTranslate) && bone.appendBoneIndex >= 0
                && static_cast<std::size_t>(bone.appendBoneIndex) < count
                && static_cast<std::size_t>(bone.appendBoneIndex) != index) {
                const BoneLocalOffset& source =
                    bone.appendLocal ? raw[static_cast<std::size_t>(bone.appendBoneIndex)] : resolvedSource;

                if (bone.appendRotate) {
                    const Quat appended = Slerp(Quat::Identity(), source.rotation, bone.appendWeight);
                    own.rotation = Normalize(own.rotation * appended);
                }
                if (bone.appendTranslate) {
                    own.translation = own.translation + source.translation * bone.appendWeight;
                }
            }

            return own;
        });

    pose = resolved;
}

} // namespace gte
