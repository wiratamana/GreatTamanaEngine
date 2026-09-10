#include "InspectorPanel.h"

#include "../EditorContext.h"
#include "../../ECS/Components/Camera.h"
#include "../../ECS/Components/DirectionalLight.h"
#include "../../ECS/Components/DynamicChainRig.h"
#include "../../ECS/Components/MeshRenderer.h"
#include "../../ECS/Components/Name.h"
#include "../../ECS/Components/SkeletalAnimator.h"
#include "../../ECS/Components/Transform.h"
#include "../../ECS/Registry.h"
#include "../../ECS/TransformHierarchy.h"
#include "../../Game/Physics/PhysicsSystem.h"
#include "../../Game/Instantiation/MeshInstantiationSystem.h"
#include "../../Game/Physics/DynamicChainPhysicsPersistence.h"
#include "../../Physics/DynamicChainDefinition.h"

#if GTE_ENABLE_PROJECT_PANEL
#include "../AssetInspectorData.h"
#include "../AssetPreviewMesh.h"
#include "../AssetPreviewTexture.h"
#include "../BoneViewerWindow.h"
#include "../MemoryPanelData.h" // FormatBytes() - reused for the asset size field below.
#include "../ModelRigCache.h" // ModelRigCache::GetOrLoad() - shared bones/rigid-bodies/joints cache, Model Part section.
#include "../ProjectPanelData.h" // Utf8ToPath()
#include "../../Assets/AssetTypes.h" // AssetType, AssetFlags, Guid
#include "../../Assets/GtaFile.h" // ReadGtaHeader()/ReadGtaFile()
#include "../../Assets/MotionFile.h" // DecodeMotionDataFromBytes()
#include "../../Assets/PhysicsData.h" // RigidBody, Joint, RigidBodyShape, RigidBodyMotionType, JointType
#include "../../ECS/Components/MeshAssetSource.h"
#include "../../Renderer/Renderer.h"
#endif

#include <imgui.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace gte {
namespace {

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

// task_manager/verlet-integration-10, PHASE4 - counts how many of
// `colliders` this SPECIFIC chain's joints could ever actually reach under
// PMX collision-group/mask rules (mirrors DynamicChainSolver.cpp's own
// GroupsMayCollide()/GroupBit() truth table exactly, but is deliberately NOT
// a call into either of those functions - they are `static`/anonymous-
// namespace, not exported; this is a small, independent, Editor-only
// re-derivation purely for an informational readout, never itself part of
// the simulation). A chain with multiple joints having DIFFERENT group/mask
// values (uncommon but not forbidden - PHASE1 seeds this per-joint, not
// per-chain) counts a collider as "reachable" if ANY of the chain's own
// joints could hit it. `group` is masked to its documented 4-bit range
// (`& 0x0Fu`) before use as a shift amount - see PHASE1's own GroupBit()
// doc comment for why this is required for safety, not merely style (a raw,
// unvalidated .pmx file byte, confirmed never range-checked anywhere in this
// engine's load pipeline).
std::size_t CountCollidersReachableByChain(const DynamicChainDefinition& chain, const std::vector<ModelColliderDefinition>& colliders)
{
    std::size_t reachable = 0;
    for (const ModelColliderDefinition& collider : colliders) {
        bool anyJointReaches = false;
        for (const DynamicJointSettings& joint : chain.jointSettings) {
            const std::uint16_t jointBit = static_cast<std::uint16_t>(1u << (joint.group & 0x0Fu));
            const std::uint16_t colliderBit = static_cast<std::uint16_t>(1u << (collider.group & 0x0Fu));
            if ((jointBit & collider.collisionMask) != 0 && (colliderBit & joint.collisionMask) != 0) {
                anyJointReaches = true;
                break;
            }
        }
        if (anyJointReaches) {
            ++reachable;
        }
    }
    return reachable;
}

// Shown whenever ctx.selection.Kind() == ModelPart (see
// task_manager/verlet-integration-2/PHASE1_SELECTION_MODEL_PART_FOUNDATION.md)
// - the sub-part-of-a-model equivalent of BuildEntityInspector()/
// BuildAssetInspector() below. Every field is read-only (plain ImGui::Text()
// or ImGui::BeginDisabled()) for the Bone/RigidBody/Joint cases - there is no
// physics simulation anywhere in the engine that consumes RigidBody/Joint
// data yet (see Assets/PhysicsData.h's own file comment), so there is
// nothing for an edit here to actually drive - same scope limit as this
// file's existing "Mesh Renderer"/"Global Physics Settings" read-only
// sections. The Verlet case (task_manager/verlet-integration-5,
// PHASE3_INSPECTOR_VERLET_JOINT_SECTION.md) is the one exception - a real,
// already-running simulation (PhysicsSystem::Update()) DOES consume
// DynamicJointSettings, so that branch genuinely live-edits
// PhysicsSystem's own DynamicChainRigCache.
void BuildModelPartInspector(Registry& registry, EditorContext& ctx, ModelRigCache& rigCache, PhysicsSystem& physicsSystem)
{
    const Entity owner = ctx.selection.SelectedModelPartEntity();
    if (!registry.IsAlive(owner)) {
        ImGui::TextDisabled("The selected model part's owning entity no longer exists.");
        return;
    }

    // Lets the user get back to the Entity Inspector view (and its "Open
    // Bone Viewer" button, only reachable from that view - see
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

    // Multi-selection summary - as of task_manager/verlet-integration-4
    // (PHASE1_SELECTION_MULTI_MODEL_PART_SUPPORT.md/
    // PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md),
    // Selection can hold MANY Model-Part indices at once (the Bone Viewer's
    // "Select All (Group)"/"Select All (Branch)" toolbar buttons, or
    // Ctrl/Shift-click). A single part's full read-only property sheet
    // below only ever makes sense for exactly ONE selected part - showing
    // it for just the FIRST of many, with no indication anything else is
    // also selected, would be a silent, misleading regression. Whenever
    // more than one index is selected, show a compact "N <Kind>s Selected"
    // summary + name list instead, and return early - the single-part
    // switch below is never reached in that case.
    const std::vector<int>& selectedIndices = ctx.selection.SelectedModelPartIndices();
    if (selectedIndices.size() > 1) {
        const ModelPartKind kind = ctx.selection.SelectedModelPartKind();
        const char* kindNoun = kind == ModelPartKind::Bone ? "Bones"
            : kind == ModelPartKind::RigidBody ? "Rigid Bodies"
            : kind == ModelPartKind::Joint      ? "Joints"
                                                : "Verlet Joints";
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "%zu %s Selected", selectedIndices.size(), kindNoun);
        ImGui::Separator();

        ImGui::BeginChild("InspectorMultiSelectionList", ImVec2(0.0f, 200.0f), true);
        for (const int selectedIndex : selectedIndices) {
            std::string label = "(out of range)";
            switch (kind) {
            case ModelPartKind::Bone:
                if (selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < rig->skeleton.bones.size()) {
                    const std::string& name = rig->skeleton.bones[static_cast<std::size_t>(selectedIndex)].name;
                    label = name.empty() ? "(unnamed)" : name;
                }
                break;
            case ModelPartKind::RigidBody:
                if (selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < rig->physics.rigidBodies.size()) {
                    const std::string& name = rig->physics.rigidBodies[static_cast<std::size_t>(selectedIndex)].name;
                    label = name.empty() ? "(unnamed)" : name;
                }
                break;
            case ModelPartKind::Joint:
                if (selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < rig->physics.joints.size()) {
                    const std::string& name = rig->physics.joints[static_cast<std::size_t>(selectedIndex)].name;
                    label = name.empty() ? "(unnamed)" : name;
                }
                break;
            case ModelPartKind::Verlet:
                if (selectedIndex >= 0 && static_cast<std::size_t>(selectedIndex) < rig->skeleton.bones.size()) {
                    const std::string& name = rig->skeleton.bones[static_cast<std::size_t>(selectedIndex)].name;
                    label = name.empty() ? "(unnamed)" : name;
                }
                break;
            }
            ImGui::BulletText("[%d] %s", selectedIndex, label.c_str());
        }
        ImGui::EndChild();
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
        {
            Vec3 position = bone.position;
            ImGui::BeginDisabled();
            ImGui::DragFloat3("Position", &position.x);
            ImGui::EndDisabled();
        }
        ImGui::Text("Deform Depth: %d", bone.deformDepth);
        {
            bool rotatable = bone.rotatable;
            bool translatable = bone.translatable;
            bool isIk = bone.isIk;
            bool deformAfterPhysics = bone.deformAfterPhysics;
            bool visible = bone.visible;
            bool controllable = bone.controllable;
            ImGui::BeginDisabled();
            ImGui::Checkbox("Rotatable", &rotatable);
            ImGui::SameLine();
            ImGui::Checkbox("Translatable", &translatable);
            ImGui::Checkbox("IK", &isIk);
            ImGui::SameLine();
            ImGui::Checkbox("Deform After Physics", &deformAfterPhysics);
            ImGui::Checkbox("Visible", &visible);
            ImGui::SameLine();
            ImGui::Checkbox("Controllable", &controllable);
            ImGui::EndDisabled();
        }

        // IK chain - only meaningful when isIk is true (see
        // Assets/SkeletonData.h's own Bone::isIk doc comment). Squarely
        // in-scope for this window: BoneViewerWindow.h's own class comment
        // names diagnosing an IK/animation bone mismatch as this whole
        // window's reason to exist.
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
            {
                float angleLimit = bone.ikAngleLimitRadians;
                ImGui::BeginDisabled();
                ImGui::DragFloat("Angle Limit (radians)", &angleLimit);
                ImGui::EndDisabled();
            }
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
            ImGui::Text("Attached Bone: %s (index %d)",
                rig->skeleton.bones[static_cast<std::size_t>(body.boneIndex)].name.c_str(), body.boneIndex);
        } else {
            ImGui::TextDisabled("Attached Bone: (none)");
        }
        ImGui::Text("Shape: %s", RigidBodyShapeLabel(body.shape));
        ImGui::Text("Motion Type: %s", RigidBodyMotionTypeLabel(body.motionType));
        // Collision filtering fields - matches Bullet's own group/mask
        // convention exactly (PhysicsData.h's own RigidBody::group/
        // collisionGroupMask doc comment) - shown as plain unsigned/hex
        // text, not editable widgets, since neither is a float/bool
        // DragFloat*/Checkbox can represent directly.
        ImGui::Text("Collision Group: %u", static_cast<unsigned>(body.group));
        ImGui::Text("Collision Mask: 0x%04X", static_cast<unsigned>(body.collisionGroupMask));
        {
            Vec3 shapeSize = body.shapeSize;
            Vec3 translate = body.translate;
            Vec3 rotateRadians = body.rotateRadians;
            float mass = body.mass;
            float linearDamping = body.linearDamping;
            float angularDamping = body.angularDamping;
            float restitution = body.restitution;
            float friction = body.friction;
            ImGui::BeginDisabled();
            ImGui::DragFloat3("Shape Size", &shapeSize.x);
            ImGui::DragFloat3("Translate", &translate.x);
            ImGui::DragFloat3("Rotate (radians)", &rotateRadians.x);
            ImGui::DragFloat("Mass", &mass);
            ImGui::DragFloat("Linear Damping", &linearDamping);
            ImGui::DragFloat("Angular Damping", &angularDamping);
            ImGui::DragFloat("Restitution", &restitution);
            ImGui::DragFloat("Friction", &friction);
            ImGui::EndDisabled();
        }
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

