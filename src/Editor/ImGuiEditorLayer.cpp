#include "EditorLayer.h"

#include "DockLayout.h"
#include "EditorCamera.h"
#include "EditorContext.h"
#include "ImGuiMemoryTracker.h"
#include "Panels/GamePanel.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/MemoryPanel.h"
#include "Panels/ProfilerPanel.h"
#if GTE_ENABLE_PROJECT_PANEL
#include "AssetPreviewMesh.h"
#include "AssetPreviewTexture.h"
#include "BoneViewerWindow.h"
#include "Panels/ProjectPanel.h"
#endif
#include "Panels/ScenePanel.h"
#include "TransformGizmo.h"
#include "../Game/Game.h"
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
//     freely dragged/split/tabbed by the user, Unity-style, PLUS
//     ImGuiConfigFlags_ViewportsEnable set, so any of those same panels can
//     also be dragged clean OUTSIDE the main OS window onto the desktop or
//     another monitor (each becoming its own real, independently movable OS
//     window) - see the constructor and RenderPlatformWindows() below.
//   - the SDL3 platform backend (input) and Vulkan renderer backend
//     (drawing), wired to the exact same device/swapchain format Renderer
//     already uses (see Renderer::GetVulkanContextInfo())
//   - TWO RenderTextures - m_gameView ("the Game view") and m_sceneView
//     ("the Scene view") - that Game's camera renders into each frame
//     instead of the swapchain, one per panel, each tracking that panel's
//     OWN content-region size (Unity's "Free Aspect" behavior) rather than
//     the OS window's size or each other's. Size tracking works like this,
//     mirrored independently for each view:
//       - GamePanel::Build()/ScenePanel::Build() (Panels/*.cpp) reads the
//         panel's current ImGui::GetContentRegionAvail() every frame it's
//         actually visible and stores it as m_ctx.desiredExtent/
//         desiredSceneExtent, and also stores whether ImGui::Begin()
//         reported the panel visible at all as
//         m_ctx.gameViewVisible/sceneViewVisible.
//       - GameViewTarget()/SceneViewTarget() (called earlier next frame,
//         before Game::Render()) return nullptr outright if that panel
//         wasn't visible last frame (skipping a RenderOffscreen() pass
//         nobody would see), otherwise compare the desired extent against
//         the texture's actual current extent and resize it first if they
//         differ - never mid-frame after this frame's ImGui::Image() call
//         has already been recorded (see GameViewTarget() for why that
//         would be unsafe).
//     This introduces at most one frame of lag between a resize (or a
//     visibility change) and the texture catching up, imperceptible in
//     practice.
//
// The overall panel layout (built once via DockLayout.cpp's
// BuildDockspaceAndMenuBar()/BuildDefaultDockLayout(), then left entirely to
// imgui.ini / user dragging afterwards) mirrors Unity's default editor: a
// full-viewport DockSpace hosting a top menu bar (File > Exit, ...),
// "Hierarchy" docked left, "Inspector" docked right, and "Scene"/"Game"
// tabbed together in the remaining center - the user can drag the "Scene"
// tab out to split it side-by-side with "Game" (or anywhere else) at any
// time, exactly like Unity's Scene/Game tabs. Application::Run() renders
// into whichever of GameViewTarget()/SceneViewTarget() come back non-null
// each frame: while tabbed together, exactly one is visible (so only that
// one is actually rendered, at zero extra GPU cost for the hidden one);
// split apart, both are visible and BOTH get rendered, each into its own
// RenderTexture at its own panel's size/aspect.
// "Game" still shows the scene through whatever ECS entity currently has
// the active Camera component (see ECS/Components/Camera.h), but "Scene"
// now shows it through its OWN independently-orbitable EditorCamera
// (m_sceneCamera - see EditorCamera.h) instead - Unity-style middle-drag
// pan / wheel dolly / right-drag look, handled entirely in
// Panels/ScenePanel.cpp (the one place that reads ImGui's mouse state) -
// see SceneViewProjection() below for how Application actually wires this
// in ahead of RenderSystem::Draw().
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
        : m_gameView(renderer.CreateRenderTexture(
              window.Width(), window.Height(), VK_FORMAT_UNDEFINED, "GameView", "GameViewDepth"))
        , m_sceneView(renderer.CreateRenderTexture(
              window.Width(), window.Height(), VK_FORMAT_UNDEFINED, "SceneView", "SceneViewDepth"))
    {
        // See EditorContext::desiredExtent/desiredSceneExtent for why both
        // are initialized to the OS window's startup size here.
        m_ctx.desiredExtent = VkExtent2D{
            static_cast<std::uint32_t>(window.Width()), static_cast<std::uint32_t>(window.Height()) };
        m_ctx.desiredSceneExtent = m_ctx.desiredExtent;

        const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
        m_device = context.device;

        // Must be installed before ImGui::CreateContext() below - see
        // ImGuiMemoryTracker's own doc comment for why (some of a context's
        // internal state is allocated the moment it's created).
        ImGuiMemoryTracker::Install();

        IMGUI_CHECKVERSION();
        m_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_context);
        ImGui::StyleColorsDark();

        // Docking lets Hierarchy/Inspector/Scene/Game be freely rearranged/
        // split within the one OS window - see the class comment above.
        // Viewports (multi-viewport/"platform windows") ADDITIONALLY lets
        // any panel be dragged OUTSIDE the main OS window entirely, onto the
        // desktop or another monitor, Unity/Unreal-style: each becomes its
        // own real OS window, backed by its own SDL3 window (created/
        // destroyed automatically by imgui_impl_sdl3.cpp's platform
        // callbacks - registered unconditionally by ImGui_ImplSDL3_InitForVulkan()
        // below, regardless of this flag) and its own Vulkan swapchain
        // (created/destroyed automatically by imgui_impl_vulkan.cpp's
        // renderer callbacks - registered by ImGui_ImplVulkan_Init() below,
        // see ImGui_ImplVulkan_InitMultiViewportSupport()). What actually
        // updates/presents those extra windows every frame is
        // RenderPlatformWindows() below, called once per frame from
        // Application::Run() right after the main swapchain is presented.
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        // Platform windows are separate, undocked OS windows - give them an
        // opaque background and square corners (matching a normal OS
        // window) instead of inheriting the main viewport's themed
        // rounded/semi-transparent look, exactly as Dear ImGui's own
        // demo/examples recommend whenever ViewportsEnable is on.
        {
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        // Dear ImGui's built-in default font (ProggyClean) has no CJK
        // glyphs at all - any Japanese/Chinese/Korean text (e.g. a
        // non-ASCII filename shown in "Project"/"Inspector" - see
        // Panels/ProjectPanel.cpp/InspectorPanel.cpp) would otherwise
        // render as an unbroken run of fallback "?" glyphs, even though the
        // underlying UTF-8 string data itself is perfectly correct (see
        // AGENTS.md-adjacent PathToUtf8()/Utf8ToPath() in
        // ProjectPanelData.h). Try a handful of CJK-capable fonts Windows
        // ships by default (in preference order), covering Basic Latin +
        // Hiragana/Katakana + a common Kanji set via
        // GetGlyphRangesJapanese() - falls back to the plain built-in font
        // only if none of them could be found/loaded (e.g. a minimal
        // Windows installation missing these). This engine deliberately
        // does NOT bundle its own CJK font (unlike stb_image/VMA/ImGui
        // itself): most CJK typefaces are not freely redistributable,
        // whereas every Windows install already ships one of these. Must
        // happen here, before ImGui_ImplVulkan_Init() below - the font
        // atlas texture is built/uploaded lazily on the first frame, but
        // every AddFont*() call must happen before that, never after.
        {
            const ImWchar* japaneseRanges = io.Fonts->GetGlyphRangesJapanese();
            constexpr const char* kCandidateCjkFonts[] = {
                "C:\\Windows\\Fonts\\meiryo.ttc",   // Meiryo - Windows Vista+, good general CJK coverage.
                "C:\\Windows\\Fonts\\YuGothM.ttc",  // Yu Gothic Medium - Windows 8.1+.
                "C:\\Windows\\Fonts\\msgothic.ttc", // MS Gothic - present on virtually every Windows version.
            };
            ImFont* cjkFont = nullptr;
            for (const char* fontPath : kCandidateCjkFonts) {
                cjkFont = io.Fonts->AddFontFromFileTTF(fontPath, 16.0f, nullptr, japaneseRanges);
                if (cjkFont != nullptr) {
                    break;
                }
            }
            if (cjkFont == nullptr) {
                // None of the candidates above could be found/loaded -
                // fall back to Dear ImGui's own built-in font (ASCII/
                // Latin-1 only, the behavior this engine already had).
                io.Fonts->AddFontDefault();
            }
        }

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
        // font atlas plus our own AddTexture() calls below for the Game/
        // Scene views.
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
        // (or our own AddTexture() descriptors) might still be referencing
        // before tearing any of it down.
        if (m_device != VK_NULL_HANDLE) {
            vkDeviceWaitIdle(m_device);
        }

        ReleaseGameViewDescriptor();
        ReleaseSceneViewDescriptor();
#if GTE_ENABLE_PROJECT_PANEL
        // Must release its own GPU texture/ImGui descriptor(s) BEFORE
        // ImGui_ImplVulkan_Shutdown() below - member destruction order
        // alone would run AFTER Shutdown() (m_assetPreview/m_assetPreviewMesh/
        // m_boneViewer are declared further down than m_context), which is
        // too late (see AssetPreviewTexture::Reset()'s own comment).
        m_assetPreview.Reset();
        m_assetPreviewMesh.Reset();
        m_boneViewer.Reset();
#endif
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext(m_context);
    }

    void ProcessEvent(const SDL_Event& event) override
    {
        ImGui::SetCurrentContext(m_context);
        ImGui_ImplSDL3_ProcessEvent(&event);

#if GTE_ENABLE_PROJECT_PANEL
        // An OS-level file/folder drag-and-drop (Windows Explorer, ...)
        // landing on ANY of this process's windows - the main window, or an
        // ImGui multi-viewport "platform window" (see the class comment
        // above) - is routed straight to ProjectPanel, entirely independent
        // of ImGui's own (widget-to-widget) drag-and-drop, which this event
        // has nothing to do with. event.drop.x/y are window-RELATIVE, so
        // they're converted to absolute desktop/screen coordinates here
        // (matching what ImGui::GetWindowPos() reports - see
        // ProjectPanel::Build()) by adding the source window's own screen
        // position, before ProjectPanel decides whether that position
        // actually falls within its own last-known on-screen rect.
        if (event.type == SDL_EVENT_DROP_FILE) {
            float screenX = event.drop.x;
            float screenY = event.drop.y;

            SDL_Window* dropWindow = SDL_GetWindowFromID(event.drop.windowID);
            if (dropWindow != nullptr) {
                int windowX = 0;
                int windowY = 0;
                SDL_GetWindowPosition(dropWindow, &windowX, &windowY);
                screenX += static_cast<float>(windowX);
                screenY += static_cast<float>(windowY);
            }

            m_projectPanel.HandleExternalFileDrop(screenX, screenY, event.drop.data != nullptr ? event.drop.data : "");
        }
#endif
    }

    void OnWindowResized(int /*width*/, int /*height*/) override
    {
        // Neither the Game-view nor Scene-view RenderTexture tracks the OS
        // window's size at all - each tracks its own ImGui panel's own
        // content-region size instead (see the class comment and
        // GameViewTarget()/SceneViewTarget() below), so an OS window resize
        // by itself is not a reason to resize either of them. Kept as a
        // no-op (rather than removed) purely to satisfy IEditorLayer's
        // interface - NullEditorLayer's version already does the same.
    }

    void NewFrame() override
    {
        // See RenderPlatformWindows()/m_frameRendered below for why this is
        // reset here, at the very start of every frame.
        m_frameRendered = false;

        ImGui::SetCurrentContext(m_context);
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        // Required by ImGuizmo before any Manipulate() call this frame
        // (Panels/ScenePanel.cpp, via TransformGizmo.h) - see
        // BeginGizmoFrame()'s own comment for why this is called right
        // here rather than from BuildUI() below.
        BeginGizmoFrame();
    }

    RenderTexture* GameViewTarget() override
    {
        // Not visible last frame (inactive tab behind "Scene", or
        // collapsed) - see EditorContext::gameViewVisible - skip rendering
        // into it entirely this frame; nobody would see it anyway.
        if (!m_ctx.gameViewVisible) {
            return nullptr;
        }

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

    RenderTexture* SceneViewTarget() override
    {
        // Same visibility-gated/resize-on-demand pattern as
        // GameViewTarget() above, applied to the Scene view's own, entirely
        // separate RenderTexture/EditorContext fields - see
        // EditorContext::sceneViewVisible/desiredSceneExtent.
        if (!m_ctx.sceneViewVisible) {
            return nullptr;
        }

        if (m_ctx.desiredSceneExtent.width > 0 && m_ctx.desiredSceneExtent.height > 0) {
            const VkExtent2D current = m_sceneView.Extent();
            if (current.width != m_ctx.desiredSceneExtent.width || current.height != m_ctx.desiredSceneExtent.height) {
                if (m_device != VK_NULL_HANDLE) {
                    vkDeviceWaitIdle(m_device);
                }
                ReleaseSceneViewDescriptor();
                m_sceneView.Resize(static_cast<int>(m_ctx.desiredSceneExtent.width),
                    static_cast<int>(m_ctx.desiredSceneExtent.height));
            }
        }
        return &m_sceneView;
    }

    // See IEditorLayer::SceneViewProjection() - m_sceneCamera is updated
    // once per frame from Panels/ScenePanel.cpp's mouse handling during
    // BuildUI() below, so this always reflects (up to one frame of lag,
    // same as every other Scene/Game-view field in EditorContext) whatever
    // the user last panned/rotated/dollied it to.
    Mat4 SceneViewProjection(float aspectWidthOverHeight) const override
    {
        return m_sceneCamera.ViewProjection(aspectWidthOverHeight);
    }

    void BuildUI(Game& game, Renderer& renderer) override
    {
        ImGui::SetCurrentContext(m_context);

        Registry& registry = game.GetRegistry();

        BuildDockspaceAndMenuBar(m_ctx);

        // Lazily (re)create the ImGui-side descriptors for the Game/Scene
        // view textures - needed on first use, and again after
        // GameViewTarget()/SceneViewTarget() invalidated the previous one
        // (a resize). Each panel owns its own descriptor/texture.
        if (m_ctx.gameViewDescriptor == VK_NULL_HANDLE) {
            m_ctx.gameViewDescriptor = ImGui_ImplVulkan_AddTexture(
                m_gameView.Sampler(), m_gameView.View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
        if (m_ctx.sceneViewDescriptor == VK_NULL_HANDLE) {
            m_ctx.sceneViewDescriptor = ImGui_ImplVulkan_AddTexture(
                m_sceneView.Sampler(), m_sceneView.View(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        BuildHierarchyPanel(game, renderer, m_ctx);
#if GTE_ENABLE_PROJECT_PANEL
        BuildInspectorPanel(registry, m_ctx, renderer, m_assetPreview, m_assetPreviewMesh, m_boneViewer);
#else
        BuildInspectorPanel(registry, m_ctx);
#endif
        BuildScenePanel(game, renderer, m_ctx, m_sceneCamera);
        BuildGamePanel(m_ctx);
        BuildMemoryPanel(m_ctx, renderer);
        m_profilerPanel.Build(m_ctx);
#if GTE_ENABLE_PROJECT_PANEL
        m_projectPanel.Build(m_ctx);
        // The Bone Viewer is its own floating window (opened on demand via
        // Inspector's "Open Bone Viewer" button - see
        // Panels/InspectorPanel.cpp) rather than part of the fixed dock
        // layout above - Build() itself is a complete no-op whenever it
        // isn't currently open (see BoneViewerWindow.h).
        m_boneViewer.Build(registry, renderer);
#endif
    }

    void Render(VkCommandBuffer cmd) override
    {
        ImGui::SetCurrentContext(m_context);
        ImGui::Render();
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

        // Marks THIS frame as having actually reached ImGui::Render()/
        // EndFrame() - see RenderPlatformWindows() below for why this
        // matters (Application::Run() calls that unconditionally every
        // frame, even ones where Renderer::Present() skipped calling this
        // very method entirely, e.g. a minimized window).
        m_frameRendered = true;
    }

    void RenderPlatformWindows() override
    {
        ImGui::SetCurrentContext(m_context);

        // Nothing to do if this frame never actually reached Render()
        // above (e.g. the OS window is currently minimized, so
        // Renderer::Present() bailed out before ever invoking the
        // recordExtra hook that calls Render()) - ImGui::Render()/
        // EndFrame() was never called this frame, and calling
        // UpdatePlatformWindows()/RenderPlatformWindowsDefault() without
        // that happening first would hit an internal Dear ImGui assert
        // ("Forgot to call Render() or EndFrame() ...").
        if (!m_frameRendered) {
            return;
        }

        // Actually creates/resizes/destroys the extra real OS windows +
        // their own Vulkan swapchains for every ImGui window currently
        // dragged outside the main viewport (see
        // ImGui_ImplVulkan_InitMultiViewportSupport()/
        // ImGui_ImplSDL3_InitMultiViewportSupport(), wired up automatically
        // back in the constructor), then records+submits+presents each of
        // them - completely independent of, and unrelated to, the main
        // swapchain Renderer::Present() just presented. A no-op whenever
        // ImGuiConfigFlags_ViewportsEnable isn't set (never the case here -
        // see the constructor - but this mirrors every official Dear ImGui
        // backend example's own guard, and keeps this method safe to call
        // unconditionally even if that ever changes).
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    bool WantsExit() const override { return m_ctx.exitRequested; }

    // Backed directly by ImGuiIO::WantCaptureMouse/WantCaptureKeyboard -
    // ImGui already recomputes these every frame (as of the last
    // NewFrame()/ProcessEvent() calls) from its own internal hover/focus/
    // active-widget state, so there is nothing else to track here. See
    // IEditorLayer::WantsCaptureMouse()/WantsCaptureKeyboard() for why
    // Application checks these before forwarding input to Game.
    bool WantsCaptureMouse() const override
    {
        ImGui::SetCurrentContext(m_context);
        return ImGui::GetIO().WantCaptureMouse;
    }

    bool WantsCaptureKeyboard() const override
    {
        ImGui::SetCurrentContext(m_context);
        return ImGui::GetIO().WantCaptureKeyboard;
    }

private:
    void ReleaseGameViewDescriptor()
    {
        if (m_ctx.gameViewDescriptor != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(m_ctx.gameViewDescriptor);
            m_ctx.gameViewDescriptor = VK_NULL_HANDLE;
        }
    }

    void ReleaseSceneViewDescriptor()
    {
        if (m_ctx.sceneViewDescriptor != VK_NULL_HANDLE) {
            ImGui_ImplVulkan_RemoveTexture(m_ctx.sceneViewDescriptor);
            m_ctx.sceneViewDescriptor = VK_NULL_HANDLE;
        }
    }

    VkDevice m_device = VK_NULL_HANDLE;
    ImGuiContext* m_context = nullptr;
    RenderTexture m_gameView;
    RenderTexture m_sceneView;

    // The Scene view's own, independently-orbitable camera (see
    // EditorCamera.h) - updated once per frame by Panels/ScenePanel.cpp
    // (called from BuildUI() above) from that panel's own mouse input, and
    // read back by SceneViewProjection() above.
    EditorCamera m_sceneCamera;

    // True once Render(cmd) has actually run THIS frame (reset to false at
    // the top of every NewFrame()) - see RenderPlatformWindows() for why
    // this guard exists.
    bool m_frameRendered = false;

    // The Editor's "Profiler" panel (see Panels/ProfilerPanel.h) - docked
    // alongside "Memory" (DockLayout.cpp). Unlike every other panel here,
    // it's NOT gated behind GTE_ENABLE_PROJECT_PANEL - it depends only on
    // Profiling::FrameProfiler, which is always compiled regardless of that
    // switch (see AGENTS.md, "Profiling").
    ProfilerPanel m_profilerPanel;

#if GTE_ENABLE_PROJECT_PANEL
    // The Editor's Unity-style "Project" panel (see Panels/ProjectPanel.h) -
    // a live view of a "Project" folder next to the built .exe, plus
    // external drag-and-drop file import. Only compiled/present at all when
    // GTE_ENABLE_PROJECT_PANEL is ON (a separate switch from
    // GTE_ENABLE_EDITOR - see the root CMakeLists.txt) - ProcessEvent()
    // above feeds it SDL_EVENT_DROP_FILE, BuildUI() below calls its Build().
    ProjectPanel m_projectPanel;

    // Backs the Inspector's live image-preview thumbnail (see
    // Panels/InspectorPanel.cpp's BuildAssetInspector()) whenever the
    // Project selection is a decodable image file - see
    // AssetPreviewTexture.h. Explicitly Reset() in the destructor above
    // BEFORE ImGui_ImplVulkan_Shutdown(), not left to member-destruction
    // order alone (which would run too late).
    AssetPreviewTexture m_assetPreview;

    // The Mesh-asset equivalent of m_assetPreview above - backs the
    // Inspector's live, auto-rotating 3D mesh preview (see
    // Panels/InspectorPanel.cpp's BuildAssetInspector()) whenever the
    // Project selection is a *.gta AssetType::Mesh file - see
    // AssetPreviewMesh.h. Also explicitly Reset() in the destructor above,
    // same reasoning as m_assetPreview.
    AssetPreviewMesh m_assetPreviewMesh;

    // The Editor's Unity-"Avatar configuration"-style Bone Viewer floating
    // debug window (BoneViewerWindow.h) - opened on demand via Inspector's
    // "Open Bone Viewer" button (Panels/InspectorPanel.cpp) for whichever
    // entity is currently selected, and stays open (drawing whatever entity
    // it was last opened onto) independent of the Hierarchy/Inspector
    // selection changing afterwards. Also explicitly Reset() in the
    // destructor above, same reasoning as m_assetPreview/m_assetPreviewMesh.
    BoneViewerWindow m_boneViewer;
#endif

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
