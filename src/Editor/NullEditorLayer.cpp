#include "EditorLayer.h"

// No-op editor layer for hosts that link gte_core without the Editor module.
// Its factory is CreateNullEditorLayer() so it never collides with CreateEditorLayer() in the same link.

namespace gte {

namespace {

// Inert stand-in: every call is a no-op and GameViewTarget() is null, so the game renders to the swapchain.
class NullEditorLayer final : public IEditorLayer {
public:
    void ProcessEvent(const SDL_Event& /*event*/) override { }
    void OnWindowResized(int /*width*/, int /*height*/) override { }
    void NewFrame() override { }
    RenderTexture* GameViewTarget() override { return nullptr; }
    RenderTexture* SceneViewTarget() override { return nullptr; }
    Mat4 SceneViewProjection(float /*aspectWidthOverHeight*/) const override { return Mat4::Identity(); }
    Vec3 SceneViewCameraWorldPosition() const override { return Vec3::Zero(); }
    void SetGameViewCompositedTexture(RenderTexture* /*texture*/) override { }
    void SetSceneViewCompositedTexture(RenderTexture* /*texture*/) override { }
    void RenderSceneGrid(const rg::RecordingContext& /*cmd*/, const Mat4& /*sceneViewProjection*/) override { }
    void BuildUI(Game& /*game*/, Renderer& /*renderer*/, const rg::RenderGraph& /*renderGraph*/,
        const std::vector<GpuDrivenBatchDebugInfo>& /*gpuDrivenBatchDebugInfo*/,
        const std::vector<RenderFeatureDebugEntry>& /*renderFeatureEntries*/,
        rg::RenderPassToggleRegistry& /*renderPassToggleRegistry*/,
        RenderFeatureCompositor* /*renderFeatureCompositor*/,
        const rg::RenderPassBlackboard& /*offscreenBlackboard*/,
        DescriptorSetLayoutHandle /*sceneServicesSetLayout*/) override
    {
    }
    void Render(const rg::RecordingContext& /*cmd*/) override { }
    void RenderPlatformWindows() override { }
    CapturedPixels CapturePlatformWindow(Renderer& /*renderer*/, const std::string& /*title*/) override { return {}; }
    bool WantsExit() const override { return false; }
    bool WantsCaptureMouse() const override { return false; }
    bool WantsCaptureKeyboard() const override { return false; }
    bool IsPlaybackPaused() const override { return false; }
    bool TryConsumeStepRequest() override { return false; }
    TabActivationResult ActivateTab(const std::string& /*panelName*/) override { return TabActivationResult{}; }

    // A release build has no "Project" panel, so import is always unavailable.
    ProjectAssetImportResult ImportExternalAssetIntoProject(
        const std::string& /*sourceAbsolutePath*/, const std::string& /*destinationRelativeFolder*/) override
    {
        ProjectAssetImportResult result;
        result.projectAvailable = false;
        return result;
    }
    IFrameDebuggerCaptureRecorder* PrepareFrameDebuggerCaptureContext() override { return nullptr; }
    void NotifyFrameDebuggerStepConsumed() override { }
    bool ConsumePendingFrameDebuggerReplayRequest() override { return false; }

    // No Frame Debugger UI or state exists in a release build, so these are inert no-ops.
    void FrameDebuggerOpenWindow() override { }
    void FrameDebuggerSetEnabled(bool /*enabled*/) override { }

    // No "New Project..." window in a release build.
    void SetProjectLifecycleCapability(IProjectLifecycleCapability* /*capability*/) override { }

    // No "Create New Asset" window in a release build.
    void SetAssetScaffoldingCapability(IAssetScaffoldingCapability* /*capability*/) override { }

    // No menu bar in a release build to wire this into.
    void SetHotReloadDebugCapability(IHotReloadDebugCapability* /*capability*/) override { }
    bool FrameDebuggerCaptureNow() override { return false; }
    void FrameDebuggerSelectEvent(int /*index*/) override { }
    bool FrameDebuggerSetChannel(const std::string& /*channel*/) override { return false; }
    void FrameDebuggerSetLevels(float /*black*/, float /*white*/) override { }
    FrameDebuggerStateSnapshotView FrameDebuggerGetState() const override { return FrameDebuggerStateSnapshotView{}; }
    FrameDebuggerEventSink* FrameDebuggerGetEventSinkForInstall() override { return nullptr; }
    FrameDebuggerEventTextureResult FrameDebuggerGetEventTexture(int /*eventIndex*/) override
    {
        return FrameDebuggerEventTextureResult{};
    }

    // No Editor-only spawn tooling in a release build.
    InstanceGridSpawnLayerResult SpawnInstanceGrid(Game& /*game*/, Renderer& /*renderer*/,
        std::uint32_t /*instanceCount*/, float /*spacing*/, DescriptorSetLayoutHandle /*sceneServicesSetLayout*/) override
    {
        InstanceGridSpawnLayerResult result;
        result.success = false;
        result.editorAvailable = false;
        result.errorMessage = "instance grid spawning is not available in this build (the Editor module is not compiled in)";
        return result;
    }

    // A release/Player build has no Editor panels.
    void AttachBuiltinFeatureModules(const std::vector<std::unique_ptr<IEngineFeatureModule>>& /*modules*/,
        Renderer& /*renderer*/, const rg::RenderGraph& /*renderGraph*/) override
    {
    }

    bool SetRenderGraphDisplayedRegime(bool /*present*/) override { return false; }

    // No editor build runner or Project database in a release build.
    MeshClusterBuildRunner* ClusterBuildRunner() override { return nullptr; }
    void RefreshProjectAssetRecord(const std::string& /*absoluteGtaPath*/) override { }
};

} // namespace

std::unique_ptr<IEditorLayer> CreateNullEditorLayer(Window& /*window*/, Renderer& /*renderer*/)
{
    return std::make_unique<NullEditorLayer>();
}

} // namespace gte
