# PHASE3 — Bone Viewer: "Select All (Group)"/"Select All (Branch)" Buttons + Ctrl-Click Toggle + Shift-Click Range Select (`src/Editor/BoneViewerWindow.cpp`) (v2)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprits D, E). Depends on: Phase 1's
`Selection::SelectModelParts()`/`ToggleModelPartInSelection()` and Phase 2's
`RigidBodyGroupSelection.h` (`SelectRigidBodiesByGroup()`/
`SelectRigidBodyBranch()`) and `BoneViewerWindow::m_rigidBodyAdjacency`/
`RigidBodyEntry::group` all existing and compiling. Produces: the actual
user-visible feature — two working toolbar buttons (with preview tooltips),
and every existing selection click path in the Bone Viewer now supports
Ctrl-click (toggle one item) and, on the two flat surfaces (Rigid Body/Joint),
a genuine Shift-click contiguous range select.

**v2 revision note**: this phase changed substantially from v1. Three
findings from `PHASE0_MASTER_STRATEGY.md`'s Revision Notes drove the changes,
each cited again inline below at the exact step it affects:
- **Finding #1 (bug fix)**: the `hasSeed` check in Step 3.2 now additionally
  requires `SelectedModelPartIndices().size() == 1` — v1's check alone
  (lowest selected index in range) stayed true even when several rigid
  bodies were already selected, letting the buttons silently reseed from an
  ambiguous "lowest of many" index instead of being disabled.
- **Finding #2 (real Shift-click range select + a new pure module)**: Step
  3.3/3.6 replace v1's "Ctrl OR Shift both toggle" flat-row/viewport-dot
  click handling with genuine Ctrl-toggle vs. Shift-range-select, backed by a
  new tiny pure module, `src/Editor/FlatListRangeSelection.h/.cpp` (Step 3.5),
  plus a new `m_flatSelectionAnchorIndex` member (Step 3.4) and its reset
  rules. The Bone tree (Step 3.3's `RenderBoneTreeNode()` bullet) explicitly
  KEEPS v1's toggle-only behavior for both modifiers, with a corrected
  comment explaining why.
- **Finding #3 (QoL)**: Step 3.2 also adds a hover tooltip to each button
  previewing the real match count, and a distinct tooltip for "Select All
  (Branch)" when the seed is itself a branch/junction.

Every "today, line N" citation below (for code this phase reads/edits that
was NOT itself touched by this v2 revision) was re-verified against the live
source tree during this v2 pass and is still byte-for-byte accurate.

## Step 1: The Goal

1. While `m_viewMode == ModelPartKind::RigidBody` and the model has at least
   one rigid body, the toolbar shows two new buttons, **"Select All (Group)"**
   and **"Select All (Branch)"**, both disabled (grayed out, per this
   codebase's existing disabled-button convention — see `ProjectPanel.cpp`'s
   own "Delete Selected" menu item gating on `HasAssetSelection()`) unless
   there is currently EXACTLY ONE rigid body selected on `m_targetEntity` to
   act as the "seed" — not merely "the lowest of possibly several selected
   indices happens to be in range" (see this document's own v2 revision note,
   Finding #1, and Step 3.2 below).
2. Clicking **"Select All (Group)"** selects every rigid body sharing the
   seed's own `RigidBodyEntry::group` value (Phase 2's
   `SelectRigidBodiesByGroup()`), replacing the current selection with that
   whole set via `Selection::SelectModelParts()`. Hovering the button (while
   enabled) previews the real match count in a tooltip.
3. Clicking **"Select All (Branch)"** selects every rigid body in the seed's
   own uninterrupted joint-connected chain, stopping at (and excluding) any
   branch/junction rigid body (Phase 2's `SelectRigidBodyBranch()`, over
   `m_rigidBodyAdjacency`), replacing the current selection the same way.
   Hovering the button (while enabled) previews the real match count, or - if
   the seed is itself a branch/junction (degree >= 3) - a distinct tooltip
   explaining that nothing will expand beyond the seed itself.
4. Every existing selection click path — the bone tree row
   (`RenderBoneTreeNode()`), the flat rigid-body/joint row
   (`RenderFlatPartRow()`), and the direct viewport-dot click (inside
   `Build()`) — now checks `ImGui::GetIO().KeyCtrl`/`KeyShift`:
   - **Bone tree** (`RenderBoneTreeNode()`): Ctrl OR Shift both call
     `ToggleModelPartInSelection()` (extend/remove one item) — unchanged from
     v1. A bone's raw array index has no meaningful linear "range" the way a
     flat Rigid Body/Joint row does, so a genuine Shift-range-select is
     deliberately out of scope for the tree — see `PHASE0_MASTER_STRATEGY.md`'s
     "What We Will NOT Do".
   - **Flat rows / direct viewport dot, Rigid Body or Joint mode only**:
     Ctrl calls `ToggleModelPartInSelection()` (toggle exactly one item, same
     as v1); Shift instead calls a NEW genuine, anchor-based, contiguous
     range select (`FlatListRangeSelection.h`'s `BuildInclusiveIndexRange()`
     feeding `Selection::SelectModelParts()`) — a real behavior change from
     v1, where Shift used to be a plain alias for Ctrl's toggle.
   - A plain click (neither modifier held) keeps replacing the whole
     selection with just the one clicked/hovered part, exactly as before this
     whole campaign, on every path.
