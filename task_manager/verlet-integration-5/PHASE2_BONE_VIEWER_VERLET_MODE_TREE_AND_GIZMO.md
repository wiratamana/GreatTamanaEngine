# PHASE2 — Bone Viewer: "Verlet" Mode Tree Pane and Viewport Gizmo (`src/Editor/BoneViewerWindow.h/.cpp`)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprits A, C, D, E). Depends on:
Phase 1 (`ModelPartKind::Verlet` and
`FindDynamicChainJointByBoneIndex()`/`DynamicChainJointLocation` compiling
and passing their own tests — this phase does not actually call the lookup
helper itself, Phase 3 does, but it must exist and compile first per the
master strategy's stated dependency order). Produces: the user-visible core
of this campaign — a 4th "Verlet" entry in the Bone Viewer's "View"
dropdown, a chain-grouped tree pane, and a fully-integrated viewport gizmo
(particle dots, chain connector lines, root/anchor markers, optional
head-collider wireframe), all sourced live from `PhysicsSystem`'s own
already-running `DynamicChainRigCache`.

## Step 1: The Goal

1. Add "Verlet" as a 4th option in the existing "Bones / Rigid Bodies /
   Joints" toolbar dropdown.
2. Show, in the left tree pane, every detected chain for the currently-
   inspected model, grouped under a per-chain header, with each chain's
   joints listed underneath as selectable rows (reusing the existing
   `RenderFlatPartRow()` machinery, not a new parallel implementation).
3. Draw, in the right 3D viewport, every joint as a distinctly-colored dot,
   every chain's joints connected root-to-tip by lines, each chain's own
   pinned root/anchor bone as a small distinct (non-selectable) marker, and
   — when a chain has one configured — its head-collider sphere as a real
   wireframe (reusing `RigidBodyWireframe.h`, the exact module the Rigid
   Body gizmo already uses for its own selected-shape wireframe).
4. Source every one of the above from `PhysicsSystem`'s own
   `DynamicChainRigCache` (via a newly-threaded `PhysicsSystem&` parameter),
   never from a second, independently-computed `DetectDynamicChains()` call
   — the exact same data (and, critically, the exact same LIVE-EDITED
   damping/stiffness/mass/head-collider values) the Inspector's existing
   "Dynamic Chain Physics" section already reads and writes.
5. Route every selection interaction (tree row click, direct viewport
   click) through `Selection` exactly like the other three modes, using the
   bone-index `partIndex` scheme Phase 1 established.

## Step 2: The Situation / The Problem

See `PHASE0_MASTER_STRATEGY.md`'s Culprits A/C/D/E for the full analysis. In
short: `BoneViewerWindow` has a well-established, 3-times-repeated shape for
"add a mode" (`verlet-integration-2`'s own Phase 3 laid it down; `verlet-
integration-3`/`-4` extended it further) — a `ModelPartKind` branch in the
toolbar label/count/warning-text, a `BuildPartListPane()` switch case, and a
`m_viewMode` branch in the viewport's connector-line/dot-color/wireframe
logic — but every one of those branches, plus the shared `OverlayPart`/
`overlayParts` abstraction and the Shift-range-select code, was written
assuming exactly 3 modes and an "overlay slot index == partIndex" identity
that does not hold for Verlet mode (Culprit C). `BoneViewerWindow` also has
zero access to `PhysicsSystem` today (Culprit E) — its only two collaborator
references are `ModelRigCache&` (asset-file-backed) and `EditorContext&`
(selection).

## Step 3: The Plan

### 3.1 `BoneViewerWindow.h` — new include/forward-declaration and `Build()` signature

Add `class PhysicsSystem;` alongside the existing `class ModelRigCache;`
forward declaration (no full `#include` needed in the header — only a
reference parameter is required, exactly like `ModelRigCache&`/
`EditorContext&` already work).

```cpp
// Build() gains physicsSystem (task_manager/verlet-integration-5,
// PHASE0_MASTER_STRATEGY.md, Culprit E) - the shared source of truth for
// "Verlet" mode's chain topology/joint parameters, owned by Game
// (Game::GetPhysicsSystem()) and passed by reference exactly like
// registry/renderer/rigCache already are. Looked up FRESH every Build()
// call (PhysicsSystem::GetDynamicChainRigCache().TryGet()) - deliberately
// NOT cached into a new member field the way m_bones/m_rigidBodies/m_joints
// are, since DynamicChainRigCache is already an in-memory, already-
// populated, O(1)-lookup map with no on-disk mtime concept to gate a reload
// against in the first place (contrast with ModelRigCache, which genuinely
// needs EnsureDataLoaded()'s own mtime-gated reload logic).
void Build(Registry& registry, Renderer& renderer, EditorContext& ctx, ModelRigCache& rigCache, PhysicsSystem& physicsSystem);
```

`EnsureDataLoaded()`'s own signature is **unchanged** — it does not need
`PhysicsSystem&` at all (see Step 2's own reasoning above); Verlet data is
read directly inside `Build()`/`BuildPartListPane()`, never cached
alongside `m_bones`/`m_rigidBodies`/`m_joints`.

`BuildPartListPane()` gains the same new parameter:

```cpp
void BuildPartListPane(const std::string& lowerFilter, EditorContext& ctx, PhysicsSystem& physicsSystem);
```

Add one new private method declaration, alongside `RenderFlatPartRow()`:

```cpp
// Renders one detected dynamic bone chain as a non-selectable, always-
// expanded ImGui tree header (e.g. "Chain 0 - Root: waist (3 joints)"),
// with each of its joints rendered underneath via the EXISTING
// RenderFlatPartRow() (ModelPartKind::Verlet, partIndex = that joint's own
// bone index - see task_manager/verlet-integration-5/
// PHASE1_CHAIN_LOOKUP_AND_SELECTION_FOUNDATION.md's own "bone index, not a
// flattened counter" decision). Hidden entirely (no header drawn at all) if
// a non-empty `lowerFilter` matches NONE of this chain's own joint names -
// the "search prunes the tree" convention every other mode's own pane
// already follows (see BoneMatchesFilterRecursive()'s doc comment).
void RenderVerletChainNode(std::int32_t chainIndex, const DynamicChainDefinition& chain,
    const std::string& lowerFilter, EditorContext& ctx);
```

Add the necessary includes to `BoneViewerWindow.h` for this declaration to
compile: `#include "../Physics/DynamicChainDefinition.h"` (for
`DynamicChainDefinition` itself — a small, header-only, ECS/GPU-free
struct, safe to include directly here exactly like `PhysicsData.h` already
is for `RigidBodyShape`).

### 3.2 `BoneViewerWindow.cpp` — new includes

```cpp
#include "../Game/Physics/PhysicsSystem.h" // PhysicsSystem::GetDynamicChainRigCache()
#include "../Physics/DynamicChainDefinition.h" // DynamicChainDefinition, DynamicJointSettings (already forward-used via the header above, but the .cpp needs the full definition too for member access)
```

(`RigidBodyWireframe.h` is already included today, for Rigid Body mode's own
wireframe — no new include needed for the head-collider sphere reuse.)

### 3.3 `BoneViewerWindow.cpp` — toolbar dropdown, count, and warning text (4-way)

```cpp
// Was: static const char* kViewModeLabels[] = { "Bones", "Rigid Bodies", "Joints" };
static const char* kViewModeLabels[] = { "Bones", "Rigid Bodies", "Joints", "Verlet" };
```

The `Combo()` call itself is unchanged (`std::size(kViewModeLabels)` already
adapts automatically).

The `partCount`/`partNoun` computation and the "no data" warning both need a
4th branch. Since Verlet mode's "count" is naturally "how many chains/
joints", not a single flat count, compute BOTH pieces once, right where the
existing ternary chain lives, reading the SAME `DynamicChainRigCache::
ModelEntry*` this step's own overlay code will need a few lines later (see
3.6) — fetch it ONCE per `Build()` call, right after `EnsureDataLoaded()`
succeeds, and pass it down to both the toolbar text and `BuildPartListPane()`/
the overlay code, rather than calling `TryGet()` redundantly three separate
times per frame:

```cpp
// Fetched once per Build() call (Culprit E) - PhysicsSystem's own
// DynamicChainRigCache is already an in-memory map, so this is a cheap,
// always-safe-to-repeat lookup; doing it once here and threading the
// pointer down avoids three redundant identical hash lookups per frame.
// May be nullptr (no entry registered yet for this path - see
// PhysicsSystem::RegisterDynamicChains()'s own "called once, at
// CreateMeshEntityFromGtaFile() time" contract; this can only happen for
// an entity that was somehow never spawned that way, which BoneViewerWindow
// can only ever be opened on in the first place via a MeshAssetSource-
// carrying entity - see the "source == nullptr" check just above - so this
// should be non-null in every real, reachable case, but is still handled
// gracefully rather than assumed).
const DynamicChainRigCache::ModelEntry* verletModel = physicsSystem.GetDynamicChainRigCache().TryGet(source->gtaPath);
```

Place this line immediately after the existing
`if (!EnsureDataLoaded(...) || !m_cachedIsValid) { ... }` early-return block
in `Build()`.

Then extend the toolbar text:

```cpp
std::size_t verletJointCount = 0;
std::size_t verletChainCount = verletModel != nullptr ? verletModel->chains.size() : 0;
if (verletModel != nullptr) {
    for (const DynamicChainDefinition& chain : verletModel->chains) {
        verletJointCount += chain.jointBoneIndices.size();
    }
}

const std::size_t partCount = m_viewMode == ModelPartKind::Bone ? m_bones.size()
    : m_viewMode == ModelPartKind::RigidBody ? m_rigidBodies.size()
    : m_viewMode == ModelPartKind::Joint      ? m_joints.size()
                                              : verletJointCount;
const char* partNoun = m_viewMode == ModelPartKind::Bone ? "bones"
    : m_viewMode == ModelPartKind::RigidBody ? "rigid bodies"
    : m_viewMode == ModelPartKind::Joint      ? "joints"
                                              : "verlet joints";
ImGui::TextDisabled("%zu %s - %u verts / %u tris", partCount, partNoun, m_vertexCount, m_indexCount / 3);
if (m_viewMode == ModelPartKind::Verlet) {
    ImGui::SameLine();
    ImGui::TextDisabled("(%zu chain%s)", verletChainCount, verletChainCount == 1 ? "" : "s");
}
```

Extend the warning-text `if`/`else if` chain with a 4th branch:

```cpp
} else if (m_viewMode == ModelPartKind::Verlet && verletJointCount == 0) {
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
        "This model has no detected dynamic (Verlet) bone-chain physics data.");
}
```

### 3.4 `BoneViewerWindow.cpp` — `BuildPartListPane()`'s new `case`

```cpp
void BoneViewerWindow::BuildPartListPane(const std::string& lowerFilter, EditorContext& ctx, PhysicsSystem& physicsSystem)
{
    switch (m_viewMode) {
    case ModelPartKind::Bone: /* unchanged */
    case ModelPartKind::RigidBody: /* unchanged */
    case ModelPartKind::Joint: /* unchanged */
    case ModelPartKind::Verlet: {
        // Re-fetched here rather than threaded as a parameter into this
        // ONE method - BuildPartListPane() already independently decides
        // what to draw per m_viewMode with no outside help today (see its
        // own existing 3 cases); re-deriving physicsSystem's cheap map
        // lookup locally keeps that same self-contained shape, and this is
        // the SAME cheap O(1) lookup already explained in 3.3 above.
        const DynamicChainRigCache::ModelEntry* model = physicsSystem.GetDynamicChainRigCache().TryGet(/* see below */);
        ...
    }
    }
}
```

**Correction/simplification before finalizing 3.4's body:** `BuildPartListPane()`
has no direct access to `source->gtaPath` (that local only exists inside
`Build()`). Rather than plumb the path string down as a SECOND new
parameter (in addition to `physicsSystem`), thread the already-resolved
`const DynamicChainRigCache::ModelEntry* verletModel` pointer itself down
from `Build()` (computed once per 3.3 above) as `BuildPartListPane()`'s new
parameter INSTEAD of `PhysicsSystem&`:

```cpp
void BuildPartListPane(const std::string& lowerFilter, EditorContext& ctx, const DynamicChainRigCache::ModelEntry* verletModel);
```

(Update 3.1's header declaration to match this exact signature instead of
the `PhysicsSystem&` one shown above — `PhysicsSystem&` itself is only ever
needed at the one call site inside `Build()` that performs the `TryGet()`
lookup; every downstream consumer just needs the resulting pointer, which
may legitimately be `nullptr`.) This mirrors the existing precedent of
`m_bones`/`m_rigidBodies`/`m_joints` already being pre-resolved, plain data
handed to `BuildPartListPane()` implicitly via `this` — `verletModel` is the
one piece of Verlet-mode data that cannot live as a member (Culprit E's own
"no new caching member" decision), so it travels as an explicit parameter
instead, the smallest deviation from the existing pattern that still avoids
a second redundant `TryGet()` call.

Final body:

```cpp
case ModelPartKind::Verlet:
    if (verletModel == nullptr || verletModel->chains.empty()) {
        ImGui::TextDisabled("(no dynamic bone chains)");
        return;
    }
    for (std::size_t i = 0; i < verletModel->chains.size(); ++i) {
        RenderVerletChainNode(static_cast<std::int32_t>(i), verletModel->chains[i], lowerFilter, ctx);
    }
    return;
```

The one call site inside `Build()` updates to
`BuildPartListPane(lowerFilter, ctx, verletModel);`.

### 3.5 `BoneViewerWindow.cpp` — `RenderVerletChainNode()`

```cpp
void BoneViewerWindow::RenderVerletChainNode(std::int32_t chainIndex, const DynamicChainDefinition& chain,
    const std::string& lowerFilter, EditorContext& ctx)
{
    // "Search prunes the tree" - hide the WHOLE chain header if a non-empty
    // filter matches none of its joints' own names (mirrors
    // BoneMatchesFilterRecursive()'s own reasoning, just non-recursive
    // since a chain's joints have no further descendants of their own).
    bool anyMatch = lowerFilter.empty();
    for (std::size_t j = 0; j < chain.jointBoneIndices.size() && !anyMatch; ++j) {
        const std::int32_t boneIndex = chain.jointBoneIndices[j];
        if (boneIndex >= 0 && static_cast<std::size_t>(boneIndex) < m_bones.size()
            && ToLower(m_bones[static_cast<std::size_t>(boneIndex)].name).find(lowerFilter) != std::string::npos) {
            anyMatch = true;
        }
    }
    if (!anyMatch) {
        return;
    }

    const char* rootName = (chain.rootBoneIndex >= 0 && static_cast<std::size_t>(chain.rootBoneIndex) < m_bones.size())
        ? m_bones[static_cast<std::size_t>(chain.rootBoneIndex)].name.c_str()
        : "(none)";
    char header[160];
    std::snprintf(header, sizeof(header), "Chain %d - Root: %s (%zu joints)", chainIndex, rootName,
        chain.jointBoneIndices.size());

    ImGui::PushID(chainIndex);
    if (ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        for (const std::int32_t boneIndex : chain.jointBoneIndices) {
            if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()) {
                continue; // Defensive - should never happen for a well-formed DynamicChainDefinition.
            }
            const BoneEntry& bone = m_bones[static_cast<std::size_t>(boneIndex)];
            RenderFlatPartRow(ModelPartKind::Verlet, boneIndex, bone.name, bone.position, lowerFilter, ctx);
        }
        if (chain.hasHeadCollider) {
            ImGui::TextDisabled("Head Collider: r=%.3f", chain.headColliderRadius);
        }
        ImGui::TreePop();
    }
    ImGui::PopID();
}
```

Note this reuses `RenderFlatPartRow()` completely unmodified in its outer
shape — the ONE internal change that function needs (disabling Shift-range-
select for `ModelPartKind::Verlet`, Culprit D) is covered in 3.7 below, and
applies automatically here too since this call site goes through the exact
same function.

### 3.6 `BoneViewerWindow.cpp` — `OverlayPart` gains a `partIndex` field (Culprit C)

```cpp
struct OverlayPart {
    Vec3 position;
    std::string name;
    std::int32_t partIndex = -1; // task_manager/verlet-integration-5 - the
        // REAL ModelPartKind partIndex this overlay slot represents. For
        // Bone/RigidBody/Joint this is always identical to this part's own
        // position in overlayParts (i == partIndex) - kept explicit rather
        // than implicit so Verlet mode (where partIndex is a bone index,
        // NOT the overlay slot position - see PHASE1's own "bone index"
        // decision) can share every downstream hover/click/highlight code
        // path unmodified. See PHASE0_MASTER_STRATEGY.md, Culprit C.
};
```

Update the three EXISTING construction sites (Bone/RigidBody/Joint) to set
this field explicitly (each simply passes its own already-known loop
index):

```cpp
// Bone:
overlayParts.push_back(OverlayPart{ bone.position, bone.name, static_cast<std::int32_t>(overlayParts.size()) });
// RigidBody:
overlayParts.push_back(OverlayPart{ body.translate, body.name.empty() ? ("Part " + std::to_string(i)) : body.name, static_cast<std::int32_t>(i) });
// Joint:
overlayParts.push_back(OverlayPart{ joint.translate, joint.name.empty() ? ("Joint " + std::to_string(i)) : joint.name, static_cast<std::int32_t>(i) });
```

**v2 correctness fix (see `PHASE0_MASTER_STRATEGY.md`'s "Revision Notes (v2)",
finding #1):** the existing code's trailing `else` branch (right after the
`RigidBody` branch above) is today Joint's own construction, written as a
bare, IMPLICIT `else { ... }` — NOT `else if (m_viewMode == ModelPartKind::Joint)`.
Naively appending a 4th `} else { // ModelPartKind::Verlet ... }` branch after
it, as v1 of this document showed below, would silently REPLACE Joint mode's
own overlay-construction body with Verlet's — a real regression (Joint mode
would stop building any overlay parts at all) that directly contradicts this
same phase's own "What We Will NOT Do" promise ("we will not change Bone/
Rigid Body/Joint mode's existing pixel/interaction behavior in any way").
Fix this exactly the same way 3.8 below already correctly handles the
identical shape for the connector-line block: first make the Joint branch an
explicit `else if`, THEN add Verlet's new branch as the trailing `else`:

