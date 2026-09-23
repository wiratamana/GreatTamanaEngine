#pragma once

#include "../Core/ISurfaceProvider.h"

#include <cstdint>
#include <string>
#include <vector>

// Forward-declared so this header does not leak the SDL dependency onto
// anything that only needs a pointer/reference to Window (Renderer, Game,
// etc.). Only Window.cpp includes <SDL3/SDL.h> directly.
struct SDL_Window;

// VkInstance/VkSurfaceKHR are now forward-declared once, canonically, by
// Core/ISurfaceProvider.h (editor-core-separation-1 campaign, PHASE10) -
// Window no longer redeclares its own copy of these two typedefs.

namespace gte {

// RAII wrapper around an SDL_Window. Owns the underlying SDL handle for its
// entire lifetime: acquired in the constructor, released in the destructor.
// Implements ISurfaceProvider (Core/ISurfaceProvider.h, editor-core-separation-1
// campaign, PHASE10) so Renderer/VulkanSurface depend on that abstract
// interface instead of this concrete class - see
// task_manager/editor-core-separation-1/PHASE10_ISURFACEPROVIDER_INTERFACE_AND_WINDOW_INVERSION.md.
// Window still physically lives inside gte_core as of this phase (Phase 14
// is what actually moves Window.cpp/SdlContext into gte_editor) - this phase
// only inverts the dependency DIRECTION, not the file's physical location.
class Window : public ISurfaceProvider {
public:
    // resizable defaults to true so the window (and its OS maximize
    // button) behaves like a normal desktop window out of the box - pass
    // false for cases that genuinely want a fixed-size window (e.g. a
    // splash/about dialog later).
    Window(const std::string& title, int width, int height, bool resizable = true);
    ~Window() override;

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    SDL_Window* Native() const noexcept { return m_window; }

    // This window's SDL window ID (SDL_GetWindowID()) - returned as a plain
    // std::uint32_t (SDL's own Uint32/SDL_WindowID are typedef'd to exactly
    // this) so this header still never needs to include an SDL header.
    // Every SDL keyboard/mouse/window event carries the ID of the specific
    // SDL window it actually happened on (sdlEvent.key.windowID, etc.) -
    // once Dear ImGui's multi-viewport feature is enabled (see
    // ImGuiEditorLayer), MORE THAN ONE SDL window can exist at a time (any
    // panel dragged outside the main window becomes its own extra SDL
    // window, owned/managed entirely by imgui_impl_sdl3.cpp), so
    // EventTranslator uses this to tell "this event happened on Application's
    // own main game window" apart from "this event happened on one of
    // ImGui's own extra platform windows" (see EventTranslator::Translate()).
    std::uint32_t Id() const noexcept;

    int Width() const noexcept override { return m_width; }
    int Height() const noexcept override { return m_height; }

    // Vulkan instance extension names this platform/window needs in order to
    // create a VkSurfaceKHR for it (e.g. VK_KHR_win32_surface). Only the
    // implementation (Window.cpp) touches SDL's Vulkan helpers - callers get
    // back plain strings, decoupled from SDL. editor-core-separation-1
    // campaign, PHASE10 - converted from a `static` method to a real virtual
    // ISurfaceProvider override (Locked Design Decision, design doc Section
    // 5.2) - every call site now goes through a live Window/ISurfaceProvider&
    // instance instead of the old static form.
    std::vector<std::string> VulkanInstanceExtensions() const override;

    // Creates a VkSurfaceKHR for this window under the given VkInstance.
    // Ownership of the returned surface transfers to the caller: it must be
    // destroyed with vkDestroySurfaceKHR(instance, surface, ...) before the
    // instance is destroyed (see VulkanSurface, which wraps exactly that).
    VkSurfaceKHR CreateVulkanSurface(VkInstance instance) const override;

private:
    SDL_Window* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
};

} // namespace gte
