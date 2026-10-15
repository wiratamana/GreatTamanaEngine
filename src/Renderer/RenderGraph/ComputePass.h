#pragma once

#include "GpuParams.h"
#include "PassChain.h"
#include "RenderGraphHandles.h"
#include "StaticName.h"
#include "../../Core/Extent.h"

#include <algorithm>
#include <cstdint>

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

    // Dispatches over `size` elements, clamped to the first written storage image.
    // Returns false when nothing was declared (inert shader or invalid bindings).
    bool Dispatch(Extent2D size, std::uint32_t depth = 1);
};

} // namespace gte::rg
