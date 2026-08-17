#pragma once

#include <string>

// Forward-declared so this header does not leak the SDL dependency onto
// anything that only needs a pointer/reference to Window (Renderer, Game,
// etc.). Only Window.cpp includes <SDL3/SDL.h> directly.
struct SDL_Window;

namespace gte {

// RAII wrapper around an SDL_Window. Owns the underlying SDL handle for its
// entire lifetime: acquired in the constructor, released in the destructor.
class Window {
public:
    Window(const std::string& title, int width, int height);
    ~Window();

    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;

    SDL_Window* Native() const noexcept { return m_window; }

    int Width() const noexcept { return m_width; }
    int Height() const noexcept { return m_height; }

private:
    SDL_Window* m_window = nullptr;
    int m_width = 0;
    int m_height = 0;
};

} // namespace gte
