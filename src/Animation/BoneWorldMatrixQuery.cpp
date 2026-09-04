#include "BoneWorldMatrixQuery.h"

#include "BoneChainResolver.h"
#include "BonePoseMath.h"

namespace gte {

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

} // namespace gte
