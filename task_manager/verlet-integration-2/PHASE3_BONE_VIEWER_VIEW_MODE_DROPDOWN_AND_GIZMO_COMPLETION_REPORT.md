# PHASE3 — Bone Viewer: View-Mode Dropdown, Generalized Gizmo, Selection Wiring — Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit A/B/C/F). Implements
`PHASE3_BONE_VIEWER_VIEW_MODE_DROPDOWN_AND_GIZMO.md` (v2) in full, on top of
Phase 1 (`Selection::SelectModelPart()`/`IsModelPartSelected()`/
`ClearModelPartIfEntity()`/`ModelPartKind`) and Phase 2 (`ModelRigCache`),
both already committed and compiling before this phase started.

## What was done

Rewired `src/Editor/BoneViewerWindow.h/.cpp` from "always shows Bones" into a
window that shows any one of Bones / Rigid Bodies / Joints, per the phase
document's Step 3:

- **`BoneViewerWindow.h`**:
  - Added `#include "Selection.h"` (for `ModelPartKind`) and
    `#include "../Assets/PhysicsData.h"` (for `RigidBodyShape`), plus forward
    declarations for `class ModelRigCache;` and `struct EditorContext;`.
  - Added `RigidBodyEntry`/`JointEntry` structs (flattened overlay-drawing
    shapes, mirroring `BoneEntry`'s own "only what the overlay needs"
    philosophy) and new members `m_rigidBodies`/`m_joints`/
    `m_viewMode` (defaulting to `ModelPartKind::Bone`, persisted across
    frames like `m_showAllNames`, never reset on reload).
  - **Deleted** `int m_selectedBoneIndex` entirely.
  - Changed `Build()`'s signature to
    `Build(Registry&, Renderer&, EditorContext&, ModelRigCache&)` and
    `EnsureDataLoaded()`'s to additionally take `EditorContext&`/
    `ModelRigCache&`.
  - Renamed `BuildBoneTreePane()` → `BuildPartListPane()` (now takes
    `EditorContext&`, branches on `m_viewMode`), added `RenderFlatPartRow()`'s
    declaration, and added `EditorContext&` to `RenderBoneTreeNode()`.
    `BoneMatchesFilterRecursive()` kept its exact signature (still
    Bone-tree-specific).
