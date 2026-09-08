# PHASE2 — COMPLETION REPORT: Contract & Documentation Alignment

Status: **DONE**. Implements exactly the plan in
`PHASE2_CONTRACT_AND_DOCUMENTATION_ALIGNMENT.md`.

## What was done

Purely doc-comment edits in two production headers — zero behavior change,
zero test changes, exactly as scoped by the phase document.

1. **`src/Physics/DynamicChainDefinition.h`** — the doc comment directly
   above `struct DynamicChainDefinition` (previously a single-sentence
   parenthetical: *"`rootBoneIndex` is NOT simulated (it is the pinned
   anchor, always taken directly from the animated FK pose every step)"*)
   was expanded to:
   - State the guarantee explicitly and name the enforcing code
     (`Physics/BoneChainPhysicsResolver.cpp`'s
     `ApplyDynamicChainPhysicsToPose()`, `task_manager/verlet-integration-8`,
     Phase 1) — `rootBoneIndex` is now documented as NEVER written by
     physics, not its rotation, not its translation, for any joint, ever.
   - Explain WHY this matters: `rootBoneIndex` is frequently a REAL, SHARED,
     load-bearing skeleton bone (e.g. MMD's own 下半身/"lower body," also the
     ordinary FK ancestor of the character's legs), not a dedicated
     physics-only "hair root" bone.
   - Note that before `verlet-integration-8` this guarantee was actually
     violated for any joint whose tree-parent was the anchor directly,
     cross-referencing the original investigation
     (`task_manager/verlet-integration-7/INVESTIGATION_FULL_BODY_DISTORTION_ON_TRANSFORM_DRAG.md`)
     — so a future reader understands this comment now describes the
     CORRECTED, enforced behavior, not an assumption that was always true.
   - The rest of the comment block (the `jointBoneIndices` explanation and
     the `verlet-integration-6` Phase 1 explicit-tree rationale) was kept
     unchanged, exactly as instructed.

2. **`src/Physics/DynamicChainDetection.h`** — the "Known Limitation"
   paragraph at the end of the file was replaced with a version that
   distinguishes the two cases instead of describing them as one blanket,
   still-accepted limitation:
   - **Narrowed, still-accepted case**: two or more joints sharing a
     NON-ANCHOR interior chain bone as their parent still exhibit
     "last-processed child wins" — confined entirely to the chain's own
     accessory bones, so it cannot cause the "whole body looks
     ragdoll-simulated" symptom, and remains a purely cosmetic, unfixed
     limitation (out of scope for this whole campaign).
   - **Fixed case, called out explicitly**: two or more joints sharing the
     chain's own ANCHOR bone (`rootBoneIndex`) as their direct tree-parent —
     exactly the shape of the reported model in the
     `verlet-integration-7` investigation — is now FIXED as of
     `verlet-integration-8`, Phase 1: each such joint corrects its own local
     translation instead of rotating the shared anchor, landing independently
     at its own target with the anchor (and every real body bone descending
     from it) never perturbed.
   - Both paragraphs cross-reference `Physics/BoneChainPhysicsResolver.h`'s
     own header comment (already rewritten by Phase 1) for the full
     derivation.

3. **`src/Physics/BoneChainPhysicsResolver.h`** — confirmed, per the phase
   document's own Step 2 item 3, that this file's header comment was already
   rewritten as part of Phase 1 itself. No further edit made here in Phase 2
   — verified by re-reading the file that it already documents both branches
   correctly.

## Verification performed

- **Fast compile check only**, per this task's workflow rules (no full
  build/regression test yet): ran `cmake --build build --target gte_core`.
  This recompiled every translation unit that includes either edited header
  — `src/Physics/DynamicChainDefinition.cpp`, `DynamicChainSolver.cpp`,
  `BoneChainPhysicsResolver.cpp`, `DynamicChainDetection.cpp`,
  `src/Game/Physics/PhysicsSystem.cpp`, plus several Editor files
  (`RenderPasses.cpp`, `HierarchyPanel.cpp`, `Game.cpp`,
  `InspectorPanel.cpp`, `ScenePanel.cpp`, `JobsPanel.cpp`, `Application.cpp`,
  `BoneViewerWindow.cpp`, `ImGuiEditorLayer.cpp`) — all 15 objects compiled
  cleanly with zero warnings/errors, and `libgte_core.a` relinked
  successfully.
- Did **not** run the full test suite or a full engine build/link beyond
  `gte_core` itself, and did **not** touch any test file — exactly matching
  this phase's own "zero behavior change, zero test changes required" scope.
  Full regression coverage is Phase 3's own explicit job — see
  `PHASE3_END_TO_END_REGRESSION_COVERAGE_AND_TEST_SUITE_RECONCILIATION.md`.

## Scope discipline (What Was NOT Done, matching the phase document exactly)

- Did not touch `DynamicChainDetection.cpp`'s Step A-H algorithm comment
  block — chain assembly itself is unaffected by Phase 1; only the ONE
  "Known Limitation" paragraph in the header changed.
- Did not touch any test file in this phase — no assertion anywhere depends
  on comment text.
- Did not change any production behavior — both edits are comment-only, in
  header files, with no code/logic touched at all.

## Next steps

Proceed to **Phase 3**
(`PHASE3_END_TO_END_REGRESSION_COVERAGE_AND_TEST_SUITE_RECONCILIATION.md`) —
add the new full-pipeline `PhysicsSystemAnchorRigidityRegressionTests.cpp`
regression test file, register it in `tests/CMakeLists.txt`, apply the
comment reconciliation pass to
`PhysicsSystemFreezeAndCulpritFTests.cpp`, and build + run the full test
suite to confirm every other named test file still passes.
