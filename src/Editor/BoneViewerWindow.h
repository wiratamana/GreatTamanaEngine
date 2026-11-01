#pragma once

#include "EditorWindow.h"
#include "Selection.h" // ModelPartKind
#include "RigidBodyGroupSelection.h" // RigidBodyJointEdge, BuildRigidBodyAdjacency()
#include "FlatListRangeSelection.h" // BuildInclusiveIndexRange()
#include "../Assets/PhysicsData.h" // RigidBodyShape
#include "../Math/Vec3.h"
#include "../Physics/DynamicChainDefinition.h" // DynamicChainDefinition
#include "../Game/Physics/DynamicChainRigCache.h" // DynamicChainRigCache::ModelEntry, used by BuildPartListPane()

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gte {

class MeshPreviewRenderer;
class ModelRigCache;
class Renderer;
struct EditorContext;

// Floating Inspector window: orbitable bind-pose mesh with bone gizmos, rigid bodies, joints and dynamic chains.
// Reads the source *.gta from disk. Call Reset() before the UI backend shuts down (the destructor also does).
class BoneViewerWindow final : public EditorWindow {
public:
    explicit BoneViewerWindow(ModelRigCache& rigCache);
    ~BoneViewerWindow();

    BoneViewerWindow(const BoneViewerWindow&) = delete;
    BoneViewerWindow& operator=(const BoneViewerWindow&) = delete;

    // Opens (or re-targets) the window onto `rootEntity`. An invalid entity shows a "no mesh asset" message.
    void Open(Entity rootEntity) noexcept;

    // Releases every held GPU resource after the GPU is idle. Also called by the destructor; safe to call repeatedly.
    void Reset();

protected:
    void OnGui(EditorFrame& frame) override;

private:
    // One skeleton bone, reduced to what the overlay draws.
    struct BoneEntry {
        std::string name;
        Vec3 position; // Bind-pose position, in the uploaded mesh's model-local space.
        std::int32_t parentIndex = -1;
    };

    // One rigid body, reduced to what the overlay draws (model-local space, like the bones).
    struct RigidBodyEntry {
        std::string name;
        Vec3 translate;
        Vec3 rotateRadians; // Euler, PMX convention; only an approximate orientation hint.
        RigidBodyShape shape = RigidBodyShape::Sphere;
        Vec3 shapeSize;
        std::int32_t boneIndex = -1; // Index into m_bones (-1 if unattached).
        std::uint8_t group = 0; // PMX collision group, used by "Select All (Group)".
        RigidBodyMotionType motionType = RigidBodyMotionType::Static; // Only Static bodies act as Verlet colliders.
        std::uint16_t collisionGroupMask = 0xFFFF; // Tells the Verlet overlay which colliders a chain can reach.
    };

    // One joint, reduced to what the overlay draws.
    struct JointEntry {
        std::string name;
        Vec3 translate;
        std::int32_t rigidBodyAIndex = -1; // Index into m_rigidBodies (-1 if out of range/unset).
        std::int32_t rigidBodyBIndex = -1;
    };

    bool EnsureDataLoaded(Renderer& renderer, const std::string& absoluteGtaPath, EditorContext& ctx, ModelRigCache& rigCache);

    // Recomputes the orbit camera's target and distance from the mesh bounds and resets yaw/pitch.
    void FrameCameraToBounds() noexcept;

    // The orbit camera's eye position, derived from target, yaw, pitch and distance.
    Vec3 ComputeEyePosition() const noexcept;

    ModelRigCache& m_rigCache;
    Entity m_targetEntity = kInvalidEntity;

    std::unique_ptr<MeshPreviewRenderer> m_preview; // Built on first use.
    bool m_previewBuildFailed = false;

    std::string m_cachedPath;
    std::filesystem::file_time_type m_cachedWriteTime{};
    bool m_cachedIsValid = false; // True if m_cachedPath resolved to a valid, non-empty Mesh *.gta last time.
    std::uint32_t m_vertexCount = 0;
    std::uint32_t m_indexCount = 0;

    // The loaded model's skeleton; empty for a mesh without bones.
    std::vector<BoneEntry> m_bones;

    // The loaded model's rigid bodies and joints; empty without physics data.
    std::vector<RigidBodyEntry> m_rigidBodies;
    std::vector<JointEntry> m_joints;

    // Which part category the toolbar dropdown shows; kept across model reloads.
    ModelPartKind m_viewMode = ModelPartKind::Bone;

    // m_boneChildren[i] lists the bones whose parentIndex is i; m_rootBoneIndices lists the bones without a valid
    // parent. Rebuilt with m_bones.
    std::vector<std::vector<std::int32_t>> m_boneChildren;
    std::vector<std::int32_t> m_rootBoneIndices;

    // m_rigidBodyAdjacency[i] lists the rigid bodies directly joined to body i. Rebuilt with m_joints.
    std::vector<std::vector<std::int32_t>> m_rigidBodyAdjacency;

    // Anchor of a Shift-click range select in the flat Rigid Body/Joint list; -1 when none.
    // Reset when the index space changes (data reload, view mode change).
    std::int32_t m_flatSelectionAnchorIndex = -1;

    // Bounding sphere of the loaded mesh (model-local space); frames the orbit camera.
    Vec3 m_boundsCenter = Vec3::Zero();
    float m_boundsRadius = 1.0f;

    // Orbit camera local to this window. Left drag rotates, wheel dollies, middle drag pans.
    float m_camYawDeg = 0.0f;
    float m_camPitchDeg = 12.0f;
    float m_camDistance = 5.0f;
    Vec3 m_camTarget = Vec3::Zero();
    bool m_needsFraming = true; // Set on (re)load; the next draw frames the camera once.

    // A drag keeps responding when the cursor leaves the image, until the button is released.
    bool m_rotating = false;
    bool m_panning = false;

    // Case-insensitive name filter: matches are highlighted and labeled, and non-matching tree branches are hidden.
    char m_searchBuffer[128] = {};

    // Draws every part's name permanently instead of only on hover or match.
    bool m_showAllNames = false;

    // Pixel width of the tree pane, changed by dragging the splitter.
    float m_treeWidth = 260.0f;

    // Rebuilds m_boneChildren/m_rootBoneIndices from m_bones.
    void RebuildBoneHierarchyIndex();

    // Rebuilds m_rigidBodyAdjacency from m_joints.
    void RebuildRigidBodyAdjacencyIndex();

    // True if the bone or any descendant has a name containing `lowerFilter`; keeps the path to a match visible.
    // `depth` guards against a cyclic parent chain.
    bool BoneMatchesFilterRecursive(std::int32_t boneIndex, const std::string& lowerFilter, int depth) const;

    // Draws a bone (and, when expanded, its descendants) as an ImGui tree node: click selects, double-click centers.
    void RenderBoneTreeNode(std::int32_t boneIndex, const std::string& lowerFilter, int depth, EditorContext& ctx);

    // Draws one flat (Rigid Body/Joint) row with the same filter, selection and recenter behavior as a bone node.
    void RenderFlatPartRow(ModelPartKind kind, std::int32_t index, const std::string& name, const Vec3& position,
        const std::string& lowerFilter, EditorContext& ctx);

    // Draws one detected dynamic bone chain as an always-expanded header with its joints as flat rows.
    // Hidden when a filter matches none of its joints.
    void RenderVerletChainNode(std::int32_t chainIndex, const DynamicChainDefinition& chain,
        const std::string& lowerFilter, EditorContext& ctx);

    // The left pane: bone tree (Bone mode) or flat list (other modes).
    // `verletModel` (Verlet mode only) may be nullptr.
    void BuildPartListPane(const std::string& lowerFilter, EditorContext& ctx, const DynamicChainRigCache::ModelEntry* verletModel);
};

} // namespace gte
