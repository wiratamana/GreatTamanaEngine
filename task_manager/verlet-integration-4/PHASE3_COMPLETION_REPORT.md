# PHASE3 — COMPLETION REPORT: Bone Viewer "Select All (Group)"/"Select All (Branch)" Buttons + Ctrl-Click Toggle + Shift-Click Range Select

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md` (v2) exactly
as written — every file/line citation in that document was re-verified against
the live source tree before editing (Phase 1's `Selection::SelectModelParts()`/
`ToggleModelPartInSelection()` and Phase 2's `RigidBodyGroupSelection.h`/
`BoneViewerWindow::RigidBodyEntry::group`/`m_rigidBodyAdjacency` were both
already landed on this branch, confirmed via `git status` — working tree was
clean before this phase started) and matched, modulo trivial line-number
drift from Phase 1/2's own prior edits (the toolbar `Separator()`/click blocks
had shifted by a few lines versus the phase document's own citations, but the
surrounding code shape was identical).

## What was done

1. **New file `src/Editor/FlatListRangeSelection.h`** — a pure,
   dependency-free module (`<cstdint>`/`<vector>` only) declaring
   `BuildInclusiveIndexRange(anchorIndex, clickedIndex)`: the inclusive,
   ascending-sorted, order-independent index range spanning two indices —
   the pure logic behind a genuine Windows-Explorer/Unity-style Shift-click
   range select. A negative `anchorIndex` (no anchor recorded yet) degrades to
   `{ clickedIndex }` alone.
2. **New file `src/Editor/FlatListRangeSelection.cpp`** — implements it
   exactly per the phase document's Step 3.6.
3. **`src/Editor/BoneViewerWindow.h`**:
   - Added `#include "FlatListRangeSelection.h"`.
   - Added a new private member, `std::int32_t m_flatSelectionAnchorIndex = -1;`,
     alongside `m_rigidBodyAdjacency`, with a doc comment covering every reset
     point (data reload, view-mode switch) and why the Bone tree deliberately
     never uses it.
