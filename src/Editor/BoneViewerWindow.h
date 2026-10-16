#pragma once

#include "Selection.h" // ModelPartKind
#include "RigidBodyGroupSelection.h" // RigidBodyJointEdge, BuildRigidBodyAdjacency()
#include "FlatListRangeSelection.h" // BuildInclusiveIndexRange()
#include "../Assets/PhysicsData.h" // RigidBodyShape
#include "../Core/GraphicsHandles.h"
#include "../Core/UiTextureHandle.h"
#include "../Math/Vec3.h"
#include "../Physics/DynamicChainDefinition.h" // DynamicChainDefinition
#include "../Game/Physics/DynamicChainRigCache.h" // DynamicChainRigCache::ModelEntry, used by BuildPartListPane()
#include "../Renderer/Compute/ComputeDescriptorSet.h"
#include "../Renderer/Pipeline.h"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gte {

class Registry;
class Renderer;
class Buffer;
class Texture2D;
class RenderTexture;
class ModelRigCache;
class PhysicsSystem;
struct EditorContext;
// Floating debug window opened from the Inspector: a live, orbitable 3D view of a model's bind-pose mesh with every
// bone drawn as a gizmo dot (plus a line to its parent) and its name on hover, to check imported bone hierarchies.
// It can also show rigid bodies and joints (m_viewMode); selection goes through the Editor's Selection.
// Reads the source *.gta file straight from disk (through ModelRigCache for the rig data), so it never depends on
// the game's skinning caches and always shows the bind pose, not a live animated pose.
// Builds its pipeline with the MeshPreview shader pair and records through CommandBuffer in a
// Renderer::RenderOffscreen() callback. Gated behind GTE_ENABLE_PROJECT_PANEL because those shaders are only built
// under that switch.
// Call Reset() before the UI backend shuts down (the destructor also calls it).
class BoneViewerWindow {
public:
    BoneViewerWindow() = default;
    ~BoneViewerWindow();

    BoneViewerWindow(const BoneViewerWindow&) = delete;
    BoneViewerWindow& operator=(const BoneViewerWindow&) = delete;

    // Opens (or re-targets) the window onto `rootEntity`. An invalid entity shows a "no mesh asset" message.
    void Open(Entity rootEntity) noexcept;

    // Builds the floating window; a no-op while closed. Called once per frame from ImGuiEditorLayer::BuildUI().
    // `rigCache` is the shared rig cache; `physicsSystem` is looked up fresh every call for the dynamic-chain view.
    void Build(Registry& registry, Renderer& renderer, EditorContext& ctx, ModelRigCache& rigCache, PhysicsSystem& physicsSystem);

    // Releases every held GPU resource after the GPU is idle. Also called by the destructor; safe to call repeatedly.
    void Reset();

private:
    // One skeleton bone, flattened down to only what this window's overlay
    // actually needs to draw (a gizmo dot + a line to its parent + its own
    // name) - deliberately NOT the full Assets/SkeletonData.h::Bone (IK/
    // append/fixed-axis/... fields are irrelevant here).
    struct BoneEntry {
        std::string name;
        Vec3 position; // Bind-pose position, same model-local space as the uploaded mesh's own vertices.
        std::int32_t parentIndex = -1;
    };