        {
            Vec3 translate = joint.translate;
            Vec3 rotateRadians = joint.rotateRadians;
            Vec3 translateLowerLimit = joint.translateLowerLimit;
            Vec3 translateUpperLimit = joint.translateUpperLimit;
            Vec3 rotateLowerLimit = joint.rotateLowerLimit;
            Vec3 rotateUpperLimit = joint.rotateUpperLimit;
            Vec3 springTranslateFactor = joint.springTranslateFactor;
            Vec3 springRotateFactor = joint.springRotateFactor;
            ImGui::BeginDisabled();
            ImGui::DragFloat3("Translate", &translate.x);
            ImGui::DragFloat3("Rotate (radians)", &rotateRadians.x);
            ImGui::DragFloat3("Translate Lower Limit", &translateLowerLimit.x);
            ImGui::DragFloat3("Translate Upper Limit", &translateUpperLimit.x);
            ImGui::DragFloat3("Rotate Lower Limit", &rotateLowerLimit.x);
            ImGui::DragFloat3("Rotate Upper Limit", &rotateUpperLimit.x);
            if (joint.type == JointType::SpringDof6) {
                ImGui::DragFloat3("Spring Translate Factor", &springTranslateFactor.x);
                ImGui::DragFloat3("Spring Rotate Factor", &springRotateFactor.x);
            }
            ImGui::EndDisabled();
        }
        break;
    }
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

        // task_manager/verlet-integration-6, Phase 5, 3.4 - show this joint's
        // own resolved TREE parent (root bone name if parentJointIndex[jointIndex]
        // < 0, otherwise the sibling joint bone's own name), mirroring
        // BoneViewerWindow.cpp's own tree-pane/gizmo resolution logic exactly.
        const std::int32_t parentJoint = (jointIndex < chain.parentJointIndex.size()) ? chain.parentJointIndex[jointIndex] : -1;
        std::string parentName = "(none)";
        if (parentJoint < 0) {
            if (chain.rootBoneIndex >= 0 && static_cast<std::size_t>(chain.rootBoneIndex) < rig->skeleton.bones.size()) {
                parentName = rig->skeleton.bones[static_cast<std::size_t>(chain.rootBoneIndex)].name;
            }
        } else if (static_cast<std::size_t>(parentJoint) < chain.jointBoneIndices.size()) {
            const std::int32_t parentBoneIndex = chain.jointBoneIndices[static_cast<std::size_t>(parentJoint)];
            if (parentBoneIndex >= 0 && static_cast<std::size_t>(parentBoneIndex) < rig->skeleton.bones.size()) {
                parentName = rig->skeleton.bones[static_cast<std::size_t>(parentBoneIndex)].name;
            }
        }
        ImGui::Text("Tree Parent: %s", parentName.empty() ? "(unnamed)" : parentName.c_str());

        // Extra ("web brace") structural constraints referencing this exact
        // joint - so a user inspecting one particle also learns it
        // participates in the skirt's web bracing, not just its own tree edge.
        std::size_t extraBraceCount = 0;
        for (const ExtraStructuralConstraint& extra : chain.extraConstraints) {
            if (extra.jointIndexA == static_cast<std::int32_t>(jointIndex)
                || extra.jointIndexB == static_cast<std::int32_t>(jointIndex)) {
                ++extraBraceCount;
            }
        }
        if (extraBraceCount > 0) {
            ImGui::TextDisabled("Extra brace constraints: %zu", extraBraceCount);
        }
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

        // task_manager/verlet-integration-10, PHASE4 - read-only, derived
        // from this joint's own PMX Dynamic/DynamicAndBoneMerge rigid body
        // shape/size (PHASE2's DeriveJointCollisionRadius()) - mirrors this
        // same function's own `restLength` readout above (BeginDisabled()/
        // EndDisabled()) since a user's edit here could never be authored
        // back into the source model either.
        ImGui::BeginDisabled();
        ImGui::DragFloat("Collision Radius (from PMX rigid body shape)", &settings.collisionRadius);
        ImGui::EndDisabled();

        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "Chain-Wide Settings");
        ImGui::DragFloat("Gravity Scale", &chain.gravityScale, 0.01f, 0.0f, 10.0f);
        ImGui::DragFloat("Wind Scale", &chain.windScale, 0.01f, 0.0f, 10.0f);
        // task_manager/verlet-integration-9, PHASE5 - replaces the old
        // "Head Collider" single-sphere bone-index/radius pair entirely.
        // Collision now automatically targets EVERY Static rigid body
        // (Sphere/Box/Capsule) the model's own PMX data describes (see
        // Physics/ModelColliderDetection.h) - there is nothing left to
        // hand-author beyond this one opt-in checkbox. task_manager/
        // verlet-integration-10, PHASE1/4 - "every" is no longer accurate:
        // PMX collision-group/layer rules (RigidBody::group/
        // collisionGroupMask) can exclude some of a model's own detected
        // colliders from THIS specific chain - the readout below now
        // reports the real reachable count instead.
        ImGui::Checkbox("Enable Collision", &chain.collisionEnabled);
        if (chain.collisionEnabled) {
            const std::size_t reachable = CountCollidersReachableByChain(chain, model->colliders);
            ImGui::TextDisabled(
                "Collides against %zu of %zu auto-detected Static rigid-body collider(s) for this model "
                "(the rest are excluded by this chain's own PMX collision-group/layer rules).",
                reachable, model->colliders.size());
            if (model->colliders.empty()) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                    "This model has no detected Static rigid-body colliders - enabling this has no effect.");
            } else if (reachable == 0) {
                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                    "None of this model's detected colliders are reachable by this chain's own PMX collision "
                    "group/mask - enabling this currently has no visible effect for this chain specifically.");
            }
        }
        break;
    }
    }
}
#endif

