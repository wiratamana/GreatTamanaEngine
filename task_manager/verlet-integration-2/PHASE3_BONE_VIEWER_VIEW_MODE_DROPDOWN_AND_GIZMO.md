# PHASE3 — Bone Viewer: View-Mode Dropdown, Generalized Gizmo, Selection Wiring (`src/Editor/BoneViewerWindow.h/.cpp`) (v2)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit A/B/C/F). Depends on: Phase 1
(`Selection::SelectModelPart()`/`IsModelPartSelected()`/`ModelPartKind`) and
Phase 2 (`ModelRigCache::GetOrLoad()`) both compiling and passing their own
tests. Produces: the user-visible core of this campaign — a "Bones / Rigid
Bodies / Joints" dropdown in the Bone Viewer's toolbar, a generalized part-
list tree pane + viewport gizmo overlay that redraws correctly for whichever
mode is active, and every selection interaction routed through the shared
`Selection` object instead of the window's own private index.

**v2 note:** three fixes on top of v1, all from
`PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v2)": (1) Depends on
section above now also needs Phase 1's **(v2)** `ClearModelPartIfEntity()`,
called from `EnsureDataLoaded()` (now also taking `EditorContext&`) to stop a
stale Model-Part selection from surviving a genuine reload of this window's
own model data (finding #1); (2) Step 3.6's viewport overlay now generalizes
the name-label/search-highlight behavior to Rigid Body/Joint modes too, not
just the dot/circle/connector-line drawing (finding #2); (3) Step 3.2 now
explicitly lists `RenderFlatPartRow()`'s own declaration (finding #5).
Everything else is unchanged from v1.

## Step 1: The Goal

Transform `BoneViewerWindow` from "always shows Bones" into a window that:

1. Shows a dropdown (an `ImGui::Combo`) in its existing toolbar row, next to
   "Reset View"/"Show All Names", offering exactly "Bones", "Rigid Bodies",
   "Joints".
2. Loads `RigidBody`/`Joint` data (not just `Bone` data) for the currently-
   open model, via Phase 2's shared `ModelRigCache`.
3. Redraws BOTH the left tree pane AND the right 3D viewport's gizmo overlay
   for whichever category the dropdown currently selects — every frame, so
   changing the dropdown takes effect immediately, the same frame it's
   changed (no separate "apply" step, since `Build()` already re-derives
   everything it draws fresh every single call).
4. Routes every "user clicked this bone/rigid body/joint" interaction (tree
   row click, direct viewport gizmo click) through
   `EditorContext::selection.SelectModelPart(m_targetEntity, m_viewMode, index)`
   instead of the window's own private `m_selectedBoneIndex`, and reads
   `ctx.selection.IsModelPartSelected(...)` (instead of comparing against
   `m_selectedBoneIndex` directly) to decide what to highlight.

## Step 2: The Situation / The Problem

See `PHASE0_MASTER_STRATEGY.md`, Culprits A/B/C/F, for the full root-cause
analysis. In short: `BoneViewerWindow.h/.cpp` today has exactly one data
model (`m_bones`/`m_boneChildren`/`m_rootBoneIndices`), one selection concept
(`int m_selectedBoneIndex`), one tree-rendering path
(`BuildBoneTreePane()`/`RenderBoneTreeNode()`/`BoneMatchesFilterRecursive()`),
and one viewport-overlay code block (inside `Build()`, projecting/drawing
only `m_bones`) — all bone-specific by construction, with `Build(Registry&,
Renderer&)`'s signature having no way to reach `EditorContext`/`Selection`
or Phase 2's `ModelRigCache` at all.

## Step 3: The Plan

### 3.1 `BoneViewerWindow.h` — new includes, enum use, and data members

Add `#include "Selection.h"` (for `ModelPartKind`) and forward-declare
`class ModelRigCache;` alongside the existing `class Registry;`/
`class Renderer;` forward declarations. Add `struct EditorContext;` forward
declaration too (only a reference parameter is needed, matching how other
panels take `EditorContext&` without a full include where possible — check
whether a forward declaration is sufficient here, given `EditorContext.h`
itself only needs to be fully visible where its members are actually
dereferenced, i.e. inside `BoneViewerWindow.cpp`, which already can/does
include it fully).