5. Once selected (by either the buttons, Ctrl-click, or Shift-range-select),
   every selected rigid body already correctly multi-highlights (orange dot +
   ring) AND multi-reveals its real wireframe — see Step 2 below for why
   this needs NO drawing-loop changes at all, only the one stale code
   comment fixed in 3.4 (unchanged from v1).

## Step 2: The Situation / The Problem

`BoneViewerWindow.cpp`'s toolbar (today, `Build()`, lines 685-721) ends with
a part-count line and empty-state messages, then a `Separator()` — this is
where the two new buttons are inserted, gated on `m_viewMode ==
ModelPartKind::RigidBody`.

Three click paths currently always call the single-select `SelectModelPart()`:

- `RenderBoneTreeNode()` (today, lines 567-576):
  ```cpp
      if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
          ctx.selection.SelectModelPart(m_targetEntity, ModelPartKind::Bone, boneIndex);
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
              m_camTarget = bone.position;
          }
      }
  ```
- `RenderFlatPartRow()` (today, lines 599-604):
  ```cpp
      if (ImGui::Selectable(label.c_str(), isSelected)) {
          ctx.selection.SelectModelPart(m_targetEntity, kind, index);
          if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
              m_camTarget = position;
          }
      }
  ```
- The direct viewport-dot click, inside `Build()` (today, lines 897-906):
  ```cpp
      if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
          if (hoveredPartIndex >= 0) {
              ctx.selection.SelectModelPart(m_targetEntity, m_viewMode, hoveredPartIndex);
          } else {
              m_rotating = true;
          }
      }
  ```

None of these three currently check any modifier key.

Crucially, **the per-part drawing loop that decides highlight color and
wireframe visibility (`Build()`, lines 992-1047) already calls
`ctx.selection.IsModelPartSelected(...)` ONCE PER PART, inside a `for` loop
over every `overlayParts[i]`** — it never assumed "at most one `i` can ever
be true." The exact same is true of `RenderBoneTreeNode()`'s/
`RenderFlatPartRow()`'s own per-row `IsModelPartSelected()` check. This means
Phase 1's change to `IsModelPartSelected()` (membership-in-a-set instead of
equality-to-one-value) is **already sufficient, by itself, to make every one
of these loops correctly multi-highlight/multi-reveal** the moment
`Selection` actually holds more than one index — confirmed by re-reading
every one of these call sites during this campaign's investigation (and
re-confirmed again during the v2 self-audit). The ONLY remaining problem is
that nothing (yet) ever PUTS more than one index into `Selection` — which is
exactly what this phase's two buttons + modifier-key handling fix.

One stale comment needs correcting: the code comment directly above the
wireframe-reveal block (today, lines 1025-1035) currently reads (in part):
*"and never for more than one body at once, since Selection is
single-selection end-to-end."* This sentence describes the OLD, now-obsolete
invariant this exact campaign breaks — it must be corrected in this phase
(3.4 below, unchanged from v1), not left to silently mislead a future reader.

## Step 3: The Plan

### 3.1 `BoneViewerWindow.cpp` — new includes

Add, alongside the existing `#include "RigidBodyWireframe.h"` (today, line
0005):

```cpp
#include "FlatListRangeSelection.h" // v2 - BuildInclusiveIndexRange() for genuine Shift-click range select.
#include "RigidBodyGroupSelection.h"
```

### 3.2 `BoneViewerWindow.cpp` — the two new toolbar buttons

Insert, immediately after the existing `ImGui::Separator();` that ends the
toolbar section (today, line 0721) and before the
`const std::string lowerFilter = ...` line that starts the part-list-pane
section (today, line 0723):

