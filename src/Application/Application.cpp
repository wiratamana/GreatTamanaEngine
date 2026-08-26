#include "Application.h"

#include "EventTranslator.h"
#include "MemorySnapshotBuilder.h"

#include "../Memory/SdlMemoryTracker.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/ScopeTimer.h"

#include <SDL3/SDL.h>

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
        // Game::Render() (clear + queue this frame's draws, see Game.h) is
        // called once per VISIBLE target, immediately followed by the
        // RenderOffscreen() call that consumes/clears that queue into it -
        // see FrameRecorder.h for why a target must consume the queue
        // before the next Render() call re-queues it for a different
        // target/aspect ratio. If "Scene" and "Game" are tabbed together,
        // only one of these two runs (at zero extra GPU cost for the
        // hidden one); split apart, both run, each into its own
        // RenderTexture/aspect ratio.
        RenderTexture* gameTarget = m_editorLayer->GameViewTarget();
        RenderTexture* sceneTarget = m_editorLayer->SceneViewTarget();

        if (gameTarget != nullptr) {
            const VkExtent2D extent = gameTarget->Extent();
            m_game.Render(m_renderer, AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height)));
            GTE_PROFILE_SCOPE("Renderer::RenderOffscreen(GameView)");
            const DrawStats gameViewStats = m_renderer.RenderOffscreen(*gameTarget, GpuTimingSlot::Offscreen0);
            // Not #if GTE_ENABLE_PROFILER-gated - see AGENTS.md's "Profiling"
            // section and this same file's own BeginFrame()/EndFrame() calls
            // above, which aren't gated either; only GTE_PROFILE_SCOPE(...)'s
            // own macro body is compile-time-gated.
            Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::GameView,
                Profiling::GpuSampleStatus::Present, gameViewStats.drawCallCount, gameViewStats.triangleCount);
            // Phase 4C (PHASE4_GPU_TIMESTAMP_QUERIES_STRATEGY_v2.md) - real
            // GPU timing, reported through the same guard that already
            // proves this pass ran this frame (see this block's own comment
            // above for why re-reading a stale value would never happen).
            const GpuTimingSample gameViewTiming = m_renderer.LastGpuTiming(GpuTimingSlot::Offscreen0);
            Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::GameView,
                ToProfilingGpuSampleStatus(gameViewTiming.status), gameViewTiming.milliseconds);
        }
        if (sceneTarget != nullptr) {
            const VkExtent2D extent = sceneTarget->Extent();
            const float aspect = AspectRatioOf(static_cast<int>(extent.width), static_cast<int>(extent.height));
            // Unlike the Game view above, the Scene view renders through
            // the Editor's OWN independently-orbitable camera (see
            // src/Editor/EditorCamera.h) rather than whatever ECS entity
            // currently has the active Camera component - see
            // IEditorLayer::SceneViewProjection()/Game::Render()'s
            // viewProjectionOverride parameter.
            const Mat4 sceneViewProjection = m_editorLayer->SceneViewProjection(aspect);
            m_game.Render(m_renderer, aspect, &sceneViewProjection);
            GTE_PROFILE_SCOPE("Renderer::RenderOffscreen(SceneView)");
            const DrawStats sceneViewStats = m_renderer.RenderOffscreen(*sceneTarget, GpuTimingSlot::Offscreen1);
            Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::SceneView,
                Profiling::GpuSampleStatus::Present, sceneViewStats.drawCallCount, sceneViewStats.triangleCount);
            const GpuTimingSample sceneViewTiming = m_renderer.LastGpuTiming(GpuTimingSlot::Offscreen1);
            Profiling::FrameProfiler::Instance().SetGpuPassTiming(Profiling::GpuPass::SceneView,
                ToProfilingGpuSampleStatus(sceneViewTiming.status), sceneViewTiming.milliseconds);
        }
        if (gameTarget == nullptr && sceneTarget == nullptr) {
            // No Editor at all (release build - always takes this path), or
            // an Editor build where both "Game" and "Scene" happen to be
            // hidden simultaneously (a rare edge case - see
            // EditorContext::gameViewVisible/sceneViewVisible) - render
            // straight to the swapchain instead, at the OS window's own
            // aspect ratio, exactly like a release build always did before
            // "Scene" got its own RenderTexture.
            m_game.Render(m_renderer, AspectRatioOf(m_windowWidth, m_windowHeight));
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
            m_editorLayer->BuildUI(m_game, m_renderer);
        }

        // File > Exit (or any other future programmatic "close" UI action)
        // ends the loop exactly like a Quit event/closing the OS window.
        if (m_editorLayer->WantsExit()) {
            running = false;
        }

        // Present the swapchain. In an Editor build this draws the editor's
        // own ImGui chrome (which itself displays the Game/Scene views
        // above) via the recordExtra hook; in a release build recordExtra is
        // effectively a no-op (NullEditorLayer::Render does nothing) and
        // this just presents whatever Game rendered straight into the
        // swapchain moments ago.
        {
            GTE_PROFILE_SCOPE("Renderer::Present");
            const std::optional<DrawStats> presentStats =
                m_renderer.Present([this](VkCommandBuffer cmd) { m_editorLayer->Render(cmd); });
            if (presentStats.has_value()) {
                Profiling::FrameProfiler::Instance().SetGpuPassDrawStats(Profiling::GpuPass::Present,
                    Profiling::GpuSampleStatus::Present, presentStats->drawCallCount, presentStats->triangleCount);
            }
            // else: Present() recorded nothing this frame (minimized window,
            // pending resize, or a just-recreated swapchain - see
            // FramePresenter::Present()'s own comment) - GpuPass::Present's
            // countStatus correctly stays at its default
            // GpuSampleStatus::Absent, with no extra code needed.
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
