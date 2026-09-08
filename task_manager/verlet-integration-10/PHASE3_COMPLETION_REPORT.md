# PHASE3 Completion Report — Collision Enabled ON by Default

Campaign: `task_manager/verlet-integration-10/` (parent: `PHASE0_MASTER_STRATEGY.md`)
Phase document executed: `PHASE3_COLLISION_ENABLED_ON_BY_DEFAULT.md` (v2)
Date: 2026-09-08

## Summary

Implemented PHASE3 of the verlet-integration-10 campaign exactly as specified
in the phase document: `DynamicChainDefinition::collisionEnabled`'s default
member-initializer now reads `true` instead of `false`, so every freshly
detected dynamic bone chain (hair/skirt/etc.) collides against its model's
own auto-detected PMX Static rigid-body colliders **immediately**, with no
manual "Enable Collision" checkbox click required in the Editor Inspector.
This directly closes the user's third and final request from this campaign's
own `PHASE0_MASTER_STRATEGY.md`: *"also by default, turn collision to ON."*
Because this phase runs strictly after PHASE1 (PMX collision group/layer
filtering) and PHASE2 (joint's own rigid-body shape as collision radius) —
both already implemented and compiling — the very first time collision
activates by default in this engine's history, it activates with the fully
PMX-faithful behavior the user asked for, not a partially-correct
intermediate state.

## Files Changed

### Production code
- `src/Physics/DynamicChainDefinition.h` — `DynamicChainDefinition::collisionEnabled`'s
  default member-initializer flipped from `false` to `true`; its doc comment
  rewritten to describe the new default, why it changed, how a human can still
  opt back out per chain via the Editor Inspector's existing checkbox, and a
  pointer to `PhysicsSystem.cpp`'s own `anyChainWantsCollision` guard for the
  accepted performance consequence (see below).
- `src/Game/Physics/PhysicsSystem.cpp` — updated the `anyChainWantsCollision`
  guard's own doc comment (previously said "an entity with collision disabled
  everywhere (today's default for every chain) pays zero extra
  `ComputeBoneWorldMatrix()` calls" — now explicitly states that, after this
  phase, the guard fires (i.e. collider resolution genuinely runs) for the
  overwhelming majority of models with at least one detected chain and at
  least one detected Static collider, an intentional/accepted per-frame cost
  of "collision on by default," not a regression. **No logic in this guard
  changed at all** — only its own comment's factual claim about what "default"
  means.

### Tests (fixed)
- `tests/Physics/DynamicChainSolverTests.cpp` — `CollidersAreIgnoredWhenCollisionEnabledIsFalse`
  no longer asserts the raw struct default (`ASSERT_FALSE(definition.collisionEnabled)`,
  which would now immediately fail); it explicitly sets
  `definition.collisionEnabled = false;` instead, since this test's own intent
  (verify the disabled-case no-op contract) is independent of whatever the
  struct's own default happens to be. This was confirmed to be the **only**
  place in the entire test suite that read a `DynamicChainDefinition`'s raw
  default `collisionEnabled` value without first explicitly assigning it —
  re-verified via a full `search_in_dir` sweep across `tests/` both before and
  after this edit (see "Verification" below).

### Doc comments confirmed to need NO change (per this phase's own analysis)
- `tests/Game/Physics/PhysicsSystemModelColliderResolutionTests.cpp` and
  `tests/Game/Physics/PhysicsSystemMultiShapeColliderTests.cpp` — every real
  `collisionEnabled` value in both files is set EXPLICITLY (via each file's
  own `RegisterAttachSeedPoseAndConfigureChain()`/`RegisterAttachAndConfigure()`
  helper, called with an explicit `true`/`false` literal at every call site),
  never inferred from the raw struct default — confirmed unaffected by this
  phase's default flip. Their own comments mentioning "the default" describe
  the value each test explicitly configured, not the struct's own default, so
  no edit was made (matching the phase document's own Step 2 analysis).
- `src/Editor/Panels/InspectorPanel.cpp`'s two "Enable Collision" checkbox
  doc-comment sites — re-confirmed neither one asserts a specific default
  value for `collisionEnabled` at all (they only describe what the checkbox
  replaces); no edit needed here, exactly as PHASE3's own Step 3.3 predicted.
  PHASE4 revisits these exact call sites for group/mask/radius visibility.

### Build registration
- No new test files were added this phase, so `tests/CMakeLists.txt` needed no
  changes.

## Verification

- **Test-suite audit (Step 2/3.4 of the phase document):** ran a full
  `search_in_dir` sweep for `collisionEnabled` across `tests/` before AND
  after making the fix. Confirmed exactly 5 files reference the symbol
  (`DynamicChainSolverTests.cpp`, `DynamicChainSolverCollisionGroupFilterTests.cpp`
  — a PHASE1 test file not mentioned in the original PHASE3 document since it
  did not exist at v1 authoring time, confirmed to already set
  `collisionEnabled` explicitly at every reference, no fix needed —
  `PhysicsSystemModelColliderResolutionTests.cpp`,
  `PhysicsSystemMultiShapeColliderTests.cpp`, and `tests/CMakeLists.txt`'s own
  taxonomy comment). Only the one test named in the phase document depended on
  the raw default.
- **Fast compile check**, per this phase's workflow instructions (no full
  build/regression yet — that's PHASE5):
  - `cmake --build build --target gte_core` — succeeds, zero errors/warnings.
  - `cmake --build build --target GreatTamanaEngineTests` — succeeds, all
    existing and new test files (including this phase's one edited test file)
    compile and link cleanly into `GreatTamanaEngineTests.exe`.
- Did **not** run `ctest`/the full regression suite (deferred to PHASE5 per
  the task instructions — PHASE5 is also where PHASE2's own flagged
  pre-existing `DynamicChainSolverCollisionGroupFilterTests` geometry-selection
  issue is addressed).

## Notes carried forward for PHASE5

- PHASE2's completion report already flagged a pre-existing, PHASE1-introduced
  test defect (`DynamicChainSolverCollisionGroupFilterTests.JointAndColliderInDifferentGroupsWithNoOverlapDoNotCollide`)
  believed to fail for a geometry-selection reason unrelated to PHASE1's own
  filtering logic. This phase did not touch that file and did not re-run
  `ctest`, so this remains exactly as flagged for PHASE5's own end-to-end
  regression pass to confirm/fix.
- Per PHASE0's Revision Notes finding #4 / this phase's own Step 4.1: expect
  `PhysicsSystem.cpp`'s `anyChainWantsCollision` guard to now fire (and its
  per-collider `ComputeBoneWorldMatrix()` resolution cost to now be paid) for
  the overwhelming majority of models with a detected chain and a detected
  Static collider — an intentional, accepted, and now explicitly documented
  consequence of this phase's own default flip, not a regression to chase.

## What's Next

PHASE4 (`PHASE4_EDITOR_VISIBILITY_FOR_GROUP_AND_RADIUS.md`) updates the
Inspector's and Bone Viewer's existing collision-related text/overlays so they
reflect PHASE1's group/mask filtering and PHASE2's per-joint effective
collision radius, instead of silently going stale now that the underlying
collision behavior changed — read that phase document next.