```cpp
    // "Select All (Group)"/"Select All (Branch)" - only meaningful in Rigid
    // Body mode (RigidBodyEntry::group and joint-adjacency are both
    // rigid-body-only concepts - see task_manager/verlet-integration-4/
    // PHASE0_MASTER_STRATEGY.md, Step 4). Seeded from whichever ONE rigid
    // body is currently the Model-Part selection on THIS window's own
    // m_targetEntity - both buttons are disabled (not merely a no-op) when
    // there is no such single seed to act from, matching this codebase's
    // existing "grey out an action with nothing valid to act on" convention
    // (see Panels/ProjectPanel.cpp's own "Delete Selected" menu item gated
    // on HasAssetSelection()).
    if (m_viewMode == ModelPartKind::RigidBody && !m_rigidBodies.empty()) {
        // v2 fix (task_manager/verlet-integration-4/PHASE0_MASTER_STRATEGY.md's
        // Revision Notes, finding #1): hasSeed now ALSO requires
        // SelectedModelPartIndices().size() == 1. v1's check below the
        // "&&"s stopped at "the lowest selected index is in range," which
        // stays true even when SEVERAL rigid bodies are already selected
        // (e.g. right after clicking one of these very buttons once
        // already, or after a Shift-range-select) - reseeding from
        // SelectedModelPartIndex() (always just the LOWEST of however many
        // are selected - see Selection.h) in that state is ambiguous and
        // contradicts this button's own "seeded from whichever ONE rigid
        // body is currently selected" contract (Step 1, goal #1) and the
        // story's own singular "pick everything with same group [as THE
        // selected one]" phrasing.
        const bool hasSeed = ctx.selection.Kind() == InspectorSelectionKind::ModelPart
            && ctx.selection.SelectedModelPartEntity() == m_targetEntity
            && ctx.selection.SelectedModelPartKind() == ModelPartKind::RigidBody
            && ctx.selection.SelectedModelPartIndices().size() == 1
            && ctx.selection.SelectedModelPartIndex() >= 0
            && static_cast<std::size_t>(ctx.selection.SelectedModelPartIndex()) < m_rigidBodies.size();

        const std::int32_t seed = hasSeed ? static_cast<std::int32_t>(ctx.selection.SelectedModelPartIndex()) : -1;
        // Only meaningful when hasSeed is true (seed >= 0 and in range) -
        // guarded accordingly at every read site below.
        const bool seedIsBranch = hasSeed && m_rigidBodyAdjacency[static_cast<std::size_t>(seed)].size() >= 3;

        ImGui::BeginDisabled(!hasSeed);
        if (ImGui::Button("Select All (Group)")) {
            std::vector<std::uint8_t> groups;
            groups.reserve(m_rigidBodies.size());
            for (const RigidBodyEntry& body : m_rigidBodies) {
                groups.push_back(body.group);
            }
            const std::vector<std::int32_t> matches = SelectRigidBodiesByGroup(groups, seed);
            ctx.selection.SelectModelParts(
                m_targetEntity, ModelPartKind::RigidBody, std::vector<int>(matches.begin(), matches.end()));
        }
        // v2 QoL addition (task_manager/verlet-integration-4/
        // PHASE0_MASTER_STRATEGY.md's Revision Notes, finding #3): preview
        // the real match count on hover, computed via the exact same Phase 2
        // function the click handler itself calls above - never a
        // second, independently-maintained estimate.
        if (hasSeed && ImGui::IsItemHovered()) {
            std::vector<std::uint8_t> groups;
            groups.reserve(m_rigidBodies.size());
            for (const RigidBodyEntry& body : m_rigidBodies) {
                groups.push_back(body.group);
            }
            const std::size_t count = SelectRigidBodiesByGroup(groups, seed).size();
            const RigidBodyEntry& seedBody = m_rigidBodies[static_cast<std::size_t>(seed)];
            ImGui::SetTooltip("Selects %zu rigid bod%s sharing collision group %u with \"%s\".", count,
                count == 1 ? "y" : "ies", static_cast<unsigned>(seedBody.group), seedBody.name.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Select All (Branch)")) {
            const std::vector<std::int32_t> matches = SelectRigidBodyBranch(m_rigidBodyAdjacency, seed);
            ctx.selection.SelectModelParts(
                m_targetEntity, ModelPartKind::RigidBody, std::vector<int>(matches.begin(), matches.end()));
        }
        if (hasSeed && ImGui::IsItemHovered()) {
            const RigidBodyEntry& seedBody = m_rigidBodies[static_cast<std::size_t>(seed)];
            if (seedIsBranch) {
                // The seed itself is a junction (degree >= 3) - the click
                // handler above will correctly select just `{ seed }` (see
                // Phase 2's own SelectRigidBodyBranchFromABranchNodeItself...
                // test) - tell the user WHY up front instead of letting them
                // discover "nothing visibly changed" by clicking blind.
                ImGui::SetTooltip(
                    "\"%s\" is itself a branch/junction (connected to %zu other rigid bodies) -\n"
                    "there is no single unambiguous chain to select; only itself will be selected.",
                    seedBody.name.c_str(), m_rigidBodyAdjacency[static_cast<std::size_t>(seed)].size());
            } else {
                const std::size_t count = SelectRigidBodyBranch(m_rigidBodyAdjacency, seed).size();
                ImGui::SetTooltip("Selects %zu rigid bod%s in \"%s\"'s own uninterrupted joint chain.", count,
                    count == 1 ? "y" : "ies", seedBody.name.c_str());
            }
        }
        ImGui::EndDisabled();
        if (!hasSeed) {
            ImGui::SameLine();
            if (ctx.selection.SelectedModelPartIndices().size() > 1) {
                ImGui::TextDisabled("(select exactly one rigid body first - %zu are currently selected)",
                    ctx.selection.SelectedModelPartIndices().size());
            } else {
                ImGui::TextDisabled("(select a rigid body first)");
            }
        }
    }
```

(`std::vector<int>(matches.begin(), matches.end())` converts Phase 2's
`std::vector<std::int32_t>` result into the `std::vector<int>`
`Selection::SelectModelParts()` expects — a plain, always-safe iterator-range
construction regardless of whether `int`/`int32_t` happen to be the same
type on a given target, with no risk of silently truncating a value.)

### 3.3 `BoneViewerWindow.cpp` — Ctrl-click toggle / Shift-click range-select

Replace `RenderBoneTreeNode()`'s click block (today, lines 567-576):

```cpp
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        ctx.selection.SelectModelPart(m_targetEntity, ModelPartKind::Bone, boneIndex);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_camTarget = bone.position;
        }
    }
```

