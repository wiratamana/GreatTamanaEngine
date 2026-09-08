#pragma once
#include "DynamicChainDefinition.h"
#include "../Animation/BoneLocalOffset.h"
#include "../Assets/SkeletonData.h"
#include "../Math/Vec3.h"

#include <vector>

namespace gte {

// Rewrites bone rotations/translations in `pose` so that every joint in
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
// Translate(localBindOffset + offset.translation) * Rotate(offset.rotation) -
// see Math/Mat4.h's TRS() doc comment), a bone's OWN rotation can NEVER move
// its OWN world position - `TRS(...).TransformPoint(Vec3::Zero())` always
// evaluates to exactly the Translate component, since `Rotate(...) *
// Scale(...) * Vec3::Zero()` is always the zero vector, no matter what the
// rotation is. A bone's rotation only ever swings its DESCENDANTS. However, a
// bone's own TRANSLATION channel DOES move its own world position (relative
// to its untouched parent), with zero effect on any sibling or on the parent
// itself - this is exactly the tool this function needs for the anchor case
// below.
//
// This function therefore branches on `definition.parentJointIndex[i]`:
//
// ANCHOR-ROOTED CASE (parentJointIndex[i] < 0 - the Bone Viewer's own
// "(root child)" label, i.e. this joint's tree-parent is
// `definition.rootBoneIndex` directly): `definition.rootBoneIndex` is
// FREQUENTLY a real, shared, load-bearing skeleton bone (e.g. MMD's own
// 下半身), which may simultaneously be the tree-parent of dozens of unrelated
// accessory joints AND the real FK ancestor of non-participating body bones
// (e.g. legs). Per DynamicChainDefinition.h's own documented contract, the
// anchor (`rootBoneIndex`) is "NOT simulated... always taken directly from
// the animated FK pose every step" - `pose[rootBoneIndex]` (neither its
// rotation NOR its translation) is therefore NEVER written by this function,
// for any joint, ever (task_manager/verlet-integration-8, Phase 1 - this
// fixes a prior defect where every anchor-rooted joint independently
// overwrote the anchor's own rotation, causing "whole body looks
// ragdoll-simulated" symptoms - see that campaign's PHASE0_MASTER_STRATEGY.md
// for the full investigation this fixes). Instead, THIS JOINT BONE's OWN
// local TRANSLATION is corrected so it lands exactly at its simulated target,
// computed once per joint, in the anchor's local space:
//   1. anchorWorld = ComputeBoneWorldMatrix(skeleton, pose, rootBoneIndex) -
//      never mutated by this function, only ever read.
//   2. desiredLocalPoint = anchorWorld.Inverse().TransformPoint(target), where
//      target = simulatedJointWorldPositions[i].
//   3. localBindOffset = skeleton.bones[boneIndex].position -
//      skeleton.bones[rootBoneIndex].position.
//   4. pose[boneIndex].translation = desiredLocalPoint - localBindOffset.
//   `pose[boneIndex].rotation` is left untouched by this branch - if this
//   same bone is itself some LATER joint's own tree-parent, the interior
//   branch below (a later iteration, per this array's own root-to-tip
//   ordering invariant) still correctly rewrites its rotation to aim that
//   descendant; translation and rotation are independent BoneLocalOffset
//   channels, so writing both across two different iterations composes
//   correctly. No angle/direction math or degenerate-direction guard is
//   needed here at all (unlike the interior case below) - it is pure,
//   always-well-defined point algebra; the only guard is the defensive
//   "matrix must be invertible" check via Mat4::TryInverse() (algebraically
//   never fails, since anchorWorld is always a pure unit-scale TRS), which
//   simply skips the joint (leaves its pose untouched) in that
//   never-expected-to-happen case.
//
// INTERIOR CASE (parentJointIndex[i] >= 0 - this joint's tree-parent is
// ANOTHER chain joint, never the anchor): completely unchanged from before
// this campaign. Since a bone's own rotation can never move its own
// position (see above), landing this joint at its target instead rewrites
// its PARENT JOINT's own rotation - this mirrors exactly how
// Animation/IkSolver.h's CCD solver already works (it rotates a LINK bone to
// swing a DESCENDANT effector toward a target - never the effector's own
// entry), just applied here to a direct parent/child pair instead of a
// multi-bone chain, and with the corrective rotation expressed in WORLD
// space (one corrective step per bone per frame, no iterative accumulation
// needed). Per joint i in this case, in root-to-tip order (each iteration
// depends on the PREVIOUS joint's own already-rewritten `pose` entry - the
// previous iteration's write is an ANCESTOR of this iteration's child bone,
// exactly like IkSolver's own CCD chain - never process joints out of order
// or in parallel against the SAME chain):
//   1. parentBoneIndex = definition.jointBoneIndices[parentJointIndex[i]] -
//      the bone whose rotation this iteration actually rewrites. Skipped
//      entirely (nothing to rotate) if out of range.
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
//      (pose[parentBoneIndex].translation is left UNCHANGED in this branch -
//      PMX bones never need a translation channel for a purely-rotated FK
//      bend, matching SkeletonPose.h's own bind-pose convention.)
//
// Note the tip bone (definition.jointBoneIndices.back()) never itself gets a
// rotation WRITE from the interior branch (nothing needs to swing ITS
// descendants) - only its ANCESTORS do (either via the interior branch's
// parent-rotation write, or via the anchor branch's own-translation write if
// it is itself anchor-rooted), which is exactly what's needed to place every
// joint (including the tip) at its own simulated position.
void ApplyDynamicChainPhysicsToPose(const SkeletonData& skeleton, const DynamicChainDefinition& definition,
    const std::vector<Vec3>& simulatedJointWorldPositions, std::vector<BoneLocalOffset>& pose);

} // namespace gte