- **`BoneViewerWindow.cpp`**:
  - `EnsureDataLoaded()` now loads bones AND rigid bodies AND joints via
    `rigCache.GetOrLoad(absoluteGtaPath)` instead of decoding `RigFileData`
    itself — the mesh-payload mtime short-circuit logic at the top is
    completely unchanged (two independent mtime checks for the same file is
    an accepted, documented minor duplication per `ModelRigCache.h`'s own
    class comment). Right at the "reload starting" point (after the
    mtime short-circuit, before re-populating anything), it now calls
    `ctx.selection.ClearModelPartIfEntity(m_targetEntity)` — closing the v2
    finding #1 gap (a stale Model-Part selection surviving a genuine model
    reload).
  - `Reset()` now also clears `m_rigidBodies`/`m_joints` alongside `m_bones`
    (the private `m_selectedBoneIndex` reset line was removed, since that
    field no longer exists).
  - `RenderBoneTreeNode()`'s selection highlight/click now reads/writes
    through `ctx.selection.IsModelPartSelected(...)`/
    `ctx.selection.SelectModelPart(...)` (`ModelPartKind::Bone`) instead of
    the deleted private index — otherwise byte-for-byte unchanged.
  - Added `RenderFlatPartRow()` — a single, non-indented, non-expandable
    `Selectable` row for a Rigid Body/Joint entry, reusing the same
    search-filter/selection/double-click-recenter shape as the bone tree's
    own leaf case.
  - `BuildPartListPane()` branches on `m_viewMode`: Bone mode walks
    `m_rootBoneIndices` through `RenderBoneTreeNode()` exactly as before;
    RigidBody/Joint modes iterate their flat vectors through
    `RenderFlatPartRow()`.
  - Added the toolbar "View" `ImGui::Combo` (Bones / Rigid Bodies / Joints),
    made the existing count line and "no data" warning mode-aware (a
    dedicated warning string per category), and updated the search hint text.
  - Generalized the viewport gizmo overlay: introduced a small local
    `OverlayPart{ position, name }` shape and, once per `Build()` call, a
    `std::vector<OverlayPart> overlayParts` built from whichever category is
    active (`m_bones` / `m_rigidBodies` / `m_joints`, falling back to
    `"Part " + index"`/`"Joint " + index"` for an empty name, matching
    `RenderFlatPartRow()`'s/the tree pane's own fallback labels exactly) —
    the hover/hit-test math and the name-label/search-color drawing loop
    (both already generic) are now written ONCE and reused for all three
    modes, per the v2 "Revision Notes" finding #2, instead of only
    generalizing the dot/circle/connector-line drawing:
    - **Bone mode**: parent-line drawing unchanged byte-for-byte; dot color
      `IM_COL32(90, 230, 130, 255)` (green), name-label/search-highlight
      logic unchanged (now reading from `overlayParts` instead of `m_bones`
      directly, same values either way).
    - **RigidBody mode**: cyan dots (`IM_COL32(80, 180, 255, 255)`), a dimmer
      connecting line to the attached bone (`boneIndex >= 0`) when in range,
      and an unfilled "size hint" circle (pixel radius = on-screen distance
      to a point offset along the view-right axis by the shape's
      characteristic size — `shapeSize.x` for Sphere/Capsule,
      `Length(shapeSize)` for Box) — a deliberate, documented screen-space
      approximation, never a true oriented 3D wireframe.
    - **Joint mode**: violet/magenta dots (`IM_COL32(200, 120, 255, 255)`),
      two connector lines (joint→bodyA, joint→bodyB) when the referenced
      rigid body indices are in range.
    - All three modes share the exact same hover-hit-test radius, and the
      exact same selected/hovered/search-match color-override tiers and
      name-label drawing (gated on `m_showAllNames`/search-match/hover/
      selected, identical to Bone mode's pre-existing behavior) — so
      toggling "Show All Names" or typing into the search box now visibly
      affects the viewport in Rigid Body/Joint modes too, not just the tree
      pane.
    - Click-to-select (tree row or direct viewport dot) now calls
      `ctx.selection.SelectModelPart(m_targetEntity, m_viewMode, index)` for
      whichever mode is active, and hover/selection state reads through
      `ctx.selection.IsModelPartSelected(...)`.
- **`ImGuiEditorLayer.cpp`**: added `#include "ModelRigCache.h"` (alongside
  the existing `BoneViewerWindow.h` include, same `GTE_ENABLE_PROJECT_PANEL`
  block), added a new `ModelRigCache m_modelRigCache;` member right after
  `m_boneViewer` (same block), and updated the one call site:
  `m_boneViewer.Build(registry, renderer, m_ctx, m_modelRigCache);` (was
  `m_boneViewer.Build(registry, renderer);`).

Nothing under `src/Editor/Panels/InspectorPanel.cpp` was touched by this
phase (it recompiled only because it transitively includes
`BoneViewerWindow.h`, whose signature changed) — its Inspector "Model Part"
section is Phase 4's job, per the phase document's own "What We Will NOT Do".
`src/Editor/Selection.h/.cpp`, `src/Editor/ModelRigCache.h/.cpp`,
`src/Assets/PhysicsData.h`, `SkeletonData.h`, `RigFile.h/.cpp` were not
touched at all, exactly as the phase document requires.

## Verification

- **Fast compile check** (per this campaign's workflow rules — no full
  build): `cmake --build build --target gte_core` — succeeded. Three files
  recompiled (`BoneViewerWindow.cpp`, `ImGuiEditorLayer.cpp`, and
  `Panels/InspectorPanel.cpp` — the last purely transitive, from the
  `BoneViewerWindow.h` signature change reaching its `#include`), zero
  warnings/errors, `libgte_core.a` relinked successfully.
- No automated test file was added/changed for this phase — `BoneViewerWindow`
  is Tier 2 (GPU/ImGui-owning, no live-`VkDevice` test infrastructure exists
  yet, see `TESTING.md`/`AGENTS.md`'s "Testability & Regression Safety"), same
  bucket as before this phase. `Selection`/`ModelRigCache` (the Tier-1 pieces
  this phase depends on) already have their own full test coverage from
  Phase 1/2, unchanged by this phase.
- Per the workflow rules for this task ("No Full Build... unless the Current
  task explicitly says to do full build"), a full `GreatTamanaEngine`
  executable build + manual, in-Editor verification against a real imported
  MMD model with rigid bodies/joints (Step 5's own checklist in the phase
  document) was **not** performed in this session — only the fast
  `gte_core` compile check. This is a known, deliberate gap relative to the
  phase document's own Step 5 item 4, which explicitly calls for launching
  the real executable; it is left to whoever runs the campaign's eventual
  full-build/regression phase (or can be done as a quick follow-up) since
  this campaign's own workflow rules only ask for a fast compile check for
  every phase except the last.

## Notes for the next phase (Phase 4 — Inspector "Model Part" section)

- `ImGuiEditorLayer` now owns exactly one shared `ModelRigCache` instance,
  `m_modelRigCache` (`GTE_ENABLE_PROJECT_PANEL`-gated, right alongside
  `m_boneViewer`) — Phase 4 needs to thread this same instance into
  `BuildInspectorPanel()`'s own signature (mirroring how this phase threaded
  it into `BoneViewerWindow::Build()`) so both consumers share one cache
  instead of redundantly reloading the same file twice per frame.
- `Selection`'s `SelectedModelPartEntity()`/`SelectedModelPartKind()`/
  `SelectedModelPartIndex()` accessors (Phase 1) are exactly what Phase 4's
  new Inspector "Model Part" branch needs to read — this phase is the first
  production call site that actually WRITES to them
  (`SelectModelPart()`), via `RenderBoneTreeNode()`/`RenderFlatPartRow()`/the
  viewport's direct-click handling.
- As called out in the phase document's own "What We Will NOT Do": it is
  expected/acceptable that, as of this phase alone, selecting a bone/rigid
  body/joint in the Bone Viewer does not yet show anything in the Inspector
  (`ctx.selection.Kind()` becomes `ModelPart`, but
  `InspectorPanel::BuildInspectorPanel()` has no branch for it yet) — this is
  Phase 4's own job to close, not a regression introduced here.
- Manual, in-Editor visual verification (Step 5 of the phase document) is
  still outstanding for this phase specifically — see the "Verification"
  section above. Whoever performs Phase 4's own manual verification should
  ideally also confirm Phase 3's own checklist (Bone mode visually
  unchanged; Rigid Bodies/Joints mode switch redraws immediately; tree/
  viewport selection stays in sync both ways; "Show All Names"/search work
  in all three modes; switching models/modes never crashes on an empty
  category; a genuine model reload clears a stale Model-Part selection) at
  the same time, since both phases will be visible together in the running
  Editor by then.
