# PHASE3 — Inspector: Focused "Verlet Joint" Model-Part Section (`src/Editor/Panels/InspectorPanel.h/.cpp`)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit F). Depends on: Phase 1
(`ModelPartKind::Verlet`, `FindDynamicChainJointByBoneIndex()`/
`DynamicChainJointLocation` compiling and tested) and Phase 2
(`BoneViewerWindow` can actually select a Verlet joint, giving this phase
something real to select before it renders anything). Produces: a new,
focused, single-joint Inspector section — selecting one Verlet particle
(tree row or viewport dot, from Phase 2) shows and **live-edits** that
exact joint's damping/stiffness/mass/rest-length/head-collider fields,
reusing the exact same `DynamicChainRigCache::TryGetMutable()` contract the
existing, more generic "Dynamic Chain Physics" collapsible section already
uses.

## Step 1: The Goal

1. Give `BuildModelPartInspector()` (`InspectorPanel.cpp`) a 4th `case
   ModelPartKind::Verlet:` branch, symmetrical in spirit with its existing
   Bone/RigidBody/Joint cases — a colored section header, then either a
   full property sheet or a graceful "index out of range"/"not a Verlet
   joint" message.
2. Make that branch resolve the selected bone index back to
   `(chainIndex, jointIndexInChain)` via Phase 1's
   `FindDynamicChainJointByBoneIndex()`, then show/edit that EXACT joint's
   `DynamicJointSettings` (damping/stiffness/mass) plus read-only context
   (which chain, which bone, its rest length to the previous joint, the
   chain's root bone, and its head-collider settings if any) — all writing
   directly into `PhysicsSystem`'s own live `DynamicChainRigCache`, the same
   object the pre-existing "Dynamic Chain Physics" section already edits.
3. Thread `PhysicsSystem&` into `BuildModelPartInspector()`'s own signature
   and its one call site (today it only receives `Registry&`,
   `EditorContext&`, `ModelRigCache&` — none of which can reach live
   simulation-tunable state).

## Step 2: The Situation / The Problem

See `PHASE0_MASTER_STRATEGY.md`'s Culprit F. `InspectorPanel.cpp` has TWO
independent code paths that both, today, only know about Bone/RigidBody/
Joint:

- `BuildEntityInspector()`'s "Dynamic Chain Physics" collapsible section —
  gated on the SELECTED ENTITY carrying a `DynamicChainRig` component,
  entirely independent of `Selection`'s `ModelPart` concept, showing EVERY
  chain/joint of that entity's model all at once. This section is NOT
  touched by this phase — it stays exactly as `verlet-integration-1`'s
  Phase 4 left it, still fully functional, still the "see everything at
  once" view.
- `BuildModelPartInspector()` — gated on `ctx.selection.Kind() ==
  InspectorSelectionKind::ModelPart`, showing a FOCUSED, single-object
  property sheet for whichever ONE bone/rigid body/joint is currently
  selected (from the Bone Viewer). This function's signature today is
  `(Registry& registry, EditorContext& ctx, ModelRigCache& rigCache)` — it
  has no way to reach `PhysicsSystem` at all, and its `switch
  (ctx.selection.SelectedModelPartKind())` has exactly 3 cases, both facts
  needing to change for a 4th, EDITABLE (not merely read-only, unlike its
  existing 3 cases — see that function's own doc comment, "there is no
  physics simulation... consumes RigidBody/Joint data yet... nothing for an
  edit here to actually drive," which is no longer true for Verlet, since a
  real simulation DOES consume `DynamicJointSettings`) case to work.

## Step 3: The Plan

### 3.1 `InspectorPanel.h` — signature change

```cpp
#if GTE_ENABLE_PROJECT_PANEL
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, Renderer& renderer, AssetPreviewTexture& assetPreview,
    AssetPreviewMesh& assetPreviewMesh, BoneViewerWindow& boneViewer, PhysicsSystem& physicsSystem, ModelRigCache& rigCache);
#else
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, PhysicsSystem& physicsSystem);
#endif
```

This top-level signature is **already exactly this shape** — `physicsSystem`
is already a parameter in both build configurations (added by
`verlet-integration-1`'s Phase 4 for `BuildEntityInspector()`'s own "Dynamic
Chain Physics" section). **No change needed here at all** — this step exists
only to confirm/document that fact so the implementer does not waste time
looking for a change that isn't required at this outermost level; the
actual signature that needs to change is `BuildModelPartInspector()`'s own,
internal, `.cpp`-only signature (3.2 below), which is not declared in the
header at all (it is a file-local, anonymous-namespace-adjacent free
function in `InspectorPanel.cpp`, only reachable from within that same
file).

### 3.2 `InspectorPanel.cpp` — `BuildModelPartInspector()`'s new signature

```cpp
// Was: void BuildModelPartInspector(Registry& registry, EditorContext& ctx, ModelRigCache& rigCache)
void BuildModelPartInspector(Registry& registry, EditorContext& ctx, ModelRigCache& rigCache, PhysicsSystem& physicsSystem)
```

Update its doc comment's own "every field is read-only... there is no
physics simulation anywhere in the engine that consumes RigidBody/Joint
data yet" sentence — that premise is now only true for the Bone/RigidBody/
Joint cases; the new Verlet case is genuinely editable, driving a REAL,
already-running simulation (`PhysicsSystem::Update()`, next fixed-timestep
tick).

Update the ONE call site (found inside `BuildInspectorPanel()`, currently
`BuildModelPartInspector(registry, ctx, rigCache);`) to
`BuildModelPartInspector(registry, ctx, rigCache, physicsSystem);` —
`physicsSystem` is already a parameter of the enclosing `BuildInspectorPanel()`
in both build configurations (3.1), so this is a same-function, no-extra-
plumbing change, exactly like Phase 2's own `Build()` → `game.GetPhysicsSystem()`
threading.

### 3.3 `InspectorPanel.cpp` — the new `case ModelPartKind::Verlet:` branch

Add `#include "../../Physics/DynamicChainDefinition.h"` (for
`FindDynamicChainJointByBoneIndex()`/`DynamicChainJointLocation`/
`DynamicJointSettings`) alongside the file's existing
`#include "../../Physics/DynamicChainDefinition.h"` line — **already
present** (confirmed: `InspectorPanel.cpp`'s own top-of-file includes
already list this exact header, added by `verlet-integration-1`'s Phase 4
for the "Dynamic Chain Physics" section). No new include needed for the
struct/settings themselves — only Phase 1's newly-added
`FindDynamicChainJointByBoneIndex()`/`DynamicChainJointLocation` need to
already be declared in that same, already-included header (which Phase 1
guarantees).

Add the new switch case, inside `BuildModelPartInspector()`'s existing
`switch (ctx.selection.SelectedModelPartKind())` (right after the existing
`case ModelPartKind::Joint: { ... break; }`):

```cpp
case ModelPartKind::Verlet: {
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Verlet Joint");

    // `index` (this function's own existing local, set earlier from
    // ctx.selection.SelectedModelPartIndex()) is a SKELETON BONE INDEX for
    // this ModelPartKind - see task_manager/verlet-integration-5/
    // PHASE1_CHAIN_LOOKUP_AND_SELECTION_FOUNDATION.md's own "bone index,
    // not a flattened counter" decision - NOT a direct index into any
    // chains/jointBoneIndices array itself.
    if (index < 0 || static_cast<std::size_t>(index) >= rig->skeleton.bones.size()) {
        ImGui::TextDisabled("Bone index %d is out of range (the model may have changed).", index);
        break;
    }
    const Bone& bone = rig->skeleton.bones[static_cast<std::size_t>(index)];
    ImGui::Text("Bone: %s (index %d)", bone.name.empty() ? "(unnamed)" : bone.name.c_str(), index);

    DynamicChainRigCache::ModelEntry* model = physicsSystem.GetDynamicChainRigCache().TryGetMutable(source->gtaPath);
    if (model == nullptr) {
        ImGui::TextDisabled("No dynamic-chain physics data registered for this model.");
        break;
    }
    const DynamicChainJointLocation location = FindDynamicChainJointByBoneIndex(model->chains, index);
    if (!location.IsValid()) {
        ImGui::TextDisabled(
            "This bone is not a physics-simulated joint in any detected chain (it may be a chain's own root/anchor bone, or unrelated).");
        break;
    }

    DynamicChainDefinition& chain = model->chains[static_cast<std::size_t>(location.chainIndex)];
    const std::size_t jointIndex = static_cast<std::size_t>(location.jointIndexInChain);
    ImGui::Text("Chain: %d (%zu joints)", location.chainIndex, chain.jointBoneIndices.size());
    ImGui::Text("Position In Chain: %d of %zu", location.jointIndexInChain + 1, chain.jointBoneIndices.size());

    const char* rootName = (chain.rootBoneIndex >= 0 && static_cast<std::size_t>(chain.rootBoneIndex) < rig->skeleton.bones.size())
        ? rig->skeleton.bones[static_cast<std::size_t>(chain.rootBoneIndex)].name.c_str()
        : "(none)";
    ImGui::Text("Chain Root (Pinned Anchor): %s (index %d)", rootName, chain.rootBoneIndex);

    if (jointIndex < chain.restLengths.size()) {
        ImGui::BeginDisabled();
        float restLength = chain.restLengths[jointIndex];
        ImGui::DragFloat("Rest Length (to previous joint)", &restLength);
        ImGui::EndDisabled();
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Simulation Parameters (live - edits apply next physics step)");
    // v2 robustness fix (see PHASE0_MASTER_STRATEGY.md's "Revision Notes
    // (v2)", finding #3): jointSettings is documented as always
    // index-aligned 1:1 with jointBoneIndices (DynamicChainDefinition.h's
    // own doc comment), so this bounds check can never actually fail in
    // practice - but the neighboring restLengths read two lines above this
    // one already defensively checks its own bounds before indexing, and
    // this whole file's every other single-part case (Bone/RigidBody/Joint,
    // just above this one) treats "should never happen per an invariant
    // elsewhere" as still worth guarding rather than an unchecked index -
    // keep this branch consistent with that same convention instead of the
    // one array access in this whole switch that silently assumed otherwise.
    if (jointIndex >= chain.jointSettings.size()) {
        ImGui::TextDisabled(
            "Joint settings index %zu is out of range for this chain (%zu entries) - the chain data may be malformed.",
            jointIndex, chain.jointSettings.size());
        break;
    }
    DynamicJointSettings& settings = chain.jointSettings[jointIndex];
    ImGui::DragFloat("Damping", &settings.damping, 0.005f, 0.0f, 1.0f);
    ImGui::DragFloat("Stiffness", &settings.stiffness, 0.005f, 0.0f, 1.0f);
    ImGui::DragFloat("Weight (Mass)", &settings.mass, 0.01f, 0.01f, 100.0f);

    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Chain-Wide Settings");
    ImGui::DragFloat("Gravity Scale", &chain.gravityScale, 0.01f, 0.0f, 10.0f);
    ImGui::DragFloat("Wind Scale", &chain.windScale, 0.01f, 0.0f, 10.0f);
    ImGui::Checkbox("Head Collider", &chain.hasHeadCollider);
    if (chain.hasHeadCollider) {
        ImGui::DragInt("Collider Bone Index", &chain.headColliderBoneIndex, 1.0f, 0,
            static_cast<int>(rig->skeleton.bones.size()) - 1);
        ImGui::DragFloat("Collider Radius", &chain.headColliderRadius, 0.01f, 0.0f, 10.0f);
    }
    break;
}
```

Note this branch deliberately reuses `rig` (this function's own existing
`const RigFileData*` local, already resolved from `ModelRigCache` a few
lines above every existing case) for read-only bone-name lookups (bone
name, root name, out-of-range bone count), and `physicsSystem`'s own
`DynamicChainRigCache` (via `TryGetMutable()`, exactly like the pre-existing
"Dynamic Chain Physics" section already does) for the actually-editable
`DynamicChainDefinition`/`DynamicJointSettings` fields — the two data
sources coexist exactly the same way they already do in the OTHER existing
section, never duplicated/reconciled here, just read from their own
respective already-established owners.

`source->gtaPath` here refers to this function's own existing `const
MeshAssetSource* source` local (already resolved near the top of
`BuildModelPartInspector()`, reused unchanged by every existing case) — no
new lookup needed for the path string itself.

### 3.4 `InspectorPanel.cpp` — `Vec3`/label mismatch guard

`DynamicJointSettings::mass`'s slider range (`0.01f` minimum) matches
`chain.jointSettings[...].mass`'s own documented "must be > 0" contract
(`DynamicChainDefinition.h`'s own doc comment on `DynamicJointSettings::
mass`) exactly — do not widen this to allow `0.0f`, which would silently
feed a zero mass into `VerletParticle::inverseMass`'s own divide-by-mass
seeding logic elsewhere (out of this phase's scope to touch, but this
Inspector slider is a direct write path into that same value, so its own
minimum bound must stay a strictly-positive guard).

## Step 4: What We Will NOT Do

- We will **not** remove or restructure the existing "Dynamic Chain
  Physics" collapsible section in `BuildEntityInspector()` — it remains the
  "see every chain/joint of this model at once" view; this phase's new
  section is purely an additional, complementary, single-joint FOCUSED
  view, reached a different way (clicking a specific gizmo particle/tree
  row rather than expanding a generic per-chain tree by number).
- We will **not** show a LIVE, currently-simulating particle position in
  this section — consistent with Phase 2's own static/bind-pose scope, this
  Inspector section shows/edits PARAMETERS (damping/stiffness/mass/
  gravity-scale/wind-scale/head-collider), never a live runtime position/
  velocity readout.
- We will **not** add per-instance (per-entity) override editing here
  either — exactly like the pre-existing "Dynamic Chain Physics" section,
  every edit in this new section writes into the model-PATH-keyed
  `DynamicChainRigCache::ModelEntry`, shared by every entity spawned from
  that same `*.gta` (see `PHASE0_MASTER_STRATEGY.md`'s own "What We Will
  NOT Do").

## Step 5: Their Role

1. Confirm `InspectorPanel.h`'s outer `BuildInspectorPanel()` signature
   already carries `physicsSystem` in both build configurations (3.1 — no
   edit expected, verify only).
2. Edit `BuildModelPartInspector()`'s own signature (3.2) and its one call
   site inside `BuildInspectorPanel()`.
3. Add the `case ModelPartKind::Verlet:` branch (3.3) with the exact fields
   listed, double-checking the `mass` slider's minimum bound (3.4).
4. Build `GreatTamanaEngine` and manually verify, against the same real
   MMD model used in Phase 2's own verification:
   - Selecting a Verlet joint (tree row or viewport dot, from Phase 2) shows
     this new section with the correct bone name/index, chain index,
     position-in-chain, root/anchor bone name, and rest length.
   - Dragging the Damping/Stiffness/Weight sliders here and then reopening
     the SAME entity's generic "Dynamic Chain Physics" collapsible section
     shows the identical, now-changed value for that exact joint — proof
     both sections read/write the same single source of truth.
   - Toggling "Head Collider" here and setting a radius makes Phase 2's own
     viewport wireframe sphere appear/update immediately (same
     `PhysicsSystem` state, read fresh every `Build()` frame per Phase 2's
     own design).
   - Selecting a BONE that is NOT a Verlet joint (an ordinary skinned bone,
     or a chain's own root bone) while `m_viewMode == ModelPartKind::Verlet`
     — reachable only via a stale/edge-case selection, e.g. switching modes
     right after selecting something — shows the graceful "not a physics-
     simulated joint" message, never a crash/out-of-bounds read.
   - Multi-selecting several Verlet joints (Ctrl-click, per Phase 2) still
     falls into the EXISTING multi-selection summary branch above this
     switch — note its `kindNoun`/inner-switch label will currently
     mislabel a Verlet multi-selection as "Joints" (Culprit B, not yet
     fixed) — confirm this is the CURRENT, EXPECTED, temporary state after
     this phase alone, closed next by Phase 4, not a regression to chase
     down here.