#if GTE_ENABLE_PROJECT_PANEL
void BuildEntityInspector(Registry& registry, EditorContext& ctx, BoneViewerWindow& boneViewer, PhysicsSystem& physicsSystem,
    MeshInstantiationSystem& meshInstantiationSystem)
#else
void BuildEntityInspector(Registry& registry, EditorContext& ctx, PhysicsSystem& physicsSystem,
    MeshInstantiationSystem& meshInstantiationSystem)
#endif
{
    const Entity entity = ctx.selection.SelectedEntity();

    if (!registry.IsAlive(entity)) {
        ImGui::TextDisabled("No entity selected.");
        return;
    }

    ImGui::Text("Entity %u (generation %u)", entity.index, entity.generation);

    // Optional editable display Name (ECS/Components/Name.h) - an entity
    // with none yet gets one lazily the moment its name is actually edited
    // here, rather than every entity paying for one up front.
    {
        Name* name = registry.TryGetComponent<Name>(entity);
        char nameBuffer[256];
        std::snprintf(nameBuffer, sizeof(nameBuffer), "%s", name != nullptr ? name->value.c_str() : "");
        if (ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer))) {
            if (name != nullptr) {
                name->value = nameBuffer;
            } else {
                registry.AddComponent<Name>(entity, Name{ std::string(nameBuffer) });
            }
        }
    }

    ImGui::Separator();

    if (Transform* transform = registry.TryGetComponent<Transform>(entity)) {
        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::DragFloat3("Position", &transform->position.x, 0.01f);

            Vec3 eulerDegrees = transform->rotation.ToEulerDegrees();
            if (ImGui::DragFloat3("Rotation", &eulerDegrees.x, 0.1f)) {
                transform->rotation = Quat::FromEulerDegrees(eulerDegrees.x, eulerDegrees.y, eulerDegrees.z);
            }

            ImGui::DragFloat3("Scale", &transform->scale.x, 0.01f);

            // Read-only "Parent" info + a one-click "Unparent" (Unity's own
            // Transform.SetParent(null)) - the Inspector-side complement to
            // "Hierarchy"'s own drag-and-drop attach/detach (see
            // Panels/HierarchyPanel.h). Reparenting itself (choosing a NEW
            // parent) is still drag-and-drop-only in "Hierarchy" - there's
            // no entity picker widget in this engine yet to pick one from
            // here.
            if (transform->parent.IsValid() && registry.IsAlive(transform->parent)) {
                ImGui::Text("Parent: Entity %u", transform->parent.index);
                ImGui::SameLine();
                if (ImGui::SmallButton("Unparent")) {
                    SetParent(registry, entity, kInvalidEntity);
                }
            } else {
                ImGui::TextDisabled("Parent: (none)");
            }
        }
    }

    if (MeshRenderer* meshRenderer = registry.TryGetComponent<MeshRenderer>(entity)) {
        if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::BeginDisabled();
            ImGui::Text("Mesh handle:     index %u, generation %u",
                meshRenderer->mesh.index, meshRenderer->mesh.generation);
            ImGui::Text("Pipeline handle: index %u, generation %u",
                meshRenderer->pipeline.index, meshRenderer->pipeline.generation);
            ImGui::EndDisabled();
        }
    }

    if (Camera* camera = registry.TryGetComponent<Camera>(entity)) {
        if (ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Active", &camera->active);
            ImGui::DragFloat("Field of View (Y)", &camera->fovYDegrees, 0.5f, 1.0f, 179.0f);
            ImGui::DragFloat("Near Z", &camera->nearZ, 0.01f, 0.001f, camera->farZ - 0.01f);
            ImGui::DragFloat("Far Z", &camera->farZ, 1.0f, camera->nearZ + 0.01f);
        }
    }

    // Atmosphere Scattering + Aerial Perspective campaign, Phase 8
    // (ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md) - mirrors the
    // "Camera" section directly above exactly (same layout/style): a color
    // picker, an illuminance field, and an active checkbox. Direction is
    // deliberately NOT edited here - it's derived from this same entity's
    // "Transform" section above (see DirectionalLight.h's own doc comment).
    if (DirectionalLight* light = registry.TryGetComponent<DirectionalLight>(entity)) {
        if (ImGui::CollapsingHeader("Directional Light", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::ColorEdit3("Color", &light->color.x);
            ImGui::DragFloat("Illuminance (lux)", &light->illuminanceLux, 100.0f, 0.0f, 200000.0f);
            ImGui::Checkbox("Active", &light->active);
        }
    }

    // Shown only for a model root entity currently playing back a motion
    // (see Game::PlayAnimationOnEntity(), src/Game/Game.cpp) - a plain
    // playback-state readout/control panel, Unity's own Animator component
    // inspector in spirit (though far simpler - no state machine here, just
    // a single clip path + play/loop/speed/frame).
    if (SkeletalAnimator* animator = registry.TryGetComponent<SkeletalAnimator>(entity)) {
        if (ImGui::CollapsingHeader("Skeletal Animator", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::TextWrapped("Clip: %s",
                animator->animationGtaPath.empty() ? "(none)" : animator->animationGtaPath.c_str());
            ImGui::Checkbox("Playing", &animator->playing);
            ImGui::SameLine();
            ImGui::Checkbox("Loop", &animator->loop);
            ImGui::DragFloat("Speed", &animator->speed, 0.01f, 0.0f, 5.0f);
            ImGui::Text("Frame: %.1f", animator->frame);
        }
    }

    // Shown for any entity carrying a DynamicChainRig component (PHASE4,
    // task_manager/verlet-integration-1/PHASE4_PARAMETER_AUTHORING_AND_DATA_DRIVEN_CONFIG.md,
    // 3.5) - i.e. a model root PhysicsSystem::AttachDynamicChainRigIfNeeded()
    // decided has at least one auto-detected dynamic bone chain (see
    // Physics/DynamicChainDetection.h). Per-joint damping/stiffness/mass
    // sliders write directly into the DynamicChainDefinition
    // PhysicsSystem's own DynamicChainRigCache holds for this model PATH
    // (shared by every entity spawned from the same *.gta - no per-instance
    // override in this phase, see that document's own "What We Will NOT
    // Do"). The GlobalPhysicsSettings readout at the bottom is read-only
    // this phase (see the same "What We Will NOT Do" section) - it exists
    // purely to make the global/local split visible/legible to a user.
    if (DynamicChainRig* rig = registry.TryGetComponent<DynamicChainRig>(entity)) {
        if (ImGui::CollapsingHeader("Dynamic Chain Physics", ImGuiTreeNodeFlags_DefaultOpen)) {
            ImGui::Checkbox("Enabled", &rig->enabled);
            ImGui::SameLine();
            ImGui::Checkbox("Freeze", &rig->frozen);
            ImGui::TextDisabled(
                "Enabled: physics runs every frame. Freeze: keep the current jiggled shape, stop simulating "
                "further, still rides along rigidly with the model.");

            // task_manager/verlet-integration-11, PHASE4 - persists the
            // CURRENT live DynamicJointSettings of every joint of every
            // chain belonging to this model path back into its own *.gta
            // file's metadata (RigFileData::jointPhysicsOverrides, PHASE1),
            // so PhysicsSystem::RegisterDynamicChains() (PHASE2) re-applies
            // them the next time this model path is instantiated - this
            // session (via the RefreshCachedJointPhysicsOverridesFromDisk()
            // call below, PHASE3) or any future one. The two static locals
            // are scoped as narrowly as possible (see this phase's own
            // strategy doc, Step 3.3, for why a plain function-local static
            // was chosen over promoting this file to a class).
            static bool s_lastJointPhysicsSaveSucceeded = false;
            static std::string s_lastJointPhysicsSaveError;

            if (ImGui::Button("Save Joint Physics to Asset")) {
                std::string errorMessage;
                const DynamicChainRigCache::ModelEntry* currentModel
                    = physicsSystem.GetDynamicChainRigCache().TryGet(rig->meshGtaPath);
                if (currentModel == nullptr) {
                    s_lastJointPhysicsSaveError = "No detected dynamic bone chains to save for this model.";
                    s_lastJointPhysicsSaveSucceeded = false;
                } else {
                    s_lastJointPhysicsSaveSucceeded
                        = SaveJointPhysicsOverridesToGtaFile(rig->meshGtaPath, currentModel->chains, &errorMessage);
                    s_lastJointPhysicsSaveError = errorMessage;
                    if (s_lastJointPhysicsSaveSucceeded) {
                        meshInstantiationSystem.RefreshCachedJointPhysicsOverridesFromDisk(rig->meshGtaPath);
                    }
                }
            }
            if (!s_lastJointPhysicsSaveError.empty() || s_lastJointPhysicsSaveSucceeded) {
                if (s_lastJointPhysicsSaveSucceeded) {
                    ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.4f, 1.0f), "Saved joint physics to asset.");
                } else {
                    ImGui::TextColored(ImVec4(0.95f, 0.4f, 0.4f, 1.0f), "Save failed: %s", s_lastJointPhysicsSaveError.c_str());
                }
            }
            ImGui::TextDisabled(
                "Writes the CURRENT damping/stiffness/mass of every joint below into this model's *.gta file, so it "
                "is restored automatically the next time this model is instantiated (this session or a future one).");

            DynamicChainRigCache::ModelEntry* model
                = physicsSystem.GetDynamicChainRigCache().TryGetMutable(rig->meshGtaPath);
            if (model == nullptr || model->chains.empty()) {
                ImGui::TextDisabled("No detected dynamic bone chains for this model.");
            } else {
                std::size_t totalJoints = 0;
                for (const DynamicChainDefinition& chain : model->chains) {
                    totalJoints += chain.jointBoneIndices.size();
                }
                ImGui::Text("%zu chain(s), %zu joint(s) total", model->chains.size(), totalJoints);
                // task_manager/verlet-integration-6, Phase 5, 3.5 (v2) - a
                // single, cheap pointer toward the Bone Viewer's Verlet tree
                // pane for a user who only ever opens the Inspector, rather
                // than duplicating the full per-entry listing in two panels.
                if (!model->diagnostics.crossChainJointsDropped.empty()
                    || !model->diagnostics.duplicateBoneRigidBodyAssignmentsDropped.empty()) {
                    ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                        "%zu cross-chain joint(s) and %zu duplicate rigid-body assignment(s) were dropped during "
                        "detection - see the Bone Viewer's Verlet tree pane for details.",
                        model->diagnostics.crossChainJointsDropped.size(),
                        model->diagnostics.duplicateBoneRigidBodyAssignmentsDropped.size());
                }

                for (std::size_t chainIndex = 0; chainIndex < model->chains.size(); ++chainIndex) {
                    DynamicChainDefinition& chain = model->chains[chainIndex];
                    ImGui::PushID(static_cast<int>(chainIndex));
                    char chainLabel[64];
                    std::snprintf(chainLabel, sizeof(chainLabel), "Chain %zu (%zu joints)", chainIndex,
                        chain.jointBoneIndices.size());
                    if (ImGui::TreeNode(chainLabel)) {
                        for (std::size_t jointIndex = 0; jointIndex < chain.jointSettings.size(); ++jointIndex) {
                            DynamicJointSettings& settings = chain.jointSettings[jointIndex];
                            ImGui::PushID(static_cast<int>(jointIndex));
                            ImGui::Text("Joint %zu", jointIndex);
                            ImGui::DragFloat("Damping", &settings.damping, 0.005f, 0.0f, 1.0f);
                            ImGui::DragFloat("Stiffness", &settings.stiffness, 0.005f, 0.0f, 1.0f);
                            ImGui::DragFloat("Weight (Mass)", &settings.mass, 0.01f, 0.01f, 100.0f);
                            // task_manager/verlet-integration-10, PHASE4 -
                            // read-only, derived from this joint's own PMX
                            // rigid body shape/size (see the single-part
                            // Inspector's own identical readout above for
                            // the full rationale).
                            ImGui::BeginDisabled();
                            ImGui::DragFloat("Collision Radius (from PMX rigid body shape)", &settings.collisionRadius);
                            ImGui::EndDisabled();
                            ImGui::PopID();
                        }

                        // task_manager/verlet-integration-9, PHASE5 -
                        // replaces the old "Head Collider" single-sphere
                        // bone-index/radius pair entirely - see
                        // Physics/DynamicChainDefinition.h's own
                        // `collisionEnabled` doc comment. task_manager/
                        // verlet-integration-10, PHASE1/4 - "every" is no
                        // longer accurate: PMX collision-group/layer rules
                        // can exclude some colliders from this specific
                        // chain - see the reachable-count readout below.
                        ImGui::Separator();
                        ImGui::Checkbox("Enable Collision", &chain.collisionEnabled);
                        if (chain.collisionEnabled) {
                            const std::size_t reachable = CountCollidersReachableByChain(chain, model->colliders);
                            ImGui::TextDisabled(
                                "Collides against %zu of %zu auto-detected Static rigid-body collider(s) for this model "
                                "(the rest are excluded by this chain's own PMX collision-group/layer rules).",
                                reachable, model->colliders.size());
                            if (model->colliders.empty()) {
                                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                                    "This model has no detected Static rigid-body colliders - enabling this has no effect.");
                            } else if (reachable == 0) {
                                ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f),
                                    "None of this model's detected colliders are reachable by this chain's own PMX collision "
                                    "group/mask - enabling this currently has no visible effect for this chain specifically.");
                            }
                        }
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
            }

            ImGui::Separator();
            ImGui::TextDisabled("Global Physics Settings (world-level, read-only):");
            GlobalPhysicsSettings& globalSettings = physicsSystem.GetGlobalPhysicsSettings();
            ImGui::BeginDisabled();
            ImGui::DragFloat3("Gravity", &globalSettings.gravity.x);
            ImGui::DragFloat3("Wind Direction", &globalSettings.wind.direction.x);
            ImGui::DragFloat("Wind Base Strength", &globalSettings.wind.baseStrength);
            ImGui::DragFloat("Wind Gust Strength", &globalSettings.wind.gustStrength);
            ImGui::EndDisabled();
        }
    }