    // One rigid body, flattened for overlay drawing - mirrors BoneEntry's
    // own "only what this window's overlay actually needs" philosophy.
    // Position/rotation are already in the SAME model-local space as
    // MeshData::positions/Bone::position (see PhysicsData.h's own
    // RigidBody::translate doc comment) - no extra transform needed to
    // compare against a bone's own position.
    struct RigidBodyEntry {
        std::string name;
        Vec3 translate;
        Vec3 rotateRadians; // Euler, PMX convention (see PhysicsData.h) - used only for an approximate visual orientation hint.
        RigidBodyShape shape = RigidBodyShape::Sphere;
        Vec3 shapeSize;
        std::int32_t boneIndex = -1; // Index into m_bones this body is attached to (-1 if unattached) - drawn as a connecting line.
        // PMX collision group (0-15, PhysicsData.h's own RigidBody::group) -
        // added by task_manager/verlet-integration-4/
        // PHASE2_RIGID_BODY_GROUP_FIELD_AND_ADJACENCY_ALGORITHMS.md purely so
        // the Bone Viewer's "Select All (Group)" toolbar button
        // (PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md)
        // has something to compare against - never drawn/used for anything
        // else in this window.
        std::uint8_t group = 0;
        // task_manager/verlet-integration-9, PHASE5 - lets the Verlet
        // overlay (BuildOverlayGeometry()'s own Verlet branch) filter down
        // to exactly the bodies DetectModelColliders() (Physics/
        // ModelColliderDetection.h) would treat as a real collision
        // obstacle (RigidBodyMotionType::Static) - mirrors that function's
        // own eligibility rule so the Bone Viewer's preview never shows a
        // shape that will not actually collide with anything at runtime.
        RigidBodyMotionType motionType = RigidBodyMotionType::Static;
        // task_manager/verlet-integration-10, PHASE4 (v2 - appended AFTER
        // motionType, the struct's own true last field BEFORE this campaign,
        // never inserted between `group` and `motionType` - the ONE real
        // construction call site in BoneViewerWindow.cpp populates this
        // struct via plain 8-argument POSITIONAL aggregate-init today; any
        // field inserted in the MIDDLE of this struct would either silently
        // absorb the wrong value or, in this specific case, fail to compile
        // outright, since `motionType` - an enum class with no implicit
        // integer conversion - would be forced into whatever new field took
        // its old position. See PHASE0_MASTER_STRATEGY.md's Step 2 point 12
        // for the general rule this struct is a concrete, previously-broken
        // example of.) - this body's own PMX collision-group MASK
        // (Assets/PhysicsData.h's own RigidBody::collisionGroupMask), added
        // purely so the Verlet overlay (BuildOverlayGeometry()'s own Verlet
        // branch) can visually distinguish a Static collider the
        // CURRENTLY-EXPANDED chain's own joints could never actually reach
        // (PHASE1's group/mask filter) from one it genuinely collides
        // against - never used for selection logic (unlike `group` above,
        // which the unrelated "Select All (Group)" toolbar button already
        // uses).
        std::uint16_t collisionGroupMask = 0xFFFF;
    };

    // One joint, flattened for overlay drawing.
    struct JointEntry {
        std::string name;
        Vec3 translate;
        std::int32_t rigidBodyAIndex = -1; // Index into m_rigidBodies (-1 if out of range/unset).
        std::int32_t rigidBodyBIndex = -1;
    };

    void EnsurePipeline(Renderer& renderer);
    bool EnsureDataLoaded(Renderer& renderer, const std::string& absoluteGtaPath, EditorContext& ctx, ModelRigCache& rigCache);
    void EnsureRenderTexture(Renderer& renderer, int width, int height);

    // Recomputes the orbit camera's target/distance from the currently-
    // loaded mesh's own bounding sphere (m_boundsCenter/m_boundsRadius) and
    // resets yaw/pitch to a fixed, pleasant default angle - called once
    // right after a (re)load, and again whenever the "Reset View" button is
    // pressed.
    void FrameCameraToBounds() noexcept;

    // The orbit camera's current eye position, derived from
    // m_camTarget/m_camYawDeg/m_camPitchDeg/m_camDistance.
    Vec3 ComputeEyePosition() const noexcept;

    bool m_open = false;
    Entity m_targetEntity = kInvalidEntity;

    Renderer* m_renderer = nullptr; // Non-owning - needed to destroy/release every handle below.

    // Lazily built on first Build() call, then reused for every
    // subsequently-viewed model - built via Renderer's configurable pipeline
    // API (Core/PipelineDesc.h) instead of hand-rolled Vulkan calls.
    std::unique_ptr<Pipeline> m_pipeline;
    DescriptorSetLayoutHandle m_setZeroFillerLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    DescriptorSetLayoutHandle m_setOneLayoutHandle = DescriptorSetLayoutHandle::Invalid;

