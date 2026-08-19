#include "EditorLayer.h"

#include "../Renderer/Renderer.h"
#include "../Window/Window.h"

// Compiled instead of NullEditorLayer.cpp only when GTE_ENABLE_EDITOR is ON
// (see CMakeLists.txt). This file - and only this file - is allowed to
// include ImGui and SDL headers directly: it is the deliberate boundary
// object for the Editor, exactly like EventTranslator is the deliberate
// boundary object for SDL in the Application layer (see AGENTS.md). Nothing
// outside this file (Application, Renderer, Game) ever includes an ImGui
// header or knows ImGui exists.
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <stdexcept>

namespace gte {

namespace {

// Real, Dear ImGui-backed editor layer. Owns:
//   - the ImGui context itself
//   - the SDL3 platform backend (input) and Vulkan renderer backend
//     (drawing), wired to the exact same device/swapchain format Renderer
//     already uses (see Renderer::GetVulkanContextInfo())
//   - a RenderTexture ("the Game view") that Game's camera renders into
//     each frame instead of the swapchain - see GameViewTarget() - kept in
//     sync with the "Game" ImGui panel's own content-region size (Unity's
//     "Free Aspect" behavior), NOT the OS window's size: resizing the
//     floating/docked Game panel resizes the render target to match
//     exactly, so the whole scene is always visible, however the panel is
//     resized or shaped. Size tracking works like this:
//       - BuildUI() reads the panel's current ImGui::GetContentRegionAvail()
//         every frame and stores it as m_desiredExtent.
//       - GameViewTarget() (called earlier next frame, before Game::Render())
//         compares m_desiredExtent against the texture's actual current
//         extent and resizes it first if they differ - never mid-frame
//         after this frame's ImGui::Image() call has already been recorded
//         (see GameViewTarget() for why that would be unsafe).
//     This introduces at most one frame of lag between a resize and the
//     texture catching up, imperceptible in practice.
class ImGuiEditorLayer final : public IEditorLayer {
public:
    ImGuiEditorLayer(Window& window, Renderer& renderer)
        : m_gameView(renderer.CreateRenderTexture(window.Width(), window.Height()))
        , m_desiredExtent{ static_cast<std::uint32_t>(window.Width()), static_cast<std::uint32_t>(window.Height()) }
    {
        const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
        m_device = context.device;

        IMGUI_CHECKVERSION();
        m_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_context);
        ImGui::StyleColorsDark();

        if (!ImGui_ImplSDL3_InitForVulkan(window.Native())) {
            ImGui::DestroyContext(m_context);
            throw std::runtime_error("ImGui_ImplSDL3_InitForVulkan failed.");
        }

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = context.apiVersion;
        initInfo.Instance = context.instance;
        initInfo.PhysicalDevice = context.physicalDevice;
        initInfo.Device = context.device;
        initInfo.QueueFamily = context.graphicsQueueFamily;
        initInfo.Queue = context.graphicsQueue;
        // Let the backend create/own its own descriptor pool instead of us
        // managing a VkDescriptorPool ourselves - see the DescriptorPoolSize
        // comment in imgui_impl_vulkan.h. Sized generously enough for the
        // font atlas plus our own AddTexture() call below for the Game view.
        initInfo.DescriptorPoolSize = 64;
        initInfo.MinImageCount = context.minImageCount;
        initInfo.ImageCount = context.imageCount;
        initInfo.UseDynamicRendering = true;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
        initInfo.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &context.colorFormat;

        if (!ImGui_ImplVulkan_Init(&initInfo)) {
            ImGui_ImplSDL3_Shutdown();
            ImGui::DestroyContext(m_context);
            throw std::runtime_error("ImGui_ImplVulkan_Init failed.");
        }
    }

    ~ImGuiEditorLayer() override
    {
        ImGui::SetCurrentContext(m_context);

        // Make sure the GPU is done with anything ImGui's Vulkan backend
        // (or our own AddTexture() descriptor) might still be referencing
        // before tearing any of it down.
        if (m_device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_device);
        }

        ReleaseGameViewDescriptor();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(m_context);
    }

    void ProcessEvent(const SDL_Event& event) override
    {
        ImGui::SetCurrentContext(m_context);
        ImGui_ImplSDL3_ProcessEvent(&event);
    }

