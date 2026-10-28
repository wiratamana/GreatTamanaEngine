#pragma once

#include "EngineContext.h"
#include "IHostServices.h"
#include "InputFrame.h"
#include "ISurfaceProvider.h"
#include "../Application/RenderPassViewData.h"
#include "../ECS/Entity.h"
#include "../Game/Game.h"
#include "../Game/SceneQuery.h"
#include "../Renderer/Culling/ClusterCullMath.h"
#include "../Renderer/MeshHandle.h"
#include "../Renderer/PipelineHandle.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/SceneServicesDescriptorSet.h"
#include "../Renderer/RenderGraph/BoundedOnceSet.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderPipeline.h"
#include "../Renderer/RenderGraph/Shader.h"
#include "ClusterCandidates.h"
#include "FrameDebuggerCaptureRecorder.h"
#include "FeatureKit/ClusterDrawTypes.h"
#include "FeatureKit/View.h"
#include "RenderTimingAdapter.h"
#include "Plugins/ProjectAssemblyHost.h"
#include "../Assets/AssetDatabase.h"
#include "Plugins/ProjectRenderFeatureCallback.h"
#include "Plugins/ProjectPreOpaqueCallback.h"
#include "Plugins/ProjectScenePassCallback.h"
#include "Plugins/RenderFeatureDescriptor.h"
#include "Plugins/UniversalRenderPassCallback.h"
#include "Plugins/RenderViewRegistry.h"

#include <array>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace gte {

// Forward declarations
class IEditorLayer;
class EditorHost;
class IPluginCapabilityOrchestrator;
class RenderFeatureCompositor;
class Mesh;

// Frame timing and draw statistics placeholder.
struct FrameStats {
};

// Main engine facade. Coordinates game updates, offscreen rendering, and frame presentation.
class Core {
public:
    // `forceGpuValidation` turns the GPU API validation on in any build type.
    Core(ISurfaceProvider& surfaceProvider, IHostServices& hostServices, bool forceGpuValidation = false,
        PresentPolicy presentPolicy = PresentPolicy::Capped);