with:

```cpp
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        // Ctrl/Shift-click both TOGGLE (add/remove) this one bone in/out of
        // the current selection - Selection's own multi-object support
        // (task_manager/verlet-integration-4/
        // PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md's
        // ToggleModelPartInSelection()). Deliberately NOT a genuine Windows-
        // Explorer-style Shift range-select here (unlike Rigid Body/Joint's
        // flat rows/viewport dot, below) - a bone's raw array index has no
        // meaningful linear "range" to a user looking at an indented
        // hierarchy tree, only its position within whichever branch happens
        // to be expanded right now; correctly supporting a range select here
        // would first require flattening the CURRENTLY VISIBLE/expanded tree
        // rows into a linear order, a meaningfully bigger and differently-
        // shaped piece of work the story never asked for - see
        // task_manager/verlet-integration-4/PHASE0_MASTER_STRATEGY.md's
        // "What We Will NOT Do" (v2). A plain click (neither modifier held)
        // keeps today's exact "replace with just this one" behavior via
        // SelectModelPart().
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl || io.KeyShift) {
            ctx.selection.ToggleModelPartInSelection(m_targetEntity, ModelPartKind::Bone, boneIndex);
        } else {
            ctx.selection.SelectModelPart(m_targetEntity, ModelPartKind::Bone, boneIndex);
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_camTarget = bone.position;
        }
    }
```

Replace `RenderFlatPartRow()`'s click block (today, lines 599-604):

```cpp
    if (ImGui::Selectable(label.c_str(), isSelected)) {
        ctx.selection.SelectModelPart(m_targetEntity, kind, index);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_camTarget = position;
        }
    }
```

