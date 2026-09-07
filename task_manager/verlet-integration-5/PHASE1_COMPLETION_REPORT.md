# PHASE1 — COMPLETION REPORT: Chain Lookup and Selection Foundation

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE1_CHAIN_LOOKUP_AND_SELECTION_FOUNDATION.md` exactly as written — the
phase document's own code excerpts (the `ModelPartKind::Verlet` enum value,
`DynamicChainJointLocation`/`FindDynamicChainJointByBoneIndex()`'s declaration
and implementation, and the test fixture shapes) were re-verified against the
live source tree (`src/Editor/Selection.h`, `src/Physics/
DynamicChainDefinition.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`,
`tests/Physics/DynamicChainDetectionTests.cpp` for style) before editing, and
matched.

## What was done

1. **`src/Editor/Selection.h`** — added `ModelPartKind::Verlet` as the 4th
   enum value, immediately after the existing `Joint`, with the exact doc
   comment the phase document specifies (explaining that this kind's
   `partIndex` is the joint's own skeleton bone index, not a flattened
   per-chain joint counter, and pointing at
   `FindDynamicChainJointByBoneIndex()` for resolving it back to a chain).
   Confirmed `Selection.cpp` needs no change — nothing there switches on
   `ModelPartKind` — and every existing `Selection` method already operates
   generically on whatever `ModelPartKind` value is passed to it, so no other
   body changed. Per this phase's own explicit scope, `BoneViewerWindow.cpp`
   and `Panels/InspectorPanel.cpp` were deliberately left untouched (their own
   exhaustive branches needing a 4th case are Phase 2/3/4's job).

2. **`src/Physics/DynamicChainDefinition.h`** — added, at the bottom of the
   file (still inside `namespace gte`):
   - `DynamicChainJointLocation` — a small plain struct
     (`chainIndex`/`jointIndexInChain`, both defaulting to `-1`) plus its
     `IsValid()` helper.
   - `FindDynamicChainJointByBoneIndex(const std::vector<DynamicChainDefinition>&, std::int32_t)`
     — declared exactly per the phase document, with its full doc comment
     (no-match contract, at-most-one-match guarantee, and why this lives here
     rather than being hand-rolled independently in both
     `BoneViewerWindow.cpp` and `InspectorPanel.cpp`).

3. **`src/Physics/DynamicChainDefinition.cpp`** (new file) — this header was
   previously pure-data/header-only with no matching `.cpp`; created one
   containing exactly `FindDynamicChainJointByBoneIndex()`'s implementation
   (a plain linear scan over `chains`, returning immediately on the first
   match, negative-input short-circuited up front) verbatim per the phase
   document.

4. **`CMakeLists.txt`** — added `src/Physics/DynamicChainDefinition.cpp`
   immediately after the existing `src/Physics/DynamicChainDefinition.h`
   entry in `gte_core`'s source list.

5. **`tests/Physics/DynamicChainDefinitionTests.cpp`** (new file) — covers
   every case the phase document's Step 3.4 lists, using a two-chain fixture
   (chain 0: `jointBoneIndices = {2, 3, 4}`; chain 1: `jointBoneIndices =
   {7, 8}`):
   - A mid-chain bone index (3) resolves to `{chainIndex=0,
     jointIndexInChain=1}`.
   - A second-chain bone index (8) resolves to `{chainIndex=1,
     jointIndexInChain=1}`.
   - A bone index that is not a joint of any chain (5, sitting between the
     two chains) returns invalid, and so does each chain's own
     `rootBoneIndex` (1 and 6) — a root is never itself a joint.
   - A negative bone index (-1) returns invalid immediately.
   - An empty `chains` vector returns invalid for any input (0, 42, -1).
   - The result is never partially valid — a found location has both fields
     `>= 0`, an unfound one has both fields exactly `-1`.
   6 `TEST()` cases total, all passing.

6. **`tests/CMakeLists.txt`** — added a descriptive paragraph for the new
   test file to the header's "Test taxonomy" comment block, immediately
   after the existing `Physics/DynamicChainDetectionTests.cpp` entry, and
   added `Physics/DynamicChainDefinitionTests.cpp` to the actual
   `GTE_TEST_SOURCES` list right next to `Physics/DynamicChainDetectionTests.cpp`.

## Verification

- **Compile check**: `cmake -S . -B build` (re-configure, picking up the two
  new/changed CMake source lists) — succeeds, no fetch needed (every
  third-party dependency was already staged).
- **Compile check**: `cmake --build build --target gte_core` — succeeds,
  0 errors/warnings; `src/Physics/DynamicChainDefinition.cpp.obj` compiles as
  a new object alongside the rest of `src/Physics/`.
- **Compile check**: `cmake --build build --target GreatTamanaEngineTests` —
  succeeds, 0 errors/warnings; `Physics/DynamicChainDefinitionTests.cpp.obj`
  compiles and links cleanly.
- **Targeted test run** (not a full `ctest` regression pass, per this
  session's workflow rules): `GreatTamanaEngineTests.exe
  --gtest_filter=DynamicChainDefinitionTests.*` — all 6 new tests pass.
  `--gtest_filter=SelectionTest.*` — all 32 pre-existing `Selection` tests
  still pass unchanged, confirming the new `ModelPartKind::Verlet` enum value
  didn't disturb any existing behavior (as expected, since no `Selection`
  method body changed).
- Per the task workflow rules, no full build/full regression (`ctest`) was
  run — this phase's own scope is confined to `Selection.h`,
  `Physics/DynamicChainDefinition.h/.cpp`, `CMakeLists.txt`, and
  `tests/Physics/DynamicChainDefinitionTests.cpp` + `tests/CMakeLists.txt`,
  none of which touch ImGui/GPU-facing code at all.

## What was deliberately NOT done (per this phase's own scope)

- `BoneViewerWindow.h/.cpp` and `Panels/InspectorPanel.h/.cpp` were not
  touched — every exhaustive switch/ternary there that needs a 4th
  `Verlet` branch is Phase 2 (Bone Viewer tree/gizmo)/Phase 3 (Inspector
  single-part section)/Phase 4 (Inspector multi-selection summary + "Select
  All (Chain)")'s own job.
- No flattened "joint counter" index scheme was invented — bone index is the
  permanent `partIndex` scheme for `ModelPartKind::Verlet`, per Step 2's
  reasoning.
- `FindDynamicChainJointByBoneIndex()` does not search `rootBoneIndex`
  fields — a chain's root is never independently selectable as a Verlet
  joint.

## Campaign status

This closes out Phase 1 of the `verlet-integration-5` campaign
(`PHASE0_MASTER_STRATEGY.md`). `ModelPartKind::Verlet` and
`FindDynamicChainJointByBoneIndex()` now exist, are fully unit-tested, and are
ready for Phase 2 (`PHASE2_BONE_VIEWER_VERLET_MODE_TREE_AND_GIZMO.md`) to
build the actual Bone Viewer "Verlet" toolbar mode on top of.

## Files touched

- `src/Editor/Selection.h`
- `src/Physics/DynamicChainDefinition.h`
- `src/Physics/DynamicChainDefinition.cpp` (new)
- `CMakeLists.txt`
- `tests/Physics/DynamicChainDefinitionTests.cpp` (new)
- `tests/CMakeLists.txt`
- `task_manager/verlet-integration-5/PHASE1_COMPLETION_REPORT.md` (this file)
