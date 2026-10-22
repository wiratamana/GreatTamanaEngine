#include "EditorLayer.h"

#include <cassert>

#include "../Core/Logging.h"
#include "../Encoding/Utf8Path.h"
#include "BuiltinFeatureEditorPanelRegistry.h"
#include "DockLayout.h"
#include "EditorCamera.h"
#include "EditorContext.h"
#include "EditorGpuMemoryNameOverlay.h"
#include "InstanceGridTestSpawner.h"
#include "ImGuiIdConflictGuard.h"
#include "ImGuiMemoryTracker.h"
#include "MeshClusterBuildRunner.h"
#include "NewProjectWindow.h"
#include "OpenProjectWindow.h"
#include "CreateAssetWindow.h"
#include "Plugins/PluginPanelDrawContextAdapter.h"
#include "../Core/EditorPanelRegistry.h"
#include "../Core/EditorPanelModule.h"
#include "Panels/FrameDebuggerPanel.h"
#include "Panels/GamePanel.h"
#include "Panels/HierarchyPanel.h"
#include "Panels/InspectorPanel.h"
#include "Panels/JobsPanel.h"
#include "Panels/LogPanel.h"
#include "Panels/MemoryPanel.h"
#include "Panels/ProfilerPanel.h"
#include "Panels/RenderGraphPanel.h"
#include "Panels/RenderFeaturesPanel.h"
#include "../Renderer/SceneGridRenderer.h"
#if GTE_ENABLE_PROJECT_PANEL
#include "AssetPreviewMesh.h"
#include "AssetPreviewTexture.h"
#include "BoneViewerWindow.h"
#include "ModelRigCache.h"
#include "Panels/ProjectPanel.h"
#endif
#include "Panels/ScenePanel.h"
#include "TransformGizmo.h"
#include "../Game/Game.h"
#include "../Renderer/RenderGraph/RenderPassToggleRegistry.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/UiTextureBackend.h"
#include "../Window/Window.h"

// Editor composition root: owns the ImGui context, the SDL3/Vulkan backends, the Game and Scene view
// RenderTextures and the shared EditorContext. Only src/Editor files know ImGui.
// Each view texture follows its panel's size with one frame of lag; hidden panels are not rendered.
#include <imgui.h>
#include <imgui_internal.h> // FindWindowByName(), ImGuiWindow::Viewport
#include <backends/imgui_impl_sdl3.h>
#include <backends/imgui_impl_vulkan.h>

#include <SDL3/SDL.h>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <system_error>

