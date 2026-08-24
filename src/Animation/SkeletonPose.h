#pragma once

#include "BoneLocalOffset.h"
#include "../Assets/SkeletonData.h"
#include "../Math/Mat4.h"

#include <vector>

namespace gte {

// Evaluates every bone's final SKINNING matrix (see below) from a bind-pose
// SkeletonData plus one BoneLocalOffset per bone - the pure forward-
// kinematics core of this engine's (still deliberately partial - see below)
// MMD animation runtime. No ECS/GPU/Renderer/file-I/O dependency at all, so
// this is fully Tier-1-testable (see tests/Animation/SkeletonPoseTests.cpp)
// exactly like ECS/TransformHierarchy.h's own ComputeWorldMatrix().
//
// PMX's own convention (see SkeletonData.h's own file comment) is that every
// Bone::position is already an ABSOLUTE, model-space bind-pose position -
// there is no separate bind ROTATION stored anywhere (every bone is
// implicitly unrotated in its bind pose, i.e. authored in a T/A-pose). This
// is what makes each bone's bind-pose WORLD matrix trivially
// Mat4::Translation(bone.position) - composing pure translations always
// telescopes to the same absolute position no matter how the hierarchy is
// walked, so there's no need to walk the parent chain just to find a bind
// pose. An ANIMATED pose, however, genuinely needs the parent chain walked
// (a rotated parent bone visibly swings every descendant with it), which is
// exactly what this function does: for each bone, in an order that always
// visits a parent before its children (safe even though
// SkeletonData::Bone::parentBoneIndex may point at a LATER array index -
// see SkeletonData.h's own file comment - via one-time-memoized recursion,
// with a cycle guard so a malformed/cyclic parent chain degrades to
// Identity() for the repeated node instead of infinite-recursing):
//
//   localBindOffset(bone) = bone.position - (parent ? parent.position : 0)
//   animatedLocal(bone)   = Translate(localBindOffset + offset.translation)
//                              * Rotate(offset.rotation)
//   animatedWorld(bone)   = animatedWorld(parent) * animatedLocal(bone)
//   skinningMatrix(bone)  = animatedWorld(bone) * Translate(-bone.position)
//
// skinningMatrix(bone) is exactly what a CPU/GPU skinning step
// (VertexSkinning.h) multiplies a BIND-POSE vertex position/normal by - it
// already folds in the inverse bind pose (Translate(-bone.position)), so a
// bone that receives an all-identity BoneLocalOffset (see above) produces
// exactly Mat4::Identity() here, leaving that bone's influenced vertices
// completely unmoved, as expected.
//
// Deliberately FORWARD-KINEMATICS ONLY: does not evaluate IK chains
// (Bone::isIk/ikLinks/ikTargetBoneIndex) or append/inherit rotation-
// translation (Bone::appendRotate/appendTranslate/appendBoneIndex) - see
// TODO.md, "Real MMD skinning/animation runtime", for why a full IK solver
// and append-inheritance evaluator are explicitly deferred follow-ups
// rather than included here. An IK-target or append-driven bone still moves
// exactly as its OWN BoneLocalOffset says (whatever a motion keyframes it
// to directly) - it just doesn't additionally get IK-solved or inherit
// another bone's motion the way a full MMD runtime would.
//
// Returns one matrix per `skeleton.bones` entry, index-aligned 1:1.
// `localOffsets` may be shorter than `skeleton.bones` (a missing tail
// entry is treated as BoneLocalOffset{} - bind pose) - never indexed out of
// bounds.
std::vector<Mat4> ComputeSkinningMatrices(
    const SkeletonData& skeleton, const std::vector<BoneLocalOffset>& localOffsets);

} // namespace gte
