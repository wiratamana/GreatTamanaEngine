# PHASE1 — COMPLETION REPORT: Anchor-Bone Immutability Fix In `BoneChainPhysicsResolver.cpp`

Status: **DONE**. Implements exactly the plan in
`PHASE1_ANCHOR_BONE_IMMUTABILITY_FIX_IN_BONE_CHAIN_PHYSICS_RESOLVER.md`.

## What was done

1. **`src/Physics/BoneChainPhysicsResolver.cpp`** — rewrote the per-joint loop
   inside `ApplyDynamicChainPhysicsToPose()` so it branches on
   `definition.parentJointIndex[i]`:
   - **`parentJoint < 0`** (this joint's tree-parent is the chain's own
     `rootBoneIndex`/anchor directly — the Bone Viewer's "(root child)"
     case): the anchor's `pose[rootBoneIndex]` entry is now **never** read
     for mutation and **never** written. Instead, the joint bone's own
     `translation` channel is corrected so it lands exactly at its simulated
     target, computed via:
     `anchorWorld = ComputeBoneWorldMatrix(..., rootBoneIndex)` (read-only) →
     `desiredLocalPoint = anchorWorld.TryInverse(...).TransformPoint(target)` →
     `localBindOffset = bone.position - anchorBone.position` →
     `pose[boneIndex].translation = desiredLocalPoint - localBindOffset`.
     `pose[boneIndex].rotation` is left untouched by this branch (a later
     iteration can still rotate this same bone if it is itself some other
     joint's tree-parent — translation and rotation are independent
     `BoneLocalOffset` channels).
   - **`parentJoint >= 0`** (this joint's tree-parent is another chain
     joint, never the anchor): left byte-for-byte unchanged — still rotates
     the parent JOINT bone exactly as before.
   - This directly closes the reported "whole body looks
     ragdoll-simulated" defect: any number of accessory joints sharing one
     real, load-bearing anchor bone (e.g. MMD's `下半身`) as their direct
     tree-parent now each independently land at their own simulated target
     by writing to their OWN distinct bone index, with zero shared mutable
     state left to fight over — and the anchor's rotation (hence the real
     legs' FK pose) is never perturbed at all.

2. **`src/Physics/BoneChainPhysicsResolver.h`** — rewrote the file's header
   doc comment to describe BOTH branches (previously it only ever described
   the old, single "always rotate the parent" behavior): kept the existing
   "IMPORTANT DESIGN NOTE" (a bone's own rotation can never move its own
   position), then documented the new anchor-rooted translation-based
   branch first, followed by the unchanged interior rotate-parent branch
   (retitled to make clear it only applies when the tree-parent is another
   chain joint, never the anchor).

3. **`tests/Physics/BoneChainPhysicsResolverTests.cpp`** — fully rewritten
   per the phase document's exact worked numbers:
   - `ProducedRotationLandsBoneAtRequestedSimulatedPosition` →
     renamed/rewritten as
     `ProducedTranslationLandsRootChildBoneAtRequestedSimulatedPositionWithoutTouchingTheAnchor`
     (asserts the anchor's rotation AND translation both stay untouched, and
     the joint's own translation carries the correction: `(1,-1,0)`).
   - `TargetAlreadyAlignedLeavesRotationUntouched` → renamed/rewritten as
     `TargetAtBindPositionLeavesTranslationNearZeroAndAnchorFullyUntouched`
     (target moved to the joint's exact bind position, asserting a
     near-zero translation correction).
   - `ThreeJointChainAppliesRootToTipInDependencyOrder` — rewritten to
     assert jointA gets a nonzero translation (anchor-rooted) AND a nonzero
     rotation (from the second, interior iteration aiming jointB), while the
     root/anchor stays fully `Identity()`/zero.
   - `OutOfRangeJointBoneIndexIsSkippedGracefully` /
     `OutOfRangeRootBoneIndexIsSkippedGracefully` — kept, with an added
     translation-untouched assertion alongside the existing rotation
     assertion.
   - `BranchingTreeRotatesTheSharedParentTowardBothChildrenIndependently` →
     renamed/rewritten as
     `RootLevelHubTranslatesEachChildIndependentlyWithoutOverwritingSiblingsOrTouchingTheSharedAnchor`
     — now asserts BOTH joints land at their own distinct targets in the
     same call (previously documented as a "last write wins" known
     limitation; now proven fixed) and the shared anchor stays fully
     untouched.
   - **New test**: `AnchorThatIsAlsoARealBodyAncestorNeverMovesRegardlessOfHubSize`
     — the direct, minimal encoding of the reported bug: an anchor bone with
     a real, non-participating "leg" descendant plus five independent
     accessory joints hubbed directly off the same anchor. Asserts the
     anchor's pose entry stays fully untouched, the leg's world position is
     bit-for-bit identical before/after the physics pass, and every
     accessory lands at its own independent target.
   - `MismatchedParentJointIndexSizeIsIgnoredGracefully` — kept unchanged
     (already exercises the unaffected top-of-function early-return path).
   - Rewrote the three prose comment blocks living outside individual test
     bodies (top-of-file header, the comment above the first test, and the
     inline setup comment inside the three-joint-chain test) so none of them
     still describe the old, buggy "always rotate the anchor" behavior —
     closing the exact kind of documentation/implementation mismatch this
     whole campaign exists to fix, this time inside the test file itself.

## Verification performed

- **Fast compile check only**, per this task's workflow rules (no full
  build/regression run yet):
  - `cmake --build build --target CMakeFiles/gte_core.dir/src/Physics/BoneChainPhysicsResolver.cpp.obj`
    → compiled cleanly, zero warnings/errors.
  - `cmake --build build --target tests/CMakeFiles/GreatTamanaEngineTests.dir/Physics/BoneChainPhysicsResolverTests.cpp.obj`
    → compiled cleanly, zero warnings/errors.
  - `cmake --build build --target gte_core` → relinked `libgte_core.a`
    successfully (this also incidentally recompiled
    `src/Game/Physics/PhysicsSystem.cpp`, an existing consumer of this same
    header, which compiled cleanly against the new
    `BoneChainPhysicsResolver.h` doc-comment-only header change).
- Did **not** run the full test suite or a full engine build/link — per this
  task's explicit "No Full Build" rule for this phase. `libgte_core.a` was
  relinked (not the test executable or `GreatTamanaEngine.exe`), confirming
  the new object file links without missing-symbol errors, but the actual
  rewritten test assertions have not yet been executed against a real test
  binary. That full verification is Phase 3's own explicit job (full-pipeline
  regression coverage) — see `PHASE3_END_TO_END_REGRESSION_COVERAGE_AND_TEST_SUITE_RECONCILIATION.md`.