#if GTE_ENABLE_PROJECT_PANEL
    // Shown for the ROOT entity of any model spawned via
    // Game::CreateMeshEntityFromGtaFile() (see ECS/Components/
    // MeshAssetSource.h) - a button that opens (or re-targets) the
    // Unity-"Avatar configuration"-style Bone Viewer floating window
    // (BoneViewerWindow.h) onto this entity, good for debugging exactly why
    // an imported model's bones/animation don't line up (mismatched names,
    // an unexpected hierarchy, ...). Always shown for any MeshAssetSource
    // entity regardless of whether its model actually turns out to have
    // skeleton data - BoneViewerWindow itself reports "no bone/skeleton
    // data" plainly if not, same "never a spurious failure message, just a
    // plain fact" convention as everything else in this panel.
    if (registry.TryGetComponent<MeshAssetSource>(entity) != nullptr) {
        if (ImGui::CollapsingHeader("Model", ImGuiTreeNodeFlags_DefaultOpen)) {
            if (ImGui::Button("Open Bone Viewer")) {
                boneViewer.Open(entity);
            }
        }
    }
#endif
}

#if GTE_ENABLE_PROJECT_PANEL

// Human-readable label for AssetType - the *.gta header's own record of
// what kind of payload it wraps (see AssetTypes.h). Purely a display
// helper; never round-tripped back into a numeric value anywhere.
const char* AssetTypeLabel(AssetType type)
{
    switch (type) {
    case AssetType::Unknown: return "Unknown";
    case AssetType::Texture: return "Texture";
    case AssetType::Mesh: return "Mesh";
    case AssetType::Material: return "Material";
    case AssetType::Shader: return "Shader";
    case AssetType::Audio: return "Audio";
    case AssetType::Scene: return "Scene";
    case AssetType::Text: return "Text";
    case AssetType::Font: return "Font";
    case AssetType::Animation: return "Animation";
    case AssetType::Prefab: return "Prefab";
    case AssetType::Other: return "Other";
    default: return "Unknown";
    }
}

