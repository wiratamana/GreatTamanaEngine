#include "DynamicChainDefinition.h"

namespace gte {

DynamicChainJointLocation FindDynamicChainJointByBoneIndex(
    const std::vector<DynamicChainDefinition>& chains, std::int32_t boneIndex)
{
    if (boneIndex < 0) {
        return DynamicChainJointLocation{};
    }
    for (std::size_t chainIndex = 0; chainIndex < chains.size(); ++chainIndex) {
        const std::vector<std::int32_t>& joints = chains[chainIndex].jointBoneIndices;
        for (std::size_t jointIndex = 0; jointIndex < joints.size(); ++jointIndex) {
            if (joints[jointIndex] == boneIndex) {
                return DynamicChainJointLocation{
                    static_cast<std::int32_t>(chainIndex), static_cast<std::int32_t>(jointIndex) };
            }
        }
    }
    return DynamicChainJointLocation{};
}

} // namespace gte
