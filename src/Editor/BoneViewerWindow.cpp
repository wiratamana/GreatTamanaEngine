#include "BoneViewerWindow.h"

#include "EditorContext.h"
#include "ModelRigCache.h"
#include "ImGuiUniqueId.h"
#include "../Encoding/Utf8Path.h"
#include "RigidBodyWireframe.h"
#include "../Assets/AssetTypes.h" // AssetType
#include "../Assets/GtaFile.h" // ReadGtaFile()
#include "../Assets/MeshFile.h" // DecodeMeshDataFromBytes()
#include "../Assets/RigFile.h" // RigFileData
#include "../ECS/Components/MeshAssetSource.h"
#include "../ECS/Registry.h"
#include "../Game/Physics/PhysicsSystem.h" // PhysicsSystem::GetDynamicChainRigCache()
#include "../Math/Mat4.h"
#include "../Math/Vec4.h"
#include "../Renderer/MeshPreviewRenderer.h"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <iterator>

namespace gte {

namespace {

// Projects a model-space point into [rectMin, rectMax) pixels.
// Returns false (outScreen untouched) for points behind the camera.
bool ProjectToScreen(const Vec3& modelPos, const Mat4& viewProj, ImVec2 rectMin, ImVec2 rectMax, ImVec2& outScreen)
{
    const Vec4 clip = viewProj * Vec4(modelPos, 1.0f);
    if (clip.w <= 0.0001f) {
        return false;
    }
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    const float width = rectMax.x - rectMin.x;
    const float height = rectMax.y - rectMin.y;
    outScreen.x = rectMin.x + (ndcX * 0.5f + 0.5f) * width;
    outScreen.y = rectMin.y + (ndcY * 0.5f + 0.5f) * height;
    return true;
}

std::string ToLower(const std::string& s)
{
    std::string result = s;
    std::transform(result.begin(), result.end(), result.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return result;
}

// One part the viewport overlay projects, labels and hit-tests; all four view modes reduce to this.
struct OverlayPart {
    Vec3 position;
    std::string name;
    // Real ModelPartKind index of this slot; differs from the slot position in Verlet mode (a bone index).
    std::int32_t partIndex = -1;
};

} // namespace

BoneViewerWindow::BoneViewerWindow() = default;

BoneViewerWindow::~BoneViewerWindow()
{
    Reset();
}

void BoneViewerWindow::Open(Entity rootEntity) noexcept
{
    m_open = true;
    m_targetEntity = rootEntity;
}

void BoneViewerWindow::Reset()
{
    m_preview.reset(); // Idles the GPU, then releases the preview's resources.
    m_previewBuildFailed = false;
    m_bones.clear();
    m_rigidBodies.clear();
    m_joints.clear();
    m_boneChildren.clear();
    m_rootBoneIndices.clear();
    m_rigidBodyAdjacency.clear();
    m_flatSelectionAnchorIndex = -1;
    m_cachedPath.clear();
    m_cachedWriteTime = std::filesystem::file_time_type{};
    m_cachedIsValid = false;
}

bool BoneViewerWindow::EnsureDataLoaded(
    Renderer& renderer, const std::string& absoluteGtaPath, EditorContext& ctx, ModelRigCache& rigCache)
{
    std::error_code timeEc;
    const std::filesystem::file_time_type writeTime = std::filesystem::last_write_time(PathFromUtf8(absoluteGtaPath), timeEc);

    if (!absoluteGtaPath.empty() && absoluteGtaPath == m_cachedPath && !timeEc && writeTime == m_cachedWriteTime) {
        return true; // Unchanged since last call - whatever m_cachedIsValid says still holds.
    }

    if (!m_preview && !m_previewBuildFailed) {
        m_preview = MeshPreviewRenderer::TryCreate(renderer);
        m_previewBuildFailed = (m_preview == nullptr);
    }
    if (!m_preview) {
        return false;
    }
    m_preview->SetMesh({}, {}); // Drops the previous model's mesh.
    m_bones.clear();
    m_rigidBodies.clear();
    m_joints.clear();

    // Reloading invalidates any model-part selection on this window's entity; other entities' selections stay.
    ctx.selection.ClearModelPartIfEntity(m_targetEntity);
    m_flatSelectionAnchorIndex = -1; // New data may change the rigid-body/joint count and order.

    m_cachedPath = absoluteGtaPath;
    m_cachedIsValid = false;
    if (!timeEc) {
        m_cachedWriteTime = writeTime;
    }

    const std::optional<GtaFileData> gta = ReadGtaFile(PathFromUtf8(absoluteGtaPath));
    if (!gta.has_value() || gta->header.Type() != AssetType::Mesh) {
        return true; // Not a (valid) Mesh *.gta - m_cachedIsValid stays false.
    }

    const std::optional<MeshData> mesh = DecodeMeshDataFromBytes(gta->payload);
    if (!mesh.has_value() || mesh->positions.empty() || mesh->indices.size() < 3) {
        return true;
    }

    // Rig data comes from the shared ModelRigCache. A riggless mesh resolves to empty rig data, not a failure.
    if (const RigFileData* rig = rigCache.GetOrLoad(absoluteGtaPath)) {
        m_bones.reserve(rig->skeleton.bones.size());
        for (const Bone& bone : rig->skeleton.bones) {
            m_bones.push_back(BoneEntry{ bone.name, bone.position, bone.parentBoneIndex });
        }

        m_rigidBodies.reserve(rig->physics.rigidBodies.size());
        for (const RigidBody& body : rig->physics.rigidBodies) {
            m_rigidBodies.push_back(RigidBodyEntry{ body.name, body.translate, body.rotateRadians, body.shape,
                body.shapeSize, body.boneIndex, body.group, body.motionType, body.collisionGroupMask });
        }

        m_joints.reserve(rig->physics.joints.size());
        for (const Joint& joint : rig->physics.joints) {
            m_joints.push_back(JointEntry{ joint.name, joint.translate, joint.rigidBodyAIndex, joint.rigidBodyBIndex });
        }
    }
    RebuildBoneHierarchyIndex(); // Still only walks m_bones - RigidBody/Joint have no tree to build.
    RebuildRigidBodyAdjacencyIndex(); // Derives the rigid-body-centric graph "Select All (Branch)" walks.

    const MeshPreviewData previewData = BuildMeshPreviewData(mesh->positions, mesh->normals);
    m_boundsCenter = previewData.center;
    m_boundsRadius = previewData.radius;

    if (!m_preview->SetMesh(previewData.vertices, mesh->indices)) {
        return true; // m_cachedIsValid stays false.
    }

    m_vertexCount = static_cast<std::uint32_t>(previewData.vertices.size());
    m_indexCount = static_cast<std::uint32_t>(mesh->indices.size());
    m_cachedIsValid = true;
    m_needsFraming = true; // A newly (re)loaded model - reframe the camera next Build() call.
    return true;
}

void BoneViewerWindow::FrameCameraToBounds() noexcept
{
    m_camTarget = m_boundsCenter;
    m_camYawDeg = 20.0f;
    m_camPitchDeg = 12.0f;
    const float fovYRadians = DegToRad(45.0f);
    m_camDistance = std::max(0.01f, (m_boundsRadius / std::tan(fovYRadians * 0.5f)) * 1.6f);
}

Vec3 BoneViewerWindow::ComputeEyePosition() const noexcept
{
    const float yawRad = DegToRad(m_camYawDeg);
    const float pitchRad = DegToRad(m_camPitchDeg);
    const Vec3 dirFromTargetToEye(
        std::sin(yawRad) * std::cos(pitchRad), std::sin(pitchRad), -std::cos(yawRad) * std::cos(pitchRad));
    return m_camTarget + dirFromTargetToEye * m_camDistance;
}

void BoneViewerWindow::RebuildBoneHierarchyIndex()
{
    m_boneChildren.assign(m_bones.size(), {});
    m_rootBoneIndices.clear();
    for (std::size_t i = 0; i < m_bones.size(); ++i) {
        const std::int32_t parent = m_bones[i].parentIndex;
        if (parent >= 0 && static_cast<std::size_t>(parent) < m_bones.size()) {
            m_boneChildren[static_cast<std::size_t>(parent)].push_back(static_cast<std::int32_t>(i));
        } else {
            m_rootBoneIndices.push_back(static_cast<std::int32_t>(i));
        }
    }
}

void BoneViewerWindow::RebuildRigidBodyAdjacencyIndex()
{
    std::vector<RigidBodyJointEdge> edges;
    edges.reserve(m_joints.size());
    for (const JointEntry& joint : m_joints) {
        edges.push_back(RigidBodyJointEdge{ joint.rigidBodyAIndex, joint.rigidBodyBIndex });
    }
    m_rigidBodyAdjacency = BuildRigidBodyAdjacency(edges, m_rigidBodies.size());
}

bool BoneViewerWindow::BoneMatchesFilterRecursive(std::int32_t boneIndex, const std::string& lowerFilter, int depth) const
{
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()
        || depth > static_cast<int>(m_bones.size())) {
        return false;
    }
    if (ToLower(m_bones[static_cast<std::size_t>(boneIndex)].name).find(lowerFilter) != std::string::npos) {
        return true;
    }
    for (const std::int32_t child : m_boneChildren[static_cast<std::size_t>(boneIndex)]) {
        if (BoneMatchesFilterRecursive(child, lowerFilter, depth + 1)) {
            return true;
        }
    }
    return false;
}

void BoneViewerWindow::RenderBoneTreeNode(std::int32_t boneIndex, const std::string& lowerFilter, int depth, EditorContext& ctx)
{
    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()
        || depth > static_cast<int>(m_bones.size())) {
        return;
    }

