#pragma once

#include <memory>
#include <string>

#include "../Editor/EditorLayer.h"
#include "../Game/Game.h"
#include "../Renderer/Renderer.h"
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
    // Declared after Renderer (and before Game) so it is destroyed before
    // Renderer's Vulkan device/instance go away, but its lifetime doesn't
    // need to relate to Game's at all.
    std::unique_ptr<IEditorLayer> m_editorLayer;
    Game m_game;
};

} // namespace gte
