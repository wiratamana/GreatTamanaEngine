#pragma once

#include "BoneLocalOffset.h"
#include "../Assets/SkeletonData.h"
#include "../Math/Mat4.h"

#include <cstddef>
#include <cstdint>

namespace gte {

// The one shared "bind-relative local transform" formula both
// SkeletonPose.cpp (ComputeSkinningMatrices() - a memoized, whole-skeleton
// pass, see BoneChainResolver.h's ResolveBoneChain()) and IkSolver.cpp
// (ComputeBoneWorldMatrix() - a non-memoized, single-bone-at-a-time query,
// see BoneChainResolver.h's ResolveSingleBoneChain(), needed since a CCD
// solve mutates a bone's own offset between successive queries mid-solve)
// each independently hand-rolled before this existed:
//
//   localBindOffset(bone) = bone.position - (parent ? parent.position : 0)
//   localMatrix(bone)     = Translate(localBindOffset + offset.translation)
//                              * Rotate(offset.rotation)
//
// See SkeletonPose.h's own file comment for the full derivation/reasoning
// (why PMX's bind pose needs no separate bind ROTATION, why this composes
// correctly into a genuine world matrix once multiplied by a resolved
// parent world matrix, ...) - this function is exactly that one formula,
// pulled out so both callers can never let it drift out of sync with each
// other again.
//
// `boneIndex` must be a valid index into `skeleton.bones` - callers are
// expected to have already range-checked it (both current callers only ever
// invoke this from inside a BoneChainResolver.h walk, which only ever visits
// valid indices).
inline Mat4 ComputeBoneLocalMatrix(const SkeletonData& skeleton, std::size_t boneIndex, const BoneLocalOffset& offset)
{
    const Bone& bone = skeleton.bones[boneIndex];

    Vec3 parentBindPosition = Vec3::Zero();
    const std::int32_t parentIndex = bone.parentBoneIndex;
    if (parentIndex >= 0 && static_cast<std::size_t>(parentIndex) < skeleton.bones.size()
        && static_cast<std::size_t>(parentIndex) != boneIndex) {
        parentBindPosition = skeleton.bones[static_cast<std::size_t>(parentIndex)].position;
    }

    const Vec3 localBindOffset = bone.position - parentBindPosition;
    return Mat4::TRS(localBindOffset + offset.translation, offset.rotation, Vec3::One());
}

} // namespace gte
