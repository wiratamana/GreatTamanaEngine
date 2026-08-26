#include "Application.h"

#include "EventTranslator.h"
#include "MemorySnapshotBuilder.h"
#include "RenderPasses.h"

#include "../Memory/SdlMemoryTracker.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/ScopeTimer.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"

#include <SDL3/SDL.h>

#include <cassert>
#include <cstdio>
#include <stdexcept>

namespace gte {

namespace {

// Aspect ratio (width / height) of a render target, for
// RenderSystem::Draw()/Game::Render() - see Application::Run() below. Falls
// back to a square (1.0f) for a degenerate/zero-height extent (e.g. a
// render target caught mid-resize, or a minimized window) rather than
// dividing by zero.
float AspectRatioOf(int width, int height) noexcept
{
    return height > 0 ? static_cast<float>(width) / static_cast<float>(height) : 1.0f;
}

// Phase 4C (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - the one, tiny
// bridge from Renderer's own (Profiling-free) GpuTimingSample::Status into
// Profiling::GpuSampleStatus - see Application::Run()'s Game/Scene/Present
// blocks below. Trivial by design (a straightforward 1:1 switch), same
// judgment already applied to AspectRatioOf() above - not worth its own
// Tier-1 test file.
Profiling::GpuSampleStatus ToProfilingGpuSampleStatus(GpuTimingSample::Status status) noexcept
{
    switch (status) {
    case GpuTimingSample::Status::Present:
        return Profiling::GpuSampleStatus::Present;
    case GpuTimingSample::Status::Unsupported:
        return Profiling::GpuSampleStatus::Unsupported;
    case GpuTimingSample::Status::Absent:
    default:
        return Profiling::GpuSampleStatus::Absent;
    }
}

} // namespace

Application::SdlContext::SdlContext()
{
#if GTE_ENABLE_EDITOR
    // Must be installed before SDL_Init() - indeed, before literally any SDL
    // call - see SdlMemoryTracker's own doc comment for why. Gated behind
    // GTE_ENABLE_EDITOR (not installed at all in a release build) since the
    // only consumer of these numbers is the Editor's "Memory" panel - a
    // release build would otherwise pay real per-allocation tracking
    // overhead (an extra pointer-arithmetic header + atomic increment on
    // EVERY SDL_malloc/calloc/realloc/free call, for the rest of the
    // process's lifetime) for a feature nothing in that build can ever
    // display. See AGENTS.md ("CPU Dependency Memory Tracking").
    SdlMemoryTracker::Install();
#endif

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
}

Application::SdlContext::~SdlContext()
{
    SDL_Quit();
}

Application::Application(const std::string& title, int width, int height)
    : m_sdlContext()
    , m_window(title, width, height)
    , m_renderer(m_window)
    , m_renderGraph(m_renderer)
    , m_editorLayer(CreateEditorLayer(m_window, m_renderer))
    , m_game()
    , m_windowWidth(width)
    , m_windowHeight(height)
{
}

Application::~Application() = default;

int Application::Run()
{
    bool running = true;
    // Identifies Application's own main game window among possibly several
    // real SDL windows (once Dear ImGui's multi-viewport feature creates
    // extra ones for panels dragged outside it) - see the mainWindowId doc
    // comment on EventTranslator::Translate(). Fetched once: an SDL window's
    // ID never changes over its lifetime.
    const Uint32 mainWindowId = m_window.Id();
    Uint64 lastTicksNs = SDL_GetTicksNS();

    InputState inputState;

    while (running) {
        // Brackets the WHOLE frame for the Profiling module (src/Profiling/)
        // - see PROFILER_STRATEGY_v2.md, Phase 1. A true no-op (frame count
        // doesn't advance) whenever GTE_ENABLE_PROFILER is off or the
        // runtime capture-enabled flag is false - see
        // Profiling::FrameProfiler.
        Profiling::FrameProfiler::Instance().BeginFrame();

        // Phase 4C (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - the
        // runtime layer of GpuTimingService's two-layer on/off gate (see
        // AGENTS.md, "Profiling", and Renderer::SetGpuTimingCaptureEnabled()'s
        // own doc comment): reuses the Editor's EXISTING "Capture" checkbox
        // rather than adding a new, Profiler-panel-specific control. A
        // plain bool crosses this boundary - Renderer stays completely free
        // of any Profiling/ header either way.
        m_renderer.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());
        // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - the same runtime toggle,
        // now ALSO applied to the render graph's own, independent
        // RenderGraphTimestampPool (m_renderGraph never shares
        // GpuTimingService's pool with Renderer - see RenderGraph.h's own
        // "GPU TIMING NOTE").
        m_renderGraph.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());

