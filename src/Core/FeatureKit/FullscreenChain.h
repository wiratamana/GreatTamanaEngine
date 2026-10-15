#pragma once

#include "Frame.h"
#include "Texture.h"

#include <array>
#include <cstddef>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>

namespace gte::fx {

class FullscreenShader;

// One fullscreen draw into a target, bound by shader variable name. Made by Frame::Fullscreen(), ended by Draw().
// The frame's scene depth is acknowledged for you unless you bind or acknowledge it yourself.
//   f.Fullscreen(shader, "Tint.Draw").Bind("src", f.SceneColor()).Push(p).Target(f.PrivateTarget()).Draw();
class FullscreenChain {
public:
    ~FullscreenChain();
    FullscreenChain(FullscreenChain&&) noexcept;
    FullscreenChain& operator=(FullscreenChain&&) noexcept;
    FullscreenChain(const FullscreenChain&) = delete;
    FullscreenChain& operator=(const FullscreenChain&) = delete;

    FullscreenChain& Bind(std::string_view name, Texture texture);
    FullscreenChain& Bind(std::string_view name, DepthRef depth);

    // Push-constant block; its size must match the shader's.
    template <typename T>
    FullscreenChain& Push(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "push constants are copied as raw bytes");
        return PushBytes(&value, sizeof(T));
    }

    // Declares a depth read the shader does not perform.
    FullscreenChain& Acknowledge(Texture depth);

    // The color target. A clear value set clears it, unset loads it.
    FullscreenChain& Target(Texture target, std::optional<std::array<float, 4>> clear = std::nullopt);

    // False when nothing was declared (disabled pass, inert shader, refused or invalid bindings).
    bool Draw();

private:
    friend class Frame;
    struct Impl;
    explicit FullscreenChain(std::unique_ptr<Impl> impl) noexcept;

    FullscreenChain& PushBytes(const void* data, std::size_t size);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
