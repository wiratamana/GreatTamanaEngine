#pragma once

#include <string>
#include <vector>

// Forward-declared so this header does not leak the SDL dependency onto
// anything that only needs a pointer/reference to Window (Renderer, Game,
// etc.). Only Window.cpp includes <SDL3/SDL.h> directly.
struct SDL_Window;

// Forward-declared Vulkan handle types, so this header does not need to
// include <volk.h>/<vulkan/vulkan.h>. These match Vulkan's own handle-macro
// expansion exactly (VK_DEFINE_HANDLE / VK_DEFINE_NON_DISPATCHABLE_HANDLE on
// 64-bit), so they are interchangeable with the "real" VkInstance/
// VkSurfaceKHR wherever this header and the real Vulkan headers both end up
// included in the same translation unit (e.g. Renderer.cpp) - this is the
// same trick SDL's own <SDL3/SDL_vulkan.h> uses.
struct VkInstance_T;
using VkInstance = VkInstance_T*;
struct VkSurfaceKHR_T;
using VkSurfaceKHR = VkSurfaceKHR_T*;

namespace gte {

// RAII wrapper around an SDL_Window. Owns the underlying SDL handle for its
// entire lifetime: acquired in the constructor, released in the destructor.
class Window {
public:
    // resizable defaults to true so the window (and its OS maximize
    // button) behaves like a normal desktop window out of the box - pass
    // false for cases that genuinely want a fixed-size window (e.g. a
    // splash/about dialog later).
    Window(const std::string& title, int width, int height, bool resizable = true);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    SDL_Window* Native() const noexcept { return m_window; }

    int Width() const noexcept { return m_width; }
    int Height() const noexcept { return m_height; }

    // Vulkan instance extension names this platform/window needs in order to
    // create a VkSurfaceKHR for it (e.g. VK_KHR_win32_surface). Only the
    // implementation (Window.cpp) touches SDL's Vulkan helpers - callers get
    // back plain strings, decoupled from SDL.
    static std::vector<std::string> VulkanInstanceExtensions();

    // Creates a VkSurfaceKHR for this window under the given VkInstance.
    // Ownership of the returned surface transfers to the caller: it must be
    // destroyed with vkDestroySurfaceKHR(instance, surface, ...) before the
    // instance is destroyed (see VulkanSurface, which wraps exactly that).
    VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const;

private:
    SDL_Window* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
};

} // namespace gte
