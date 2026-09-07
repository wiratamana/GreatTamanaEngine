# PHASE2 — COMPLETION REPORT: Bone Viewer "Verlet" Mode Tree Pane and Viewport Gizmo

Parent: `PHASE0_MASTER_STRATEGY.md`. Implements
`PHASE2_BONE_VIEWER_VERLET_MODE_TREE_AND_GIZMO.md` exactly as written (v2) —
every exact struct/signature/branch-ordering fix called out in that
document's own v2 revision notes (the `OverlayPart` construction's Joint
branch needing to become an explicit `else if` before the new Verlet `else`,
and the identical treatment for the connector-line block) was applied
verbatim against the real, currently-compiling source tree
(`src/Editor/BoneViewerWindow.h/.cpp`, `src/Editor/ImGuiEditorLayer.cpp`)
before editing.

## What was done

1. **`src/Editor/BoneViewerWindow.h`**
   - Added a `class PhysicsSystem;` forward declaration and a real
     `#include "../Game/Physics/DynamicChainRigCache.h"` (needed, not just a
     forward declaration, since `BuildPartListPane()`'s new parameter type is
     the nested `DynamicChainRigCache::ModelEntry*`) plus
     `#include "../Physics/DynamicChainDefinition.h"` for
     `DynamicChainDefinition` itself (used by `RenderVerletChainNode()`'s
     signature).
   - `Build()` gained a new `PhysicsSystem& physicsSystem` parameter (Culprit
     E) with a doc comment explaining the "fetched fresh every `Build()`
     call, never cached into a new member field" decision.
   - `BuildPartListPane()`'s signature changed to take
     `const DynamicChainRigCache::ModelEntry* verletModel` (the 3.4
     correction from the phase document — `PhysicsSystem&` itself is only
     needed at the one `Build()` call site that performs the `TryGet()`
     lookup; every downstream consumer just needs the resulting pointer).
   - Added the new private method declaration
     `RenderVerletChainNode(std::int32_t chainIndex, const DynamicChainDefinition& chain, const std::string& lowerFilter, EditorContext& ctx)`.

