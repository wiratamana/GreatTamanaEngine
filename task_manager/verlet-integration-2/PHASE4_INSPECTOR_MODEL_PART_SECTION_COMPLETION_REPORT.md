# PHASE4 — Inspector: Model-Part Info Section (`src/Editor/Panels/InspectorPanel.h/.cpp`) — Completion Report

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit D/E). Implements
`PHASE4_INSPECTOR_MODEL_PART_SECTION.md` (v2) in full, on top of Phase 1
(`Selection::SelectedModelPartEntity()`/`SelectedModelPartKind()`/
`SelectedModelPartIndex()`), Phase 2 (`ModelRigCache::GetOrLoad()`), and Phase 3
(the `ImGuiEditorLayer`-owned `ModelRigCache m_modelRigCache` member), all
already committed and compiling before this phase started.

## What was done

Added the last missing link this campaign exists to deliver: a dedicated,
read-only "Model Part" section in the Inspector, shown whenever
`ctx.selection.Kind() == InspectorSelectionKind::ModelPart` — per the phase
document's Step 3, with one deliberate deviation noted below.

- **`src/Editor/Panels/InspectorPanel.h`**:
  - Added `class ModelRigCache;` forward declaration inside the existing
    `#if GTE_ENABLE_PROJECT_PANEL` block, alongside `AssetPreviewTexture`/
    `AssetPreviewMesh`/`BoneViewerWindow`.
  - Changed the `GTE_ENABLE_PROJECT_PANEL`-ON `BuildInspectorPanel()` signature
    to add a trailing `ModelRigCache& rigCache` parameter (the non-
    `GTE_ENABLE_PROJECT_PANEL` signature is unchanged, exactly as the phase
    document requires — a `ModelPart` selection is structurally unreachable
    when that switch is OFF).
  - Extended the function's own doc comment with a new "ModelPart" bullet
    describing what the new section shows, mirroring the existing Entity/
    Asset bullets' own level of detail.
- **`src/Editor/Panels/InspectorPanel.cpp`**:
  - Added three new includes inside the existing `#if GTE_ENABLE_PROJECT_PANEL`
    block: `"../ModelRigCache.h"` and `"../../Assets/PhysicsData.h"` (for
    `RigidBody`/`Joint`/`RigidBodyShape`/`RigidBodyMotionType`/`JointType`).
    `Assets/SkeletonData.h`'s `Bone` type is already reachable transitively via
    `RigFile.h`, itself reachable via `ModelRigCache.h`, exactly as the phase
    document anticipated — no separate `SkeletonData.h` include was needed.
  - Added `RigidBodyShapeLabel()`/`RigidBodyMotionTypeLabel()`/`JointTypeLabel()`
    — three small label helpers in the existing anonymous namespace, mirroring
    `AssetTypeLabel()`/`AssetFlagsLabel()`'s own "always produce something
    displayable" convention further down the file.
  - Added `BuildModelPartInspector(Registry&, EditorContext&, ModelRigCache&)`,
    placed right before `BuildEntityInspector()` as the phase document
    specifies, implementing every field the v2 phase document calls for:
    - **Bone**: name/English name/index/parent index/position/deform depth/
      rotatable/translatable/IK/deform-after-physics flags, **plus (v2)**
      `visible`/`controllable`, **plus (v2)** a conditional "IK Chain" section
      (target bone/iteration count/angle limit/every `IkLink`) shown only when
      `bone.isIk` is true.
    - **RigidBody**: name/index/attached bone/shape/motion type, **plus (v2)**
      collision group (`%u`) and collision mask (`0x%04X`), plus shape size/
      translate/rotate/mass/damping/restitution/friction.
    - **Joint**: name/index/type/both connected rigid bodies (by name+index, or
      "(none)"), transform, translate/rotate limits, and — only for
      `JointType::SpringDof6` — the two spring factor fields.
    - **(v2)** A "Select Owning Entity" button at the top (`ctx.selection.
      SelectEntity(owner)`), immediately followed by a separator, exactly as
      the phase document specifies — lets a user return to the Entity
      Inspector view (and its "Open Bone Viewer" button) without leaving the
      Bone Viewer and re-picking the same entity in Hierarchy.
    - Every out-of-range index (a stale selection against since-changed data)
      degrades to a plain `ImGui::TextDisabled()` message rather than an
      out-of-bounds read, matching every other guard already in this file.
  - **One deliberate deviation from the phase document's own draft C++**: every
    field the phase document's draft fed into a `DragFloat*`/`Checkbox` via
    `const_cast<float*>`/`const_cast<bool*>` (while wrapped in
    `ImGui::BeginDisabled()`) is instead read into a small **local, non-const
    copy** first (e.g. `Vec3 position = bone.position; ImGui::DragFloat3(...,
    &position.x);`). The phase document's own Step 3.3 trailing note
    explicitly flagged this exact choice as "the safer default to avoid
    introducing this codebase's first `const_cast`" and left it to the
    implementer — this session picked that safer option, matching every other
    `BeginDisabled()`-wrapped block already in this file/`BuildEntityInspector()`
    to date. Behaviorally identical (the widgets are always disabled, so the
    "written-back" value is discarded either way) and does not otherwise
    deviate from any structural requirement in the phase document.
  - Wired the new branch into `BuildInspectorPanel()`, immediately after the
    existing `Asset` branch and before the unconditional `BuildEntityInspector()`
    fallback — same `if (...) { Build...(); ImGui::End(); return; }` shape the
    `Asset` branch already uses.