    // Defined out-of-line in Core.cpp to support incomplete types in unique_ptr.
    ~Core();

    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&) = delete;
    Core& operator=(Core&&) = delete;

    // Advances engine time and updates game logic.
    void Update(const InputFrame& input, float deltaTime);

    // Builds and executes offscreen passes (primary and secondary views).
    void BuildFrame();

    // Builds and executes the presentation pass to the swapchain.
    void Present();

    Renderer& GetRenderer() noexcept { return m_renderer; }

    // Shared scene descriptor set (set = 1) for pipelines.
    SceneServicesDescriptorSet& GetSceneServicesDescriptorSet() noexcept { return m_sceneServicesDescriptorSet; }

    Registry& GetRegistry() noexcept { return m_game.GetRegistry(); }
    Game& GetGame() noexcept { return m_game; }
    rg::RenderGraph& GetRenderGraph() noexcept { return m_renderGraph; }
    EngineContext& GetEngineContext() noexcept { return m_engineContext; }
    Time& GetTime() noexcept { return m_engineContext.time; }
    const FrameStats& GetFrameStats() const noexcept { return m_frameStats; }

    // Registry for toggling render passes at runtime.
    rg::RenderPassToggleRegistry& GetRenderPassToggleRegistryMutable() noexcept
    {
        return m_renderPassToggleRegistry;
    }

    // Enables or disables a built-in render pass by name.
    bool SetBuiltInRenderPassEnabled(const std::string& name, bool enabled)
    {
        return m_renderPassToggleRegistry.SetEnabled(name, enabled);
    }

    // Read-only blackboard from the offscreen pass execution (for frame debugger).
    const rg::RenderPassBlackboard& GetOffscreenBlackboardForFrameDebugger() const noexcept
    {
        return m_offscreenBlackboardThisFrame;
    }

    // Primary view render target for this frame (nullptr if the view is inactive).
    RenderTexture* GetPrimaryViewTargetThisFrame() const noexcept { return m_primaryTargetThisFrame; }

    // Texture name of a view's target, as shown to tools. Comes from the editor layer when one is attached.
    const char* ViewResourceName(rg::ViewScope scope) const noexcept;

    // Sets the callback that records UI / ImGui commands during present.
    void SetPresentImGuiRecorder(std::function<void(const rg::RecordingContext&)> recorder)
    {
        m_presentImGuiRecorder = std::move(recorder);
    }

    // Updates cached window size when the OS window is resized.
    void NotifyWindowResized(int width, int height) noexcept
    {
        m_windowWidth = width;
        m_windowHeight = height;
    }

    // Sets the optional editor layer hook (nullptr in standalone builds).
    void SetEditorLayerHook(IEditorLayer* editorLayer) noexcept { m_editorLayer = editorLayer; }

    // Loads compiled project assemblies from disk.
    void LoadProjectAssemblies(const std::filesystem::path& outputDirectory, EditorHost* editorHost);

    // Registers a custom render pass provider from a project assembly.
    void RegisterProjectRenderPassProvider(const char* debugName, rg::ProviderScope scope,
        rg::RenderPassProvider provider, rg::ProviderTiming timing = rg::ProviderTiming::BeforeDeferredPasses);

    // Unregisters a project assembly render pass provider.
    void UnregisterProjectRenderPassProvider(const char* debugName);

    // Registers a custom render feature with the compositor.
    bool RegisterProjectRenderFeature(const char* debugName, RenderFeatureStage stage,
        RenderFeatureBlendMode blendMode, std::int32_t priority, ProjectRenderFeatureCallback callback);

    // Unregisters a custom render feature.
    void UnregisterProjectRenderFeature(const char* debugName);

    // Registers a custom pass executed before opaque geometry.
    bool AddPreOpaquePass(const char* debugName, ProjectPreOpaqueCallback callback, std::int32_t priority = 0);
    void RemovePreOpaquePass(const char* debugName);

    // Registers a custom pass executed after opaque geometry.
    bool AddPostOpaquePass(const char* debugName, ProjectScenePassCallback callback, std::int32_t priority = 0);
    void RemovePostOpaquePass(const char* debugName);

    // Registers a custom pass executed after transparent geometry.
    bool AddPostTransparentPass(const char* debugName, ProjectScenePassCallback callback, std::int32_t priority = 0);
    void RemovePostTransparentPass(const char* debugName);

    // Creates or gets a persistent named render view; colorFormat Undefined == the scene pixel format.
    rg::RenderViewId CreateRenderView(const char* name, std::uint32_t width, std::uint32_t height,
        bool depthOnly = false, bool allowDepthSampledAccess = false, PixelFormat colorFormat = PixelFormat::Undefined);
    RenderTexture* FindRenderViewTarget(rg::RenderViewId view) const noexcept;

    // Resizes a view made by CreateRenderView (waits for the GPU to go idle). False when it failed.
    bool ResizeRenderView(rg::RenderViewId view, std::uint32_t width, std::uint32_t height);

    // Registers a post-processing pass executed after scene composition.
    bool AddScreenPostProcessPass(const char* debugName, ProjectRenderFeatureCallback callback,
        RenderFeatureBlendMode blendMode = RenderFeatureBlendMode::AlphaOver,
        std::optional<std::int32_t> priority = std::nullopt);

    // Registers a built-in engine render pass for a specific event stage.
    bool RegisterRenderPass(FrameEvent event, const char* debugName, std::int32_t priority,
        UniversalRenderPassAction action, RenderFeatureBlendMode blendMode = RenderFeatureBlendMode::AlphaOver);

    // Accesses the dynamic project assembly host.
    ProjectAssemblyHost& GetProjectAssemblyHost() noexcept { return m_projectAssemblyHost; }

    // Accesses the engine asset database.
    AssetDatabase& GetAssetDatabase() noexcept { return m_assetDatabase; }

    // Non-owning pointer to the render feature compositor.
    RenderFeatureCompositor* GetRenderFeatureCompositor() const noexcept
    {
        return m_renderFeatureCompositorPtr;
    }

    // Render target handle and extent of a feature view.
    struct PluginRenderFeatureTargetInfo {
        rg::TextureHandle target;
        Extent2D extent{};
    };

    // Resolves the current view's original render target.
    std::optional<PluginRenderFeatureTargetInfo> FindPluginRenderFeatureTarget(
        const rg::RenderPassFrameContext& frame);

    // Returns camera and target view data for a given render view.
    std::optional<RenderPassViewData> FindRenderPassViewData(rg::RenderViewId view) const noexcept;

    // Same data without a copy; null when the view has none. Valid for the current frame only.
    const RenderPassViewData* FindRenderPassViewDataNoCopy(rg::RenderViewId view) const noexcept
    {
        return FindViewData(view);
    }

    // Marks `texture` as the `scope` view's output for this frame; the editor and HTTP endpoints show it instead
    // of the raw view. `name` is its graph texture name; a later exposure replaces an earlier one.
    void ExposeViewOutput(rg::ViewScope scope, RenderTexture& texture, std::string_view name);

    // Marks `entities` as drawn by a pass chain in `scope`, so this frame's CPU scene draw of that view skips them.
    // For kit verbs only, and only after every pass of the chain was declared (see ClusterClaim.h).
    void ClaimEntities(rg::ViewScope scope, std::span<const Entity> entities);

    // Asks the CPU scene draw of `scope` to skip entities whose padded bounds miss `viewProjection`. False when refused
    // (shared scope, captured primary frame, unusable matrix). The last request of a frame wins. Kit verbs only.
    bool SetSceneDrawCull(rg::ViewScope scope, const Mat4& viewProjection, float paddingFraction);

    // Counts of the last CPU scene draw of `scope`; zeros unless it ran in the previous frame or in `currentFrameIndex`.
    SceneCullCounts LastSceneDrawCullCounts(rg::ViewScope scope, std::uint64_t currentFrameIndex) const noexcept;

    // Camera of the primary view; with no primary view data this frame it is built from the active camera at the last
    // known primary aspect (else `fallbackAspect`). Empty without an active camera or with a non-finite matrix.
    std::optional<fx::View> PrimaryCameraView(float fallbackAspect);

    // True while a frame capture records this frame's CPU draw list.
    bool CaptureArmedThisFrame() const noexcept { return m_captureArmedThisFrame; }

    // Instances of cluster-ready meshes this frame, rebuilt by BuildFrame(). Valid until the next BuildFrame().
    std::span<const fx::ClusterInstance> ClusterCandidates() const noexcept { return m_clusterCandidates; }

    // The mesh behind `token`; null when it is stale or unknown.
    const Mesh* ResolveClusterMesh(fx::MeshToken token);

    // True when the mesh has a cluster table to draw with. A mesh without one gets a one-cluster table built
    // and uploaded once; a failure is remembered and retried only after a growing number of frames.
    // Kit verbs only, while declaring (the upload waits for the GPU).
    bool EnsureClusterTable(fx::MeshToken token, std::uint64_t frameIndex);

    // Forgets the synthesis bookkeeping of a released mesh.
    void ForgetClusterSynthesis(MeshHandle mesh) { m_synthesisBackoff.erase(fx::MeshToken{ mesh.index, mesh.generation }); }

    // Reserves an empty per-frame entity set that the CPU scene draw can skip, and returns its handle.
    // An empty handle means no set is left this frame.
    fx::EntityExclusion ReserveExclusion();

    // Fills a set reserved this frame. Ignores an empty or foreign handle.
    void FillExclusion(fx::EntityExclusion exclusion, std::span<const Entity> entities);

    // Changes whenever BuildFrame() clears the exclusion sets. A pass compares it at execute time to catch a stale set.
    const std::uint64_t* ExclusionEpoch() const noexcept { return &m_exclusionEpoch; }

    // Asks BuildFrame() of the next frame to track entity movement (kClusterFlagMoved). Kit verbs only; call every
    // frame the occlusion pyramid is built.
    void RequestOcclusionTracking() noexcept { m_occlusionRequested = true; }

    // Most exclusion sets one frame can reserve.
    static constexpr std::size_t kMaxExclusionSetsPerFrame = 16;

