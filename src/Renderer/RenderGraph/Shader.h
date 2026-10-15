#pragma once

#include "../../Core/GraphicsHandles.h"
#include "../../Core/PipelineDesc.h"
#include "../../Core/PixelFormat.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace gte {
class ComputePipeline;
class Pipeline;
struct ReflectedDescriptorBinding;
} // namespace gte

namespace gte::rg {

class RenderGraphBuilder;

enum class ShaderKind : std::uint8_t {
    Compute,  // shaders/<base>.comp.spv
    Graphics, // shaders/<base>.vert.spv + shaders/<base>.frag.spv
};

// One fullscreen pipeline variant: the target format and the blend mode.
struct FullscreenKey {
    PixelFormat colorFormat = PixelFormat::Undefined;
    BlendMode blend = BlendMode::Opaque;

    friend constexpr bool operator==(const FullscreenKey&, const FullscreenKey&) = default;
};

// A shader loaded from "shaders/", reflected once. Pipelines are built lazily; a failed build retries on a
// cooldown and never throws. The Renderer must outlive it, and the device must be idle when it is destroyed.
class Shader {
public:
    // Opaque state, defined in Shader.cpp (public so its build helpers can take it).
    struct Impl;

    explicit Shader(const char* baseName, ShaderKind kind = ShaderKind::Compute);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&&) = delete;
    Shader& operator=(Shader&&) = delete;

    // Graphics pipelines evicted but not yet freed (the GPU may still read them for a few frames).
    std::size_t RetiredPipelineCount() const noexcept;

private:
    friend class ComputePass;
    friend class FullscreenPass;
    friend struct PassChainState;

    // True when the shader is reflected and ready. False (silently) when there is no renderer or a retry is cooling down.
    bool EnsureBuilt(RenderGraphBuilder& builder);

    ShaderKind Kind() const noexcept;
    std::span<const ReflectedDescriptorBinding> ReflectedBindings() const noexcept;
    std::uint32_t PushConstantSize() const noexcept;
    ShaderStageMask PushStages() const noexcept;
    DescriptorSetLayoutHandle Layout() const noexcept;

    const ComputePipeline* Compute() const noexcept;

    // Pipeline for this target format and blend mode, built on first use. Null while a failed build cools down.
    // Valid until its frame ends: an evicted pipeline is retired, not destroyed.
    const Pipeline* GraphicsPipeline(RenderGraphBuilder& builder, const FullscreenKey& key);

    // True the first time `key` is seen, so a message is logged once per key.
    bool NoteOnce(std::string_view key);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::rg
