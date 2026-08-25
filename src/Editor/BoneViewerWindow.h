#pragma once

#include "../ECS/Entity.h"
#include "../Math/Vec3.h"

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
// Deliberately reads STRAIGHT FROM THE SOURCE *.gta FILE on disk (via
// GtaFile.h/MeshFile.h/RigFile.h), the exact same "asset importer" reading
// path AssetPreviewMesh.h already uses for the Inspector's own Project-panel
// mesh preview - NOT from Game's private, path-keyed skinning caches
// (Game.h's m_meshSkinningCache) - so this window has zero dependency on
// Game's internal caching/animation-runtime state at all, only on
// MeshAssetSource's own recorded gtaPath (ECS/Components/MeshAssetSource.h)
// for whichever entity it's currently showing. This ALWAYS shows the
// model's original BIND POSE (identity model matrix, bones at their
// authored SkeletonData::position - see Assets/SkeletonData.h's own doc
// comment for why that's already in the same model-local space as
// MeshData::positions, needing no extra transform) - it deliberately does
// NOT reflect whatever pose a live SkeletalAnimator might currently be
// posing the SAME entity's GPU mesh into (see ECS/Components/
// SkeletalAnimator.h) - a live posed-skeleton overlay is a natural, but
// separate, follow-up once this static bind-pose view proves useful.
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
    void Build(Registry& registry, Renderer& renderer);

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

    void EnsurePipeline(Renderer& renderer);
    bool EnsureDataLoaded(Renderer& renderer, const std::string& absoluteGtaPath);
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
    // - a bone whose name contains this (case-insensitively) as a
    // substring is drawn highlighted/always-labeled; every other bone is
    // still drawn as a plain gizmo dot, labeled only on hover.
    char m_searchBuffer[128] = {};

    // When true, every bone's name is drawn permanently instead of only on
    // hover/search-match - handy for a small enough skeleton, toggled via
    // the window's own toolbar checkbox.
    bool m_showAllNames = false;
};

} // namespace gte
