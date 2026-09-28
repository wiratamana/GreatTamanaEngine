#pragma once

#include <memory>
#include <string>

#include "../Core/Core.h"
#include "../Core/EditorCapabilities.h"
#include "../Core/EngineContext.h"
#include "EditorHostServices.h"
#include "EditorLayer.h"
#include "../Game/Game.h"
#include "../Network/NetworkServer.h"
#include "../Renderer/Atmosphere/AtmosphereLutRenderer.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/VolumeTexturePreviewRenderer.h"
#include "../Window/Window.h"
#include "../Application/AssetImportCommandBridge.h"
#include "../Application/EngineCommandBridge.h"
#include "../Application/EditorUiCommandBridge.h"
#include "../Application/FrameCaptureBridge.h"
#include "../Application/FrameDebuggerCommandBridge.h"
// editor-core-separation-8 campaign, PHASE5
// (PHASE5_CROSS_THREAD_BRIDGE_AND_HTTP_ENDPOINTS.md) - the new
// RenderGraphControlCommandBridge (m_renderGraphControlCommandBridge below).
#include "../Application/RenderGraphControlCommandBridge.h"
// editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
// BIG-STEP 3), PHASE3 - the new ProjectAssemblyHotReloadCommandBridge
// (m_hotReloadCommandBridge below).
#include "../Application/ProjectAssemblyHotReloadCommandBridge.h"
// editor-core-separation-17 campaign (On-Engine Project Workflow plan,
// BIG-STEP 3), PHASE3 - the new ProjectLifecycleLoadCommandBridge
// (m_projectLifecycleLoadCommandBridge below).
#include "../Application/ProjectLifecycleLoadCommandBridge.h"

namespace gte {

// editor-core-separation-1 campaign, PHASE15
// (PHASE15_EDITORHOST_COMPOSITION_ROOT_CORE_CONSTRUCTION.md) - the new
// composition root that REPLACES Application's role (design doc Section 6,
// "Editor's Composition Root Contract"): owns SdlContext + its own
// authoring Window (NOT the same window instance a shipped Player build
// would use - design doc Section 1.1), constructs Core (gte_core's own
// public facade) by injecting Window as ISurfaceProvider& and its own
// small EditorHostServices as IHostServices&, constructs the concrete
// Editor UI/state via CreateEditorLayer() (src/Editor/EditorLayer.h,
// resolving unambiguously to ImGuiEditorLayer.cpp since PHASE9's
// CreateEditorLayer()/CreateNullEditorLayer() rename), and wires
// Core::SetEditorLayerHook() - PHASE0_MASTER_STRATEGY.md's Locked Design
// Decision #8, the ONE nullable "big" IEditorLayer* opaque hook - to that
// freshly-constructed instance, in the constructor BODY (never the
// initializer list, since m_editorLayer must already be fully constructed
// first), so it is genuinely non-null before Run() is ever called.
//
// editor-core-separation-1 campaign, PHASE16
// (PHASE16_EDITORHOST_MAIN_LOOP_AND_AUTOMATION_BRIDGES.md) - Run() now owns
// the REAL main loop (byte-for-byte copied from Application::Run()'s own
// post-PHASE13 shape - see EditorHost.cpp's own doc comment), and every
// automation bridge (EngineCommandBridge/FrameCaptureBridge/
// EditorUiCommandBridge/FrameDebuggerCommandBridge/AssetImportCommandBridge)
// plus the embedded Network::NetworkServer now live HERE, never on Core
// (design doc Section 6.1: "Core stays a pure engine facade: no HTTP
// server, no automation-bridge knowledge, ever"). The ONE real Bucket B
// capability adapter this campaign found (ISceneIOCapability -
// PHASE5/PHASE6), previously wired through Application's own TEMPORARY
// SetSceneIOCapability() setter, is now wired directly here, in this
// class's own constructor body, its real, PERMANENT home.
//
// Application (src/Application/Application.h/.cpp) stays fully intact and
// unused as of this phase (shrunk further to reflect the bridges' move
// here - see that file's own doc comments) - only main.cpp stopped
// constructing it (it now constructs EditorHost instead). PHASE17
// (PHASE17_APPLICATION_RETIREMENT_AND_EXECUTABLE_RENAME.md) is what
// actually retires Application once EditorHost fully covers its
// responsibilities.
class EditorHost {
public:
    EditorHost(const std::string& title, int width, int height);
    ~EditorHost();

