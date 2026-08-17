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
            const std::optional<Event> event = EventTranslator::Translate(sdlEvent);
            if (!event.has_value()) {
                continue;
            }

            if (event->type == EventType::Quit) {
                running = false;
            }

            inputState.Apply(*event);
            m_game.OnEvent(*event);
        }

        const Uint64 nowTicksNs = SDL_GetTicksNS();
        const double deltaSeconds = static_cast<double>(nowTicksNs - lastTicksNs) / 1000000000.0;
        lastTicksNs = nowTicksNs;

        m_game.Update(deltaSeconds, inputState);
        m_game.Render(m_renderer);
    }

    return 0;
}

} // namespace gte
