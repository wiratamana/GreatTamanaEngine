#include "EditorLayer.h"

#include "DockLayout.h"
#include "EditorContext.h"
#include "Panels/GamePanel.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/ScenePanel.h"

#include "../Renderer/Renderer.h"
#include "../Window/Window.h"

// Compiled instead of NullEditorLayer.cpp only when GTE_ENABLE_EDITOR is ON
// (see CMakeLists.txt). This file - together with DockLayout.cpp and every
// Panels/*.cpp, which are ALSO only ever compiled under GTE_ENABLE_EDITOR -
// is the deliberate boundary for the Editor: nothing outside src/Editor/
// (Application, Renderer, Game) ever includes an ImGui header or knows
// ImGui exists, exactly like EventTranslator is the deliberate boundary
// object for SDL in the Application layer (see AGENTS.md, "Editor Module
// Structure", for the full rationale behind spreading this boundary across
// several files instead of just this one).
//
// ImGuiEditorLayer itself is the Editor's composition root, not a monolith:
// it owns the ImGui context/SDL3+Vulkan backend lifecycle (this file) and
// the shared EditorContext (m_ctx - see EditorContext.h), while
// BuildUI() below just calls out, in a fixed, deliberate order, to
// BuildDockspaceAndMenuBar() (DockLayout.cpp) and each panel builder
// (Panels/*.cpp) - none of which carry any persistent state of their own;
// they read/write m_ctx instead.
//
// Owns:
//   - the ImGui context itself (docking branch - see cmake/FetchImGui.cmake)
//     with ImGuiConfigFlags_DockingEnable set, so every panel below can be
//     freely dragged/split/tabbed by the user, Unity-style.
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
//       - GamePanel::Build() (Panels/GamePanel.cpp) reads the panel's
//         current ImGui::GetContentRegionAvail() every frame and stores it
//         as m_ctx.desiredExtent.
//       - GameViewTarget() (called earlier next frame, before
//         Game::Render()) compares m_ctx.desiredExtent against the
//         texture's actual current extent and resizes it first if they
//         differ - never mid-frame after this frame's ImGui::Image() call
//         has already been recorded (see GameViewTarget() for why that
//         would be unsafe).
//     This introduces at most one frame of lag between a resize and the
//     texture catching up, imperceptible in practice.
//
// The overall panel layout (built once via DockLayout.cpp's
// BuildDockspaceAndMenuBar()/BuildDefaultDockLayout(), then left entirely to
// imgui.ini / user dragging afterwards) mirrors Unity's default editor: a
// full-viewport DockSpace hosting a top menu bar (File > Exit, ...),
// "Hierarchy" docked left, "Inspector" docked right, and "Scene"/"Game"
// tabbed together in the remaining center - the user can drag the "Scene"
// tab out to split it side-by-side with "Game" (or anywhere else) at any
// time, exactly like Unity's Scene/Game tabs.
//
// IMPORTANT LIMITATION: the engine has no separate editor scene camera yet
// (no Camera component/view-projection matrix at all - see README.md,
// "Rendering") - "Scene" therefore just displays the SAME m_gameView
// texture as "Game" for now (see Panels/ScenePanel.cpp). Only "Game"'s own
// content region drives m_ctx.desiredExtent/GameViewTarget()'s resize, so
// resizing "Scene" alone never resizes the real Game-view RenderTexture.
// Splitting "Scene" out from "Game" today just gives two views of the
// identical image - a real, independently-orbitable Scene camera is a
// natural follow-up once Camera exists as an ECS component.
#include <imgui.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <stdexcept>

namespace gte {

namespace {

class ImGuiEditorLayer final : public IEditorLayer {
public:
    ImGuiEditorLayer(Window& window, Renderer& renderer)
        : m_gameView(renderer.CreateRenderTexture(window.Width(), window.Height()))
    {
        // See EditorContext::desiredExtent for why this is initialized to
        // the OS window's startup size here.
        m_ctx.desiredExtent = VkExtent2D{
            static_cast<std::uint32_t>(window.Width()), static_cast<std::uint32_t>(window.Height()) };

        const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
        m_device = context.device;

        IMGUI_CHECKVERSION();
        m_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_context);
        ImGui::StyleColorsDark();

