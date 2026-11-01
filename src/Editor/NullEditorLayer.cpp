#include "EditorLayer.h"
#include "EditorViewNames.h"

// No-op editor layer for hosts that link gte_core without the Editor module.
// Its factory is CreateNullEditorLayer() so it never collides with CreateEditorLayer() in the same link.

namespace gte {

namespace {

// Inert stand-in: every call is a no-op and PrimaryViewTarget() is null, so the game renders to the swapchain.
class NullEditorLayer final : public IEditorLayer {
public:
    void ProcessEvent(const SDL_Event& /*event*/) override { }
    void OnWindowResized(int /*width*/, int /*height*/) override { }
    void NewFrame() override { }
    RenderTexture* PrimaryViewTarget() override { return nullptr; }
    RenderTexture* SecondaryViewTarget() override { return nullptr; }
    const char* ViewResourceName(rg::ViewScope scope) const noexcept override { return EditorViewResourceName(scope); }
    Mat4 SecondaryViewProjection(float /*aspectWidthOverHeight*/) const override { return Mat4::Identity(); }
    Vec3 SecondaryViewCameraWorldPosition() const override { return Vec3::Zero(); }
    void SetPrimaryViewCompositedTexture(RenderTexture* /*texture*/) override { }
    void SetSecondaryViewCompositedTexture(RenderTexture* /*texture*/) override { }
    void RenderSceneGrid(const rg::RecordingContext& /*cmd*/, const Mat4& /*sceneViewProjection*/) override { }
    void BuildUI(Game& /*game*/, Renderer& /*renderer*/, const rg::RenderGraph& /*renderGraph*/,
        const std::vector<RenderFeatureDebugEntry>& /*renderFeatureEntries*/,
        rg::RenderPassToggleRegistry& /*renderPassToggleRegistry*/,
        RenderFeatureCompositor* /*renderFeatureCompositor*/,
        const rg::RenderPassBlackboard& /*offscreenBlackboard*/,
        DescriptorSetLayoutHandle /*sceneServicesSetLayout*/) override
    {
    }
    void Render(const rg::RecordingContext& /*cmd*/) override { }
    void RenderPlatformWindows() override { }
    CaptureResult CapturePlatformWindow(Renderer& /*renderer*/, const std::string& /*title*/) override
    {
        return { {}, CaptureStatus::NothingToCapture };
    }
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
    void FrameDebuggerNotifyOffscreenFailed() override { }

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

    // No editor build runner in a release build.
    MeshClusterBuildRunner* ClusterBuildRunner() override { return nullptr; }

    // No cluster viewer or Scene camera tools in a release build.
    void OnMeshCloneFinished(const std::string& /*createdPathUtf8*/, bool /*openViewer*/) override { }
    IClusterViewerControl* ClusterViewerControl() override { return nullptr; }
    ISceneCameraControl* SceneCameraControl() override { return nullptr; }
    HizDemoEditorHandles HizDemoHandles() noexcept override { return {}; }

    // No Inspector or dock layout in a release build.
    AssetSelectionResult SelectProjectAsset(const std::string& /*absolutePath*/) override
    {
        AssetSelectionResult result;
        result.editorAvailable = false;
        result.message = "the Editor module is not compiled in";
        return result;
    }

    EntitySelectionResult SelectEntityByName(Game& /*game*/, const std::string& /*name*/) override
    {
        EntitySelectionResult result;
        result.editorAvailable = false;
        result.message = "the Editor module is not compiled in";
        return result;
    }

    InspectorStateView InspectorState() const override
    {
        InspectorStateView view;
        view.editorAvailable = false;
        return view;
    }

    bool InjectMouse(const MouseInjection& /*injection*/) override { return false; }

    HierarchyStateReport HierarchyState(Game& /*game*/, std::uint64_t /*first*/, std::uint64_t /*count*/) const override
    {
        HierarchyStateReport report;
        report.editorAvailable = false;
        return report;
    }

    bool SetWindowOpen(const std::string& /*name*/, bool /*open*/) override { return false; }

    HierarchyActionReport HierarchyAction(Game& /*game*/, const HierarchyActionCommand& /*request*/) override
    {
        HierarchyActionReport report;
        report.editorAvailable = false;
        return report;
    }

    ClusterBuildStartResult InspectorBuildClusters(const InspectorClusterOptions& /*options*/) override
    {
        ClusterBuildStartResult result;
        result.status = ClusterBuildStart::Unavailable;
        result.message = "the Editor module is not compiled in";
        return result;
    }

    SplitViewsLayerResult SetSceneGameSplit(bool /*enabled*/) override
    {
        SplitViewsLayerResult result;
        result.message = "the Editor module is not compiled in";
        return result;
    }
};

} // namespace

std::unique_ptr<IEditorLayer> CreateNullEditorLayer(Window& /*window*/, Renderer& /*renderer*/)
{
    return std::make_unique<NullEditorLayer>();
}

} // namespace gte