namespace gte {

namespace {

class ImGuiEditorLayer final : public IEditorLayer {
public:
    ImGuiEditorLayer(Window& window, Renderer& renderer)
        : m_gameView(renderer.CreateRenderTexture(window.Width(), window.Height(), renderer.ScenePixelFormat(), "GameView",
              "GameViewDepth", /*allowStorageImageAccess=*/true, /*allowDepthSampledAccess=*/true))
        , m_sceneView(renderer.CreateRenderTexture(window.Width(), window.Height(), renderer.ScenePixelFormat(), "SceneView",
              "SceneViewDepth", /*allowStorageImageAccess=*/true, /*allowDepthSampledAccess=*/true))
        , m_sceneGrid(renderer)
    {
        // Both views start at the OS window's size so the first frame sees no resize mismatch.
        m_ctx.desiredExtent =
            Extent2D{ static_cast<std::uint32_t>(window.Width()), static_cast<std::uint32_t>(window.Height()) };
        m_ctx.desiredSceneExtent = m_ctx.desiredExtent;
        m_ctx.clusterBuildRunner = &m_clusterBuildRunner;

        m_renderer = &renderer; // Non-owning - needed by Release*Descriptor() below, which take no renderer parameter.

        // Must be installed before ImGui::CreateContext() below - see
        // ImGuiMemoryTracker's own doc comment for why (some of a context's
        // internal state is allocated the moment it's created).
        ImGuiMemoryTracker::Install();

        IMGUI_CHECKVERSION();
        m_context = ImGui::CreateContext();
        ImGui::SetCurrentContext(m_context);
        ImGui::StyleColorsDark();

        // Docking lets panels be rearranged; viewports let them be dragged out into their own OS windows
        // (created by the SDL3/Vulkan backends, presented by RenderPlatformWindows()).
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
        io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;

        // Platform windows are real OS windows: use an opaque background and square corners.
        {
            ImGuiStyle& style = ImGui::GetStyle();
            style.WindowRounding = 0.0f;
            style.Colors[ImGuiCol_WindowBg].w = 1.0f;
        }

        // The default ImGui font has no CJK glyphs. Try the CJK fonts Windows ships (none is bundled), else keep
        // the built-in font. Must run before ImGui_ImplVulkan_Init(), as fonts cannot be added after atlas build.
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

        // Renderer's Vulkan context via the dedicated ImGui bridge; kept alive so pColorAttachmentFormats stays valid.
        const Renderer::VulkanContextInfo context = renderer.GetImGuiVulkanBackendInfo();

        ImGui_ImplVulkan_InitInfo initInfo{};
        initInfo.ApiVersion = context.apiVersion;
        initInfo.Instance = context.instance;
        initInfo.PhysicalDevice = context.physicalDevice;
        initInfo.Device = context.device;
        initInfo.QueueFamily = context.graphicsQueueFamily;
        initInfo.Queue = context.graphicsQueue;
        // The backend owns its descriptor pool; sized for the font atlas plus the Game/Scene view textures.
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

        // Bridges Renderer::GetUiTextureHandle() to this real ImGui Vulkan
        // backend - Renderer itself never includes an ImGui header.
        SetUiTextureBackend(
            [](VkSampler sampler, VkImageView view) {
                return ImGui_ImplVulkan_AddTexture(sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            },
            [](VkDescriptorSet set) { ImGui_ImplVulkan_RemoveTexture(set); });
    }

    ~ImGuiEditorLayer() override
    {
        ImGui::SetCurrentContext(m_context);

        // Make sure the GPU is done with anything ImGui's Vulkan backend
        // (or our own AddTexture() descriptors) might still be referencing
        // before tearing any of it down.
        if (m_renderer != nullptr) {
            m_renderer->WaitForGpuIdle();
        }

        ReleaseGameViewDescriptor();
        ReleaseSceneViewDescriptor();
        // Panels declared after m_context must release their descriptors before ImGui_ImplVulkan_Shutdown().
        m_frameDebuggerPanel.ReleasePreviewDescriptor();
        m_frameDebuggerPanel.ReleaseShaderPropertyTexturePreview();
        m_renderGraphPanel.ReleaseTexturePreview();
#if GTE_ENABLE_PROJECT_PANEL
        // Release GPU textures/descriptors before ImGui_ImplVulkan_Shutdown(); member destruction runs too late.
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
        // OS file drops on any window (main or ImGui platform window) go to ProjectPanel.
        // Drop coordinates are window-relative, so convert them to screen coordinates first.
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
        // Intentionally empty: the view textures follow their panels' sizes, not the OS window's.
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

        // Reset ID-conflict tracking; must run after ImGui::NewFrame() and before any widget is built.
        ImGuiIdConflictGuard::Instance().BeginFrame();

        // Required by ImGuizmo before any Manipulate() call this frame.
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

        // Apply the panel size captured last frame before rendering; resizing mid-frame could destroy a
        // texture that an already-recorded ImGui::Image() still references.
        if (m_ctx.desiredExtent.width > 0 && m_ctx.desiredExtent.height > 0) {
            const Extent2D current = m_gameView.Extent();
            if (current.width != m_ctx.desiredExtent.width || current.height != m_ctx.desiredExtent.height) {
                // Resizes are rare and user-driven, so a full GPU stall is acceptable.
                m_renderer->WaitForGpuIdle();
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
        // Same visibility gating and resize-on-demand as GameViewTarget(), for the Scene view.
        if (!m_ctx.sceneViewVisible) {
            return nullptr;
        }

        if (m_ctx.desiredSceneExtent.width > 0 && m_ctx.desiredSceneExtent.height > 0) {
            const Extent2D current = m_sceneView.Extent();
            if (current.width != m_ctx.desiredSceneExtent.width || current.height != m_ctx.desiredSceneExtent.height) {
                m_renderer->WaitForGpuIdle();
                ReleaseSceneViewDescriptor();
                m_sceneView.Resize(static_cast<int>(m_ctx.desiredSceneExtent.width),
                    static_cast<int>(m_ctx.desiredSceneExtent.height));
            }
        }
        return &m_sceneView;
    }

    // Reflects the Scene camera as of the last BuildUI() (up to one frame of lag).
    Mat4 SceneViewProjection(float aspectWidthOverHeight) const override
    {
        return m_sceneCamera.ViewProjection(aspectWidthOverHeight);
    }

    // See IEditorLayer::SceneViewCameraWorldPosition()'s own doc comment.
    Vec3 SceneViewCameraWorldPosition() const override { return m_sceneCamera.GetTransform().position; }


    // Remembers the pointer; BuildUI() creates the ImGui descriptor from it lazily.
    void SetGameViewCompositedTexture(RenderTexture* texture) override { m_gameViewComposited = texture; }
    void SetSceneViewCompositedTexture(RenderTexture* texture) override { m_sceneViewComposited = texture; }

    // See IEditorLayer::RenderSceneGrid(). Called only when the Scene view rendered, so no visibility check is needed.
    void RenderSceneGrid(const rg::RecordingContext& cmd, const Mat4& sceneViewProjection) override
    {
        m_sceneGrid.Draw(cmd, sceneViewProjection);
    }

    void BuildUI(Game& game, Renderer& renderer, const rg::RenderGraph& renderGraph,
        const std::vector<RenderFeatureDebugEntry>& renderFeatureEntries,
        rg::RenderPassToggleRegistry& renderPassToggleRegistry,
        RenderFeatureCompositor* renderFeatureCompositor,
        const rg::RenderPassBlackboard& offscreenBlackboard,
        DescriptorSetLayoutHandle sceneServicesSetLayout) override
    {
        ImGui::SetCurrentContext(m_context);

        Registry& registry = game.GetRegistry();

        BuildDockspaceAndMenuBar(m_ctx, game, renderer, m_hotReloadDebugCapability, sceneServicesSetLayout);

        // New/Open Project and Create Asset windows are available in every build, regardless of
        // GTE_ENABLE_PROJECT_PANEL.
        m_newProjectWindow.Build(m_ctx, m_projectLifecycleCapability);
        m_openProjectWindow.Build(m_ctx, m_projectLifecycleCapability);
        m_createAssetWindow.Build(m_ctx, m_assetScaffoldingCapability);

        // Lazily (re)create the ImGui descriptors for the Game/Scene view textures (first use, or after a resize).
        // The panels show m_gameView/m_sceneView directly: the compositor blends every feature into them,
        // while a per-feature *Composited texture only reflects that feature's position in the chain.
        {
            RenderTexture* gameSource = &m_gameView;
            if (m_ctx.gameViewDescriptor == UiTextureHandle::Invalid || gameSource->View() != m_lastKnownGameView) {
                ReleaseGameViewDescriptor();
                m_ctx.gameViewDescriptor = renderer.GetUiTextureHandle(*gameSource);
                m_lastKnownGameView = gameSource->View();
            }
        }
        {
            RenderTexture* sceneSource = &m_sceneView;
            if (m_ctx.sceneViewDescriptor == UiTextureHandle::Invalid || sceneSource->View() != m_lastKnownSceneView) {
                ReleaseSceneViewDescriptor();
                m_ctx.sceneViewDescriptor = renderer.GetUiTextureHandle(*sceneSource);
                m_lastKnownSceneView = sceneSource->View();
            }
        }
        BuildHierarchyPanel(game, renderer, m_ctx, sceneServicesSetLayout);
#if GTE_ENABLE_PROJECT_PANEL
        BuildInspectorPanel(registry, m_ctx, renderer, m_assetPreview, m_assetPreviewMesh, m_boneViewer,
            game.GetPhysicsSystem(), game.GetMeshInstantiationSystem(), m_modelRigCache);
#else
        BuildInspectorPanel(registry, m_ctx, game.GetPhysicsSystem(), game.GetMeshInstantiationSystem());
#endif
        if (!m_ctx.pendingAssetRecordRefresh.empty()) {
            RefreshProjectAssetRecord(m_ctx.pendingAssetRecordRefresh);
            m_ctx.pendingAssetRecordRefresh.clear();
        }
        BuildScenePanel(game, renderer, m_ctx, m_sceneCamera, sceneServicesSetLayout);
        BuildGamePanel(m_ctx);
        BuildMemoryPanel(m_ctx, renderer);
        m_profilerPanel.Build(m_ctx);
        m_renderGraphPanel.Build(m_ctx, renderer, renderGraph, renderPassToggleRegistry);
        m_renderFeaturesPanel.Build(m_ctx, renderFeatureEntries, renderFeatureCompositor);
        // Jobs panel; also hosts the CPU/GPU skinning toggle, hence `game`.
        m_jobsPanel.Build(m_ctx, game);
        // Log panel; depends only on EditorContext and the thread-safe logger.
        m_logPanel.Build(m_ctx);
        // Frame Debugger window (Build() is a no-op unless open). It gets the raw and composited Game views for
        // before/after comparison, plus the toggle registry and offscreen blackboard for its honesty checks.
        m_frameDebuggerPanel.Build(m_ctx, renderer, renderGraph, m_gameView, m_gameViewComposited,
            &renderPassToggleRegistry, &offscreenBlackboard);
#if GTE_ENABLE_PROJECT_PANEL
        m_projectPanel.Build(m_ctx);
        // Bone Viewer floating window; Build() is a no-op while it is closed.
        m_boneViewer.Build(registry, renderer, m_ctx, m_modelRigCache, game.GetPhysicsSystem());
#endif

        // Draw every registered plugin panel; the content goes through PluginPanelDrawContextAdapter.
        for (const auto& entry : EditorPanelRegistry::Instance().PluginPanels()) {
            if (ImGui::Begin(entry.name.c_str())) {
                PluginPanelDrawContextAdapter drawContext;
                entry.module->BuildPanel(drawContext);
            }
            ImGui::End(); // Always called, matching ImGui::Begin()'s own documented contract, even when Begin() returned false.
        }
    }

    void Render(const rg::RecordingContext& cmd) override
    {
        ImGui::SetCurrentContext(m_context);
        ImGui::Render();
        // The ImGui Vulkan backend needs the raw command buffer; this is the only place allowed to open it.
        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), static_cast<VkCommandBuffer>(cmd.PlatformHandle()));

        // RenderPlatformWindows() checks this: it runs every frame, even when Render() was skipped.
        m_frameRendered = true;
    }

    void RenderPlatformWindows() override
    {
        ImGui::SetCurrentContext(m_context);

        // Skip when this frame never reached Render() (e.g. minimized window): ImGui asserts if
        // UpdatePlatformWindows() runs without a prior Render()/EndFrame().
        if (!m_frameRendered) {
            return;
        }

        // Create, resize and render the extra OS windows of panels dragged outside the main viewport.
        ImGuiIO& io = ImGui::GetIO();
        if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }
    }