## Scope discipline (What Was NOT Done, matching the phase document exactly)

- Did not touch the `parentJoint >= 0` branch's math at all — zero behavior
  change for interior chain bones aiming their own descendants.
- Did not add any new public function/parameter to
  `ApplyDynamicChainPhysicsToPose()` — its signature is unchanged.
- Did not touch `DynamicChainDetection.cpp`'s anchor-selection/chain-assembly
  algorithm, `DynamicChainSolver.cpp`, `ChainConstraints.cpp`,
  `VerletIntegration.cpp`, `WindField.cpp`, or
  `Game/Physics/PhysicsSystem.cpp`'s own call sites — exactly as scoped by
  `PHASE0_MASTER_STRATEGY.md`'s "What We Will NOT Do."
- Did not attempt to fix the interior (non-anchor) "last write wins" hub
  limitation `DynamicChainDetection.h` already documents as an accepted,
  separate, out-of-scope limitation.

## Next steps

Proceed to **Phase 2**
(`PHASE2_CONTRACT_AND_DOCUMENTATION_ALIGNMENT.md`) — update the doc comments
in `src/Physics/DynamicChainDefinition.h` (`rootBoneIndex`) and
`src/Physics/DynamicChainDetection.h` (the "Known Limitation" note) so they
accurately describe this phase's new, fixed behavior.
