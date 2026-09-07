# PHASE2 — COMPLETION REPORT: Rigid Body `group` Field + Pure Group/Branch Selection Algorithms

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE2_RIGID_BODY_GROUP_FIELD_AND_ADJACENCY_ALGORITHMS.md` exactly as
written (no deviations from the phase document were needed — every file/line
citation in that document was re-verified against the live source tree
before editing and matched byte-for-byte, including Phase 1's own prior
completion, confirmed already landed on this branch).

## What was done

1. **New file `src/Editor/RigidBodyGroupSelection.h`** — a pure,
   dependency-free (no ImGui/SDL/Vulkan/engine-type dependency at all, only
   `<cstddef>`/`<cstdint>`/`<vector>`) module declaring:
   - `RigidBodyJointEdge` — the minimal `{ rigidBodyAIndex, rigidBodyBIndex }`
     shape `BuildRigidBodyAdjacency()` needs.
   - `BuildRigidBodyAdjacency(edges, rigidBodyCount)` — turns a flat list of
     joint rigid-body-pairs into a genuine per-rigid-body adjacency list,
     defensively dropping out-of-range/self/negative edges and de-duplicating
     repeated pairs.
   - `SelectRigidBodiesByGroup(groups, seedIndex)` — a flat property
     comparison returning every rigid-body index (including the seed) sharing
     the seed's own PMX collision `group` value.
   - `SelectRigidBodyBranch(adjacency, seedIndex)` — a graph walk (explicit
     stack, depth-first order — deliberately NOT part of the function's own
     contract, only the final sorted result is) collecting every reachable
     rigid body except any "branch" node (degree >= 3), matching the story's
     own `+--A--+`/`|     |` diagram exactly; a seed that is itself a branch
     node returns just `{ seedIndex }`.
2. **New file `src/Editor/RigidBodyGroupSelection.cpp`** — implements all
   three functions exactly per the phase document's Step 3.2.
3. **`src/Editor/BoneViewerWindow.h`**:
   - Added `#include "RigidBodyGroupSelection.h"`.
   - `RigidBodyEntry` gained a `std::uint8_t group = 0;` field (PMX collision
     group 0-15), populated from `RigidBody::group`.
   - Added a new private member, `std::vector<std::vector<std::int32_t>>
     m_rigidBodyAdjacency;`, and a new private method declaration,
     `void RebuildRigidBodyAdjacencyIndex();`, placed right alongside
     `RebuildBoneHierarchyIndex()`'s own declaration.
4. **`src/Editor/BoneViewerWindow.cpp`**:
   - `EnsureDataLoaded()`'s `RigidBodyEntry` construction now also copies
     `body.group` across.
   - `RebuildRigidBodyAdjacencyIndex()` is now called right after
     `RebuildBoneHierarchyIndex()`, once per (re)load.
   - Added the new method's definition (builds a `RigidBodyJointEdge` list
     from `m_joints`, then calls `BuildRigidBodyAdjacency()`), placed right
     after `RebuildBoneHierarchyIndex()`'s own definition.
   - `Reset()` now also clears `m_rigidBodyAdjacency`, alongside every other
     per-load cached derived structure.
5. **Root `CMakeLists.txt`** — added `src/Editor/RigidBodyGroupSelection.h`/
   `.cpp` to the `GTE_ENABLE_PROJECT_PANEL` source block, alongside
   `RigidBodyWireframe.h/.cpp`.
6. **`tests/Editor/RigidBodySelectionAlgorithmsTests.cpp`** (new file) — 12
   Tier-1 tests covering `BuildRigidBodyAdjacency()` (bidirectional
   neighbor lists, dropping out-of-range/self/duplicate edges, correct
   sizing on an empty edge list), `SelectRigidBodiesByGroup()` (matching
   group membership including the seed, a unique-group seed returning only
   itself, out-of-range seed returning empty), and `SelectRigidBodyBranch()`
   (the story's own `+--A--+` topology transcribed to indices and verified
   from all three interior seeds, a branch-node seed returning only itself,
   a closed loop with no branches selecting the whole loop, an open-ended
   chain selecting to both leaf ends, no-joints-at-all selecting only the
   seed, and out-of-range seed returning empty).
7. **`tests/CMakeLists.txt`** — registered the new test file in the
   `GTE_ENABLE_PROJECT_PANEL` test-source block, alongside
   `RigidBodyWireframeTests.cpp`.

One self-correction made while editing: `edit_line`'s insertion at
`BoneViewerWindow.cpp`'s `EnsureDataLoaded()` initially left a duplicated
`RebuildBoneHierarchyIndex();` call line (the tool's own boundary
auto-dedup did not catch it since the duplicate landed one line further
in than the checked boundary) — caught immediately on the follow-up
read and fixed before building.

## Verification

- **Compile check**: `cmake --build build --target gte_core` — succeeds,
  rebuilding `RigidBodyGroupSelection.cpp`, `BoneViewerWindow.cpp`,
  `Panels/InspectorPanel.cpp`, and `ImGuiEditorLayer.cpp` with zero errors.
- **Compile check**: `cmake --build build --target GreatTamanaEngineTests`
  — succeeds (only `Editor/RigidBodySelectionAlgorithmsTests.cpp` needed
  compiling as a new translation unit).
- **Targeted test run**:
  `GreatTamanaEngineTests.exe --gtest_filter=RigidBodyGroupSelectionTest.*`
  — **12/12 tests pass**, 0 failures.
- **Regression check on the shared `GTE_ENABLE_PROJECT_PANEL` test-source
  block**:
  `GreatTamanaEngineTests.exe --gtest_filter=RigidBodyWireframeTest.*:ModelRigCacheTest.*:SelectionTest.*`
  — **47/47 tests pass** (8 `RigidBodyWireframeTest` + 7 `ModelRigCacheTest`
  + 32 `SelectionTest`, the last of which is Phase 1's own suite, confirmed
  still fully green on top of this phase's changes), 0 failures.
- Per the workflow rules for this task, a full build/full regression
  (`ctest`) was deliberately NOT run — only this fast, targeted
  compile-and-test check, as instructed.

## Notes for the next phase

- Phase 3 (`PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md`)
  now has everything it needs: `Selection::SelectModelParts()`/
  `ToggleModelPartInSelection()` (Phase 1) plus
  `RigidBodyGroupSelection.h`'s `SelectRigidBodiesByGroup()`/
  `SelectRigidBodyBranch()` and `BoneViewerWindow::m_rigidBodyAdjacency`
  (this phase) are all in place and compiling.
- No visible behavior change yet — this phase is data plumbing + pure
  algorithms only; nothing calls `SelectRigidBodiesByGroup()`/
  `SelectRigidBodyBranch()` from `BoneViewerWindow.cpp` until Phase 3 wires
  the two new toolbar buttons.
- `m_rigidBodyAdjacency` is rebuilt once per (re)load (right alongside
  `m_boneChildren`/`m_rootBoneIndices`), so Phase 3's "Select All (Branch)"
  button click handler can hand it straight to `SelectRigidBodyBranch()`
  with no per-click recomputation needed.

## Files touched

- `src/Editor/RigidBodyGroupSelection.h` (new)
- `src/Editor/RigidBodyGroupSelection.cpp` (new)
- `src/Editor/BoneViewerWindow.h`
- `src/Editor/BoneViewerWindow.cpp`
- `CMakeLists.txt`
- `tests/Editor/RigidBodySelectionAlgorithmsTests.cpp` (new)
- `tests/CMakeLists.txt`
- `task_manager/verlet-integration-4/PHASE2_COMPLETION_REPORT.md` (this file)