4. **`src/Editor/BoneViewerWindow.cpp`**:
   - `Reset()` and `EnsureDataLoaded()`'s "genuine reload starting" block both
     now also reset `m_flatSelectionAnchorIndex = -1`.
   - The View combo's callback now also resets `m_flatSelectionAnchorIndex`
     when the view mode changes.
   - Inserted the two new toolbar buttons ("Select All (Group)"/"Select All
     (Branch)"), gated on `m_viewMode == ModelPartKind::RigidBody &&
     !m_rigidBodies.empty()`, right after the toolbar's `Separator()` and
     before the part-list-pane section — `hasSeed` requires EXACTLY ONE
     currently-selected rigid body on this window's own `m_targetEntity`
     (the v2 bug fix: also checks `SelectedModelPartIndices().size() == 1`,
     not just "the lowest selected index is in range"), each button disabled
     otherwise with an inline hint showing how many are currently selected.
     Both buttons preview their real match count via a hover tooltip computed
     through the exact same Phase 2 functions the click handler itself calls;
     "Select All (Branch)" shows a distinct tooltip when the seed is itself a
     branch/junction (degree >= 3).
   - `RenderBoneTreeNode()`'s click block: Ctrl OR Shift both still call
     `ToggleModelPartInSelection()` (unchanged from pre-Phase-3 behavior, by
     design — a bone's raw array index has no meaningful linear range in an
     indented, collapsible tree).
   - `RenderFlatPartRow()`'s click block: Ctrl now TOGGLES exactly one row
     (`ToggleModelPartInSelection()`) while Shift performs a genuine
     contiguous RANGE select from `m_flatSelectionAnchorIndex` through the
     clicked row (`BuildInclusiveIndexRange()` feeding
     `Selection::SelectModelParts()`) — a real behavior change from the
     pre-Phase-3 "Shift also just toggles" convention. A plain click keeps
     replacing the whole selection with just the one row and moves the range
     anchor to it, same as a real Ctrl-click does.
   - The direct viewport-dot click (inside `Build()`): same Ctrl-toggle/
     Shift-range-select split as `RenderFlatPartRow()`, gated on
     `m_viewMode != ModelPartKind::Bone` (Bone mode keeps toggle-only for
     both modifiers). The pre-existing `const ImGuiIO& io = ImGui::GetIO();`
     declaration (previously a few lines further down, used only for
     orbit-camera input) was moved up to directly above this click block
     since it now also needs `io.KeyCtrl`/`io.KeyShift` here — every other
     pre-existing use of `io` further down in `Build()` is unaffected, it is
     the exact same local, just declared earlier.
   - Corrected the stale comment directly above the wireframe-reveal block
     (previously claiming "never for more than one body at once, since
     Selection is single-selection end-to-end") to explain that Selection can
     now hold many rigid bodies at once and that this loop's `isSelected`
     per-part membership check already draws a wireframe for every one of
     them with no drawing-loop changes needed.
5. **Root `CMakeLists.txt`** — added `src/Editor/FlatListRangeSelection.h`/
   `.cpp` to the `GTE_ENABLE_PROJECT_PANEL` source block, alongside
   `RigidBodyGroupSelection.h/.cpp`.
6. **`tests/Editor/FlatListRangeSelectionTests.cpp`** (new file) — 5 Tier-1
   tests covering `BuildInclusiveIndexRange()`: no-anchor degrades to just the
   clicked index, anchor-before/anchor-after both build the same ascending
   inclusive range, an anchor equal to the clicked index returns a single
   element, and adjacent indices return exactly the two elements.
7. **`tests/CMakeLists.txt`** — registered the new test file in the
   `GTE_ENABLE_PROJECT_PANEL` test-source block, alongside
   `RigidBodySelectionAlgorithmsTests.cpp`.

No self-corrections were needed beyond the two auto-dedup notices `edit_line`
itself reported and confirmed were correct (an accidental exact-line overlap
between the inserted content's own trailing closing brace/statement and the
pre-existing text immediately after the edited range — in both cases the tool
correctly removed the now-redundant leftover original line, verified by
reading the surrounding context immediately afterward).

## Verification

- **Compile check**: `cmake --build build --target gte_core` — succeeds,
  rebuilding `FlatListRangeSelection.cpp`, `BoneViewerWindow.cpp`,
  `Panels/InspectorPanel.cpp`, and `ImGuiEditorLayer.cpp` with zero errors.
- **Compile check**: `cmake --build build --target GreatTamanaEngineTests`
  — succeeds (only `Editor/FlatListRangeSelectionTests.cpp` needed compiling
  as a new translation unit).
- **Targeted test run**:
  `GreatTamanaEngineTests.exe --gtest_filter=FlatListRangeSelectionTest.*:SelectionTest.*:RigidBodyGroupSelectionTest.*:RigidBodyWireframeTest.*:ModelRigCacheTest.*`
  — **64/64 tests pass** (5 new `FlatListRangeSelectionTest` + 32
  `SelectionTest` [Phase 1] + 12 `RigidBodyGroupSelectionTest` [Phase 2] + 8
  `RigidBodyWireframeTest` + 7 `ModelRigCacheTest`, all pre-existing suites
  confirmed still green on top of this phase's changes), 0 failures.
- Per the workflow rules for this task, a full build/full regression
  (`ctest`) was deliberately NOT run — only this fast, targeted
  compile-and-test check, as instructed. A live, real-model manual smoke test
  (Step 5, item 6 of the phase document — opening the actual `GreatTamanaEngine`
  executable against a real jiggle-bone MMD model) was also NOT performed in
  this session, consistent with "Fast Compile Check" being the instructed
  verification depth for this phase.

## Notes for the next phase

- Phase 4 (`PHASE4_INSPECTOR_MULTI_SELECTION_SUMMARY.md`) now has everything
  it needs: `Selection::SelectedModelPartIndices()` (Phase 1) already reflects
  every selection surface this phase wires up (the two toolbar buttons,
  Ctrl-click toggle, and genuine Shift-range-select on both the flat rows and
  the direct viewport-dot click) — `InspectorPanel.cpp`'s
  `BuildModelPartInspector()` can read it directly with no further
  `BoneViewerWindow`/`Selection` changes needed.
- Every existing per-part highlight/reveal loop in `BoneViewerWindow.cpp`
  (tree row, flat row, direct viewport-dot click, and the wireframe-reveal
  block) already correctly multi-highlights/multi-reveals now that a
  multi-item selection is actually reachable through real user input, not
  just the two new buttons — confirmed by inspecting every `IsModelPartSelected()`
  call site, none of which needed a code change beyond the one stale comment.

## Files touched

- `src/Editor/FlatListRangeSelection.h` (new)
- `src/Editor/FlatListRangeSelection.cpp` (new)
- `src/Editor/BoneViewerWindow.h`
- `src/Editor/BoneViewerWindow.cpp`
- `CMakeLists.txt`
- `tests/Editor/FlatListRangeSelectionTests.cpp` (new)
- `tests/CMakeLists.txt`
- `task_manager/verlet-integration-4/PHASE3_COMPLETION_REPORT.md` (this file)
