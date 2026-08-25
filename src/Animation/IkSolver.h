#pragma once

#include "BoneLocalOffset.h"
#include "../Assets/SkeletonData.h"

#include <vector>

namespace gte {

// Evaluates every IK chain in `skeleton` (Bone::isIk/ikLinks/
// ikTargetBoneIndex/ikIterationCount/ikAngleLimitRadians - see
// SkeletonData.h) and adjusts `pose` IN PLACE so each chain's own link bones
// (e.g. a leg's thigh/knee) rotate to bring their EFFECTOR bone
// (Bone::ikTargetBoneIndex, e.g. the ankle) to wherever the IK bone ITSELF
// was animated to (see BoneLocalOffset.h) - a Cyclic-Coordinate-Descent
// (CCD) solver, the standard technique MMD viewers/engines use for this
// exact problem.
//
// WHY THIS EXISTS (the "legs don't move" bug this file fixes): a VMD dance
// motion never keyframes a leg's thigh/knee bones directly - it keyframes
// an invisible IK TARGET bone at the foot instead (MMD's own
// 左足ＩＫ/右足ＩＫ, "left/right leg IK"). SkeletonPose.h's
// ComputeSkinningMatrices() is deliberately forward-kinematics only (see its
// own file comment) - it moves a bone exactly as far as ITS OWN
// BoneLocalOffset says and no further, so without this pass, the thigh/knee
// bones' entries in `pose` stay at BoneLocalOffset{} (bind pose) for the
// WHOLE clip, since no motion keyframe ever targets them directly - the
// model plays back with its torso/arms/head animating normally while both
// legs stay frozen in the bind pose. Bone::isIk/ikLinks/ikTargetBoneIndex/
// ikIterationCount/ikAngleLimitRadians (SkeletonData.h) are already
// extracted from the source .pmx by PmxLoader.h/RigFile.h - they just were
// never evaluated by anything until this file existed (see TODO.md, "Real
// MMD skinning/animation runtime").
//
// HOW IT WORKS: for each bone with `isIk == true`, this walks its own
// `ikLinks` (PMX's own storage order - nearest-to-the-effector first, e.g.
// [knee, thigh] for a leg) up to `ikIterationCount` times. Each step:
//   1. Re-derives the CURRENT world position of both the link bone and the
//      effector bone by walking the (partially-already-adjusted) `pose`
//      up to the skeleton root - so a change made to an earlier link this
//      same pass (e.g. the knee) is immediately visible to a later one
//      (e.g. the thigh), and a change made in an earlier ITERATION is
//      visible to every subsequent one.
//   2. Computes, in the link bone's own local space (cancelling out
//      whatever it/its ancestors are currently rotated by), the axis/angle
//      rotation that would swing the effector directly onto the target -
//      clamped to at most `ikAngleLimitRadians` per step (PMX's own
//      "how far can a single IK iteration correct" limit) to avoid
//      overshoot/oscillation.
//   3. Applies that rotation on top of the link bone's own current
//      BoneLocalOffset::rotation (which already represents this bone's
//      TOTAL rotation from its bind pose - see BoneLocalOffset.h), then, if
//      the link has a PMX angle limit (`hasAngleLimit` - e.g. a knee
//      restricted to bending on one axis only), clamps that total rotation
//      component-wise (decomposed to Euler XYZ) against
//      `angleLimitMin`/`angleLimitMax`.
// Converges (stops iterating early) once a full pass changes nothing.
//
// Deliberately NOT a bit-perfect reimplementation of MMD's own IK solver
// (e.g. no dedicated single-axis "solve on this plane only" fast path some
// MMD engines use for an axis-limited link) - a generic axis-angle CCD step
// plus a post-hoc Euler-angle-limit clamp is a pragmatic, visually-close
// approximation, the same "reasonable first approximation, real bezier
// later" spirit as MotionSampler.h's own linear/slerp keyframe
// interpolation. No ECS/GPU/Renderer/file-I/O dependency at all - fully
// Tier-1-testable exactly like SkeletonPose.h (see
// tests/Animation/IkSolverTests.cpp).
//
// `pose` is resized up to `skeleton.bones.size()` if it started out
// shorter (a missing tail entry is treated as BoneLocalOffset{}, matching
// ComputeSkinningMatrices()'s own convention). Malformed/out-of-range bone,
// link, or target indices are silently skipped rather than crashing - same
// "degrade gracefully" convention as SkeletonPose.cpp's own cycle guard.
// Call this AFTER MotionSampler.h's SampleAnimationPose() (so `pose` holds
// each bone's raw keyframed offset, including the IK target bone's own
// animated position) and BEFORE SkeletonPose.h's ComputeSkinningMatrices()
// (so the final skinning matrices reflect the IK-solved thigh/knee
// rotations too).
void SolveIkChains(const SkeletonData& skeleton, std::vector<BoneLocalOffset>& pose);

} // namespace gte