    CapturedPixels CapturePlatformWindow(Renderer& renderer, const std::string& title) override
    {
        ImGui::SetCurrentContext(m_context);
        ImGuiWindow* window = ImGui::FindWindowByName(title.c_str());
        ImGui_ImplVulkanH_Window* platformWindow = (window != nullptr && window->Viewport != nullptr)
            ? ImGui_ImplVulkanH_GetWindowDataFromViewport(window->Viewport)
            : nullptr;
        if (platformWindow == nullptr || platformWindow->Swapchain == VK_NULL_HANDLE) {
            return {}; // Wrong title or still docked.
        }

        const ImGui_ImplVulkanH_Frame& frame = platformWindow->Frames[platformWindow->FrameIndex];
        const Extent2D extent{
            static_cast<std::uint32_t>(platformWindow->Width), static_cast<std::uint32_t>(platformWindow->Height) };
        const rg::ResourceState presentState{
            VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, VK_ACCESS_2_NONE };

        // The backbuffer is only queued and untracked by the barrier planner, so a full device stall is the only safe readback.
        renderer.WaitForGpuIdle();
        return renderer.CaptureImagePixels(
            frame.Backbuffer, VK_IMAGE_ASPECT_COLOR_BIT, platformWindow->SurfaceFormat.format, extent, presentState);
    }

