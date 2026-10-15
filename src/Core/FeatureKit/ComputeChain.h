#pragma once

#include "Frame.h"
#include "GpuParams.h"
#include "Texture.h"
#include "../Extent.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string_view>
#include <type_traits>

namespace gte::fx {

class ComputeShader;

// One compute dispatch, bound by shader variable name. Made by Frame::Compute(), ended by Dispatch().
// Bound names must be literals (they are read at Dispatch()). A refused call makes Dispatch() declare nothing.
//   f.Compute(shader, "Blur.Dispatch").Params(params, p).Bind("src", tex).Bind("dst", out).Dispatch(extent);
class ComputeChain {
public:
    ~ComputeChain();
    ComputeChain(ComputeChain&&) noexcept;
    ComputeChain& operator=(ComputeChain&&) noexcept;
    ComputeChain(const ComputeChain&) = delete;
    ComputeChain& operator=(const ComputeChain&) = delete;

    ComputeChain& Bind(std::string_view name, Texture texture);
    ComputeChain& Bind(std::string_view name, DepthRef depth);

    // Push-constant block; its size must match the shader's.
    template <typename T>
    ComputeChain& Push(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "push constants are copied as raw bytes");
        return PushBytes(&value, sizeof(T));
    }

    // Uploads `value` into `params` and binds it to the shader variable "params".
    template <typename T>
    ComputeChain& Params(GpuParams<T>& params, const T& value)
    {
        return ParamsBytes("params", detail::ParamsAccess::Of(params), &value, sizeof(T));
    }

    // Same, bound to the shader variable `bindingName` (a literal), for a shader with several parameter blocks.
    template <typename T>
    ComputeChain& Params(std::string_view bindingName, GpuParams<T>& params, const T& value)
    {
        return ParamsBytes(bindingName, detail::ParamsAccess::Of(params), &value, sizeof(T));
    }

    // Dispatches over `size` elements, clamped to the first written storage image.
    // False when nothing was declared (disabled pass, inert shader, refused or invalid bindings).
    bool Dispatch(Extent2D size, std::uint32_t depth = 1);

private:
    friend class Frame;
    struct Impl;
    explicit ComputeChain(std::unique_ptr<Impl> impl) noexcept;

    ComputeChain& PushBytes(const void* data, std::size_t size);
    ComputeChain& ParamsBytes(std::string_view bindingName, rg::GpuParamsBuffer& params, const void* data, std::size_t size);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::fx
