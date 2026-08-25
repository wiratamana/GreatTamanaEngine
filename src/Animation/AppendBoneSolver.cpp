#include "AppendBoneSolver.h"

#include "../Math/Quat.h"

#include <cstdint>
#include <functional>

namespace gte {

void ApplyAppendInheritance(const SkeletonData& skeleton, std::vector<BoneLocalOffset>& pose)
{
    const std::size_t count = skeleton.bones.size();
    if (pose.size() < count) {
        pose.resize(count);
    }

    // `pose` holds each bone's own RAW (FK/IK-solved, pre-append) offset -
    // kept as the read side throughout, so appendLocal's "that bone's own
    // local offset only" case (see this file's own header comment) always
    // means exactly that, never an already-appended value. `resolved` is
    // the append-adjusted output, built up as each bone is visited.
    const std::vector<BoneLocalOffset> raw = pose;
    std::vector<BoneLocalOffset> resolved = pose;
    // 0 = unvisited, 1 = in-progress (cycle guard), 2 = done.
    std::vector<std::uint8_t> state(count, 0);

    std::function<BoneLocalOffset(std::size_t)> resolve = [&](std::size_t index) -> BoneLocalOffset {
        if (state[index] == 2) {
            return resolved[index];
        }
        if (state[index] == 1) {
            // A cyclic append chain (malformed data) - fall back to this
            // bone's own un-appended offset for THIS recursion rather than
            // recursing forever.
            return raw[index];
        }
        state[index] = 1;

        const Bone& bone = skeleton.bones[index];
        BoneLocalOffset own = raw[index];

        if ((bone.appendRotate || bone.appendTranslate) && bone.appendBoneIndex >= 0
            && static_cast<std::size_t>(bone.appendBoneIndex) < count
            && static_cast<std::size_t>(bone.appendBoneIndex) != index) {
            const std::size_t sourceIndex = static_cast<std::size_t>(bone.appendBoneIndex);
            const BoneLocalOffset source = bone.appendLocal ? raw[sourceIndex] : resolve(sourceIndex);

            if (bone.appendRotate) {
                const Quat appended = Slerp(Quat::Identity(), source.rotation, bone.appendWeight);
                own.rotation = Normalize(own.rotation * appended);
            }
            if (bone.appendTranslate) {
                own.translation = own.translation + source.translation * bone.appendWeight;
            }
        }

        resolved[index] = own;
        state[index] = 2;
        return own;
    };

    for (std::size_t i = 0; i < count; ++i) {
        resolve(i);
    }

    pose = std::move(resolved);
}

} // namespace gte
