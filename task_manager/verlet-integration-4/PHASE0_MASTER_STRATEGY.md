# PHASE0 — MASTER STRATEGY: Selection Multi-Object Support + Bone Viewer "Select All (Group)" / "Select All (Branch)" (v2 — second-iteration self-audit)

Orchestrator document for this campaign (folder kept as `verlet-integration-4`
per the task's own filing convention, following `verlet-integration-3`'s own
precedent of using this folder name for a Bone-Viewer/Selection campaign, not
Verlet physics). Every child phase document in this folder implements one
compiling, testable slice of this plan. This document is the single source of
truth for **ordering, ownership, and the identified root causes** — read this
first, then execute `PHASE1_...md` → `PHASE2_...md` → `PHASE3_...md` →
`PHASE4_...md` in order. Every phase is a real, compilable increment that
leaves the engine building correctly end-to-end and adds/edits real
`.h`/`.cpp`/`CMakeLists.txt`/test files — none of them is "just planning."

**Nothing in this campaign has been implemented yet** — every "today, line N"
citation across all four child phase documents was independently re-verified
directly against the live source tree during this v2 pass
(`src/Editor/Selection.h/.cpp`, `src/Editor/BoneViewerWindow.h/.cpp`,
`src/Editor/Panels/InspectorPanel.cpp`, `src/Assets/PhysicsData.h`,
`src/Editor/RigidBodyWireframe.h`, `src/Editor/ModelRigCache.h`, the root
`CMakeLists.txt`, `tests/CMakeLists.txt`) and is still byte-for-byte accurate —
no phase document's file/line/signature citations needed correcting. See
"Revision Notes (v2)" below for what DID need fixing.

## Campaign file map

| File | Delivers |
|---|---|
| `PHASE0_MASTER_STRATEGY.md` | This document — root-cause analysis + ordering + the v2 self-audit findings. |
| `PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md` | Rewrites `src/Editor/Selection.h/.cpp` so the Model-Part selection is a SET of indices instead of one — `SelectModelParts()`, `ToggleModelPartInSelection()`, `SelectedModelPartIndices()` — the actual "Selection class need to support multiple objects selection" ask, plus updated/extended `tests/Editor/SelectionTests.cpp` (with one v2-added coverage gap closed). |
| `PHASE2_RIGID_BODY_GROUP_FIELD_AND_ADJACENCY_ALGORITHMS.md` | New `src/Editor/RigidBodyGroupSelection.h/.cpp` — pure, Tier-1-tested `BuildRigidBodyAdjacency()`/`SelectRigidBodiesByGroup()`/`SelectRigidBodyBranch()`; `BoneViewerWindow::RigidBodyEntry` gains the missing `group` field; a new joint-adjacency cache (`m_rigidBodyAdjacency`) is rebuilt alongside the existing rig data. Covered by `tests/Editor/RigidBodySelectionAlgorithmsTests.cpp`. |
| `PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md` | `src/Editor/BoneViewerWindow.cpp` — two new toolbar buttons ("Select All (Group)" / "Select All (Branch)") that call Phase 1 + Phase 2's new APIs, plus Ctrl-click (toggle)/Shift-click (genuine Windows-Explorer-style contiguous range select, v2) extend-selection wiring on every existing click path (tree row, flat row, direct viewport dot). Also adds a small new pure module, `src/Editor/FlatListRangeSelection.h/.cpp` (v2), and fixes a v1 correctness bug in the two buttons' own "is there a single seed?" check. |
| `PHASE4_INSPECTOR_MULTI_SELECTION_SUMMARY.md` | `src/Editor/Panels/InspectorPanel.cpp` — `BuildModelPartInspector()` learns to show a compact "N Rigid Bodies Selected" list instead of a single stale property sheet whenever more than one Model Part is selected, plus final regression-safety test additions. Unchanged since v1 — this v2 self-audit found nothing to fix here. |

## Revision Notes (v2 — second-iteration self-audit)

All four child phase documents were re-read end-to-end against the CURRENT
source tree, hunting specifically for gaps, incorrectness, insufficiency,
missing pieces, and worth-adding QoL content, before any implementation
began. Six concrete findings came out of this pass, fixed directly in the
phase document(s) each belongs to:

1. **PHASE3 — a real correctness bug in the two buttons' own "is there a
   single seed to act on?" check.** v1's `hasSeed` boolean only verified that
   `SelectedModelPartIndex() >= 0` and in range — but `SelectedModelPartIndex()`
   (Phase 1's own accessor) is defined as "the LOWEST currently-selected
   index," which is still a valid, in-range value even when SEVERAL rigid
   bodies are currently selected (e.g. right after clicking "Select All
   (Group)" once already, or after a v2 Shift-range-select spanning several
   rows). That means v1's buttons would silently stay ENABLED and reseed
   themselves from the lowest of a multi-item selection instead of being
   disabled — directly contradicting this same document's own Step 1 goal #1
   ("both buttons disabled ... unless there is currently a SINGLE rigid body
   selected") and the story's own singular "pick everything with same group
   [as **the** selected one]" phrasing. Fixed: `hasSeed` now additionally
   requires `SelectedModelPartIndices().size() == 1` — see PHASE3's own
   updated Step 3.2.
2. **PHASE1/PHASE3 — the "Ctrl/Shift-click ... Windows Explorer/Unity style"
   framing overclaimed what v1 actually built.** Real Windows Explorer/Unity
   list selection has Ctrl-click TOGGLE one item and Shift-click perform a
   genuine contiguous RANGE select from a remembered anchor to the clicked
   item — two different operations. v1 treated Ctrl and Shift as identical
   aliases for the same single-item toggle, then labeled that "Windows
   Explorer/Unity style" — a real terminology/behavior gap for anyone
   actually trying to use this feature the way its own documentation
   describes it, and a missed, easy QoL win given "Selection class need to
   support multiple objects selection" is the campaign's own headline ask.
   Fixed: PHASE3 now implements genuine anchor-based Shift range-select for
   the two FLAT, linearly-ordered selection surfaces (Rigid Body/Joint list
   rows, and the direct viewport-dot click) via a new tiny pure module,
   `src/Editor/FlatListRangeSelection.h/.cpp`'s `BuildInclusiveIndexRange()`
   (Tier-1-tested, `tests/Editor/FlatListRangeSelectionTests.cpp`) — Ctrl
   still toggles, exactly as v1 designed. The Bone tree (`RenderBoneTreeNode()`)
   deliberately KEEPS v1's "Ctrl and Shift both toggle" behavior, with its own
   comment now explaining why (a bone's raw array index has no meaningful
   linear "range" to a user looking at an indented hierarchy the way a flat
   Rigid Body/Joint row list does) rather than silently misdescribing itself
   as Explorer-style.
3. **PHASE3 — no user feedback before/while clicking either button.** v1's
   two buttons give zero indication, before clicking, of how many rigid
   bodies a click will actually select, and zero indication that clicking
   "Select All (Branch)" on a rigid body that is ITSELF a branch/junction
   (degree ≥ 3) only ever reselects that one body (a documented, correct, but
   silently surprising edge case — see PHASE2's own
   `SelectBranchFromABranchNodeItselfReturnsOnlyTheSeed` test). Fixed:
   PHASE3's Step 3.2 now adds a hover tooltip on each button previewing the
   real match count (computed via the exact same Phase 2 functions the click
   handler itself calls), and an explicit "(this rigid body is itself a
   branch/junction — nothing to expand)" tooltip variant for the Branch
   button in that specific case.
4. **PHASE1 — a real Tier-1 coverage gap in `ToggleModelPartInSelection()`'s
   own test suite.** Every v1 test for the "starts a fresh selection instead
   of extending" branch only ever exercises a PRE-EXISTING, but INCOMPATIBLE,
   ModelPart selection (a different `owningEntity`, or a different
   `partKind`) — none of them ever start from `Kind() == InspectorSelectionKind::None`
   (nothing selected yet) or `Kind() == InspectorSelectionKind::Entity`
   (Hierarchy currently has the selection) at all, both of which take the
   exact same `m_kind != InspectorSelectionKind::ModelPart` branch in the
   real implementation but were never independently asserted. Per `AGENTS.md`'s
   "Every change to Tier 1 code must come with a matching test change" rule,
   this is exactly the kind of untested branch that rule exists to catch.
   Fixed: PHASE1's Step 3.8 gains one new test,
   `ToggleModelPartInSelectionStartsAFreshSelectionWhenNothingOrAnUnrelatedKindWasSelected`.
5. **PHASE2 — a small documentation-precision nit, no functional change.**
   `SelectRigidBodyBranch()`'s own doc comment says it "walks outward ... in
   both directions," which reads like a breadth-first search: the real
   implementation is an explicit `std::vector`-backed stack (LIFO
   `pending.back()`/`pop_back()`), i.e. depth-first order. This has zero
   effect on the actual returned RESULT (`std::sort()`ed before returning,
   so the final member set is traversal-order-independent — confirmed by
   re-reading the algorithm and every one of PHASE2's own existing tests),
   but a future reader who assumes queue/BFS-style "distance from seed"
   ordering from that wording would be misled if a later change ever wanted
   to reason about visitation order specifically. Fixed: PHASE2's own header
   doc comment for `SelectRigidBodyBranch()` now says "graph walk" instead of
   implying a specific (BFS) traversal order, and calls out that the
   traversal order is deliberately unspecified/irrelevant since the result is
   always sorted.
6. **PHASE4 — confirmed unchanged.** Re-read against the live
   `InspectorPanel.cpp` line-for-line (today's lines 88-294): every citation
   still matches exactly, the multi-selection branch's own scope (index +
   name only, no per-part detail fields, no editing) is still correct and
   sufficient, and it composes correctly with both v1's button-driven
   multi-selection AND v2's new Shift-range-select — a multi-selection
   produced either way is just "more than one index in
   `SelectedModelPartIndices()`" from `BuildModelPartInspector()`'s own point
   of view, so nothing about how the selection was BUILT needs to leak into
   how it's SUMMARIZED. No changes made to `PHASE4_INSPECTOR_MULTI_SELECTION_SUMMARY.md`.

Nothing else in this campaign's four phase documents needed a substantive
change — every other Culprit's own file/field/signature citation, every
"What We Will NOT Do" scope limit, and the overall phase ordering all held up
against the live source tree exactly as originally written.

## Step 1: The Goal (Where are we going?)

Per the story, in this exact order:

1. **`Selection` (`src/Editor/Selection.h/.cpp`) must support selecting
   MULTIPLE objects at once**, not just one — today it hard-codes "at most
   one Model-Part index" into its own data model (`int m_modelPartIndex`).
   This must become a genuine set, with clean APIs for "replace the whole
   selection with this set" (what a "Select All ..." button needs), "add/
   remove one item from the existing set" (what Ctrl-click needs), and — as
   of this v2 pass — a genuine contiguous range built from a remembered
   anchor (what a real Shift-click needs), while every existing
   single-selection call site in the codebase (bone tree clicks, asset/entity
   selection, the Inspector's single-part property sheet) keeps compiling and
   behaving exactly as it does today.
2. **The Bone Viewer's Rigid Body view gets two new "select all" toolbar
   buttons**, seeded from whichever ONE rigid body is currently selected (and
   ONLY when the selection is genuinely exactly one rigid body — see
   Revision Notes finding #1 above):
   - **"Select All (Group)"**: selects every rigid body in the whole model
     that shares that rigid body's own PMX collision `group` value
     (`RigidBody::group`, `src/Assets/PhysicsData.h`) — a flat, non-graph
     property comparison.
   - **"Select All (Branch)"**: walks the graph of rigid bodies connected to
     each other via Joints (`Joint::rigidBodyAIndex`/`rigidBodyBIndex`,
     same file), starting from the seed body, expanding outward in both
     directions along the chain, and STOPPING at (excluding) any rigid body
     that is itself a **branch/junction** — one connected to three or more
     other rigid bodies at once. Per the story's own diagram:
     ```
     +--A--+
     |     |
     ```
     every rigid body making up the `A` segment gets selected; both `+`
     junction endpoints are excluded from the result.

## Step 2: The Situation / The Problem (Where are we now?)

A close read of `src/Editor/Selection.h/.cpp`, `src/Editor/BoneViewerWindow.h/.cpp`,
`src/Editor/Panels/InspectorPanel.cpp`, and `src/Assets/PhysicsData.h` found
the exact root causes — the "culprits":

1. **Culprit A — `Selection` physically CANNOT hold more than one Model-Part
   index.** Its private state is:
   ```cpp
   Entity m_modelPartEntity = kInvalidEntity;
   ModelPartKind m_modelPartKind = ModelPartKind::Bone;
   int m_modelPartIndex = -1;
   ```
   and `SelectModelPart(Entity, ModelPartKind, int partIndex)` unconditionally
   OVERWRITES `m_modelPartIndex` with exactly one value; `IsModelPartSelected(...)`
   does an exact `==` comparison against that one stored value. Even if
   Phase 2/3's group/branch algorithms below correctly computed "these 6
   rigid bodies belong together," there is currently no way to hand that
   whole list to `Selection` and have more than the LAST one actually stay
   selected/highlighted. **This is the actual, single blocking culprit for
   the entire story** — every other gap below is secondary to it, because
   even a perfect group/branch algorithm is useless without somewhere to put
   its answer. Fixed in Phase 1, first, before anything else.
2. **Culprit B — `RigidBody::group` (`src/Assets/PhysicsData.h`, PMX collision
   group 0-15) is decoded by `PmxLoader.cpp` into `PhysicsData` correctly, but
   `BoneViewerWindow::RigidBodyEntry` (its own flattened, overlay-facing copy —
   see `BoneViewerWindow.h`) never carries it through.** `EnsureDataLoaded()`
   builds each `RigidBodyEntry` from only
   `{ name, translate, rotateRadians, shape, shapeSize, boneIndex }` — `group`
   is silently dropped. "Select all group" has no data to compare against
   today; this is a one-field data-plumbing gap, fixed in Phase 2.
3. **Culprit C — there is no adjacency/graph structure anywhere that turns
   "a flat list of Joints, each naming two rigid-body indices" into "which
   rigid bodies does body N directly touch."** `BoneViewerWindow::m_joints`
   stores `JointEntry{ name, translate, rigidBodyAIndex, rigidBodyBIndex }`
   per joint (a Joint-centric view), used today only to draw two connector
   lines per joint (`Build()`'s Joint-mode overlay block). Nothing builds the
   REVERSE, rigid-body-centric view ("who is body N connected to") that a
   branch-walk needs. Fixed in Phase 2, as a small, pure, Tier-1-testable
   graph module — never reinvented ad hoc inside `BoneViewerWindow.cpp`
   itself, matching this codebase's own established convention of putting
   graph/geometry logic in a standalone pure module next to the ImGui code
   that calls it (see `RigidBodyWireframe.h`, added by
   `task_manager/verlet-integration-3`, for the exact same pattern applied to
   a different problem).
4. **Culprit D — no button, and no algorithm, exists anywhere to actually
   perform "select all in group" / "select all in branch."** Fixed in
   Phase 2 (the pure algorithms) + Phase 3 (the toolbar buttons that call
   them and feed `Selection::SelectModelParts()`).
5. **Culprit E — every existing click path in `BoneViewerWindow.cpp`
   (`RenderBoneTreeNode()`, `RenderFlatPartRow()`, and the direct
   viewport-dot click inside `Build()`) unconditionally calls
   `ctx.selection.SelectModelPart(...)`** — a plain click always REPLACES
   the whole selection with one item, with no Ctrl/Shift-click path to
   EXTEND it. Without this, `Selection` supporting multiple objects would
   only ever be reachable through the two new buttons, never through direct
   interactive picking — a weak, incomplete reading of "Selection class need
   to support multiple objects selection." Fixed in Phase 3, alongside the
   two buttons, using Phase 1's new `ToggleModelPartInSelection()` for
   Ctrl-click, and (as of this v2 pass) a new, genuine, anchor-based
   contiguous range select for Shift-click on the two flat (Rigid Body/Joint)
   surfaces — see Revision Notes finding #2 above and PHASE3's own updated
   Step 3.3/3.6.
6. **Culprit F — `InspectorPanel.cpp`'s `BuildModelPartInspector()` reads
   only `ctx.selection.SelectedModelPartIndex()` (a single int) and renders
   ONE part's full property sheet.** The moment Phase 1-3 land, this
   function will silently keep showing only the FIRST of many now-selected
   rigid bodies, with no indication to the user that more are actually
   selected (a misleading regression, not a crash). Fixed in Phase 4.
7. **What is already correct and needs NO change**, confirmed by this same
   read: `Selection::IsModelPartSelected()`'s CALL SITES (the per-part
   dot-color / tree-row-highlight logic in `BoneViewerWindow.cpp`, and the
   `if (isSelected) { ... }` wireframe-reveal block added by
   `verlet-integration-3`) already call it PER-PART, in a loop, once per
   rigid body/bone/joint index — they never assume "at most one true result
   across the whole loop." This means once `IsModelPartSelected()`'s
   internal check becomes "is `partIndex` a MEMBER of the current set"
   instead of "does it equal the one stored value," every one of these
   existing call sites starts correctly multi-highlighting/multi-revealing
   automatically, with ZERO changes needed to the drawing loop itself. This
   is exactly why Phase 1 is scoped to `Selection` alone.

## Step 3: The Plan (How will we get there?)

Execute the four phases strictly in order — each depends on the previous:

1. **Phase 1** rewrites `Selection`'s Model-Part storage from a single `int`
   to a de-duplicated, sorted `std::vector<int>`, adding `SelectModelParts()`
   (replace-the-whole-set) and `ToggleModelPartInSelection()` (Ctrl-click
   add/remove), and a new `SelectedModelPartIndices()` accessor —
   `SelectModelPart()`/`SelectedModelPartIndex()` become thin single-element
   wrappers so every pre-existing call site keeps compiling unchanged.
   (v2: one additional test closing a coverage gap — see Revision Notes
   finding #4.)
2. **Phase 2** adds `RigidBody::group` to `BoneViewerWindow::RigidBodyEntry`
   and a new pure module, `RigidBodyGroupSelection.h/.cpp`, with
   `BuildRigidBodyAdjacency()`, `SelectRigidBodiesByGroup()`, and
   `SelectRigidBodyBranch()` — each independently Tier-1-tested against the
   story's own `+--A--+` topology. (v2: one doc-comment wording fix — see
   Revision Notes finding #5.)
3. **Phase 3** wires two new `BoneViewerWindow` toolbar buttons to Phase 1 +
   Phase 2's APIs, and updates every existing selection click path to
   support Ctrl-click (toggle) and Shift-click (v2: genuine contiguous range
   select on the two flat surfaces, via a new `FlatListRangeSelection.h/.cpp`
   pure module) extension. (v2: also fixes the `hasSeed` correctness bug and
   adds preview tooltips — see Revision Notes findings #1/#3.)
4. **Phase 4** updates `InspectorPanel::BuildModelPartInspector()` to show a
   correct multi-selection summary instead of a stale single-part sheet, and
   closes out the campaign with final regression-safety test additions.
   (v2: confirmed unchanged — see Revision Notes finding #6.)

## Step 4: What We Will NOT Do (Focus)

- We will **not** touch Hierarchy-entity (`Selection::SelectEntity()`) or
  Project-asset (`Selection::SelectAsset()`) selection at all — both stay
  exactly the single-selection model they are today. The story's own request
  is scoped to "Selection class need to support multiple objects selection"
  in service of the Bone Viewer's Rigid Body buttons; there is no request
  (and no existing UI) for multi-selecting entities in "Hierarchy" or assets
  in "Project" in this campaign.
- We will **not** add a physics simulation backend, or make `RigidBody`/
  `Joint` data mutable/editable — this campaign is entirely about WHICH
  already-existing, already-decoded rigid bodies get selected/highlighted,
  never about simulating or editing their physical properties.
- We will **not** implement box/lasso/rectangle-drag multi-select in the 3D
  viewport — only Ctrl-click/Shift-click (per-dot, per-row) and the two
  "Select All ..." buttons. A drag-select rectangle is a reasonable future
  follow-up but is not named by the story and adds a new input-handling
  surface this campaign does not need.
- We will **not** extend the "Select All (Group)"/"Select All (Branch)"
  buttons to Bone or Joint view modes — the story is specific to "Bone
  Viewer Rigid body views," and `RigidBody::group` / joint-adjacency are
  concepts that only meaningfully exist for rigid bodies (bones have no
  `group` field, and joints connect rigid bodies, not each other).
- We will **not** change how the Bone Viewer's wireframe-reveal-on-select
  block (`verlet-integration-3`'s own addition) computes or draws a
  wireframe — it already iterates its OWN `isSelected` check per rigid
  body, so it automatically starts drawing MULTIPLE wireframes once
  multi-selection is live, with no code changes required there beyond
  correcting one now-stale comment (see Phase 3, Step 3.4).
- We will **not** (v2) implement a genuine Shift-range-select for the Bone
  tree (`RenderBoneTreeNode()`) — a bone's raw array index has no meaningful
  linear "range" relative to what's actually visible/expanded in an indented
  hierarchy tree the way a flat Rigid Body/Joint row list does; correctly
  supporting it would require flattening the CURRENTLY VISIBLE/expanded tree
  rows into a linear order first, a meaningfully larger and differently-shaped
  piece of work the story never asked for. The tree keeps v1's "Ctrl and
  Shift both toggle" behavior — see Revision Notes finding #2.

## Step 5: Their Role (What does this mean for you, the implementer?)

Treat each child phase document as a standalone work order: it names the
exact files to add/modify, the exact structs/functions/signatures to write,
the exact existing call sites to touch, and the exact test file to add/extend.
Follow `AGENTS.md`'s "Testability & Regression Safety" rule and land each
phase's new/updated test file in the SAME change as its production code —
do not defer testing to a later phase. Do not reorder the phases: Phase 2's
`RigidBodyGroupSelection.h` and Phase 3's button click handlers both depend
on Phase 1's `Selection::SelectModelParts()` already existing and compiling;
Phase 3's button click handlers depend on Phase 2's algorithms already
existing and compiling; Phase 4's Inspector summary depends on Phase 1's
`SelectedModelPartIndices()` already existing. Build `gte_core` +
`GreatTamanaEngineTests` after each phase and confirm every new/existing
test still passes before starting the next phase.