    bool WantsExit() const override { return m_ctx.exitRequested; }

    // ImGui recomputes WantCapture* every frame from its hover/focus state; Application checks them
    // before forwarding input to Game.
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

    // Backed by EditorContext's pause/step toggles; see IEditorLayer::IsPlaybackPaused().
    bool IsPlaybackPaused() const override { return m_ctx.playbackPaused; }

    bool TryConsumeStepRequest() override
    {
        if (!m_ctx.stepOneFrameRequested) {
            return false;
        }
        m_ctx.stepOneFrameRequested = false;
        return true;
    }

    // See IEditorLayer::ActivateTab(). Sets the ImGui context explicitly because it may not be current here.
    TabActivationResult ActivateTab(const std::string& panelName) override
    {
        ImGui::SetCurrentContext(m_context);
        TabActivationResult result;
        result.tabExists = FindAndFocusEditorWindow(panelName.c_str());
        return result;
    }

    // Forwards into InstanceGridTestSpawner::Spawn() with the grid resources this layer owns.
    InstanceGridSpawnLayerResult SpawnInstanceGrid(Game& game, Renderer& renderer, std::uint32_t instanceCount,
        float spacing, DescriptorSetLayoutHandle sceneServicesSetLayout) override
    {
        const InstanceGridSpawnResult spawned = InstanceGridTestSpawner::Spawn(
            game, renderer, m_ctx.instanceGrid, instanceCount, spacing, sceneServicesSetLayout);
        InstanceGridSpawnLayerResult result;
        result.success = spawned.success;
        result.errorMessage = spawned.errorMessage;
        result.instanceCount = spawned.instanceCount;
        return result;
    }