```cpp
} else if (m_viewMode == ModelPartKind::Joint) {
    overlayParts.reserve(m_joints.size());
    for (std::size_t i = 0; i < m_joints.size(); ++i) {
        const JointEntry& joint = m_joints[i];
        overlayParts.push_back(
            OverlayPart{ joint.translate, joint.name.empty() ? ("Joint " + std::to_string(i)) : joint.name,
                static_cast<std::int32_t>(i) });
    }
} else { // ModelPartKind::Verlet
    if (verletModel != nullptr) {
        for (const DynamicChainDefinition& chain : verletModel->chains) {
            for (const std::int32_t boneIndex : chain.jointBoneIndices) {
                if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()) {
                    continue;
                }
                const BoneEntry& bone = m_bones[static_cast<std::size_t>(boneIndex)];
                overlayParts.push_back(OverlayPart{ bone.position, bone.name, boneIndex });
            }
        }
    }
}
```

Note the Joint body above is copied byte-for-byte from the existing code's
current (implicit-`else`) Joint construction — including its own
`static_cast<std::int32_t>(i)` `partIndex` already shown in the "three
EXISTING construction sites" snippet just above (Joint's own line is
reproduced again here only because the surrounding brace now changes from a
bare `else` to `else if (...)`, not because its body itself changed at all).

