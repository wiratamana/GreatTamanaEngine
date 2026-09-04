#pragma once

#include "BoneLocalOffset.h"
#include "../Assets/SkeletonData.h"
#include "../Math/Mat4.h"

#include <cstdint>
#include <vector>

namespace gte {

// A single bone's CURRENT world matrix, re-derived fresh from `pose` every
// call by walking only that bone's own ancestor chain
// (BoneChainResolver.h's ResolveSingleBoneChain()) - deliberately NOT
// memoized across calls, since `pose` may be mutated between successive
// queries by a caller mid-solve (IkSolver.cpp's CCD loop; Physics/
// DynamicChainSolver.h's per-joint aim-solve - see
// Physics/BoneChainPhysicsResolver.h). Shares the exact same bind-relative
// local-transform formula SkeletonPose.cpp uses (BonePoseMath.h's
// ComputeBoneLocalMatrix()), so every caller of this function can never
// silently drift out of sync with the FK pass itself.
//
// Promoted out of IkSolver.cpp (where it originated) into this shared
// header specifically so Physics/DynamicChainSolver.h (src/Physics/) can
// reuse it rather than hand-rolling a second, independent copy of this
// exact cycle-guarded ancestor walk - see AGENTS.md, "Skeletal Animation
// Pose Resolution": "Never hand-roll a new cycle-guarded
// bone-ancestor-chain walk - use Animation/BoneChainResolver.h's ...
// instead."
Mat4 ComputeBoneWorldMatrix(const SkeletonData& skeleton, const std::vector<BoneLocalOffset>& pose, std::int32_t boneIndex);

} // namespace gte
