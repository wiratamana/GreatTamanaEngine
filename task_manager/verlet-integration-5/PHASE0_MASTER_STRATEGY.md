# PHASE0 — MASTER STRATEGY: A 4th Bone-Viewer Mode, "Verlet" (v1)

Orchestrator document for the `verlet-integration-5` campaign. Every child
phase document in this folder implements one slice of this plan. Read this
file first, then execute `PHASE1_...md` → `PHASE4_...md` strictly in order —
each phase's code depends on the previous phase's deliverables already
compiling. Every phase below is a real, compilable, testable increment that
adds or edits real `.h`/`.cpp` files; none of them are "just planning."

## Campaign file map

| File | Delivers |
|---|---|
| `PHASE0_MASTER_STRATEGY.md` | This document — root-cause analysis + ordering. |
| `PHASE1_CHAIN_LOOKUP_AND_SELECTION_FOUNDATION.md` | `Selection.h` gains `ModelPartKind::Verlet` (the 4th selectable category); `src/Physics/` gains a small, pure, Tier-1-tested `FindDynamicChainJointByBoneIndex()` helper that both later phases share. Zero ECS/Editor/GPU dependency. |
| `PHASE2_BONE_VIEWER_VERLET_MODE_TREE_AND_GIZMO.md` | The user-visible core of this campaign: `BoneViewerWindow` gains a "Verlet" toolbar option, a chain-grouped tree pane, and a viewport gizmo (particle dots, chain connector lines, a pinned-root anchor marker, and an optional head-collider wireframe sphere) — sourced from the SAME already-running `PhysicsSystem::DynamicChainRigCache` the Inspector's existing "Dynamic Chain Physics" section already reads/edits, never a second, independently-detected copy. |
| `PHASE3_INSPECTOR_VERLET_JOINT_SECTION.md` | A new "Verlet Joint" Model-Part Inspector section — selecting one gizmo particle shows/edits that exact joint's damping/stiffness/mass/rest-length/head-collider fields in place, reusing `PhysicsSystem::GetDynamicChainRigCache()`'s existing live-edit contract. |
| `PHASE4_MULTISELECT_AND_REGRESSION_CLOSURE.md` | Closes two real gaps a naive "just add a 4th enum value" approach would otherwise silently leave behind: the Inspector's existing multi-selection summary switch/label (today hard-coded to exactly 3 kinds) and a "Select All (Chain)" convenience button mirroring Rigid Body's own "Select All (Group)/(Branch)" — plus the full manual cross-mode regression pass. |

## Step 1: The Goal (Where are we going?)

`BoneViewerWindow` (`src/Editor/BoneViewerWindow.h/.cpp`) today shows exactly
three categories of an imported MMD model through its "View" toolbar
dropdown — **Bones**, **Rigid Bodies**, **Joints** — each with its own
tree/list pane and its own distinctly-colored viewport gizmo, all routed
through the shared `Selection` object (`src/Editor/Selection.h`). This
campaign adds a **4th mode, "Verlet"**, that visualizes exactly what this
engine's already-implemented Verlet dynamic-bone-chain physics system
(`src/Physics/`, `src/Game/Physics/PhysicsSystem.h`, built across
`task_manager/verlet-integration-1`) actually simulates for the currently-
inspected model:

- **Particles** — one gizmo dot per physics-simulated ("jiggle") bone joint,
  drawn at its bind-pose rest position (consistent with how Bone/Rigid
  Body/Joint modes already always show bind-pose data, never a live,
  currently-running simulation frame).
- **Chains** — each detected chain's joints connected by lines, in
  root-to-tip order, exactly the linear run `DynamicChainDefinition`
  (`src/Physics/DynamicChainDefinition.h`) already models.
- **The pinned root/anchor** — the one bone each chain is rooted to (never
  itself simulated — see `DynamicChainDefinition::rootBoneIndex`'s own doc
  comment), drawn as a small, visually distinct, non-selectable marker so a
  user can see exactly where a chain "hangs from."
- **The optional head-collider** — when a chain has
  `DynamicChainDefinition::hasHeadCollider` set, its collision sphere is
  drawn as a real wireframe sphere (reusing the exact geometry helper the
  Rigid Body gizmo already uses), not merely implied.

