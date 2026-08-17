#include "Window.h"

#include <SDL3/SDL.h>

#include <stdexcept>
#include <utility>

namespace gte {

Window::Window(const std::string& title, int width, int height)
    : m_width(width)
    , m_height(height)
{
    m_window = SDL_CreateWindow(title.c_str(), width, height, 0);
    if (m_window == nullptr) {
        throw std::runtime_error(std::string("Failed to create SDL window: ") + SDL_GetError());
    }
}

Window::~Window()
{
    if (m_window != nullptr) {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

Window::Window(Window&& other) noexcept
    : m_window(std::exchange(other.m_window, nullptr))
    , m_width(other.m_width)
    , m_height(other.m_height)
{
}

Window& Window::operator=(Window&& other) noexcept
{
    if (this != &other) {
        if (m_window != nullptr) {
            SDL_DestroyWindow(m_window);
        }
        m_window = std::exchange(other.m_window, nullptr);
        m_width = other.m_width;
        m_height = other.m_height;
    }
    return *this;
}

} // namespace gte
