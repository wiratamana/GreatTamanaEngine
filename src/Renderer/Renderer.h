#pragma once

#include <cstdint>

struct SDL_Renderer;

namespace gte {

class Window;

// RAII wrapper around an SDL_Renderer bound to a Window. Owns the underlying
// SDL handle for its entire lifetime: acquired in the constructor, released
// in the destructor.
class Renderer {
public:
    explicit Renderer(Window& window);
    ~Renderer();

    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) noexcept;

    // Clears the back buffer to the given RGBA color (0-255 per channel).
    void Clear(std::uint8_t r = 0, std::uint8_t g = 0, std::uint8_t b = 0, std::uint8_t a = 255);

    // Presents the back buffer to the window.
    void Present();

    SDL_Renderer* Native() const noexcept { return m_renderer; }

private:
    SDL_Renderer* m_renderer = nullptr;
};

} // namespace gte
