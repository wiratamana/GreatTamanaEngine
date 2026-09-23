#pragma once

#include <memory>
#include <string>

#include "../Core/EngineContext.h"
// editor-core-separation-1 campaign, PHASE12 - gte::Core, the new gte_core
// public facade Application now constructs and delegates its own
// Renderer/RenderGraph/Game/EngineContext-Time ownership to (see this
// class's own m_core/m_renderer/m_game/m_engineContext member comments
// below). Core.h itself already includes EngineContext.h/Renderer.h/
// RenderGraph.h/Game.h transitively - the explicit includes below/above are
// kept anyway for this file's own existing "state exactly what this file
// needs" discipline, unaffected by this addition.
#include "../Core/Core.h"
#include "../Editor/EditorLayer.h"
#include "../Game/Game.h"
#include "../Renderer/Atmosphere/AtmosphereLutRenderer.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Window/Window.h"

namespace gte {

// The only layer that knows about SDL directly. Owns SDL's lifetime plus the
// main loop, and wires the Window/Renderer/Editor/Game abstractions
// together.
//
// Application is also the composition root for the optional Editor layer:
// it is the only place that asks "where should Game render this frame?"
// (IEditorLayer::GameViewTarget() - an off-screen RenderTexture in an
// Editor build, or nullptr meaning straight to the swapchain in a release
// build) and wires the answer into Renderer::RenderOffscreen()/Present().
// Game itself never knows the Editor exists either way.
//
// editor-core-separation-1 campaign, PHASE13
// (PHASE13_CORE_FRAME_ORCHESTRATION_EXTRACTION.md) - Run()'s own per-frame
// RENDER-GRAPH-BUILDING orchestration (offscreen regime, present regime,
// GPU-skinning dispatch requests, per-view data, Atmosphere passes,
// GPU-driven batch culling readback, and every "render-graph-frame-building"
// IEditorLayer call site named in PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision #8) physically moved into Core::Update()/BuildFrame()/Present() -
// this class's own Run() now interleaves three much-shorter calls into
// m_core around every HOST-LEVEL IEditorLayer call site (UI-building/input-
// routing/automation - Decision #8's second bucket), which all stay directly
// on m_editorLayer, exactly as before. See PHASE13_COMPLETION_REPORT.md for
// the full, itemized accounting of every one of Run()'s ~30 IEditorLayer
// call sites' new home.
//
// editor-core-separation-1 campaign, PHASE16
// (PHASE16_EDITORHOST_MAIN_LOOP_AND_AUTOMATION_BRIDGES.md) - EVERY
// automation bridge (EngineCommandBridge/FrameCaptureBridge/
// EditorUiCommandBridge/FrameDebuggerCommandBridge/AssetImportCommandBridge),
// the embedded Network::NetworkServer, the ISceneIOCapability wiring, and
// the VolumeTexturePreviewRenderer (all HOST-LEVEL AUTOMATION concerns, per
// design doc Section 6.1) moved OUT of this class and into
// src/Editor/EditorHost.h/.cpp - EditorHost's own real, permanent home for
// all of them. This class is now genuinely, completely UNUSED (confirmed:
// nothing anywhere in this codebase constructs an Application instance
// anymore - main.cpp constructs EditorHost, since PHASE15) and is kept
// around, still compiling, ONLY because Phase 17
// (PHASE17_APPLICATION_RETIREMENT_AND_EXECUTABLE_RENAME.md) is the phase
// that actually deletes it - see that phase's own file for why the deletion
// is deliberately its own separate, reviewable step rather than folded into
// this one.
class Application {
public:
    Application(const std::string& title, int width, int height);
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;
    Application(Application&&) = delete;
    Application& operator=(Application&&) = delete;

    // Runs the main loop until the window is closed. Returns a process exit code.
    int Run();

private:
    // RAII guard for SDL_Init()/SDL_Quit(). Declared FIRST so it is
    // constructed before, and destroyed after, every other SDL-owning member
    // below it (Window, Renderer, ...) - this keeps init/shutdown ordering
    // correct automatically instead of relying on manual cleanup code.
    struct SdlContext {
        SdlContext();
        ~SdlContext();

        SdlContext(const SdlContext&) = delete;
        SdlContext& operator=(const SdlContext&) = delete;
    };

    SdlContext m_sdlContext;
    Window m_window;

    // editor-core-separation-1 campaign, PHASE12
    // (PHASE12_CORE_CLASS_SKELETON_AND_CONSTRUCTION.md) - a trivial,
    // TEMPORARY IHostServices implementation, needed only because Core's
    // constructor requires a real IHostServices& (Core's own frozen public
    // contract, design doc Section 5.2). Routes into the SAME global
    // log-sink mechanism GTE_LOG_* itself already uses (Core/LogSink.h) -
    // this is NOT a second, competing logging path, just a thin adapter
    // satisfying Core's constructor signature. PHASE16 moved the real,
    // permanent equivalent of this onto EditorHost
    // (Editor/EditorHostServices.h) - this one is kept here, unused by
    // anything else, purely so this now-dead class still compiles until
    // Phase 17 deletes it outright.
    struct ApplicationHostServices : IHostServices {
        void Log(LogLevel level, std::string_view message) override
        {
            LogToActiveSink(level, "Core", message);
        }
    };

