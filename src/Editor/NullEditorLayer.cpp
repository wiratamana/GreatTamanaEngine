#include "EditorLayer.h"

// Compiled instead of ImGuiEditorLayer.cpp when GTE_ENABLE_EDITOR is OFF
// (see CMakeLists.txt) - this file, and this file alone, is what a
// "final/release game build" links for the Editor seam. It has no SDL,
// Vulkan-beyond-forward-declares, or ImGui dependency whatsoever.

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
    std::optional<rg::TextureHandle> AddBlurValidationPass(rg::RenderGraphBuilder& /*builder*/,
        Renderer& /*renderer*/, rg::TextureHandle /*sceneViewHandle*/, VkExtent2D /*sceneExtent*/) override
    {
        return std::nullopt;
    }
    void FinalizeBlurValidationForSampling(VkCommandBuffer /*cmd*/) override { }
    void RenderSceneGrid(Renderer& /*renderer*/, VkCommandBuffer /*cmd*/, const Mat4& /*sceneViewProjection*/) override { }
    void BuildUI(Game& /*game*/, Renderer& /*renderer*/, const rg::RenderGraph& /*renderGraph*/,
        AtmosphereSettings& /*atmosphereSettings*/, AtmosphereLutRenderer& /*atmosphereLutRenderer*/) override
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
    FrameDebuggerCaptureContext* PrepareFrameDebuggerCaptureContext() override { return nullptr; }
    void NotifyFrameDebuggerStepConsumed() override { }
    bool ConsumePendingFrameDebuggerReplayRequest() override { return false; }

    // task_manager/frame-debugger-3 campaign, PHASE7 - see EditorLayer.h's
    // own doc comments for the full contract; every one of these is a safe,
    // inert no-op for a release build (no Frame Debugger UI/state exists to
    // affect at all).
    void FrameDebuggerOpenWindow() override { }
    void FrameDebuggerSetEnabled(bool /*enabled*/) override { }
    bool FrameDebuggerCaptureNow() override { return false; }
    void FrameDebuggerSelectEvent(int /*index*/) override { }
    bool FrameDebuggerSetChannel(const std::string& /*channel*/) override { return false; }
    void FrameDebuggerSetLevels(float /*black*/, float /*white*/) override { }
    FrameDebuggerStateSnapshotView FrameDebuggerGetState() const override { return FrameDebuggerStateSnapshotView{}; }
};

} // namespace

std::unique_ptr<IEditorLayer> CreateEditorLayer(Window& /*window*/, Renderer& /*renderer*/)
{
    return std::make_unique<NullEditorLayer>();
}

} // namespace gte
