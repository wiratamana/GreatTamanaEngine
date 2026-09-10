#pragma once

#include <memory>
#include <string>

#include "../Editor/EditorLayer.h"
#include "../Game/Game.h"
#include "../Network/NetworkServer.h"
#include "../Renderer/Atmosphere/AtmosphereLutRenderer.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Window/Window.h"
#include "EngineCommandBridge.h"
#include "FrameCaptureBridge.h"

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
    Renderer m_renderer;
    // Phase 7 (RENDERGRAPH_PHASE7_APPLICATION_MIGRATION_STRATEGY_v2.md) -
    // the ONE shared RenderGraph instance Game view/Scene view/Present are
    // all recorded through (two Execute() calls per frame - see Run() and
    // RenderPasses.h). Declared right after m_renderer (constructed with a
    // reference to it) so it's already fully constructed by the time
    // m_editorLayer/m_game below might indirectly need it.
    rg::RenderGraph m_renderGraph;

    // Atmosphere Scattering + Aerial Perspective campaign, Phase 3
    // (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE3_TRANSMITTANCE_LUT_v1.md)
    // - owns every atmosphere LUT/pass's ComputePipeline/descriptor set/
    // output texture across frames (see AtmosphereLutRenderer.h). Declared
    // right after m_renderGraph/before m_editorLayer for the same reason
    // m_renderGraph itself is: Run()'s offscreen-regime build lambda (see
    // src/Application/AtmospherePassSequence.h/Application.cpp) calls into
    // it every frame, so it must already be alive by the time that lambda
    // can possibly run, and must outlive it (destroyed only once the
    // Vulkan device it was built against is done being used). This is its
    // PERMANENT home as of Phase 7 (ATMOSPHERE_PHASE7_SKY_BACKGROUND_AND_COMPOSITE_PASSES_v1.md)
    // - see that phase's own completion report.
    AtmosphereLutRenderer m_atmosphereLutRenderer;

    // Atmosphere Scattering + Aerial Perspective campaign, Phase 8
    // (task_manager/atmosphere-scattering-1/ATMOSPHERE_PHASE8_SUN_ECS_AND_EDITOR_CONTROLS_v1.md)
    // - the small set of tunable, non-spatial atmosphere knobs edited live
    // via the Editor's new "Atmosphere" panel (Panels/AtmospherePanel.h) -
    // see AtmosphereTypes.h's own AtmosphereSettings doc comment for why
    // this lives on Application (not EditorContext, not an ECS component).
    // Threaded into the per-frame atmosphere pass-building code in Run()
    // (ground-albedo tint into AtmosphereParametersGpu before the shared
    // LUT passes, sky exposure into MakeRecordSkyBackgroundCallback(),
    // aerial-perspective strength into AddAtmosphereCompositePass()).
    AtmosphereSettings m_atmosphereSettings;

    // Declared after Renderer (and before Game) so it is destroyed before
    // Renderer's Vulkan device/instance go away, but its lifetime doesn't
    // need to relate to Game's at all.
    std::unique_ptr<IEditorLayer> m_editorLayer;
    Game m_game;

    // network-impl-2 campaign (task_manager/network-impl-2/) - the ONE
    // sanctioned cross-thread bridge a Network route handler is allowed to
    // touch (see AGENTS.md, "Networking", and FrameCaptureBridge.h's own
    // header comment). Declared BEFORE m_networkServer (constructed first,
    // destroyed last relative to it) so its address can be handed into
    // m_networkServer's own constructor below.
    FrameCaptureBridge m_captureBridge;

    // network-impl-3 campaign (task_manager/network-impl-3/) - the SECOND
    // sanctioned cross-thread bridge a Network route handler is allowed to
    // touch, this one for ECS-MUTATING commands (instantiate_primitive/
    // delete_entity - see AGENTS.md, "Networking", and
    // EngineCommandBridge.h's own header comment). Declared right after
    // m_captureBridge, for the exact same reason: BEFORE m_networkServer
    // (constructed first, destroyed last relative to it) so its address can
    // be handed into m_networkServer's own constructor below.
    EngineCommandBridge m_commandBridge;

    // Networking campaign (task_manager/network-impl-1/) - an embedded,
    // loopback-only HTTP server (see AGENTS.md, "Networking"). Declared
    // LAST (after Game) so it is DESTROYED FIRST, before Game/the Editor/
    // Renderer/Window/SDL start tearing down - a defensive ordering choice,
    // not a strictly necessary one today (no route handler touches any
    // engine state at all yet - see NetworkRoutes.h), but it's what a
    // FUTURE endpoint that DOES need to bridge into engine state would
    // already want: the background thread is guaranteed fully stopped
    // before anything it might eventually reference starts being torn down.
    Network::NetworkServer m_networkServer;

    // Current OS window size, kept up to date by the same WindowResized
    // event Renderer::OnResize()/m_editorLayer->OnWindowResized() react to
    // (Window's own Width()/Height() only ever reflect its CONSTRUCTION
    // size, never a later resize) - used only as the aspect ratio for the
    // release-build ("no Editor, straight to the swapchain") rendering path
    // in Run(); the Editor build's Game/Scene views each use their own
    // RenderTexture's aspect ratio instead (see
    // IEditorLayer::GameViewTarget()/SceneViewTarget()).
    int m_windowWidth = 0;
    int m_windowHeight = 0;
};

} // namespace gte