    // Search filter: hide a bone with no matching name and no matching descendant.
    if (!lowerFilter.empty() && !BoneMatchesFilterRecursive(boneIndex, lowerFilter, 0)) {
        return;
    }

    const BoneEntry& bone = m_bones[static_cast<std::size_t>(boneIndex)];
    const std::vector<std::int32_t>& children = m_boneChildren[static_cast<std::size_t>(boneIndex)];

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick
        | ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_DefaultOpen;
    if (children.empty()) {
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    }
    if (ctx.selection.IsModelPartSelected(m_targetEntity, ModelPartKind::Bone, boneIndex)) {
        flags |= ImGuiTreeNodeFlags_Selected;
    }

    const std::string label = bone.name.empty() ? ("Bone " + std::to_string(boneIndex)) : bone.name;

    ScopedUniqueId idScope(boneIndex, "BoneViewerWindow::RenderBoneTreeNode", bone.name.c_str());
    const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);

    if (ImGui::IsItemClicked(ImGuiMouseButton_Left)) {
        // Ctrl/Shift-click toggles this bone; no range select, as bone indices have no linear order in a tree.
        // A plain click selects only this bone.
        const ImGuiIO& io = ImGui::GetIO();
        if (io.KeyCtrl || io.KeyShift) {
            ctx.selection.ToggleModelPartInSelection(m_targetEntity, ModelPartKind::Bone, boneIndex);
        } else {
            ctx.selection.SelectModelPart(m_targetEntity, ModelPartKind::Bone, boneIndex);
        }
        // Double-click re-centers the orbit camera on this bone.
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_camTarget = bone.position;
        }
    }

    if (opened && !children.empty()) {
        for (const std::int32_t child : children) {
            RenderBoneTreeNode(child, lowerFilter, depth + 1, ctx);
        }
        ImGui::TreePop();
    }
}

void BoneViewerWindow::RenderFlatPartRow(ModelPartKind kind, std::int32_t index, const std::string& name,
    const Vec3& position, const std::string& lowerFilter, EditorContext& ctx)
{
    if (!lowerFilter.empty() && ToLower(name).find(lowerFilter) == std::string::npos) {
        return; // Same "search prunes the list" convention as the bone tree.
    }

    const bool isSelected = ctx.selection.IsModelPartSelected(m_targetEntity, kind, index);
    const std::string label = name.empty() ? ("Part " + std::to_string(index)) : name;

    ScopedUniqueId idScope(index, "BoneViewerWindow::RenderFlatPartRow", name.c_str());
    if (ImGui::Selectable(label.c_str(), isSelected)) {
        // Ctrl toggles this row; Shift selects the range from the anchor row (replacing the selection);
        // a plain click selects only this row and moves the anchor.
        const ImGuiIO& io = ImGui::GetIO();
        // Verlet part indices are bone indices, not a dense list, so a raw range would include non-joint bones.
        // Verlet rows toggle only; Rigid Body and Joint rows support range select.
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
        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
            m_camTarget = position;
        }
    }
}

