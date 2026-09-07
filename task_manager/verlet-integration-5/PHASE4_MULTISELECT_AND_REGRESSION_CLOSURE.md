# PHASE4 — Multi-Selection Summary Fix, "Select All (Chain)", and Regression Closure

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit B, remaining part). Depends on:
Phase 1 (`ModelPartKind::Verlet`), Phase 2 (Bone Viewer can select/
multi-select Verlet joints), Phase 3 (the single-selection Inspector
branch already exists, so this phase's own multi-selection fix has a
correct single-selection sibling to fall back to once a user narrows back
down to one). Produces: a correct 4-way Inspector multi-selection summary
(closing the last silent "falls through to the wrong label" gap this
campaign's own `ModelPartKind` extension opened), a "Select All (Chain)"
toolbar convenience button in the Bone Viewer (mirroring Rigid Body's
existing "Select All (Group)"/"Select All (Branch)" buttons), and the full,
final cross-mode manual regression pass that closes this campaign out.

## Step 1: The Goal

1. Fix `InspectorPanel.cpp`'s existing multi-selection summary (shown
   whenever more than one Model-Part index is selected at once) so a
   multi-selected set of Verlet joints is labeled and listed correctly,
   instead of silently falling through to whatever the previous last
   `ModelPartKind` branch produces.
2. Add a small, genuinely useful "Select All (Chain)" button to the Bone
   Viewer's toolbar, visible only in Verlet mode, that selects every joint
   of whichever chain the currently-selected joint belongs to — the Verlet
   equivalent of Rigid Body mode's existing "Select All (Group)"/"Select
   All (Branch)" buttons, built entirely from primitives (`Selection::
   SelectModelParts()`, Phase 1's `FindDynamicChainJointByBoneIndex()`)
   that already exist by this point in the campaign.
3. Run, and record the results of, the full manual regression checklist
   across all four modes — the final acceptance gate for this whole
   campaign.

## Step 2: The Situation / The Problem

`InspectorPanel.cpp`'s multi-selection summary block (inside
`BuildModelPartInspector()`, guarded by `if (selectedIndices.size() > 1)`)
was written, like every other exhaustive switch this campaign's own Master
Strategy document flags under Culprit B, assuming exactly 3 `ModelPartKind`
values:

```cpp
const char* kindNoun = kind == ModelPartKind::Bone ? "Bones" : kind == ModelPartKind::RigidBody ? "Rigid Bodies" : "Joints";
...
switch (kind) {
case ModelPartKind::Bone: /* ... */ break;
case ModelPartKind::RigidBody: /* ... */ break;
case ModelPartKind::Joint: /* ... */ break;
}
```

Both the `kindNoun` ternary chain and the inner `switch`'s per-row name
lookup silently treat any 4th value (`ModelPartKind::Verlet`) as if it were
`Joint` (the ternary's final `else`) with no matching inner `case` at all
(the `switch` itself compiles fine — C++ does not require exhaustiveness —
but produces the `label = "(out of range)"` fallback the loop already
initializes for every unmatched row). Left unfixed, Ctrl-clicking several
Verlet joints in Phase 2's own Bone Viewer and then looking at the
Inspector would show "N Joints Selected" (wrong noun) followed by a list of
rows all reading "(out of range)" (wrong per-row names) — a real,
user-visible regression this campaign itself introduces if left unaddressed,
not a pre-existing bug.

Separately, Rigid Body mode's own "Select All (Group)"/"Select All
(Branch)" toolbar buttons (`verlet-integration-4`) proved genuinely useful
for quickly multi-selecting a structurally-related set of parts. Verlet
mode has an equally natural, even simpler, structurally-related set: "every
joint of the same chain as this one" — worth the same one-button
convenience, now that Phase 1's lookup helper and `Selection::
SelectModelParts()` (already built by `verlet-integration-4`) make it a
small, mechanical addition.

## Step 3: The Plan

### 3.1 `InspectorPanel.cpp` — 4-way `kindNoun` and multi-selection row switch

```cpp
// Was: const char* kindNoun = kind == ModelPartKind::Bone ? "Bones" : kind == ModelPartKind::RigidBody ? "Rigid Bodies" : "Joints";
const char* kindNoun = kind == ModelPartKind::Bone ? "Bones"
    : kind == ModelPartKind::RigidBody ? "Rigid Bodies"
    : kind == ModelPartKind::Joint      ? "Joints"
                                        : "Verlet Joints";