    EditorHost(const EditorHost&) = delete;
    EditorHost& operator=(const EditorHost&) = delete;
    EditorHost(EditorHost&&) = delete;
    EditorHost& operator=(EditorHost&&) = delete;

    // Runs the main loop until the window is closed. Returns a process
    // exit code. PHASE16: the REAL per-frame loop - see EditorHost.cpp for
    // the full body, copied from Application::Run()'s own post-PHASE13
    // shape.
    int Run();

private:
    // RAII guard for SDL_Init()/SDL_Quit() - mirrors
    // Application::SdlContext's exact shape/precedent (src/Application/
    // Application.h) almost verbatim. Declared FIRST so it is constructed
    // before, and destroyed after, every other SDL-owning member below it
    // (Window, ...) - this keeps init/shutdown ordering correct
    // automatically instead of relying on manual cleanup code.
    struct SdlContext {
        SdlContext();
        ~SdlContext();

        SdlContext(const SdlContext&) = delete;
        SdlContext& operator=(const SdlContext&) = delete;
    };

    // Construction order matches Step 3.3 of PHASE15's own strategy file
    // EXACTLY (SdlContext -> Window -> Core -> CreateEditorLayer() ->
    // Core::SetEditorLayerHook(), the last of which happens in the
    // constructor BODY, see EditorHost.cpp): each member below depends only
    // on members declared BEFORE it (m_window needs m_sdlContext already
    // initialized; m_core needs m_window - as ISurfaceProvider& - and
    // m_hostServices already constructed; m_editorLayer needs m_window and
    // m_core's own Renderer already constructed).
    SdlContext m_sdlContext;
    Window m_window;
    EditorHostServices m_hostServices;
    Core m_core;

    // editor-core-separation-1 campaign, PHASE16 - REFERENCE members bound
    // to m_core's own real, owned instances, mirroring
    // Application::m_renderer/m_renderGraph/m_game/m_engineContext's exact
    // precedent (PHASE12/PHASE13) - Run()'s own host-level code (input
    // handling, BuildUI(), the relocated FrameCaptureBridge success-path
    // capture code) reaches these directly rather than through
    // m_core.GetX() at every call site.
    Renderer& m_renderer;
    rg::RenderGraph& m_renderGraph;

    // Declared after Renderer/RenderGraph (and before Game) so it is
    // destroyed before Renderer's Vulkan device/instance go away, but its
    // lifetime doesn't need to relate to Game's at all.
    std::unique_ptr<IEditorLayer> m_editorLayer;
    Game& m_game;
    EngineContext& m_engineContext;

    // editor-core-separation-1 campaign, PHASE16 - REFERENCE members bound
    // to m_core's own real, owned AtmosphereSettings/AtmosphereLutRenderer
    // instances - needed for two remaining host-level call sites:
    // IEditorLayer::BuildUI() (the "Atmosphere" panel edits/reads them
    // live) and the relocated Game-View FrameCaptureBridge success-path
    // capture (reads m_atmosphereLutRenderer.CompositedOutput() as its
    // capture source) - see Run()'s own body.
    AtmosphereSettings& m_atmosphereSettings;
    AtmosphereLutRenderer& m_atmosphereLutRenderer;

