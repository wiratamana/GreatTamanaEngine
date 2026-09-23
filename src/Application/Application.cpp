#include "Application.h"

#include "EventTranslator.h"
#include "EngineCommandDispatch.h"
#include "MemorySnapshotBuilder.h"

#include "../Editor/Logger.h" // PHASE16 of editor-core-separation-1 moves this include (and the InstallLogSink() call in the constructor body below) into EditorHost - Application still needs it directly for now (this file still owns the ONE composition-root call site that installs the real sink).
// editor-core-separation-1 campaign, PHASE6
// (PHASE6_EDITOR_CAPABILITY_CALL_SITE_CONVERSION_SCENE_IO.md) - the REAL
// ISceneIOCapability implementation. Unconditional since PHASE8
// (GTE_ENABLE_EDITOR no longer exists anywhere in this codebase) - Phase 16
// moves this whole wiring concern into EditorHost instead.
#include "../Editor/EditorSceneIOCapability.h"
#include "../Encoding/DepthVisualization.h"
#include "../Encoding/HdrColorVisualization.h"
#include "../Encoding/PixelConversion.h"
#include "../Encoding/PngEncoder.h"
#include "../Memory/SdlMemoryTracker.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/ScopeTimer.h"
#include "../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../Renderer/RenderGraph/RenderGraphDebugTextureRegistry.h"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>
#include <stdexcept>

