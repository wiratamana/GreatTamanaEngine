#include "Application.h"

#include "EventTranslator.h"

#include <SDL3/SDL.h>

#include <stdexcept>

namespace gte {

Application::SdlContext::SdlContext()
{
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
{
}

Application::~Application() = default;

int Application::Run()
{
    bool running = true;
    Uint64 lastTicksNs = SDL_GetTicksNS();

    InputState inputState;

    while (running) {
        // Clear last frame's transient "just pressed/released" flags and
        // per-frame mouse/wheel deltas before this frame's events arrive.
        inputState.BeginFrame();

        SDL_Event sdlEvent;
        while (SDL_PollEvent(&sdlEvent)) {
            // Editor gets first look at every raw event (its own SDL3
            // backend tracks mouse/keyboard for ImGui widgets) - a no-op in
            // a release build (NullEditorLayer).
            m_editorLayer->ProcessEvent(sdlEvent);

            const std::optional<Event> event = EventTranslator::Translate(sdlEvent);
            if (!event.has_value()) {
                continue;
            }

            if (event->type == EventType::Quit) {
                running = false;
            } else if (event->type == EventType::WindowResized) {
                const auto& resized = std::get<WindowResizedEventData>(event->data);
                m_renderer.OnResize(resized.width, resized.height);
                // Keeps the Editor's Game-view RenderTexture tracking the
                // window's size (no-op in a release build) - see
                // ImGuiEditorLayer::OnWindowResized.
                m_editorLayer->OnWindowResized(resized.width, resized.height);
            }

            inputState.Apply(*event);
            m_game.OnEvent(*event);
        }

        const Uint64 nowTicksNs = SDL_GetTicksNS();
        const double deltaSeconds = static_cast<double>(nowTicksNs - lastTicksNs) / 1000000000.0;
        lastTicksNs = nowTicksNs;

        m_editorLayer->NewFrame();

        // Clears last frame's queued Submit() draw items before Game gets a
        // chance to queue this frame's - see Renderer::BeginFrame().
        m_renderer.BeginFrame();

        m_game.Update(deltaSeconds, inputState);
        // Game only clears/draws here - it never decides *where* that ends
        // up (swapchain vs. an off-screen texture); see below.
        m_game.Render(m_renderer);

        // Ask the Editor where Game's frame should actually land: an
        // off-screen RenderTexture it wants to display in a "Game" panel
        // (Editor build), or nullptr meaning "the swapchain, fullscreen"
        // (release build - see NullEditorLayer). This is the one seam that
        // decides Unity-style Editor-vs-final-build rendering, and it lives
        // here in Application (the composition root), not in Game.
        if (RenderTexture* gameTarget = m_editorLayer->GameViewTarget()) {
            m_renderer.RenderOffscreen(*gameTarget);
        }

        // Build every editor panel (currently just "Game") now that its
        // texture (if any) has this frame's contents.
        m_editorLayer->BuildUI();

        // Present the swapchain. In an Editor build this draws the editor's
        // own ImGui chrome (which itself displays the Game view above) via
        // the recordExtra hook; in a release build recordExtra is
        // effectively a no-op (NullEditorLayer::Render does nothing) and
        // this just presents whatever Game rendered straight into the
        // swapchain moments ago.
        m_renderer.Present([this](VkCommandBuffer cmd) { m_editorLayer->Render(cmd); });
    }

    return 0;
}

} // namespace gte
