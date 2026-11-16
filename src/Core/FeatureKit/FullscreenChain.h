#pragma once

#include "Frame.h"
#include "GpuBufferRef.h"
#include "GpuParams.h"
#include "Texture.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string_view>
#include <type_traits>

namespace gte::fx {

class FullscreenShader;

// How a depth test compares the draw's depth with the stored depth.
enum class DepthCompare : std::uint8_t { Less, LessEqual, Equal, Always };

// One draw into a target, bound by shader variable name. Made by Frame::Fullscreen(), ended by Draw().
// It draws a fullscreen triangle, or with IndirectDraw() what a GPU buffer says (shader-made triangle list).
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

    // Binds a graph buffer to the shader variable `name` (a literal), read-only: the shader must declare it
    // `readonly buffer` (vertex and fragment stage both work). A compute pass that wrote it earlier is barriered.
    // Needs BufferUse::Storage on the buffer.
    FullscreenChain& BindBuffer(std::string_view name, GpuBufferRef buffer);

    // Push-constant block; its size must match the shader's.
    template <typename T>
    FullscreenChain& Push(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "push constants are copied as raw bytes");
        return PushBytes(&value, sizeof(T));
    }

    // Uploads `value` into `params` and binds it to the shader variable "params" (a readonly storage block).
    template <typename T>
    FullscreenChain& Params(GpuParams<T>& params, const T& value)
    {
        return ParamsBytes("params", detail::ParamsAccess::Of(params), &value, sizeof(T));
    }

    // Same, bound to the shader variable `bindingName` (a literal).
    template <typename T>
    FullscreenChain& Params(std::string_view bindingName, GpuParams<T>& params, const T& value)
    {
        return ParamsBytes(bindingName, detail::ParamsAccess::Of(params), &value, sizeof(T));
    }

    // Draws the 16-byte command {vertexCount, instanceCount, firstVertex, firstInstance} at `byteOffset` (a multiple
    // of 4) of `args` instead of the triangle; firstInstance must be 0. Needs BufferUse::Indirect.
    // A compute pass that wrote `args` is barriered first. One pass name per draw (e.g. per LOD) lets one shader
    // draw several commands.
    FullscreenChain& IndirectDraw(GpuBufferRef args, std::uint64_t byteOffset = 0);

    // Declares a depth read the shader does not perform.
    FullscreenChain& Acknowledge(Texture depth);

    // The color target. A clear value set clears it, unset loads it.
    FullscreenChain& Target(Texture target, std::optional<std::array<float, 4>> clear = std::nullopt);

    // Tests (and, with `write`, updates) the depth of `depth` with `compare`. The depth is loaded, never cleared.
    // `depth` may be the view's own scene depth: it then counts as acknowledged.
    FullscreenChain& DepthTest(Texture depth, DepthCompare compare, bool write);

    // False when nothing was declared (disabled pass, inert shader, refused or invalid bindings).
    bool Draw();

private:
    friend class Frame;
    friend class PostOpaqueFrame;
    struct Impl;
    explicit FullscreenChain(std::unique_ptr<Impl> impl) noexcept;

    FullscreenChain& PushBytes(const void* data, std::size_t size);
    FullscreenChain& ParamsBytes(std::string_view bindingName, rg::GpuParamsBuffer& params, const void* data, std::size_t size);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
