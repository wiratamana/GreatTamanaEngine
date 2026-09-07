#pragma once

#include "Selection.h" // ModelPartKind
#include "RigidBodyGroupSelection.h" // RigidBodyJointEdge, BuildRigidBodyAdjacency()
#include "FlatListRangeSelection.h" // BuildInclusiveIndexRange()
#include "../Assets/PhysicsData.h" // RigidBodyShape
#include "../Math/Vec3.h"
#include "../Physics/DynamicChainDefinition.h" // DynamicChainDefinition - task_manager/verlet-integration-5, Phase 2
#include "../Game/Physics/DynamicChainRigCache.h" // DynamicChainRigCache::ModelEntry - used by BuildPartListPane()'s signature below

#include <volk.h>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace gte {

class Registry;
class Renderer;
class Buffer;
class RenderTexture;
class ModelRigCache;
class PhysicsSystem; // task_manager/verlet-integration-5, Phase 2 - PhysicsSystem::GetDynamicChainRigCache()
struct EditorContext;
// A Unity-"Avatar configuration"-style debug window: opened on demand (via
// a button in the Inspector - see Panels/InspectorPanel.cpp's
// BuildEntityInspector()) as its own floating ImGui window ("on the fly",
// separate from the main docked layout - and, since ImGuiConfigFlags_
// ViewportsEnable is already set for the whole Editor, this floating window
// can itself be dragged clean outside the main OS window like any other
// panel), showing a live 3D view of a spawned MMD model's BIND-POSE mesh
// with every one of its skeleton's bones drawn as a small gizmo dot (plus a
// line to its parent bone), and that bone's own NAME shown on hover/search -
// exactly the debugging tool needed to eyeball whether an imported model's
// bone hierarchy/naming actually looks right, and to figure out why an
// imported model + animation pairing doesn't match up (e.g. a renamed/
// missing bone the animation's own MotionData never finds a match for - see
// Animation/MotionSampler.h's ResolveBoneTracksToSkeleton() and Game.h's
// PlayAnimationOnEntity() doc comment for the full "matched purely by name"
// story this window is meant to help debug).
//
// As of task_manager/verlet-integration-2 (PHASE3_BONE_VIEWER_VIEW_MODE_DROPDOWN_AND_GIZMO.md),
// this window can also show Rigid Bodies/Joints (m_viewMode, a "Bones /
// Rigid Bodies / Joints" toolbar dropdown) instead of only Bones - loaded
// via the shared ModelRigCache (see ModelRigCache.h) rather than decoding
// RigFileData itself, and every selection interaction (tree row click,
// direct viewport gizmo click) routes through the Editor's single
// gate-keeper, Selection (see Selection.h's SelectModelPart()/
// IsModelPartSelected()), instead of this class's own (now-removed) private
// m_selectedBoneIndex.
//
// Deliberately reads STRAIGHT FROM THE SOURCE *.gta FILE on disk (via
// GtaFile.h/MeshFile.h/RigFile.h, through ModelRigCache for the metadata
// half), the exact same "asset importer" reading path AssetPreviewMesh.h
// already uses for the Inspector's own Project-panel mesh preview - NOT from
// Game's private, path-keyed skinning caches (Game.h's m_meshSkinningCache)
// - so this window has zero dependency on Game's internal caching/
// animation-runtime state at all, only on MeshAssetSource's own recorded
// gtaPath (ECS/Components/MeshAssetSource.h) for whichever entity it's
// currently showing. This ALWAYS shows the model's original BIND POSE
// (identity model matrix, bones at their authored SkeletonData::position -
// see Assets/SkeletonData.h's own doc comment for why that's already in the
// same model-local space as MeshData::positions, needing no extra
// transform) - it deliberately does NOT reflect whatever pose a live
// SkeletalAnimator might currently be posing the SAME entity's GPU mesh into
// (see ECS/Components/SkeletalAnimator.h) - a live posed-skeleton overlay is
// a natural, but separate, follow-up once this static bind-pose view proves
// useful.
//
// Builds its own small VkPipeline/VkPipelineLayout directly (reusing the
// exact same MeshPreview.vert/.frag shader pair + PreviewVertex layout
// AssetPreviewMesh.cpp already uses - both are simple position+normal,
// fixed-direction-lambert previews, just with an independent, user-
// orbitable camera here instead of AssetPreviewMesh's fixed auto-spin one)
// and records its own draw via a Renderer::RenderOffscreen() recordExtra
// callback - the same "an external Vulkan-based rendering backend owned by
// the Editor module" pattern AGENTS.md sanctions (see "Editor Module
// Structure"). Gated behind GTE_ENABLE_PROJECT_PANEL (like AssetPreviewMesh
// itself) purely because MeshPreview.vert/frag are only ever compiled/
// staged under that same switch (see CMakeLists.txt) - nothing about the
// bone-viewing feature ITSELF is Project-panel-specific.
//
// Owns its GPU buffers/RenderTexture/ImGui descriptor/pipeline for as long
// as they're needed - all released by Reset() (called by the destructor,
// and MUST also be called explicitly by ImGuiEditorLayer's destructor
// BEFORE ImGui_ImplVulkan_Shutdown(), same requirement as
// AssetPreviewMesh::Reset()/AssetPreviewTexture::Reset()).
class BoneViewerWindow {
public:
    BoneViewerWindow() = default;
    ~BoneViewerWindow();