Now update EVERY downstream consumer of a raw overlay-slot index that used
to assume identity with `partIndex`, replacing the raw index with
`overlayParts[i].partIndex` (for the drawing-loop `isSelected` check) or
`overlayParts[static_cast<std::size_t>(hoveredPartIndex)].partIndex` (for
the click-handling block) — there are exactly three such sites:

1. The per-part drawing loop's `isSelected` check:
   ```cpp
   // Was: ctx.selection.IsModelPartSelected(m_targetEntity, m_viewMode, static_cast<std::int32_t>(i));
   const bool isSelected = ctx.selection.IsModelPartSelected(m_targetEntity, m_viewMode, overlayParts[i].partIndex);
   ```
2. The direct-viewport-click handler's plain-click/Ctrl-click/Shift-click
   branch (currently reads `hoveredPartIndex` — an `int`, the overlay SLOT —
   directly): every `ctx.selection.SelectModelPart(m_targetEntity, m_viewMode, hoveredPartIndex)` /
   `ToggleModelPartInSelection(m_targetEntity, m_viewMode, hoveredPartIndex)` call becomes
   `.../*same*/(..., overlayParts[static_cast<std::size_t>(hoveredPartIndex)].partIndex)`.
   `m_flatSelectionAnchorIndex = hoveredPartIndex;` becomes
   `m_flatSelectionAnchorIndex = overlayParts[static_cast<std::size_t>(hoveredPartIndex)].partIndex;`
   (the anchor must also store a real `partIndex`, not a raw overlay slot,
   since `RenderFlatPartRow()`'s own Shift-range logic — Rigid Body/Joint
   only, per 3.7 below — reads it back as a `partIndex` too).