Selecting a particle in the Bone Viewer (tree row or a direct viewport
click) makes it the current `Selection`/Inspector target exactly like every
other mode, and the Inspector gains a new, focused "Verlet Joint" section
(Phase 3) that shows and **live-edits** that one joint's damping/stiffness/
mass — the same fields the Inspector's existing, more generic "Dynamic
Chain Physics" collapsible section (added by `verlet-integration-1`'s
Phase 4, still present and unchanged by this campaign) already edits, just
reached by clicking the actual particle instead of expanding a tree by
chain/joint number.

## Step 2: The Situation / The Problem (Where are we now?)

A full read of the current source tree — `src/Editor/BoneViewerWindow.h/
.cpp`, `src/Editor/Selection.h`, `src/Editor/Panels/InspectorPanel.h/.cpp`,
`src/Game/Physics/PhysicsSystem.h`, `src/Game/Physics/
DynamicChainRigCache.h`, `src/Physics/DynamicChainDefinition.h`,
`src/Physics/DynamicChainDetection.h`, `src/Game/Game.h`,
`src/Editor/ImGuiEditorLayer.cpp` — turned up the exact culprits this
campaign's phases each close:

1. **Culprit A — the Verlet physics system is fully implemented and
   already running, but has zero visualization anywhere.** Every piece this
   campaign needs already exists and already works: `Physics/
   DynamicChainDefinition.h`'s `DynamicChainDefinition`/`DynamicJointSettings`,
   `Physics/DynamicChainDetection.h`'s `DetectDynamicChains()` (auto-derives
   chains straight from `Bone::deformAfterPhysics` + `RigidBody::motionType`
   — real PMX physics data, no new authoring format), `Game/Physics/
   PhysicsSystem.h`'s `PhysicsSystem` (owns a `DynamicChainRigCache`,
   already registered for every spawned mesh via
   `Game::CreateMeshEntityFromGtaFile()` → `RegisterDynamicChains()` +
   `AttachDynamicChainRigIfNeeded()`), and even a live Inspector editing
   surface (`Panels/InspectorPanel.cpp`'s existing "Dynamic Chain Physics"
   collapsible section, shown for any entity carrying a `DynamicChainRig`
   component). None of this is reachable from the Bone Viewer today — a
   user can only ever SEE a jiggle chain's topology by scrolling through the
   generic entity Inspector's "Chain 0/Joint 0/Joint 1/..." tree text, never
   by looking at the actual 3D shape of the chain the way Bones/Rigid
   Bodies/Joints already can be.