    // See IEditorLayer::ImportExternalAssetIntoProject(); reports no project when GTE_ENABLE_PROJECT_PANEL is off.
    ProjectAssetImportResult ImportExternalAssetIntoProject(
        const std::string& sourceAbsolutePath, const std::string& destinationRelativeFolder) override
    {
        ProjectAssetImportResult result;
#if GTE_ENABLE_PROJECT_PANEL
        const AssetImportResult imported = m_projectPanel.ImportExternalFile(
            PathFromUtf8(sourceAbsolutePath), destinationRelativeFolder);
        result.projectAvailable = true;
        result.success = imported.success;
        result.message = imported.message;

        // Non-throwing relative() overload; on failure fall back to the absolute path.
        std::error_code relEc;
        const std::filesystem::path relativePath =
            std::filesystem::relative(imported.finalPath, m_projectPanel.GetRootPath(), relEc);
        result.finalRelativePath = (!relEc) ? PathToUtf8(relativePath) : PathToUtf8(imported.finalPath);
        result.finalAbsolutePath = PathToUtf8(imported.finalPath);

        result.guid = imported.guid.IsValid() ? imported.guid.ToString() : std::string();
        result.convertedToMeshAsset = imported.convertedToMeshAsset;
        result.meshSourceFormat = (imported.meshSourceFormat == MeshSourceFormat::Stl) ? "stl"
            : (imported.meshSourceFormat == MeshSourceFormat::Pmx) ? "pmx"
            : (imported.meshSourceFormat == MeshSourceFormat::Gltf) ? "gltf" : std::string();
        result.convertedToKtx2 = imported.convertedToKtx2;
        result.convertedToMotionAsset = imported.convertedToMotionAsset;
        result.meshVertexCount = imported.meshVertexCount;
        result.meshTriangleCount = imported.meshTriangleCount;
#else
        result.projectAvailable = false;
#endif
        return result;
    }

    MeshClusterBuildRunner* ClusterBuildRunner() override { return &m_clusterBuildRunner; }

    void RefreshProjectAssetRecord(const std::string& absoluteGtaPath) override
    {
#if GTE_ENABLE_PROJECT_PANEL
        m_projectPanel.RefreshAssetRecord(PathFromUtf8(absoluteGtaPath));
#else
        (void)absoluteGtaPath;
#endif
    }

    AssetSelectionResult SelectProjectAsset(const std::string& absolutePath) override
    {
        AssetSelectionResult result;
#if GTE_ENABLE_PROJECT_PANEL
        const std::filesystem::path& root = m_projectPanel.GetRootPath();
        std::filesystem::path path;
        if (const std::optional<std::string> invalid = ValidateMeshAssetPath(absolutePath, root, path)) {
            result.message = *invalid;
            return result;
        }
        // Same strings a click in the Project panel stores.
        const std::string relativePath = ProjectRelativeSlashPath(path, root);
        m_ctx.selection.SelectAsset(PathToUtf8(root / PathFromUtf8(relativePath)), relativePath, false);
        result.success = true;
#else
        (void)absolutePath;
        result.editorAvailable = false;
        result.message = "the Project panel is not compiled in";
#endif
        return result;
    }

