# PHASE4 — Inspector: Model-Part Info Section (`src/Editor/Panels/InspectorPanel.h/.cpp`) (v2)

Parent: `PHASE0_MASTER_STRATEGY.md` (Culprit D/E). Depends on: Phase 1
(`Selection::SelectedModelPartEntity()`/`SelectedModelPartKind()`/
`SelectedModelPartIndex()`) and Phase 2 (`ModelRigCache::GetOrLoad()`), both
already compiling/tested; and Phase 3 having already added the
`ImGuiEditorLayer`-owned `ModelRigCache m_modelRigCache` member (reused here,
not re-created). Produces: the last missing link — a dedicated, read-only
"Model Part" section in the Inspector, shown whenever
`ctx.selection.Kind() == InspectorSelectionKind::ModelPart`, closing the loop
the story's requirement 4 asks for end-to-end.

**v2 note:** two additions on top of v1, both from
`PHASE0_MASTER_STRATEGY.md`'s own "Revision Notes (v2)": (1) the Bone case
now also shows the IK chain (when `bone.isIk`) plus the `visible`/
`controllable` flags, and the RigidBody case now also shows the collision-
filtering fields (`group`/`collisionGroupMask`) — data that was already
sitting right there in `RigFileData` for free (finding #3); (2) a small
"Select Owning Entity" button is added at the top of the section, so a user
who wants the "Open Bone Viewer" button back (only reachable from the Entity
Inspector view) isn't forced to leave the Bone Viewer and re-pick the same
entity in Hierarchy (finding #4). Everything else is unchanged from v1.

## Step 1: The Goal

Whenever the user has selected a bone, rigid body, or joint inside the Bone
Viewer (Phase 3), the Inspector panel — the SAME "Inspector" panel that
already shows Transform/MeshRenderer/Camera/etc. for an Entity selection, and
file metadata for an Asset selection — must show that part's own full,
read-only details instead: for a Bone, its name/index/parent/position/
deform-depth/flags (**(v2)** including `visible`/`controllable`, plus its
full IK chain when `isIk`); for a Rigid Body, its name/index/attached-bone/
shape/size/transform/mass/damping/restitution/friction/motion-type
(**(v2)** plus its collision group/mask); for a Joint, its
name/index/type/connected-rigid-bodies/transform/limits/spring factors.

## Step 2: The Situation / The Problem

`Panels/InspectorPanel.cpp`'s `BuildInspectorPanel()` today branches only:

```cpp
if (ctx.selection.Kind() == InspectorSelectionKind::Asset && !...empty()) {
    BuildAssetInspector(...);
    return;
}
BuildEntityInspector(registry, ctx, boneViewer, physicsSystem); // unconditional fallback
```

A `ModelPart` selection (Phase 1/3) falls straight through to
`BuildEntityInspector()`, which reads `ctx.selection.SelectedEntity()` — an
entirely unrelated field (whatever Hierarchy last picked, possibly
`kInvalidEntity`) — and shows either the wrong thing or "No entity selected."
See `PHASE0_MASTER_STRATEGY.md`, Culprit D, and
`PHASE3_BONE_VIEWER_VIEW_MODE_DROPDOWN_AND_GIZMO.md`'s own Step 4 note on
this being an expected, temporary gap closed by this phase.

## Step 3: The Plan

### 3.1 `InspectorPanel.h` — signature change

`BuildInspectorPanel()`'s `GTE_ENABLE_PROJECT_PANEL`-ON signature gains a
`ModelRigCache&` parameter (the SAME shared instance Phase 3 added to
`ImGuiEditorLayer` as `m_modelRigCache` — not a second instance):

```cpp
class ModelRigCache; // forward decl, alongside the existing BoneViewerWindow etc.

#if GTE_ENABLE_PROJECT_PANEL
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, Renderer& renderer, AssetPreviewTexture& assetPreview,
    AssetPreviewMesh& assetPreviewMesh, BoneViewerWindow& boneViewer, PhysicsSystem& physicsSystem, ModelRigCache& rigCache);
#else
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, PhysicsSystem& physicsSystem);
#endif
```

(The non-`GTE_ENABLE_PROJECT_PANEL` signature is left UNCHANGED — a
`ModelPart` selection can only ever be produced by `BoneViewerWindow`
clicking, which is itself only ever compiled under
`GTE_ENABLE_PROJECT_PANEL` (Phase 3, unchanged gating) — so the whole
"Model Part" section this phase adds is naturally, structurally
unreachable when that switch is OFF, and must be `#if`-guarded the same
way, never touching the `#else` branch at all.)

### 3.2 `InspectorPanel.cpp` — new includes

```cpp
#include "../ModelRigCache.h" // only reachable inside the existing #if GTE_ENABLE_PROJECT_PANEL block
#include "../../Assets/PhysicsData.h" // RigidBody, Joint, RigidBodyShape, RigidBodyMotionType, JointType
```

(`Assets/SkeletonData.h`'s `Bone` type is already reachable transitively via
`RigFile.h`, itself reachable via `ModelRigCache.h`.)

### 3.3 `InspectorPanel.cpp` — new section function

Add a new free function, `BuildModelPartInspector()`, inside the existing
anonymous namespace, right before `BuildEntityInspector()` (grouped with the
other `#if GTE_ENABLE_PROJECT_PANEL`-only section builders):

```cpp
#if GTE_ENABLE_PROJECT_PANEL

// Human-readable label helpers - same "always produce something
// displayable" convention as AssetTypeLabel()/AssetFlagsLabel() further
// down this file.
const char* RigidBodyShapeLabel(RigidBodyShape shape)
{
    switch (shape) {
    case RigidBodyShape::Sphere: return "Sphere";
    case RigidBodyShape::Box: return "Box";
    case RigidBodyShape::Capsule: return "Capsule";
    default: return "Unknown";
    }
}

const char* RigidBodyMotionTypeLabel(RigidBodyMotionType type)
{
    switch (type) {
    case RigidBodyMotionType::Static: return "Static (follows bone)";
    case RigidBodyMotionType::Dynamic: return "Dynamic (physics-driven)";
    case RigidBodyMotionType::DynamicAndBoneMerge: return "Dynamic + Bone Merge";
    default: return "Unknown";
    }
}

const char* JointTypeLabel(JointType type)
{
    switch (type) {
    case JointType::SpringDof6: return "Spring 6-DOF";
    case JointType::Dof6: return "6-DOF";
    case JointType::P2P: return "Point-to-Point";
    case JointType::ConeTwist: return "Cone Twist";
    case JointType::Slider: return "Slider";
    case JointType::Hinge: return "Hinge";
    default: return "Unknown";
    }
}

// Shown whenever ctx.selection.Kind() == ModelPart (see
// PHASE1_SELECTION_MODEL_PART_FOUNDATION.md) - the sub-part-of-a-model
// equivalent of BuildEntityInspector()/BuildAssetInspector() below. Every
// field is read-only (ImGui::BeginDisabled()) - there is no physics
// simulation anywhere in the engine that consumes RigidBody/Joint data yet
// (see Assets/PhysicsData.h's own file comment), so there is nothing for an
// edit here to actually drive - same scope limit as this file's existing
// "Mesh Renderer"/"Global Physics Settings" read-only sections.
void BuildModelPartInspector(Registry& registry, EditorContext& ctx, ModelRigCache& rigCache)
{
    const Entity owner = ctx.selection.SelectedModelPartEntity();
    if (!registry.IsAlive(owner)) {
        ImGui::TextDisabled("The selected model part's owning entity no longer exists.");
        return;
    }

    // (v2) Lets the user get back to the Entity Inspector view (and its
    // "Open Bone Viewer" button, only reachable from that view - see
    // InspectorPanel.h's own class comment) without leaving the Bone Viewer
    // and re-picking the same entity in Hierarchy - see
    // PHASE0_MASTER_STRATEGY.md's "Revision Notes (v2)", finding #4. Calling
    // SelectEntity() flips ctx.selection.Kind() to Entity immediately, so
    // NEXT frame's BuildInspectorPanel() call takes the BuildEntityInspector()
    // branch instead of this one - this frame still finishes rendering the
    // Model Part section below unchanged (harmless - Kind() only matters at
    // the top of BuildInspectorPanel(), already past by the time this runs).
    if (ImGui::Button("Select Owning Entity")) {
        ctx.selection.SelectEntity(owner);
    }
    ImGui::Separator();

    const MeshAssetSource* source = registry.TryGetComponent<MeshAssetSource>(owner);
    if (source == nullptr || source->gtaPath.empty()) {
        ImGui::TextDisabled("The selected model part's owning entity has no associated mesh asset.");
        return;
    }

    const RigFileData* rig = rigCache.GetOrLoad(source->gtaPath);
    if (rig == nullptr) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to load rig data for:");
        ImGui::TextWrapped("%s", source->gtaPath.c_str());
        return;
    }

    const int index = ctx.selection.SelectedModelPartIndex();

    switch (ctx.selection.SelectedModelPartKind()) {
    case ModelPartKind::Bone: {
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Bone");
        if (index < 0 || static_cast<std::size_t>(index) >= rig->skeleton.bones.size()) {
            ImGui::TextDisabled("Bone index %d is out of range (the model may have changed).", index);
            break;
        }
        const Bone& bone = rig->skeleton.bones[static_cast<std::size_t>(index)];
        ImGui::Text("Name: %s", bone.name.empty() ? "(unnamed)" : bone.name.c_str());
        if (!bone.englishName.empty()) {
            ImGui::Text("English Name: %s", bone.englishName.c_str());
        }
        ImGui::Text("Index: %d", index);
        ImGui::Text("Parent Bone Index: %d", bone.parentBoneIndex);
        ImGui::BeginDisabled();
        ImGui::DragFloat3("Position", const_cast<float*>(&bone.position.x));
        ImGui::EndDisabled();
        ImGui::Text("Deform Depth: %d", bone.deformDepth);
        ImGui::BeginDisabled();
        ImGui::Checkbox("Rotatable", const_cast<bool*>(&bone.rotatable));
        ImGui::SameLine();
        ImGui::Checkbox("Translatable", const_cast<bool*>(&bone.translatable));
        ImGui::Checkbox("IK", const_cast<bool*>(&bone.isIk));
        ImGui::SameLine();
        ImGui::Checkbox("Deform After Physics", const_cast<bool*>(&bone.deformAfterPhysics));
        // (v2) Visible/Controllable were already decoded into Bone but never
        // surfaced here - see PHASE0_MASTER_STRATEGY.md's "Revision Notes
        // (v2)", finding #3.
        ImGui::Checkbox("Visible", const_cast<bool*>(&bone.visible));
        ImGui::SameLine();
        ImGui::Checkbox("Controllable", const_cast<bool*>(&bone.controllable));
        ImGui::EndDisabled();

        // (v2) IK chain - only meaningful when isIk is true (see
        // Assets/SkeletonData.h's own Bone::isIk doc comment). Squarely
        // in-scope for this window: BoneViewerWindow.h's own class comment
        // names diagnosing an IK/animation bone mismatch as this whole
        // window's reason to exist, and PHASE0_MASTER_STRATEGY.md's
        // "Revision Notes (v2)", finding #3, calls this out specifically.
        if (bone.isIk) {
            ImGui::Separator();
            ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "IK Chain");
            if (bone.ikTargetBoneIndex >= 0
                && static_cast<std::size_t>(bone.ikTargetBoneIndex) < rig->skeleton.bones.size()) {
                const Bone& target = rig->skeleton.bones[static_cast<std::size_t>(bone.ikTargetBoneIndex)];
                ImGui::Text("Target Bone: %s (index %d)", target.name.c_str(), bone.ikTargetBoneIndex);
            } else {
                ImGui::TextDisabled("Target Bone: (none)");
            }
            ImGui::Text("Iteration Count: %d", bone.ikIterationCount);
            ImGui::BeginDisabled();
            ImGui::DragFloat("Angle Limit (radians)", const_cast<float*>(&bone.ikAngleLimitRadians));
            ImGui::EndDisabled();
            ImGui::Text("Links (%zu):", bone.ikLinks.size());
            for (std::size_t linkIndex = 0; linkIndex < bone.ikLinks.size(); ++linkIndex) {
                const Bone::IkLink& link = bone.ikLinks[linkIndex];
                std::string linkLabel = "(invalid)";
                if (link.boneIndex >= 0 && static_cast<std::size_t>(link.boneIndex) < rig->skeleton.bones.size()) {
                    linkLabel = rig->skeleton.bones[static_cast<std::size_t>(link.boneIndex)].name;
                }
                ImGui::BulletText("[%zu] %s (index %d)%s", linkIndex, linkLabel.c_str(), link.boneIndex,
                    link.hasAngleLimit ? " - angle limited" : "");
            }
        }
        break;
    }
    case ModelPartKind::RigidBody: {
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Rigid Body");
        if (index < 0 || static_cast<std::size_t>(index) >= rig->physics.rigidBodies.size()) {
            ImGui::TextDisabled("Rigid body index %d is out of range (the model may have changed).", index);
            break;
        }
        const RigidBody& body = rig->physics.rigidBodies[static_cast<std::size_t>(index)];
        ImGui::Text("Name: %s", body.name.empty() ? "(unnamed)" : body.name.c_str());
        ImGui::Text("Index: %d", index);
        if (body.boneIndex >= 0 && static_cast<std::size_t>(body.boneIndex) < rig->skeleton.bones.size()) {
            ImGui::Text("Attached Bone: %s (index %d)", rig->skeleton.bones[static_cast<std::size_t>(body.boneIndex)].name.c_str(), body.boneIndex);
        } else {
            ImGui::TextDisabled("Attached Bone: (none)");
        }
        ImGui::Text("Shape: %s", RigidBodyShapeLabel(body.shape));
        ImGui::Text("Motion Type: %s", RigidBodyMotionTypeLabel(body.motionType));
        // (v2) Collision filtering fields were already decoded but never
        // surfaced here - see PHASE0_MASTER_STRATEGY.md's "Revision Notes
        // (v2)", finding #3. Matches Bullet's own group/mask convention
        // exactly (PhysicsData.h's own RigidBody::group/collisionGroupMask
        // doc comment) - shown as plain unsigned/hex text, not editable
        // widgets, since neither is a float/bool DragFloat*/Checkbox can
        // represent directly.
        ImGui::Text("Collision Group: %u", static_cast<unsigned>(body.group));
        ImGui::Text("Collision Mask: 0x%04X", static_cast<unsigned>(body.collisionGroupMask));
        ImGui::BeginDisabled();
        ImGui::DragFloat3("Shape Size", const_cast<float*>(&body.shapeSize.x));
        ImGui::DragFloat3("Translate", const_cast<float*>(&body.translate.x));
        ImGui::DragFloat3("Rotate (radians)", const_cast<float*>(&body.rotateRadians.x));
        ImGui::DragFloat("Mass", const_cast<float*>(&body.mass));
        ImGui::DragFloat("Linear Damping", const_cast<float*>(&body.linearDamping));
        ImGui::DragFloat("Angular Damping", const_cast<float*>(&body.angularDamping));
        ImGui::DragFloat("Restitution", const_cast<float*>(&body.restitution));
        ImGui::DragFloat("Friction", const_cast<float*>(&body.friction));
        ImGui::EndDisabled();
        break;
    }
    case ModelPartKind::Joint: {
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Joint");
        if (index < 0 || static_cast<std::size_t>(index) >= rig->physics.joints.size()) {
            ImGui::TextDisabled("Joint index %d is out of range (the model may have changed).", index);
            break;
        }
        const Joint& joint = rig->physics.joints[static_cast<std::size_t>(index)];
        ImGui::Text("Name: %s", joint.name.empty() ? "(unnamed)" : joint.name.c_str());
        ImGui::Text("Index: %d", index);
        ImGui::Text("Type: %s", JointTypeLabel(joint.type));

        auto rigidBodyLabel = [&](std::int32_t rigidBodyIndex) -> std::string {
            if (rigidBodyIndex < 0 || static_cast<std::size_t>(rigidBodyIndex) >= rig->physics.rigidBodies.size()) {
                return "(none)";
            }
            const RigidBody& rb = rig->physics.rigidBodies[static_cast<std::size_t>(rigidBodyIndex)];
            return (rb.name.empty() ? std::string("(unnamed)") : rb.name) + " (index " + std::to_string(rigidBodyIndex) + ")";
        };
        ImGui::Text("Rigid Body A: %s", rigidBodyLabel(joint.rigidBodyAIndex).c_str());
        ImGui::Text("Rigid Body B: %s", rigidBodyLabel(joint.rigidBodyBIndex).c_str());

        ImGui::BeginDisabled();
        ImGui::DragFloat3("Translate", const_cast<float*>(&joint.translate.x));
        ImGui::DragFloat3("Rotate (radians)", const_cast<float*>(&joint.rotateRadians.x));
        ImGui::DragFloat3("Translate Lower Limit", const_cast<float*>(&joint.translateLowerLimit.x));
        ImGui::DragFloat3("Translate Upper Limit", const_cast<float*>(&joint.translateUpperLimit.x));
        ImGui::DragFloat3("Rotate Lower Limit", const_cast<float*>(&joint.rotateLowerLimit.x));
        ImGui::DragFloat3("Rotate Upper Limit", const_cast<float*>(&joint.rotateUpperLimit.x));
        if (joint.type == JointType::SpringDof6) {
            ImGui::DragFloat3("Spring Translate Factor", const_cast<float*>(&joint.springTranslateFactor.x));
            ImGui::DragFloat3("Spring Rotate Factor", const_cast<float*>(&joint.springRotateFactor.x));
        }
        ImGui::EndDisabled();
        break;
    }
    }
}
#endif
```

(The `const_cast<float*>`/`const_cast<bool*>` calls above mirror a real,
necessary pattern for feeding a `const`-sourced field into an ImGui widget
that technically accepts a mutable pointer while ALWAYS being wrapped in
`ImGui::BeginDisabled()` — the same trick already implicitly relied upon
is avoidable by instead taking local non-const copies before each
`BeginDisabled()` block if a reviewer prefers to avoid `const_cast`
entirely; either is acceptable, but prefer local copies
(`Vec3 position = bone.position; ImGui::DragFloat3("Position",
&position.x);`) if the team's own style leans against `const_cast` — check
whether `BuildEntityInspector()`'s existing "Mesh Renderer" read-only
section sets a precedent either way (it does not need this trick today
since it uses `ImGui::Text()` exclusively, not `DragFloat*`/`Checkbox`, so
there is no existing precedent to match here; picking the "local non-const
copy" style is the safer default to avoid introducing this codebase's
first `const_cast`).

### 3.4 `InspectorPanel.cpp` — wire the new branch into `BuildInspectorPanel()`

```cpp
#if GTE_ENABLE_PROJECT_PANEL
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, Renderer& renderer, AssetPreviewTexture& assetPreview,
    AssetPreviewMesh& assetPreviewMesh, BoneViewerWindow& boneViewer, PhysicsSystem& physicsSystem, ModelRigCache& rigCache)
#else
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, PhysicsSystem& physicsSystem)
#endif
{
    ImGui::Begin("Inspector");

#if GTE_ENABLE_PROJECT_PANEL
    if (ctx.selection.Kind() == InspectorSelectionKind::Asset && !ctx.selection.SelectedAssetAbsolutePath().empty()) {
        BuildAssetInspector(ctx, renderer, assetPreview, assetPreviewMesh);
        ImGui::End();
        return;
    }
    if (ctx.selection.Kind() == InspectorSelectionKind::ModelPart) {
        BuildModelPartInspector(registry, ctx, rigCache);
        ImGui::End();
        return;
    }
#endif

#if GTE_ENABLE_PROJECT_PANEL
    BuildEntityInspector(registry, ctx, boneViewer, physicsSystem);
#else
    BuildEntityInspector(registry, ctx, physicsSystem);
#endif

    ImGui::End();
}
```

### 3.5 `ImGuiEditorLayer.cpp` — final call-site update

`m_modelRigCache` already exists as a member (added by Phase 3, 3.7) — this
phase only updates the ONE remaining call site that needs it:

```cpp
// Was: BuildInspectorPanel(registry, m_ctx, renderer, m_assetPreview, m_assetPreviewMesh, m_boneViewer, game.GetPhysicsSystem());
BuildInspectorPanel(registry, m_ctx, renderer, m_assetPreview, m_assetPreviewMesh, m_boneViewer, game.GetPhysicsSystem(), m_modelRigCache);
```

## Step 4: What We Will NOT Do

- We will **not** make any field in the new "Model Part" section editable —
  every widget is wrapped in `ImGui::BeginDisabled()`/uses plain `ImGui::Text()`,
  matching this file's own existing "Mesh Renderer"/"Global Physics Settings"
  read-only precedent, and matching `PHASE0_MASTER_STRATEGY.md`'s own "What
  We Will NOT Do" scope limit.
- We will **not** add a way to jump FROM the Inspector's Model Part section
  back to opening/focusing the Bone Viewer window on that same part (e.g. a
  "Show in Bone Viewer" button) — the existing "Open Bone Viewer" button
  already lives in the Entity section for the model's root entity; a
  reverse-navigation affordance is a plausible future nicety, not part of
  this story's four stated requirements. **(v2)** This is DIFFERENT from,
  and not contradicted by, the small "Select Owning Entity" button 3.3 now
  adds — that button only flips `ctx.selection` back to the OWNING ENTITY
  (restoring the normal Entity Inspector view, including its own "Open Bone
  Viewer" button), it does not itself open/focus/scroll-to-anything inside
  the Bone Viewer window.
- We will **not** change `BuildAssetInspector()`/`BuildEntityInspector()`'s
  own existing behavior in any way beyond the new branch ordering in
  `BuildInspectorPanel()` itself (which must run BEFORE the unconditional
  `BuildEntityInspector()` fallback, exactly like the existing `Asset`
  branch already does, and must `return` immediately after, exactly
  matching that existing branch's own shape).
- We will **not** attempt to keep the Model Part section visible/valid if
  the underlying `*.gta` changes shape between frames in a way that makes
  the stored index stall out of range (e.g. an external re-import shrinking
  the rig) — the existing per-case `index out of range` guards already
  degrade gracefully (a plain disabled message, never a crash/out-of-bounds
  read) and that is sufficient; this is not expected to happen during normal
  Editor use (nothing in this engine hot-reloads a `*.gta` while its
  Inspector info is on screen) and is not worth further defensive UX.

## Step 5: Their Role

Implementer checklist for this phase:

1. Edit `src/Editor/Panels/InspectorPanel.h` per 3.1 (forward-declare
   `ModelRigCache`, add the new parameter to the `GTE_ENABLE_PROJECT_PANEL`
   signature only).
2. Edit `src/Editor/Panels/InspectorPanel.cpp` per 3.2–3.4 (new includes,
   `BuildModelPartInspector()` + its three label helpers — including
   **(v2)** the Bone case's IK-chain/`visible`/`controllable` additions, the
   RigidBody case's collision-group/mask additions, and the new "Select
   Owning Entity" button — wire the new branch into `BuildInspectorPanel()`).
3. Edit `src/Editor/ImGuiEditorLayer.cpp` per 3.5 (update the
   `BuildInspectorPanel(...)` call site to pass `m_modelRigCache`).
4. Build `GreatTamanaEngine` and manually verify, against a real imported
   MMD model with rigid bodies/joints, completing the full loop this
   campaign exists to deliver:
   - Open the Bone Viewer, select a bone → the Inspector immediately shows
     that bone's name/parent/position/flags.
   - Switch the Bone Viewer's dropdown to "Rigid Bodies", select one → the
     Inspector immediately shows that rigid body's shape/size/transform/
     mass/damping/motion type/attached bone.
   - Switch to "Joints", select one → the Inspector immediately shows that
     joint's type/connected rigid bodies/transform/limits.
   - Clicking back on an entity row in "Hierarchy" correctly returns the
     Inspector to the normal Transform/MeshRenderer/etc. entity view (proves
     `Kind()` correctly flips away from `ModelPart` and nothing about the
     new section leaks into the entity view or vice versa).
   - With `GTE_ENABLE_PROJECT_PANEL=OFF`, confirm the engine still builds
     (the whole feature — Bone Viewer AND this Inspector section — is
     structurally absent, with no dangling reference to `ModelRigCache`/
     `ModelPartKind` anywhere in the non-Project-panel build path).
   - **(v2)** Select an IK bone (e.g. a leg/foot IK bone on a typical MMD
     model) → the Inspector shows its Target Bone/Iteration Count/Angle
     Limit/Links; select a NON-IK bone → the IK Chain section doesn't
     appear at all.
   - **(v2)** Select a rigid body → its Collision Group/Mask are shown
     alongside every other already-working field.
   - **(v2)** With a bone/rigid body/joint selected, click "Select Owning
     Entity" → the Inspector immediately reverts to that entity's normal
     Transform/MeshRenderer/etc. view, INCLUDING its own "Open Bone Viewer"
     button, without ever touching Hierarchy.