```

Add the matching 4th `case` to the per-row label switch inside the same
multi-selection block:

```cpp
case ModelPartKind::Verlet:
    if (selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < rig->skeleton.bones.size()) {
        const std::string& name = rig->skeleton.bones[static_cast<std::size_t>(selectedIndex)].name;
        label = name.empty() ? "(unnamed)" : name;
    }
    break;
```

(This mirrors the existing `ModelPartKind::Bone` case exactly, byte for
byte — correct, since a Verlet joint's `partIndex` IS a bone index, per
Phase 1's own decision; the row simply shows that bone's name, same as Bone
mode's own multi-selection row would for the same index.)

### 3.2 `src/Editor/BoneViewerWindow.cpp` — "Select All (Chain)" button

Add a new toolbar block, alongside (and following the same
`ImGui::BeginDisabled(!hasSeed)` structure as) the existing Rigid-Body-only
"Select All (Group)/(Branch)" block, this one gated on `m_viewMode ==
ModelPartKind::Verlet`:

```cpp
if (m_viewMode == ModelPartKind::Verlet) {
    const bool hasSeed = ctx.selection.Kind() == InspectorSelectionKind::ModelPart
        && ctx.selection.SelectedModelPartEntity() == m_targetEntity
        && ctx.selection.SelectedModelPartKind() == ModelPartKind::Verlet
        && ctx.selection.SelectedModelPartIndices().size() == 1;

    // Only meaningful when hasSeed is true - guarded accordingly below,
    // exactly like Rigid Body mode's own seed/location pattern.
    const DynamicChainJointLocation seedLocation = (hasSeed && verletModel != nullptr)
        ? FindDynamicChainJointByBoneIndex(verletModel->chains, ctx.selection.SelectedModelPartIndex())
        : DynamicChainJointLocation{};

    ImGui::BeginDisabled(!hasSeed || !seedLocation.IsValid());
    if (ImGui::Button("Select All (Chain)")) {
        const DynamicChainDefinition& chain = verletModel->chains[static_cast<std::size_t>(seedLocation.chainIndex)];
        std::vector<int> indices(chain.jointBoneIndices.begin(), chain.jointBoneIndices.end());
        ctx.selection.SelectModelParts(m_targetEntity, ModelPartKind::Verlet, std::move(indices));
    }
    if (hasSeed && seedLocation.IsValid() && ImGui::IsItemHovered()) {
        const DynamicChainDefinition& chain = verletModel->chains[static_cast<std::size_t>(seedLocation.chainIndex)];
        ImGui::SetTooltip("Selects all %zu joints of Chain %d.", chain.jointBoneIndices.size(), seedLocation.chainIndex);
    }
    ImGui::EndDisabled();
    if (!hasSeed || !seedLocation.IsValid()) {
        ImGui::SameLine();
        if (ctx.selection.SelectedModelPartIndices().size() > 1) {
            ImGui::TextDisabled("(select exactly one Verlet joint first - %zu are currently selected)",
                ctx.selection.SelectedModelPartIndices().size());
        } else {
            ImGui::TextDisabled("(select a Verlet joint first)");
        }
    }
}
```

This block needs `#include "../Physics/DynamicChainDefinition.h"`'s
`FindDynamicChainJointByBoneIndex()`/`DynamicChainJointLocation` — already
added to this file by Phase 2 (3.2 of that document), no new include
required here.

Place this block immediately after the existing Rigid-Body-only "Select
All (Group)/(Branch)" `if` block (both blocks are mutually exclusive on
`m_viewMode`, so their relative order does not matter functionally — keep
them adjacent for readability, matching how the toolbar already groups
mode-specific controls together).

### 3.3 Full cross-mode manual regression checklist

Run this checklist end to end, on at least one real imported MMD model
with PMX physics-driven bones (ideally one with more than one independent
chain, e.g. twin-tails/hair AND a skirt, to exercise multi-chain selection):

1. **Bone mode** — unchanged from before this whole campaign: tree/gizmo
   look identical, single-click/Ctrl-click/double-click-recenter all work,
   Inspector's single-Bone section shows correctly, multi-selecting several
   bones shows "N Bones Selected" with correct per-row names.
2. **Rigid Body mode** — unchanged: dots/wireframes/connector lines to
   bones, "Select All (Group)"/"Select All (Branch)" both still work
   exactly as before, Inspector single/multi sections both correct.
3. **Joint mode** — unchanged: dots/connector lines to both rigid bodies,
   Inspector single/multi sections both correct.