    BoneViewerWindow(const BoneViewerWindow&) = delete;
    BoneViewerWindow& operator=(const BoneViewerWindow&) = delete;

    // Opens (or re-targets, if already open) the window onto `rootEntity` -
    // called by the Inspector's "Open Bone Viewer" button
    // (Panels/InspectorPanel.cpp) for whichever entity is currently
    // selected. Does not itself validate `rootEntity` in any way (that
    // happens every frame inside Build(), against whatever the Registry
    // currently says) - opening onto a bad entity just shows a "no mesh
    // asset" message until a valid one is opened instead.
    void Open(Entity rootEntity) noexcept;

    // Builds the floating window - a complete no-op if not currently open
    // (see Open() above / the window's own close button). Called once per
    // frame from ImGuiEditorLayer::BuildUI(), after BuildInspectorPanel().
    // `ctx` is the shared EditorContext (read/written for Selection - see
    // Selection.h) and `rigCache` is the shared ModelRigCache (Culprit A/E,
    // PHASE0_MASTER_STRATEGY.md) both owned by ImGuiEditorLayer, passed by
    // reference exactly like `registry`/`renderer` already are.
    // `physicsSystem` (task_manager/verlet-integration-5, PHASE0_MASTER_STRATEGY.md,
    // Culprit E) is "Verlet" mode's own source of truth - looked up FRESH
    // every Build() call (PhysicsSystem::GetDynamicChainRigCache().TryGet())
    // rather than cached into a new member field, since DynamicChainRigCache
    // is already an in-memory, already-populated, O(1)-lookup map with no
    // on-disk mtime concept to gate a reload against in the first place.
    void Build(Registry& registry, Renderer& renderer, EditorContext& ctx, ModelRigCache& rigCache, PhysicsSystem& physicsSystem);

    // Releases every currently-held GPU resource (vertex/index buffers,
    // RenderTexture, ImGui descriptor, pipeline) - waiting for the GPU to be
    // idle first, same reasoning as AssetPreviewMesh::Reset(). Called by
    // the destructor, and safe to call repeatedly/on an already-empty
    // instance.
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

    VkDevice m_device = VK_NULL_HANDLE;

    // Lazily built on first Build() call, then reused for every
    // subsequently-viewed model - this pipeline's shape never depends on
    // which model is currently loaded.
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

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
    VkDescriptorSet m_descriptor = VK_NULL_HANDLE;
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
