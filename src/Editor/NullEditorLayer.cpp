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
    void BuildUI(Registry& /*registry*/) override { }
    void Render(VkCommandBuffer /*cmd*/) override { }
    void RenderPlatformWindows() override { }
    bool WantsExit() const override { return false; }
    bool WantsCaptureMouse() const override { return false; }
    bool WantsCaptureKeyboard() const override { return false; }
};

} // namespace

std::unique_ptr<IEditorLayer> CreateEditorLayer(Window& /*window*/, Renderer& /*renderer*/)
{
    return std::make_unique<NullEditorLayer>();
}

} // namespace gte