// Comma-joined label for whichever AssetFlags bits are set on a *.gta
// header (see AssetTypes.h) - "None" if none are set, matching
// AssetTypeLabel()'s "always produce something displayable" convention.
std::string AssetFlagsLabel(AssetFlags flags)
{
    std::string result;
    if (HasFlag(flags, AssetFlags::Compressed)) {
        result += "Compressed";
    }
    if (HasFlag(flags, AssetFlags::Encrypted)) {
        if (!result.empty()) {
            result += ", ";
        }
        result += "Encrypted";
    }
    return result.empty() ? "None" : result;
}

// Shows the metadata actually recorded INSIDE the *.gta file itself
// (GtaHeader's fields, plus the metadata/payload byte ranges that follow
// it - see GtaFile.h) - as opposed to BuildPlainFileMetadata() below, which
// is only ever plain OS filesystem info (name/size/last-write-time) that
// knows nothing about the asset FORMAT. This is what actually answers "what
// is this asset" (its stable Guid, its declared AssetType, the exact byte
// size of the KTX2 payload libktx produced) rather than just "what does the
// OS say about this file", the same distinction Unity draws between an
// asset's own Import Settings and its raw file properties.
//
// `preview` is only used for its decoded pixel width/height (already paid
// for by the caller's AssetPreviewTexture::Resolve() call, so this never
// re-decodes anything) - passed as std::nullopt when the pixel preview
// itself failed, in which case every OTHER field here (still read straight
// from the 64-byte header, no decode required) is shown anyway.
void BuildGtaTextureMetadata(
    const GtaHeader& header, std::uintmax_t fileSizeBytes, const std::optional<AssetPreviewTexture::Preview>& preview)
{
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "GTA Asset Metadata");
    ImGui::Text("Asset Type: %s", AssetTypeLabel(header.Type()));
    ImGui::Text("Format Version: %llu", static_cast<unsigned long long>(header.version));
    ImGui::Text("GUID: %s", header.Id().ToString().c_str());
    ImGui::Text("Flags: %s", AssetFlagsLabel(header.Flags()).c_str());

    // payloadOffset is always >= sizeof(GtaHeader) for a well-formed file
    // (see GtaHeader's own doc comment) - ReadGtaHeader() already validated
    // the magic, but guard the subtraction anyway rather than trust that.
    const std::uint64_t metadataSize
        = header.payloadOffset >= sizeof(GtaHeader) ? header.payloadOffset - sizeof(GtaHeader) : 0;
    const std::uint64_t payloadSize = fileSizeBytes >= header.payloadOffset ? fileSizeBytes - header.payloadOffset : 0;

    ImGui::Separator();
    if (preview.has_value()) {
        ImGui::Text("Dimensions: %d x %d px", preview->width, preview->height);
    }
    // Every *.gta AssetType::Texture payload today is the exact same
    // container EncodeImageBytesToKtx2() (Ktx2Encoder.h) produces - a
    // single-mip, single-layer, single-face, uncompressed
    // VK_FORMAT_R8G8B8A8_UNORM KTX2 - so this label is a true fact read
    // from how the format is actually produced/decoded (Ktx2Decoder.cpp
    // rejects anything else), not a guess. Update this the moment real
    // block-compression/supercompression lands (see TODO.md).
    ImGui::Text("Texture Format: RGBA8, Uncompressed (KTX2)");
    ImGui::Text("Payload Size (KTX2): %s", FormatBytes(payloadSize).c_str());
    if (metadataSize > 0) {
        ImGui::Text("Metadata Size: %s", FormatBytes(metadataSize).c_str());
    }
    ImGui::Text("On-disk Size (.gta): %s", FormatBytes(fileSizeBytes).c_str());
}