2. **Culprit B — `Selection`'s `ModelPartKind` enum only has three values,
   and every "kind → label/behavior" switch in the codebase is written as an
   exhaustive 3-way branch, not a `default:`-safe one.** `Selection.h`'s own
   class comment already anticipates this exact extension ("any future
   selectable 'thing' should extend it the same way rather than adding a
   new ad hoc field elsewhere") — but every existing consumer
   (`BoneViewerWindow.cpp`'s toolbar label array/count ternary/warning-text
   branch, `InspectorPanel.cpp`'s single-part switch AND its separate
   multi-selection-summary `kindNoun` ternary + inner switch) was written as
   a closed, exhaustive 3-way branch with no `default:` fallback silently
   catching a 4th value — meaning simply adding `ModelPartKind::Verlet` to
   the enum, by itself, would compile cleanly (C++ does not require
   `switch` to be exhaustive) but silently fall through every one of those
   branches to whatever the LAST `case`/ternary-else happens to be (e.g. a
   Verlet multi-selection would silently be mislabeled "Joints" in the
   Inspector's summary header). **Fixed by Phase 1** (the enum addition
   itself, with an explicit audit note) **and Phase 4** (the one branch this
   campaign's own earlier phases don't already touch directly — the
   multi-selection summary).
3. **Culprit C — `BoneViewerWindow`'s overlay code silently assumes "overlay
   slot index == `ModelPartKind` selection index" for all three existing
   modes, an assumption Verlet mode breaks.** For Bone/Rigid Body/Joint mode,
   `overlayParts[i]` is built by iterating `m_bones`/`m_rigidBodies`/
   `m_joints` in order, so the loop index `i` IS simultaneously "the slot to
   project/draw" and "the exact `partIndex` to pass to
   `ctx.selection.SelectModelPart()`." A Verlet particle's natural, globally-
   unique, collision-free identity is its own **skeleton bone index** (see
   Phase 1's own rationale for why bone index is the correct, safe choice —
   not a freshly-invented 0..N flattened joint counter), which is NOT the
   same as its position within a freshly-built "every joint of every chain,
   in order" overlay list. Every existing call site that currently reads a
   raw loop/hover index and feeds it straight into
   `ctx.selection.SelectModelPart(m_targetEntity, m_viewMode, thatRawIndex)`
   must be generalized to go through an explicit "overlay slot → real
   `partIndex`" mapping instead of assuming identity. **Fixed by Phase 2**,
   which extends the existing `OverlayPart` struct with exactly this
   mapping field rather than hand-rolling a parallel, easily-desynced array.
4. **Culprit D — Rigid Body/Joint's existing Shift-click "contiguous range
   select" (`FlatListRangeSelection.h`'s `BuildInclusiveIndexRange()`)
   silently assumes its index space is dense (every integer between two
   valid indices is itself a valid, meaningful index of the SAME kind).**
   That assumption holds for Rigid Body/Joint (both are flat arrays,
   0..count-1) but does **not** hold for Verlet mode once `partIndex` is a
   bone index (Phase 1's choice, Culprit C): the raw integer range between
   two joint bone indices can and typically will include bone indices that
   are not physics-driven joints at all (an ordinary skinned bone that
   happens to sit between them in the skeleton's array). Naively reusing
   `RenderFlatPartRow()`/`Build()`'s existing Shift-range-select code
   unmodified for Verlet mode would silently "select" bones that are not
   Verlet joints, which the Inspector (Phase 3) would then have nothing
   sensible to show for. **Fixed by Phase 2**, which explicitly disables
   Shift-range-select for Verlet mode (falling back to the same
   Ctrl-click-equivalent toggle behavior Bone mode's own tree already uses,
   for the identical underlying reason: a non-dense index space has no
   meaningful linear "range").
5. **Culprit E — `BoneViewerWindow` has no dependency on `PhysicsSystem`
   today, by design, and gaining one is a deliberate, one-time architecture
   decision this campaign must make explicitly, not accidentally.** Every
   other mode's data (`m_bones`/`m_rigidBodies`/`m_joints`) is loaded via
   `ModelRigCache`, which reads straight from the source `*.gta` file on
   disk — `BoneViewerWindow`'s own class comment is explicit that this gives
   it "zero dependency on Game's internal caching/animation-runtime state at
   all." Chain **topology** (which bones form a chain, and their bind-pose
   positions) could technically be re-derived the same way, by calling
   `DetectDynamicChains()` directly against `ModelRigCache`'s own
   `RigFileData::skeleton`/`physics`. However, that would produce a SECOND,
   independent set of `DynamicChainDefinition`s with their own independent
   `DynamicJointSettings` (damping/stiffness/mass) — silently diverging from
   whatever `PhysicsSystem`'s own `DynamicChainRigCache` holds the moment a
   user edits a slider in the Inspector's existing "Dynamic Chain Physics"
   section (`verlet-integration-1` Phase 4), which writes into
   `PhysicsSystem`'s copy, not a Bone-Viewer-private one. `InspectorPanel.h`
   already crossed this exact bridge once (`BuildInspectorPanel()`'s
   signature already threads a `PhysicsSystem&` parameter through, in EVERY
   build configuration, specifically for that same "Dynamic Chain Physics"
   section) — `Game::GetPhysicsSystem()` is already a public, Editor-facing
   accessor for precisely this purpose. **Fixed by Phase 2**, which threads
   `PhysicsSystem&` into `BoneViewerWindow::Build()` the same way
   `ModelRigCache&` was threaded in by `verlet-integration-2`'s own Phase 2,
   and reads `physicsSystem.GetDynamicChainRigCache().TryGet(absoluteGtaPath)`
   **fresh, every frame, with no new caching member** — a deliberately
   simpler plumbing shape than `ModelRigCache`'s own mtime-gated reload
   logic, justified in full by Phase 2 itself (`DynamicChainRigCache` is
   already an in-memory, already-populated, already-cheap-to-query map; it
   has no on-disk mtime concept to gate against in the first place).
6. **Culprit F — a selected Verlet particle has nothing to show in the
   Inspector today, and the existing "Dynamic Chain Physics" section cannot
   be reused as-is for a single-joint focus view.** That section (Phase 4 of
   `verlet-integration-1`) is deliberately a whole-model summary — every
   chain, every joint, all at once, gated on the SELECTED ENTITY carrying a
   `DynamicChainRig` component, completely independent of `Selection`'s
   `ModelPart`/`Kind()` concept. `InspectorPanel.cpp`'s OTHER switch —
   `BuildModelPartInspector()`, gated on
   `ctx.selection.Kind() == InspectorSelectionKind::ModelPart` — is the one
   that already gives Bone/Rigid Body/Joint each their own single-object,
   click-to-focus property view, and it has no `case
   ModelPartKind::Verlet:` branch, nor any access to `PhysicsSystem` at all
   (its signature only takes `Registry&`, `EditorContext&`, `ModelRigCache&`
   today — read-only asset data, appropriate for Bone/Rigid Body/Joint,
   which have no live simulation state to edit). **Fixed by Phase 3**, which
   threads `PhysicsSystem&` into `BuildModelPartInspector()` too (mirroring
   Culprit E's own fix, one call-site level up) and adds the new branch.

## Step 3: The Plan (How will we get there?)

Execute phases 1 → 4, strictly in order:

1. **Phase 1** lands the two purely-additive, zero-GPU/zero-ImGui
   foundations both later phases depend on: `Selection.h`'s
   `ModelPartKind::Verlet` enum value (plus an explicit audit + fix of every
   pre-existing exhaustive switch this touches that Phase 1 itself can reach
   without yet touching `BoneViewerWindow`/`InspectorPanel`'s own bodies),
   and `Physics/DynamicChainDefinition.h`'s new, small, pure, Tier-1-tested
   `FindDynamicChainJointByBoneIndex()` helper — the shared "given a bone
   index, which chain/joint-within-chain is it?" lookup Phases 2 and 3 BOTH
   need, written and tested exactly once.
2. **Phase 2** is the user-visible core: `BoneViewerWindow` gains the
   "Verlet" toolbar entry, a chain-grouped tree pane (reusing the existing
   `RenderFlatPartRow()` per joint row, wrapped in a small new
   `RenderVerletChainNode()` per chain), a fully generalized viewport
   overlay (particles, chain connector lines, root/anchor markers, the
   optional head-collider wireframe), the `PhysicsSystem&` plumbing (Culprit
   E), the `OverlayPart` generalization (Culprit C), and the Shift-range-
   select guard (Culprit D).
3. **Phase 3** adds the focused "Verlet Joint" Inspector section —
   `BuildModelPartInspector()` gains its `PhysicsSystem&` parameter and its
   4th `case`, using Phase 1's lookup helper to resolve the selected bone
   index back into a `(chainIndex, jointIndexInChain)` pair, then showing/
   editing that exact joint's fields via the exact same
   `DynamicChainRigCache::TryGetMutable()` contract the existing "Dynamic
   Chain Physics" section already uses.
4. **Phase 4** closes the two remaining gaps a straightforward
   implementation of Phases 1-3 would otherwise leave behind (Culprit B's
   Inspector multi-selection summary switch, still hard-coded to 3 kinds
   even after Phase 3 lands its own single-selection branch) plus one small,
   consistency-driven feature addition ("Select All (Chain)", mirroring
   Rigid Body's own "Select All (Group)/(Branch)" buttons using the exact
   same `Selection::SelectModelParts()` multi-select primitive already
   built by `verlet-integration-4`), and runs the full manual cross-mode
   regression checklist.

## Step 4: What We Will NOT Do (Focus)

- We will **not** show a LIVE, currently-simulating view of a chain jiggling
  in real time — every particle is drawn at its bind-pose rest position,
  exactly like Bone/Rigid Body/Joint modes already do, and exactly as this
  campaign's own requirements settled on. A future "watch it jiggle live"
  overlay is a genuinely separate, larger follow-up (it would need
  `Registry`-level access to a specific live entity's `DynamicChainRig::
  chainStates`/`ResolvedAnimationPose`, not merely `PhysicsSystem`'s
  path-keyed, model-level `DynamicChainRigCache`) and is explicitly out of
  scope here.
- We will **not** touch `src/Physics/DynamicChainSolver.cpp`,
  `ChainConstraints.cpp`, `VerletIntegration.cpp`, `WindField.cpp`,
  `FixedTimestepAccumulator.cpp`, `SphereCollider.cpp`, or
  `BoneChainPhysicsResolver.cpp` at all — every one of those is the actual
  runtime simulation math, already complete and already correct; this
  campaign is Editor-visualization-only.
- We will **not** build a second, independent chain-detection code path —
  Phase 2 deliberately reads `PhysicsSystem`'s own already-populated
  `DynamicChainRigCache` rather than re-calling `DetectDynamicChains()`
  itself (Culprit E), so there is exactly one source of truth for "what are
  this model's chains/joint parameters" in the whole Editor.
- We will **not** add per-instance (per-entity) override support for
  Verlet joint parameters — the Inspector's existing "Dynamic Chain
  Physics" section already documents this exact limitation (edits apply per
  model path, shared by every entity spawned from it) and Phase 3's new
  section inherits the identical limitation for the identical reason (it
  edits the exact same underlying `DynamicChainDefinition`).
- We will **not** attempt true, oriented per-joint capsule/box wireframes
  for the particle chain itself — a filled dot + straight connector line
  per segment is sufficient (mirrors Bone mode's own dot + parent-line
  convention exactly); only the OPTIONAL head-collider sphere gets a real
  wireframe, and only because that code (`RigidBodyWireframe.h`) already
  exists and is a trivial, single-shape reuse.

## Step 5: Their Role (What does this mean for you, the implementer?)

Treat each child phase document as a standalone work order: it names the
exact files to add/modify, the exact struct/function signatures to write,
the exact existing call sites to touch, and the exact test files to add or
extend. Do not skip a phase's own test file — every new pure/Tier-1-testable
piece of logic in this plan (Phase 1's lookup helper) must land with its
`tests/` counterpart in the same change, per this codebase's own
"Testability & Regression Safety" convention, which every prior
`verlet-integration-*` campaign already followed and this one inherits
unmodified. Do not reorder the phases — Phase 2 cannot compile without
Phase 1's `ModelPartKind::Verlet` enum value and lookup helper existing
first, and Phase 3 cannot resolve a selected joint back to its chain/index
without that same Phase 1 helper.


## Revision Notes (v2)

This campaign's documents (this file plus `PHASE1`–`PHASE4`) were written once
(v1), then re-audited line-by-line against the ACTUAL current source tree
(`src/Editor/Selection.h`, `src/Editor/BoneViewerWindow.h/.cpp`, `src/Editor/
Panels/InspectorPanel.h/.cpp`, `src/Physics/DynamicChainDefinition.h`,
`src/Physics/DynamicChainDetection.h`, `src/Game/Physics/PhysicsSystem.h/
DynamicChainRigCache.h`, `src/Editor/RigidBodyWireframe.h`, `src/Editor/
FlatListRangeSelection.h`, `CMakeLists.txt`, `tests/CMakeLists.txt`) before
being finalized as v2. The overwhelming majority of v1's claims (every exact
struct/function signature, every "this already exists, confirmed" assertion,
every line-level code reference) were verified BYTE-FOR-BYTE accurate against
the real, currently-compiling code — a second full rewrite was judged NOT
worth doing, since doing so would only re-type already-correct material and
risk introducing new transcription mistakes. Instead, v2 applies three
targeted, surgical fixes directly to the three affected child documents,
found and fixed in this pass:

1. **PHASE2, §3.6 (`OverlayPart` construction's "4th branch") was missing a
   step §3.8 (the connector-line block) already correctly performs.** v1's
   §3.6 showed appending a new `} else { // ModelPartKind::Verlet ... }`
   branch directly after the existing Bone/RigidBody `if`/`else if` chain —
   but the real code's trailing branch there is Joint's own construction,
   written today as a bare, IMPLICIT `else { ... }` (confirmed directly
   against `BoneViewerWindow.cpp` lines 996-1015), not an explicit
   `else if (m_viewMode == ModelPartKind::Joint)`. Applying v1's snippet
   literally would have silently DELETED Joint mode's own overlay-part
   construction (replacing its body with Verlet's), a genuine functional
   regression directly contradicting this same phase's own "What We Will Not
   Do" promise that Bone/Rigid Body/Joint's existing behavior is untouched —
   and an easy mistake for an implementer to make, since §3.8 immediately
   below it in the very same document DOES correctly call out "make it an
   explicit `else if (m_viewMode == ModelPartKind::Joint)` first" for the
   structurally identical connector-line `if` chain, creating an inconsistent,
   trap-laying asymmetry between two adjacent sections of the same document.
   **Fixed**: PHASE2 v2's §3.6 now spells out the same "make Joint explicit
   first" step explicitly, with Joint's own unchanged body reproduced so nothing
   is left ambiguous.
2. **PHASE4, §3.3 (final regression checklist)'s Verlet-mode item "Double-
   clicking a row/dot recenters the orbit camera on that joint" was verified
   against the real code and found INACCURATE for the "dot" half of that
   claim.** `BoneViewerWindow.cpp`'s direct-viewport-click handler (inside
   `Build()`, the `hoveredPartIndex` branch) has NO
   `ImGui::IsMouseDoubleClicked()` check anywhere in its body — only
   `RenderBoneTreeNode()`/`RenderFlatPartRow()` (i.e. TREE/LIST ROWS) recenter
   the camera on double-click, for ALL THREE pre-existing modes, today. Left
   as-written, an implementer running this checklist against a freshly-built
   Verlet mode would find double-clicking a viewport dot does nothing, and
   could easily either (a) waste time debugging a "regression" that is
   actually a pre-existing gap shared identically by every other mode, or (b)
   scope-creep a fix into this campaign that was never asked for and touches
   all four modes' shared click-handling code at once. **Fixed**: PHASE4 v2's
   checklist item now explicitly tests only the tree-row double-click (the
   one behavior that genuinely, verifiably works today) and explicitly notes
   the direct-viewport-dot double-click gap as pre-existing/out-of-scope, so
   it is never mistaken for something this campaign broke or was meant to add.
3. **PHASE3, §3.3's new "Verlet Joint" Inspector section read
   `chain.jointSettings[jointIndex]` with no bounds guard, inconsistent with
   its own immediately-preceding `restLengths` read** (which correctly checks
   `jointIndex < chain.restLengths.size()` first) **and with every other
   single-part case in the same `switch`** (Bone/RigidBody/Joint, immediately
   above it in the same file, each of which bounds-checks its own selected
   index before dereferencing). `jointSettings` is documented
   (`DynamicChainDefinition.h`) as always index-aligned 1:1 with
   `jointBoneIndices`, so this exact defensive check can, in practice, never
   actually fire — but "provably true per an invariant maintained elsewhere"
   is exactly the situation every other bounds check in this same file
   already exists for too (e.g. `rig->skeleton.bones.size()` checks that
   could equally argue "the model can't have changed since load"). **Fixed**:
   PHASE3 v2 adds the identical guard shape (an out-of-range `TextDisabled()`
   message + `break;`, matching this function's own established convention
   verbatim) immediately before the `jointSettings[jointIndex]` read.

No other correctness issues were found — Phase 1's bone-index selection
scheme, its `FindDynamicChainJointByBoneIndex()` contract/tests, Phase 2's
`PhysicsSystem&` threading/`verletModel` fetch-once shape/Shift-range-select
guard, Phase 3's `BuildModelPartInspector()` signature change/single-joint
field mapping, and Phase 4's `kindNoun`/multi-selection-row fix and "Select
All (Chain)" button were all independently re-derived from the real source
during this audit and matched v1's plan exactly — those sections are
unchanged in v2.