3. The Shift-range-select branch's `BuildInclusiveIndexRange(m_flatSelectionAnchorIndex,
   static_cast<std::int32_t>(hoveredPartIndex))` call becomes
   `BuildInclusiveIndexRange(m_flatSelectionAnchorIndex, overlayParts[static_cast<std::size_t>(hoveredPartIndex)].partIndex)`
   — though per 3.7 below, this whole branch is now additionally guarded to
   never execute for Verlet mode at all, so this substitution is only
   reachable for Rigid Body/Joint, where `overlayParts[i].partIndex == i`
   always holds anyway (a no-op in practice for those two modes, but keeps
   the code textually uniform/correct-by-construction rather than
   correct-by-coincidence).

### 3.7 `BoneViewerWindow.cpp` — disable Shift-range-select for Verlet mode (Culprit D)

In `RenderFlatPartRow()`, change the Shift-branch's own guard condition from
an unconditional `if (io.KeyShift)` to:

```cpp
// Verlet mode's partIndex is a skeleton BONE index, not a dense 0..N-1
// flat-list position (see task_manager/verlet-integration-5/
// PHASE0_MASTER_STRATEGY.md, Culprit D) - a raw inclusive integer range
// between two bone indices would silently include bones that are not even
// Verlet joints at all. Shift-click on a Verlet row therefore falls back
// to the exact same toggle-only behavior Bone mode's own tree already uses
// for the identical underlying reason (RenderBoneTreeNode()'s own doc
// comment) - only Rigid Body/Joint (both genuinely dense, 0..count-1
// index spaces) get a real contiguous range select.
const bool supportsRangeSelect = kind != ModelPartKind::Verlet;
if (supportsRangeSelect && io.KeyShift) {
    const std::vector<std::int32_t> range = BuildInclusiveIndexRange(m_flatSelectionAnchorIndex, index);
    ctx.selection.SelectModelParts(m_targetEntity, kind, std::vector<int>(range.begin(), range.end()));
} else if (io.KeyCtrl || io.KeyShift) {
    ctx.selection.ToggleModelPartInSelection(m_targetEntity, kind, index);
    m_flatSelectionAnchorIndex = index;
} else {
    ctx.selection.SelectModelPart(m_targetEntity, kind, index);
    m_flatSelectionAnchorIndex = index;
}
```

Apply the identical `supportsRangeSelect` change to `Build()`'s own direct-
viewport-click handler, which today computes
`const bool supportsRangeSelect = m_viewMode != ModelPartKind::Bone;` —
change to:

```cpp
const bool supportsRangeSelect = m_viewMode != ModelPartKind::Bone && m_viewMode != ModelPartKind::Verlet;
```

### 3.8 `BoneViewerWindow.cpp` — Verlet connector lines, root/anchor marker, head-collider wireframe

Add a 4th branch to the existing `if (m_viewMode == Bone) {...} else if
(m_viewMode == RigidBody) {...} else {...}` connector-line block (which
today has exactly 3 branches, the last one implicitly meaning "Joint" —
make it an explicit `else if (m_viewMode == ModelPartKind::Joint)` first,
then add the new 4th `else if` for Verlet, so the structure stays
symmetrical and no branch is left as a silent fallback):

```cpp
} else if (m_viewMode == ModelPartKind::Verlet) {
    if (verletModel != nullptr) {
        for (const DynamicChainDefinition& chain : verletModel->chains) {
            // Root/anchor marker - drawn even though it is NOT part of
            // overlayParts/selectable (DynamicChainDefinition::rootBoneIndex
            // is never itself simulated - see that struct's own doc
            // comment) - a small, visually distinct, non-interactive square
            // so a user can see exactly where a chain "hangs from."
            Vec3 prevPos;
            bool havePrev = false;
            if (chain.rootBoneIndex >= 0 && static_cast<std::size_t>(chain.rootBoneIndex) < m_bones.size()) {
                prevPos = m_bones[static_cast<std::size_t>(chain.rootBoneIndex)].position;
                havePrev = true;
                ImVec2 rootScreen;
                if (ProjectToScreen(prevPos, viewProj, imageMin, imageMax, rootScreen)) {
                    constexpr float kHalf = 4.0f;
                    drawList->AddRectFilled(ImVec2(rootScreen.x - kHalf, rootScreen.y - kHalf),
                        ImVec2(rootScreen.x + kHalf, rootScreen.y + kHalf), IM_COL32(200, 200, 200, 255));
                }
            }
            // Chain connector lines, root -> joint[0] -> joint[1] -> ...,
            // reprojected directly from m_bones (independent of
            // overlayParts/screenPositions - mirrors how RigidBody mode's
            // own "attached bone" connector line already reprojects
            // m_bones[boneIndex].position directly rather than relying on
            // the Bone-mode overlay array being populated this frame).
            for (const std::int32_t boneIndex : chain.jointBoneIndices) {
                if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()) {
                    continue;
                }
                const Vec3 jointPos = m_bones[static_cast<std::size_t>(boneIndex)].position;
                if (havePrev) {
                    ImVec2 prevScreen, jointScreen;
                    if (ProjectToScreen(prevPos, viewProj, imageMin, imageMax, prevScreen)
                        && ProjectToScreen(jointPos, viewProj, imageMin, imageMax, jointScreen)) {
                        drawList->AddLine(prevScreen, jointScreen, IM_COL32(255, 90, 170, 160), 2.0f);
                    }
                }
                prevPos = jointPos;
                havePrev = true;
            }
            // Optional head-collider wireframe (Sphere shape - reusing
            // RigidBodyWireframe.h's own existing per-shape geometry
            // builder exactly like Rigid Body mode's selected-shape
            // wireframe does) - drawn unconditionally whenever configured,
            // NOT selection-gated (it is a debug aid for the whole chain,
            // not itself a selectable part).
            if (chain.hasHeadCollider && chain.headColliderBoneIndex >= 0
                && static_cast<std::size_t>(chain.headColliderBoneIndex) < m_bones.size()
                && chain.headColliderRadius > 0.0f) {
                const Vec3 colliderCenter = m_bones[static_cast<std::size_t>(chain.headColliderBoneIndex)].position;
                const std::vector<WireframeSegment> wireframe = BuildRigidBodyWireframe(
                    RigidBodyShape::Sphere, Vec3(chain.headColliderRadius, 0.0f, 0.0f), colliderCenter, Vec3::Zero());
                for (const WireframeSegment& segment : wireframe) {
                    ImVec2 screenA, screenB;
                    if (ProjectToScreen(segment.a, viewProj, imageMin, imageMax, screenA)
                        && ProjectToScreen(segment.b, viewProj, imageMin, imageMax, screenB)) {
                        drawList->AddLine(screenA, screenB, IM_COL32(255, 90, 170, 90), 1.25f);
                    }
                }
            }
        }
    }
}
```

Add Verlet's own base dot color to the existing 3-way `dotColor` ternary
(make it a 4-way, same pattern as 3.3's `partCount`/`partNoun`):

```cpp
ImU32 dotColor = m_viewMode == ModelPartKind::Bone ? IM_COL32(90, 230, 130, 255)
    : m_viewMode == ModelPartKind::RigidBody ? IM_COL32(80, 180, 255, 255)
    : m_viewMode == ModelPartKind::Joint      ? IM_COL32(200, 120, 255, 255)
                                              : IM_COL32(255, 90, 170, 255); // Verlet - hot pink/magenta, distinct from every other mode's base color AND from the shared selected-orange/search-yellow/hovered-white overrides.
