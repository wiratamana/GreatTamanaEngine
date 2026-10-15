#pragma once

#include "GpuParams.h"
#include "RenderGraphHandles.h"
#include "StaticName.h"
#include "../../Core/Extent.h"
#include "../../Core/GraphicsHandles.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

namespace gte::rg {

class Shader;

// Marks a texture binding as a depth sampler.
struct DepthRef {
    TextureHandle texture;
};

inline DepthRef Depth(TextureHandle texture) noexcept
{
    return DepthRef{ texture };
}

// Clamps a dispatch to an image extent. A zero image extent means "unknown" and leaves it untouched.
constexpr Extent2D ClampDispatchSize(Extent2D requested, Extent2D imageExtent) noexcept
{
    return Extent2D{
        imageExtent.width != 0 ? std::min(requested.width, imageExtent.width) : requested.width,
        imageExtent.height != 0 ? std::min(requested.height, imageExtent.height) : requested.height,
    };
}

// One compute dispatch, bound by shader variable name; read/write access comes from the shader.
//   ComputePass(builder, shader, "Pass.Name", scope, event).Bind("src", tex).Dispatch(extent);
// Use as a temporary and always end with Dispatch(). Names must be string literals.
// A shader that is not ready makes the whole chain inert: it declares nothing.
class ComputePass {
public:
    ComputePass(RenderGraphBuilder& builder, Shader& shader, StaticName name, ViewScope scope, RenderPassEvent event);
    ~ComputePass();

    ComputePass(const ComputePass&) = delete;
    ComputePass& operator=(const ComputePass&) = delete;
    ComputePass(ComputePass&&) = delete;
    ComputePass& operator=(ComputePass&&) = delete;

    ComputePass& Bind(std::string_view name, TextureHandle texture);
    ComputePass& Bind(std::string_view name, DepthRef depth);
    ComputePass& Bind(std::string_view name, BufferObjectHandle buffer);

    // Uploads `value` and binds the buffer to the shader variable "params".
    template <typename T>
    ComputePass& Params(GpuParams<T>& params, const T& value)
    {
        if (m_inert) {
            return *this;
        }
        if (!params.Upload(m_builder, value, m_name.value)) {
            m_failed = true;
            return *this;
        }
        return Bind("params", params.Handle());
    }

    // Push-constant block; its size must match the shader's.
    template <typename T>
    ComputePass& Push(const T& value)
    {
        static_assert(std::is_trivially_copyable_v<T>, "push constants are copied as raw bytes");
        if (!m_inert) {
            m_push.resize(sizeof(T));
            std::memcpy(m_push.data(), &value, sizeof(T));
        }
        return *this;
    }

    // Dispatches over `size` elements, clamped to the first written storage image.
    // Returns false when nothing was declared (inert shader or invalid bindings).
    bool Dispatch(Extent2D size, std::uint32_t depth = 1);

    // Dispatches over the extent of the first written storage image.
    bool Dispatch();

private:
    struct Argument {
        std::string_view name;
        TextureHandle texture{};
        BufferObjectHandle buffer = BufferObjectHandle::Invalid;
        bool isBuffer = false;
        bool isDepth = false;
    };

    void Add(const Argument& argument);
    bool Finish(Extent2D requested, bool useImageExtent, std::uint32_t depth);
    void Report(const std::string& text);

    RenderGraphBuilder& m_builder;
    Shader& m_shader;
    StaticName m_name;
    ViewScope m_scope;
    RenderPassEvent m_event;
    std::vector<Argument> m_arguments;
    std::vector<std::byte> m_push;
    bool m_inert = false;
    bool m_failed = false;
    bool m_dispatched = false;
};

} // namespace gte::rg
