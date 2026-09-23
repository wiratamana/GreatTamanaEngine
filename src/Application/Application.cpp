#include "Application.h"

#include "EventTranslator.h"
#include "MemorySnapshotBuilder.h"

#include "../Editor/Logger.h" // Application still needs it directly for now (see this file's own header comment) - Phase 17 deletes this file outright.
#include "../Memory/SdlMemoryTracker.h"
#include "../Profiling/FrameProfiler.h"
#include "../Profiling/ScopeTimer.h"

#include <SDL3/SDL.h>

#include <stdexcept>

namespace gte {

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
{
    // editor-core-separation-1 campaign, PHASE3
    // (PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md) - installs the ONE real
    // ILogSink this engine ships (Editor/Logger.h's LoggerLogSink) into the
    // global sink mechanism (Core/LogSink.h) - install-once, idempotent.
    // Harmless to leave here even though this class is otherwise unused -
    // EditorHost's own constructor (the real, live composition root since
    // PHASE15) installs the exact same sink itself, and InstallLogSink() is
    // explicitly documented as idempotent.
    gte::InstallLogSink(&gte::LoggerLogSink::Instance());

    // editor-core-separation-1 campaign, PHASE12
    // (PHASE12_CORE_CLASS_SKELETON_AND_CONSTRUCTION.md, PHASE0's Locked
    // Design Decision #8) - wires m_core's own nullable IEditorLayer* hook
    // to the real, already-constructed m_editorLayer instance.
    m_core.SetEditorLayerHook(m_editorLayer.get());

    // editor-core-separation-1 campaign, PHASE13
    // (PHASE13_CORE_FRAME_ORCHESTRATION_EXTRACTION.md) - hands Core the ONE
    // callback that actually calls IEditorLayer::Render(cmd) - see
    // Core::SetPresentImGuiRecorder()'s own doc comment (Core.h).
    m_core.SetPresentImGuiRecorder([this](VkCommandBuffer cmd) { m_editorLayer->Render(cmd); });

    // logger-1 campaign, Phase 2 - proves GTE_LOG_* is reachable.
    GTE_LOG_INFO("Application", "Application constructed (unused - see this class's own header comment; EditorHost is the real composition root since PHASE15).");
}

Application::~Application() = default;

int Application::Run()
{
    // editor-core-separation-1 campaign, PHASE16
    // (PHASE16_EDITORHOST_MAIN_LOOP_AND_AUTOMATION_BRIDGES.md) - this method
    // is GENUINELY UNUSED (nothing anywhere in this codebase constructs an
    // Application instance anymore - see main.cpp/EditorHost.h's own header
    // comments). Every automation-bridge-servicing block this method used
    // to contain (EngineCommandBridge/FrameCaptureBridge/
    // EditorUiCommandBridge/FrameDebuggerCommandBridge/
    // AssetImportCommandBridge/Network::NetworkServer/ISceneIOCapability/
    // VolumeTexturePreviewRenderer - all HOST-LEVEL AUTOMATION concerns, per
    // design doc Section 6.1) moved to EditorHost::Run() together with
    // their owning members (Application.h) - this method keeps only the
    // core render-loop shape (input polling/translation, Core::Update()/
    // BuildFrame()/Present(), BuildUI(), WantsExit(), platform-window
    // present) so this now-dead class stays honestly self-consistent (no
    // dangling reference to a member that no longer exists) until Phase 17
    // deletes it outright.
    bool running = true;
    const Uint32 mainWindowId = m_window.Id();
    Uint64 lastTicksNs = SDL_GetTicksNS();

    InputState inputState;

    while (running) {
        Profiling::FrameProfiler::Instance().BeginFrame();

        m_renderer.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());
        m_renderGraph.SetGpuTimingCaptureEnabled(Profiling::FrameProfiler::Instance().IsCaptureEnabled());

        inputState.BeginFrame();

        {
            GTE_PROFILE_SCOPE("Application::PollEvents");
            SDL_Event sdlEvent;
            while (SDL_PollEvent(&sdlEvent)) {
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
                    m_core.NotifyWindowResized(resized.width, resized.height);
                    m_editorLayer->OnWindowResized(resized.width, resized.height);
                }

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

        const bool playbackPaused = m_editorLayer->IsPlaybackPaused();
        const bool stepRequestedRaw = m_editorLayer->TryConsumeStepRequest();
        const bool steppedThisFrame = playbackPaused && stepRequestedRaw;

        if (steppedThisFrame) {
            m_editorLayer->NotifyFrameDebuggerStepConsumed();
        }

        InputFrame inputFrame;
        inputFrame.inputState = &inputState;
        inputFrame.playbackPaused = playbackPaused;
        inputFrame.stepRequested = stepRequestedRaw;
        m_core.Update(inputFrame, static_cast<float>(deltaSeconds));

        gte::Logger::SetCurrentFrame(m_engineContext.time.FrameCount());

        m_editorLayer->NewFrame();

        m_renderer.BeginFrame();

        m_core.BuildFrame();

        {
            GTE_PROFILE_SCOPE("IEditorLayer::BuildUI");
            m_editorLayer->BuildUI(m_game, m_renderer, m_renderGraph, m_atmosphereSettings, m_atmosphereLutRenderer,
                m_core.GetGpuDrivenBatchDebugInfo());
        }

        if (m_editorLayer->WantsExit()) {
            running = false;
        }

        m_core.Present();

        m_editorLayer->RenderPlatformWindows();

        Profiling::FrameProfiler::Instance().SetMemorySnapshot(BuildMemorySnapshot(m_renderer.GetMemoryTotals()));

        Profiling::FrameProfiler::Instance().EndFrame();
    }

    return 0;
}

} // namespace gte
