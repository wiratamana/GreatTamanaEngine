#pragma once

#include "BoneLocalOffset.h"
#include "../Assets/SkeletonData.h"

#include <vector>

namespace gte {

// Resolves PMX "append" (a.k.a. "grant"/付与) bone inheritance: for every
// bone with Bone::appendRotate and/or Bone::appendTranslate set, blends
// `appendWeight` of its `appendBoneIndex` source bone's rotation/
// translation into its OWN entry in `pose`, in place.
//
// WHY THIS EXISTS: many higher-quality MMD rigs (this engine's own leg IK
// fix - see Animation/IkSolver.h - surfaced a real, concrete case of this)
// route the actual per-vertex skin weights through a SEPARATE, parallel
// "D-bone" chain (e.g. "左足D"/"左ひざD"/"左足首D", each parented the same
// way as - and bind-position-coincident with - "左足"/"左ひざ"/"左足首", but
// never directly keyframed themselves) purely so the main FK/IK chain stays
// "clean" for animators/IK solving while the mesh itself deforms through
// the D-chain. Every one of those D-bones has appendRotate=true,
// appendBoneIndex pointing at its non-D counterpart, and appendWeight==1.0
// ("copy 100% of that bone's rotation"). Without this pass, a model rigged
// this way visibly does NOT move at those joints even once IK/FK is solved
// correctly for the main chain - the mesh is skinned to the D-bones, which
// never receive any of that motion, since nothing evaluates
// Bone::appendRotate/appendTranslate anywhere else in this engine
// (SkeletonPose.h's ComputeSkinningMatrices() is explicit that it doesn't -
// see its own file comment).
//
// HOW IT WORKS: for bone i with appendRotate/appendTranslate set,
//   source = bone.appendLocal
//                ? pose[bone.appendBoneIndex]              // that bone's OWN local offset only (ignores ITS OWN append, if any)
//                : <appendBoneIndex's fully-resolved offset>  // that bone's TOTAL/cascaded offset (recursively resolved first)
//   if appendRotate:    pose[i].rotation    = Normalize(pose[i].rotation * Slerp(Identity, source.rotation, appendWeight))
//   if appendTranslate: pose[i].translation = pose[i].translation + source.translation * appendWeight
// `appendWeight` may legitimately be negative (e.g. a shoulder-cancel bone
// countering its parent's rotation) - Slerp(Identity, q, t) is a well-
// defined extrapolation for t outside [0,1], giving exactly the scaled/
// inverted rotation such a bone needs. Resolution order is topological
// (a bone's append source is fully resolved before that bone itself, with
// a cycle guard degrading to that bone's own un-appended pose instead of
// recursing forever) so a cascading append chain (an append source that
// itself has an append) resolves correctly, even though no bone in this
// engine's own test/production rigs actually needs more than one level.
//
// Call this AFTER Animation/IkSolver.h's SolveIkChains() (so an append
// source that's also an IK chain link - e.g. a leg's thigh/knee - carries
// its IK-solved rotation, not just its raw keyframed one) and BEFORE
// SkeletonPose.h's ComputeSkinningMatrices() (so the final skinning
// matrices reflect every appended bone's inherited motion too).
//
// No ECS/GPU/Renderer/file-I/O dependency at all - Tier-1-testable exactly
// like IkSolver.h/SkeletonPose.h (see tests/Animation/AppendBoneSolverTests.cpp).
// `pose` is resized up to `skeleton.bones.size()` if shorter. Malformed/
// out-of-range/self-referencing appendBoneIndex values are silently
// skipped/ignored rather than crashing, same "degrade gracefully"
// convention as the rest of this module.
void ApplyAppendInheritance(const SkeletonData& skeleton, std::vector<BoneLocalOffset>& pose);

} // namespace gte