    // Neutral stand-in for the shared set=1 lighting interface: a 1x1 white texture at binding 0 and a
    // zero-filled UBO at binding 8. The set comes from Renderer's shared pool and is never freed individually.
    ComputeDescriptorSet m_localSetOneDescriptorSet;
    std::unique_ptr<Texture2D> m_localWhiteTexture;
    std::unique_ptr<Buffer> m_localUniformBuffer;

    std::string m_cachedPath;
    std::filesystem::file_time_type m_cachedWriteTime{};
    bool m_cachedIsValid = false; // True if m_cachedPath resolved to a valid, non-empty Mesh *.gta last time.

    std::unique_ptr<Buffer> m_vertexBuffer;
    std::unique_ptr<Buffer> m_indexBuffer;
    std::uint32_t m_vertexCount = 0;
    std::uint32_t m_indexCount = 0;

    // The currently-loaded model's skeleton, flattened for overlay drawing -
    // empty for a boneless/riggless mesh (see BuildEntityInspector()'s own
    // "no bone/skeleton data" message in that case).
    std::vector<BoneEntry> m_bones;

    // The currently-loaded model's rigid bodies/joints, flattened for
    // overlay drawing the same way m_bones is - both loaded (alongside
    // m_bones) via the shared ModelRigCache in EnsureDataLoaded(). Empty for
    // a model with no physics data at all (most non-jiggle-bone models).
    std::vector<RigidBodyEntry> m_rigidBodies;
    std::vector<JointEntry> m_joints;

    // Which of the three categories the toolbar dropdown currently shows -
    // persisted across frames (like m_showAllNames), NOT reset on reload (a
    // user reloading/reopening onto a different model most likely wants to
    // keep looking at the same category they were just looking at).
    ModelPartKind m_viewMode = ModelPartKind::Bone;

    // Child-index adjacency derived from every BoneEntry::parentIndex above
    // (m_boneChildren[i] lists every bone whose parentIndex == i) plus the
    // list of ROOT bones (parentIndex invalid/out of range) - the two things
    // BuildPartListPane()/RenderBoneTreeNode() need to walk the skeleton as a
    // real indented tree "start from root", mirroring "Hierarchy"'s own
    // GetChildren()-based tree (see Panels/HierarchyPanel.cpp). Rebuilt once
    // per (re)load, right alongside m_bones itself - see
    // RebuildBoneHierarchyIndex().
    std::vector<std::vector<std::int32_t>> m_boneChildren;
    std::vector<std::int32_t> m_rootBoneIndices;

    // Per-rigid-body adjacency derived from every JointEntry's own
    // rigidBodyAIndex/rigidBodyBIndex above (m_rigidBodyAdjacency[i] lists
    // every OTHER rigid body directly joined to body i) - the graph the
    // Bone Viewer's "Select All (Branch)" toolbar button
    // (PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md) walks
    // via RigidBodyGroupSelection.h's SelectRigidBodyBranch(). Rebuilt once
    // per (re)load, right alongside m_rigidBodies/m_joints themselves - see
    // RebuildRigidBodyAdjacencyIndex().
    std::vector<std::vector<std::int32_t>> m_rigidBodyAdjacency;

    // The last FLAT-list part index (Rigid Body/Joint mode only - see
    // m_viewMode) that was the target of a PLAIN or Ctrl-click - Shift-
    // click's own "anchor" for a genuine Windows-Explorer-style contiguous
    // range select (see FlatListRangeSelection.h's BuildInclusiveIndexRange(),
    // and task_manager/verlet-integration-4/
    // PHASE3_BONE_VIEWER_SELECT_ALL_BUTTONS_AND_MULTISELECT_INPUT.md's own
    // v2 revision). Reset to -1 (no anchor) whenever the index space it
    // refers to stops meaning the same thing: a genuine data reload
    // (EnsureDataLoaded()'s "reload starting" block, right alongside
    // ctx.selection.ClearModelPartIfEntity() - see Step 3.8 below) and any
    // m_viewMode change (the View combo callback - Step 3.8 below) -
    // RigidBody/Joint each have their own independent index space, and Bone
    // mode has no flat-list range-select concept at all (see
    // RenderBoneTreeNode()'s own doc comment, Step 3.3, for why the tree
    // stays toggle-only for both Ctrl AND Shift).
    std::int32_t m_flatSelectionAnchorIndex = -1;