**New struct siblings to `BoneEntry`** (kept in the same "flattened,
overlay-drawing-only" spirit — deliberately NOT the full `RigidBody`/`Joint`
structs from `PhysicsData.h`, which carry fields (`collisionGroupMask`,
spring factors, ...) irrelevant to drawing a gizmo dot):

```cpp
// One rigid body, flattened for overlay drawing - mirrors BoneEntry's own
// "only what this window's overlay actually needs" philosophy. Position/
// rotation are already in the SAME model-local space as MeshData::positions/
// Bone::position (see PhysicsData.h's own RigidBody::translate doc comment)
// - no extra transform needed to compare against a bone's own position.
struct RigidBodyEntry {
    std::string name;
    Vec3 translate;
    Vec3 rotateRadians; // Euler, PMX convention (see PhysicsData.h) - used only for an approximate visual orientation hint, see 3.4.
    RigidBodyShape shape;
    Vec3 shapeSize;
    std::int32_t boneIndex = -1; // Index into m_bones this body is attached to (-1 if unattached) - drawn as a connecting line, see 3.4.
};

// One joint, flattened for overlay drawing.
struct JointEntry {
    std::string name;
    Vec3 translate;
    std::int32_t rigidBodyAIndex = -1; // Index into m_rigidBodies (-1 if out of range/unset).
    std::int32_t rigidBodyBIndex = -1;
};
```

(`RigidBodyShape` is `PhysicsData.h`'s existing enum — `#include
"../Assets/PhysicsData.h"` in `BoneViewerWindow.h`, or forward-declare the
enum's containing header only in the `.cpp` if the header itself can get
away with an opaque `std::uint8_t`-backed forward decl; simplest and
safest is a direct `#include "../Assets/PhysicsData.h"`, mirroring how
`SkeletonData.h`'s `Bone` type was already directly usable via `RigFile.h`'s
own transitive include chain today.)

Add new members, alongside the existing `m_bones`/`m_boneChildren`/
`m_rootBoneIndices`:

```cpp
std::vector<RigidBodyEntry> m_rigidBodies;
std::vector<JointEntry> m_joints;

// Which of the three categories the toolbar dropdown currently shows -
// persisted across frames (like m_showAllNames), NOT reset on reload (a
// user reloading/reopening onto a different model most likely wants to
// keep looking at the same category they were just looking at).
ModelPartKind m_viewMode = ModelPartKind::Bone;
```

**Delete** `int m_selectedBoneIndex = -1;` entirely — replaced by
`Selection` (Phase 1). Every place that read/wrote it is rewired in 3.4
below.

### 3.2 `BoneViewerWindow.h` — signature changes

```cpp
// Build() gains ctx (to read/write the shared Selection - see
// PHASE0_MASTER_STRATEGY.md, Culprit C/F) and rigCache (Phase 2's shared
// ModelRigCache, used to load m_bones/m_rigidBodies/m_joints instead of this
// class decoding RigFileData itself - see Culprit A/E). Both are owned by
// ImGuiEditorLayer and passed by reference, same pattern as `registry`/
// `renderer` already are.
void Build(Registry& registry, Renderer& renderer, EditorContext& ctx, ModelRigCache& rigCache);
```

