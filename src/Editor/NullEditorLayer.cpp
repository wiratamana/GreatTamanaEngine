#include "EditorLayer.h"

// editor-core-separation-1 campaign, PHASE9 (PHASE9_CMAKE_TARGET_SPLIT.md,
// Locked Design Decision #9) - lives in gte_core's own unconditional source
// list (never gte_editor's), always compiled alongside ImGuiEditorLayer.cpp
// (gte_editor-only, ALWAYS built per Rule 4) into this repo's own single
// executable. Its own factory function is named CreateNullEditorLayer() -
// deliberately NOT CreateEditorLayer() - so both files' factory functions
// coexist in the same final link with zero ODR conflict (previously safe
// only because exactly one of the two ever compiled into any given build,
// selected by the old GTE_ENABLE_EDITOR on/off switch - a precondition that
// stopped holding the moment gte_editor became unconditionally, always
// linked). Nothing in THIS repo ever calls CreateNullEditorLayer() - it
// exists purely so a future Player host (linking gte_core alone, never
// seeing gte_editor's source) has something to call for a no-op Editor
// implementation. It has no SDL, Vulkan-beyond-forward-declares, or ImGui
// dependency whatsoever.

namespace gte {

namespace {

// Inert stand-in: every call is a no-op, and GameViewTarget() always
// returns nullptr so Application/Game render straight to the swapchain,
// fullscreen - exactly as if no Editor existed at all.
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
    void RenderSceneGrid(Renderer& /*renderer*/, VkCommandBuffer /*cmd*/, const Mat4& /*sceneViewProjection*/) override { }
    void BuildUI(Game& /*game*/, Renderer& /*renderer*/, const rg::RenderGraph& /*renderGraph*/,
        const std::vector<GpuDrivenBatchDebugInfo>& /*gpuDrivenBatchDebugInfo*/,
        const std::vector<RenderFeatureDebugEntry>& /*renderFeatureEntries*/,
        rg::RenderPassToggleRegistry& /*renderPassToggleRegistry*/,
        RenderFeatureCompositor* /*renderFeatureCompositor*/,
        const rg::RenderPassBlackboard& /*offscreenBlackboard*/) override // editor-core-separation-22, PHASE6.
    {
    }
    void Render(VkCommandBuffer /*cmd*/) override { }
    void RenderPlatformWindows() override { }
    bool WantsExit() const override { return false; }
    bool WantsCaptureMouse() const override { return false; }
    bool WantsCaptureKeyboard() const override { return false; }
    bool IsPlaybackPaused() const override { return false; }
    bool TryConsumeStepRequest() override { return false; }
    TabActivationResult ActivateTab(const std::string& /*panelName*/) override { return TabActivationResult{}; }

    // task_manager/stl-parser-2, PHASE1 - a release build has no "Project"
    // panel at all, so this is always unavailable.
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

    // task_manager/frame-debugger-3 campaign, PHASE7 - see EditorLayer.h's
    // own doc comments for the full contract; every one of these is a safe,
    // inert no-op for a release build (no Frame Debugger UI/state exists to
    // affect at all).
    void FrameDebuggerOpenWindow() override { }
    void FrameDebuggerSetEnabled(bool /*enabled*/) override { }

    // editor-core-separation-16 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 2), PHASE4 - a release build has no "New Project..." window
    // to give a capability to at all - see IEditorLayer::
    // SetProjectLifecycleCapability()'s own doc comment for the full
    // contract.
    void SetProjectLifecycleCapability(IProjectLifecycleCapability* /*capability*/) override { }

    // editor-core-separation-18 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 4), PHASE3 - a release build has no "Create New Asset" window
    // to give a capability to at all, mirroring
    // SetProjectLifecycleCapability() immediately above.
    void SetAssetScaffoldingCapability(IAssetScaffoldingCapability* /*capability*/) override { }

    // editor-core-separation-19 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 5), PHASE1 - a release build has no menu bar to wire this
    // into at all, mirroring SetAssetScaffoldingCapability() immediately
    // above.
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

    // GPU-Driven Frustum Culling + Indirect Draw campaign (render-pass-5),
    // PHASE6 - a release build has no Editor-only validation spawn tooling
    // at all, mirroring ImportExternalAssetIntoProject()'s own "not
    // available" precedent above.
    GpuDrivenTestBatchSpawnResult SpawnGpuDrivenTestBatch(
        Game& /*game*/, Renderer& /*renderer*/, std::uint32_t /*instanceCount*/) override
    {
        GpuDrivenTestBatchSpawnResult result;
        result.success = false;
        result.editorAvailable = false;
        result.errorMessage = "GPU-driven test batch spawning is not available in this build (the Editor module is not compiled in)";
        return result;
    }

    // A release/Player build has no Editor panels.
    void AttachBuiltinFeatureModules(const std::vector<std::unique_ptr<IEngineFeatureModule>>& /*modules*/,
        Renderer& /*renderer*/, const rg::RenderGraph& /*renderGraph*/) override
    {
    }

    void SetRenderGraphDisplayedRegime(bool /*present*/) override { }
};

} // namespace

std::unique_ptr<IEditorLayer> CreateNullEditorLayer(Window& /*window*/, Renderer& /*renderer*/)
{
    return std::make_unique<NullEditorLayer>();
}

} // namespace gte
