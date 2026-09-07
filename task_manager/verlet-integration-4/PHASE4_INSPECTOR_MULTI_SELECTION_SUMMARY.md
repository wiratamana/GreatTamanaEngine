# PHASE4 — Inspector: Multi-Selection Summary (`src/Editor/Panels/InspectorPanel.cpp`)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit F). Depends on: Phase 1's
`Selection::SelectedModelPartIndices()` existing and compiling; functionally
depends on Phase 3's buttons/Ctrl-Shift-click actually being reachable to
produce a multi-item selection to display, though this phase's OWN code only
directly calls Phase 1's API. Produces: the Inspector correctly reflects
"how many, and which" Model Parts are currently selected, closing out the
regression this whole campaign would otherwise silently introduce.

## Step 1: The Goal

1. When exactly ONE Model Part is currently selected (the pre-campaign,
   default case), `InspectorPanel`'s "Model Part" view keeps showing that
   one part's full, existing read-only property sheet — completely
   unchanged behavior for the common case.
2. When TWO OR MORE Model Parts are currently selected (only reachable after
   Phase 3's buttons/Ctrl-Shift-click), the Inspector instead shows a
   compact summary: how many are selected, of which kind (Bones/Rigid
   Bodies/Joints), and a scrollable list naming each one by index + name —
   never a silently-stale single-part property sheet for only the first of
   many.
3. Zero change to the Bone/Rigid Body/Joint single-part detail rendering
   code itself (the big `switch` statement) — this phase only adds a branch
   BEFORE it.

## Step 2: The Situation / The Problem

`InspectorPanel.cpp`'s `BuildModelPartInspector()` (today, lines 88-294)
reads the CURRENT selection as:

```cpp
    const int index = ctx.selection.SelectedModelPartIndex();

    switch (ctx.selection.SelectedModelPartKind()) {
    case ModelPartKind::Bone: {
        ...
    case ModelPartKind::RigidBody: {
        ...
    case ModelPartKind::Joint: {
        ...
    }
```

(today, lines 124-293) — entirely in terms of ONE `index`. The moment
Phase 1-3 land, `ctx.selection.SelectedModelPartIndices()` can return more
than one element, but `SelectedModelPartIndex()` (Phase 1's own
backward-compatible accessor) always returns just the LOWEST of them — so
this function would keep compiling and running, but would silently show only
the first of potentially many selected rigid bodies' full property sheet,
with absolutely no indication to the user that anything else is also
selected. This is a real, user-visible regression this campaign must not
ship without addressing, even though it is not a crash/compile error.

## Step 3: The Plan

### 3.1 `InspectorPanel.cpp` — insert the multi-selection branch

In `BuildModelPartInspector()`, right after the existing rig-load failure
check (today, lines 117-122):

```cpp
    const RigFileData* rig = rigCache.GetOrLoad(source->gtaPath);
    if (rig == nullptr) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to load rig data for:");
        ImGui::TextWrapped("%s", source->gtaPath.c_str());
        return;
    }
```

and BEFORE the existing single-index read (today, line 124):

```cpp
    const int index = ctx.selection.SelectedModelPartIndex();
```

insert:

```cpp
    // Multi-selection summary - as of task_manager/verlet-integration-4
    // (PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md/
    // PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md),
    // Selection can hold MANY Model-Part indices at once (the Bone Viewer's
    // "Select All (Group)"/"Select All (Branch)" toolbar buttons, or
    // Ctrl/Shift-click). A single part's full read-only property sheet
    // below only ever makes sense for exactly ONE selected part - showing
    // it for just the FIRST of many, with no indication anything else is
    // also selected, would be a silent, misleading regression. Whenever
    // more than one index is selected, show a compact "N <Kind>s Selected"
    // summary + name list instead, and return early - the single-part
    // switch below is never reached in that case.
    const std::vector<int>& selectedIndices = ctx.selection.SelectedModelPartIndices();
    if (selectedIndices.size() > 1) {
        const ModelPartKind kind = ctx.selection.SelectedModelPartKind();
        const char* kindNoun = kind == ModelPartKind::Bone ? "Bones" : kind == ModelPartKind::RigidBody ? "Rigid Bodies" : "Joints";
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%zu %s Selected", selectedIndices.size(), kindNoun);
        ImGui::Separator();

        ImGui::BeginChild("InspectorMultiSelectionList", ImVec2(0.0f, 200.0f), true);
        for (const int selectedIndex : selectedIndices) {
            std::string label = "(out of range)";
            switch (kind) {
            case ModelPartKind::Bone:
                if (selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < rig->skeleton.bones.size()) {
                    const std::string& name = rig->skeleton.bones[static_cast<std::size_t>(selectedIndex)].name;
                    label = name.empty() ? "(unnamed)" : name;
                }
                break;
            case ModelPartKind::RigidBody:
                if (selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < rig->physics.rigidBodies.size()) {
                    const std::string& name = rig->physics.rigidBodies[static_cast<std::size_t>(selectedIndex)].name;
                    label = name.empty() ? "(unnamed)" : name;
                }
                break;
            case ModelPartKind::Joint:
                if (selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < rig->physics.joints.size()) {
                    const std::string& name = rig->physics.joints[static_cast<std::size_t>(selectedIndex)].name;
                    label = name.empty() ? "(unnamed)" : name;
                }
                break;
            }
            ImGui::BulletText("[%d] %s", selectedIndex, label.c_str());
        }
        ImGui::EndChild();
        return;
    }
```

(`#include <vector>`/`<string>` are already present at the top of
`InspectorPanel.cpp` — no new includes are required for this edit.)

### 3.2 No other changes

The existing `const int index = ctx.selection.SelectedModelPartIndex();` line
and the entire `switch` statement after it (today, lines 124-293) are
completely UNCHANGED — for the `selectedIndices.size() <= 1` case (zero or
one selected, i.e. every pre-campaign scenario plus the common
single-selection post-campaign case), execution falls through exactly as it
always has, rendering that one part's full property sheet. `index` is still
correctly `-1` (out of range, handled by each `case`'s own existing
`if (index < 0 || ...)` guard) when `selectedIndices` is empty, and correctly
the one selected index when it has exactly one element — both unchanged from
before Phase 1.

## Step 4: What We Will NOT Do

- We will **not** make the multi-selection summary list editable, or add a
  per-row "deselect this one" button — it is a read-only summary; use
  Ctrl/Shift-click in the Bone Viewer itself to adjust the selection, exactly
  like the single-part view is already entirely read-only today (see
  `BuildModelPartInspector()`'s own doc comment: "there is no physics
  simulation anywhere in the engine that consumes RigidBody/Joint data yet").
- We will **not** show any per-part DETAIL fields (shape/mass/translate/...)
  in the multi-selection summary — only index + name, matching the "just
  tell me what's selected" purpose of a multi-selection summary in
  Unity/other DCC tools, which also collapse to a compact multi-object view
  rather than trying to show N full property sheets stacked on top of each
  other.
- We will **not** add a "Select Owning Entity" button to the multi-selection
  branch — it stays only in the existing single-part view (`owningEntity` is
  already unambiguous and shared across every selected part regardless of
  count, but jumping to it mid-multi-select isn't a requirement here); the
  existing single-part branch's own such button is untouched.

## Step 5: Their Role

Implementer checklist for this phase (the final phase of this campaign):

1. Apply 3.1 to `InspectorPanel.cpp` exactly as written.
2. Build `gte_core` + `GreatTamanaEngineTests` and confirm the full suite —
   every `SelectionTest.*` (Phase 1), `RigidBodyGroupSelectionTest.*`
   (Phase 2), and every pre-existing test — still passes.
3. Build the real `GreatTamanaEngine` executable and, against a real
   imported MMD model, confirm end-to-end:
   - Selecting exactly one rigid body (plain click, or after a "Select All
     ..." action that happens to resolve to just one body) shows the
     existing full property sheet (Name/Attached Bone/Shape/Motion Type/
     Collision Group/Collision Mask/Shape Size/Translate/Rotate/Mass/
     Damping/Restitution/Friction) completely unchanged from before this
     campaign.
   - Clicking "Select All (Group)" or "Select All (Branch)" on a body whose
     group/branch actually contains more than one member instead shows
     "N Rigid Bodies Selected" plus a scrollable, correctly-indexed/-named
     list of exactly those bodies.
   - Ctrl/Shift-clicking down to exactly one remaining selected body
     switches the Inspector back to the full single-part property sheet for
     that one body, live, the next frame.
   - The same multi-selection summary behavior holds if a future caller
     ever produces a multi-Bone or multi-Joint selection too (this phase's
     `kind`-branching summary code handles all three `ModelPartKind` values
     identically, even though only Rigid Body currently has a UI path that
     produces more than one selected index — see
     `PHASE0_MASTER_STRATEGY.md`'s "What We Will NOT Do" for why Bone/Joint
     multi-select buttons themselves are out of scope, while the Inspector
     summary itself is written generically rather than Rigid-Body-only).
4. This closes out the campaign — re-read `PHASE0_MASTER_STRATEGY.md`'s
   Culprit list (A through F) once more and confirm every single one has a
   corresponding fix landed in Phases 1-4 before considering this campaign
   complete.