// The Mesh-asset equivalent of BuildGtaTextureMetadata() above - shown when
// the selected *.gta wraps AssetType::Mesh (the result of importing a .pmx
// model - see src/Assets/AssetImporter.h). `preview` (AssetPreviewMesh's own
// result) supplies the vertex/triangle counts; std::nullopt when the live
// 3D preview itself failed to render (a corrupt/truncated payload despite a
// valid header), in which case every OTHER field here is still shown.
void BuildGtaMeshMetadata(
    const GtaHeader& header, std::uintmax_t fileSizeBytes, const std::optional<AssetPreviewMesh::Preview>& preview)
{
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "GTA Asset Metadata");
    ImGui::Text("Asset Type: %s", AssetTypeLabel(header.Type()));
    ImGui::Text("Format Version: %llu", static_cast<unsigned long long>(header.version));
    ImGui::Text("GUID: %s", header.Id().ToString().c_str());
    ImGui::Text("Flags: %s", AssetFlagsLabel(header.Flags()).c_str());

    const std::uint64_t metadataSize
        = header.payloadOffset >= sizeof(GtaHeader) ? header.payloadOffset - sizeof(GtaHeader) : 0;
    const std::uint64_t payloadSize = fileSizeBytes >= header.payloadOffset ? fileSizeBytes - header.payloadOffset : 0;

    ImGui::Separator();
    if (preview.has_value()) {
        ImGui::Text("Vertices: %llu", static_cast<unsigned long long>(preview->vertexCount));
        ImGui::Text("Triangles: %llu", static_cast<unsigned long long>(preview->triangleCount));
    }
    // Every *.gta AssetType::Mesh payload today is the exact same layout
    // MeshFile.h's EncodeMeshDataToBytes() produces (see that file) - plain
    // positions/normals/UVs/indices, no materials/bones/morphs yet.
    ImGui::Text("Mesh Format: Positions + Normals + UVs + Indices (GTEMESH1)");
    ImGui::Text("Payload Size: %s", FormatBytes(payloadSize).c_str());
    if (metadataSize > 0) {
        ImGui::Text("Metadata Size: %s", FormatBytes(metadataSize).c_str());
    }
    ImGui::Text("On-disk Size (.gta): %s", FormatBytes(fileSizeBytes).c_str());
}

// The Animation-asset equivalent of BuildGtaTextureMetadata()/
// BuildGtaMeshMetadata() above - shown when the selected *.gta wraps
// AssetType::Animation (the result of importing a .vmd motion file - see
// src/Assets/VmdLoader.h/AssetImporter.h). Unlike the Texture/Mesh cases,
// there is no live GPU preview for a motion (a flat keyframe list has
// nothing to rasterize/render), so this is always the FULL story for an
// Animation asset - no separate "viewer" pane ever exists alongside it (see
// BuildAssetInspector() below, which never puts an Animation selection
// through the preview/splitter layout at all). `motion` is decoded straight
// from the *.gta's own PAYLOAD bytes (MotionFile.h's
// DecodeMotionDataFromBytes()) by the caller - std::nullopt when that
// payload is corrupt/truncated despite a valid *.gta header, in which case
// every OTHER field here (still read straight from the 64-byte header, no
// decode required) is shown anyway.
void BuildGtaAnimationMetadata(const GtaHeader& header, std::uintmax_t fileSizeBytes, const std::optional<MotionData>& motion)
{
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 1.0f, 1.0f), "GTA Asset Metadata");
    ImGui::Text("Asset Type: %s", AssetTypeLabel(header.Type()));
    ImGui::Text("Format Version: %llu", static_cast<unsigned long long>(header.version));
    ImGui::Text("GUID: %s", header.Id().ToString().c_str());
    ImGui::Text("Flags: %s", AssetFlagsLabel(header.Flags()).c_str());

    const std::uint64_t metadataSize
        = header.payloadOffset >= sizeof(GtaHeader) ? header.payloadOffset - sizeof(GtaHeader) : 0;
    const std::uint64_t payloadSize = fileSizeBytes >= header.payloadOffset ? fileSizeBytes - header.payloadOffset : 0;

    ImGui::Separator();
    if (motion.has_value()) {
        if (!motion->modelName.empty()) {
            ImGui::Text("Target Model Name: %s", motion->modelName.c_str());
        }

        // Frame range across every track combined (bone/morph/camera/
        // light/shadow/IK - whichever ones this particular .vmd actually
        // populated) - a quick "how long is this motion" hint without
        // needing a full playback/scrubbing UI (see TODO.md: no
        // interpolation evaluation/keyframe playback exists anywhere in
        // this engine yet).
        bool hasAnyFrame = false;
        std::uint32_t minFrame = 0;
        std::uint32_t maxFrame = 0;
        auto scanFrames = [&](const auto& list) {
            for (const auto& kf : list) {
                if (!hasAnyFrame) {
                    minFrame = kf.frame;
                    maxFrame = kf.frame;
                    hasAnyFrame = true;
                } else {
                    minFrame = std::min(minFrame, kf.frame);
                    maxFrame = std::max(maxFrame, kf.frame);
                }
            }
        };
        scanFrames(motion->boneKeyframes);
        scanFrames(motion->morphKeyframes);
        scanFrames(motion->cameraKeyframes);
        scanFrames(motion->lightKeyframes);
        scanFrames(motion->shadowKeyframes);
        scanFrames(motion->ikKeyframes);
        if (hasAnyFrame) {
            ImGui::Text("Frame Range: %u - %u (VMD's fixed 30fps grid)", minFrame, maxFrame);
        }

        // Distinct bone names this motion actually drives - matched by
        // NAME against a target model's own SkeletonData::bones at
        // playback time (see MotionData.h's own doc comment); nothing here
        // resolves that yet, but the plain name list is still useful to
        // eyeball which rig a motion expects.
        std::vector<std::string> uniqueBoneNames;
        uniqueBoneNames.reserve(motion->boneKeyframes.size());
        for (const auto& kf : motion->boneKeyframes) {
            uniqueBoneNames.push_back(kf.boneName);
        }
        std::sort(uniqueBoneNames.begin(), uniqueBoneNames.end());
        uniqueBoneNames.erase(std::unique(uniqueBoneNames.begin(), uniqueBoneNames.end()), uniqueBoneNames.end());

        ImGui::Text("Bone Keyframes: %llu (%llu unique bones)",
            static_cast<unsigned long long>(motion->boneKeyframes.size()),
            static_cast<unsigned long long>(uniqueBoneNames.size()));
        ImGui::Text("Morph Keyframes: %llu", static_cast<unsigned long long>(motion->morphKeyframes.size()));
        ImGui::Text("Camera Keyframes: %llu", static_cast<unsigned long long>(motion->cameraKeyframes.size()));
        ImGui::Text("Light Keyframes: %llu", static_cast<unsigned long long>(motion->lightKeyframes.size()));
        ImGui::Text("Shadow Keyframes: %llu", static_cast<unsigned long long>(motion->shadowKeyframes.size()));
        ImGui::Text("IK Keyframes: %llu", static_cast<unsigned long long>(motion->ikKeyframes.size()));

        // A small, scrollable, collapsible detail section listing every
        // distinct bone name this motion drives - same "CollapsingHeader/
        // TreeNode for optional detail" shape as BuildEntityInspector()'s
        // own component sections above. Collapsed by default so a motion
        // with hundreds of bones doesn't dominate the metadata list.
        if (!uniqueBoneNames.empty() && ImGui::TreeNode("Bone Names")) {
            ImGui::BeginChild(
                "InspectorAnimationBoneNames", ImVec2(0, 150.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& name : uniqueBoneNames) {
                ImGui::TextUnformatted(name.c_str());
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }

        // Same idea for morph names, when present (a facial/expression
        // motion rather than a body motion).
        std::vector<std::string> uniqueMorphNames;
        uniqueMorphNames.reserve(motion->morphKeyframes.size());
        for (const auto& kf : motion->morphKeyframes) {
            uniqueMorphNames.push_back(kf.morphName);
        }
        std::sort(uniqueMorphNames.begin(), uniqueMorphNames.end());
        uniqueMorphNames.erase(std::unique(uniqueMorphNames.begin(), uniqueMorphNames.end()), uniqueMorphNames.end());
        if (!uniqueMorphNames.empty() && ImGui::TreeNode("Morph Names")) {
            ImGui::BeginChild(
                "InspectorAnimationMorphNames", ImVec2(0, 100.0f), true, ImGuiWindowFlags_HorizontalScrollbar);
            for (const auto& name : uniqueMorphNames) {
                ImGui::TextUnformatted(name.c_str());
            }
            ImGui::EndChild();
            ImGui::TreePop();
        }

        ImGui::Separator();
    }
    // Every *.gta AssetType::Animation payload today is the exact same
    // layout MotionFile.h's EncodeMotionDataToBytes() produces (see that
    // file) - model name + bone/morph/camera/light/shadow/IK keyframe
    // tracks, no metadata section used (unlike AssetType::Mesh).
    ImGui::Text("Motion Format: Bone/Morph/Camera/Light/Shadow/IK Keyframes (GTEMOTN1)");
    ImGui::Text("Payload Size: %s", FormatBytes(payloadSize).c_str());
    if (metadataSize > 0) {
        ImGui::Text("Metadata Size: %s", FormatBytes(metadataSize).c_str());
    }
    ImGui::Text("On-disk Size (.gta): %s", FormatBytes(fileSizeBytes).c_str());
}

// Plain OS filesystem metadata (AssetInspectorData.h) - name/extension say
// nothing about how a *.gta's own payload is structured, only what the
// filesystem itself reports.
void BuildPlainFileMetadata(const AssetMetadata& metadata, const std::string& absolutePath)
{
    ImGui::Text("Type: %s",
        metadata.isDirectory ? "Folder" : (metadata.extension.empty() ? "File" : metadata.extension.c_str()));
    if (!metadata.isDirectory) {
        ImGui::Text("Size: %s", FormatBytes(metadata.sizeBytes).c_str());
    }
    if (metadata.hasLastWriteTime) {
        ImGui::Text("Last modified: %s", metadata.lastWriteTimeText.c_str());
    }
    ImGui::TextWrapped("Path: %s", absolutePath.c_str());
}

// The Unity-style "texture viewer" strip anchored to the BOTTOM of the
// Inspector's content area: a small title bar (the asset's name) followed
// by the actual image, contain-fit (scaled to fit entirely inside the
// available area, centered, aspect preserved - never cropped/stretched)
// against a dark backdrop, with a small dimensions overlay in the corner -
// the same layout Unity's own texture Inspector uses below its Import
// Settings list. Fills whatever height the caller's BeginChild() already
// reserved for it; see BuildAssetInspector()'s splitter for how that
// height is chosen/resized.
void BuildTextureViewer(const AssetMetadata& metadata, const AssetPreviewTexture::Preview& preview)
{
    ImGui::BeginChild(
        "InspectorPreviewViewer", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::TextUnformatted(metadata.name.c_str());
    ImGui::Separator();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x > 1.0f && avail.y > 1.0f) {
        const float aspect
            = preview.height > 0 ? static_cast<float>(preview.width) / static_cast<float>(preview.height) : 1.0f;

        float displayWidth = avail.x;
        float displayHeight = aspect > 0.0f ? displayWidth / aspect : displayWidth;
        if (displayHeight > avail.y) {
            displayHeight = avail.y;
            displayWidth = displayHeight * aspect;
        }

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        // Dark backdrop across the whole viewer area (same spirit as
        // Unity's own texture preview background), so a non-square/
        // non-viewer-shaped image is clearly letterboxed rather than
        // looking like it's floating on the panel's normal background.
        drawList->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(35, 35, 35, 255));

        const float offsetX = (avail.x - displayWidth) * 0.5f;
        const float offsetY = (avail.y - displayHeight) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + offsetX, origin.y + offsetY));
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(preview.descriptor)),
            ImVec2(displayWidth, displayHeight));

        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%d x %d", preview.width, preview.height);
        drawList->AddText(ImVec2(origin.x + 6.0f, origin.y + avail.y - 20.0f), IM_COL32(255, 255, 255, 255), overlay);
    }

    ImGui::EndChild();
}