namespace gte {

namespace {

// network-impl-2 campaign, Phase 3
// (PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md) - true for the
// two BGRA channel-order formats VulkanSwapchain.cpp's ChooseSurfaceFormat()
// is actually known to negotiate (it prefers VK_FORMAT_B8G8R8A8_UNORM but
// falls back to formats.front(), i.e. whatever the platform/driver reports
// first, if that exact combination isn't available). An unrecognized
// BGRA-like variant this two-value check doesn't catch would silently
// produce a channel-swapped (red/blue reversed) PNG with no error at all -
// an accepted, narrow risk (see PHASE3's own Non-Goals) - if a future
// "screenshot has wrong colors" report ever shows up, start here.
bool IsBgraFormat(VkFormat format) noexcept
{
    return format == VK_FORMAT_B8G8R8A8_UNORM || format == VK_FORMAT_B8G8R8A8_SRGB;
}

// network-impl-4 campaign, Phase 5 - GET /list_textures' own small,
// human-readable resolution helpers. Both are deliberately narrow (only the
// enumerators/formats this engine's render graph can actually produce
// today), mirroring IsBgraFormat()'s own "accepted narrow risk, documented"
// precedent immediately above - an unrecognized regime can never actually
// occur (ExecuteTimingMode has exactly two enumerators, both handled), and
// an unrecognized VkFormat falls back to a safe, clearly-labeled numeric
// string rather than a crash or a silently-wrong label, mirroring
// src/Editor/MemoryPanelData.cpp's own ToString(VkFormat) fallback
// convention (not reused directly - that function lives under
// GTE_ENABLE_EDITOR-gated src/Editor/, and this call site must work in
// every build configuration, editor or not).
const char* ToDebugTextureRegimeString(rg::ExecuteTimingMode mode)
{
    switch (mode) {
    case rg::ExecuteTimingMode::SynchronousImmediateReadback: return "synchronous";
    case rg::ExecuteTimingMode::PipelinedDeferredReadback: return "pipelined";
    }
    return "unknown"; // Unreachable - every real enumerator handled above.
}

std::string DebugTextureColorFormatName(VkFormat format)
{
    switch (format) {
    case VK_FORMAT_B8G8R8A8_UNORM: return "B8G8R8A8_UNORM";
    case VK_FORMAT_B8G8R8A8_SRGB: return "B8G8R8A8_SRGB";
    case VK_FORMAT_R8G8B8A8_UNORM: return "R8G8B8A8_UNORM";
    case VK_FORMAT_R8G8B8A8_SRGB: return "R8G8B8A8_SRGB";
    default: break;
    }
    char buffer[32];
    std::snprintf(buffer, sizeof(buffer), "VkFormat(%d)", static_cast<int>(format));
    return std::string(buffer);
}

} // namespace

Application::SdlContext::SdlContext()
{
    // Must be installed before SDL_Init() - indeed, before literally any SDL
    // call - see SdlMemoryTracker's own doc comment for why. Unconditional
    // since PHASE8 of editor-core-separation-1 (GTE_ENABLE_EDITOR no longer
    // exists anywhere in this codebase) - the only consumer of these numbers
    // is the Editor's "Memory" panel; a future Player host that never builds
    // gte_editor's Memory panel simply never reads these tracked numbers.
    // See AGENTS.md ("CPU Dependency Memory Tracking").
    SdlMemoryTracker::Install();

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
    // editor-core-separation-1 campaign, PHASE12 - m_hostServices has no
    // constructor dependencies of its own (a stateless adapter); m_core is
    // constructed injecting m_window (as ISurfaceProvider&) and
    // m_hostServices (as IHostServices&) - both already fully constructed
    // by this point (Application.h's own member declaration order).
    , m_hostServices()
    , m_core(m_window, m_hostServices)
    // The lines below now bind REFERENCE members to m_core's own real,
    // owned instances (see Application.h's own member doc comments for the
    // full "why a reference" reasoning) - m_core is already fully
    // constructed by this point.
    , m_renderer(m_core.GetRenderer())
    , m_renderGraph(m_core.GetRenderGraph())
    , m_editorLayer(CreateEditorLayer(m_window, m_renderer))
    , m_game(m_core.GetGame())
    , m_engineContext(m_core.GetEngineContext())
    , m_atmosphereSettings(m_core.GetAtmosphereSettings())
    , m_atmosphereLutRenderer(m_core.GetAtmosphereLutRenderer())
    // network-impl-2 campaign, Phase 3 - hands FrameCaptureBridge's address
    // into NetworkServer's constructor (a defaulted pointer parameter - see
    // NetworkServer.h) so its /get_game_view route handler can reach it.
    // Safe: m_captureBridge is declared (and thus constructed) before
    // m_networkServer, per Application.h's own member ordering.
    // network-impl-3 campaign, Phase 4 - ALSO hands EngineCommandBridge's
    // address into NetworkServer's constructor (a second, appended
    // defaulted pointer parameter), for the exact same reason -
    // m_commandBridge is likewise declared before m_networkServer.
    // network-impl-7 campaign, Phase 3 - ALSO hands EditorUiCommandBridge's
    // address into NetworkServer's constructor (a third, appended defaulted
    // pointer parameter), for the exact same reason - m_uiCommandBridge is
    // likewise declared before m_networkServer.
    // task_manager/frame-debugger-3 campaign, PHASE7 - ALSO hands
    // FrameDebuggerCommandBridge's address into NetworkServer's constructor
    // (a fourth, appended defaulted pointer parameter), for the exact same
    // reason - m_frameDebuggerCommandBridge is likewise declared before
    // m_networkServer.
    // task_manager/stl-parser-2 campaign, PHASE1 - ALSO hands
    // AssetImportCommandBridge's address into NetworkServer's constructor
    // (a fifth, appended defaulted pointer parameter), for the exact same
    // reason - m_assetImportCommandBridge is likewise declared before
    // m_networkServer.
    , m_networkServer(&m_captureBridge, &m_commandBridge, &m_uiCommandBridge, &m_frameDebuggerCommandBridge,
          &m_assetImportCommandBridge)
{
    // editor-core-separation-1 campaign, PHASE3
    // (PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md) - installs the ONE real
    // ILogSink this engine ships (Editor/Logger.h's LoggerLogSink) into the
    // new global sink mechanism (Core/LogSink.h), FIRST, before anything
    // else in this constructor body runs (including NetworkServer::Start()
    // immediately below, which can itself call GTE_LOG_*) - install-once,
    // idempotent, mirrors SdlMemoryTracker::Install()'s own "before first
    // use" timing rule. Phase 16 of this same campaign moves this one call
    // site into EditorHost's own constructor instead, once EditorHost
    // exists.
    gte::InstallLogSink(&gte::LoggerLogSink::Instance());

    // editor-core-separation-1 campaign, PHASE12
    // (PHASE12_CORE_CLASS_SKELETON_AND_CONSTRUCTION.md, PHASE0's Locked
    // Design Decision #8) - wires m_core's own nullable IEditorLayer* hook
    // to the real, already-constructed m_editorLayer instance. Called here,
    // from the constructor BODY (never the initializer list) specifically
    // because m_editorLayer must already be fully constructed first - by
    // this point in the body every member's own constructor has already
    // run, so this is safe regardless of m_core's/m_editorLayer's relative
    // declaration order.
    m_core.SetEditorLayerHook(m_editorLayer.get());

    // editor-core-separation-1 campaign, PHASE13
    // (PHASE13_CORE_FRAME_ORCHESTRATION_EXTRACTION.md) - hands Core the ONE
    // callback that actually calls IEditorLayer::Render(cmd) - an explicitly
    // HOST-LEVEL IEditorLayer method (Locked Design Decision #8's second
    // bucket) that Application (not Core) must keep calling directly
    // against its own m_editorLayer. This lambda captures `this`
    // (Application's own `this`, never Core's), so the real call still
    // textually happens here, in Application.cpp, exactly as it always has
    // - Core merely stores/forwards this std::function value, never calling
    // IEditorLayer::Render() itself. Set once (this callback's own behavior
    // never varies frame-to-frame) - see Core::SetPresentImGuiRecorder()'s
    // own doc comment (Core.h).
    m_core.SetPresentImGuiRecorder([this](VkCommandBuffer cmd) { m_editorLayer->Render(cmd); });

    // editor-core-separation-1 campaign, PHASE6
    // (PHASE6_EDITOR_CAPABILITY_CALL_SITE_CONVERSION_SCENE_IO.md) - wires the
    // ONE real ISceneIOCapability implementation this engine ships
    // (Editor/EditorSceneIOCapability.h) into the nullable pointer
    // EngineCommandDispatch.cpp (Core-destined, always-compiled) now consults
    // at runtime instead of a compile-time `#if` (this call site is now
    // unconditional since PHASE8 - GTE_ENABLE_EDITOR no longer exists
    // anywhere in this codebase). A function-local `static` (mirrors
    // LoggerLogSink::Instance()'s own Meyers-singleton precedent immediately
    // above) - EditorSceneIOCapability is pure delegation with no state
    // of its own, so one whole-process-lifetime instance is all this needs.
    static EditorSceneIOCapability s_editorSceneIOCapability;
    SetSceneIOCapability(&s_editorSceneIOCapability);

#if GTE_ENABLE_NETWORK
    // Loopback-only (127.0.0.1 is baked into NetworkServer itself - Start()
    // deliberately has no host parameter, see Phase 2), port 8080 - see
    // AGENTS.md, "Networking", and
    // task_manager/network-impl-1/PHASE0_MASTER_STRATEGY.md's locked
    // design decisions for why this is hardcoded rather than configurable
    // yet, and why this is safe to auto-start unconditionally. A bind
    // failure (e.g. another instance of the engine already running and
    // holding port 8080) is logged by NetworkServer itself and is
    // NON-FATAL - the rest of Application still starts normally either way.
    m_networkServer.Start(8080);
#endif

    // logger-1 campaign, Phase 2 - proves GTE_LOG_* is reachable, right
    // after every other subsystem (including the network server, above) is
    // already constructed/started. Unconditional at the language level
    // (Core/Logging.h, editor-core-separation-1 campaign's own PHASE3) -
    // reaches the real Logger only because InstallLogSink() above already
    // ran; a build/host that never calls InstallLogSink() would make this
    // a safe, silent no-op instead (see Core/LogSink.h).
    GTE_LOG_INFO("Application", "GreatTamanaEngine started.");
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
                // a release build (NullEditorLayer). HOST-LEVEL (Locked Design
                // Decision #8's second bucket) - stays directly on
                // m_editorLayer, never routed through Core.
                m_editorLayer->ProcessEvent(sdlEvent);

                const std::optional<Event> event = EventTranslator::Translate(sdlEvent, mainWindowId);
                if (!event.has_value()) {
                    continue;
                }

                if (event->type == EventType::Quit) {
                    running = false;
                } else if (event->type == EventType::WindowResized) {
                    const auto& resized = std::get<WindowResizedEventData>(event->data);
                    m_renderer.OnResize(resized.width, resized.height);
                    // editor-core-separation-1 campaign, PHASE13 - keeps
                    // Core's own present-regime "no Game/Scene panel
                    // visible, render Game directly to swapchain" fallback
                    // aspect-ratio computation correct across a live resize
                    // (mirrors this call's own former m_windowWidth/
                    // m_windowHeight cache exactly) - see
                    // Core::NotifyWindowResized()'s own doc comment.
                    m_core.NotifyWindowResized(resized.width, resized.height);
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

        // network-impl-3 campaign (task_manager/network-impl-3/) - drains at
        // most ONE pending network-issued engine command
        // (instantiate_primitive/delete_entity) per frame, as EARLY as
        // possible - right after input polling, BEFORE Game::Update()/
        // Physics/Animation run this frame - so a freshly spawned/deleted
        // entity is fully consistent for the REST of this exact frame (see
        // PHASE4_ENGINE_COMMAND_BRIDGE_AND_MAIN_LOOP_INTEGRATION.md, and
        // PHASE0_MASTER_STRATEGY.md's own Locked Design Decision #6).
        if (const std::optional<EngineCommandRequest> request = m_commandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("Application::ExecuteEngineCommand");
            const EngineCommandResult result = ExecuteEngineCommand(m_game, m_renderer, m_sceneIOCapability, *request);
            m_commandBridge.FulfillCommand(result);
        }

        const Uint64 nowTicksNs = SDL_GetTicksNS();
        const double deltaSeconds = static_cast<double>(nowTicksNs - lastTicksNs) / 1000000000.0;
        lastTicksNs = nowTicksNs;

        // frame-debugger-1 campaign, PHASE4 - read the Editor's own Pause/Step
        // toolbar state (see PHASE3's IEditorLayer::IsPlaybackPaused()/
        // TryConsumeStepRequest()) - HOST-LEVEL (Locked Design Decision #8's
        // second bucket: "Playback pause STATE itself... stays a call
        // Application makes directly against m_editorLayer BEFORE calling
        // m_core.Update(), passing the already-resolved paused/step booleans
        // IN as plain arguments"). Always false/false for a release build
        // (NullEditorLayer), so Application behaves exactly like before this
        // whole campaign whenever there is no Editor.
        const bool playbackPaused = m_editorLayer->IsPlaybackPaused();
        // Deliberately called EVERY frame, unconditionally (never short-circuited
        // by `playbackPaused &&`) so a stray/stale pending step request can never
        // linger un-cleared even in an edge case the toolbar's own "Step is
        // disabled while not paused" UI guard wasn't supposed to allow in the
        // first place - see IEditorLayer::TryConsumeStepRequest()'s own doc
        // comment.
        const bool stepRequestedRaw = m_editorLayer->TryConsumeStepRequest();
        const bool steppedThisFrame = playbackPaused && stepRequestedRaw;

        // task_manager/frame-debugger-3 campaign, PHASE3
        // (PHASE3_FRAME_HISTORY_RING_BUFFER_AND_CAPTURE_TRIGGER.md, Step
        // 3.2, call site 2) - records "a Step happened this frame" for the
        // Frame Debugger's own later use - HOST-LEVEL, stays directly on
        // m_editorLayer (Locked Design Decision #8's second bucket).
        if (steppedThisFrame) {
            m_editorLayer->NotifyFrameDebuggerStepConsumed();
        }

        // editor-core-separation-1 campaign, PHASE13
        // (PHASE13_CORE_FRAME_ORCHESTRATION_EXTRACTION.md) - advances Time
        // (respecting the already-resolved Pause/Step booleans above) and
        // dispatches Game::Update() - see Core::Update()'s own doc comment
        // and InputFrame.h's own doc comment for why these two booleans
        // travel inside InputFrame rather than growing Update()'s own frozen
        // 2-parameter signature.
        InputFrame inputFrame;
        inputFrame.inputState = &inputState;
        inputFrame.playbackPaused = playbackPaused;
        inputFrame.stepRequested = stepRequestedRaw;
        m_core.Update(inputFrame, static_cast<float>(deltaSeconds));

        // logger-1 campaign, Phase 2 - stamps every Logger entry recorded
        // from here until the next Advance() (now inside Core::Update()
        // above) with THIS frame's number. Safe to call unconditionally -
        // Logger::SetCurrentFrame() is a real no-op when no sink is
        // installed. Reads m_engineContext.time.FrameCount() via the
        // reference member bound to Core's own real EngineContext/Time
        // instance (PHASE12) - this call itself never touches m_editorLayer,
        // so it stays here directly on Application rather than moving into
        // Core (Core must never #include Editor/Logger.h - see
        // PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md).
        gte::Logger::SetCurrentFrame(m_engineContext.time.FrameCount());

        m_editorLayer->NewFrame();

        // network-impl-7 campaign - drains at most ONE pending
        // GET /activate_tab request per frame, IMMEDIATELY after
        // NewFrame() and BEFORE BuildUI() - this is the one window in the
        // frame where Dear ImGui's window/dock state is valid to touch
        // (NewFrame() already ran) AND where a change here is still
        // visible in THIS SAME frame's own tab rendering (BuildUI() has
        // not run yet - see PHASE3_APPLICATION_WIRING_AND_FRAME_LOOP_INTEGRATION.md's
        // own "why this exact frame position" reasoning for the full
        // justification). Mirrors EngineCommandBridge's own "drain as
        // early as possible" precedent above, just relative to a different
        // pair of per-frame calls.
        if (const std::optional<EditorUiCommandRequest> uiRequest = m_uiCommandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("Application::ExecuteEditorUiCommand");
            EditorUiCommandResult uiResult;
            uiResult.kind = uiRequest->kind;
            // GPU-Driven Frustum Culling + Indirect Draw campaign
            // (render-pass-5), PHASE6 - a second EditorUiCommandKind now
            // exists (SpawnGpuDrivenTestBatch) - branches exactly the same
            // shape ExecuteEngineCommand() (EngineCommandDispatch.cpp)
            // already uses for ITS bridge's own multiple kinds.
            if (uiRequest->kind == EditorUiCommandKind::SpawnGpuDrivenTestBatch) {
                const GpuDrivenTestBatchSpawnResult spawned = m_editorLayer->SpawnGpuDrivenTestBatch(
                    m_game, m_renderer, uiRequest->spawnGpuDrivenTestBatch.instanceCount);
                uiResult.spawnGpuDrivenTestBatch.success = spawned.success;
                uiResult.spawnGpuDrivenTestBatch.editorAvailable = spawned.editorAvailable;
                uiResult.spawnGpuDrivenTestBatch.errorMessage = spawned.errorMessage;
                uiResult.spawnGpuDrivenTestBatch.instanceCount = spawned.instanceCount;
            } else {
                const TabActivationResult activation = m_editorLayer->ActivateTab(uiRequest->activateTab.tabName);
                uiResult.activateTab.tabExists = activation.tabExists;
                uiResult.activateTab.success = activation.tabExists;
            }
            m_uiCommandBridge.FulfillCommand(uiResult);
        }

        // task_manager/frame-debugger-3 campaign, PHASE7
        // (PHASE7_NETWORK_HTTP_AUTOMATION_AND_MAIN_VIEWPORT_PINNING.md, Step
        // 3.3) - drains at most ONE pending Frame Debugger command per
        // frame, at the SAME point in the loop EditorUiCommandBridge's own
        // pump immediately above already runs (right after NewFrame() and
        // BEFORE BuildUI()) - see FrameDebuggerCommandBridge.h's own header
        // comment for why this is safe even for a command (CaptureNow)
        // whose real effect only fully "lands" later the SAME frame, inside
        // BuildUI()'s own FrameDebuggerPanel::Build() call: every
        // IEditorLayer::FrameDebugger*() method below is a cheap, direct
        // main-thread call (never itself blocking on anything), and
        // FrameDebuggerGetState() is read back AFTER dispatching whichever
        // command actually ran, so `fdResult.state` always reflects this
        // exact call's own real effect before FulfillCommand() unblocks the
        // waiting network thread.
        if (const std::optional<FrameDebuggerCommandRequest> fdRequest =
                m_frameDebuggerCommandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("Application::ExecuteFrameDebuggerCommand");
            FrameDebuggerCommandResult fdResult;
            fdResult.kind = fdRequest->kind;
            switch (fdRequest->kind) {
            case FrameDebuggerCommandKind::OpenWindow:
                m_editorLayer->FrameDebuggerOpenWindow();
                fdResult.success = true;
                break;
            case FrameDebuggerCommandKind::SetEnabled:
                m_editorLayer->FrameDebuggerSetEnabled(fdRequest->setEnabled.enabled);
                fdResult.success = true;
                break;
            case FrameDebuggerCommandKind::CaptureNow:
                fdResult.success = m_editorLayer->FrameDebuggerCaptureNow();
                break;
            case FrameDebuggerCommandKind::SelectEvent:
                m_editorLayer->FrameDebuggerSelectEvent(fdRequest->selectEvent.index);
                fdResult.success = true;
                break;
            case FrameDebuggerCommandKind::SetChannel:
                fdResult.success = m_editorLayer->FrameDebuggerSetChannel(fdRequest->setChannel.channel);
                break;
            case FrameDebuggerCommandKind::SetLevels:
                m_editorLayer->FrameDebuggerSetLevels(fdRequest->setLevels.black, fdRequest->setLevels.white);
                fdResult.success = true;
                break;
            case FrameDebuggerCommandKind::GetState:
                fdResult.success = true;
                break;
            }

            const FrameDebuggerStateSnapshotView stateView = m_editorLayer->FrameDebuggerGetState();
            fdResult.state.enabled = stateView.enabled;
            fdResult.state.windowOpen = stateView.windowOpen;
            fdResult.state.hasCapturedFrame = stateView.hasCapturedFrame;
            fdResult.state.totalEventCount = stateView.totalEventCount;
            fdResult.state.selectedEventIndex = stateView.selectedEventIndex;
            fdResult.state.channel = stateView.channel;
            fdResult.state.levelsBlack = stateView.levelsBlack;
            fdResult.state.levelsWhite = stateView.levelsWhite;

            m_frameDebuggerCommandBridge.FulfillCommand(fdResult);
        }

        // task_manager/stl-parser-2, PHASE1 - drains at most ONE pending
        // /import_asset request per frame. Unlike EditorUiCommandBridge's own
        // pump above, this one does not need ImGui's context to be valid at all -
        // ImportExternalAssetIntoProject() only touches ProjectPanel's own
        // AssetDatabase/filesystem state, never ImGui - but it is kept at this
        // same point in the frame, appended after every other bridge's own drain
        // block, for locality with them.
        if (const std::optional<AssetImportCommandRequest> importRequest =
                m_assetImportCommandBridge.TryPeekPendingCommandRequest()) {
            GTE_PROFILE_SCOPE("Application::ExecuteAssetImportCommand");
            AssetImportCommandResult importResult;
            importResult.kind = importRequest->kind;
            // Only one AssetImportCommandKind exists today (ImportExternalFile) -
            // a future addition would branch on importRequest->kind here, the
            // same shape ExecuteEngineCommand() already uses for its own bridge.
            const ProjectAssetImportResult layerResult = m_editorLayer->ImportExternalAssetIntoProject(
                importRequest->importExternalFile.sourceAbsolutePath,
                importRequest->importExternalFile.destinationRelativeFolder);
            ImportExternalFileOutcome& outcome = importResult.importExternalFile;
            outcome.projectAvailable = layerResult.projectAvailable;
            outcome.success = layerResult.success;
            outcome.message = layerResult.message;
            outcome.finalRelativePath = layerResult.finalRelativePath;
            outcome.finalAbsolutePath = layerResult.finalAbsolutePath;
            outcome.guid = layerResult.guid;
            outcome.convertedToMeshAsset = layerResult.convertedToMeshAsset;
            outcome.meshSourceFormat = layerResult.meshSourceFormat;
            outcome.convertedToKtx2 = layerResult.convertedToKtx2;
            outcome.convertedToMotionAsset = layerResult.convertedToMotionAsset;
            outcome.meshVertexCount = layerResult.meshVertexCount;
            outcome.meshTriangleCount = layerResult.meshTriangleCount;
            m_assetImportCommandBridge.FulfillCommand(importResult);
        }


        // Clears last frame's queued Submit() draw items before Game gets a
        // chance to queue this frame's - see Renderer::BeginFrame().
        m_renderer.BeginFrame();

        // editor-core-separation-1 campaign, PHASE13
        // (PHASE13_CORE_FRAME_ORCHESTRATION_EXTRACTION.md) - the REAL
        // per-frame Render Graph orchestration (offscreen regime: Game
        // View + Scene View, Atmosphere passes, GPU-skinning dispatch
        // requests, GPU-driven batch culling readback, and every
        // render-graph-frame-building IEditorLayer call site named in
        // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8) now lives
        // entirely inside Core::BuildFrame() - see Core.cpp and
        // PHASE13_COMPLETION_REPORT.md for the full, itemized accounting of
        // every one of Run()'s former ~30 IEditorLayer call sites' new home.
        m_core.BuildFrame();

        // network-impl-2 campaign, Phase 3
        // (PHASE3_GAME_VIEW_CAPTURE_AND_GET_GAME_VIEW_ENDPOINT.md) - the
        // FrameCaptureBridge fast-fail + success-path capture for the Game
        // View - a HOST-LEVEL AUTOMATION concern (design doc Section 6.1:
        // "Core stays a pure engine facade: no HTTP server, no automation-
        // bridge knowledge, ever") that stays here, RELOCATED to run right
        // AFTER BuildFrame() returns (safe: BuildFrame() already
        // fence-waited via EndOffscreenRenderGraphRecording() internally
        // before returning, so every pixel this capture reads is already
        // final) - see Core::GetGameViewTargetThisFrame()'s own doc comment
        // (Core.h) and PHASE13_COMPLETION_REPORT.md for the full reasoning.
        // Reads m_core's own resolved Game View target instead of calling
        // IEditorLayer::GameViewTarget() a second time (Locked Design
        // Decision #8's own Definition of Done: this call now lives ONLY
        // inside Core::BuildFrame()).
        RenderTexture* gameTargetThisFrame = m_core.GetGameViewTargetThisFrame();
        if (gameTargetThisFrame == nullptr && m_captureBridge.IsCaptureRequested(FrameCaptureKind::GameView)) {
            m_captureBridge.FailPendingRequest(FrameCaptureKind::GameView, FrameCaptureFailureReason::TargetNotAvailable);
        }
        if (gameTargetThisFrame != nullptr && m_captureBridge.IsCaptureRequested(FrameCaptureKind::GameView)) {
            // Atmosphere Scattering + Aerial Perspective campaign,
            // Phase 7 - captures "GameViewComposited" (the atmosphere-
            // composited output) instead of the original, pre-composite
            // render target, now that it's PERMANENTLY what the Game
            // View actually displays. Falls back to `gameTargetThisFrame`
            // itself only in the (should-be-unreachable) case the
            // composited texture somehow doesn't exist yet.
            RenderTexture* captureSource = m_atmosphereLutRenderer.CompositedOutput("GameViewComposited");
            if (captureSource == nullptr) {
                captureSource = gameTargetThisFrame;
            }
            Renderer::CapturedRawPixels raw = m_renderer.CaptureRenderTexturePixels(*captureSource);
            if (IsBgraFormat(raw.format)) {
                Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
            }
            std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw.pixels.data(), raw.width, raw.height);
            m_captureBridge.FulfillPendingRequest(FrameCaptureKind::GameView,
                CapturedPngImage{ std::move(png), raw.width, raw.height });
        }

        // Build every editor panel (Hierarchy/Inspector/Scene/Game/Memory/menu
        // bar) now that the Game/Scene view textures (if any) have this
        // frame's contents. Passes Game itself (Hierarchy/Inspector observe/
        // edit its ECS world via Game::GetRegistry(), and Hierarchy's
        // "Create 3D Object" menu spawns entities via
        // Game::CreatePrimitiveEntity() - see IEditorLayer::BuildUI()) and
        // Renderer itself (Memory - see Renderer::GetMemoryTotals()/
        // GetMemoryResources()). HOST-LEVEL (Locked Design Decision #8's
        // second bucket) - stays directly on m_editorLayer.
        // `m_core.GetGpuDrivenBatchDebugInfo()` (editor-core-separation-1
        // campaign, PHASE13) reads the SAME per-frame readout
        // Application::m_gpuDrivenBatchDebugInfoLastFrame used to be -
        // physically relocated into Core together with the GPU-driven-batch
        // orchestration that produces it (see Core.h's own doc comment).
        {
            GTE_PROFILE_SCOPE("IEditorLayer::BuildUI");
            m_editorLayer->BuildUI(m_game, m_renderer, m_renderGraph, m_atmosphereSettings, m_atmosphereLutRenderer,
                m_core.GetGpuDrivenBatchDebugInfo());
        }

        // File > Exit (or any other future programmatic "close" UI action)
        // ends the loop exactly like a Quit event/closing the OS window.
        if (m_editorLayer->WantsExit()) {
            running = false;
        }

        // network-impl-2 campaign, Phase 5
        // (PHASE5_GET_SWAPCHAIN_ENDPOINT_AND_FORMAT_NEGOTIATION_REUSE.md) -
        // must run BEFORE Core::Present() below so
        // SwapchainCaptureService::RecordCaptureIfRequested() (Phase 4) sees
        // m_captureRequested == true in time this frame. Unconditional, at
        // Run()'s own top-level body (never nested inside an
        // if (gameTarget != nullptr || sceneTarget != nullptr) block, unlike
        // the Game-view fast-fail check above) - runs every single frame, in
        // every build configuration, regardless of whether a Game/Scene view
        // exists this frame. Cheap, side-effect-free read per
        // IsCaptureRequested()'s own doc comment; RequestSwapchainCapture()
        // itself is idempotent/safe to call repeatedly while a request is
        // already pending.
        if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::Swapchain)) {
            m_renderer.RequestSwapchainCapture();
        }