    InspectorStateView InspectorState() const override
    {
        InspectorStateView view;
        const Selection& selection = m_ctx.selection;
        switch (selection.Kind()) {
        case InspectorSelectionKind::None: view.selectionKind = "none"; break;
        case InspectorSelectionKind::Entity: view.selectionKind = "entity"; break;
        case InspectorSelectionKind::Asset:
            view.selectionKind = "asset";
            view.selectedPath = selection.SelectedAssetAbsolutePath();
            break;
        case InspectorSelectionKind::ModelPart: view.selectionKind = "model_part"; break;
        }
        const InspectorShownState& shown = m_ctx.clusterTool.shown;
        view.shownPath = shown.assetPath;
        view.clustersText = shown.clustersText;
        view.clusterStatus = shown.clusterStatus;
        view.buildEnabled = shown.buildEnabled;
        view.buildDisabledReason = shown.buildDisabledReason;
        view.buildRunning = m_clusterBuildRunner.Busy();
        return view;
    }

    ClusterBuildStartResult InspectorBuildClusters(const InspectorClusterOptions& options) override
    {
        const Selection& selection = m_ctx.selection;
        if (selection.Kind() != InspectorSelectionKind::Asset || selection.SelectedAssetAbsolutePath().empty()) {
            ClusterBuildStartResult refused;
            refused.status = ClusterBuildStart::Refused;
            refused.message = "no mesh asset is selected in the Inspector";
            return refused;
        }
        ClusterToolUiState& ui = m_ctx.clusterTool;
        ui.weldIndex = options.positionOnlyWeld ? 1 : 0;
        ui.maxVertices = static_cast<int>(options.maxVertices);
        ui.maxTriangles = static_cast<int>(options.maxTriangles);
        ui.coneWeight = options.coneWeight;
        return StartInspectorClusterBuild(m_ctx, selection.SelectedAssetAbsolutePath());
    }

    SplitViewsLayerResult SetSceneGameSplit(bool enabled) override
    {
        ImGui::SetCurrentContext(m_context);
        const SceneGameSplitResult applied = gte::SetSceneGameSplit(m_ctx, enabled);
        SplitViewsLayerResult result;
        result.success = applied.applied;
        result.split = applied.split;
        result.message = applied.error;
        return result;
    }


    // Frame Debugger capture hooks; both forward into m_frameDebuggerPanel, which owns the capture state.
    IFrameDebuggerCaptureRecorder* PrepareFrameDebuggerCaptureContext() override
    {
        return m_frameDebuggerPanel.PrepareCaptureContextForThisFrame(m_ctx);
    }

    void NotifyFrameDebuggerStepConsumed() override { m_frameDebuggerPanel.NotifyStepConsumed(); }

    // Forwards to m_frameDebuggerPanel.
    bool ConsumePendingFrameDebuggerReplayRequest() override
    {
        return m_frameDebuggerPanel.ConsumePendingReplayRequest();
    }

    // Frame Debugger HTTP commands; each forwards to the matching m_frameDebuggerPanel method.
    void FrameDebuggerOpenWindow() override { m_frameDebuggerPanel.RequestOpenWindow(m_ctx); }
    void FrameDebuggerSetEnabled(bool enabled) override { m_frameDebuggerPanel.SetEnabledFromCommand(m_ctx, enabled); }

    // Mirrors the Render Graph panel's own toolbar Regime combo - an HTTP
    // command flips the same EditorContext field a human click would.
    // Returns whether the "Render Graph" window was actually open to see it.
    bool SetRenderGraphDisplayedRegime(bool present) override
    {
        m_ctx.renderGraphDisplayedRegime = present ? RenderGraphRegimeChoice::Present : RenderGraphRegimeChoice::Offscreen;
        return m_ctx.renderGraphWindowOpen;
    }

    // Stored only; m_newProjectWindow's Build() invokes it.
    void SetProjectLifecycleCapability(IProjectLifecycleCapability* capability) override
    {
        m_projectLifecycleCapability = capability;
    }