    void OnWindowResized(int /*width*/, int /*height*/) override
    {
        // The Game-view RenderTexture no longer tracks the OS window's size
        // at all - it tracks the "Game" ImGui panel's own content-region
        // size instead (see the class comment and GameViewTarget() below),
        // so an OS window resize by itself is not a reason to resize it.
        // Kept as a no-op (rather than removed) purely to satisfy
        // IEditorLayer's interface - NullEditorLayer's version already does
        // the same.
    }

    void NewFrame() override
    {
        ImGui::SetCurrentContext(m_context);
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
    }

    RenderTexture* GameViewTarget() override
    {
        // Apply whatever panel size BuildUI() captured LAST frame, before
        // Game renders this frame - never mid/after-frame, since that could
        // destroy the VkImage/VkImageView an already-recorded ImGui::Image()
        // draw call (from this same frame) still references. Doing it here,
        // right before Application calls Renderer::RenderOffscreen(), is the
        // earliest point in the frame where it's both needed (Game is about
        // to render into this) and safe (last frame's ImGui draw data was
        // already submitted/executed before this frame started).
        if (m_desiredExtent.width > 0 && m_desiredExtent.height > 0) {
            const VkExtent2D current = m_gameView.Extent();
            if (current.width != m_desiredExtent.width || current.height != m_desiredExtent.height) {
                // Resizes are rare/user-driven (dragging the panel's
                // border), so a full device stall here is the simplest
                // correct thing - not a per-frame cost. Same reasoning
                // OnWindowResized used to apply for OS window resizes.
                if (m_device != VK_NULL_HANDLE) {
                    vkDeviceWaitIdle(m_device);
                }
                ReleaseGameViewDescriptor();
                m_gameView.Resize(static_cast<int>(m_desiredExtent.width), static_cast<int>(m_desiredExtent.height));
                // A new ImGui descriptor for the resized texture is
                // (re)created lazily in BuildUI() the next time it's needed.
            }
        }
        return &m_gameView;
    }

    void BuildUI() override
    {
        ImGui::SetCurrentContext(m_context);

        // Lazily (re)create the ImGui-side descriptor for the Game view
        // texture - needed on first use, and again after GameViewTarget()
        // invalidated the previous one.
        if (m_gameViewDescriptor == VK_NULL_HANDLE) {
            m_gameViewDescriptor = ImGui_ImplVulkan_AddTexture(
                m_gameView.Sampler(), m_gameView.View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        // No scrollbars: the image is always drawn at exactly the panel's
        // available content size (see below), so it never overflows the
        // panel and a scrollbar would only ever be visual noise.
        ImGui::Begin("Game", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        // The panel's current content-region size, in pixels - this is what
        // the Game-view RenderTexture should become. Stored for
        // GameViewTarget() to apply at the very start of next frame (see
        // its comment for why it can't safely happen mid-frame here).
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x >= 1.0f && avail.y >= 1.0f) {
            m_desiredExtent.width = static_cast<std::uint32_t>(avail.x);
            m_desiredExtent.height = static_cast<std::uint32_t>(avail.y);
        }

        // Always draw at exactly the panel's available size (never the
        // texture's own, possibly one-frame-stale, extent) - this is what
        // makes it behave like Unity's "Free Aspect" Game view: the image
        // fills the panel exactly, whatever size/aspect it's resized to,
        // with no leftover space and nothing overflowing it.
        ImGui::Image(
            static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_gameViewDescriptor)),
            avail);
        ImGui::End();
    }

    void Render(VkCommandBuffer cmd) override
    {
        ImGui::SetCurrentContext(m_context);
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }

private:
    void ReleaseGameViewDescriptor()
    {
        if (m_gameViewDescriptor != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(m_gameViewDescriptor);
            m_gameViewDescriptor = VK_NULL_HANDLE;
        }
    }

    VkDevice m_device = VK_NULL_HANDLE;
    ImGuiContext* m_context = nullptr;
    RenderTexture m_gameView;
    // Size (in pixels) the "Game" panel's content region actually was as of
    // last frame's BuildUI() - what GameViewTarget() resizes m_gameView to
    // at the start of the next frame. Initialized to the OS window's
    // startup size so the very first frame (before BuildUI() has ever run)
    // doesn't see a spurious mismatch against m_gameView's own initial size.
    VkExtent2D m_desiredExtent{};
    VkDescriptorSet m_gameViewDescriptor = VK_NULL_HANDLE;
};

} // namespace

std::unique_ptr<IEditorLayer> CreateEditorLayer(Window& window, Renderer& renderer)
{
    return std::make_unique<ImGuiEditorLayer>(window, renderer);
}

} // namespace gte