- **`src/Editor/ImGuiEditorLayer.cpp`**: updated the one production call site
  to pass the already-existing `m_modelRigCache` member (added by Phase 3):
  `BuildInspectorPanel(registry, m_ctx, renderer, m_assetPreview,
  m_assetPreviewMesh, m_boneViewer, game.GetPhysicsSystem(), m_modelRigCache);`.

Nothing under `src/Editor/BoneViewerWindow.*`, `src/Editor/Selection.h/.cpp`,
`src/Editor/ModelRigCache.h/.cpp`, `src/Assets/PhysicsData.h`,
`src/Assets/SkeletonData.h`, or `src/Assets/RigFile.h/.cpp` was touched by this
phase, exactly as the phase document's "What We Will NOT Do" requires.
`BuildAssetInspector()`/`BuildEntityInspector()`'s own existing behavior is
unchanged beyond the new branch ordering in `BuildInspectorPanel()` itself.

## Verification

- **Fast compile check** (per this campaign's workflow rules — no full build
  yet): `cmake --build build --target gte_core` — succeeded. Two files
  recompiled (`Panels/InspectorPanel.cpp`, `ImGuiEditorLayer.cpp`), zero
  warnings/errors, `libgte_core.a` relinked successfully.
- No automated test file was added/changed for this phase — `InspectorPanel`
  is Tier 2 (ImGui-owning UI code, no live-`VkDevice`/ImGui-context test
  infrastructure exists yet, see `TESTING.md`/`AGENTS.md`'s "Testability &
  Regression Safety"), same bucket `BoneViewerWindow` already sits in (Phase 3's
  own completion report). `Selection`/`ModelRigCache` (the Tier-1 pieces this
  phase depends on) already have their own full test coverage from Phase 1/2,
  unchanged by this phase.
- Per this task's own workflow rules ("No Full Build... unless the Current
  task explicitly says to do full build"), a full `GreatTamanaEngine`
  executable build + manual, in-Editor verification against a real imported
  MMD model with rigid bodies/joints (the phase document's own Step 5
  checklist) was **not** performed in this session — only the fast `gte_core`
  compile check, matching Phase 3's own completion report's identical,
  explicitly-documented gap. This is left to whoever runs this campaign's
  eventual full-build/regression phase, or can be done as a quick follow-up.

## Campaign status

This closes out all four phases of `PHASE0_MASTER_STRATEGY.md`
(`verlet-integration-2`): Bone Viewer now supports Bones/Rigid Bodies/Joints
view modes with a generalized gizmo overlay (Phase 3), routed entirely through
the Editor's single `Selection` gate-keeper (Phase 1) backed by a shared,
mtime-cached `ModelRigCache` (Phase 2), and the Inspector now shows full,
read-only details for whichever part is selected (Phase 4, this report) —
including a way back to the owning entity's own normal Inspector view. The one
remaining outstanding item across the whole campaign is the manual, in-Editor
visual verification against a real imported MMD model with rigid bodies/joints
(Phase 3's and this phase's own Step 5 checklists) — not performed in either
session per this task's own "fast compile check only" workflow rule, and left
to whoever performs the campaign's eventual full build/regression pass.