**(v2)** `EnsureDataLoaded()` gains BOTH a `ModelRigCache&` parameter (it no
longer decodes `RigFileData` itself — see 3.3) AND an `EditorContext& ctx`
parameter, so it can call Phase 1's **(v2)**
`ctx.selection.ClearModelPartIfEntity(m_targetEntity)` the moment a genuine
reload starts (see 3.3 and `PHASE0_MASTER_STRATEGY.md`'s "Revision Notes
(v2)", finding #1):

```cpp
bool EnsureDataLoaded(Renderer& renderer, const std::string& absoluteGtaPath, EditorContext& ctx, ModelRigCache& rigCache);
```

The one call site inside `Build()` (today: `EnsureDataLoaded(renderer,
source->gtaPath)`) updates to `EnsureDataLoaded(renderer, source->gtaPath,
ctx, rigCache)` — `ctx` is already a `Build()` parameter (above), so this is
a same-function, no-extra-plumbing change.

`RenderBoneTreeNode()`/`BoneMatchesFilterRecursive()`/`BuildBoneTreePane()`
are renamed and generalized (see 3.4) — update their declarations to:

```cpp
void RenderBoneTreeNode(std::int32_t boneIndex, const std::string& lowerFilter, int depth, EditorContext& ctx);
bool BoneMatchesFilterRecursive(std::int32_t boneIndex, const std::string& lowerFilter, int depth) const; // unchanged signature - still bone-tree-specific, see 3.4.
void BuildPartListPane(const std::string& lowerFilter, EditorContext& ctx); // renamed from BuildBoneTreePane().
```

**(v2)** Add one brand-new private method declaration alongside the three
above — v1 defined this method's BODY in 3.4 and named it in Step 5's own
checklist, but never actually listed its declaration here, leaving its exact
signature/placement implicit:

```cpp
// Renders one non-tree (Rigid Body/Joint) row - a single, non-indented,
// non-expandable Selectable, reusing the same search-filter/selection/
// double-click-recenter shape RenderBoneTreeNode()'s own leaf case already
// has, just without the tree-node machinery (Rigid Body/Joint have no
// bind-pose parent/child tree to walk - see PHASE0_MASTER_STRATEGY.md,
// Culprit B). See 3.4 for the full body.
void RenderFlatPartRow(ModelPartKind kind, std::int32_t index, const std::string& name, const Vec3& position, const std::string& lowerFilter, EditorContext& ctx);
```

### 3.3 `BoneViewerWindow.cpp` — `EnsureDataLoaded()`: load all three categories via `ModelRigCache`

Replace the existing inline `DecodeRigDataFromBytes(gta->metadata)` call
(and everything downstream of it that only ever populated `m_bones`) with a
call into the shared cache:

```cpp
bool BoneViewerWindow::EnsureDataLoaded(Renderer& renderer, const std::string& absoluteGtaPath, EditorContext& ctx, ModelRigCache& rigCache)
{
    // ... existing mtime short-circuit / ReadGtaFile()/DecodeMeshDataFromBytes()
    // logic for the MESH PAYLOAD (vertex buffers) stays EXACTLY as it is
    // today - ModelRigCache never touches the payload, only the metadata
    // (see PHASE2_SHARED_RIG_DATA_CACHE.md, 3.1's own class comment) ...

    m_bones.clear();
    m_rigidBodies.clear();
    m_joints.clear();

    // (v2) A genuine reload is starting (we did not take the mtime
    // short-circuit return above) - whatever Model-Part index Selection may
    // still be holding for THIS window's own m_targetEntity means nothing
    // against the data about to be (re)loaded (mirrors the private
    // m_selectedBoneIndex = -1 reset this same code used to do before Phase
    // 1 moved selection out of this class - see
    // PHASE0_MASTER_STRATEGY.md's "Revision Notes (v2)", finding #1). Only
    // clears it if it currently belongs to m_targetEntity - a Model-Part
    // selection belonging to some OTHER entity (e.g. the user picked a
    // different entity in Hierarchy since) is correctly left untouched.
    ctx.selection.ClearModelPartIfEntity(m_targetEntity);

    if (const RigFileData* rig = rigCache.GetOrLoad(absoluteGtaPath)) {
        m_bones.reserve(rig->skeleton.bones.size());
        for (const Bone& bone : rig->skeleton.bones) {
            m_bones.push_back(BoneEntry{ bone.name, bone.position, bone.parentBoneIndex });
        }

        m_rigidBodies.reserve(rig->physics.rigidBodies.size());
        for (const RigidBody& body : rig->physics.rigidBodies) {
            m_rigidBodies.push_back(RigidBodyEntry{ body.name, body.translate, body.rotateRadians, body.shape, body.shapeSize, body.boneIndex });
        }

        m_joints.reserve(rig->physics.joints.size());
        for (const Joint& joint : rig->physics.joints) {
            m_joints.push_back(JointEntry{ joint.name, joint.translate, joint.rigidBodyAIndex, joint.rigidBodyBIndex });
        }
    }
    RebuildBoneHierarchyIndex(); // unchanged - still only walks m_bones, RigidBody/Joint have no tree to build.

    // ... existing vertex/index-buffer upload logic, unchanged ...
}
```

Note this DELIBERATELY does not change the "mtime short-circuit" logic at
the TOP of `EnsureDataLoaded()` at all (the `m_cachedPath`/
`m_cachedWriteTime`/`m_cachedIsValid` block that decides whether to reload
the MESH payload) — that stays exactly as-is; the change above only touches
what happens to the METADATA half once a reload is already underway. Two
independent mtime checks now exist for the same file (this window's own, for
the mesh payload, and `ModelRigCache`'s own internal one, for the metadata)
— an accepted, explicitly-documented minor duplication (see
`PHASE2_SHARED_RIG_DATA_CACHE.md`, 3.1's own class comment) rather than a
bug.

Reset `m_bones`/`m_rigidBodies`/`m_joints` (all three, not just `m_bones`) in
every place the existing code already resets `m_bones` alone — `Reset()`
and the top of `EnsureDataLoaded()`'s "reload starting" block. **(v2)** The
new `ctx.selection.ClearModelPartIfEntity(m_targetEntity)` call belongs ONLY
at that same "reload starting" point in `EnsureDataLoaded()` — `Reset()`
itself is not touched (it has no `EditorContext&` to reach `ctx.selection`
with, and is only ever called from the destructor, where the whole window
and its shared `Selection`/`ModelRigCache` references are going away
together anyway - see `BoneViewerWindow.h`'s own class comment on `Reset()`).

### 3.4 `BoneViewerWindow.cpp` — generalized tree pane

Rename `BuildBoneTreePane()` → `BuildPartListPane()`, adding an `EditorContext&
ctx` parameter, and branch on `m_viewMode`:

```cpp
void BoneViewerWindow::BuildPartListPane(const std::string& lowerFilter, EditorContext& ctx)
{
    switch (m_viewMode) {
    case ModelPartKind::Bone:
        if (m_bones.empty()) { ImGui::TextDisabled("(no bones)"); return; }
        for (const std::int32_t root : m_rootBoneIndices) {
            RenderBoneTreeNode(root, lowerFilter, 0, ctx);
        }
        return;
    case ModelPartKind::RigidBody:
        if (m_rigidBodies.empty()) { ImGui::TextDisabled("(no rigid bodies)"); return; }
        for (std::size_t i = 0; i < m_rigidBodies.size(); ++i) {
            RenderFlatPartRow(ModelPartKind::RigidBody, static_cast<std::int32_t>(i), m_rigidBodies[i].name, m_rigidBodies[i].translate, lowerFilter, ctx);
        }
        return;
    case ModelPartKind::Joint:
        if (m_joints.empty()) { ImGui::TextDisabled("(no joints)"); return; }
        for (std::size_t i = 0; i < m_joints.size(); ++i) {
            const JointEntry& joint = m_joints[i];
            std::string label = joint.name.empty() ? ("Joint " + std::to_string(i)) : joint.name;
            RenderFlatPartRow(ModelPartKind::Joint, static_cast<std::int32_t>(i), label, joint.translate, lowerFilter, ctx);
        }
        return;
    }
}
```

`RenderBoneTreeNode()` keeps its existing recursive-tree body UNCHANGED
except for the selection plumbing (highlight/click), which now reads/writes
through `ctx.selection` instead of `m_selectedBoneIndex`:

```cpp
// Was: if (m_selectedBoneIndex == boneIndex) flags |= ImGuiTreeNodeFlags_Selected;
if (ctx.selection.IsModelPartSelected(m_targetEntity, ModelPartKind::Bone, boneIndex)) {
    flags |= ImGuiTreeNodeFlags_Selected;
}
...
if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
    // Was: m_selectedBoneIndex = boneIndex;
    ctx.selection.SelectModelPart(m_targetEntity, ModelPartKind::Bone, boneIndex);
    if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        m_camTarget = bone.position;
    }
}
```

Add a new small helper, `RenderFlatPartRow()`, for the two non-tree
categories — a single, non-indented, non-expandable selectable row (no
children, no `TreePop()`), reusing the exact same search-filter/selection/
double-click-recenter shape `RenderBoneTreeNode()`'s leaf case already has,
just without the tree-node machinery:

```cpp
void BoneViewerWindow::RenderFlatPartRow(ModelPartKind kind, std::int32_t index, const std::string& name,
    const Vec3& position, const std::string& lowerFilter, EditorContext& ctx)
{
    if (!lowerFilter.empty() && ToLower(name).find(lowerFilter) == std::string::npos) {
        return; // Same "search prunes the list" convention as the bone tree.
    }

    const bool isSelected = ctx.selection.IsModelPartSelected(m_targetEntity, kind, index);
    const std::string label = name.empty() ? ("Part " + std::to_string(index)) : name;

    ImGui::PushID(index);
    if (ImGui::Selectable(label.c_str(), isSelected)) {
        ctx.selection.SelectModelPart(m_targetEntity, kind, index);
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_camTarget = position;
        }
    }
    ImGui::PopID();
}
```

`BoneMatchesFilterRecursive()` stays exactly as-is (only ever called from
`RenderBoneTreeNode()`'s own Bone-mode path — RigidBody/Joint rows filter
themselves directly by name, with no descendant concept to recurse into).

### 3.5 `BoneViewerWindow.cpp` — toolbar dropdown

In `Build()`'s toolbar section (right after the existing "Show All Names"
checkbox, before the bone-count `TextDisabled` line — or reposition the
count/warning text to be mode-aware, see below), add:

```cpp
ImGui::SameLine();
ImGui::PushItemWidth(140.0f);
static const char* kViewModeLabels[] = { "Bones", "Rigid Bodies", "Joints" };
int viewModeIndex = static_cast<int>(m_viewMode);
if (ImGui::Combo("View", &viewModeIndex, kViewModeLabels, static_cast<int>(std::size(kViewModeLabels)))) {
    m_viewMode = static_cast<ModelPartKind>(viewModeIndex);
}
ImGui::PopItemWidth();
```

Update the existing count line (`"%zu bones - %u verts / %u tris"`) to be
mode-aware, e.g.:

```cpp
const std::size_t partCount = m_viewMode == ModelPartKind::Bone ? m_bones.size()
    : m_viewMode == ModelPartKind::RigidBody ? m_rigidBodies.size() : m_joints.size();
const char* partNoun = m_viewMode == ModelPartKind::Bone ? "bones"
    : m_viewMode == ModelPartKind::RigidBody ? "rigid bodies" : "joints";
ImGui::TextDisabled("%zu %s - %u verts / %u tris", partCount, partNoun, m_vertexCount, m_indexCount / 3);
```

Update the existing "no bone/skeleton data" warning to only show in Bone
mode (`m_viewMode == ModelPartKind::Bone && m_bones.empty()`), and add
sibling warnings for the other two modes (`"This model has no rigid-body
physics data."` / `"This model has no joint physics data."`), same styling.

Update the call to the (renamed) tree-pane builder:
`BuildPartListPane(lowerFilter, ctx);` (was `BuildBoneTreePane(lowerFilter);`).

### 3.6 `BoneViewerWindow.cpp` — generalized viewport gizmo overlay

The existing overlay-drawing block inside `Build()` (screen-projection +
hover hit-test + drag/selection + dot/line drawing) is restructured into
three per-mode branches sharing the same `ProjectToScreen()` helper
(unchanged — it is already position/rect-agnostic) and the same hover/
click-handling SHAPE, but different SOURCE DATA/DRAWING per mode:

**Bone mode** — UNCHANGED from today's behavior byte-for-byte: dot + parent
line, green base color, orange selected outline, PLUS the existing name-label
overlay (drawn above the dot whenever `m_showAllNames` is on, OR the bone's
name matches the search filter - drawn yellow in that case - OR the dot is
currently hovered/selected) — except the selection read/write at the bottom
now goes through `ctx.selection.IsModelPartSelected(
m_targetEntity, ModelPartKind::Bone, i)` / `ctx.selection.SelectModelPart(
m_targetEntity, ModelPartKind::Bone, hoveredBoneIndex)` instead of
`m_selectedBoneIndex`. **(v2)** This name-label/search-color behavior is
explicitly called out here because v1 only generalized it implicitly (via
the shared helper described at the end of this section) but never actually
said Rigid Body/Joint modes get it too — see the two modes below.

**Rigid Body mode** — for each `RigidBodyEntry`:
- Project `translate` to screen exactly like a bone position.
- Draw a dot at that point (distinct base color from bones — e.g.
  `IM_COL32(80, 180, 255, 255)`, a cyan/blue, chosen to be visually distinct
  from the existing green bone dots and the orange/white selected/hovered
  overrides, which stay the SAME override colors across all three modes for
  consistency).
- Draw an approximate "size" indicator: project a SECOND point offset from
  `translate` along the view-right axis by the shape's characteristic size
  (`shapeSize.x` for `Sphere`/`Capsule` radius, `Length(shapeSize)` for
  `Box`'s half-extent — a deliberately approximate, screen-space-only
  silhouette, NOT a true oriented 3D wireframe box/capsule — see
  `PHASE0_MASTER_STRATEGY.md`, "What We Will NOT Do"), then draw a circle
  whose pixel radius is the on-screen distance between the two projected
  points (`drawList->AddCircle(center, pixelRadius, color, 0, 1.5f)`,
  UNFILLED — the small filled dot at the center already marks the exact
  origin, the outline circle is purely the size hint).
- If `boneIndex >= 0` and in range, additionally draw a thin connecting line
  from this rigid body's screen position to its attached bone's screen
  position (reusing `m_bones[boneIndex].position`, projected the same way),
  in a dimmer variant of the rigid-body color — the "which bone drives this"
  hint, symmetrical with a bone's own parent-line.
- Hover hit-test / click-to-select / double-click-to-recenter: identical
  shape to Bone mode, just iterating `m_rigidBodies` and calling
  `ctx.selection.SelectModelPart(m_targetEntity, ModelPartKind::RigidBody, i)`.
- **(v2)** Name-label/search-color overlay: identical shape to Bone mode too
  (see the `PHASE0_MASTER_STRATEGY.md` "Revision Notes (v2)", finding #2) —
  the label drawn above the dot is `RigidBodyEntry::name` (or `"Part " +
  index` if empty, matching `RenderFlatPartRow()`'s own fallback label
  exactly), shown whenever `m_showAllNames` is on, OR the name matches the
  search filter (dot recolored yellow in that case, same as Bone mode's
  `matchesFilter` tier), OR the dot is currently hovered/selected (white/
  orange overrides, unchanged) — the toolbar's search box and "Show All
  Names" checkbox stay meaningful in this mode instead of only affecting
  the tree pane.

**Joint mode** — for each `JointEntry`:
- Project `translate` to screen; draw a dot in a THIRD distinct base color
  (e.g. `IM_COL32(200, 120, 255, 255)`, a violet/magenta).
- If `rigidBodyAIndex`/`rigidBodyBIndex` are both in range, draw TWO thin
  lines: joint→bodyA's screen position and joint→bodyB's screen position
  (both projected from `m_rigidBodies[...].translate`) — the "what this
  joint connects" hint, doubling the single-parent-line pattern bones/rigid
  bodies each only need one of.
- Hover hit-test / click-to-select / double-click-to-recenter: identical
  shape again, iterating `m_joints`, `ModelPartKind::Joint`.
- **(v2)** Name-label/search-color overlay: identical shape to Bone/Rigid
  Body mode — the label is `JointEntry::name` (or `"Joint " + index` if
  empty, matching `BuildPartListPane()`'s own existing tree-row fallback
  label exactly), same `m_showAllNames`/search-match-yellow/hovered-white/
  selected-orange layering as the other two modes.

**(v2)** Restructure the existing single "compute `screenPositions`/
`onScreen` arrays sized to `m_bones.size()`" block into a small local helper
invoked with whichever category's own position list AND name list is
relevant this frame (e.g. parallel local `std::vector<Vec3>` /
`std::vector<std::string>` of "positions/names to project and label this
frame", built once per mode at the top of the overlay block: `m_viewMode ==
Bone` → each `m_bones[i].position`/`m_bones[i].name`; `RigidBody` → each
`m_rigidBodies[i].translate`/`m_rigidBodies[i].name` (or `"Part " + i` if
empty); `Joint` → each `m_joints[i].translate`/`m_joints[i].name` (or
`"Joint " + i` if empty)) — this keeps BOTH the hover/hit-test math (already
generic — nearest on-screen point within a pixel radius) AND the name-label/
search-color drawing loop (also already generic once it reads from this same
parallel names list instead of hardcoding `m_bones[i].name`) written ONCE and
reused for all three modes, rather than tripled.

### 3.7 `ImGuiEditorLayer.cpp` — wiring

Add a new member, `ModelRigCache m_modelRigCache;` (declared alongside the
existing `m_assetPreview`/`m_assetPreviewMesh`/`m_boneViewer` members, inside
the same `#if GTE_ENABLE_PROJECT_PANEL` block — `#include "ModelRigCache.h"`
alongside the existing `#include "BoneViewerWindow.h"`).

Update the one call site in `BuildUI()`:

```cpp
// Was: m_boneViewer.Build(registry, renderer);
m_boneViewer.Build(registry, renderer, m_ctx, m_modelRigCache);
```

(Phase 4 will separately thread `m_modelRigCache` into `BuildInspectorPanel()`
too — that call-site edit belongs to Phase 4's own document, since it also
needs `InspectorPanel.h`'s signature to change; this phase only needs to add
the member and update the ONE call site above it already owns.)

## Step 4: What We Will NOT Do

- We will **not** attempt a true, oriented 3D wireframe box/capsule/sphere
  for the Rigid Body gizmo — the screen-space dot + approximate-radius
  circle described in 3.6 is a deliberate, documented simplification (see
  `PHASE0_MASTER_STRATEGY.md`, Step 4).
- We will **not** honor `RigidBody::rotateRadians`/PMX's own Euler rotation
  ORDER convention exactly for the size-hint circle's offset direction — an
  arbitrary, fixed screen-space offset axis (view-right) is sufficient for a
  debug-only size hint; getting this pixel-perfect is out of scope (matches
  the same "not full PMX physics fidelity" scope limit already established).
- We will **not** change how `m_bones`'s own tree/parent-line Bone-mode
  behavior looks or works in any way — every existing Bone-mode pixel/
  interaction must stay identical to today, verified by manually reopening
  the window on a real model after this phase and confirming Bone mode is
  visually unchanged.
- We will **not** persist `m_viewMode` to disk/across process restarts — an
  in-memory-only, per-session default (`ModelPartKind::Bone`), same as every
  other `BoneViewerWindow` UI toggle today (`m_showAllNames`, `m_treeWidth`).
- We will **not** yet make `InspectorPanel` show anything about a selected
  model part — that is Phase 4's job. This phase's own manual verification
  is limited to: the dropdown works, the tree pane/viewport gizmo redraw
  correctly per mode, and clicking a part visibly highlights consistently
  between the tree row and its viewport dot (both now reading the SAME
  `ctx.selection`). It is EXPECTED and ACCEPTABLE that, immediately after
  this phase alone, the Inspector panel either shows an unrelated/stale
  entity or "No entity selected" while a model part is selected in the Bone
  Viewer — `ctx.selection.Kind()` is now `ModelPart`, and
  `InspectorPanel::BuildInspectorPanel()` has no branch for that yet (see
  `PHASE0_MASTER_STRATEGY.md`, Culprit D) — this is a known, temporary,
  self-correcting rough edge closed by Phase 4, not a regression to chase
  down within this phase.

## Step 5: Their Role

Implementer checklist for this phase:

1. Edit `src/Editor/BoneViewerWindow.h` per 3.1/3.2 (new includes/forward
   decls, `RigidBodyEntry`/`JointEntry`, `m_rigidBodies`/`m_joints`/
   `m_viewMode` members, delete `m_selectedBoneIndex`, update every changed
   method signature — including **(v2)** `EnsureDataLoaded()`'s new
   `EditorContext& ctx` parameter — and add `RenderFlatPartRow()`'s
   declaration).
2. Edit `src/Editor/BoneViewerWindow.cpp` per 3.3–3.6 (load all three
   categories via `ModelRigCache` in `EnsureDataLoaded()` — including
   **(v2)** the new `ctx.selection.ClearModelPartIfEntity(m_targetEntity)`
   call at the top of its "reload starting" block, and updating its one call
   site inside `Build()` to pass `ctx` — generalize the tree pane, add the
   toolbar dropdown, generalize the viewport overlay INCLUDING **(v2)** the
   name-label/search-color overlay for Rigid Body/Joint modes, not just
   Bone mode). Reset `m_rigidBodies`/`m_joints` everywhere `m_bones` is
   already reset.
3. Edit `src/Editor/ImGuiEditorLayer.cpp` per 3.7 (new `m_modelRigCache`
   member, update the one `m_boneViewer.Build(...)` call site).
4. Build `GreatTamanaEngine` (the real executable, not just tests — this
   phase's correctness is primarily visual/interactive) and manually verify,
   against a real imported MMD model with rigid bodies/joints:
   - Bone mode looks and behaves EXACTLY as before this phase.
   - Switching to "Rigid Bodies" immediately redraws the tree pane (flat
     list) and viewport (cyan dots + size circles + bone-attachment lines)
     with no extra click/refresh needed.
   - Switching to "Joints" immediately redraws similarly (violet dots +
     two-body connector lines).
   - Clicking a row in any mode highlights the matching viewport dot, and
     clicking a viewport dot highlights the matching tree row — both derived
     from the SAME `ctx.selection.IsModelPartSelected()` call.
   - Switching modes back and forth, and reloading a different model, never
     crashes/asserts even if the newly-active mode's own list is empty for
     that model (e.g. a model with bones but no physics data at all).
   - **(v2)** Toggling "Show All Names" and typing into the search box while
     in "Rigid Bodies"/"Joints" mode visibly labels/highlights the viewport
     dots too, not just the tree rows (the finding #2 fix).
   - **(v2)** With a bone selected (Inspector/tree row highlighted), trigger
     a genuine reload of the SAME model (e.g. re-import the same `.pmx`
     through the Project panel while the Bone Viewer is open on an entity
     spawned from it) and confirm the selection/highlight clears rather than
     silently pointing at whatever the same numeric index now means in the
     freshly reloaded data (the finding #1 fix).