    ApplicationHostServices m_hostServices;

    // editor-core-separation-1 campaign, PHASE12
    // (PHASE12_CORE_CLASS_SKELETON_AND_CONSTRUCTION.md) - gte_core's own
    // public facade (src/Core/Core.h). Owns the real Renderer/RenderGraph/
    // Game/EngineContext-Time instances (see m_renderer's own doc comment
    // below for why Application still keeps same-named REFERENCE members
    // bound to these). PHASE13 additionally moved BOTH rg::RenderPipeline
    // instances, the Atmosphere/GPU-driven-batch orchestration state, and
    // every per-frame render-graph-building IEditorLayer call site into
    // Core - Application keeps only the small set of same-named reference
    // members below that its own still-host-level Run() body (BuildUI())
    // needs.
    // `SetEditorLayerHook(m_editorLayer.get())`/`SetPresentImGuiRecorder(...)`
    // are called once, from this class's own constructor BODY (never the
    // initializer list - m_editorLayer must already be fully constructed
    // first) - see Application.cpp. Declared right after m_window/
    // m_hostServices (its own two constructor dependencies) so both are
    // already fully constructed by the time this runs.
    Core m_core;
    // editor-core-separation-1 campaign, PHASE12 (extended PHASE13) -
    // Renderer/RenderGraph/Game/EngineContext-Time/AtmosphereSettings/
    // AtmosphereLutRenderer are no longer owned VALUE members here: the real
    // instances now physically live inside m_core (below), per the design
    // doc's own Section 2.1 ownership graph. These are kept as same-named
    // REFERENCE members bound to m_core's own accessors at construction
    // time - a documented, lower-risk pattern (established PHASE12, extended
    // PHASE13 to the two Atmosphere members once their own only real
    // per-frame consumer moved into Core too): every one of Run()'s own
    // still-host-level call sites (BuildUI()) that read these members keeps
    // compiling and behaving BYTE-FOR-BYTE UNCHANGED. See
    // PHASE12_COMPLETION_REPORT.md/PHASE13_COMPLETION_REPORT.md for the full
    // reasoning.
    Renderer& m_renderer;
    // Phase 7 (RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md) -
    // the ONE shared RenderGraph instance Game view/Scene view/Present are
    // all recorded through (two Execute() calls per frame - now both fully
    // inside Core::BuildFrame()/Present(), PHASE13). Declared right after
    // m_renderer so it's already fully constructed by the time m_editorLayer/
    // m_game below might indirectly need it.
    rg::RenderGraph& m_renderGraph;

    // Declared after Renderer (and before Game) so it is destroyed before
    // Renderer's Vulkan device/instance go away, but its lifetime doesn't
    // need to relate to Game's at all.
    std::unique_ptr<IEditorLayer> m_editorLayer;
    // editor-core-separation-1 campaign, PHASE12 - REFERENCE, not a real
    // owned instance anymore: the real Game instance now physically lives
    // inside m_core (Core::GetGame()) - see m_renderer's own doc comment
    // above for the full "why a reference, not m_core.GetGame() at every
    // call site" reasoning (identical here).
    Game& m_game;

    // frame-debugger-1 campaign (task_manager/frame-debugger-1/
    // PHASE0_MASTER_STRATEGY.md) - the ONE EngineContext instance for the
    // whole process, advanced exactly once per Run() loop iteration
    // (via Core::Update(), PHASE13) and passed by const reference into
    // Game::Update() (now inside Core::Update() too). See EngineContext.h's
    // own doc comment for why this stays deliberately minimal (just `time`
    // for now).
    // editor-core-separation-1 campaign, PHASE12 - REFERENCE, not a real
    // owned instance anymore: the real EngineContext/Time instance now
    // physically lives inside m_core (Core::GetEngineContext()).
    EngineContext& m_engineContext;

    // editor-core-separation-1 campaign, PHASE13 - REFERENCE members bound to
    // m_core's own real, owned AtmosphereSettings/AtmosphereLutRenderer
    // instances (Core::GetAtmosphereSettings()/GetAtmosphereLutRenderer()) -
    // both moved into Core together with their only real per-frame
    // ORCHESTRATION consumer, but still needed here for the one remaining
    // host-level call site left on this now-dead class: IEditorLayer::
    // BuildUI() (the "Atmosphere" panel edits/reads them live) - see Run()'s
    // own body and PHASE13_COMPLETION_REPORT.md. The OTHER former reader
    // (the FrameCaptureBridge Game-View capture success path) moved to
    // EditorHost together with the bridge itself (PHASE16).
    AtmosphereSettings& m_atmosphereSettings;
    AtmosphereLutRenderer& m_atmosphereLutRenderer;
};

} // namespace gte