        // Clear last frame's transient "just pressed/released" flags and
        // per-frame mouse/wheel deltas before this frame's events arrive.
        inputState.BeginFrame();

        {
            GTE_PROFILE_SCOPE("Application::PollEvents");
            SDL_Event sdlEvent;
            while (SDL_PollEvent(&sdlEvent)) {
                // Editor gets first look at every raw event (its own SDL3
                // backend tracks mouse/keyboard for ImGui widgets) - a no-op in
                // a release build (NullEditorLayer).
                m_editorLayer->ProcessEvent(sdlEvent);

                const std::optional<Event> event = EventTranslator::Translate(sdlEvent, mainWindowId);
                if (!event.has_value()) {
                    continue;
                }

                if (event->type == EventType::Quit) {
                    running = false;
                } else if (event->type == EventType::WindowResized) {
                    const auto& resized = std::get<WindowResizedEventData>(event->data);
                    m_windowWidth = resized.width;
                    m_windowHeight = resized.height;
                    m_renderer.OnResize(resized.width, resized.height);
                    // Keeps the Editor's Game-view RenderTexture tracking the
                    // window's size (no-op in a release build) - see
                    // ImGuiEditorLayer::OnWindowResized.
                    m_editorLayer->OnWindowResized(resized.width, resized.height);
                }

                // Withhold mouse/keyboard events the Editor UI itself wants this
                // frame (e.g. the cursor is over an ImGui panel, a slider is
                // being dragged, a text field has keyboard focus, ...) from
                // ever reaching gameplay - otherwise clicking/typing into the
                // Editor's own panels would ALSO register as gameplay input
                // underneath them (the classic ImGui-in-a-game-engine
                // "click-through" problem). WantsCaptureMouse()/
                // WantsCaptureKeyboard() are always false for NullEditorLayer,
                // so a release build forwards every event to Game exactly as
                // before this check existed. Quit/WindowResized above are
                // handled unconditionally regardless of this - they aren't
                // input Game reacts to via InputState/OnEvent() in this sense,
                // and Renderer/the Editor's own resize handling must always see
                // them either way.
                const bool isMouseEvent = event->type == EventType::MouseMoved
                    || event->type == EventType::MouseButtonDown
                    || event->type == EventType::MouseButtonUp
                    || event->type == EventType::MouseWheel;
                const bool isKeyboardEvent = event->type == EventType::KeyDown || event->type == EventType::KeyUp;

                const bool consumedByEditorUI = (isMouseEvent && m_editorLayer->WantsCaptureMouse())
                    || (isKeyboardEvent && m_editorLayer->WantsCaptureKeyboard());

                if (!consumedByEditorUI) {
                    inputState.Apply(*event);
                    m_game.OnEvent(*event);
                }
            }
        }

        const Uint64 nowTicksNs = SDL_GetTicksNS();
        const double deltaSeconds = static_cast<double>(nowTicksNs - lastTicksNs) / 1000000000.0;
        lastTicksNs = nowTicksNs;

        m_editorLayer->NewFrame();

        // Clears last frame's queued Submit() draw items before Game gets a
        // chance to queue this frame's - see Renderer::BeginFrame().
        m_renderer.BeginFrame();