with (v2 — genuine Shift-range-select instead of v1's "Shift also just
toggles"):

```cpp
    if (ImGui::Selectable(label.c_str(), isSelected)) {
        // v2 (task_manager/verlet-integration-4/PHASE0_MASTER_STRATEGY.md's
        // Revision Notes, finding #2): Ctrl-click TOGGLES exactly this one
        // row in/out of the current selection (Selection's own
        // ToggleModelPartInSelection()); Shift-click instead performs a
        // genuine Windows-Explorer/Unity-style contiguous RANGE select from
        // m_flatSelectionAnchorIndex (the last plain- or Ctrl-clicked row -
        // see that field's own doc comment, BoneViewerWindow.h) through
        // THIS row, REPLACING the whole selection with exactly that range
        // (never a union with whatever was selected before - a real
        // Explorer Shift-click does the same). A plain click (neither
        // modifier held) keeps today's exact "replace with just this one"
        // behavior via SelectModelPart(), and also moves the range anchor to
        // this row, same as a real Ctrl-click does.
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyShift) {
            const std::vector<std::int32_t> range = BuildInclusiveIndexRange(m_flatSelectionAnchorIndex, index);
            ctx.selection.SelectModelParts(m_targetEntity, kind, std::vector<int>(range.begin(), range.end()));
        } else if (io.KeyCtrl) {
            ctx.selection.ToggleModelPartInSelection(m_targetEntity, kind, index);
            m_flatSelectionAnchorIndex = index;
        } else {
            ctx.selection.SelectModelPart(m_targetEntity, kind, index);
            m_flatSelectionAnchorIndex = index;
        }
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_camTarget = position;
        }
    }
```

Replace the direct viewport-dot click, inside `Build()` (today, lines
897-906):

```cpp
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hoveredPartIndex >= 0) {
                // Clicked directly on a part's gizmo dot - select it
                // (mirrors clicking its row in the tree/list pane) instead
                // of starting an orbit-camera drag.
                ctx.selection.SelectModelPart(m_targetEntity, m_viewMode, hoveredPartIndex);
            } else {
                m_rotating = true;
            }
        }
```

with (v2 — mirrors `RenderFlatPartRow()`'s own Ctrl/Shift split above, but
gated on `m_viewMode` since this ONE click handler serves all three view
modes and only Rigid Body/Joint have a meaningful linear range at all):

```cpp
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hoveredPartIndex >= 0) {
                // Clicked directly on a part's gizmo dot - select it
                // (mirrors clicking its row in the tree/list pane) instead
                // of starting an orbit-camera drag. Range-select (Shift)
                // only makes sense for a flat, linearly-ordered list (Rigid
                // Body/Joint mode) - Bone mode keeps the same toggle-only
                // behavior as its own tree rows (RenderBoneTreeNode()) for
                // both Ctrl AND Shift, since a bone's raw array index
                // carries no meaningful "range" to a user (see that
                // function's own updated doc comment above, and
                // task_manager/verlet-integration-4/
                // PHASE0_MASTER_STRATEGY.md's Revision Notes, finding #2).
                const bool supportsRangeSelect = m_viewMode != ModelPartKind::Bone;
                if (supportsRangeSelect && io.KeyShift) {
                    const std::vector<std::int32_t> range = BuildInclusiveIndexRange(
                        m_flatSelectionAnchorIndex, static_cast<std::int32_t>(hoveredPartIndex));
                    ctx.selection.SelectModelParts(
                        m_targetEntity, m_viewMode, std::vector<int>(range.begin(), range.end()));
                } else if (io.KeyCtrl || io.KeyShift) {
                    ctx.selection.ToggleModelPartInSelection(m_targetEntity, m_viewMode, hoveredPartIndex);
                    if (supportsRangeSelect) {
                        m_flatSelectionAnchorIndex = hoveredPartIndex;
                    }
                } else {
                    ctx.selection.SelectModelPart(m_targetEntity, m_viewMode, hoveredPartIndex);
                    if (supportsRangeSelect) {
                        m_flatSelectionAnchorIndex = hoveredPartIndex;
                    }
                }
            } else {
                m_rotating = true;
            }
        }
```

(`io` here is `const ImGuiIO&` already fetched a few lines below this block
today — at line 917, `const ImGuiIO& io = ImGui::GetIO();` — move that
existing declaration up to directly ABOVE this `if (hovered && ...)` block
instead of leaving it below, since this edit now needs `io` here too; every
other existing use of `io` further down in `Build()` keeps working unchanged
since it's the exact same local, just declared a few lines earlier. This was
already true in v1 and is unchanged here.)

### 3.4 `BoneViewerWindow.cpp` — correct the stale single-selection comment

Replace the comment block directly above the wireframe-reveal `if` (today,
lines 1025-1035):

```cpp
                // As of task_manager/verlet-integration-3
                // (PHASE2_BONE_VIEWER_SELECT_TO_REVEAL_WIREFRAME_INTEGRATION.md):
                // an UNSELECTED rigid body shows ONLY its plain dot (drawn
                // above, shared with Bone/Joint mode) - exactly a "single
                // selectable point", per the story's own requirement. Only
                // the CURRENTLY SELECTED rigid body additionally reveals its
                // real, per-shape wireframe (a wire sphere/box/capsule built
                // from its actual shape/size/rotation - see
                // RigidBodyWireframe.h), never a generic screen-space circle
                // - and never for more than one body at once, since
                // Selection is single-selection end-to-end.
```

with:

```cpp
                // As of task_manager/verlet-integration-3
                // (PHASE2_BONE_VIEWER_SELECT_TO_REVEAL_WIREFRAME_INTEGRATION.md):
                // an UNSELECTED rigid body shows ONLY its plain dot (drawn
                // above, shared with Bone/Joint mode) - exactly a "single
                // selectable point", per that campaign's own requirement.
                // Every CURRENTLY SELECTED rigid body additionally reveals
                // its real, per-shape wireframe (a wire sphere/box/capsule
                // built from its actual shape/size/rotation - see
                // RigidBodyWireframe.h), never a generic screen-space
                // circle. As of task_manager/verlet-integration-4
                // (PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md/
                // PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md),
                // Selection can hold MANY rigid bodies at once (Ctrl-click,
                // Shift-range-select, or the "Select All (Group)"/"Select
                // All (Branch)" toolbar buttons) - `isSelected` here is a
                // per-part membership check, so this loop naturally draws a
                // wireframe for EVERY currently-selected rigid body, not
                // just one; no code in this loop needed to change for that
                // to be correct - only IsModelPartSelected()'s own
                // semantics did (see Selection.h).
```

### 3.5 New file: `src/Editor/FlatListRangeSelection.h` (v2)

```cpp
#pragma once

#include <cstdint>
#include <vector>

namespace gte {

// Builds the inclusive, ascending-sorted index range spanning `anchorIndex`
// and `clickedIndex` - the pure logic behind a genuine Windows Explorer/
// Unity-style Shift-click "range select" (as opposed to Ctrl-click's own
// single-item TOGGLE, see Selection.h's ToggleModelPartInSelection()): every
// index from whichever of the two is smaller up to whichever is larger,
// inclusive of both endpoints. Order-independent -
// BuildInclusiveIndexRange(2, 5) and BuildInclusiveIndexRange(5, 2) both
// return { 2, 3, 4, 5 }. Deliberately decoupled from BoneViewerWindow/
// RigidBodyEntry/JointEntry (plain int32_t in, plain int32_t vector out) -
// this is pure index arithmetic with no notion of "rigid body" or "joint"
// at all, exactly like RigidBodyGroupSelection.h's own functions are
// deliberately decoupled from RigidBody/Joint (see that header's own "What
// We Will NOT Do" reasoning) - reusable by ANY future flat, linearly-ordered
// list this Editor ever grows (not just Rigid Body/Joint rows).
//
// If `anchorIndex` is negative (no anchor recorded yet - see
// BoneViewerWindow.h's own m_flatSelectionAnchorIndex doc comment for every
// point it gets reset to -1: a fresh window, a genuine data reload, or a
// view-mode switch), there is no meaningful "distance" to span at all, so
// the result is just `{ clickedIndex }` alone - a Shift-click with no prior
// plain/Ctrl-click anchor degrades to selecting just the row that was
// actually clicked, never a crash or an empty result. `clickedIndex` itself
// is assumed non-negative by every real caller (BoneViewerWindow.cpp never
// calls this with a negative clickedIndex - see RenderFlatPartRow()/Build()'s
// own bounds-checked callers) but is not itself defensively re-validated
// here, since this function has no array to index out of bounds against -
// it only ever produces a list of plain integers for the caller to hand to
// Selection::SelectModelParts(), which is itself already defensive.
std::vector<std::int32_t> BuildInclusiveIndexRange(std::int32_t anchorIndex, std::int32_t clickedIndex);

} // namespace gte
```

### 3.6 New file: `src/Editor/FlatListRangeSelection.cpp` (v2)

```cpp
#include "FlatListRangeSelection.h"

#include <algorithm>
#include <cstddef>

namespace gte {

std::vector<std::int32_t> BuildInclusiveIndexRange(std::int32_t anchorIndex, std::int32_t clickedIndex)
{
    if (anchorIndex < 0) {
        return { clickedIndex };
    }

    const std::int32_t lo = std::min(anchorIndex, clickedIndex);
    const std::int32_t hi = std::max(anchorIndex, clickedIndex);

    std::vector<std::int32_t> result;
    result.reserve(static_cast<std::size_t>(hi - lo + 1));
    for (std::int32_t i = lo; i <= hi; ++i) {
        result.push_back(i);
    }
    return result;
}

} // namespace gte
```

### 3.7 `BoneViewerWindow.h` — new include + `m_flatSelectionAnchorIndex` member (v2)

Add, alongside Phase 2's own `#include "RigidBodyGroupSelection.h"` (added
near the top of the header, alongside `#include "../Assets/PhysicsData.h"`):

