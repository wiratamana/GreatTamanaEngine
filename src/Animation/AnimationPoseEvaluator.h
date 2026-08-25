#pragma once

#include "MotionSampler.h"
#include "../Math/Mat4.h"

#include <vector>

namespace gte {

// Evaluates ONE animated skeleton pose, end to end, in the one fixed order
// this engine's MMD animation runtime depends on being followed EXACTLY:
// sample keyframes -> solve IK -> apply append/grant inheritance -> compute
// final forward-kinematics skinning matrices. Previously this exact 4-call
// sequence was inlined by hand inside Game::UpdateSkeletalAnimators()
// (src/Game/Game.cpp) - the ONLY call site - which meant the
// correctness-critical ordering below was enforced purely by a human
// reading a comment there, with nothing to stop a second call site (e.g. a
// future live posed-skeleton overlay for the Bone Viewer debug window - see
// TODO.md, "Bone Viewer: live posed-skeleton overlay") from getting it
// subtly wrong (e.g. running append before IK - see
// tests/Animation/AnimationPoseEvaluatorTests.cpp's
// AppendedBoneInheritsIkSolvedRotationNotRawBindPose for exactly what breaks
// if that happens). Pulling it out into one function makes THIS pipeline
// itself the single source of truth for the order, reusable and
// independently regression-tested rather than reproduced by hand at every
// call site.
//
// WHY THIS EXACT ORDER:
//   1. SampleAnimationPose()    - raw per-bone keyframed offsets, including
//                                 an IK bone's own animated TARGET position
//                                 (Animation/MotionSampler.h).
//   2. SolveIkChains()          - bends each IK chain's link bones (e.g. a
//                                 leg's thigh/knee) to bring their effector
//                                 onto wherever step 1 put the IK bone -
//                                 must run AFTER sampling (needs the IK
//                                 bone's own animated position to solve
//                                 toward) and BEFORE append inheritance (an
//                                 append SOURCE that's also an IK link must
//                                 already carry its IK-solved rotation - see
//                                 Animation/IkSolver.h's own file comment).
//   3. ApplyAppendInheritance() - resolves PMX append/grant bone inheritance
//                                 (e.g. a rig's separate "D-bone" skinning
//                                 chain) - must run AFTER IK solving (the
//                                 point above) and BEFORE the final FK
//                                 evaluation, so the skinning matrices
//                                 reflect inherited motion too (see
//                                 Animation/AppendBoneSolver.h's own file
//                                 comment).
//   4. ComputeSkinningMatrices() - the final forward-kinematics pass,
//                                 producing one ready-to-use skinning matrix
//                                 per bone (Animation/SkeletonPose.h).
//
// Pure CPU-side math over already-loaded skeleton/motion data - no ECS/GPU/
// Renderer/file-I/O dependency at all, exactly like each of the four
// functions it composes - Tier-1-testable (see
// tests/Animation/AnimationPoseEvaluatorTests.cpp).
std::vector<Mat4> EvaluateAnimatedSkinningPose(
    const SkeletonData& skeleton, const ResolvedAnimationBinding& binding, float frame);

} // namespace gte