private:
    // A view output a feature exposed this frame (see ExposeViewOutput()). `name` points into the exposing
    // feature's own storage and is valid for the frame.
    struct ExposedViewOutput {
        rg::ViewScope scope = rg::ViewScope::Shared;
        RenderTexture* texture = nullptr;
        std::string_view name;
    };

    void RegisterBuiltinCapabilityOrchestrators();
    void RegisterOffscreenRenderPipelineProviders();
    void RegisterPresentRenderPipelineProvider();
    RenderTexture* AcquireDirectPrimaryTarget();
    const RenderPassViewData* FindViewData(rg::RenderViewId view) const noexcept;
    const ExposedViewOutput* FindExposedViewOutput(rg::ViewScope scope) const noexcept;
    void BuildClusterCandidates();

    IEditorLayer* m_editorLayer = nullptr; // Optional editor hook.

    Renderer m_renderer;
    SceneServicesDescriptorSet m_sceneServicesDescriptorSet;
    RenderViewRegistry m_renderViewRegistry;
    rg::RenderGraph m_renderGraph;
    Game m_game;
    EngineContext m_engineContext;

    FrameStats m_frameStats;

    int m_windowWidth = 0;
    int m_windowHeight = 0;

    rg::RenderPipeline m_offscreenRenderPipeline;
    rg::RenderPipeline m_presentRenderPipeline;
    rg::RenderPassToggleRegistry m_renderPassToggleRegistry;

    // Detached from the renderer and the graphs in ~Core, before any member is destroyed.
    RenderTimingAdapter m_timingAdapter;

    // Reused every frame to gather the executed passes' GPU records.
    std::vector<GpuPassRecord> m_gpuRecordScratch;

    // Publishes the per-view GPU numbers of the newest synchronous snapshot to the profiler.
    void PublishGpuMetrics(bool primaryActive, bool secondaryActive);

    // Remembers which profiler frame each presented frame belongs to, and hands a late present GPU span to it.
    void TrackPresentedFrame();
    struct PresentedFrameLink {
        std::uint64_t presentedFrame = 0; // 0 = empty
        std::uint64_t profilerFrame = 0;
    };
    std::array<PresentedFrameLink, 4> m_presentedFrameLinks{};
    std::uint64_t m_lastPresentedFrame = 0;
    std::uint64_t m_lastLateGpuFrame = 0;

    std::vector<ExposedViewOutput> m_exposedViewOutputsThisFrame; // Cleared at the start of each BuildFrame().

    // Offscreen blackboard kept alive across the frame for debugger inspection.
    rg::RenderPassBlackboard m_offscreenBlackboardThisFrame;

    // Skinning requests and buffer handles for this frame.
    std::vector<AnimationSystem::GpuSkinningDispatchRequest> m_gpuSkinningRequestsThisFrame;
    std::vector<rg::BufferHandle> m_gpuSkinningHandlesThisFrame;

    // View data for this frame (primary and secondary views).
    std::vector<RenderPassViewData> m_currentViewDataThisFrame;

    IFrameDebuggerCaptureRecorder* m_currentFrameDebuggerCaptureForOffscreenPipeline = nullptr;
    bool m_captureArmedThisFrame = false;

    // Aspect of the primary view in the last frame it had data; 0 = unknown.
    float m_lastPrimaryAspect = 0.0f;

    // Entities drawn by a pass chain instead of the CPU scene draw, per view scope. Cleared at the start of BuildFrame().
    std::array<std::unordered_set<Entity>, rg::kViewScopeCount> m_claimedEntities;

    // Frustum cull request and the counts of its last draw, per view scope. The request is cleared at the start of BuildFrame().
    struct SceneCullResult {
        SceneCullCounts counts;
        std::uint64_t frameIndex = 0;
        bool valid = false; // False until a draw of this scope has stored its counts.
    };
    std::array<std::optional<SceneCullRequest>, rg::kViewScopeCount> m_sceneCullThisFrame;
    std::array<SceneCullResult, rg::kViewScopeCount> m_sceneCullCountsLast;

    // Cluster-ready instances and entity exclusion sets of this frame. Both are cleared at the start of
    // BuildFrame(); a deque keeps every reserved set at a stable address. The epoch grows with each clear.
    std::vector<fx::ClusterInstance> m_clusterCandidates;
    std::deque<std::unordered_set<Entity>> m_exclusionSets;
    std::uint64_t m_exclusionEpoch = 0;

    // Failed one-cluster syntheses per mesh; entries die with their mesh.
    std::unordered_map<fx::MeshToken, SynthesisBackoff, fx::MeshTokenHash> m_synthesisBackoff;

    // Last-frame world matrices for the moved flag. Filled only while a pyramid build asked for it last frame.
    ClusterMovedTracker m_clusterMoved;
    bool m_occlusionRequested = false;

    // Opaque features that broke the pass tag rule; their callbacks no longer run.
    std::unordered_set<std::string> m_faultedOpaqueFeatures;

    // De-duplicates frame-build failure logs (a failing frame repeats every frame).
    rg::BoundedOnceSet<64> m_frameFailureOnce;

    // True once the lost-device line was logged. Frames stop for good after a device loss.
    bool m_lostLogged = false;
    bool StopIfDeviceLost();

    // Present pass state.
    rg::TextureHandle m_swapchainImageThisFrame;
    std::function<void(const rg::RecordingContext&)> m_recordImGuiThisFrame;
    std::function<void(const rg::RecordingContext&)> m_presentImGuiRecorder;

    // Primary target used when no view panel provides one; copied to the swapchain by the present graph.
    std::optional<RenderTexture> m_directPrimaryTarget;
    bool m_directPrimaryTargetUsedThisFrame = false;
    rg::Shader m_presentPrimaryViewShader{ "PresentBlit", rg::ShaderKind::Graphics };

    ProjectAssemblyHost m_projectAssemblyHost;
    AssetDatabase m_assetDatabase;

    std::vector<std::unique_ptr<IPluginCapabilityOrchestrator>> m_capabilityOrchestrators;
    RenderFeatureCompositor* m_renderFeatureCompositorPtr = nullptr;

    RenderTexture* m_primaryTargetThisFrame = nullptr;
};

} // namespace gte
