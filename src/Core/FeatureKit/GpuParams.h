#pragma once

#include "../../Renderer/RenderGraph/GpuParams.h"

#include <type_traits>

namespace gte::fx {

namespace detail {
struct ParamsAccess;
} // namespace detail

// CPU-written parameter block for one shader. Owned by the feature object; its size is fixed by the first use.
template <typename T>
class GpuParams {
    static_assert(std::is_trivially_copyable_v<T>, "GpuParams<T> is copied to the GPU as raw bytes");

private:
    friend struct detail::ParamsAccess;

    rg::GpuParamsBuffer m_buffer;
};

namespace detail {

// The chains reach the buffer through this; feature code cannot.
struct ParamsAccess {
    template <typename T>
    static rg::GpuParamsBuffer& Of(GpuParams<T>& params) noexcept
    {
        return params.m_buffer;
    }
};

} // namespace detail

} // namespace gte::fx
