#include "SkeletonPose.h"

#include <cstdint>
#include <functional>

namespace gte {

std::vector<Mat4> ComputeSkinningMatrices(
    const SkeletonData& skeleton, const std::vector<BoneLocalOffset>& localOffsets)
{
    const std::size_t count = skeleton.bones.size();
    std::vector<Mat4> worldMatrices(count, Mat4::Identity());
    // 0 = unvisited, 1 = in-progress (cycle guard), 2 = done.
    std::vector<std::uint8_t> state(count, 0);

    std::function<Mat4(std::size_t)> computeWorld = [&](std::size_t index) -> Mat4 {
        if (state[index] == 2) {
            return worldMatrices[index];
        }
        if (state[index] == 1) {
            // A cyclic parent chain (malformed data) - break the cycle by
            // treating this bone as having no parent for THIS recursion,
            // rather than recursing forever.
            return Mat4::Identity();
        }
        state[index] = 1;

        const Bone& bone = skeleton.bones[index];
        const BoneLocalOffset offset = (index < localOffsets.size()) ? localOffsets[index] : BoneLocalOffset{};

        Vec3 parentBindPosition = Vec3::Zero();
        Mat4 parentWorld = Mat4::Identity();
        if (bone.parentBoneIndex >= 0 && static_cast<std::size_t>(bone.parentBoneIndex) < count
            && static_cast<std::size_t>(bone.parentBoneIndex) != index) {
            const std::size_t parentIndex = static_cast<std::size_t>(bone.parentBoneIndex);
            parentBindPosition = skeleton.bones[parentIndex].position;
            parentWorld = computeWorld(parentIndex);
        }

        const Vec3 localBindOffset = bone.position - parentBindPosition;
        const Mat4 local = Mat4::TRS(localBindOffset + offset.translation, offset.rotation, Vec3::One());
        const Mat4 world = parentWorld * local;

        worldMatrices[index] = world;
        state[index] = 2;
        return world;
    };

    for (std::size_t i = 0; i < count; ++i) {
        computeWorld(i);
    }

    std::vector<Mat4> skinningMatrices(count);
    for (std::size_t i = 0; i < count; ++i) {
        skinningMatrices[i] = worldMatrices[i] * Mat4::Translation(-skeleton.bones[i].position);
    }
    return skinningMatrices;
}

} // namespace gte
