#include "EditorHost.h"

#include "Logger.h" // LoggerLogSink::Instance() - the ONE real ILogSink this engine ships.
#include "../Memory/SdlMemoryTracker.h"

#include <SDL3/SDL.h>

#include <cassert>
#include <stdexcept>

namespace gte {

EditorHost::SdlContext::SdlContext()
{
    // Must be installed before SDL_Init() - indeed, before literally any
    // SDL call - see SdlMemoryTracker's own doc comment for why. Mirrors
    // Application::SdlContext::SdlContext()'s exact precedent
    // (Application.cpp) verbatim. See AGENTS.md ("CPU Dependency Memory
    // Tracking").
    SdlMemoryTracker::Install();

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        throw std::runtime_error(std::string("SDL_Init failed: ") + SDL_GetError());
    }
}

EditorHost::SdlContext::~SdlContext()
{
    SDL_Quit();
}

EditorHost::EditorHost(const std::string& title, int width, int height)
    : m_sdlContext()
    , m_window(title, width, height)
    // m_hostServices has no constructor dependencies of its own (a
    // stateless adapter) - m_core is constructed injecting m_window (as
    // ISurfaceProvider&) and m_hostServices (as IHostServices&), both
    // already fully constructed by this point (matches this class's own
    // member declaration order, EditorHost.h).
    , m_hostServices()
    , m_core(m_window, m_hostServices)
    // CreateEditorLayer() (src/Editor/EditorLayer.h) - PHASE9's own rename
    // (Locked Design Decision #9) means this resolves unambiguously to
    // ImGuiEditorLayer.cpp's real, ImGui-backed implementation, with
    // exactly ONE definition of this name anywhere in the link. Takes
    // Window& directly (real SDL/ImGui backend init needs the concrete SDL
    // handle - a deliberately different, correctly out-of-scope concern
    // from Renderer/VulkanSurface's own ISurfaceProvider-based
    // surface-creation need, see PHASE10_COMPLETION_REPORT.md) and
    // m_core.GetRenderer() (the exact same Renderer instance Core itself
    // owns and renders through every frame).
    , m_editorLayer(CreateEditorLayer(m_window, m_core.GetRenderer()))
{
    // editor-core-separation-1 campaign, PHASE3
    // (PHASE3_LOGGING_GLOBAL_LOGSINK_EXTRACTION.md) - installs the ONE real
    // ILogSink this engine ships into the global sink mechanism
    // (Core/LogSink.h), mirroring Application::Application()'s own
    // identical precedent (Application.cpp) - install-once, idempotent.
    // PHASE16 of this campaign is what moves the automation bridges/
    // NetworkServer::Start() call that used to follow this in Application
    // into this same constructor body - this phase's own Run() (a
    // deliberate, temporary stub, see EditorHost.cpp below) never starts a
    // NetworkServer at all yet.
    gte::InstallLogSink(&gte::LoggerLogSink::Instance());

    // PHASE0_MASTER_STRATEGY.md's Locked Design Decision #8 - the ONE new
    // wiring call this whole campaign exists to add: Core::BuildFrame()'s
    // own render-graph-frame-building hooks (Phase 13) need a genuinely
    // non-null m_editorLayer from this point onward, forever (a Player host
    // never reaches this class at all - it would call
    // gte::CreateNullEditorLayer() and pass THAT into Core instead, but
    // EditorHost itself only ever exists to drive the real, ImGui-backed
    // authoring app). The debug-only assert below exists specifically to
    // catch a future "forgot/mistyped this exact call, or passed the wrong
    // pointer" regression immediately at startup, in the constructor,
    // rather than confusingly far downstream inside Phase 16's own
    // blur/GBuffer validation smoke check (exactly the risk PHASE15's own
    // strategy file calls out by name) - CreateEditorLayer() itself always
    // returns a genuinely non-null pointer in practice (std::make_unique
    // either succeeds or throws, propagating out of this constructor
    // entirely - it never silently returns null), so this assert is a
    // regression guard against THIS call site, not a defense against
    // CreateEditorLayer() itself.
    assert(m_editorLayer != nullptr && "EditorHost: m_editorLayer must be non-null before SetEditorLayerHook()");
    m_core.SetEditorLayerHook(m_editorLayer.get());

    GTE_LOG_INFO("EditorHost",
        "EditorHost constructed: SdlContext -> Window -> Core -> CreateEditorLayer() -> "
        "Core::SetEditorLayerHook() all completed, with a genuinely non-null IEditorLayer*.");
}

EditorHost::~EditorHost() = default;

int EditorHost::Run()
{
    // editor-core-separation-1 campaign, PHASE15 - a deliberate, TEMPORARY
    // stub. This phase's own job is proving EditorHost's CONSTRUCTION order
    // (SdlContext -> Window -> Core -> CreateEditorLayer() ->
    // Core::SetEditorLayerHook()) is correct, NOT building the real main
    // loop (PHASE16_EDITORHOST_MAIN_LOOP_AND_AUTOMATION_BRIDGES.md's job).
    // This minimal SDL event pump exists only so the window this
    // constructor already opened stays alive/responsive to a real OS close
    // request for a live smoke check (see PHASE15_COMPLETION_REPORT.md) -
    // it deliberately does NOT call m_core.Update()/BuildFrame()/Present()
    // at all yet (nothing is simulated or rendered through this path this
    // phase), and it does NOT service any automation bridge/NetworkServer
    // (none exist on this class yet either). Application (still fully
    // intact and unused, per this phase's own explicit "Out of Scope")
    // remains the only path that actually renders a frame today.
    bool running = true;
    while (running) {
        SDL_Event sdlEvent;
        while (SDL_PollEvent(&sdlEvent)) {
            if (sdlEvent.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }
        SDL_Delay(16);
    }
    return 0;
}

} // namespace gte