        // Game::Update() itself is already wrapped in
        // GTE_PROFILE_SCOPE("Game::Update") (see src/Game/Game.cpp) - not
        // wrapped again here too, since the flat, name-keyed CPU scope
        // aggregation model (see AGENTS.md, "Profiling") would otherwise
        // double-count identically-named nested scopes rather than
        // measuring the same call twice for no reason.
        m_game.Update(deltaSeconds, inputState);

        // Ask the Editor where Game's frame(s) should actually land this
        // frame: an off-screen RenderTexture per visible panel ("Game"
        // and/or "Scene", each independently - an Editor build), or nullptr
        // for either/both meaning "not currently visible, don't bother" (a
        // hidden/inactive dock tab) or "no Editor at all" (a release build -
        // see NullEditorLayer). This is the one seam that decides
        // Unity-style Editor-vs-final-build rendering, and it lives here in
        // Application (the composition root), not in Game.
        //
        // Render Graph migration (Phase 7 -
        // RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md): Game
        // view / Scene view / Present are recorded via TWO
        // RenderGraph::Execute() calls per frame - one for the SYNCHRONOUS
        // offscreen regime (Game+Scene together, sharing FramePresenter's
        // own dedicated offscreen command buffer/fence), one for the
        // PIPELINED swapchain regime (Present alone) - see RenderPasses.h
        // and this class's own m_renderGraph member. A dependency-cycle
        // exception from RenderGraphCompiler::Compile() is structurally
        // unreachable through any graph this engine declares today (see
        // RENDERGRAPH_PHASE3_COMPLETION_REPORT.md) but is still caught
        // here, loudly, per RENDERGRAPH_PHASE6_COMPLETION_REPORT.md's own
        // Step 3.5 guidance - never silently swallowed.
        RenderTexture* gameTarget = m_editorLayer->GameViewTarget();
        RenderTexture* sceneTarget = m_editorLayer->SceneViewTarget();

        // Call 1 of 2: the SYNCHRONOUS offscreen regime - Game view + Scene
        // view together. Runs unconditionally whenever either target is
        // non-null, completely independent of whatever the swapchain is
        // doing this frame (a minimized OS window does not affect this
        // call at all).
        if (gameTarget != nullptr || sceneTarget != nullptr) {
            try {
                GTE_PROFILE_SCOPE("RenderGraph::Execute(Offscreen)");
                const VkCommandBuffer offscreenCmd = m_renderer.BeginOffscreenRenderGraphRecording();
                m_renderGraph.Execute(offscreenCmd, rg::ExecuteTimingMode::SynchronousImmediateReadback,
                    [&](rg::RenderGraphBuilder& b) {
                        std::vector<rg::TextureHandle> outputs;
                        if (gameTarget != nullptr) {
                            const VkExtent2D extent = gameTarget->Extent();
                            const float aspect =
                                AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));
                            const rg::TextureHandle h =
                                b.ImportTexture("GameView", gameTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
                            AddGameViewPass(b, m_game, m_renderer, h, aspect);
                            outputs.push_back(h);
                        }
                        if (sceneTarget != nullptr) {
                            const VkExtent2D extent = sceneTarget->Extent();
                            const float aspect =
                                AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));
                            // Unlike the Game view above, the Scene view
                            // renders through the Editor's OWN independently-
                            // orbitable camera (see src/Editor/EditorCamera.h)
                            // rather than whatever ECS entity currently has
                            // the active Camera component - see
                            // IEditorLayer::SceneViewProjection()/
                            // Game::Render()'s viewProjectionOverride
                            // parameter.
                            const Mat4 sceneViewProjection = m_editorLayer->SceneViewProjection(aspect);
                            const rg::TextureHandle h =
                                b.ImportTexture("SceneView", sceneTarget->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
                            AddSceneViewPass(b, m_game, m_renderer, h, aspect, sceneViewProjection);
                            outputs.push_back(h);
                        }
                        return outputs;
                    });