        // editor-core-separation-1 campaign, PHASE13 - Call 2 of 2: the
        // PIPELINED swapchain-present regime, now entirely inside
        // Core::Present() - in an Editor build this draws the editor's own
        // ImGui chrome (which itself displays the Game/Scene views above)
        // via the recordImGui hook Application supplied at construction time
        // (Core::SetPresentImGuiRecorder()); in a release build (or the rare
        // Editor edge case where both "Game"/"Scene" are simultaneously
        // hidden) it ALSO renders Game directly into the swapchain first -
        // see RenderPasses.h's AddPresentPass() and Core.cpp.
        m_core.Present();

        // network-impl-2 campaign, Phase 5 - the FrameCaptureBridge
        // success-path capture for the swapchain - HOST-LEVEL AUTOMATION,
        // RELOCATED to run right AFTER Present() returns (safe - Present()'s
        // own Renderer::PresentViaRenderGraph() call already fence-waits
        // before returning) - see PHASE13_COMPLETION_REPORT.md.
        if (std::optional<CapturedSwapchainPixels> raw = m_renderer.TakeLastCompletedSwapchainCapture()) {
            if (IsBgraFormat(raw->format)) {
                Encoding::ConvertBgraToRgbaInPlace(raw->pixels.data(), raw->width, raw->height);
            }
            std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw->pixels.data(), raw->width, raw->height);
            m_captureBridge.FulfillPendingRequest(FrameCaptureKind::Swapchain,
                CapturedPngImage{ std::move(png), raw->width, raw->height });
        }

        // Update/present any panel the user has dragged outside the main OS
        // window (Dear ImGui multi-viewport/"platform windows" - a no-op in
        // a release build, see NullEditorLayer::RenderPlatformWindows()).
        // Deliberately AFTER the main swapchain Present() above: each such
        // window owns its own, completely independent Vulkan swapchain, so
        // there is no ordering requirement against the main window's own
        // present - see IEditorLayer::RenderPlatformWindows(). HOST-LEVEL
        // (Locked Design Decision #8's second bucket).
        m_editorLayer->RenderPlatformWindows();

        // network-impl-4 campaign, Phase 4
        // (task_manager/network-impl-4/PHASE4_FRAMECAPTUREBRIDGE_NAMED_TEXTURE_SUPPORT.md) -
        // GET /get_texture's own capture path. Placed here (unconditionally, once
        // per Run() iteration, AFTER every render-graph Execute() call this frame)
        // so RenderGraph::DebugTextureSnapshotFor() sees the freshest possible
        // registry state, and so WaitForGpuIdle() below waits out every submission
        // this frame - both regimes - before the readback below runs. Cheap,
        // side-effect-free when nothing is pending (IsCaptureRequested() is a
        // plain bool read).
        if (m_captureBridge.IsCaptureRequested(FrameCaptureKind::NamedTexture)) {
            const std::string requestedName = m_captureBridge.RequestedTextureName();
            const DebugTextureChannel requestedChannel = m_captureBridge.RequestedTextureChannel();

            if (const std::optional<rg::DebugTextureSnapshot> snapshot = m_renderGraph.DebugTextureSnapshotFor(requestedName)) {
                const bool wantsDepth = (requestedChannel == DebugTextureChannel::Depth);
                if (wantsDepth && !snapshot->hasDepth) {
                    // Positively known, permanent-for-this-registration failure -
                    // fail fast (409) rather than waiting out the full timeout,
                    // exactly mirroring the Game-view "no panel visible"
                    // fast-fail's own reasoning.
                    m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                } else {
                    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision 4 -
                    // computed BEFORE WaitForGpuIdle()/the readback below, from the
                    // snapshot as it was at the moment this request was actually
                    // serviced (never re-queried afterward - a capture that takes a
                    // few extra milliseconds to read back must not report itself
                    // as "0 frames old" merely because the CURRENT counter moved on
                    // in the meantime; it genuinely reflects THIS snapshot's own
                    // last-write frame, compared against "now").
                    const std::uint64_t framesSinceUpdate =
                        m_renderGraph.CurrentDebugTextureFrameCounter() - snapshot->lastUpdatedFrameCounter;

                    m_renderer.WaitForGpuIdle();

                    const VkImage image = wantsDepth ? snapshot->target.depthImage : snapshot->target.image;
                    const VkImageAspectFlags aspect = wantsDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
                    const VkFormat format = wantsDepth ? snapshot->target.depthFormat : snapshot->target.format;
                    const rg::ResourceState state = wantsDepth ? snapshot->depthState : snapshot->colorState;

                    // Atmosphere Scattering + Aerial Perspective campaign,
                    // Phase 4 (ATMOSPHERE_PHASE4_MULTISCATTERING_LUT_v1.md) -
                    // the FIRST capturable color texture that is genuinely
                    // NOT 4 bytes/pixel (VK_FORMAT_R16G16B16A16_SFLOAT is 8 -
                    // see AtmosphereLutRenderer's own Multi-Scattering LUT
                    // output). Every other capturable texture (color OR
                    // depth) is still exactly 4 bytes/pixel, unchanged - see
                    // Renderer::CaptureImagePixels()'s own doc comment.
                    const bool isHdrColor = !wantsDepth && format == VK_FORMAT_R16G16B16A16_SFLOAT;
                    const int bytesPerPixel = isHdrColor ? 8 : 4;

                    Renderer::CapturedRawPixels raw =
                        m_renderer.CaptureImagePixels(image, aspect, format, snapshot->target.extent, state, bytesPerPixel);

                    // A separate, always-4-bytes/pixel buffer PNG encoding
                    // actually reads from - identical to raw.pixels for
                    // every "traditional" 4-bytes/pixel capture (no copy
                    // needed - encodePixels just points straight at it), but
                    // a genuinely different, freshly-allocated buffer for the
                    // HDR case above (raw.pixels itself stays 8-bytes/pixel
                    // native data - ConvertHdrRgba16fToRgba8() below is an
                    // OUT conversion, not in-place, mirroring
                    // ConvertDepthToGrayscaleRgba8()'s own out-buffer shape).
                    std::vector<std::uint8_t> hdrConvertedPixels;
                    const std::uint8_t* encodePixels = raw.pixels.data();

                    bool ok = true;
                    if (wantsDepth) {
                        // Safe to write in-place into the SAME buffer it reads from
                        // (see PHASE4's own detailed correctness note): every pixel's
                        // 4 input bytes are fully read into a local temporary BEFORE
                        // any of that pixel's own 4 output bytes are written, and
                        // input/output share the exact same per-pixel byte offset
                        // (no shift) - never a different pixel's range.
                        ok = Encoding::ConvertDepthToGrayscaleRgba8(raw.pixels.data(), raw.format, raw.width, raw.height, raw.pixels.data());
                    } else if (isHdrColor) {
                        hdrConvertedPixels.resize(static_cast<std::size_t>(raw.width) * static_cast<std::size_t>(raw.height) * 4);
                        ok = Encoding::ConvertHdrRgba16fToRgba8(
                            raw.pixels.data(), raw.format, raw.width, raw.height, hdrConvertedPixels.data());
                        encodePixels = hdrConvertedPixels.data();
                    } else if (IsBgraFormat(raw.format)) {
                        Encoding::ConvertBgraToRgbaInPlace(raw.pixels.data(), raw.width, raw.height);
                    }

                    if (!ok) {
                        // Depth format this device negotiated isn't one
                        // ConvertDepthToGrayscaleRgba8() recognizes, or the color
                        // format isn't one ConvertHdrRgba16fToRgba8() recognizes -
                        // see PHASE3/this phase's own accepted, documented, narrow
                        // risk.
                        m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                    } else {
                        std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(encodePixels, raw.width, raw.height);
                        m_captureBridge.FulfillPendingRequest(FrameCaptureKind::NamedTexture,
                            CapturedPngImage{ std::move(png), raw.width, raw.height, framesSinceUpdate });
                    }
                }
            } else if (const std::optional<rg::DebugVolumeTextureSnapshot> volumeSnapshot =
                           m_renderGraph.DebugVolumeTextureSnapshotFor(requestedName)) {
                // network-impl-6 campaign, Phase 4
                // (task_manager/network-impl-6/PHASE4_NAMED_TEXTURE_ENDPOINT_VOLUME_BRANCH_WIRING.md)
                // - a name that isn't a registered 2D texture but IS a
                // registered volume texture now renders a fresh raymarch
                // thumbnail on demand instead of copying already-rendered
                // pixels (a volume texture has no "existing rendered 2D
                // contents" to copy - see PHASE0_MASTER_STRATEGY.md's Step 2).
                if (requestedChannel == DebugTextureChannel::Depth) {
                    // A volume texture has no depth-companion concept at all
                    // (see VolumeTarget.h) - the same positively-known,
                    // permanent-failure fast-fail (409) the 2D branch above
                    // uses for wantsDepth && !snapshot->hasDepth (see
                    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision 6).
                    m_captureBridge.FailPendingRequest(FrameCaptureKind::NamedTexture, FrameCaptureFailureReason::TargetNotAvailable);
                } else {
                    // Computed BEFORE WaitForGpuIdle()/the render below, from
                    // the snapshot as it was at the moment this request was
                    // actually serviced - exactly mirroring the 2D branch's
                    // own identical reasoning above (Phase 2 deliberately did
                    // not add a second, volume-specific frame counter).
                    const std::uint64_t framesSinceUpdate =
                        m_renderGraph.CurrentDebugTextureFrameCounter() - volumeSnapshot->lastUpdatedFrameCounter;

                    m_renderer.WaitForGpuIdle();

                    // atmosphere-scattering-2 campaign, Phase 4
                    // (task_manager/atmosphere-scattering-2/PHASE4_ATMOSPHERE_AWARE_VOLUME_DEBUG_PREVIEW.md)
                    // - auto-detect the Aerial Perspective volume by name so its
                    // preview uses the atmosphere-aware transmittance/in-scattering
                    // interpretation instead of the generic density/color one -
                    // zero new HTTP endpoint/query parameter (see that phase's own
                    // Locked Design Decision 6). Every other volume texture name
                    // still resolves to GenericDensityInAlpha, byte-for-byte the
                    // same behavior network-impl-6 already shipped.
                    //
                    // frame-debugger-5 campaign, PHASE4
                    // (task_manager/frame-debugger-5/PHASE4_VOLUME_TEXTURE_RAYMARCH_PREVIEW_REUSE.md)
                    // - this rule is now a shared, named, pure function
                    // (VolumeTexturePreviewRenderer.h's SelectVolumeTexturePreviewInterpretation())
                    // rather than inlined here, since FrameDebuggerCurrentCapture::CaptureFrame()
                    // now needs the exact same rule for a second real call site - byte-for-byte
                    // unchanged behavior for this call site.
                    const VolumeTexturePreviewInterpretation interpretation =
                        SelectVolumeTexturePreviewInterpretation(requestedName);

                    const VolumeTexturePreviewRenderer::CapturedRawPixels raw = m_volumeTexturePreviewRenderer.RenderPreview(
                        m_renderer, volumeSnapshot->target, volumeSnapshot->state, interpretation);

                    // raw.pixels is already tightly-packed RGBA8 (see Phase 3's
                    // own RenderPreview() doc comment) - no BGRA swizzle, no
                    // HDR conversion, no depth-to-grayscale conversion needed
                    // at all, unlike the 2D branch's several format-dependent
                    // branches above.
                    std::vector<std::uint8_t> png = Encoding::EncodeRgba8ToPng(raw.pixels.data(), raw.width, raw.height);
                    m_captureBridge.FulfillPendingRequest(FrameCaptureKind::NamedTexture,
                        CapturedPngImage{ std::move(png), raw.width, raw.height, framesSinceUpdate });
                }
            }
            // else: this name has never been registered as EITHER kind yet this
            // session - leave the request pending; either it starts rendering
            // within the bridge's existing fixed timeout (a later Run()
            // iteration's own check above then succeeds), or the caller
            // eventually gets HTTP 504 - exactly the same accepted "main thread
            // hasn't produced this yet" bucket network-impl-2's own
            // PHASE0_MASTER_STRATEGY.md already documents.
        }

        // network-impl-4 campaign, Phase 5
        // (task_manager/network-impl-4/PHASE5_HTTP_ENDPOINTS_GET_TEXTURE_AND_LIST_TEXTURES.md) -
        // GET /list_textures' only data source. Resolved to plain
        // PublishedTextureListEntry values HERE, never inside FrameCaptureBridge
        // itself - see that struct's own doc comment (FrameCaptureBridge.h) for
        // why. Cheap - see PHASE0_MASTER_STRATEGY.md's own Locked Design Decision
        // 8: O(declared textures), the same order of magnitude as
        // Renderer::GetMemoryResources()'s own existing "safe to call every frame"
        // per-frame snapshot copy.
        {
            const std::vector<rg::DebugTextureSnapshot> snapshots = m_renderGraph.ListDebugTextures();
            const std::uint64_t currentFrameCounter = m_renderGraph.CurrentDebugTextureFrameCounter();

            std::vector<PublishedTextureListEntry> published;
            published.reserve(snapshots.size());
            for (const rg::DebugTextureSnapshot& snap : snapshots) {
                PublishedTextureListEntry entry;
                entry.name = snap.name;
                entry.regime = ToDebugTextureRegimeString(snap.regime);
                entry.format = DebugTextureColorFormatName(snap.target.format);
                entry.width = snap.target.extent.width;
                entry.height = snap.target.extent.height;
                entry.hasDepth = snap.hasDepth;
                // Same subtraction PHASE0_MASTER_STRATEGY.md's own Locked Design
                // Decision 4 defines for /get_texture's single-entry case (Phase 4),
                // just computed here for EVERY known texture at once, once per
                // frame, rather than once per request.
                entry.framesSinceUpdate = currentFrameCounter - snap.lastUpdatedFrameCounter;
                entry.kind = "texture2d"; // network-impl-6 campaign, Phase 5 - explicit at every call site, see PublishedTextureListEntry's own doc comment.
                published.push_back(std::move(entry));
            }

            // network-impl-6 campaign, Phase 5
            // (task_manager/network-impl-6/PHASE5_LIST_TEXTURES_VOLUME_SURFACING.md) -
            // every registered VOLUME texture also gets its own
            // PublishedTextureListEntry, appended right after every 2D one, so
            // GET /list_textures surfaces both kinds side by side in one flat
            // array - this is what lets an LLM/AI caller discover a
            // texture_name worth calling GET /get_texture with, without
            // already knowing the Atmosphere feature's internal naming
            // convention.
            for (const rg::DebugVolumeTextureSnapshot& vol : m_renderGraph.ListDebugVolumeTextures()) {
                PublishedTextureListEntry entry;
                entry.name = vol.name;
                entry.regime = ToDebugTextureRegimeString(vol.regime);
                entry.format = DebugTextureColorFormatName(vol.target.format);
                entry.width = vol.target.extent.width;
                entry.height = vol.target.extent.height;
                entry.hasDepth = false; // A volume texture has no depth-companion concept at all - see VolumeTarget.h.
                entry.framesSinceUpdate = currentFrameCounter - vol.lastUpdatedFrameCounter;
                entry.kind = "texture3d";
                entry.depth = vol.target.extent.depth;
                published.push_back(std::move(entry));
            }

            m_captureBridge.PublishTextureList(std::move(published));
        }

        // Phase 5 (GPU memory usage over time) - see PHASE5_GPU_MEMORY_
        // HISTORY_STRATEGY_v2.md: one real GPU memory snapshot per
        // profiler frame, taken as late as possible in the frame (still
        // inside this BeginFrame()/EndFrame() bracket) so it reflects
        // every resource created/destroyed anywhere this frame, including
        // by IEditorLayer::BuildUI()'s own Inspector/Project-panel asset
        // loading above. Unconditional - not #if GTE_ENABLE_PROFILER-gated,
        // matching this same function's own BeginFrame()/EndFrame()/
        // SetGpuPassDrawStats() calls, none of which are gated either (only
        // GTE_PROFILE_SCOPE(...)'s own macro body is compile-time-gated -
        // see AGENTS.md, "Profiling"). Renderer::GetMemoryTotals() is O(1)
        // and always meaningful (no "didn't run this frame" concept, unlike
        // a GpuPass's draw-call count), so this is always
        // GpuSampleStatus::Present.
        Profiling::FrameProfiler::Instance().SetMemorySnapshot(BuildMemorySnapshot(m_renderer.GetMemoryTotals()));

        Profiling::FrameProfiler::Instance().EndFrame();
    }

    return 0;
}

} // namespace gte
