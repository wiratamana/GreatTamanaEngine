#pragma once

#include "EngineContext.h"
#include "IHostServices.h"
#include "InputFrame.h"
#include "ISurfaceProvider.h"
#include "../Application/RenderPassViewData.h"
#include "../ECS/Entity.h"
#include "../Game/Game.h"
#include "../Renderer/Culling/GpuDrivenBatchCache.h"
#include "../Renderer/Culling/GpuDrivenBatchDebugInfo.h"
#include "../Renderer/MeshHandle.h"
#include "../Renderer/PipelineHandle.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/SceneServicesDescriptorSet.h"
#include "../Renderer/RenderGraph/BoundedOnceSet.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/RenderGraph/RenderPipeline.h"
#include "FrameDebuggerCaptureRecorder.h"
#include "Plugins/ProjectAssemblyHost.h"
#include "../Assets/AssetDatabase.h"
#include "Plugins/ProjectRenderFeatureCallback.h"
#include "Plugins/ProjectPreOpaqueCallback.h"
#include "Plugins/ProjectScenePassCallback.h"
#include "Plugins/RenderFeatureDescriptor.h"
#include "Plugins/UniversalRenderPassCallback.h"
#include "Plugins/RenderViewRegistry.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace gte {

// Forward declarations
class IEditorLayer;
class EditorHost;
class IPluginCapabilityOrchestrator;
class RenderFeatureCompositor;

// Frame timing and draw statistics placeholder.
struct FrameStats {
};

// Main engine facade. Coordinates game updates, offscreen rendering, and frame presentation.
class Core {
public:
    Core(ISurfaceProvider& surfaceProvider, IHostServices& hostServices);

    // Defined out-of-line in Core.cpp to support incomplete types in unique_ptr.
    ~Core();

    Core(const Core&) = delete;
    Core& operator=(const Core&) = delete;
    Core(Core&&) = delete;
    Core& operator=(Core&&) = delete;

    // Advances engine time and updates game logic.
    void Update(const InputFrame& input, float deltaTime);

    // Builds and executes offscreen passes (Game and Scene views).
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

    // Culling debug statistics from the previous frame.
    const std::vector<GpuDrivenBatchDebugInfo>& GetGpuDrivenBatchDebugInfo() const noexcept
    {
        return m_gpuDrivenBatchDebugInfoLastFrame;
    }

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

    // Game view render target for this frame (nullptr if game view is inactive).
    RenderTexture* GetGameViewTargetThisFrame() const noexcept { return m_gameTargetThisFrame; }

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

    // Creates or gets a persistent named render view; colorFormat Undefined matches the renderer's color format.
    rg::RenderViewId CreateRenderView(const char* name, std::uint32_t width, std::uint32_t height,
        bool depthOnly = false, bool allowDepthSampledAccess = false, PixelFormat colorFormat = PixelFormat::Undefined);
    RenderTexture* FindRenderViewTarget(rg::RenderViewId view) const noexcept;

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

    // Marks `texture` as the output of the `scope` view for this frame. The editor panel and the HTTP endpoints
    // show it instead of the raw view; it is finalized for sampling once the offscreen graph ran.
    // `name` is the graph texture name it was imported under.
    void ExposeViewOutput(rg::ViewScope scope, RenderTexture& texture, std::string_view name);

    // Registers a pass name to be counted in per-view GPU draw stats.
    bool RegisterViewContentPassName(const char* name);

private:
    // Render data for a GPU-driven indirect draw batch.
    struct GpuDrivenBatchRenderData {
        MeshHandle mesh;
        PipelineHandle originalPipeline;
        std::size_t instanceCount = 0;
        rg::BufferHandle inputHandle;
        rg::BufferHandle indirectHandle;
        rg::BufferHandle countHandle;
        rg::BufferHandle readbackHandle;
        DescriptorSetHandle cullingDescriptorSet = DescriptorSetHandle::Invalid;
        DescriptorSetHandle instanceBufferDescriptorSet = DescriptorSetHandle::Invalid;
        const char* resetPassName = nullptr;
        const char* cullingPassName = nullptr;
        const char* indirectDrawPassName = nullptr;
        const char* readbackPassName = nullptr;
        const char* displayName = nullptr;
        std::vector<Entity> entities; // Entities included in this indirect batch.
    };

    // A view output a feature exposed this frame (see ExposeViewOutput()).
    struct ExposedViewOutput {
        rg::ViewScope scope = rg::ViewScope::Shared;
        RenderTexture* texture = nullptr;
        std::string name;
    };

    void RegisterBuiltinCapabilityOrchestrators();
    void RegisterOffscreenRenderPipelineProviders();
    void RegisterPresentRenderPipelineProvider();
    const RenderPassViewData* FindViewData(rg::RenderViewId view) const noexcept;
    const ExposedViewOutput* FindExposedViewOutput(rg::ViewScope scope) const noexcept;

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

    std::vector<ExposedViewOutput> m_exposedViewOutputsThisFrame; // Cleared at the start of each BuildFrame().

    std::vector<std::string> m_viewContentPassNames{ "RenderOpaque", "RenderTransparent" };

    // Offscreen blackboard kept alive across the frame for debugger inspection.
    rg::RenderPassBlackboard m_offscreenBlackboardThisFrame;

    // Skinning requests and buffer handles for this frame.
    std::vector<AnimationSystem::GpuSkinningDispatchRequest> m_gpuSkinningRequestsThisFrame;
    std::vector<rg::BufferHandle> m_gpuSkinningHandlesThisFrame;

    // View data for this frame (Game and Scene views).
    std::vector<RenderPassViewData> m_currentViewDataThisFrame;

    IFrameDebuggerCaptureRecorder* m_currentFrameDebuggerCaptureForOffscreenPipeline = nullptr;

    // GPU-driven culling cache and frame batch data.
    GpuDrivenBatchCache m_gpuDrivenBatchCache;
    std::vector<GpuDrivenBatchRenderData> m_gpuDrivenBatchesThisFrame;
    std::unordered_set<Entity> m_gpuDrivenBatchedEntitiesThisFrame;
    Mat4 m_gpuDrivenGameViewProjectionThisFrame;
    std::vector<GpuDrivenBatchDebugInfo> m_gpuDrivenBatchDebugInfoLastFrame;

    // De-duplicates frame-build failure logs (a failing frame repeats every frame).
    rg::BoundedOnceSet<64> m_frameFailureOnce;

    // Present pass state.
    bool m_needsDirectGameRenderThisFrame = false;
    std::optional<float> m_directGameRenderAspectThisFrame;
    rg::TextureHandle m_swapchainImageThisFrame;
    std::function<void(const rg::RecordingContext&)> m_recordImGuiThisFrame;
    std::function<void(const rg::RecordingContext&)> m_presentImGuiRecorder;

    ProjectAssemblyHost m_projectAssemblyHost;
    AssetDatabase m_assetDatabase;

    std::vector<std::unique_ptr<IPluginCapabilityOrchestrator>> m_capabilityOrchestrators;
    RenderFeatureCompositor* m_renderFeatureCompositorPtr = nullptr;

    RenderTexture* m_gameTargetThisFrame = nullptr;
    RenderTexture* m_sceneTargetThisFrame = nullptr;
};

} // namespace gte