4. **Verlet mode** (this campaign's own new surface):
   - Tree pane groups joints correctly under each chain's own header,
     search filter correctly hides whole chains with no matching joint and
     correctly narrows a chain's own joint list otherwise.
   - Viewport shows magenta/pink particle dots, amber connector lines
     root-to-tip per chain, gray square root/anchor markers, and (for any
     chain with `hasHeadCollider` set via either Inspector section) a dim
     magenta wireframe sphere at the configured collider bone/radius.
   - Single-click/Ctrl-click both work identically between tree row and
     viewport dot; Shift-click on either falls back to toggle (never a
     spurious wide range-select including non-joint bones — confirm by
     deliberately Shift-clicking two joints from DIFFERENT chains and
     checking the selection is exactly `{those two}`, not a numeric range
     spanning unrelated bones in between).
   - Double-clicking a TREE ROW recenters the orbit camera on that joint
     (mirrors every other mode's own row double-click — `RenderFlatPartRow()`,
     reused unmodified for Verlet's own rows per Phase 2's 3.5). v2 accuracy
     fix (see `PHASE0_MASTER_STRATEGY.md`'s "Revision Notes (v2)", finding
     #2): do NOT also expect double-clicking a viewport DOT to recenter the
     camera — verified directly against `BoneViewerWindow.cpp`'s own direct-
     viewport-click handler (`Build()`'s `hoveredPartIndex` branch): it has
     NO `ImGui::IsMouseDoubleClicked()` check at all today, for ANY of the
     three pre-existing modes (only `RenderBoneTreeNode()`/`RenderFlatPartRow()`,
     i.e. tree/list ROWS, recenter on double-click). This is a real,
     pre-existing gap shared identically by Bone/Rigid Body/Joint mode, not
     something Phase 2/3 regressed for Verlet specifically — do not chase it
     as a Verlet-mode bug during this checklist, and do not silently "fix"
     it as an uncoordinated scope-creep addition here either; if a uniform
     "double-click any mode's viewport dot to recenter" feature is ever
     wanted, it is a separate, deliberate follow-up campaign touching all
     four modes' shared click-handling block at once, not a one-mode patch.
   - "Select All (Chain)" selects exactly that joint's own chain's full
     joint set, disabled/greyed with an explanatory tooltip-free disabled
     hint whenever zero or more-than-one joint is currently selected.
   - Single-selecting a Verlet joint shows Phase 3's new Inspector section
     with correct data; editing its sliders there is reflected immediately
     in both the generic "Dynamic Chain Physics" section AND (for
     damping/stiffness/mass — cosmetically invisible in a static bind-pose
     view, but the underlying data is provably the same object) does not
     desync between the two.
   - Multi-selecting several Verlet joints shows "N Verlet Joints Selected"
     with correct per-row bone names (Phase 4's own fix, 3.1) — the
     regression this phase exists to close.
   - A model with ZERO detected chains shows the correct empty-state
     message in the tree pane, toolbar warning text, and (attempting to
     select nothing) never crashes.
5. **Cross-mode**: switching the "View" dropdown among all four modes
   repeatedly, and reloading a different model entirely while the Bone
   Viewer stays open, never crashes/asserts regardless of which mode was
   active at the moment of the switch/reload, and the Model-Part selection
   correctly clears whenever the underlying entity's data genuinely reloads
   (pre-existing `ClearModelPartIfEntity()` behavior — confirm it still
   fires correctly for a Verlet selection too, since that call is generic
   over `ModelPartKind` and needed no change anywhere in this campaign).

## Step 4: What We Will NOT Do

- We will **not** add a "Select All (Group)"-equivalent for Verlet mode
  based on any concept other than "same chain" — Verlet joints have no PMX
  collision-group field at all (that concept belongs only to `RigidBody`),
  so "chain membership" is the one, sufficient, natural grouping.
- We will **not** revisit `Selection.cpp`'s own implementation in this
  phase — every fix here lives entirely in `InspectorPanel.cpp` (the label/
  switch fix) and `BoneViewerWindow.cpp` (the new button), `Selection`'s own
  methods remain byte-for-byte unchanged from Phase 1 onward.
- We will **not** treat this checklist as optional or partially-run — every
  item in 3.3 must be confirmed before considering this campaign complete,
  per the Master Strategy's own closing instruction that phases must not be
  reordered or skipped.

## Step 5: Their Role

1. Edit `InspectorPanel.cpp`'s `kindNoun` ternary and multi-selection
   `switch` per 3.1.
2. Edit `BoneViewerWindow.cpp`'s toolbar to add the "Select All (Chain)"
   block per 3.2.
3. Build `GreatTamanaEngine` and run the FULL checklist in 3.3, on a real
   model, item by item, fixing forward (not by reopening an earlier phase's
   own document) any small discrepancy found — this phase is this
   campaign's final acceptance gate.
