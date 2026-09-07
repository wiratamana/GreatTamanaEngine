#pragma once
#include "DynamicChainDefinition.h"
#include "../Animation/BoneLocalOffset.h"
#include "../Assets/SkeletonData.h"
#include "../Math/Vec3.h"

#include <vector>

namespace gte {

// Rewrites bone rotations in `pose` so that every joint in
// `definition.jointBoneIndices` visually lands at its corresponding
// `simulatedJointWorldPositions[i]` - the physics payoff of this whole
// campaign. Must be called AFTER Animation/AppendBoneSolver.h's
// ApplyAppendInheritance() and BEFORE Animation/SkeletonPose.h's
// ComputeSkinningMatrices() - see PHASE3's own pipeline-ordering rule.
//
// IMPORTANT DESIGN NOTE (found and fixed during Phase 2 implementation,
// verified against PHASE2_BONE_CHAIN_PHYSICS_BRIDGE.md's OWN required
// round-trip test - see tests/Physics/BoneChainPhysicsResolverTests.cpp):
// per this engine's own bind-relative local-transform formula
// (Animation/BonePoseMath.h's ComputeBoneLocalMatrix(), composed as
// Translate(localBindOffset) * Rotate(offset.rotation) - see Math/Mat4.h's
// TRS() doc comment), a bone's OWN rotation can NEVER move its OWN world
// position - `TRS(...).TransformPoint(Vec3::Zero())` always evaluates to
// exactly the Translate component, since `Rotate(...) * Scale(...) *
// Vec3::Zero()` is always the zero vector, no matter what the rotation is.
// A bone's rotation only ever swings its DESCENDANTS. Therefore, to make
// joint `i` (definition.jointBoneIndices[i]) actually LAND at
// `simulatedJointWorldPositions[i]`, this function must rewrite its
// PARENT's rotation (resolved via definition.parentJointIndex[i] - see
// step 1 below) - never `pose[jointBoneIndices[i]]`
// itself. This mirrors exactly how Animation/IkSolver.h's CCD solver
// already works (it rotates a LINK bone to swing a DESCENDANT effector
// toward a target - never the effector's own entry), just applied here to a
// direct parent/child pair instead of a multi-bone chain, and with the
// corrective rotation expressed in WORLD space (one corrective step per
// bone per frame, no iterative accumulation needed).
//
// Per joint i, in root-to-tip order (each iteration depends on the
// PREVIOUS joint's own already-rewritten `pose` entry - the previous
// iteration's write is an ANCESTOR of this iteration's child bone, exactly
// like IkSolver's own CCD chain - never process joints out of order or in
// parallel against the SAME chain):
//   1. parentBoneIndex is resolved via definition.parentJointIndex[i]
//      (task_manager/verlet-integration-6, Phase 1 - an explicit TREE-parent
//      position within jointBoneIndices, -1 meaning "my parent is
//      rootBoneIndex directly"): definition.rootBoneIndex when
//      parentJointIndex[i] < 0, otherwise
//      definition.jointBoneIndices[parentJointIndex[i]] - THIS is the bone
//      whose rotation gets rewritten this iteration. Skipped entirely
//      (nothing to rotate) if out of range (e.g. a chain with no real root
//      bone at all).
//   2. parentWorld = ComputeBoneWorldMatrix(skeleton, pose, parentBoneIndex);
//      parentWorldPos = parentWorld.TransformPoint(Vec3::Zero()).
//   3. currentChildWorld = ComputeBoneWorldMatrix(skeleton, pose, jointBoneIndices[i]).
//      currentDir = Normalize(currentChildWorld.TransformPoint(Vec3::Zero()) - parentWorldPos)
//      - the child bone's direction BEFORE this iteration's own correction
//      (i.e. wherever plain FK/IK/append, plus every EARLIER iteration in
//      this same call, left it).
//   4. targetDir = Normalize(simulatedJointWorldPositions[i] - parentWorldPos).
//   5. Skip (leave `pose[parentBoneIndex]` untouched) if either direction is
//      degenerate (near-zero length) or already ~parallel/antiparallel with
//      no well-defined rotation axis - mirroring IkSolver.cpp's own
//      kMinDirectionLengthSq/kMinAngleRadians guards.
//   6. axis = Normalize(Cross(currentDir, targetDir)); angle = acos(Clamp(Dot(currentDir, targetDir), -1, 1)).
//   7. delta = Quat::FromAxisAngle(axis, angle) - a WORLD-space rotation
//      (one corrective step per bone per frame, unlike IkSolver's
//      many-iteration local-space accumulation):
//      newParentWorldRotation = delta * Quat::FromMat4(parentWorld).
//   8. Convert newParentWorldRotation back to parentBoneIndex's LOCAL offset
//      rotation by removing parentBoneIndex's OWN parent's world rotation
//      (grandparentWorld = ComputeBoneWorldMatrix(skeleton, pose,
//      skeleton.bones[parentBoneIndex].parentBoneIndex) - gracefully
//      Identity() if parentBoneIndex has no parent of its own):
//      pose[parentBoneIndex].rotation = Normalize(Quat::FromMat4(grandparentWorld).Inverse() * newParentWorldRotation).
//      (pose[parentBoneIndex].translation is left UNCHANGED - PMX bones
//      never need a translation channel for a purely-rotated FK bend,
//      matching SkeletonPose.h's own bind-pose convention.)
//
// Note the tip bone (definition.jointBoneIndices.back()) never itself gets a
// rotation WRITE from this function (nothing needs to swing ITS
// descendants) - only its ANCESTORS do, which is exactly what's needed to
// place every joint (including the tip) at its own simulated position.
void ApplyDynamicChainPhysicsToPose(const SkeletonData& skeleton, const DynamicChainDefinition& definition,
    const std::vector<Vec3>& simulatedJointWorldPositions, std::vector<BoneLocalOffset>& pose);

} // namespace gte