    // Stored only; m_createAssetWindow's Build() invokes it.
    void SetAssetScaffoldingCapability(IAssetScaffoldingCapability* capability) override
    {
        m_assetScaffoldingCapability = capability;
    }

    // Stored only; BuildDockspaceAndMenuBar() invokes it.
    void SetHotReloadDebugCapability(IHotReloadDebugCapability* capability) override
    {
        m_hotReloadDebugCapability = capability;
    }

    // Registers each module's Editor panel (if any) from BuiltinFeatureEditorPanelRegistry, once.
    // Called by EditorHost after BuiltinFeatureModuleRegistry::CreateAll().
    void AttachBuiltinFeatureModules(const std::vector<std::unique_ptr<IEngineFeatureModule>>& modules,
        Renderer& renderer, const rg::RenderGraph& renderGraph) override
    {
        for (const auto& module : modules) {
            std::unique_ptr<IEditorPanelModule_v1> panel =
                BuiltinFeatureEditorPanelRegistry::Instance().TryCreatePanel(*module, m_ctx, renderer, renderGraph);
            if (panel != nullptr) {
                EditorPanelRegistry::Instance().RegisterPluginPanel(panel->GetPanelName(), panel.get());
                m_builtinFeaturePanelModules.push_back(std::move(panel));
            }
        }
    }
    bool FrameDebuggerCaptureNow() override { return m_frameDebuggerPanel.CaptureNowFromCommand(); }
    void FrameDebuggerSelectEvent(int index) override { m_frameDebuggerPanel.SelectEventFromCommand(index); }
    bool FrameDebuggerSetChannel(const std::string& channel) override
    {
        return m_frameDebuggerPanel.SetChannelFromCommand(channel);
    }
    void FrameDebuggerSetLevels(float black, float white) override
    {
        m_frameDebuggerPanel.SetLevelsFromCommand(black, white);
    }
    FrameDebuggerStateSnapshotView FrameDebuggerGetState() const override
    {
        return m_frameDebuggerPanel.BuildStateSnapshotView(m_ctx);
    }
    FrameDebuggerEventSink* FrameDebuggerGetEventSinkForInstall() override
    {
        return m_frameDebuggerPanel.EventSinkForInstall();
    }
    FrameDebuggerEventTextureResult FrameDebuggerGetEventTexture(int eventIndex) override
    {
        return m_frameDebuggerPanel.GetEventTextureFromCommand(eventIndex);
    }

private:
    void ReleaseGameViewDescriptor()
    {
        if (m_ctx.gameViewDescriptor != UiTextureHandle::Invalid) {
            m_renderer->ReleaseUiTextureHandle(m_ctx.gameViewDescriptor);
            m_ctx.gameViewDescriptor = UiTextureHandle::Invalid;
            m_lastKnownGameView = VK_NULL_HANDLE;
        }
    }

    void ReleaseSceneViewDescriptor()
    {
        if (m_ctx.sceneViewDescriptor != UiTextureHandle::Invalid) {
            m_renderer->ReleaseUiTextureHandle(m_ctx.sceneViewDescriptor);
            m_ctx.sceneViewDescriptor = UiTextureHandle::Invalid;
            m_lastKnownSceneView = VK_NULL_HANDLE;
        }
    }

    Renderer* m_renderer = nullptr; // Non-owning - see constructor comment.
    ImGuiContext* m_context = nullptr;
    RenderTexture m_gameView;
    RenderTexture m_sceneView;

    // Non-owning composited-view textures. m_lastKnown*View is the view each ImGui descriptor was made for,
    // so BuildUI() can tell when a fresh descriptor is needed.
    RenderTexture* m_gameViewComposited = nullptr;
    RenderTexture* m_sceneViewComposited = nullptr;
    VkImageView m_lastKnownGameView = VK_NULL_HANDLE;
    VkImageView m_lastKnownSceneView = VK_NULL_HANDLE;

    // Scene view's own orbit camera; ScenePanel updates it from mouse input each frame.
    EditorCamera m_sceneCamera;

    // Infinite ground grid for the Scene view; drawn whenever the Scene view renders, with no toggle.
    SceneGridRenderer m_sceneGrid;