// The Mesh-asset equivalent of BuildTextureViewer() above - same contain-fit
// layout against the same dark backdrop, but the "image" is a LIVE,
// continuously-rerendered, auto-rotating 3D view of the mesh (see
// AssetPreviewMesh.h) rather than a static decoded texture - so this is
// called fresh every frame the viewer is visible, unlike the texture
// preview (which just redisplays whatever AssetPreviewTexture cached).
// Shows a vertex/triangle-count overlay in the corner instead of pixel
// dimensions, matching this asset type's own metadata (see
// BuildGtaMeshMetadata() above).
void BuildMeshViewer(const AssetMetadata& metadata, const AssetPreviewMesh::Preview& preview)
{
    ImGui::BeginChild(
        "InspectorPreviewViewer", ImVec2(0, 0), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImGui::TextUnformatted(metadata.name.c_str());
    ImGui::Separator();

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x > 1.0f && avail.y > 1.0f) {
        const float aspect
            = preview.height > 0 ? static_cast<float>(preview.width) / static_cast<float>(preview.height) : 1.0f;

        float displayWidth = avail.x;
        float displayHeight = aspect > 0.0f ? displayWidth / aspect : displayWidth;
        if (displayHeight > avail.y) {
            displayHeight = avail.y;
            displayWidth = displayHeight * aspect;
        }

        const ImVec2 origin = ImGui::GetCursorScreenPos();
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        drawList->AddRectFilled(origin, ImVec2(origin.x + avail.x, origin.y + avail.y), IM_COL32(35, 35, 35, 255));

        const float offsetX = (avail.x - displayWidth) * 0.5f;
        const float offsetY = (avail.y - displayHeight) * 0.5f;
        ImGui::SetCursorScreenPos(ImVec2(origin.x + offsetX, origin.y + offsetY));
        ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(preview.descriptor)),
            ImVec2(displayWidth, displayHeight));

        char overlay[64];
        std::snprintf(overlay, sizeof(overlay), "%llu verts, %llu tris", static_cast<unsigned long long>(preview.vertexCount),
            static_cast<unsigned long long>(preview.triangleCount));
        drawList->AddText(ImVec2(origin.x + 6.0f, origin.y + avail.y - 20.0f), IM_COL32(255, 255, 255, 255), overlay);
    }

    ImGui::EndChild();
}