void BoneViewerWindow::RenderVerletChainNode(std::int32_t chainIndex, const DynamicChainDefinition& chain,
    const std::string& lowerFilter, EditorContext& ctx)
{
    // Search prunes the tree: hide the whole chain header when no joint name matches the filter.
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

    ScopedUniqueId idScope(chainIndex, "BoneViewerWindow::RenderVerletChainNode");
    if (ImGui::TreeNodeEx(header, ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
        for (std::size_t j = 0; j < chain.jointBoneIndices.size(); ++j) {
            const std::int32_t boneIndex = chain.jointBoneIndices[j];
            if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()) {
                continue; // Defensive - should never happen for a well-formed DynamicChainDefinition.
            }
            const BoneEntry& bone = m_bones[static_cast<std::size_t>(boneIndex)];
            // Show each joint's resolved tree parent next to its name.
            const std::int32_t parentJoint = (j < chain.parentJointIndex.size()) ? chain.parentJointIndex[j] : -1;
            std::string label = bone.name;
            if (parentJoint < 0) {
                label += " (root child)";
            } else if (static_cast<std::size_t>(parentJoint) < chain.jointBoneIndices.size()) {
                const std::int32_t parentBoneIndex = chain.jointBoneIndices[static_cast<std::size_t>(parentJoint)];
                if (parentBoneIndex >= 0 && static_cast<std::size_t>(parentBoneIndex) < m_bones.size()) {
                    label += " (parent: " + m_bones[static_cast<std::size_t>(parentBoneIndex)].name + ")";
                }
            }
            RenderFlatPartRow(ModelPartKind::Verlet, boneIndex, label, bone.position, lowerFilter, ctx);
        }
        // Collision is a per-chain opt-in against the model's auto-detected Static rigid bodies.
        if (chain.collisionEnabled) {
            ImGui::TextDisabled("Collision: enabled (collides against this model's auto-detected colliders, "
                "subject to PMX collision-group/layer rules)");
        }
        ImGui::TreePop();
    }
}

void BoneViewerWindow::BuildPartListPane(
    const std::string& lowerFilter, EditorContext& ctx, const DynamicChainRigCache::ModelEntry* verletModel)
{
    switch (m_viewMode) {
    case ModelPartKind::Bone:
        if (m_bones.empty()) {
            ImGui::TextDisabled("(no bones)");
            return;
        }
        for (const std::int32_t root : m_rootBoneIndices) {
            RenderBoneTreeNode(root, lowerFilter, 0, ctx);
        }
        return;
    case ModelPartKind::RigidBody:
        if (m_rigidBodies.empty()) {
            ImGui::TextDisabled("(no rigid bodies)");
            return;
        }
        for (std::size_t i = 0; i < m_rigidBodies.size(); ++i) {
            RenderFlatPartRow(ModelPartKind::RigidBody, static_cast<std::int32_t>(i), m_rigidBodies[i].name,
                m_rigidBodies[i].translate, lowerFilter, ctx);
        }
        return;
    case ModelPartKind::Joint:
        if (m_joints.empty()) {
            ImGui::TextDisabled("(no joints)");
            return;
        }
        for (std::size_t i = 0; i < m_joints.size(); ++i) {
            const JointEntry& joint = m_joints[i];
            const std::string label = joint.name.empty() ? ("Joint " + std::to_string(i)) : joint.name;
            RenderFlatPartRow(ModelPartKind::Joint, static_cast<std::int32_t>(i), label, joint.translate, lowerFilter, ctx);
        }
        return;
    case ModelPartKind::Verlet: {
        if (verletModel == nullptr) {
            ImGui::TextDisabled("(no dynamic bone chains)");
            return;
        }
        const DynamicChainDetectionDiagnostics& diagnostics = verletModel->diagnostics;
        if (verletModel->chains.empty() && diagnostics.orphanedDynamicBoneIndices.empty()
            && diagnostics.crossChainJointsDropped.empty() && diagnostics.duplicateBoneRigidBodyAssignmentsDropped.empty()) {
            ImGui::TextDisabled("(no dynamic bone chains)");
            return;
        }
        for (std::size_t i = 0; i < verletModel->chains.size(); ++i) {
            RenderVerletChainNode(static_cast<std::int32_t>(i), verletModel->chains[i], lowerFilter, ctx);
        }
        // Orphaned (non-simulated) rigid-body bones: informational, non-selectable, red-tinted.
        if (!diagnostics.orphanedDynamicBoneIndices.empty()) {
            if (ImGui::TreeNodeEx("Orphaned (Not Simulated)", ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth)) {
                for (const std::int32_t boneIndex : diagnostics.orphanedDynamicBoneIndices) {
                    const std::string label = (boneIndex >= 0 && static_cast<std::size_t>(boneIndex) < m_bones.size())
                        ? m_bones[static_cast<std::size_t>(boneIndex)].name
                        : std::string("(unknown)");
                    ImGui::TextColored(ImVec4(0.95f, 0.35f, 0.35f, 1.0f), "%s", label.c_str());
                }
                ImGui::TreePop();
            }
        }
        // Remaining diagnostic lists; skipped when empty.
        if (!diagnostics.crossChainJointsDropped.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Cross-Chain Joints Dropped (%zu)",
                diagnostics.crossChainJointsDropped.size());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "These PMX Joints connect two bones that ended up in two DIFFERENT detected chains "
                    "(e.g. two independently-anchored skirt panels laced together). Not simulated as a "
                    "constraint - cross-chain bracing is out of scope for this engine today.");
            }
            for (const std::int32_t jointIndex : diagnostics.crossChainJointsDropped) {
                const std::string label = (jointIndex >= 0 && static_cast<std::size_t>(jointIndex) < m_joints.size())
                    ? m_joints[static_cast<std::size_t>(jointIndex)].name
                    : std::string("(unknown)");
                ImGui::TextDisabled("  Joint %d: %s", jointIndex, label.c_str());
            }
        }
        if (!diagnostics.duplicateBoneRigidBodyAssignmentsDropped.empty()) {
            ImGui::TextColored(ImVec4(0.95f, 0.75f, 0.35f, 1.0f), "Duplicate Rigid-Body-Per-Bone Assignments Dropped (%zu)",
                diagnostics.duplicateBoneRigidBodyAssignmentsDropped.size());
            if (ImGui::IsItemHovered()) {
                ImGui::SetTooltip(
                    "Two or more rigid bodies are attached to the same bone. Only the lowest-index one is "
                    "used for chain-detection purposes (as either an anchor or a member); the rest are "
                    "listed here so an unexpected anchor/member choice can be traced back to its cause.");
            }
            for (const std::int32_t rigidBodyIndex : diagnostics.duplicateBoneRigidBodyAssignmentsDropped) {
                const std::string label = (rigidBodyIndex >= 0 && static_cast<std::size_t>(rigidBodyIndex) < m_rigidBodies.size())
                    ? m_rigidBodies[static_cast<std::size_t>(rigidBodyIndex)].name
                    : std::string("(unknown)");
                ImGui::TextDisabled("  Rigid Body %d: %s", rigidBodyIndex, label.c_str());
            }
        }
        return;
    }
    }
}

