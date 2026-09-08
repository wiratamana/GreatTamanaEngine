# PHASE4 Completion Report — Editor Visibility for Group/Mask Filtering and Joint Radius

Campaign: `task_manager/verlet-integration-10/` (parent: `PHASE0_MASTER_STRATEGY.md`)
Phase document executed: `PHASE4_EDITOR_VISIBILITY_FOR_GROUP_AND_RADIUS.md` (v2)
Date: 2026-09-08

## Summary

Implemented PHASE4 of the verlet-integration-10 campaign exactly as specified
in the phase document (v2). This phase changes **zero simulation behavior** —
it only updates the Editor's existing Inspector/Bone-Viewer surfaces so they
accurately describe the collision behavior PHASE1 (PMX collision-group/layer
filtering) and PHASE2 (joint's own rigid-body shape as collision radius)
already implemented, instead of silently going stale now that "collision"
no longer means "every joint vs. every collider, unconditionally."

Before this phase, the Inspector claimed a chain "Collides against every
auto-detected Static rigid body for this model" — no longer true once PHASE1's
PMX collision-group/mask rules can legitimately exclude some colliders from a
specific chain. The Bone Viewer's tree pane and 3D overlay had the same
staleness. All of that is now fixed, without introducing any new UI feature.

Every source file/line cited in the phase document was re-confirmed against
the live tree before editing (per this campaign's own "confirm every quoted
line number before editing" convention) and matched exactly — including the
one call site (`BoneViewerWindow.cpp`, `RigidBodyEntry{...}`) that had a
demonstrated compile-break history in this phase's own v1.

## Files Changed

### Production code

- `src/Editor/Panels/InspectorPanel.cpp`
  - Added a new, anonymous-namespace, Editor-only helper,
    `CountCollidersReachableByChain()` — an independent re-derivation of
    `DynamicChainSolver.cpp`'s group/mask AND-test (never a call into that
    `static` function, per this campaign's own "never call a production
    static function from another translation unit" rule), with `group`
    masked (`& 0x0Fu`) before use as a shift amount for the same
    undefined-behavior-safety reason PHASE1's `GroupBit()` requires.
  - **Both** existing "Enable Collision" call sites (the single-part
    "Verlet Joint" Model-Part Inspector, and the per-entity "Dynamic Chain
    Physics" per-chain section) now report "Collides against N of M
    auto-detected Static rigid-body collider(s)..." instead of the old,
    now-inaccurate "every ... rigid body", plus a new amber warning when a
    chain's own PMX group/mask makes every detected collider unreachable
    (distinct from the pre-existing "model has no colliders at all"
    warning).
  - Both call sites also gained a new read-only
    `DragFloat("Collision Radius (from PMX rigid body shape)", ...)`
    readout (wrapped in `BeginDisabled()`/`EndDisabled()`, mirroring the
    existing `restLength` readout's own established read-only convention)
    surfacing PHASE2's per-joint `DynamicJointSettings::collisionRadius`.
- `src/Editor/BoneViewerWindow.h`
  - `RigidBodyEntry` gained a new trailing field, `collisionGroupMask`
    (`std::uint16_t`, default `0xFFFF`), appended strictly AFTER
    `motionType` (the struct's true last field before this campaign) — never
    inserted between `group` and `motionType`, the exact placement this
    phase document's own v1→v2 revision exists to get right (an earlier,
    now-superseded v1 plan would have forced an unrelated `enum class`
    value into this slot, a guaranteed compile error).
- `src/Editor/BoneViewerWindow.cpp`
  - The one real `RigidBodyEntry{...}` construction call site (confirmed,
    still the *only* one in the repository via a fresh `RigidBodyEntry{`
    sweep) updated with a 9th trailing positional argument,
    `body.collisionGroupMask`.
  - The Verlet tree pane's per-chain "Collision: enabled" line now also
    states "subject to PMX collision-group/layer rules".
  - The 3D-viewport Verlet overlay's per-model Static-collider wireframe
    pass now computes, once per model, the union of every collider
    reachable by at least one collision-enabled chain
    (`colliderIsReachableByAnyChain`, matching PHASE1's own group/mask
    AND-test, `group` masked the same safe way), matches each
    `RigidBodyEntry` back to its `ModelColliderDefinition` via the combined
    `(boneIndex, shape, shapeSize)` key (not `boneIndex` alone — resolves
    the rare-but-valid ambiguity of two Static bodies on the same bone),
    and draws an unreachable collider at a visibly dimmer alpha
    (`IM_COL32(255, 90, 170, 30)` vs. the existing `90`). A body with no
    matching `ModelColliderDefinition` at all (should not normally happen,
    never assumed blindly) is drawn at the original, un-dimmed alpha —
    "no PHASE1 data available" is never confused with "PHASE1 confirmed
    unreachable."

### Tests

No new test files this phase (this is a pure Editor-surface/text-accuracy
phase, matching the phase document's own scope) — `tests/CMakeLists.txt`
needed no changes.

## Verification

- **Line-number/identifier re-confirmation (Step 4 of the phase document):**
  every quoted call site (`InspectorPanel.cpp`'s two "Enable Collision"
  blocks and its `restLength` readout precedent; `BoneViewerWindow.h`'s
  `RigidBodyEntry` field list/order; `BoneViewerWindow.cpp`'s single
  construction call site, tree-pane line, and 3D-overlay wireframe loop)
  was re-read directly from the live files before editing and matched the
  phase document's own quotations exactly — including the real local
  variable name `verletModel` (confirmed, not a member, no `m_` prefix) and
  the real field order `name, translate, rotateRadians, shape, shapeSize,
  boneIndex, group, motionType` (8 fields, positional aggregate-init).
- **`RigidBodyEntry{` sweep:** re-ran a repository-wide search for
  `RigidBodyEntry{` (and the space variant) immediately before editing the
  struct — confirmed exactly one construction call site exists, matching
  the phase document's own explicit instruction to re-verify this
  specific point given its demonstrated v1 compile-break history.
- **Fast compile check**, per this phase's workflow instructions (no full
  build/regression yet — that's PHASE5):
  - `cmake --build build --target gte_core` — succeeds, zero errors/warnings
    (`InspectorPanel.cpp` and `BoneViewerWindow.cpp` both rebuilt cleanly).
  - `cmake --build build --target GreatTamanaEngineTests` — succeeds, links
    cleanly (no new test files this phase, so this mainly confirms nothing
    else in the Editor-dependent link graph broke).
- Did **not** run `ctest`/the full regression suite (deferred to PHASE5 per
  the task instructions).

## Notes carried forward for PHASE5

- PHASE2's completion report already flagged a pre-existing, PHASE1-introduced
  test defect
  (`DynamicChainSolverCollisionGroupFilterTests.JointAndColliderInDifferentGroupsWithNoOverlapDoNotCollide`)
  believed to fail for a geometry-selection reason unrelated to PHASE1's own
  filtering logic. This phase did not touch that file and did not run
  `ctest`, so this remains exactly as flagged for PHASE5's own end-to-end
  regression pass to confirm/fix.
- This phase's own Editor-only re-derivations
  (`CountCollidersReachableByChain()` in `InspectorPanel.cpp`, and the
  reachability-union block in `BoneViewerWindow.cpp`) are deliberately
  independent, duplicated implementations of the same group/mask AND-test
  `DynamicChainSolver.cpp`'s own (production, `static`) filter uses — per
  this campaign's own established convention, not an oversight. If PHASE1's
  own filter semantics ever change, these two Editor-only copies must be
  updated in lockstep (no shared/exported function links them together
  today).

## What's Next

PHASE5 (`PHASE5_END_TO_END_REGRESSION_AND_BUILD_REGISTRATION.md`) adds the
end-to-end regression tests proving group/mask filtering, joint-radius
inflation, and default-on collision all work correctly together through the
full `PhysicsSystem::Update()` pipeline, performs the final
`CMakeLists.txt`/documentation audit sweep, and — per its own instructions —
is where a full build/regression (`ctest`) is expected to actually run.