    // True once Render(cmd) has actually run THIS frame (reset to false at
    // the top of every NewFrame()) - see RenderPlatformWindows() for why
    // this guard exists.
    bool m_frameRendered = false;

    // "Profiler" panel, docked with Memory. Not gated by GTE_ENABLE_PROJECT_PANEL.
    ProfilerPanel m_profilerPanel;

    // "Render Graph" floating window, toggled by ctx.renderGraphWindowOpen and the Window menu.
    RenderGraphPanel m_renderGraphPanel;

    // "Render Features" panel: GPU-driven batch culling readout and feature priority/enable authoring.
    // Opened and closed via ctx.renderFeaturesWindowOpen.
    RenderFeaturesPanel m_renderFeaturesPanel;

    // "Jobs" panel, docked with Memory/Profiler. Depends only on the profiler and job system.
    JobsPanel m_jobsPanel;

    // "Log" panel, docked with Memory/Profiler/Jobs/Atmosphere. Depends only on the thread-safe logger state.
    LogPanel m_logPanel;

    // Floating "Frame Debugger" window, opened from the Window menu. It owns an ImGui preview descriptor,
    // so the destructor must release it before ImGui_ImplVulkan_Shutdown().
    FrameDebuggerPanel m_frameDebuggerPanel;

#if GTE_ENABLE_PROJECT_PANEL
    // "Project" panel: live folder view plus external drag-and-drop import.
    // ProcessEvent() feeds it SDL_EVENT_DROP_FILE.
    ProjectPanel m_projectPanel;

    // Inspector's image preview for decodable image files.
    // Reset() in the destructor before ImGui_ImplVulkan_Shutdown().
    AssetPreviewTexture m_assetPreview;

    // Inspector's 3D mesh preview for *.gta Mesh assets. Reset() in the destructor, like m_assetPreview.
    AssetPreviewMesh m_assetPreviewMesh;

    // Bone Viewer floating window, opened from the Inspector; keeps its entity when the selection changes.
    // Reset() in the destructor, like m_assetPreview.
    BoneViewerWindow m_boneViewer;

    // Mtime-cached rig data shared by BoneViewerWindow and InspectorPanel, so both read one copy.
    ModelRigCache m_modelRigCache;
#endif

    // "Create New Project" floating window (Project > New Project...). No GPU resources, so no Reset() needed.
    // Outside GTE_ENABLE_PROJECT_PANEL: it must exist in every build.
    NewProjectWindow m_newProjectWindow;
    // "Open Project" floating window (Project > Open Project...). No GPU resources; always present.
    OpenProjectWindow m_openProjectWindow;
    // Non-owning; EditorHost's static capability outlives this object.
    IProjectLifecycleCapability* m_projectLifecycleCapability = nullptr;
    // "Create New Asset" floating window, opened from the Project panel's Create submenu.
    // No GPU resources; exists regardless of GTE_ENABLE_PROJECT_PANEL.
    CreateAssetWindow m_createAssetWindow;
    // Non-owning; EditorHost's static capability outlives this object.
    IAssetScaffoldingCapability* m_assetScaffoldingCapability = nullptr;
    // Non-owning; EditorHost's static capability outlives this object. Read by BuildDockspaceAndMenuBar()
    // because the "Compile" menu item lives in the menu bar.
    IHotReloadDebugCapability* m_hotReloadDebugCapability = nullptr;

    // Shared state read and written by the dock layout and every panel builder; see EditorContext.h.
    EditorContext m_ctx;

    // The one cluster build runner of this editor. EditorHost polls it; the Inspector starts builds on it.
    MeshClusterBuildRunner m_clusterBuildRunner;

    // Owns every built-in feature's Editor panel module; lives for the rest of the process.
    std::vector<std::unique_ptr<IEditorPanelModule_v1>> m_builtinFeaturePanelModules;
};

} // namespace

std::unique_ptr<IEditorLayer> CreateEditorLayer(Window& window, Renderer& renderer)
{
    // Install the debug-name overlay first: the view RenderTextures built by the constructor's
    // member initializers name themselves on construction.
    EditorGpuMemoryNameOverlay::Install(*renderer.GetMemoryTracker());
    return std::make_unique<ImGuiEditorLayer>(window, renderer);
}

} // namespace gte
