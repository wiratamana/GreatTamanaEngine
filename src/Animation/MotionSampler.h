#pragma once

#include "BoneLocalOffset.h"
#include "../Assets/MotionData.h"
#include "../Assets/SkeletonData.h"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace gte {

// Groups a MotionData's own flat, unsorted BoneKeyframe list (see
// MotionData.h's own file comment - a .vmd never guarantees its keyframes
// are grouped by bone or sorted by frame) by bone NAME, sorting each bone's
// own list by `frame` ascending - the shared prep step every model this
// motion gets bound to (via ResolveBoneTracksToSkeleton() below) reuses,
// computed once per distinct MotionData rather than re-scanned per bone per
// frame.
std::unordered_map<std::string, std::vector<BoneKeyframe>> GroupAndSortBoneTracksByName(const MotionData& motion);

// The result of resolving one MotionData against one SkeletonData by bone
// NAME (see ResolveBoneTracksToSkeleton() below) - this engine's answer to
// "the bones/weights in the animation file and the model file itself don't
// necessarily match": every list here is aligned 1:1 with
// SkeletonData::bones (index i here == skeleton bone i), and a skeleton
// bone with no matching motion track simply gets an EMPTY keyframe list -
// SampleBoneTrack() below already treats that as "stays at bind pose", the
// deliberate, tolerant fallback for a motion authored against (or shared
// across) a different model's own bone set. A motion's own bone track
// whose name doesn't match ANY bone in the skeleton is silently dropped -
// never a failure either way. `lastFrame` is the motion's own maximum
// bone-keyframe frame number (0 if the motion has no bone keyframes at
// all) - the natural loop point for a playback system (see
// Game::UpdateSkeletalAnimators(), src/Game/Game.cpp).
struct ResolvedAnimationBinding {
    std::vector<std::vector<BoneKeyframe>> perBoneKeyframes; // sized skeleton.bones.size()
    std::uint32_t lastFrame = 0;
};

ResolvedAnimationBinding ResolveBoneTracksToSkeleton(const SkeletonData& skeleton, const MotionData& motion);

// Samples ONE bone's already name-resolved, frame-sorted keyframe list
// (ResolvedAnimationBinding::perBoneKeyframes[i] above) at a given
// (possibly fractional) VMD frame number, via linear interpolation of
// translation and spherical interpolation (Slerp) of rotation between the
// two keyframes bracketing `frame` - deliberately NOT the true MMD bezier
// curve BoneKeyframe::interpolation bytes describe (see MotionData.h's own
// comment - nothing in this engine decodes/evaluates those yet); linear/
// slerp is a reasonable, visually-close first approximation and a natural
// place to plug in real bezier evaluation later without changing this
// function's own signature. Clamps to the first/last keyframe's own value
// when `frame` falls outside the list's own range. Returns
// BoneLocalOffset{} (bind pose, no extra offset at all) for an empty list -
// see ResolvedAnimationBinding's own doc comment above for why that's the
// correct, expected fallback.
BoneLocalOffset SampleBoneTrack(const std::vector<BoneKeyframe>& sortedKeyframes, float frame);

// Samples EVERY bone in `binding` at `frame` in one call - the direct input
// SkeletonPose.h's ComputeSkinningMatrices() expects.
std::vector<BoneLocalOffset> SampleAnimationPose(const ResolvedAnimationBinding& binding, float frame);

} // namespace gte
