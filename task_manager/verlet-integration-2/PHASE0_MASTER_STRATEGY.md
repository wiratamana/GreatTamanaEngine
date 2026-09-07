# PHASE0 — MASTER STRATEGY: Bone Viewer — Bones / Rigid Bodies / Joints Mode, Selection Integration, Inspector Readout (v2 — second-iteration self-audit)

Orchestrator document for this campaign (folder kept as `verlet-integration-2`
per the task's own instructions — the campaign's actual subject is the Bone
Viewer feature described below, not Verlet physics; the folder name is only
a filing location). Every child phase document in this folder implements one
slice of this plan. This document is the single source of truth for
**ordering, ownership, and the identified root causes** — read this first,
then execute `PHASE1_...md` → `PHASE4_...md` in order. Each phase is a real,
compilable increment that leaves the engine building and running correctly
end-to-end; none of them are "just planning" — every phase produces new/
modified `.h`/`.cpp`/`CMakeLists.txt`/test files.

## Campaign file map

| File | Delivers |
|---|---|
| `PHASE0_MASTER_STRATEGY.md` | This document — root-cause analysis + ordering. |
| `PHASE1_SELECTION_MODEL_PART_FOUNDATION.md` | Extends `src/Editor/Selection.h/.cpp` with a new `InspectorSelectionKind::ModelPart` + a free `ModelPartKind {Bone, RigidBody, Joint}` enum + `SelectModelPart()`/`IsModelPartSelected()`/accessors — the pure, Tier-1-tested data-model foundation everything else plugs into. No UI code touched yet. |
| `PHASE2_SHARED_RIG_DATA_CACHE.md` | New `src/Editor/ModelRigCache.h/.cpp` — extracts/generalizes the mtime-checked "read a Mesh `*.gta`'s METADATA section as `RigFileData`" logic that today lives ad hoc inside `BoneViewerWindow::EnsureDataLoaded()`, into its own small, reusable, Tier-1-tested class, so **both** `BoneViewerWindow` (Phase 3) and `InspectorPanel` (Phase 4) read the exact same cached bones/rigid-bodies/joints data for a given entity's model, instead of two independent, silently-divergible copies of the same decode+cache logic. |
| `PHASE3_BONE_VIEWER_VIEW_MODE_DROPDOWN_AND_GIZMO.md` | The user-visible heart of the campaign: `BoneViewerWindow` gains the "Bones / Rigid Bodies / Joints" dropdown, a generalized part-list tree/viewport-gizmo overlay that redraws correctly for whichever mode is active, and every click/selection now routes through Phase 1's `Selection::SelectModelPart()` instead of the window's own private `m_selectedBoneIndex`. |
| `PHASE4_INSPECTOR_MODEL_PART_SECTION.md` | `Panels/InspectorPanel.cpp` gains a new "Model Part" section, shown whenever `ctx.selection.Kind() == InspectorSelectionKind::ModelPart`, reading the exact same `ModelRigCache` (Phase 2) to show full read-only details of whichever bone/rigid body/joint is currently selected. Closes the loop the story asks for end-to-end. |

## Revision Notes (v2 — second-iteration self-audit)

This campaign's phase documents (all five, including this one) were re-read
end-to-end against the CURRENT source tree — not just cross-checked against
each other — specifically hunting for gaps, incorrectness, insufficiency, and
missing pieces, before any implementation began. Every field name/signature/
file path Culprits A–F below cite was independently re-verified directly
against the live source tree (`Selection.h`, `PhysicsData.h`, `SkeletonData.h`,
`BoneViewerWindow.h/.cpp`, `InspectorPanel.h/.cpp`, `ImGuiEditorLayer.cpp`,
`GtaFile.h`, `RigFile.h`, `MeshAssetSource.h`, `CMakeLists.txt`,
`tests/CMakeLists.txt`) and is still byte-for-byte accurate — no Culprit below
needed correcting. Five concrete findings DID come out of this pass, each
fixed directly in the phase document it belongs to:

1. **PHASE1/PHASE3 — a real safety net the refactor was silently about to
   drop: the private `int m_selectedBoneIndex`'s own "reset to -1 on every
   reload" behavior.** `BoneViewerWindow::EnsureDataLoaded()` (today)
   unconditionally resets `m_selectedBoneIndex = -1` at the top of its
   "reload starting" block, with its own comment explaining exactly why: *"A
   bone index from whatever was loaded before means nothing for a new
   asset."* v1's Phase 3 draft moved this same index into the shared,
   cross-frame-persistent `Selection` object but never carried this specific
   safety net over — nothing in v1 cleared `ctx.selection`'s Model-Part
   fields when `m_targetEntity`'s OWN underlying `*.gta` genuinely reloads
   (e.g. a live re-import while the Bone Viewer happens to be open), so a
   stale index that used to mean "bone 5" could silently keep showing itself
   as "selected" (a highlighted row/dot, AND described in the Inspector)
   against whatever bone/rigid body/joint index 5 happens to mean in the
   newly reloaded data instead — a correctness regression versus TODAY's own
   behavior, not a pre-existing gap. Fixed: PHASE1 gains a new
   `Selection::ClearModelPartIfEntity(Entity)` mutator (mirroring
   `ClearAssetIfPath()`'s own existing "clear only if it currently matches,
   revert Kind() to None only if it was the one on top" shape exactly), and
   PHASE3's `EnsureDataLoaded()` (now taking a new `EditorContext&`
   parameter) calls it at the exact same "reload starting" point that
   already resets `m_bones`/`m_rigidBodies`/`m_joints`.
