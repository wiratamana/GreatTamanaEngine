# PHASE1 — Selection: Model-Part Selection Foundation — Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE1_SELECTION_MODEL_PART_FOUNDATION.md` (v2) in full.

## What was done

Extended `src/Editor/Selection.h`/`.cpp` with a third `InspectorSelectionKind`
value, `ModelPart`, plus a new free `ModelPartKind { Bone, RigidBody, Joint }`
enum — the pure, Tier-1-tested data-model foundation the rest of this
campaign (Phase 2's `ModelRigCache`, Phase 3's generalized `BoneViewerWindow`,
Phase 4's Inspector "Model Part" section) plugs into. No ImGui/
`BoneViewerWindow`/`InspectorPanel` code was touched in this phase, exactly as
the strategy document requires.

Concretely, per the phase document's Step 3:

- **`Selection.h`**: added `ModelPartKind` (free enum, unqualified, same
  convention as `GizmoOperation`/`InspectorSelectionKind`), appended
  `ModelPart` to `InspectorSelectionKind` (append-only, existing three values
  untouched), added three new private fields
  (`m_modelPartEntity`/`m_modelPartKind`/`m_modelPartIndex`, defaulting to
  `kInvalidEntity`/`ModelPartKind::Bone`/`-1`), and added the public surface:
  `SelectModelPart(Entity, ModelPartKind, int)`,
  `SelectedModelPartEntity()`/`SelectedModelPartKind()`/`SelectedModelPartIndex()`
  accessors, `IsModelPartSelected(Entity, ModelPartKind, int)` (all three
  fields must match, mirroring `IsEntitySelected()`/`IsAssetSelected()`), and
  the v2-required `ClearModelPartIfEntity(Entity)` mutator (mirrors
  `ClearAssetIfPath()`'s "clear only if it currently matches, revert `Kind()`
  to `None` only if it was the one on top" shape — this is what will let
  Phase 3's `BoneViewerWindow` safely drop a stale Model-Part selection
  whenever the entity's underlying model data genuinely reloads, closing the
  gap the v1→v2 self-audit found).
- **`Selection.cpp`**: implemented `SelectModelPart()`, `IsModelPartSelected()`,
  and `ClearModelPartIfEntity()` exactly per the phase document's Step 3.3,
  and extended `Clear()` to also reset the three new fields to their
  documented defaults. `SelectEntity()`/`SelectAsset()` needed no changes at
  all — verified directly by the new cross-kind tests below.
- **`tests/Editor/SelectionTests.cpp`**: added every test case the phase
  document's Step 3.4 calls for (12 new `TEST(SelectionTest, ...)` cases,
  bringing the file from 10 to 22 cases total), including all three v2
  additions for `ClearModelPartIfEntity()`. Every pre-existing test was left
  behaviorally unchanged (verified byte-for-byte identical assertions after
  the edit, aside from restoring the file's original line ordering that an
  intermediate `edit_line` step briefly disturbed and was corrected before
  building).

## Verification

- **Fast compile check** (per this campaign's workflow rules — no full
  build): `cmake --build build --target gte_core` — succeeded, only
  `Selection.cpp` and its usual `src/Editor/` neighbors rebuilt, zero
  warnings/errors.
- `cmake --build build --target GreatTamanaEngineTests` — succeeded
  (rebuilt `Editor/SelectionTests.cpp.obj` + relinked the test binary).
- Targeted test run (not the full suite, per the "no full regression yet"
  workflow rule): `GreatTamanaEngineTests.exe --gtest_filter=SelectionTest.*`
  — **22/22 passed**, 0 failures, including every pre-existing case
  (proving this was a behavior-preserving extension for `Entity`/`Asset`,
  not just "new tests pass") and every new `ModelPart`/`ClearModelPartIfEntity`
  case from the phase document's Step 3.4.

## Notes for the next phase (Phase 2 — `ModelRigCache`)

- `Selection`'s new API surface is exactly what the phase document specified
  — no deviations. Phase 2 does not depend on this phase's internals at all
  (`ModelRigCache` is a standalone cache class), but Phase 3/4 both depend on
  this exact, now-compiling-and-tested API being in place before they start.
- Nothing under `src/Editor/BoneViewerWindow.*` or
  `src/Editor/Panels/InspectorPanel.cpp` was touched — `Selection`'s new
  fields are currently unused by any production call site, exactly as
  intended for a foundation-only phase. `Selection`'s class-level doc comment
  was updated to mention the `ModelPart` extension has already happened, so a
  future reader doesn't need to cross-reference this campaign's task manager
  folder just to understand the class's own history.
- One pre-existing, unrelated `git status` observation for whoever picks up
  Phase 2: the working tree already had a set of untracked/deleted files
  under `task_manager/separate-anim-physics-system-9/` →
  `task_manager/verlet-integration-1/` (a folder rename from an earlier,
  unrelated session) sitting in the working tree before this phase's own
  changes were made. This report's own commit stages ONLY the files this
  phase actually touched (`src/Editor/Selection.h/.cpp`,
  `tests/Editor/SelectionTests.cpp`, and this report) — it deliberately does
  not touch or commit that unrelated rename, so as not to conflate it with
  this campaign's own history.