```

Every other line in that same per-part drawing loop (hover/select color
override, name label, filled-circle radius) is already generic across
modes and needs **no** further change.

### 3.9 `src/Editor/ImGuiEditorLayer.cpp` — wiring

Update the one call site:

```cpp
// Was: m_boneViewer.Build(registry, renderer, m_ctx, m_modelRigCache);
m_boneViewer.Build(registry, renderer, m_ctx, m_modelRigCache, game.GetPhysicsSystem());
```

`game` is already in scope at this call site (confirmed directly — the
immediately-preceding `BuildInspectorPanel(...)` call on the line above
already reads `game.GetPhysicsSystem()`). No new member/include is needed
on `ImGuiEditorLayer` itself — `PhysicsSystem` only needs to be visible
where `Game.h` is already included (it is, transitively, since `Game&` is
already a parameter of this whole function).

## Step 4: What We Will NOT Do

- We will **not** attempt to make the root/anchor marker independently
  selectable — see Phase 1's own scope decision; a user who wants to
  inspect that same bone can already do so by switching to Bone mode.
- We will **not** persist `verletModel`/any Verlet-mode data as a new
  member field on `BoneViewerWindow` — every read goes through the one
  `TryGet()` call per `Build()` frame described in 3.3, by design (Culprit
  E).
- We will **not** change Bone/Rigid Body/Joint mode's existing pixel/
  interaction behavior in any way — every one of this phase's diffs to
  shared code (`OverlayPart`, `RenderFlatPartRow()`, the click-handling
  block) is either purely additive (a new field/branch) or is proven, by
  the `partIndex == i` identity noted in 3.6, to be a no-op for the three
  pre-existing modes.
- We will **not** yet make the Inspector show anything for a selected
  Verlet joint — that is Phase 3's job. It is expected and acceptable that,
  immediately after this phase alone, selecting a Verlet particle shows
  whatever `InspectorPanel.cpp`'s `BuildModelPartInspector()` currently does
  for an unrecognized `ModelPartKind` (see Phase 1's Culprit B note — this
  phase does not touch `InspectorPanel.cpp` at all).

## Step 5: Their Role

1. Edit `src/Editor/BoneViewerWindow.h` per 3.1 (forward declaration, new
   `Build()`/`BuildPartListPane()` signatures — note the 3.4 correction that
   changes `BuildPartListPane()`'s new parameter to
   `const DynamicChainRigCache::ModelEntry*` rather than `PhysicsSystem&` —
   and the new `RenderVerletChainNode()` declaration; add the
   `DynamicChainDefinition.h` include).
2. Edit `src/Editor/BoneViewerWindow.cpp` per 3.2–3.8, in this order: new
   includes; toolbar (3.3); `BuildPartListPane()`'s new case + the
   `verletModel` fetch moved into `Build()` (3.3/3.4); `RenderVerletChainNode()`
   (3.5); `OverlayPart::partIndex` plus every downstream consumer site
   (3.6); the Shift-range-select guard in BOTH `RenderFlatPartRow()` and
   `Build()`'s direct-click handler (3.7); the connector-line/root-marker/
   head-collider-wireframe branch plus the 4th `dotColor` (3.8).
3. Edit `src/Editor/ImGuiEditorLayer.cpp` per 3.9 (one-line call-site
   update).
4. Build `GreatTamanaEngine` (the real executable) and manually verify,
   against a real imported MMD model that has PMX physics-driven ("jiggle")
   bones (a model with hair/skirt/tail secondary motion is ideal):
   - Bone/Rigid Body/Joint modes look and behave EXACTLY as before this
     phase (the `OverlayPart::partIndex`/Shift-range-select changes are
     genuine no-ops for them — confirm this explicitly, e.g. by Shift-
     clicking two Rigid Bodies and confirming the same contiguous range
     selects as before).
   - Switching to "Verlet" immediately shows the chain-grouped tree pane
     and the pink/magenta particle dots + amber-connector chain lines +
     gray root-anchor squares in the viewport, with no extra click/refresh
     needed.
   - Clicking a joint row highlights the matching viewport dot and vice
     versa (both derived from the same `ctx.selection.IsModelPartSelected()`
     call, now correctly keyed by bone index for this mode).
   - Editing a joint's damping/stiffness/mass/head-collider via the
     Inspector's PRE-EXISTING "Dynamic Chain Physics" section (unchanged by
     this phase) and reopening/re-viewing the Bone Viewer's Verlet mode
     shows the SAME chain topology (this phase reads the identical
     `PhysicsSystem` state that section writes — there is only one source
     of truth).
   - A model with zero detected chains shows "no dynamic bone chains" in
     the tree pane and the matching toolbar warning text, never a crash/
     empty-but-silently-broken view.
   - Ctrl-clicking multiple Verlet joints (tree rows or viewport dots) adds
     each to the selection (toggle), while Shift-clicking behaves like
     Ctrl-click (falls back to toggle, per 3.7), never a spurious "also
     selected every bone in between" result.