```cpp
#include "FlatListRangeSelection.h" // BuildInclusiveIndexRange()
```

Add, alongside Phase 2's own `m_rigidBodyAdjacency` member (declared near
`m_rootBoneIndices`):

```cpp
    // The last FLAT-list part index (Rigid Body/Joint mode only - see
    // m_viewMode) that was the target of a PLAIN or Ctrl-click - Shift-
    // click's own "anchor" for a genuine Windows-Explorer-style contiguous
    // range select (see FlatListRangeSelection.h's BuildInclusiveIndexRange(),
    // and task_manager/verlet-integration-4/
    // PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md's own
    // v2 revision). Reset to -1 (no anchor) whenever the index space it
    // refers to stops meaning the same thing: a genuine data reload
    // (EnsureDataLoaded()'s "reload starting" block, right alongside
    // ctx.selection.ClearModelPartIfEntity() - see Step 3.8 below) and any
    // m_viewMode change (the View combo callback - Step 3.8 below) -
    // RigidBody/Joint each have their own independent index space, and Bone
    // mode has no flat-list range-select concept at all (see
    // RenderBoneTreeNode()'s own doc comment, Step 3.3, for why the tree
    // stays toggle-only for both Ctrl AND Shift).
    std::int32_t m_flatSelectionAnchorIndex = -1;
```

### 3.8 `BoneViewerWindow.cpp` — reset `m_flatSelectionAnchorIndex` at both required points (v2)