        // Docking (NOT multi-viewport/platform windows - those stay off:
        // this Editor only docks panels within the one OS window) is what
        // lets Hierarchy/Inspector/Scene/Game be freely rearranged/split -
        // see the class comment above.
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;

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
        // Apply whatever panel size GamePanel::Build() captured LAST frame
        // (Panels/GamePanel.cpp), before Game renders this frame - never
        // mid/after-frame, since that could destroy the VkImage/VkImageView
        // an already-recorded ImGui::Image() draw call (from this same
        // frame) still references. Doing it here, right before Application
        // calls Renderer::RenderOffscreen(), is the earliest point in the
        // frame where it's both needed (Game is about to render into this)
        // and safe (last frame's ImGui draw data was already submitted/
        // executed before this frame started).
        if (m_ctx.desiredExtent.width > 0 && m_ctx.desiredExtent.height > 0) {
            const VkExtent2D current = m_gameView.Extent();
            if (current.width != m_ctx.desiredExtent.width || current.height != m_ctx.desiredExtent.height) {
                // Resizes are rare/user-driven (dragging the panel's
                // border), so a full device stall here is the simplest
                // correct thing - not a per-frame cost. Same reasoning
                // OnWindowResized used to apply for OS window resizes.
                if (m_device != VK_NULL_HANDLE) {
                    vkDeviceWaitIdle(m_device);
                }
                ReleaseGameViewDescriptor();
                m_gameView.Resize(
                    static_cast<int>(m_ctx.desiredExtent.width), static_cast<int>(m_ctx.desiredExtent.height));
                // A new ImGui descriptor for the resized texture is
                // (re)created lazily in BuildUI() the next time it's needed.
            }
        }
        return &m_gameView;
    }

    void BuildUI(Registry& registry) override
    {
        ImGui::SetCurrentContext(m_context);

        BuildDockspaceAndMenuBar(m_ctx);

        // Lazily (re)create the ImGui-side descriptor for the Game view
        // texture - needed on first use, and again after GameViewTarget()
        // invalidated the previous one. Shared by both ScenePanel and
        // GamePanel (see the class comment's "IMPORTANT LIMITATION" note).
        if (m_ctx.gameViewDescriptor == VK_NULL_HANDLE) {
            m_ctx.gameViewDescriptor = ImGui_ImplVulkan_AddTexture(
                m_gameView.Sampler(), m_gameView.View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        BuildHierarchyPanel(registry, m_ctx);
        BuildInspectorPanel(registry, m_ctx);
        BuildScenePanel(m_ctx);
        BuildGamePanel(m_ctx);
    }

    void Render(VkCommandBuffer cmd) override
    {
        ImGui::SetCurrentContext(m_context);
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }

    bool WantsExit() const override { return m_ctx.exitRequested; }

private:
    void ReleaseGameViewDescriptor()
    {
        if (m_ctx.gameViewDescriptor != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(m_ctx.gameViewDescriptor);
            m_ctx.gameViewDescriptor = VK_NULL_HANDLE;
        }
    }

    VkDevice m_device = VK_NULL_HANDLE;
    ImGuiContext* m_context = nullptr;
    RenderTexture m_gameView;

    // Shared state read/written by DockLayout.cpp's
    // BuildDockspaceAndMenuBar() and every Panels/*.cpp builder called from
    // BuildUI() above - see EditorContext.h for what each field means and
    // exactly who reads/writes it.
    EditorContext m_ctx;
};

} // namespace

std::unique_ptr<IEditorLayer> CreateEditorLayer(Window& window, Renderer& renderer)
{
    return std::make_unique<ImGuiEditorLayer>(window, renderer);
}

} // namespace gte