    // network-impl-6 campaign, Phase 4 - the GET /get_texture volume-texture
    // raymarch preview renderer. A genuinely lazy/on-demand class
    // (EnsureInitialized() does nothing until the first real RenderPreview()
    // call), so adding it unconditionally as a plain member here costs
    // nothing at startup. HOST-LEVEL AUTOMATION concern (design doc Section
    // 6.1) - never moves into Core.
    VolumeTexturePreviewRenderer m_volumeTexturePreviewRenderer;

    // editor-core-separation-1 campaign, PHASE16
    // (PHASE16_EDITORHOST_MAIN_LOOP_AND_AUTOMATION_BRIDGES.md) - every
    // automation bridge RELOCATED here from Application (design doc Section
    // 6.1: "these attach to EditorHost, never to Core"). Declared in the
    // exact same relative order/reasoning Application.h used to document:
    // each bridge below is declared BEFORE m_networkServer (constructed
    // first, destroyed last relative to it) so its address can be handed
    // into m_networkServer's own constructor.
    FrameCaptureBridge m_captureBridge;
    EngineCommandBridge m_commandBridge;
    EditorUiCommandBridge m_uiCommandBridge;
    FrameDebuggerCommandBridge m_frameDebuggerCommandBridge;
    AssetImportCommandBridge m_assetImportCommandBridge;
    // editor-core-separation-8 campaign, PHASE5
    // (PHASE5_CROSS_THREAD_BRIDGE_AND_HTTP_ENDPOINTS.md) - the new bridge
    // backing the 6 GET /render_graph/* mutation/discovery routes. Same
    // "declared BEFORE m_networkServer" placement reasoning as every other
    // bridge above.
    RenderGraphControlCommandBridge m_renderGraphControlCommandBridge;
    // editor-core-separation-14 campaign (Project Assembly Hot Reload plan,
    // BIG-STEP 3), PHASE3 - the cross-thread hand-off for
    // PerformProjectAssemblyHotReload(). Same "declared BEFORE
    // m_networkServer" placement reasoning as every other bridge above -
    // this one, however, is NOT itself handed into NetworkServer's
    // constructor (see EditorHotReloadDebugCapability::
    // SetHotReloadCommandBridge(), this same phase, for how the route reaches
    // it instead).
    ProjectAssemblyHotReloadCommandBridge m_hotReloadCommandBridge;

    // editor-core-separation-17 campaign (On-Engine Project Workflow plan,
    // BIG-STEP 3), PHASE3 - the cross-thread hand-off for the "Open
    // Project" Tier-3 real .dll load, submitted ONLY by
    // EditorProjectLifecycleCapability::OpenProjectAssembly() (the
    // network-thread-facing method - OpenProjectAssemblyOnMainThread(),
    // the ImGui-facing method, never touches this bridge at all, see
    // PHASE0_MASTER_STRATEGY.md Section 2.2). Same "declared BEFORE
    // m_networkServer" placement reasoning as every other bridge above.
    ProjectLifecycleLoadCommandBridge m_projectLifecycleLoadCommandBridge;

    // Networking campaign (task_manager/network-impl-1/) - an embedded,
    // loopback-only HTTP server (see AGENTS.md, "Networking"). Declared
    // LAST (after every bridge above) so it is DESTROYED FIRST, before the
    // Editor/Renderer/Window/SDL start tearing down.
    Network::NetworkServer m_networkServer;

    // editor-core-separation-1 campaign, PHASE16 - the ONE real Bucket B
    // capability this campaign found (ISceneIOCapability,
    // Core/EditorCapabilities.h) - wired to a real EditorSceneIOCapability
    // instance directly in this class's own constructor body (its
    // PERMANENT home, replacing Application's own TEMPORARY
    // SetSceneIOCapability() setter from PHASE5/PHASE6). Consulted by
    // Run()'s own EngineCommandBridge servicing code
    // (ExecuteEngineCommand()).
    ISceneIOCapability* m_sceneIOCapability = nullptr;
};

} // namespace gte