    // Bounding sphere of the currently-uploaded mesh (model-local space) -
    // used by FrameCameraToBounds() to auto-frame the orbit camera whenever
    // a new model is loaded.
    Vec3 m_boundsCenter = Vec3::Zero();
    float m_boundsRadius = 1.0f;

    std::unique_ptr<RenderTexture> m_renderTexture;
    UiTextureHandle m_descriptor = UiTextureHandle::Invalid;
    int m_texWidth = 0;
    int m_texHeight = 0;

    // Simple target-relative orbit camera, entirely local to this window
    // (deliberately NOT EditorCamera - see EditorCamera.h's own class
    // comment: it's a free-fly camera exclusively for the "Scene" panel,
    // with no notion of an orbit target/auto-framing, which this window
    // genuinely needs whenever a newly-opened model is a wildly different
    // scale from whatever was framed before). Left-mouse-drag rotates
    // (yaw/pitch around m_camTarget), mouse wheel dollies (m_camDistance),
    // middle-mouse-drag pans (m_camTarget itself) - handled entirely in
    // Build() below, the one place that reads ImGui's mouse state.
    float m_camYawDeg = 0.0f;
    float m_camPitchDeg = 12.0f;
    float m_camDistance = 5.0f;
    Vec3 m_camTarget = Vec3::Zero();
    bool m_needsFraming = true; // Set on (re)load - makes the next Build() call FrameCameraToBounds() once.

    // Drag-capture state, same "keeps responding even if the cursor drifts
    // outside the image mid-drag, ends only once the button is released"
    // pattern as EditorContext::sceneCameraPanning/sceneCameraRotating (see
    // Panels/ScenePanel.cpp) - local here since this window's camera input
    // has no other shared state to live alongside.
    bool m_rotating = false;
    bool m_panning = false;

    // Bone-name search filter (Unity's own "All" search field in its
    // Avatar configuration screen - see the attached reference screenshot)
    // - a part whose name contains this (case-insensitively) as a substring
    // is drawn highlighted/always-labeled; every other part is still drawn
    // as a plain gizmo dot, labeled only on hover. Also filters the tree/
    // list pane (see BuildPartListPane()) - a bone with no matching name AND
    // no matching descendant is hidden from the tree entirely while a
    // filter is active, same "search prunes the tree" convention Unity's own
    // Hierarchy search box uses; a rigid body/joint row is simply hidden if
    // its own name doesn't match (no descendant concept for those two).
    char m_searchBuffer[128] = {};

    // When true, every part's name is drawn permanently instead of only on
    // hover/search-match - handy for a small enough skeleton, toggled via
    // the window's own toolbar checkbox. Applies to all three view modes.
    bool m_showAllNames = false;

    // Persisted (across frames) pixel width of the tree pane, adjusted live
    // by dragging the splitter between it and the 3D viewport - same
    // "persist across frames, clamp to sane bounds every Build() call"
    // convention as EditorContext::inspectorPreviewHeight (see
    // Panels/InspectorPanel.cpp's BuildAssetInspector()).
    float m_treeWidth = 260.0f;

    // Rebuilds m_boneChildren/m_rootBoneIndices from m_bones' own
    // parentIndex fields - called once right after m_bones itself is
    // (re)populated in EnsureDataLoaded().
    void RebuildBoneHierarchyIndex();

    // Rebuilds m_rigidBodyAdjacency from m_joints' own rigidBodyAIndex/
    // rigidBodyBIndex fields - called once right after m_joints itself is
    // (re)populated in EnsureDataLoaded(), mirroring
    // RebuildBoneHierarchyIndex()'s own "derive an index right after the
    // source data it's built from" convention.
    void RebuildRigidBodyAdjacencyIndex();