In `Reset()` (today, alongside the other per-load vectors cleared at lines
152-156, right after Phase 2's own `m_rigidBodyAdjacency.clear();`), add:

```cpp
    m_flatSelectionAnchorIndex = -1;
```

In `EnsureDataLoaded()`'s "genuine reload starting" block, right after the
existing `ctx.selection.ClearModelPartIfEntity(m_targetEntity);` call (today,
line 364), add:

```cpp
    m_flatSelectionAnchorIndex = -1; // Different data, possibly a different rigid-body/joint count/order - see this field's own doc comment.
```

In `Build()`'s toolbar, in the View combo's callback (today):

```cpp
    if (ImGui::Combo("View", &viewModeIndex, kViewModeLabels, static_cast<int>(std::size(kViewModeLabels)))) {
        m_viewMode = static_cast<ModelPartKind>(viewModeIndex);
    }
```

add the reset inside the same `if` body:

```cpp
    if (ImGui::Combo("View", &viewModeIndex, kViewModeLabels, static_cast<int>(std::size(kViewModeLabels)))) {
        m_viewMode = static_cast<ModelPartKind>(viewModeIndex);
        m_flatSelectionAnchorIndex = -1; // Different index space (Rigid Body vs. Joint vs. Bone) - see this field's own doc comment.
    }
```

### 3.9 `CMakeLists.txt` — new source files (v2, cumulative with Phase 2's own edit)

The `GTE_ENABLE_PROJECT_PANEL` block (root `CMakeLists.txt`) now reads, with
BOTH Phase 2's `RigidBodyGroupSelection.h/.cpp` AND this phase's
`FlatListRangeSelection.h/.cpp` present:

```cmake
    if(GTE_ENABLE_PROJECT_PANEL)
        target_sources(gte_core PRIVATE
            src/Editor/ProjectPanelData.h
            src/Editor/ProjectPanelData.cpp
            src/Editor/Panels/ProjectPanel.h
            src/Editor/Panels/ProjectPanel.cpp
            src/Editor/AssetInspectorData.h
            src/Editor/AssetInspectorData.cpp
            src/Editor/AssetPreviewTexture.h
            src/Editor/AssetPreviewTexture.cpp
            src/Editor/AssetPreviewMesh.h
            src/Editor/AssetPreviewMesh.cpp
            src/Editor/ModelRigCache.h
            src/Editor/ModelRigCache.cpp
            src/Editor/RigidBodyWireframe.h
            src/Editor/RigidBodyWireframe.cpp
            src/Editor/RigidBodyGroupSelection.h
            src/Editor/RigidBodyGroupSelection.cpp
            src/Editor/FlatListRangeSelection.h
            src/Editor/FlatListRangeSelection.cpp
            src/Editor/BoneViewerWindow.h
            src/Editor/BoneViewerWindow.cpp
        )
    endif()
```

### 3.10 New test file: `tests/Editor/FlatListRangeSelectionTests.cpp` (v2)

```cpp
// Unit tests for FlatListRangeSelection (src/Editor/FlatListRangeSelection.h)
// - the pure "build a Shift-click contiguous range" logic behind the Bone
// Viewer's Rigid Body/Joint flat-row and viewport-dot Shift-click handling
// (see AGENTS.md, "Testability and Regression Safety", and task_manager/
// verlet-integration-4/PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md's
// own v2 revision). Deliberately pure logic with no ImGui/SDL/Vulkan/
// BoneViewerWindow dependency at all - only compiled/linked when
// GTE_ENABLE_EDITOR AND GTE_ENABLE_PROJECT_PANEL are both ON, since
// FlatListRangeSelection itself is only ever compiled into gte_core then
// (see tests/CMakeLists.txt, alongside RigidBodySelectionAlgorithmsTests.cpp).

#include "Editor/FlatListRangeSelection.h"

#include <gtest/gtest.h>

namespace gte {
namespace {

TEST(FlatListRangeSelectionTest, NoAnchorReturnsOnlyTheClickedIndex)
{
    EXPECT_EQ(BuildInclusiveIndexRange(-1, 7), (std::vector<std::int32_t>{ 7 }));
}

TEST(FlatListRangeSelectionTest, AnchorBeforeClickedBuildsAscendingInclusiveRange)
{
    EXPECT_EQ(BuildInclusiveIndexRange(2, 5), (std::vector<std::int32_t>{ 2, 3, 4, 5 }));
}

TEST(FlatListRangeSelectionTest, AnchorAfterClickedStillBuildsAscendingInclusiveRange)
{
    EXPECT_EQ(BuildInclusiveIndexRange(5, 2), (std::vector<std::int32_t>{ 2, 3, 4, 5 }));
}

TEST(FlatListRangeSelectionTest, AnchorEqualToClickedReturnsASingleElement)
{
    EXPECT_EQ(BuildInclusiveIndexRange(3, 3), (std::vector<std::int32_t>{ 3 }));
}

TEST(FlatListRangeSelectionTest, AdjacentAnchorAndClickedReturnsExactlyTheTwoElements)
{
    EXPECT_EQ(BuildInclusiveIndexRange(4, 5), (std::vector<std::int32_t>{ 4, 5 }));
}

} // namespace
} // namespace gte
```

### 3.11 `tests/CMakeLists.txt` — register the new test file (v2, cumulative with Phase 2's own edit)

The `GTE_ENABLE_PROJECT_PANEL` nested block (`tests/CMakeLists.txt`) now
reads, with BOTH Phase 2's `RigidBodySelectionAlgorithmsTests.cpp` AND this
phase's `FlatListRangeSelectionTests.cpp` present:

```cmake
    if(GTE_ENABLE_PROJECT_PANEL)
        list(APPEND GTE_TEST_SOURCES
            Editor/ProjectPanelDataTests.cpp
            Editor/AssetInspectorDataTests.cpp
            Editor/ModelRigCacheTests.cpp
            Editor/RigidBodyWireframeTests.cpp
            Editor/RigidBodySelectionAlgorithmsTests.cpp
            Editor/FlatListRangeSelectionTests.cpp
        )
    endif()
```

### 3.12 No other changes

`RigidBodyEntry::group`/`m_rigidBodyAdjacency` (Phase 2), and
`Selection::SelectModelParts()`/`ToggleModelPartInSelection()` (Phase 1), are
both already exactly what this phase's new code calls, unchanged. The
per-part dot-color/ring/wireframe drawing loop, the bone-tree/flat-row
rendering shape, the toolbar's search box/Reset View/Show All Names combo
label rendering, and the orbit-camera input all stay untouched beyond the
edits in 3.2/3.3/3.8 above — no other line in `Build()` changes.

## Step 4: What We Will NOT Do

- We will **not** show the two new buttons in Bone or Joint view mode — see
  `PHASE0_MASTER_STRATEGY.md`'s "What We Will NOT Do."
- We will **not** add a THIRD button for "deselect all" — Ctrl-clicking every
  currently-selected dot/row already achieves that via
  `ToggleModelPartInSelection()`, and `Selection::Clear()` already exists for
  a future global "deselect everything" action if ever needed elsewhere.
- We will **not** make the two buttons act on ANY currently-hovered rigid
  body — only the current Model-Part SELECTION (`SelectedModelPartIndex()`,
  guarded by the v2 `hasSeed` fix to mean "exactly one selected") is ever the
  seed, matching the story's own "select all group: it will iterate ... and
  pick everything with same group [as the selected one]" phrasing.
- We will **not** add drag-rectangle/lasso selection in this phase — see
  `PHASE0_MASTER_STRATEGY.md`'s "What We Will NOT Do."
- We will **not** change `RenderBoneTreeNode()`'s/`RenderFlatPartRow()`'s
  double-click-to-recenter-camera behavior — it stays keyed off
  `ImGui::IsMouseDoubleClicked()` exactly as before, entirely independent of
  which selection-mutating branch just ran.
- We will **not** (v2) implement a genuine Shift-range-select for the Bone
  tree — see `PHASE0_MASTER_STRATEGY.md`'s "What We Will NOT Do" and this
  document's own Step 1/3.3 for the full reasoning (no natural linear order
  over a currently-expanded/collapsed hierarchy tree).