2. **`src/Editor/BoneViewerWindow.cpp`**
   - New includes: `../Game/Physics/PhysicsSystem.h` (for
     `PhysicsSystem::GetDynamicChainRigCache()`).
   - `OverlayPart` gained a `std::int32_t partIndex = -1` field (Culprit C),
     with a doc comment explaining why Bone/RigidBody/Joint keep
     `partIndex == i` (their own loop index) while Verlet's is a bone index.
     All three pre-existing construction sites (Bone, RigidBody, Joint) now
     set this field explicitly; the toolbar's `kViewModeLabels` array gained
     `"Verlet"` as a 4th entry.
   - `Build()`'s toolbar text/part-count/warning-text ternaries were all
     extended to a 4th branch: `verletJointCount`/`verletChainCount` are
     computed once from the newly-fetched `verletModel` (see below), the
     part-count line grows a `"(N chains)"` suffix in Verlet mode, and a 4th
     warning line ("This model has no detected dynamic (Verlet) bone-chain
     physics data.") was added.
   - Right after the existing `EnsureDataLoaded()`/`m_cachedIsValid` early-
     return block, `Build()` now fetches
     `const DynamicChainRigCache::ModelEntry* verletModel = physicsSystem.GetDynamicChainRigCache().TryGet(source->gtaPath);`
     exactly once per frame (Culprit E) — never cached as a member, per the
     phase document's own explicit "no new caching member" decision — and
     threads it down to `BuildPartListPane()` and every viewport
     overlay/click-handling site that needs it.
   - `BuildPartListPane()` gained a 4th `case ModelPartKind::Verlet:` — shows
     `"(no dynamic bone chains)"` when `verletModel` is null/empty, otherwise
     calls the new `RenderVerletChainNode()` once per detected chain.
   - Added `RenderVerletChainNode()` — a non-selectable, always-expanded
     `ImGui::TreeNodeEx` header (`"Chain N - Root: <name> (K joints)"`),
     search-filter-prunable exactly like every other pane (hides the whole
     header if no joint name matches), with each joint rendered underneath
     via the **existing, unmodified** `RenderFlatPartRow()`
     (`ModelPartKind::Verlet`, `partIndex` = that joint's own bone index) and
     an optional trailing "Head Collider: r=..." line.
   - `RenderFlatPartRow()`'s Shift-range-select branch is now guarded by
     `const bool supportsRangeSelect = kind != ModelPartKind::Verlet;`
     (Culprit D) — Shift-click on a Verlet row falls back to the same
     toggle-only behavior Ctrl-click already has, since a bone index is not
     a dense, meaningfully-rangeable index space. The identical guard was
     applied to `Build()`'s own direct-viewport-click handler
     (`m_viewMode != ModelPartKind::Bone && m_viewMode != ModelPartKind::Verlet`).
   - The direct-viewport-click handler now resolves the REAL `partIndex`
     via `overlayParts[hoveredPartIndex].partIndex` before calling
     `SelectModelPart()`/`ToggleModelPartInSelection()`/
     `SelectModelParts()`/updating `m_flatSelectionAnchorIndex` — previously
     it read the raw overlay-slot index directly, which is only safe because
     Bone/RigidBody/Joint's `partIndex == i` identity held; Verlet's does
     not (Culprit C).
   - `overlayParts` construction (inside `Build()`'s viewport section) is now
     an explicit 4-way `if`/`else if`/`else if`/`else` chain (the v2-required
     fix: Joint's own construction, previously an implicit trailing `else`,
     is now `else if (m_viewMode == ModelPartKind::Joint)` with its body
     copied byte-for-byte, so the new Verlet `else` branch cannot silently
     replace it) — the Verlet branch walks every chain's `jointBoneIndices`
     and pushes one `OverlayPart` per valid joint, `partIndex` = that bone
     index.
   - The per-part drawing loop's `isSelected` check now reads
     `overlayParts[i].partIndex` instead of the raw loop index `i`.
   - The connector-line `if`/`else if`/`else` block is now an explicit 4-way
     chain the same way (Joint's branch made an explicit `else if` first,
     verbatim body preserved), with a new
     `else if (m_viewMode == ModelPartKind::Verlet)` branch drawing: a small
     gray filled-rect root/anchor marker per chain (non-selectable, per
     Phase 1's own scope decision), pink/magenta connector lines from root
     through every joint in order, and — when `hasHeadCollider` is set — a
     wireframe sphere via the existing `BuildRigidBodyWireframe()`
     (`RigidBodyShape::Sphere`), all reprojected directly from `m_bones`
     (independent of `overlayParts`/`screenPositions`, mirroring RigidBody
     mode's own "attached bone" connector-line convention).
   - `dotColor`'s 3-way ternary became a 4-way ternary, with Verlet's own
     base color (`IM_COL32(255, 90, 170, 255)`, hot pink/magenta) distinct
     from every other mode's base color and from the shared selected-orange/
     search-yellow/hovered-white overrides.

3. **`src/Editor/ImGuiEditorLayer.cpp`** — updated the one `Build()` call
   site: `m_boneViewer.Build(registry, renderer, m_ctx, m_modelRigCache, game.GetPhysicsSystem());`
   (`game` was already in scope at this call site, confirmed directly — the
   immediately-preceding `BuildInspectorPanel(...)` call already reads
   `game.GetPhysicsSystem()`).

## Verification

- **Compile check**: `cmake --build build --target gte_core` — succeeds, 0
  errors/warnings. `BoneViewerWindow.cpp`, `ImGuiEditorLayer.cpp`, and
  `Panels/InspectorPanel.cpp` (transitively, via `Selection.h`) all rebuild
  cleanly.
- **Compile check**: `cmake --build build --target GreatTamanaEngine` — the
  real executable builds and links successfully end to end (confirms the
  full call chain — `Game::GetPhysicsSystem()` → `ImGuiEditorLayer::BuildUI()`
  → `BoneViewerWindow::Build()` — compiles correctly, not just the static
  library in isolation).
- Per the task workflow rules, no full build/full regression (`ctest`) was
  run — this phase's own scope is confined to
  `src/Editor/BoneViewerWindow.h/.cpp` and `src/Editor/ImGuiEditorLayer.cpp`,
  none of which have any Tier-1-testable pure-logic surface of their own
  (this whole phase is ImGui/GPU-facing Bone Viewer UI code, Tier 2 per
  `TESTING.md`) — Phase 1's own `FindDynamicChainJointByBoneIndex()`/
  `ModelPartKind::Verlet` tests already passed before this phase started and
  were not touched here.
- No manual/visual runtime verification against a live MMD model with
  physics-driven bone chains was performed in this session (would require
  launching the Editor interactively) — the phase document's own Step 5,
  item 4 checklist (visual confirmation of the tree pane/gizmo/selection
  round-trip against a real jiggle-bone model) is left as a follow-up
  sanity check the next time the Editor is run interactively, consistent
  with this session's "fast compile check only" workflow rule.

## What was deliberately NOT done (per this phase's own scope)

- `src/Editor/Panels/InspectorPanel.h/.cpp` was not touched — a selected
  Verlet particle still shows whatever `BuildModelPartInspector()`'s
  existing (3-case, no `default:`) switch currently falls through to for an
  unrecognized `ModelPartKind` value; adding the "Verlet Joint" section is
  Phase 3's job.
- No `verletModel`/Verlet-mode data was persisted as a new
  `BoneViewerWindow` member field — every read goes through the one
  `TryGet()` call per `Build()` frame, by design (Culprit E).
- Bone/Rigid Body/Joint mode's existing pixel/interaction behavior was not
  changed — every diff to shared code (`OverlayPart`, `RenderFlatPartRow()`,
  the click-handling block) is either purely additive or a proven no-op for
  the three pre-existing modes (their own `partIndex == i` identity still
  holds after this phase).
- The root/anchor marker was not made independently selectable, and no
  second, independent `DetectDynamicChains()` call path was introduced —
  every Verlet-mode read goes through `PhysicsSystem`'s own already-running
  `DynamicChainRigCache`, the same live data the Inspector's existing
  "Dynamic Chain Physics" section already edits.

## Campaign status

This closes out Phase 2 of the `verlet-integration-5` campaign
(`PHASE0_MASTER_STRATEGY.md`). The Bone Viewer's "View" toolbar now has a 4th
"Verlet" option with a working chain-grouped tree pane and viewport gizmo
(particle dots, chain connector lines, root/anchor markers, optional
head-collider wireframe), fully wired to `PhysicsSystem`'s own live chain
data and routed through `Selection`'s bone-index `partIndex` scheme (Phase 1).
Ready for Phase 3 (`PHASE3_INSPECTOR_VERLET_JOINT_SECTION.md`) to add the
Inspector's own "Verlet Joint" single-selection section.

## Files touched

- `src/Editor/BoneViewerWindow.h`
- `src/Editor/BoneViewerWindow.cpp`
- `src/Editor/ImGuiEditorLayer.cpp`
- `task_manager/verlet-integration-5/PHASE2_COMPLETION_REPORT.md` (this file)
