#pragma once

#include "../../Core/GraphicsHandles.h"
#include "../../Core/PipelineDesc.h"
#include "../../Core/PixelFormat.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

namespace gte {
class ComputePipeline;
class Pipeline;
class Renderer;
struct ReflectedDescriptorBinding;
} // namespace gte

namespace gte::rg {

class RenderGraphBuilder;

enum class ShaderKind : std::uint8_t {
    Compute,  // shaders/<base>.comp.spv
    Graphics, // shaders/<base>.vert.spv + shaders/<base>.frag.spv
};

// One fullscreen pipeline variant: the attachment formats and the blend and depth state.
struct FullscreenKey {
    static constexpr std::size_t kMaxTargets = PipelineDesc::kMaxColorAttachments;

    std::array<PixelFormat, kMaxTargets> colorFormats{};
    std::uint8_t colorCount = 0;
    PixelFormat depthFormat = PixelFormat::Undefined; // Undefined: no depth attachment.
    BlendMode blend = BlendMode::Opaque;
    CompareOp depthCompare = CompareOp::Always;
    bool depthTest = false;
    bool depthWrite = false;

    // One color target, no depth.
    static constexpr FullscreenKey Single(PixelFormat color, BlendMode blend = BlendMode::Opaque) noexcept
    {
        FullscreenKey key;
        key.colorFormats[0] = color;
        key.colorCount = 1;
        key.blend = blend;
        return key;
    }

    friend constexpr bool operator==(const FullscreenKey&, const FullscreenKey&) = default;
};

// A shader loaded from "shaders/", reflected once. Pipelines are built lazily; a failed build retries on a
// cooldown and never throws. The Renderer must outlive it, and the device must be idle when it is destroyed.
class Shader {
public:
    explicit Shader(const char* baseName, ShaderKind kind = ShaderKind::Compute);
    ~Shader();

    Shader(const Shader&) = delete;
    Shader& operator=(const Shader&) = delete;
    Shader(Shader&&) = delete;
    Shader& operator=(Shader&&) = delete;

private:
    friend class ComputePass;
    friend class FullscreenPass;
    friend struct PassChainState;

    struct Impl; // Defined in Shader.cpp.

    static void BuildCompute(Impl& impl, Renderer& renderer);
    static void BuildGraphics(Impl& impl, Renderer& renderer);
    static Pipeline BuildFullscreenPipeline(const Impl& impl, Renderer& renderer, const FullscreenKey& key);

    // True when the shader is reflected and ready. False (silently) when there is no renderer or a retry is cooling down.
    bool EnsureBuilt(RenderGraphBuilder& builder);

    ShaderKind Kind() const noexcept;
    std::string_view Label() const noexcept;
    std::span<const ReflectedDescriptorBinding> ReflectedBindings() const noexcept;
    std::uint32_t PushConstantSize() const noexcept;
    ShaderStageMask PushStages() const noexcept;
    DescriptorSetLayoutHandle Layout() const noexcept;

    const ComputePipeline* Compute() const noexcept;

    // Pipeline for this target format and blend mode, built on first use. Null while a failed build cools down.
    // Valid until its frame ends: an evicted pipeline is retired, not destroyed.
    const Pipeline* GraphicsPipeline(RenderGraphBuilder& builder, const FullscreenKey& key);

    std::unique_ptr<Impl> m_impl;
};

} // namespace gte::rg