- We will **not** (v2) persist `m_flatSelectionAnchorIndex` across a Bone
  Viewer session in any way beyond the in-memory field itself (no save-to-disk,
  no "remember across app restarts") — it is exactly as ephemeral as
  `m_rotating`/`m_panning`, reset at the two points Step 3.8 documents.

## Step 5: Their Role

Implementer checklist for this phase:

1. Confirm Phase 1 (`Selection::SelectModelParts()`/
   `ToggleModelPartInSelection()`) and Phase 2
   (`RigidBodyGroupSelection.h`'s functions, `RigidBodyEntry::group`,
   `m_rigidBodyAdjacency`) already exist, compile, and pass their own tests
   before starting this phase.
2. Add `src/Editor/FlatListRangeSelection.h/.cpp` exactly per 3.5/3.6,
   register them in the root `CMakeLists.txt` per 3.9.
3. Apply 3.1-3.4 and 3.7-3.8 to `BoneViewerWindow.h/.cpp` exactly as written,
   taking care with 3.3's note about moving the existing `const ImGuiIO& io =
   ImGui::GetIO();` declaration earlier rather than duplicating it, and with
   3.8's THREE separate reset points for `m_flatSelectionAnchorIndex`
   (`Reset()`, `EnsureDataLoaded()`'s reload-start block, and the View combo
   callback) — missing any one of them leaves a stale anchor index that can
   silently build a nonsensical range against freshly (re)loaded or
   differently-kinded data.
4. Add `tests/Editor/FlatListRangeSelectionTests.cpp` exactly per 3.10, and
   register it per 3.11.
5. Build `gte_core` + `GreatTamanaEngineTests` and confirm the full suite
   (including every Phase 1/Phase 2 test AND the new
   `FlatListRangeSelectionTest.*` cases) still passes — this phase's own
   `BoneViewerWindow.cpp` changes are UI/ImGui call sites with no new
   Tier-1-testable pure logic of their own beyond the new
   `FlatListRangeSelection.h` module (which DOES get its own test file, per
   3.10 — this is a genuine, if small, deviation from v1/`verlet-
   integration-3`'s own "UI-only phases need no new test file" precedent,
   justified because this phase newly introduces real pure logic that
   didn't exist in v1's version of this phase at all).
6. Build the real `GreatTamanaEngine` executable and, against a real
   imported MMD model with a multi-body rigid-body chain (a skirt or hair
   jiggle-bone model with several distinct PMX collision groups is ideal),
   open the Bone Viewer, switch to "Rigid Bodies" mode, and confirm:
   - Both new buttons are disabled/greyed with the "(select a rigid body
     first)"/"(select exactly one rigid body first - N are currently
     selected)" hint until exactly one rigid body is selected — including
     immediately after a previous "Select All (...)" click or Shift-range-
     select left MORE than one body selected (the v2 bug fix, Finding #1).
   - Hovering either enabled button shows a tooltip previewing the real
     match count; hovering "Select All (Branch)" on a body that is itself a
     branch/junction shows the distinct "nothing to expand" tooltip instead.
   - Selecting one rigid body then clicking "Select All (Group)" highlights
     (orange dot + ring + wireframe) every rigid body sharing that body's own
     collision group, and only those.
   - Selecting one rigid body then clicking "Select All (Branch)" highlights
     its own uninterrupted joint-chain, stopping cleanly at any body
     connected to 3+ others, which itself stays unhighlighted.
   - Ctrl-clicking additional rows/dots after either button (or after a
     plain single click) ADDS them to the highlighted set without losing
     what was already selected; Ctrl-clicking an already-selected row/dot
     REMOVES just that one.
   - Plain-clicking row A, then Shift-clicking row D (with B/C in between),
     selects EXACTLY {A, B, C, D} — a genuine contiguous range, replacing
     whatever was selected before, on both the list-pane rows AND the direct
     viewport-dot click. Shift-clicking again afterwards (still from the
     SAME original anchor A) correctly extends/shrinks the range rather than
     ranging from wherever the previous Shift-click landed.
   - Switching the "View" dropdown away from and back to "Rigid Bodies" (or
     to "Joints") resets the range anchor — the very next Shift-click with no
     prior plain/Ctrl-click in the new mode selects just the one row clicked,
     never a stale/nonsensical range left over from the previous mode.
   - Switching to Bone mode: Ctrl-click AND Shift-click on a tree row both
     still just toggle exactly one bone, exactly like before this whole
     campaign — no range-select behavior appears there.
   - A plain (no modifier) click anywhere always collapses back down to
     exactly one selected part, matching pre-campaign behavior.
