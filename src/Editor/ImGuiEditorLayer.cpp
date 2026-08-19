#include "EditorLayer.h"

#include "../ECS/Components/MeshRenderer.h"
#include "../ECS/Components/Transform.h"
#include "../Renderer/Renderer.h"
#include "../Window/Window.h"

// Compiled instead of NullEditorLayer.cpp only when GTE_ENABLE_EDITOR is ON
// (see CMakeLists.txt). This file - and only this file - is allowed to
// include ImGui and SDL headers directly: it is the deliberate boundary
// object for the Editor, exactly like EventTranslator is the deliberate
// boundary object for SDL in the Application layer (see AGENTS.md). Nothing
// outside this file (Application, Renderer, Game) ever includes an ImGui
// header or knows ImGui exists.
//
// imgui_internal.h is needed (only here) for the DockBuilder* API used to
// lay out the default Unity-style panel arrangement below - it is not part
// of ImGui's stable public API, but building a default dock layout
// programmatically has no supported alternative in Dear ImGui today.
#include <imgui.h>
#include <imgui_internal.h>
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace gte {

namespace {

// Real, Dear ImGui-backed editor layer. Owns:
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
//       - BuildGamePanel() reads the panel's current
//         ImGui::GetContentRegionAvail() every frame and stores it as
//         m_desiredExtent.
//       - GameViewTarget() (called earlier next frame, before Game::Render())
//         compares m_desiredExtent against the texture's actual current
//         extent and resizes it first if they differ - never mid-frame
//         after this frame's ImGui::Image() call has already been recorded
//         (see GameViewTarget() for why that would be unsafe).
//     This introduces at most one frame of lag between a resize and the
//     texture catching up, imperceptible in practice.
//
// The overall panel layout (built once via BuildDefaultDockLayout(), then
// left entirely to imgui.ini / user dragging afterwards) mirrors Unity's
// default editor: a full-viewport DockSpace hosting a top menu bar (File >
// Exit, ...), "Hierarchy" docked left, "Inspector" docked right, and
// "Scene"/"Game" tabbed together in the remaining center - the user can drag
// the "Scene" tab out to split it side-by-side with "Game" (or anywhere
// else) at any time, exactly like Unity's Scene/Game tabs.
//
// IMPORTANT LIMITATION: the engine has no separate editor scene camera yet
// (no Camera component/view-projection matrix at all - see README.md,
// "Rendering") - "Scene" therefore just displays the SAME m_gameView texture
// as "Game" for now (see BuildScenePanel()). Only "Game"'s own content
// region drives m_desiredExtent/GameViewTarget()'s resize, so resizing
// "Scene" alone never resizes the real Game-view RenderTexture. Splitting
// "Scene" out from "Game" today just gives two views of the identical
// image - a real, independently-orbitable Scene camera is a natural
// follow-up once Camera exists as an ECS component.
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
        // Apply whatever panel size BuildGamePanel() captured LAST frame,
        // before Game renders this frame - never mid/after-frame, since
        // that could destroy the VkImage/VkImageView an already-recorded
        // ImGui::Image() draw call (from this same frame) still
        // references. Doing it here, right before Application calls
        // Renderer::RenderOffscreen(), is the earliest point in the frame
        // where it's both needed (Game is about to render into this) and
        // safe (last frame's ImGui draw data was already submitted/
        // executed before this frame started).
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

    void BuildUI(Registry& registry) override
    {
        ImGui::SetCurrentContext(m_context);

        BuildDockspaceAndMenuBar();

        // Lazily (re)create the ImGui-side descriptor for the Game view
        // texture - needed on first use, and again after GameViewTarget()
        // invalidated the previous one. Shared by both "Scene" and "Game"
        // (see the class comment's "IMPORTANT LIMITATION" note).
        if (m_gameViewDescriptor == VK_NULL_HANDLE) {
            m_gameViewDescriptor = ImGui_ImplVulkan_AddTexture(
                m_gameView.Sampler(), m_gameView.View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        BuildHierarchyPanel(registry);
        BuildInspectorPanel(registry);
        BuildScenePanel();
        BuildGamePanel();
    }

    void Render(VkCommandBuffer cmd) override
    {
        ImGui::SetCurrentContext(m_context);
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);
    }

    bool WantsExit() const override { return m_exitRequested; }

private:
    void ReleaseGameViewDescriptor()
    {
        if (m_gameViewDescriptor != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(m_gameViewDescriptor);
            m_gameViewDescriptor = VK_NULL_HANDLE;
        }
    }

    // Hosts a full-viewport, invisible window carrying the top menu bar
    // (File > Exit, ...) and the DockSpace every other panel below docks
    // into. Building the default Hierarchy/Inspector/Scene+Game layout is a
    // ONE-SHOT repair, latched by m_dockLayoutEnsured (see
    // DefaultDockLayoutIsNeeded() below) - once every one of
    // these panels is confirmed to have a real dock (whether from this
    // default layout or an already-valid saved imgui.ini), this NEVER
    // touches the docking system again for the rest of the process.
    //
    // This one-shot-ness is not just an optimization - it's required for
    // correctness. Dragging a tab to split/detach it (e.g. pulling "Scene"
    // away from "Game") makes Dear ImGui briefly report that window's
    // DockId as 0 WHILE THE DRAG IS STILL IN PROGRESS, before the drop
    // target is chosen - a naive "rebuild the default layout whenever any
    // of our panels has DockId == 0" check run every frame would catch
    // exactly that transient mid-drag state and immediately call
    // DockBuilderRemoveNode() + redock everything back to the default
    // layout, cancelling the user's drag before they can ever complete it.
    // That made splitting/undocking ANY of these panels look completely
    // impossible. Checking only until the layout is confirmed once (then
    // never again) fixes this while still auto-repairing a stale
    // imgui.ini saved by an older build of this engine, from before these
    // panels existed (which would otherwise leave them as permanent tiny
    // undocked floating windows).
    void BuildDockspaceAndMenuBar()
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(viewport->WorkPos);
        ImGui::SetNextWindowSize(viewport->WorkSize);
        ImGui::SetNextWindowViewport(viewport->ID);

        constexpr ImGuiWindowFlags hostFlags = ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoTitleBar
            | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove
            | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus | ImGuiWindowFlags_NoBackground
            | ImGuiWindowFlags_MenuBar;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
        ImGui::Begin("EditorDockSpaceHost", nullptr, hostFlags);
        ImGui::PopStyleVar(3);

        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                // Simple example of a menu item that exits the application
                // programmatically: sets a flag Application::Run() checks
                // once per frame (see IEditorLayer::WantsExit()) rather than
                // calling exit()/SDL_Quit() directly here, so shutdown still
                // goes through Application's normal RAII teardown.
                if (ImGui::MenuItem("Exit")) {
                    m_exitRequested = true;
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenuBar();
        }

        const ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");
        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_PassthruCentralNode);

        // See the class-level comment above for why this whole block only
        // ever runs until m_dockLayoutEnsured latches true, never again
        // after that - this is what lets the user freely drag/split/
        // undock any panel afterwards without ever fighting this code.
        if (!m_dockLayoutEnsured) {
            bool allPanelsAccountedFor = true;
            for (const char* panelName : { "Hierarchy", "Inspector", "Scene", "Game" }) {
                // A panel that has never called Begin() yet this session
                // (e.g. this is the very first frame ever, before this same
                // BuildUI() call reaches BuildHierarchyPanel()/etc.) doesn't
                // exist as an ImGuiWindow yet - we can't yet be sure whether
                // it'll end up docked or not, so don't latch "ensured" on
                // this frame; just wait and check again next frame instead.
                if (ImGui::FindWindowByName(panelName) == nullptr) {
                    allPanelsAccountedFor = false;
                    break;
                }
            }

            if (DefaultDockLayoutIsNeeded(dockspaceId)) {
                BuildDefaultDockLayout(dockspaceId, viewport->WorkSize);
                m_dockLayoutEnsured = true; // We just fixed it ourselves - trust it, never recheck.
            } else if (allPanelsAccountedFor) {
                // Nothing needed fixing AND we've actually observed a real,
                // live window for all four panels with a real dock already
                // (e.g. a valid saved imgui.ini) - safe to stop checking
                // forever.
                m_dockLayoutEnsured = true;
            }
            // else: not enough information yet (some panel hasn't had its
            // first Begin() this session) - leave m_dockLayoutEnsured false
            // and re-evaluate next frame.
        }

        ImGui::End();
    }

    // True if the dockspace node itself doesn't exist yet, OR if any of our
    // four panels currently exists as a window but has never actually been
    // docked (DockId == 0) - see BuildDockspaceAndMenuBar()'s comment for
    // why this is checked only until latched, never on every frame forever.
    static bool DefaultDockLayoutIsNeeded(ImGuiID dockspaceId)
    {
        if (ImGui::DockBuilderGetNode(dockspaceId) == nullptr) {
            return true;
        }
        for (const char* panelName : { "Hierarchy", "Inspector", "Scene", "Game" }) {
            const ImGuiWindow* window = ImGui::FindWindowByName(panelName);
            if (window != nullptr && window->DockId == 0) {
                return true;
            }
        }
        return false;
    }

    // Unity-style default arrangement, built exactly once (see caller):
    //   +----------+-------------------------------+----------+
    //   |          |         (menu bar)             |          |
    //   | Hierarchy|      Scene | Game (tabs)        | Inspector|
    //   |  (left)  |         (center)                |  (right) |
    //   +----------+-------------------------------+----------+
    // "Scene" and "Game" are docked into the SAME center node (as tabs) -
    // the user can drag the "Scene" tab out to split it away from "Game"
    // at any time afterwards (see class comment).
    static void BuildDefaultDockLayout(ImGuiID dockspaceId, ImVec2 size)
    {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
        ImGui::DockBuilderSetNodeSize(dockspaceId, size);

        ImGuiID center = dockspaceId;
        const ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f, nullptr, &center);
        const ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);

        ImGui::DockBuilderDockWindow("Hierarchy", left);
        ImGui::DockBuilderDockWindow("Inspector", right);
        ImGui::DockBuilderDockWindow("Scene", center);
        ImGui::DockBuilderDockWindow("Game", center);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    // Lists every entity that has a Transform (this engine's ECS has no
    // separate "GameObject"/name concept yet - see ECS/Components/
    // Transform.h - so a Transform is the closest thing to "something that
    // belongs in the Hierarchy", the same role it plays in Unity). Clicking
    // an entry selects it for the Inspector panel below.
    void BuildHierarchyPanel(Registry& registry)
    {
        ImGui::Begin("Hierarchy");

        ComponentStorage<Transform>& transforms = registry.Storage<Transform>();
        for (std::size_t i = 0; i < transforms.Size(); ++i) {
            const Entity entity = transforms.EntityAt(i);

            char label[32];
            std::snprintf(label, sizeof(label), "Entity %u", entity.index);

            const bool isSelected = (entity == m_selectedEntity);
            if (ImGui::Selectable(label, isSelected)) {
                m_selectedEntity = entity;
            }
        }

        if (transforms.Size() == 0) {
            ImGui::TextDisabled("(no entities)");
        }

        ImGui::End();
    }

    // Shows/edits the Hierarchy panel's currently-selected entity's
    // components - Transform fields are directly editable (position/scale
    // as-is, rotation shown/edited as Euler degrees and converted back to
    // the stored Quat - see Math/Quat.h's FromEulerDegrees()/
    // ToEulerDegrees()); MeshRenderer's handles are shown read-only (no
    // asset-picker UI exists yet to let a user reassign them).
    void BuildInspectorPanel(Registry& registry)
    {
        ImGui::Begin("Inspector");

        if (!registry.IsAlive(m_selectedEntity)) {
            ImGui::TextDisabled("No entity selected.");
            ImGui::End();
            return;
        }

        ImGui::Text("Entity %u (generation %u)", m_selectedEntity.index, m_selectedEntity.generation);
        ImGui::Separator();

        if (Transform* transform = registry.TryGetComponent<Transform>(m_selectedEntity)) {
            if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::DragFloat3("Position", &transform->position.x, 0.01f);

                Vec3 eulerDegrees = transform->rotation.ToEulerDegrees();
                if (ImGui::DragFloat3("Rotation", &eulerDegrees.x, 0.1f)) {
                    transform->rotation = Quat::FromEulerDegrees(eulerDegrees.x, eulerDegrees.y, eulerDegrees.z);
                }

                ImGui::DragFloat3("Scale", &transform->scale.x, 0.01f);
            }
        }

        if (MeshRenderer* meshRenderer = registry.TryGetComponent<MeshRenderer>(m_selectedEntity)) {
            if (ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
                ImGui::BeginDisabled();
                ImGui::Text("Mesh handle:     index %u, generation %u",
                    meshRenderer->mesh.index, meshRenderer->mesh.generation);
                ImGui::Text("Pipeline handle: index %u, generation %u",
                    meshRenderer->pipeline.index, meshRenderer->pipeline.generation);
                ImGui::EndDisabled();
            }
        }

        ImGui::End();
    }

    // Placeholder Scene view - see the class comment's "IMPORTANT
    // LIMITATION": until the engine has a real editor scene camera, this
    // just displays the exact same texture as "Game". Deliberately does
    // NOT touch m_desiredExtent (only BuildGamePanel() does), so resizing
    // this panel alone never resizes the actual Game-view RenderTexture.
    void BuildScenePanel()
    {
        ImGui::Begin("Scene", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x >= 1.0f && avail.y >= 1.0f) {
            ImGui::Image(static_cast<ImTextureID>(reinterpret_cast<intptr_t>(m_gameViewDescriptor)), avail);
        }

        ImGui::End();
    }

    // The real Game view - drives m_desiredExtent (see GameViewTarget()),
    // i.e. the actual Game-view RenderTexture always matches exactly this
    // panel's own current size/shape (Unity's "Free Aspect" behavior),
    // whatever "Scene" (or any other panel) happens to be sized at.
    void BuildGamePanel()
    {
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

    VkDevice m_device = VK_NULL_HANDLE;
    ImGuiContext* m_context = nullptr;
    RenderTexture m_gameView;
    // Size (in pixels) the "Game" panel's content region actually was as of
    // last frame's BuildGamePanel() - what GameViewTarget() resizes
    // m_gameView to at the start of the next frame. Initialized to the OS
    // window's startup size so the very first frame (before BuildUI() has
    // ever run) doesn't see a spurious mismatch against m_gameView's own
    // initial size.
    VkExtent2D m_desiredExtent{};
    VkDescriptorSet m_gameViewDescriptor = VK_NULL_HANDLE;

    // Latches true the first time every one of Hierarchy/Inspector/Scene/
    // Game is confirmed to have a real dock (see BuildDockspaceAndMenuBar()'s
    // comment) - once true, BuildDockspaceAndMenuBar() never touches the
    // docking system again for the rest of the process, which is what lets
    // the user freely drag/split/undock any panel afterwards.
    bool m_dockLayoutEnsured = false;

    // Hierarchy/Inspector selection state - kInvalidEntity means "nothing
    // selected", shown by the Inspector as "No entity selected."
    Entity m_selectedEntity = kInvalidEntity;

    // Set by File > Exit (see BuildDockspaceAndMenuBar()); read once per
    // frame by Application::Run() via WantsExit().
    bool m_exitRequested = false;
};

} // namespace

std::unique_ptr<IEditorLayer> CreateEditorLayer(Window& window, Renderer& renderer)
{
    return std::make_unique<ImGuiEditorLayer>(window, renderer);
}

} // namespace gte