    // True if `boneIndex` itself, or ANY of its descendants (recursively),
    // has a name containing `lowerFilter` as a case-insensitive substring -
    // what decides whether an ancestor bone stays visible in the tree while
    // a search filter is active, even if the ancestor's OWN name doesn't
    // match (so the path down to a deeply-nested match is never hidden).
    // `depth` is a defensive recursion-depth guard against a malformed/
    // cyclic parentIndex chain (see Assets/SkeletonData.h's own bones
    // being "never assumed sorted/acyclic") - capped at m_bones.size(),
    // the maximum depth a genuinely acyclic skeleton could ever have.
    bool BoneMatchesFilterRecursive(std::int32_t boneIndex, const std::string& lowerFilter, int depth) const;

    // Renders `boneIndex` (and, if expanded, every descendant) as one
    // indented ImGui tree node, wiring up row selection (single-click) and
    // camera re-centering (double-click) - the tree-pane equivalent of
    // Panels/HierarchyPanel.cpp's RenderEntityNode(). Same recursion-depth
    // guard as BoneMatchesFilterRecursive() above. Selection highlight/
    // click now routes through ctx.selection.IsModelPartSelected()/
    // SelectModelPart() (ModelPartKind::Bone) instead of a private index.
    void RenderBoneTreeNode(std::int32_t boneIndex, const std::string& lowerFilter, int depth, EditorContext& ctx);

    // Renders one non-tree (Rigid Body/Joint) row - a single, non-indented,
    // non-expandable Selectable, reusing the same search-filter/selection/
    // double-click-recenter shape RenderBoneTreeNode()'s own leaf case
    // already has, just without the tree-node machinery (Rigid Body/Joint
    // have no bind-pose parent/child tree to walk - see
    // PHASE0_MASTER_STRATEGY.md, Culprit B).
    void RenderFlatPartRow(ModelPartKind kind, std::int32_t index, const std::string& name, const Vec3& position,
        const std::string& lowerFilter, EditorContext& ctx);

    // Renders one detected dynamic bone chain (task_manager/verlet-integration-5,
    // PHASE2_BONE_VIEWER_VERLET_MODE_TREE_AND_GIZMO.md) as a non-selectable,
    // always-expanded ImGui tree header (e.g. "Chain 0 - Root: waist (3 joints)"),
    // with each of its joints rendered underneath via the EXISTING
    // RenderFlatPartRow() (ModelPartKind::Verlet, partIndex = that joint's own
    // bone index - see PHASE1_CHAIN_LOOKUP_AND_SELECTION_FOUNDATION.md's own
    // "bone index, not a flattened counter" decision). Hidden entirely (no
    // header drawn at all) if a non-empty `lowerFilter` matches NONE of this
    // chain's own joint names - the "search prunes the tree" convention every
    // other mode's own pane already follows (see BoneMatchesFilterRecursive()'s
    // doc comment).
    void RenderVerletChainNode(std::int32_t chainIndex, const DynamicChainDefinition& chain,
        const std::string& lowerFilter, EditorContext& ctx);

    // Renders every root bone (see m_rootBoneIndices) as the top level of a
    // real indented hierarchy tree, "starting from root" (Bone mode), or a
    // flat list of Selectable rows (Rigid Body/Joint mode, via
    // RenderFlatPartRow()) - branches on m_viewMode. The left-hand pane of
    // this window, alongside the 3D viewport on the right (see Build()).
    // `verletModel` (Verlet mode only, may be nullptr - see Build()'s own
    // fetch-once-per-frame comment) is threaded down explicitly rather than
    // as a member field/a PhysicsSystem& parameter - see
    // PHASE2_BONE_VIEWER_VERLET_MODE_TREE_AND_GIZMO.md, section 3.4.
    void BuildPartListPane(const std::string& lowerFilter, EditorContext& ctx, const DynamicChainRigCache::ModelEntry* verletModel);
};

} // namespace gte
