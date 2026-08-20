#include "Window.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <stdexcept>
#include <utility>

namespace gte {

Window::Window(const std::string& title, int width, int height, bool resizable)
    : m_width(width)
    , m_height(height)
{
    const SDL_WindowFlags flags = resizable ? SDL_WINDOW_RESIZABLE : 0;
    m_window = SDL_CreateWindow(title.c_str(), width, height, flags);
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

std::vector<std::string> Window::VulkanInstanceExtensions()
{
    Uint32 count = 0;
    char const* const* extensions = SDL_Vulkan_GetInstanceExtensions(&count);
    if (extensions == nullptr) {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }

    std::vector<std::string> result;
    result.reserve(count);
    for (Uint32 i = 0; i < count; ++i) {
        result.emplace_back(extensions[i]);
    }
    return result;
}

std::uint32_t Window::Id() const noexcept
{
    return static_cast<std::uint32_t>(SDL_GetWindowID(m_window));
}

VkSurfaceKHR Window::CreateVulkanSurface(VkInstance instance) const
{
    VkSurfaceKHR surface = nullptr;
    if (!SDL_Vulkan_CreateSurface(m_window, instance, nullptr, &surface)) {
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
    return surface;
}

} // namespace gte
