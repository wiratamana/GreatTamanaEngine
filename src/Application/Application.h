#pragma once

#include <memory>
#include <string>

#include "../Editor/EditorLayer.h"
#include "../Game/Game.h"
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
    // Declared after Renderer (and before Game) so it is destroyed before
    // Renderer's Vulkan device/instance go away, but its lifetime doesn't
    // need to relate to Game's at all.
    std::unique_ptr<IEditorLayer> m_editorLayer;
    Game m_game;

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