                // Manual finalize: transitions whichever of Game/Scene were
                // actually rendered this call from ColorAttachmentWrite to a
                // real ShaderRead layout, for Dear ImGui's own (render-
                // graph-external) descriptor set to sample during the
                // Present regime call below - see RenderPasses.h's own doc
                // comment on FinalizeRenderTextureForExternalSampling().
                if (gameTarget != nullptr) {
                    FinalizeRenderTextureForExternalSampling(offscreenCmd, *gameTarget);
                }
                if (sceneTarget != nullptr) {
                    FinalizeRenderTextureForExternalSampling(offscreenCmd, *sceneTarget);
                }

                m_renderer.EndOffscreenRenderGraphRecording();

                // B.1 (B1_REAL_GPU_TIMING_STRATEGY_v1.md) - must run
                // immediately after EndOffscreenRenderGraphRecording()
                // returns (i.e. after that call's own fence wait has
                // already completed) - reads back every real GPU
                // timestamp written during the SynchronousImmediateReadback
                // Execute() call just above, for whichever of "GameView"/
                // "SceneView" actually ran this frame.
                m_renderGraph.FinalizeSynchronousGpuTiming();
            } catch (const std::exception& e) {
                std::fprintf(stderr, "RenderGraph offscreen Execute() failed: %s\n", e.what());
                assert(false && "RenderGraph offscreen Execute() threw - see stderr");
            }
        }

        // Not #if GTE_ENABLE_PROFILER-gated - see AGENTS.md's "Profiling"
        // section and this same function's own BeginFrame()/EndFrame()
        // calls, which aren't gated either; only GTE_PROFILE_SCOPE(...)'s
        // own macro body is compile-time-gated. "GameView"/"SceneView" must
        // match RenderPasses.cpp's own AddGameViewPass()/AddSceneViewPass()
        // pass name literals exactly.
        if (gameTarget != nullptr) {
            const rg::PassGpuStats gameViewStats = m_renderGraph.LastKnownStatsFor("GameView");
            Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::GameView,
                Profiling::GpuSampleStatus::Present, gameViewStats.drawStats.drawCallCount,
                gameViewStats.drawStats.triangleCount);
            Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::GameView,
                ToProfilingGpuSampleStatus(gameViewStats.timing.status), gameViewStats.timing.milliseconds);
        }
        if (sceneTarget != nullptr) {
            const rg::PassGpuStats sceneViewStats = m_renderGraph.LastKnownStatsFor("SceneView");
            Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::SceneView,
                Profiling::GpuSampleStatus::Present, sceneViewStats.drawStats.drawCallCount,
                sceneViewStats.drawStats.triangleCount);
            Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::SceneView,
                ToProfilingGpuSampleStatus(sceneViewStats.timing.status), sceneViewStats.timing.milliseconds);
        }

        // Build every editor panel (Hierarchy/Inspector/Scene/Game/Memory/menu
        // bar) now that the Game/Scene view textures (if any) have this
        // frame's contents. Passes Game itself (Hierarchy/Inspector observe/
        // edit its ECS world via Game::GetRegistry(), and Hierarchy's
        // "Create 3D Object" menu spawns entities via
        // Game::CreatePrimitiveEntity() - see IEditorLayer::BuildUI()) and
        // Renderer itself (Memory - see Renderer::GetMemoryTotals()/
        // GetMemoryResources()).
        {
            GTE_PROFILE_SCOPE("IEditorLayer::BuildUI");
            m_editorLayer->BuildUI(m_game, m_renderer, m_renderGraph);
        }

        // File > Exit (or any other future programmatic "close" UI action)
        // ends the loop exactly like a Quit event/closing the OS window.
        if (m_editorLayer->WantsExit()) {
            running = false;
        }

        // Call 2 of 2: the PIPELINED swapchain-present regime - Present
        // alone. In an Editor build this draws the editor's own ImGui
        // chrome (which itself displays the Game/Scene views above) via the
        // recordImGui hook; in a release build (or the rare Editor edge
        // case where both "Game"/"Scene" are simultaneously hidden) it ALSO
        // renders Game directly into the swapchain first - see
        // RenderPasses.h's AddPresentPass().
        {
            GTE_PROFILE_SCOPE("Renderer::PresentViaRenderGraph");
            const bool needsDirectGameRender = (gameTarget == nullptr && sceneTarget == nullptr);
            const std::optional<float> directGameRenderAspect = needsDirectGameRender
                ? std::optional<float>(AspectRatioOf(m_windowWidth, m_windowHeight))
                : std::nullopt;

            std::optional<DrawStats> presentStats;
            try {
                presentStats = m_renderer.PresentViaRenderGraph(m_renderGraph, needsDirectGameRender,
                    [&](rg::RenderGraphBuilder& b, rg::TextureHandle swapchainImage) {
                        AddPresentPass(b, m_game, m_renderer, swapchainImage, directGameRenderAspect,
                            [this](VkCommandBuffer cmd) { m_editorLayer->Render(cmd); });
                        return std::vector<rg::TextureHandle>{ swapchainImage };
                    });
            } catch (const std::exception& e) {
                std::fprintf(stderr, "RenderGraph Present Execute() failed: %s\n", e.what());
                assert(false && "RenderGraph Present Execute() threw - see stderr");
            }

            if (presentStats.has_value()) {
                Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::Present,
                    Profiling::GpuSampleStatus::Present, presentStats->drawCallCount, presentStats->triangleCount);
                // "Present" must match RenderPasses.cpp's own
                // AddPresentPass() pass name literal exactly.
                const GpuTimingSample presentTiming = m_renderGraph.LastKnownStatsFor("Present").timing;
                Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::Present,
                    ToProfilingGpuSampleStatus(presentTiming.status), presentTiming.milliseconds);
            }
            // else: PresentViaRenderGraph() recorded nothing this frame
            // (minimized window, pending resize, or a just-recreated
            // swapchain) - GpuPass::Present's countStatus AND timingStatus
            // both correctly stay at their default GpuSampleStatus::Absent,
            // with no extra code needed.
        }

        // Update/present any panel the user has dragged outside the main OS
        // window (Dear ImGui multi-viewport/"platform windows" - a no-op in
        // a release build, see NullEditorLayer::RenderPlatformWindows()).
        // Deliberately AFTER the main swapchain Present() above: each such
        // window owns its own, completely independent Vulkan swapchain, so
        // there is no ordering requirement against the main window's own
        // present - see IEditorLayer::RenderPlatformWindows().
        m_editorLayer->RenderPlatformWindows();

        // Phase 5 (GPU memory usage over time) - see PHASE5_GPU_MEMORY_
        // HISTORY_STRATEGY_v2.md: one real GPU memory snapshot per
        // profiler frame, taken as late as possible in the frame (still
        // inside this BeginFrame()/EndFrame() bracket) so it reflects
        // every resource created/destroyed anywhere this frame, including
        // by IEditorLayer::BuildUI()'s own Inspector/Project-panel asset
        // loading above. Unconditional - not #if GTE_ENABLE_PROFILER/
        // GTE_ENABLE_EDITOR-gated, matching this same function's own
        // BeginFrame()/EndFrame()/SetGpuPassDrawStats() calls, none of
        // which are gated either (only GTE_PROFILE_SCOPE(...)'s own macro
        // body is compile-time-gated - see AGENTS.md, "Profiling").
        // Renderer::GetMemoryTotals() is O(1) and always meaningful (no
        // "didn't run this frame" concept, unlike a GpuPass's draw-call
        // count), so this is always GpuSampleStatus::Present.
        Profiling::FrameProfiler::Instance().SetMemorySnapshot(BuildMemorySnapshot(m_renderer.GetMemoryTotals()));

        Profiling::FrameProfiler::Instance().EndFrame();
    }

    return 0;
}

} // namespace gte