void BuildAssetInspector(
    EditorContext& ctx, Renderer& renderer, AssetPreviewTexture& assetPreview, AssetPreviewMesh& assetPreviewMesh)
{
    const std::string& absolutePath = ctx.selection.SelectedAssetAbsolutePath();
    const std::string& relativePath = ctx.selection.SelectedAssetRelativePath();

    const AssetMetadata metadata = BuildAssetMetadata(Utf8ToPath(absolutePath));

    ImGui::Text("%s", metadata.name.empty() ? "(Project root)" : metadata.name.c_str());
    ImGui::TextDisabled("%s", relativePath.empty() ? "(root)" : relativePath.c_str());
    ImGui::Separator();

    if (!metadata.exists) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "This item no longer exists on disk.");
        return;
    }

    // Peek this file's *.gta header (cheap - see GtaFile.h's
    // ReadGtaHeader()) whenever the extension is ".gta" at all, regardless
    // of whether it turns out to be a texture/mesh - this is what lets the
    // metadata section below show real GTA-format fields (GUID/AssetType/
    // flags/payload size) for ANY *.gta asset, not only ones that also
    // happen to preview as an image or a mesh.
    const bool isGta = !metadata.isDirectory && metadata.extension == ".gta";
    const std::optional<GtaHeader> gtaHeader = isGta ? ReadGtaHeader(Utf8ToPath(absolutePath)) : std::nullopt;
    const bool isGtaTexture = gtaHeader.has_value() && gtaHeader->Type() == AssetType::Texture;
    const bool isGtaMesh = gtaHeader.has_value() && gtaHeader->Type() == AssetType::Mesh;
    const bool isGtaAnimation = gtaHeader.has_value() && gtaHeader->Type() == AssetType::Animation;

    // Decode the motion data straight out of the *.gta's own PAYLOAD bytes
    // whenever the header confirms AssetType::Animation - there is no GPU
    // preview to gate this behind (unlike the texture/mesh cases below),
    // this is just a plain binary decode (MotionFile.h's
    // DecodeMotionDataFromBytes()), so it always runs up front. std::nullopt
    // if the full *.gta can't be read at all or its payload is corrupt/
    // truncated despite a valid header - BuildGtaAnimationMetadata() still
    // shows every OTHER (header-derived) field in that case.
    std::optional<MotionData> motionData;
    if (isGtaAnimation) {
        if (const std::optional<GtaFileData> gtaFile = ReadGtaFile(Utf8ToPath(absolutePath)); gtaFile.has_value()) {
            motionData = DecodeMotionDataFromBytes(gtaFile->payload);
        }
    }

    // Attempt a live texture preview for a FILE whose extension is either a
    // format AssetPreviewTexture/stb_image can decode directly, OR a *.gta
    // confirmed above to actually wrap AssetType::Texture (the result of
    // AssetImporter::ImportAssetFile() gating a dropped PNG/JPG through the
    // KTX2 import pipeline - see src/Assets/AssetImporter.h). A *.gta
    // wrapping something other than a texture (Mesh/Scene/...) is
    // deliberately NOT treated as "should have previewed but failed" - it
    // just falls through to plain metadata below, exactly like any other
    // non-image extension, with no spurious "failed to load" message.
    const bool attemptTexturePreview = !metadata.isDirectory && (IsSupportedImageExtension(metadata.extension) || isGtaTexture);

    std::optional<AssetPreviewTexture::Preview> preview;
    if (attemptTexturePreview) {
        preview = assetPreview.Resolve(renderer, absolutePath);
    }

    // Attempt a live 3D mesh preview whenever the *.gta header confirms
    // AssetType::Mesh - rendered at (roughly) the Inspector's current
    // remaining content size, well before the exact bottom-viewer height is
    // known (see AssetPreviewMesh.h's own comment: unlike a static texture,
    // this is a genuine render, but the result is still just displayed
    // contain-fit/scaled afterwards by BuildMeshViewer() below, exactly
    // like BuildTextureViewer() already does regardless of a texture's own
    // native resolution - so an approximate render target size here is
    // perfectly fine, no second render needed once the final split layout
    // is known).
    std::optional<AssetPreviewMesh::Preview> meshPreview;
    if (isGtaMesh && !metadata.isDirectory) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        meshPreview = assetPreviewMesh.Render(
            renderer, absolutePath, static_cast<int>(avail.x), static_cast<int>(avail.y));
    }

    if (preview.has_value() || meshPreview.has_value()) {
        // Unity-style split layout: a scrollable metadata list on top, a
        // user-draggable splitter, then the texture/mesh viewer pinned to
        // the BOTTOM of the panel. ctx.inspectorPreviewHeight
        // (EditorContext.h) is the persisted (across frames/selections)
        // pixel height of the bottom viewer, adjusted live below by
        // dragging the splitter - exactly like Unity's own Inspector
        // preview pane. Shared between the texture and mesh preview cases -
        // only one of the two is ever non-null for a given selection.
        constexpr float kSplitterThickness = 6.0f;
        constexpr float kMinPreviewHeight = 100.0f;
        constexpr float kMinMetadataHeight = 80.0f;

        const float totalAvail = ImGui::GetContentRegionAvail().y;
        const float maxPreviewHeight = std::max(kMinPreviewHeight, totalAvail - kSplitterThickness - kMinMetadataHeight);
        ctx.inspectorPreviewHeight = std::clamp(ctx.inspectorPreviewHeight, kMinPreviewHeight, maxPreviewHeight);
        const float metadataHeight = std::max(0.0f, totalAvail - ctx.inspectorPreviewHeight - kSplitterThickness);

        ImGui::BeginChild("InspectorMetadataRegion", ImVec2(0, metadataHeight), false);
        if (isGtaTexture && gtaHeader.has_value()) {
            BuildGtaTextureMetadata(*gtaHeader, metadata.sizeBytes, preview);
            ImGui::Separator();
        } else if (isGtaMesh && gtaHeader.has_value()) {
            BuildGtaMeshMetadata(*gtaHeader, metadata.sizeBytes, meshPreview);
            ImGui::Separator();
        }
        BuildPlainFileMetadata(metadata, absolutePath);
        ImGui::EndChild();

        // The draggable splitter itself - a thin full-width button styled
        // like a scrollbar grip. Dragging it up/down adjusts
        // inspectorPreviewHeight (subtracting the mouse's vertical delta,
        // since the viewer is anchored to the BOTTOM: dragging the
        // splitter UP must grow the viewer, i.e. increase its height).
        ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrab));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrabHovered));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrabActive));
        ImGui::Button("##InspectorPreviewSplitter", ImVec2(-1.0f, kSplitterThickness));
        ImGui::PopStyleColor(3);
        if (ImGui::IsItemActive()) {
            ctx.inspectorPreviewHeight -= ImGui::GetIO().MouseDelta.y;
        }
        if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
            ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeNS);
        }

        if (preview.has_value()) {
            BuildTextureViewer(metadata, *preview);
        } else {
            BuildMeshViewer(metadata, *meshPreview);
        }
        return;
    }

    if (attemptTexturePreview) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to load image preview.");
        ImGui::Separator();
    } else if (isGtaMesh) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to load mesh preview.");
        ImGui::Separator();
    } else if (isGtaAnimation && !motionData.has_value()) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to decode motion data.");
        ImGui::Separator();
    }
    if (isGtaTexture && gtaHeader.has_value()) {
        // The pixel preview failed (corrupt/truncated KTX2 payload, etc.)
        // but the header itself is still valid - show what the header
        // alone can tell us rather than nothing at all.
        BuildGtaTextureMetadata(*gtaHeader, metadata.sizeBytes, std::nullopt);
        ImGui::Separator();
    } else if (isGtaMesh && gtaHeader.has_value()) {
        BuildGtaMeshMetadata(*gtaHeader, metadata.sizeBytes, std::nullopt);
        ImGui::Separator();
    } else if (isGtaAnimation && gtaHeader.has_value()) {
        BuildGtaAnimationMetadata(*gtaHeader, metadata.sizeBytes, motionData);
        ImGui::Separator();
    }
    BuildPlainFileMetadata(metadata, absolutePath);
}
#endif

} // namespace

#if GTE_ENABLE_PROJECT_PANEL
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, Renderer& renderer, AssetPreviewTexture& assetPreview,
    AssetPreviewMesh& assetPreviewMesh, BoneViewerWindow& boneViewer, PhysicsSystem& physicsSystem,
    MeshInstantiationSystem& meshInstantiationSystem, ModelRigCache& rigCache)
#else
void BuildInspectorPanel(Registry& registry, EditorContext& ctx, PhysicsSystem& physicsSystem,
    MeshInstantiationSystem& meshInstantiationSystem)
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
        BuildModelPartInspector(registry, ctx, rigCache, physicsSystem);
        ImGui::End();
        return;
    }
#endif

#if GTE_ENABLE_PROJECT_PANEL
    BuildEntityInspector(registry, ctx, boneViewer, physicsSystem, meshInstantiationSystem);
#else
    BuildEntityInspector(registry, ctx, physicsSystem, meshInstantiationSystem);
#endif

    ImGui::End();
}

} // namespace gte
