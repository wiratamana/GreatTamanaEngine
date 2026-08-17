#include "Application.h"

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

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
        }

        const Uint64 nowTicksNs = SDL_GetTicksNS();
        const double deltaSeconds = static_cast<double>(nowTicksNs - lastTicksNs) / 1000000000.0;
        lastTicksNs = nowTicksNs;

        m_game.Update(deltaSeconds);
        m_game.Render(m_renderer);
    }

    return 0;
}

} // namespace gte
