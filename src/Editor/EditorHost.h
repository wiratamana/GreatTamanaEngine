#pragma once

#include <memory>
#include <string>

#include "../Core/Core.h"
#include "EditorHostServices.h"
#include "EditorLayer.h"
#include "../Window/Window.h"

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
// THIS PHASE (PHASE15) builds STRUCTURE ONLY: Run() is a deliberate,
// TEMPORARY stub (see EditorHost.cpp's own doc comment) that does NOT yet
// call Core::Update()/BuildFrame()/Present() at all, and does NOT yet own
// any automation bridge (EngineCommandBridge/FrameCaptureBridge/
// EditorUiCommandBridge/FrameDebuggerCommandBridge/
// AssetImportCommandBridge/Network::NetworkServer) - PHASE16
// (PHASE16_EDITORHOST_MAIN_LOOP_AND_AUTOMATION_BRIDGES.md) gives Run() its
// real per-frame loop and attaches every one of those bridges HERE, never
// to Core (design doc Section 6.1: "Core stays a pure engine facade: no
// HTTP server, no automation-bridge knowledge, ever").
//
// Application (src/Application/Application.h/.cpp) stays fully intact and
// unused as of this phase - only main.cpp stopped constructing it (it now
// constructs EditorHost instead). PHASE17
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
    // exit code. PHASE15: a deliberate, TEMPORARY stub - see EditorHost.cpp
    // for exactly what it does today and why. PHASE16 replaces this with
    // the real per-frame Core::Update()/BuildFrame()/Present() loop plus
    // every automation bridge's own per-frame servicing.
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
    std::unique_ptr<IEditorLayer> m_editorLayer;
};

} // namespace gte
