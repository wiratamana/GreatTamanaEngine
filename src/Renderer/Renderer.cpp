#include "Renderer.h"

#include "../Window/Window.h"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <utility>

namespace gte {

Renderer::Renderer(Window& window)
{
    m_renderer = SDL_CreateRenderer(window.Native(), nullptr);
    if (m_renderer == nullptr) {
        throw std::runtime_error(std::string("Failed to create SDL renderer: ") + SDL_GetError());
    }
}

Renderer::~Renderer()
{
    if (m_renderer != nullptr) {
        SDL_DestroyRenderer(m_renderer);
        m_renderer = nullptr;
    }
}

Renderer::Renderer(Renderer&& other) noexcept
    : m_renderer(std::exchange(other.m_renderer, nullptr))
{
}

Renderer& Renderer::operator=(Renderer&& other) noexcept
{
    if (this != &other) {
        if (m_renderer != nullptr) {
            SDL_DestroyRenderer(m_renderer);
        }
        m_renderer = std::exchange(other.m_renderer, nullptr);
    }
    return *this;
}

void Renderer::Clear(std::uint8_t r, std::uint8_t g, std::uint8_t b, std::uint8_t a)
{
    SDL_SetRenderDrawColor(m_renderer, r, g, b, a);
    SDL_RenderClear(m_renderer);
}

void Renderer::Present()
{
    SDL_RenderPresent(m_renderer);
}

} // namespace gte
