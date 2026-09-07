# PHASE1 — COMPLETION REPORT: Selection Multi-Object Model-Part Support

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements `PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md`
exactly as written (no deviations from the phase document were needed — every
file/line citation in that document was re-verified against the live source
tree before editing and matched byte-for-byte).

## What was done

`src/Editor/Selection.h`/`Selection.cpp` were rewritten so the Model-Part
selection is a de-duplicated, ascending-sorted **set** (`std::vector<int>`)
instead of a single `int`:

1. **`Selection.h`** — added `#include <algorithm>` and `#include <vector>`
   alongside the existing `#include <string>`; replaced the private
   `int m_modelPartIndex = -1;` field with `std::vector<int> m_modelPartIndices;`;
   added two new public methods:
   - `void SelectModelParts(Entity owningEntity, ModelPartKind partKind, std::vector<int> indices)`
     — replaces the ENTIRE current Model-Part selection with a given set
     (sorted + de-duplicated internally); an empty set is a genuine CLEAR
     (mirrors `ClearModelPartIfEntity()`'s own "revert `Kind()` to `None`"
     behavior).
   - `void ToggleModelPartInSelection(Entity owningEntity, ModelPartKind partKind, int index)`
     — Ctrl-click's "extend by one item" semantics: adds the index if absent,
     removes it if present; starts a fresh single-element selection if the
     current selection is for a different entity/kind (or nothing at all);
     reverts `Kind()` to `None` if removing the last remaining item.
   - `SelectModelPart()` became a thin wrapper: `SelectModelParts(owningEntity, partKind, { partIndex })`.
   - `SelectedModelPartIndex()` became "the lowest currently-selected index,
     or -1 if empty" (`m_modelPartIndices.front()`), with a doc-comment
     warning that a future caller needing "exactly one selected" must also
     check `SelectedModelPartIndices().size() == 1`.
   - New accessor `const std::vector<int>& SelectedModelPartIndices() const`
     — the new source of truth for "how many/which" parts are selected.
   - Updated the class's own top comment to record this second extension,
     following the precedent it already documents for the first one.
2. **`Selection.cpp`** — added `#include <algorithm>`; implemented
   `SelectModelParts()` (sort + `std::unique`, empty-set-is-a-clear
   handling), `ToggleModelPartInSelection()` (compatible-selection check via
   `Kind()`/`m_modelPartEntity`/`m_modelPartKind`, `std::find`-based
   add/remove, empty-after-toggle clear); rewired `SelectModelPart()` to
   delegate to `SelectModelParts()`; updated `ClearModelPartIfEntity()`/
   `Clear()` to call `m_modelPartIndices.clear()` instead of resetting a
   single int; rewired `IsModelPartSelected()` to a `std::find`-based
   membership test against the set instead of an `==` comparison against one
   stored value.
3. **`tests/Editor/SelectionTests.cpp`** — added all 8 new test cases named
   in the phase document's Step 3.8, including the v2-added
   `ToggleModelPartInSelectionStartsAFreshSelectionWhenNothingOrAnUnrelatedKindWasSelected`
   coverage-gap closer (exercises starting from `Kind() == None` and
   `Kind() == Entity`, not just a pre-existing but incompatible ModelPart
   selection). No existing test needed editing — every pre-existing
   `SelectModelPart()`/`SelectedModelPartIndex()`-based test kept passing
   unchanged, confirming the single-selection behavior every other panel
   still relies on is preserved byte-for-byte.

One small self-correction made while transcribing the new tests into the
file: `edit_line`'s insertion left a duplicated pair of closing
`} // namespace` / `} // namespace gte` lines at the very end of the file
(the original single trailing line wasn't fully consumed by the replace);
this was caught immediately on the follow-up full-file read and fixed before
building.

## Verification

- **`BoneViewerWindow.cpp`/`InspectorPanel.cpp` call-site check**: searched
  the whole `src/` tree for every `SelectModelPart(`/`IsModelPartSelected(`/
  `SelectedModelPartIndex()` call site and for any direct
  `m_modelPartIndex` access — every existing call site uses only the
  pre-existing single-selection signatures (unchanged), and no code outside
  `Selection.cpp` ever touched the private field directly. Per the phase
  plan, these two files are intentionally NOT touched in this phase.
- **Compile check**: `cmake --build build --target gte_core` — succeeds,
  rebuilding `Selection.cpp` plus every other `src/Editor/` translation unit
  (`BoneViewerWindow.cpp`, `InspectorPanel.cpp`, and every `Panels/*.cpp`)
  with zero errors, confirming the new `Selection.h` API is fully
  source-compatible with every existing caller.
- **Compile check**: `cmake --build build --target GreatTamanaEngineTests`
  — succeeds (only `tests/Editor/SelectionTests.cpp` needed recompiling).
- **Targeted test run**: `GreatTamanaEngineTests.exe --gtest_filter=SelectionTest.*`
  — **32/32 tests pass** (24 pre-existing + 8 new from this phase), 0
  failures. Per the workflow rules for this task, a full build/full
  regression (`ctest`) was deliberately NOT run — only this fast, targeted
  compile-and-test check, as instructed.

## Notes for the next phase

- `Selection`'s public surface now has everything Phase 2/3 need:
  `SelectModelParts()` for the two new toolbar buttons, and
  `ToggleModelPartInSelection()` for Ctrl-click. Phase 3's Shift-click range
  select will build its own range as a plain `std::vector<int>` and hand it
  to `SelectModelParts()` directly, per the phase 0 plan — no further
  `Selection` changes are needed for that.
- `SelectedModelPartIndex()` is deliberately "lowest of the set", not "is
  there exactly one" — Phase 3's own `hasSeed` button-enablement check must
  additionally verify `SelectedModelPartIndices().size() == 1` (already
  called out in both the Phase 0 and Phase 1 documents' own revision notes).
- Every existing per-part highlight/reveal loop in `BoneViewerWindow.cpp`
  (tree row, flat row, direct viewport-dot click, and the
  `verlet-integration-3` wireframe-reveal-on-select block) reads through
  `IsModelPartSelected()` per-part, in a loop — since that method now does a
  set-membership test, all of these will automatically start correctly
  multi-highlighting the moment Phase 2/3 populate a multi-item selection,
  with zero further changes needed to those loops.

## Files touched

- `src/Editor/Selection.h`
- `src/Editor/Selection.cpp`
- `tests/Editor/SelectionTests.cpp`
- `task_manager/verlet-integration-4/PHASE1_COMPLETION_REPORT.md` (this file)
