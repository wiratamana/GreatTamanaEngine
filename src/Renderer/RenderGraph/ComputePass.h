#pragma once

#include "GpuParams.h"
#include "PassChain.h"
#include "RenderGraphHandles.h"
#include "StaticName.h"
#include "../../Core/Extent.h"

#include <algorithm>
#include <cstdint>
#include <vector>

namespace gte::rg {

class Shader;

// Clamps a dispatch to an image extent. A zero image extent means "unknown" and leaves it untouched.
constexpr Extent2D ClampDispatchSize(Extent2D requested, Extent2D imageExtent) noexcept
{
    return Extent2D{
        imageExtent.width != 0 ? std::min(requested.width, imageExtent.width) : requested.width,
        imageExtent.height != 0 ? std::min(requested.height, imageExtent.height) : requested.height,
    };
}

// One compute dispatch bound by shader variable name; access comes from reflection. End with Dispatch().
//   ComputePass(builder, shader, "Pass.Name", scope, event).Bind("src", tex).Dispatch(extent);
class ComputePass : public PassChain<ComputePass> {
public:
    ComputePass(RenderGraphBuilder& builder, Shader& shader, StaticName name, ViewScope scope, RenderPassEvent event);
    ~ComputePass();

    ComputePass(const ComputePass&) = delete;
    ComputePass& operator=(const ComputePass&) = delete;
    ComputePass(ComputePass&&) = delete;
    ComputePass& operator=(ComputePass&&) = delete;

    // Gives this dispatch its own descriptor set when the same pass name repeats in one view (one per instance key).
    ComputePass& Instance(std::uint64_t key) noexcept
    {
        m_instance = key;
        return *this;
    }

    // Uploads `size` bytes and binds the buffer to the shader variable "params".
    ComputePass& ParamsBytes(GpuParamsBuffer& params, const void* data, std::size_t size)
    {
        return ParamsBytes("params", params, data, size);
    }

    // Same, bound to the shader variable `bindingName` (a literal: it is read at Dispatch()).
    ComputePass& ParamsBytes(std::string_view bindingName, GpuParamsBuffer& params, const void* data, std::size_t size);

    // Uploads `value` and binds the buffer to the shader variable "params".
    template <typename T>
    ComputePass& Params(GpuParams<T>& params, const T& value)
    {
        if (m_state.inert) {
            return *this;
        }
        if (!params.Upload(m_state.builder, value, m_state.name.value)) {
            m_state.failed = true;
            return *this;
        }
        return Bind("params", params.Handle());
    }

    // Declares a graph write the shader performs on an imported buffer that is also bound by name with Bind().
    ComputePass& Writes(BufferHandle buffer)
    {
        if (!m_state.inert) {
            m_bufferWrites.push_back(buffer);
        }
        return *this;
    }

    // Dispatches over `size` elements, clamped to the first written storage image.
    // Returns false when nothing was declared (inert shader or invalid bindings).
    bool Dispatch(Extent2D size, std::uint32_t depth = 1);

private:
    std::uint64_t m_instance = 0;
    std::vector<BufferHandle> m_bufferWrites;
};

} // namespace gte::rg
