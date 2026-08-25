#include "SkeletonPose.h"

#include "BoneChainResolver.h"
#include "BonePoseMath.h"

#include <cstdint>

namespace gte {

std::vector<Mat4> ComputeSkinningMatrices(
    const SkeletonData& skeleton, const std::vector<BoneLocalOffset>& localOffsets)
{
    const std::size_t count = skeleton.bones.size();

    // One memoized pass over the whole skeleton (BoneChainResolver.h's
    // ResolveBoneChain() - see its own file comment) walking
    // Bone::parentBoneIndex, composing each bone's own bind-relative local
    // matrix (BonePoseMath.h's ComputeBoneLocalMatrix()) on top of its
    // already-resolved parent's world matrix.
    const std::vector<Mat4> worldMatrices = ResolveBoneChain<Mat4>(skeleton, Mat4::Identity(),
        [&](std::size_t index) -> std::int32_t { return skeleton.bones[index].parentBoneIndex; },
        [&](std::size_t index, const Mat4& parentWorld) -> Mat4 {
            const BoneLocalOffset offset = index < localOffsets.size() ? localOffsets[index] : BoneLocalOffset{};
            return parentWorld * ComputeBoneLocalMatrix(skeleton, index, offset);
        });

    std::vector<Mat4> skinningMatrices(count);
    for (std::size_t i = 0; i < count; ++i) {
        skinningMatrices[i] = worldMatrices[i] * Mat4::Translation(-skeleton.bones[i].position);
    }
    return skinningMatrices;
}

} // namespace gte