2. **PHASE3 — the viewport gizmo's name-label/search-highlight behavior
   silently stayed Bone-mode-only.** v1's Step 3.6 fully generalizes the
   Rigid Body/Joint dot + size-hint circle + connector-line drawing, but
   never mentions the OTHER half of what Bone mode's own overlay already
   does every frame: drawing a part's name above its dot (gated on
   `m_showAllNames`/search-match/hover/selected) and recoloring a
   search-matched dot yellow. Nothing in v1 hides/disables the toolbar's
   search box or "Show All Names" checkbox per-mode — both stay visible, and
   already correctly prune/highlight the Rigid Body/Joint TREE rows (Phase
   3's own `RenderFlatPartRow()`) — so a user searching or toggling "Show
   All Names" while looking at Rigid Bodies/Joints would see the tree pane
   respond but the viewport silently not: an inconsistent, half-generalized
   feature. Fixed: PHASE3's Step 3.6 now explicitly extends the shared
   per-mode "positions to project this frame" list to also carry each part's
   own display name, and reuses the exact same name-label/search-color logic
   Bone mode already has, for all three modes.
3. **PHASE4 — "full read-only details" under-delivered on data already
   sitting right there for free.** v1's `BuildModelPartInspector()` Bone case
   shows `isIk` as a plain checkbox but never shows the IK chain itself
   (`ikTargetBoneIndex`/`ikIterationCount`/`ikAngleLimitRadians`/`ikLinks`) —
   squarely in-scope, since `BoneViewerWindow.h`'s own class comment names
   diagnosing exactly this ("a renamed/missing bone the animation's own
   MotionData never finds a match for") as this whole window's reason to
   exist, and IK chains are one of the most common sources of that kind of
   mismatch. It also omits the `visible`/`controllable` bone flags entirely,
   and the RigidBody case omits both collision-filtering fields
   (`group`/`collisionGroupMask`) despite every other RigidBody field being
   shown. Fixed: PHASE4's Step 3.3 now shows all of these, the IK chain
   conditionally (only rendered when `bone.isIk` is true, mirroring the
   Joint case's own existing "spring factors only for `SpringDof6`"
   conditional pattern).
4. **PHASE4 — selecting a Model Part leaves no way back to the Entity
   Inspector (and its "Open Bone Viewer" button) without leaving the Bone
   Viewer entirely and re-picking the same entity in Hierarchy.** Once
   `ctx.selection.Kind()` becomes `ModelPart`, `BuildInspectorPanel()`
   returns immediately after `BuildModelPartInspector()` and never reaches
   `BuildEntityInspector()` (Culprit D's own fix) — which is exactly where
   the "Open Bone Viewer" button lives (see `InspectorPanel.h`'s own class
   comment). A user who wants to retarget the SAME Bone Viewer window onto a
   DIFFERENT entity while a bone/rigid body/joint happens to be selected has
   no direct affordance to get back to an entity view at all. Fixed: PHASE4
   adds one small, low-risk button (`ctx.selection.SelectEntity(owner)`) at
   the top of `BuildModelPartInspector()` — distinct from, and not a
   substitute for, the deliberately-still-unimplemented reverse "jump
   to/focus the Bone Viewer" affordance PHASE4's own "What We Will NOT Do"
   already scopes out.
5. **PHASE3 — a small implementer-facing documentation gap.** v1's Step 3.2
   (header changes) never actually showed `RenderFlatPartRow()`'s own
   declaration - only Step 3.4's body and Step 5's checklist mention it
   exists, leaving an implementer to infer its exact signature/placement.
   Fixed: Step 3.2 now lists it explicitly, right alongside the other
   renamed/changed declarations.

Nothing else in this campaign's four phase documents needed a substantive
change — the ordering, the six Culprits' own file/field/signature citations,
and every "What We Will NOT Do" scope limit in PHASE1–PHASE4 all held up
against the live source tree exactly as originally written.

## Step 1: The Goal (Where are we going?)

Take the existing "Bone Viewer" debug window (`src/Editor/BoneViewerWindow.h/.cpp`,
opened from the Inspector's "Open Bone Viewer" button — see
`Panels/InspectorPanel.cpp`) from "shows only Bones" to a tool that can show
**any one of three categories** already fully available in the engine's own
imported model data (`src/Assets/PhysicsData.h`'s `RigidBody`/`Joint`,
alongside the existing `SkeletonData::Bone`), exactly as the brief specifies:

1. A dropdown to pick which category is currently displayed: **Bones**,
   **Rigid Bodies**, or **Joints**.
2. Changing that dropdown updates the 3D viewport's gizmo overlay to show
   the newly-selected category (dots for bones, shape-approximating markers
   for rigid bodies, connector markers for joints) — immediately, the very
   next frame, no separate "apply"/"refresh" step.
3. Whichever part (bone/rigid body/joint) the user clicks — in the tree pane
   or directly on its gizmo in the viewport — becomes selectable through the
   Editor's own single gate-keeper, `Selection` (`src/Editor/Selection.h`),
   exactly the same "one choke point for the-selection-changed" mechanism
   `HierarchyPanel`/`ProjectPanel` already use for entities/assets, not a
   second, parallel, BoneViewerWindow-private selection concept.
4. Whatever part is currently selected has its own full info shown in the
   Inspector (`Panels/InspectorPanel.cpp`) — name, indices, transform,
   shape/mass/damping for a rigid body, connected-bodies/limits for a joint,
   parent/flags for a bone — the same "Inspector shows whatever `Selection`
   currently points at" pattern the Inspector already uses for Entity/Asset.

## Step 2: The Situation / The Problem (Where are we now?)

A deep read of the current source tree (`src/Editor/BoneViewerWindow.h/.cpp`,
`src/Editor/Selection.h/.cpp`, `src/Editor/Panels/InspectorPanel.cpp`,
`src/Assets/PhysicsData.h`, `src/Assets/RigFile.h/.cpp`) turned up the exact
root causes — the "culprits" — that must each be addressed by a specific
phase below. This is, again, not a greenfield problem: **the raw data for
all three categories is already fully imported and already on disk** for
every spawned MMD model — nothing needs to be re-parsed from `.pmx`, no new
asset format, no new importer code. The gap is entirely in the Editor UI
layer that currently only ever reads/shows one of the three categories.

1. **Culprit A — `BoneViewerWindow` never reads the physics half of the rig
   data it already has on disk.** `EnsureDataLoaded()`
   (`BoneViewerWindow.cpp`) calls `DecodeRigDataFromBytes(gta->metadata)`
   and then only ever touches the result's `rig->skeleton.bones` field —
   `rig->physics.rigidBodies`/`rig->physics.joints` (already fully decoded,
   right there in the same `RigFileData`, see `src/Assets/RigFile.h`'s own
   `RigFileData` struct and `PhysicsData.h`'s `RigidBody`/`Joint`) are
   silently discarded every single call. There is currently **no in-memory
   representation at all** for "this window's currently-loaded model's rigid
   bodies/joints" — `m_bones` has no siblings. **Fixed by Phase 2** (the
   shared cache that exposes the whole `RigFileData`, physics included) and
   **Phase 3** (which actually stores/uses `RigidBody`/`Joint` entries
   flattened for overlay drawing, mirroring `BoneEntry`).
2. **Culprit B — every piece of `BoneViewerWindow`'s UI is hard-wired to
   "bones" specifically, with no mode concept at all.** There is no
   dropdown/enum anywhere in the class; `BuildBoneTreePane()`/
   `RenderBoneTreeNode()` walk `m_bones`/`m_boneChildren`/`m_rootBoneIndices`
   by name; the viewport overlay block in `Build()` projects `m_bones[i]
   .position`, draws a parent-line via `m_bones[i].parentIndex`, and hit-tests
   only against bone dots. None of this generalizes to a flat rigid-body/
   joint list (which has no bind-pose parent/child tree — a `RigidBody`
   attaches to a `Bone` by index, and a `Joint` connects two `RigidBody`s by
   index, neither of which is a tree at all). **Fixed by Phase 3**, which
   introduces an explicit `ModelPartKind m_viewMode` and branches the tree
   pane / viewport overlay on it, without disturbing the existing Bone-mode
   code path's own behavior.
3. **Culprit C — `BoneViewerWindow` keeps its own private, local selection
   state (`int m_selectedBoneIndex`) instead of using the Editor's own
   `Selection` gate-keeper.** `Selection.h`'s own class comment is explicit
   that it is "the single gate-keeper for every ... selection in the
   Editor" and that "no panel keeps its own local 'am I highlighted' state
   either" — `BoneViewerWindow` is exactly the kind of second, ad hoc
   selection concept that comment warns against, and today nothing outside
   this one window can ever know/react to which bone is selected inside it
   (in particular, the Inspector has no way to show anything about it).
   **Fixed by Phase 1** (extends `Selection`/`InspectorSelectionKind` with a
   new `ModelPart` kind general enough for a bone OR a rigid body OR a
   joint) **and Phase 3** (which deletes `m_selectedBoneIndex` entirely and
   routes every click through `Selection::SelectModelPart()`/
   `IsModelPartSelected()` instead).
4. **Culprit D — the Inspector has no branch for anything but `Entity`/
   `Asset`.** `Panels/InspectorPanel.cpp`'s `BuildInspectorPanel()` branches
   only on `ctx.selection.Kind() == InspectorSelectionKind::Asset` vs. an
   unconditional `BuildEntityInspector()` fallback — a hypothetical
   `ModelPart` selection today would silently fall through to
   `BuildEntityInspector()`, which reads `ctx.selection.SelectedEntity()`
   (a **completely unrelated** field — whatever Hierarchy last picked, not
   the model-part's owning entity) and shows either the wrong entity's
   Transform/MeshRenderer/etc. or "No entity selected." **Fixed by Phase 4**,
   which adds a dedicated `ModelPart` branch reading `Selection`'s own new
   `SelectedModelPartEntity()`/`SelectedModelPartKind()`/
   `SelectedModelPartIndex()` accessors (Phase 1) instead.
5. **Culprit E — there is no reusable way to fetch a model's decoded rig
   data (bones + physics) OUTSIDE `BoneViewerWindow`'s own private,
   window-scoped cache.** `InspectorPanel` (Phase 4) needs the exact same
   `RigFileData` `BoneViewerWindow` already loads/caches, to describe
   whatever bone/rigid body/joint is selected — but `BoneViewerWindow`'s
   `EnsureDataLoaded()` is a private method operating on private members,
   only ever populated for whichever ONE entity the window currently has
   open (or nothing at all, if the window is closed, which must not block
   the Inspector from still showing model-part info). Re-deriving a SECOND,
   independent "read `*.gta`, decode `RigFileData`, mtime-invalidate" copy
   directly inside `InspectorPanel.cpp` would be exactly the "several
   independent, subtly different hand-rolled versions of the same pattern"
   anti-pattern this codebase has already had to refactor out once before
   (see `AGENTS.md`, "Skeletal Animation Pose Resolution" — the
   `ResolveBoneChain()`/`ResolveSingleBoneChain()` extraction). **Fixed by
   Phase 2**, which promotes this logic into a shared, small,
   Tier-1-testable `ModelRigCache` class used identically by both
   `BoneViewerWindow` and `InspectorPanel`, owned by one shared instance in
   `ImGuiEditorLayer` so the two consumers don't even redundantly reload the
   same file twice per frame when both are showing the same model at once.
6. **Culprit F — `BoneViewerWindow::Build(Registry&, Renderer&)`'s current
   signature has no way to reach `EditorContext`/`Selection` at all.** The
   one production call site, `ImGuiEditorLayer::BuildUI()`, calls
   `m_boneViewer.Build(registry, renderer)` — with no `EditorContext&`
   parameter, `BoneViewerWindow` structurally cannot write into
   `ctx.selection` (required by Culprit C's fix) or read the shared
   `ModelRigCache` (required by Culprit A/E's fix). **Fixed by Phase 3**,
   which changes this signature (and its one call site) to
   `Build(Registry&, Renderer&, EditorContext&, ModelRigCache&)`.

## Step 3: The Plan (How will we get there?)

Execute phases 1 → 4, in order — Phase 3 depends on Phase 1's `Selection`
API surface and Phase 2's `ModelRigCache` API surface existing and
compiling first; Phase 4 depends on both of those too, but not on Phase 3's
own internal `BoneViewerWindow` changes (only on the ONE shared
`ImGuiEditorLayer`-owned `ModelRigCache` instance Phase 3 introduces as a
member, and the `Selection` accessors Phase 1 adds):

1. **Phase 1** extends `Selection`/`InspectorSelectionKind` with the new
   `ModelPart` kind + `ModelPartKind` enum + `SelectModelPart()`/
   `IsModelPartSelected()`/plain accessors, mirroring the exact
   `SelectEntity()`/`SelectAsset()` shape/tests already established. Pure
   data-model change, fully Tier-1-tested, zero UI code touched.
2. **Phase 2** extracts a new `ModelRigCache` class that loads + mtime-
   caches one model's whole `RigFileData` (bones + rigid bodies + joints)
   straight from its source `*.gta`, Tier-1-tested against a real temp file
   (mirroring `Editor/ProjectPanelDataTests.cpp`'s own "real temp directory,
   no ImGui/Renderer/SDL" convention) — no `BoneViewerWindow`/
   `InspectorPanel` code touched yet.
3. **Phase 3** rewires `BoneViewerWindow` on top of both: adds the mode
   dropdown, generalizes the tree pane + viewport overlay to draw whichever
   category is active, deletes `m_selectedBoneIndex` in favor of
   `Selection::SelectModelPart()`/`IsModelPartSelected()`, and changes its
   own `Build()` signature (+ the one `ImGuiEditorLayer.cpp` call site) to
   thread through `EditorContext&`/`ModelRigCache&` — the first frame a
   user can see Rigid Bodies/Joints in the viewer and have a click register
   in the shared `Selection`.
4. **Phase 4** adds the Inspector's own "Model Part" section, reading
   `Selection`'s new accessors (Phase 1) and the same shared `ModelRigCache`
   instance (Phase 2/3) to show full read-only details for whichever part
   is currently selected — the last missing link, closing the loop the
   story's requirement 4 asks for.

## Step 4: What We Will NOT Do (Focus)

- We will **not** build a general 3D wireframe primitive renderer (true
  oriented box/capsule/sphere mesh wireframes) for the Rigid Body gizmo —
  Phase 3 uses a simple, honestly-documented screen-space approximation
  (a colored dot at the body's origin plus a size-derived circle), the same
  "good enough for a debug view, not a simulation" scope discipline this
  codebase's own verlet-integration-1 campaign already applied to PMX
  `Joint`/`RigidBody` fidelity in general (see that campaign's own
  `PHASE0_MASTER_STRATEGY.md`, "What We Will NOT Do": *"We will not attempt
  full PMX `Joint`/`RigidBody` 6-DOF constraint fidelity"*).
- We will **not** make any rigid body/joint field editable anywhere — every
  new Inspector field (Phase 4) is read-only (`ImGui::BeginDisabled()`),
  exactly like the existing "Mesh Renderer"/"Global Physics Settings"
  read-only sections already in `InspectorPanel.cpp` — there is no physics
  simulation anywhere in the engine that consumes `RigidBody`/`Joint` data
  yet (see `PhysicsData.h`'s own file comment), so there is nothing for an
  edit to actually drive.
- We will **not** touch `src/Assets/PhysicsData.h`, `SkeletonData.h`,
  `RigFile.h/.cpp`, or `PmxLoader.cpp` at all — every byte of data this
  campaign needs is already extracted/decoded by the existing import
  pipeline; this is purely an Editor-UI-layer feature.
- We will **not** touch `src/Game/Physics/PhysicsSystem.*` or anything under
  `src/Physics/` (the verlet-integration-1 dynamic-bone-chain campaign) —
  that is a separate, already-completed campaign with its own, differently-
  shaped `DynamicChainDefinition`/`DynamicJointSettings` data model; this
  campaign is strictly about VIEWING/SELECTING the raw imported
  `RigidBody`/`Joint` PMX data, never about simulating it.
- We will **not** add multi-select (selecting several bones/rigid bodies at
  once) — `Selection` stays a single-selection gate-keeper end-to-end,
  exactly as it is today for Entity/Asset.
- We will **not** change `TransformGizmo.h`/ImGuizmo's translate/rotate/
  scale manipulator in any way — "gizmo" in this campaign's own scope means
  the Bone Viewer's existing hand-drawn dot/line overlay (`ImDrawList`
  circles/lines drawn directly in `BoneViewerWindow::Build()`), generalized
  to draw whichever category is active; it does not mean adding a
  drag-to-manipulate ImGuizmo widget to the Bone Viewer's own viewport.

## Step 5: Their Role (What does this mean for you, the implementer?)

Treat each child phase document as a standalone work order: it names the
exact files to add/modify, the exact structs/functions/signatures to write,
the exact existing call sites to touch, and the exact test files to add or
extend. Do not skip a phase's own test file — every new Tier-1-testable
piece of logic in this plan must land with its `tests/` counterpart in the
same change, per `AGENTS.md`'s own "Testability & Regression Safety" rule.
Do not reorder the phases — Phase 3 cannot compile without Phase 1's
`Selection::SelectModelPart()`/`ModelPartKind` and Phase 2's
`ModelRigCache::GetOrLoad()` existing first, and Phase 4 cannot compile
without those same two either. After each phase, build `gte_core` +
`GreatTamanaEngineTests` and confirm every new/existing test passes (and, for
Phase 3/4, actually launch the Editor once and open the Bone Viewer on a real
imported model to eyeball the result) before starting the next phase.