void BoneViewerWindow::Build(
    Registry& registry, Renderer& renderer, EditorContext& ctx, ModelRigCache& rigCache, PhysicsSystem& physicsSystem)
{
    if (!m_open) {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(900.0f, 650.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin("Bone Viewer", &m_open)) {
        ImGui::End();
        return;
    }

    if (!registry.IsAlive(m_targetEntity)) {
        ImGui::TextDisabled("The selected entity no longer exists.");
        ImGui::End();
        return;
    }

    const MeshAssetSource* source = registry.TryGetComponent<MeshAssetSource>(m_targetEntity);
    if (source == nullptr || source->gtaPath.empty()) {
        ImGui::TextDisabled("This entity has no associated mesh asset (MeshAssetSource) to inspect.");
        ImGui::End();
        return;
    }

    if (!EnsureDataLoaded(renderer, source->gtaPath, ctx, rigCache) || !m_cachedIsValid) {
        ImGui::TextColored(ImVec4(1.0f, 0.55f, 0.2f, 1.0f), "Failed to load mesh/skeleton data for:");
        ImGui::TextWrapped("%s", source->gtaPath.c_str());
        ImGui::End();
        return;
    }

    // Fetched once per Build() and passed down. May be nullptr (no entry registered for this path yet).
    const DynamicChainRigCache::ModelEntry* verletModel = physicsSystem.GetDynamicChainRigCache().TryGet(source->gtaPath);

    if (m_needsFraming) {
        FrameCameraToBounds();
        m_needsFraming = false;
    }

    // --- Toolbar -----------------------------------------------------------
    ImGui::PushItemWidth(240.0f);
    ImGui::InputTextWithHint("##BoneViewerSearch", "Search by name...", m_searchBuffer, sizeof(m_searchBuffer));
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Reset View")) {
        FrameCameraToBounds();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Show All Names", &m_showAllNames);
    ImGui::SameLine();
    ImGui::PushItemWidth(140.0f);
    static const char* kViewModeLabels[] = { "Bones", "Rigid Bodies", "Joints", "Verlet" };
    int viewModeIndex = static_cast<int>(m_viewMode);
    if (ImGui::Combo("View", &viewModeIndex, kViewModeLabels, static_cast<int>(std::size(kViewModeLabels)))) {
        m_viewMode = static_cast<ModelPartKind>(viewModeIndex);
        m_flatSelectionAnchorIndex = -1; // Different index space (Rigid Body vs. Joint vs. Bone) - see this field's own doc comment.
    }
    ImGui::PopItemWidth();
    ImGui::SameLine();

    // Verlet mode counts chains and joints, from the verletModel fetched above.
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

    if (m_viewMode == ModelPartKind::Bone && m_bones.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
            "This model has no bone/skeleton data (a boneless mesh, or one imported before rig extraction existed).");
    } else if (m_viewMode == ModelPartKind::RigidBody && m_rigidBodies.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "This model has no rigid-body physics data.");
    } else if (m_viewMode == ModelPartKind::Joint && m_joints.empty()) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f), "This model has no joint physics data.");
    } else if (m_viewMode == ModelPartKind::Verlet && verletJointCount == 0) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.3f, 1.0f),
            "This model has no detected dynamic (Verlet) bone-chain physics data.");
    }
    ImGui::Separator();

    // "Select All (Group)"/"Select All (Branch)": rigid-body mode only, seeded from the one selected rigid body.
    // Both buttons are disabled when there is no single seed.
    if (m_viewMode == ModelPartKind::RigidBody && !m_rigidBodies.empty()) {
        // The seed must be exactly one selected rigid body; with several selected, reseeding is ambiguous.
        const bool hasSeed = ctx.selection.Kind() == InspectorSelectionKind::ModelPart
            && ctx.selection.SelectedModelPartEntity() == m_targetEntity
            && ctx.selection.SelectedModelPartKind() == ModelPartKind::RigidBody
            && ctx.selection.SelectedModelPartIndices().size() == 1
            && ctx.selection.SelectedModelPartIndex() >= 0
            && static_cast<std::size_t>(ctx.selection.SelectedModelPartIndex()) < m_rigidBodies.size();

        const std::int32_t seed = hasSeed ? static_cast<std::int32_t>(ctx.selection.SelectedModelPartIndex()) : -1;
        // Only meaningful when hasSeed is true.
        const bool seedIsBranch = hasSeed && m_rigidBodyAdjacency[static_cast<std::size_t>(seed)].size() >= 3;

        ImGui::BeginDisabled(!hasSeed);
        if (ImGui::Button("Select All (Group)")) {
            std::vector<std::uint8_t> groups;
            groups.reserve(m_rigidBodies.size());
            for (const RigidBodyEntry& body : m_rigidBodies) {
                groups.push_back(body.group);
            }
            const std::vector<std::int32_t> matches = SelectRigidBodiesByGroup(groups, seed);
            ctx.selection.SelectModelParts(
                m_targetEntity, ModelPartKind::RigidBody, std::vector<int>(matches.begin(), matches.end()));
        }
        // Hover preview of the match count, computed by the same function the click uses.
        if (hasSeed && ImGui::IsItemHovered()) {
            std::vector<std::uint8_t> groups;
            groups.reserve(m_rigidBodies.size());
            for (const RigidBodyEntry& body : m_rigidBodies) {
                groups.push_back(body.group);
            }
            const std::size_t count = SelectRigidBodiesByGroup(groups, seed).size();
            const RigidBodyEntry& seedBody = m_rigidBodies[static_cast<std::size_t>(seed)];
            ImGui::SetTooltip("Selects %zu rigid bod%s sharing collision group %u with \"%s\".", count,
                count == 1 ? "y" : "ies", static_cast<unsigned>(seedBody.group), seedBody.name.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Select All (Branch)")) {
            const std::vector<std::int32_t> matches = SelectRigidBodyBranch(m_rigidBodyAdjacency, seed);
            ctx.selection.SelectModelParts(
                m_targetEntity, ModelPartKind::RigidBody, std::vector<int>(matches.begin(), matches.end()));
        }
        if (hasSeed && ImGui::IsItemHovered()) {
            const RigidBodyEntry& seedBody = m_rigidBodies[static_cast<std::size_t>(seed)];
            if (seedIsBranch) {
                // A junction seed selects only itself; say so up front.
                ImGui::SetTooltip(
                    "\"%s\" is itself a branch/junction (connected to %zu other rigid bodies) -\n"
                    "there is no single unambiguous chain to select; only itself will be selected.",
                    seedBody.name.c_str(), m_rigidBodyAdjacency[static_cast<std::size_t>(seed)].size());
            } else {
                const std::size_t count = SelectRigidBodyBranch(m_rigidBodyAdjacency, seed).size();
                ImGui::SetTooltip("Selects %zu rigid bod%s in \"%s\"'s own uninterrupted joint chain.", count,
                    count == 1 ? "y" : "ies", seedBody.name.c_str());
            }
        }
        ImGui::EndDisabled();
        if (!hasSeed) {
            ImGui::SameLine();
            if (ctx.selection.SelectedModelPartIndices().size() > 1) {
                ImGui::TextDisabled("(select exactly one rigid body first - %zu are currently selected)",
                    ctx.selection.SelectedModelPartIndices().size());
            } else {
                ImGui::TextDisabled("(select a rigid body first)");
            }
        }
    }

    // "Select All (Chain)": seeded from the one selected Verlet joint, selects every joint of its chain.
    if (m_viewMode == ModelPartKind::Verlet) {
        const bool hasSeed = ctx.selection.Kind() == InspectorSelectionKind::ModelPart
            && ctx.selection.SelectedModelPartEntity() == m_targetEntity
            && ctx.selection.SelectedModelPartKind() == ModelPartKind::Verlet
            && ctx.selection.SelectedModelPartIndices().size() == 1;

        // Only meaningful when hasSeed is true.
        const DynamicChainJointLocation seedLocation = (hasSeed && verletModel != nullptr)
            ? FindDynamicChainJointByBoneIndex(verletModel->chains, ctx.selection.SelectedModelPartIndex())
            : DynamicChainJointLocation{};

        ImGui::BeginDisabled(!hasSeed || !seedLocation.IsValid());
        if (ImGui::Button("Select All (Chain)")) {
            const DynamicChainDefinition& chain = verletModel->chains[static_cast<std::size_t>(seedLocation.chainIndex)];
            std::vector<int> indices(chain.jointBoneIndices.begin(), chain.jointBoneIndices.end());
            ctx.selection.SelectModelParts(m_targetEntity, ModelPartKind::Verlet, std::move(indices));
        }
        if (hasSeed && seedLocation.IsValid() && ImGui::IsItemHovered()) {
            const DynamicChainDefinition& chain = verletModel->chains[static_cast<std::size_t>(seedLocation.chainIndex)];
            ImGui::SetTooltip("Selects all %zu joints of Chain %d.", chain.jointBoneIndices.size(), seedLocation.chainIndex);
        }
        ImGui::EndDisabled();
        if (!hasSeed || !seedLocation.IsValid()) {
            ImGui::SameLine();
            if (ctx.selection.SelectedModelPartIndices().size() > 1) {
                ImGui::TextDisabled("(select exactly one Verlet joint first - %zu are currently selected)",
                    ctx.selection.SelectedModelPartIndices().size());
            } else {
                ImGui::TextDisabled("(select a Verlet joint first)");
            }
        }
    }

    const std::string lowerFilter = ToLower(std::string(m_searchBuffer));

    // --- Left pane: part list (indented bone tree, or a flat rigid-body/joint list) ---
    constexpr float kSplitterThickness = 6.0f;
    constexpr float kMinTreeWidth = 150.0f;
    constexpr float kMinViewportWidth = 200.0f;
    const float fullWidth = ImGui::GetContentRegionAvail().x;
    const float maxTreeWidth = std::max(kMinTreeWidth, fullWidth - kSplitterThickness - kMinViewportWidth);
    m_treeWidth = Clamp(m_treeWidth, kMinTreeWidth, maxTreeWidth);

    ImGui::BeginChild("BoneViewerTree", ImVec2(m_treeWidth, 0.0f), true);
    BuildPartListPane(lowerFilter, ctx, verletModel);
    ImGui::EndChild();

    // Draggable splitter between the part list and the viewport.
    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrab));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrabHovered));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_ScrollbarGrabActive));
    ImGui::Button("##BoneViewerTreeSplitter", ImVec2(kSplitterThickness, -1.0f));
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemActive()) {
        m_treeWidth += ImGui::GetIO().MouseDelta.x;
    }
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        ImGui::SetMouseCursor(ImGuiMouseCursor_ResizeEW);
    }
    ImGui::SameLine();

    // --- Right pane: the live 3D viewport -------------------------------------
    ImGui::BeginChild(
        "BoneViewerViewport", ImVec2(0.0f, 0.0f), false, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    const ImVec2 avail = ImGui::GetContentRegionAvail();
    if (avail.x >= 1.0f && avail.y >= 1.0f) {
        const int width = std::max(1, static_cast<int>(avail.x));
        const int height = std::max(1, static_cast<int>(avail.y));

        const Vec3 eye = ComputeEyePosition();
        const Mat4 view = Mat4::LookAtLH(eye, m_camTarget, Vec3::Up());
        const float aspect = static_cast<float>(width) / static_cast<float>(height);
        const float fovYRadians = DegToRad(45.0f);
        const float nearZ = std::max(0.01f, m_boundsRadius * 0.01f);
        const float farZ = m_camDistance + m_boundsRadius * 6.0f + 10.0f;
        const Mat4 proj = Mat4::PerspectiveFovLH_ZO(fovYRadians, aspect, nearZ, farZ, /*flipY=*/true);
        const Mat4 viewProj = proj * view;

        // Neutral dark backdrop.
        const UiTextureHandle previewTexture = m_preview->Render(Mat4::Identity(), viewProj, width, height, { 30, 32, 38, 255 });
        if (previewTexture == UiTextureHandle::Invalid) {
            ImGui::TextDisabled("The 3D preview is not available.");
            ImGui::EndChild();
            ImGui::End();
            return;
        }

        const ImVec2 imageMin = ImGui::GetCursorScreenPos();
        ImGui::Image(static_cast<ImTextureID>(static_cast<std::uint64_t>(previewTexture)), avail);
        const ImVec2 imageMax(imageMin.x + avail.x, imageMin.y + avail.y);
        const bool hovered = ImGui::IsItemHovered();

        // Parts to project, label and hit-test for the active mode; defines OverlayPart's meaning per mode.
        std::vector<OverlayPart> overlayParts;
        if (m_viewMode == ModelPartKind::Bone) {
            overlayParts.reserve(m_bones.size());
            for (const BoneEntry& bone : m_bones) {
                overlayParts.push_back(OverlayPart{ bone.position, bone.name, static_cast<std::int32_t>(overlayParts.size()) });
            }
        } else if (m_viewMode == ModelPartKind::RigidBody) {
            overlayParts.reserve(m_rigidBodies.size());
            for (std::size_t i = 0; i < m_rigidBodies.size(); ++i) {
                const RigidBodyEntry& body = m_rigidBodies[i];
                overlayParts.push_back(OverlayPart{ body.translate,
                    body.name.empty() ? ("Part " + std::to_string(i)) : body.name, static_cast<std::int32_t>(i) });
            }
        } else if (m_viewMode == ModelPartKind::Joint) {
            overlayParts.reserve(m_joints.size());
            for (std::size_t i = 0; i < m_joints.size(); ++i) {
                const JointEntry& joint = m_joints[i];
                overlayParts.push_back(OverlayPart{ joint.translate,
                    joint.name.empty() ? ("Joint " + std::to_string(i)) : joint.name, static_cast<std::int32_t>(i) });
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

        // Project every part with this frame's viewProj; shared by hit-testing and the overlay pass.
        std::vector<ImVec2> screenPositions(overlayParts.size());
        std::vector<char> onScreen(overlayParts.size(), 0);
        for (std::size_t i = 0; i < overlayParts.size(); ++i) {
            ImVec2 screen;
            if (ProjectToScreen(overlayParts[i].position, viewProj, imageMin, imageMax, screen)) {
                screenPositions[i] = screen;
                onScreen[i] = 1;
            }
        }

        // Nearest on-screen part dot to the cursor (12px radius); used for hover and click hit-testing.
        int hoveredPartIndex = -1;
        if (hovered) {
            const ImVec2 mousePos = ImGui::GetMousePos();
            float hoveredDistSq = 144.0f; // 12px radius.
            for (std::size_t i = 0; i < overlayParts.size(); ++i) {
                if (!onScreen[i]) {
                    continue;
                }
                const float dx = mousePos.x - screenPositions[i].x;
                const float dy = mousePos.y - screenPositions[i].y;
                const float distSq = dx * dx + dy * dy;
                if (distSq < hoveredDistSq) {
                    hoveredDistSq = distSq;
                    hoveredPartIndex = static_cast<int>(i);
                }
            }
        }

        // --- Orbit camera input / direct-click part selection (applied to the next frame) ---
        const ImGuiIO& io = ImGui::GetIO();
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
            if (hoveredPartIndex >= 0) {
                // Clicked a part's dot: select it instead of starting an orbit drag.
                // Range-select needs a dense flat list, so Bone and Verlet modes only toggle.
                const bool supportsRangeSelect = m_viewMode != ModelPartKind::Bone && m_viewMode != ModelPartKind::Verlet;
                // Real part index of this overlay slot (a bone index in Verlet mode).
                const std::int32_t realPartIndex = overlayParts[static_cast<std::size_t>(hoveredPartIndex)].partIndex;
                if (supportsRangeSelect && io.KeyShift) {
                    const std::vector<std::int32_t> range = BuildInclusiveIndexRange(m_flatSelectionAnchorIndex, realPartIndex);
                    ctx.selection.SelectModelParts(
                        m_targetEntity, m_viewMode, std::vector<int>(range.begin(), range.end()));
                } else if (io.KeyCtrl || io.KeyShift) {
                    ctx.selection.ToggleModelPartInSelection(m_targetEntity, m_viewMode, realPartIndex);
                    if (supportsRangeSelect) {
                        m_flatSelectionAnchorIndex = realPartIndex;
                    }
                } else {
                    ctx.selection.SelectModelPart(m_targetEntity, m_viewMode, realPartIndex);
                    if (supportsRangeSelect) {
                        m_flatSelectionAnchorIndex = realPartIndex;
                    }
                }
            } else {
                m_rotating = true;
            }
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Left)) {
            m_rotating = false;
        }
        if (hovered && ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
            m_panning = true;
        }
        if (ImGui::IsMouseReleased(ImGuiMouseButton_Middle)) {
            m_panning = false;
        }

        if (m_rotating) {
            m_camYawDeg += io.MouseDelta.x * 0.3f;
            m_camPitchDeg = Clamp(m_camPitchDeg + io.MouseDelta.y * 0.3f, -85.0f, 85.0f);
        }
        if (m_panning) {
            const Vec3 forward = Normalize(m_camTarget - eye);
            Vec3 right = Normalize(Cross(Vec3::Up(), forward));
            if (LengthSquared(right) < kEpsilon) {
                right = Vec3::Right();
            }
            const Vec3 camUp = Cross(forward, right);
            const float panSpeed = m_camDistance * 0.0015f;
            m_camTarget += right * (-io.MouseDelta.x * panSpeed) + camUp * (io.MouseDelta.y * panSpeed);
        }
        if (hovered && io.MouseWheel != 0.0f) {
            m_camDistance = std::max(m_boundsRadius * 0.05f, m_camDistance - io.MouseWheel * (m_camDistance * 0.15f));
        }

        // --- Part gizmo overlay --------------------------------------------
        if (!overlayParts.empty()) {
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            drawList->PushClipRect(imageMin, imageMax, true);

            if (m_viewMode == ModelPartKind::Bone) {
                // Parent lines first, so dots and labels paint over them.
                for (std::size_t i = 0; i < m_bones.size(); ++i) {
                    const std::int32_t parent = m_bones[i].parentIndex;
                    if (parent < 0 || static_cast<std::size_t>(parent) >= m_bones.size()) {
                        continue;
                    }
                    if (!onScreen[i] || !onScreen[static_cast<std::size_t>(parent)]) {
                        continue;
                    }
                    drawList->AddLine(screenPositions[static_cast<std::size_t>(parent)], screenPositions[i],
                        IM_COL32(70, 200, 100, 200), 2.0f);
                }
            } else if (m_viewMode == ModelPartKind::RigidBody) {
                // Dimmer line from each rigid body to its attached bone.
                for (std::size_t i = 0; i < m_rigidBodies.size(); ++i) {
                    const std::int32_t boneIndex = m_rigidBodies[i].boneIndex;
                    if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size() || !onScreen[i]) {
                        continue;
                    }
                    ImVec2 boneScreen;
                    if (ProjectToScreen(m_bones[static_cast<std::size_t>(boneIndex)].position, viewProj, imageMin, imageMax,
                            boneScreen)) {
                        drawList->AddLine(screenPositions[i], boneScreen, IM_COL32(80, 180, 255, 110), 1.5f);
                    }
                }
            } else if (m_viewMode == ModelPartKind::Joint) {
                // Connector lines from each joint to both of its rigid bodies (when in range).
                for (std::size_t i = 0; i < m_joints.size(); ++i) {
                    if (!onScreen[i]) {
                        continue;
                    }
                    const JointEntry& joint = m_joints[i];
                    for (const std::int32_t bodyIndex : { joint.rigidBodyAIndex, joint.rigidBodyBIndex }) {
                        if (bodyIndex < 0 || static_cast<std::size_t>(bodyIndex) >= m_rigidBodies.size()) {
                            continue;
                        }
                        ImVec2 bodyScreen;
                        if (ProjectToScreen(m_rigidBodies[static_cast<std::size_t>(bodyIndex)].translate, viewProj, imageMin,
                                imageMax, bodyScreen)) {
                            drawList->AddLine(screenPositions[i], bodyScreen, IM_COL32(200, 120, 255, 140), 1.5f);
                        }
                    }
                }
            } else if (m_viewMode == ModelPartKind::Verlet) {
                // Chain connector lines and root markers, read from the shared Dynamic Chain model.
                if (verletModel != nullptr) {
                    for (const DynamicChainDefinition& chain : verletModel->chains) {
                        // Root/anchor marker: not selectable (the root bone is never simulated), drawn as a small square.
                        if (chain.rootBoneIndex >= 0 && static_cast<std::size_t>(chain.rootBoneIndex) < m_bones.size()) {
                            const Vec3 rootPos = m_bones[static_cast<std::size_t>(chain.rootBoneIndex)].position;
                            ImVec2 rootScreen;
                            if (ProjectToScreen(rootPos, viewProj, imageMin, imageMax, rootScreen)) {
                                constexpr float kHalf = 4.0f;
                                drawList->AddRectFilled(ImVec2(rootScreen.x - kHalf, rootScreen.y - kHalf),
                                    ImVec2(rootScreen.x + kHalf, rootScreen.y + kHalf), IM_COL32(200, 200, 200, 255));
                            }
                        }
                        // Tree edges: each joint connects to its resolved parent (the chain root when it has none).
                        for (std::size_t i = 0; i < chain.jointBoneIndices.size(); ++i) {
                            const std::int32_t boneIndex = chain.jointBoneIndices[i];
                            if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()) {
                                continue;
                            }
                            const std::int32_t parentJoint = (i < chain.parentJointIndex.size()) ? chain.parentJointIndex[i] : -1;
                            const bool parentIsRoot = parentJoint < 0;
                            const std::int32_t parentBoneIndex = parentIsRoot
                                ? chain.rootBoneIndex
                                : ((static_cast<std::size_t>(parentJoint) < chain.jointBoneIndices.size())
                                        ? chain.jointBoneIndices[static_cast<std::size_t>(parentJoint)]
                                        : -1);
                            if (parentBoneIndex < 0 || static_cast<std::size_t>(parentBoneIndex) >= m_bones.size()) {
                                continue;
                            }
                            const Vec3 parentPos = m_bones[static_cast<std::size_t>(parentBoneIndex)].position;
                            const Vec3 jointPos = m_bones[static_cast<std::size_t>(boneIndex)].position;
                            ImVec2 parentScreen, jointScreen;
                            if (ProjectToScreen(parentPos, viewProj, imageMin, imageMax, parentScreen)
                                && ProjectToScreen(jointPos, viewProj, imageMin, imageMax, jointScreen)) {
                                drawList->AddLine(parentScreen, jointScreen, IM_COL32(255, 90, 170, 160), 2.0f);
                            }
                        }
                        // Extra structural ("web brace") constraints: thinner and dimmer than the tree edges.
                        for (const ExtraStructuralConstraint& extra : chain.extraConstraints) {
                            if (extra.jointIndexA < 0 || extra.jointIndexB < 0
                                || static_cast<std::size_t>(extra.jointIndexA) >= chain.jointBoneIndices.size()
                                || static_cast<std::size_t>(extra.jointIndexB) >= chain.jointBoneIndices.size()) {
                                continue;
                            }
                            const std::int32_t boneA = chain.jointBoneIndices[static_cast<std::size_t>(extra.jointIndexA)];
                            const std::int32_t boneB = chain.jointBoneIndices[static_cast<std::size_t>(extra.jointIndexB)];
                            if (boneA < 0 || boneB < 0 || static_cast<std::size_t>(boneA) >= m_bones.size()
                                || static_cast<std::size_t>(boneB) >= m_bones.size()) {
                                continue;
                            }
                            ImVec2 screenA, screenB;
                            if (ProjectToScreen(m_bones[static_cast<std::size_t>(boneA)].position, viewProj, imageMin, imageMax, screenA)
                                && ProjectToScreen(m_bones[static_cast<std::size_t>(boneB)].position, viewProj, imageMin, imageMax, screenB)) {
                                drawList->AddLine(screenA, screenB, IM_COL32(120, 200, 255, 110), 1.0f);
                            }
                        }
                    }
                    // Colliders reachable by any collision-enabled chain, matched to rigid bodies by
                    // (boneIndex, shape, shapeSize); `group` is masked to 4 bits before use as a shift.
                    std::vector<bool> colliderIsReachableByAnyChain(verletModel->colliders.size(), false);
                    for (const DynamicChainDefinition& c : verletModel->chains) {
                        if (!c.collisionEnabled) {
                            continue;
                        }
                        for (std::size_t ci = 0; ci < verletModel->colliders.size(); ++ci) {
                            const ModelColliderDefinition& colliderDef = verletModel->colliders[ci];
                            for (const DynamicJointSettings& joint : c.jointSettings) {
                                const std::uint16_t jointBit = static_cast<std::uint16_t>(1u << (joint.group & 0x0Fu));
                                const std::uint16_t colliderBit = static_cast<std::uint16_t>(1u << (colliderDef.group & 0x0Fu));
                                if ((jointBit & colliderDef.collisionMask) != 0 && (colliderBit & joint.collisionMask) != 0) {
                                    colliderIsReachableByAnyChain[ci] = true;
                                    break;
                                }
                            }
                        }
                    }
                    // Model-wide Static collider wireframes, drawn once per model (bind-pose transform, no bone tracking).
                    for (const RigidBodyEntry& body : m_rigidBodies) {
                        if (body.motionType != RigidBodyMotionType::Static) {
                            continue;
                        }
                        // Dim bodies no collision-enabled chain can reach; unmatched bodies keep full alpha.
                        bool matchedReachable = true;
                        bool foundMatch = false;
                        for (std::size_t ci = 0; ci < verletModel->colliders.size() && !foundMatch; ++ci) {
                            const ModelColliderDefinition& colliderDef = verletModel->colliders[ci];
                            if (colliderDef.boneIndex == body.boneIndex && colliderDef.shape == body.shape
                                && colliderDef.shapeSize.x == body.shapeSize.x && colliderDef.shapeSize.y == body.shapeSize.y
                                && colliderDef.shapeSize.z == body.shapeSize.z) {
                                foundMatch = true;
                                matchedReachable = colliderIsReachableByAnyChain[ci];
                            }
                        }
                        const ImU32 wireColor = (foundMatch && !matchedReachable) ? IM_COL32(255, 90, 170, 30) : IM_COL32(255, 90, 170, 90);
                        const std::vector<WireframeSegment> wireframe =
                            BuildRigidBodyWireframe(body.shape, body.shapeSize, body.translate, body.rotateRadians);
                        for (const WireframeSegment& segment : wireframe) {
                            ImVec2 screenA, screenB;
                            if (ProjectToScreen(segment.a, viewProj, imageMin, imageMax, screenA)
                                && ProjectToScreen(segment.b, viewProj, imageMin, imageMax, screenB)) {
                                drawList->AddLine(screenA, screenB, wireColor, 1.25f);
                            }
                        }
                    }
                    // Orphaned (non-simulated) rigid-body bones: red markers, drawn regardless of selection.
                    for (const std::int32_t boneIndex : verletModel->diagnostics.orphanedDynamicBoneIndices) {
                        if (boneIndex < 0 || static_cast<std::size_t>(boneIndex) >= m_bones.size()) {
                            continue;
                        }
                        ImVec2 screen;
                        if (ProjectToScreen(m_bones[static_cast<std::size_t>(boneIndex)].position, viewProj, imageMin, imageMax, screen)) {
                            constexpr float kHalf = 5.0f;
                            drawList->AddRectFilled(ImVec2(screen.x - kHalf, screen.y - kHalf), ImVec2(screen.x + kHalf, screen.y + kHalf),
                                IM_COL32(220, 40, 40, 255));
                            drawList->AddRect(ImVec2(screen.x - kHalf - 1, screen.y - kHalf - 1), ImVec2(screen.x + kHalf + 1, screen.y + kHalf + 1),
                                IM_COL32(255, 255, 255, 200));

                            // Hover tooltip explains why this bone is flagged.
                            const ImVec2 hoverMin(screen.x - kHalf - 1, screen.y - kHalf - 1);
                            const ImVec2 hoverMax(screen.x + kHalf + 1, screen.y + kHalf + 1);
                            if (ImGui::IsMouseHoveringRect(hoverMin, hoverMax)) {
                                const char* boneName = m_bones[static_cast<std::size_t>(boneIndex)].name.c_str();
                                ImGui::SetTooltip(
                                    "Orphaned: %s\nNo Static rigid body is reachable from this bone, either via the "
                                    "Joint graph at all, or via this bone's own real skeleton ancestry.\nNot physics-"
                                    "simulated - the animated (FK) pose is used unchanged.",
                                    boneName);
                            }
                        }
                    }
                }
            }

            for (std::size_t i = 0; i < overlayParts.size(); ++i) {
                if (!onScreen[i]) {
                    continue;
                }
                const bool matchesFilter =
                    !lowerFilter.empty() && ToLower(overlayParts[i].name).find(lowerFilter) != std::string::npos;
                const bool isHovered = hovered && (static_cast<int>(i) == hoveredPartIndex);
                const bool isSelected =
                    ctx.selection.IsModelPartSelected(m_targetEntity, m_viewMode, overlayParts[i].partIndex);

                // Dot color per mode; priority: selected > hovered > search match > mode default.
                ImU32 dotColor = m_viewMode == ModelPartKind::Bone ? IM_COL32(90, 230, 130, 255)
                    : m_viewMode == ModelPartKind::RigidBody ? IM_COL32(80, 180, 255, 255)
                    : m_viewMode == ModelPartKind::Joint      ? IM_COL32(200, 120, 255, 255)
                                                              : IM_COL32(255, 90, 170, 255); // Verlet
                if (matchesFilter) {
                    dotColor = IM_COL32(255, 215, 60, 255);
                }
                if (isHovered) {
                    dotColor = IM_COL32(255, 255, 255, 255);
                }
                if (isSelected) {
                    dotColor = IM_COL32(255, 140, 0, 255);
                }
                drawList->AddCircleFilled(screenPositions[i], isSelected ? 6.0f : (isHovered ? 5.0f : 3.5f), dotColor);
                if (isSelected) {
                    drawList->AddCircle(screenPositions[i], 9.0f, IM_COL32(255, 140, 0, 255), 0, 2.0f);
                }

                // Unselected rigid bodies show only their dot; every selected one also reveals its per-shape wireframe.
                if (m_viewMode == ModelPartKind::RigidBody && isSelected) {
                    const RigidBodyEntry& body = m_rigidBodies[i];
                    const std::vector<WireframeSegment> wireframe =
                        BuildRigidBodyWireframe(body.shape, body.shapeSize, body.translate, body.rotateRadians);
                    for (const WireframeSegment& segment : wireframe) {
                        ImVec2 screenA, screenB;
                        if (ProjectToScreen(segment.a, viewProj, imageMin, imageMax, screenA)
                            && ProjectToScreen(segment.b, viewProj, imageMin, imageMax, screenB)) {
                            drawList->AddLine(screenA, screenB, dotColor, 1.5f);
                        }
                    }
                }

                if (m_showAllNames || matchesFilter || isHovered || isSelected) {
                    const ImVec2 textPos(screenPositions[i].x + 7.0f, screenPositions[i].y - 7.0f);
                    const ImVec2 textSize = ImGui::CalcTextSize(overlayParts[i].name.c_str());
                    drawList->AddRectFilled(ImVec2(textPos.x - 2.0f, textPos.y - 1.0f),
                        ImVec2(textPos.x + textSize.x + 2.0f, textPos.y + textSize.y + 1.0f), IM_COL32(0, 0, 0, 160));
                    drawList->AddText(textPos, IM_COL32(255, 255, 255, 255), overlayParts[i].name.c_str());
                }
            }

            drawList->PopClipRect();
        }
    }

    ImGui::EndChild();

    ImGui::End();
}

} // namespace gte
