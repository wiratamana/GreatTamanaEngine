#include "MotionSampler.h"

#include <algorithm>

namespace gte {

std::unordered_map<std::string, std::vector<BoneKeyframe>> GroupAndSortBoneTracksByName(const MotionData& motion)
{
    std::unordered_map<std::string, std::vector<BoneKeyframe>> grouped;
    for (const BoneKeyframe& keyframe : motion.boneKeyframes) {
        grouped[keyframe.boneName].push_back(keyframe);
    }
    for (auto& entry : grouped) {
        std::sort(entry.second.begin(), entry.second.end(),
            [](const BoneKeyframe& a, const BoneKeyframe& b) { return a.frame < b.frame; });
    }
    return grouped;
}

ResolvedAnimationBinding ResolveBoneTracksToSkeleton(const SkeletonData& skeleton, const MotionData& motion)
{
    ResolvedAnimationBinding binding;
    binding.perBoneKeyframes.resize(skeleton.bones.size());

    const std::unordered_map<std::string, std::vector<BoneKeyframe>> grouped = GroupAndSortBoneTracksByName(motion);

    for (std::size_t i = 0; i < skeleton.bones.size(); ++i) {
        if (const auto found = grouped.find(skeleton.bones[i].name); found != grouped.end()) {
            binding.perBoneKeyframes[i] = found->second;
        }
        // else: stays empty - see this struct's own doc comment above.
    }

    for (const BoneKeyframe& keyframe : motion.boneKeyframes) {
        binding.lastFrame = std::max(binding.lastFrame, keyframe.frame);
    }

    return binding;
}

BoneLocalOffset SampleBoneTrack(const std::vector<BoneKeyframe>& sortedKeyframes, float frame)
{
    if (sortedKeyframes.empty()) {
        return BoneLocalOffset{};
    }

    if (frame <= static_cast<float>(sortedKeyframes.front().frame)) {
        const BoneKeyframe& kf = sortedKeyframes.front();
        return BoneLocalOffset{ kf.translation, kf.rotation };
    }
    if (frame >= static_cast<float>(sortedKeyframes.back().frame)) {
        const BoneKeyframe& kf = sortedKeyframes.back();
        return BoneLocalOffset{ kf.translation, kf.rotation };
    }

    // Find the first keyframe whose frame is > `frame` - the two range
    // checks above guarantee there's at least one earlier keyframe left to
    // bracket with (i.e. `it` is never `begin()`).
    const auto it = std::upper_bound(sortedKeyframes.begin(), sortedKeyframes.end(), frame,
        [](float value, const BoneKeyframe& kf) { return value < static_cast<float>(kf.frame); });

    const BoneKeyframe& b = *it;
    const BoneKeyframe& a = *(it - 1);

    const float span = static_cast<float>(b.frame) - static_cast<float>(a.frame);
    const float t = span > 0.0f ? (frame - static_cast<float>(a.frame)) / span : 0.0f;

    BoneLocalOffset result;
    result.translation = Lerp(a.translation, b.translation, t);
    result.rotation = Slerp(a.rotation, b.rotation, t);
    return result;
}

std::vector<BoneLocalOffset> SampleAnimationPose(const ResolvedAnimationBinding& binding, float frame)
{
    std::vector<BoneLocalOffset> pose(binding.perBoneKeyframes.size());
    for (std::size_t i = 0; i < binding.perBoneKeyframes.size(); ++i) {
        pose[i] = SampleBoneTrack(binding.perBoneKeyframes[i], frame);
    }
    return pose;
}

} // namespace gte
