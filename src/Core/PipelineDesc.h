#pragma once

#include "Extent.h"
#include "GraphicsHandles.h"
#include "PixelFormat.h"
#include "../Renderer/VertexLayout.h"

#include <array>
#include <cstdint>
#include <span>

namespace gte {

enum class BlendMode : std::uint8_t { Opaque, AlphaBlend, Additive };
enum class CullMode : std::uint8_t { None, Front, Back };
enum class CompareOp : std::uint8_t { Never, Less, Equal, LessEqual, Greater, GreaterEqual, Always };
enum class Topology : std::uint8_t { TriangleList, LineList, PointList };
enum class DescriptorKind : std::uint8_t { CombinedImageSampler, UniformBuffer, StorageBuffer, StorageImage };

enum class ShaderStageMask : std::uint8_t {
    None = 0,
    Vertex = 1 << 0,
    Fragment = 1 << 1,
    Compute = 1 << 2,
};

constexpr ShaderStageMask operator|(ShaderStageMask a, ShaderStageMask b) noexcept
{
    return static_cast<ShaderStageMask>(static_cast<std::uint8_t>(a) | static_cast<std::uint8_t>(b));
}
constexpr bool HasStage(ShaderStageMask mask, ShaderStageMask stage) noexcept
{
    return (static_cast<std::uint8_t>(mask) & static_cast<std::uint8_t>(stage)) != 0;
}

// One binding inside a caller-defined descriptor-set layout - no raw
// VkDescriptorSetLayoutBinding ever reaches outside src/Renderer. Build a
// real layout from a span of these via Renderer::CreateDescriptorSetLayout().
struct DescriptorBindingDesc {
    std::uint32_t binding = 0;
    DescriptorKind kind = DescriptorKind::CombinedImageSampler;
    std::uint32_t count = 1;
    ShaderStageMask stages = ShaderStageMask::Fragment;
};

// Extra pipeline-layout state beyond PipelineDesc's fixed fields below -
// default-constructed means "no extra descriptor sets, no push constants".
// `extraSetLayouts` is a non-owning span - the caller's own local array must
// stay alive across the single, synchronous Renderer::CreatePipeline() call
// that reads it (same lifetime contract as every other std::span parameter
// in this engine - never stored beyond that one call).
struct PipelineLayoutDesc {
    DescriptorSetLayoutHandle sceneServicesSetLayout = DescriptorSetLayoutHandle::Invalid;
    std::span<const DescriptorSetLayoutHandle> extraSetLayouts;
    std::uint32_t pushConstantByteSize = 0;
    ShaderStageMask pushConstantStages = ShaderStageMask::None;
};

struct PipelineDesc {
    // Multi-Render-Target cap - kept as its own small local constant (see
    // Renderer/Pipeline.h's kPipelineMaxColorAttachments for the same
    // reasoning: this is a lower-level, Vulkan-free contract and must not
    // reach into a higher-level/Vulkan-bearing header just for one constant).
    static constexpr std::uint32_t kMaxColorAttachments = 8;

    VertexLayout vertexLayout = VertexLayout::PositionNormalUv;
    BlendMode blendMode = BlendMode::Opaque;
    CullMode cullMode = CullMode::Back;
    Topology topology = Topology::TriangleList;
    CompareOp depthCompare = CompareOp::Less;
    bool depthTest = true;
    bool depthWrite = true;

    // Owned by value (never a span into a temporary) so a PipelineDesc
    // returned by value from a PipelinePreset factory below is always
    // self-contained - zero dangling pointers. colorAttachmentCount == 0
    // means no color attachment at all (a depth-only pipeline).
    std::array<PixelFormat, kMaxColorAttachments> colorAttachmentFormats{};
    std::uint32_t colorAttachmentCount = 0;

    PixelFormat depthAttachmentFormat = PixelFormat::Undefined;
    PipelineLayoutDesc layout = {};
    const char* debugName = nullptr;

    // Read-only view for Renderer::CreatePipeline()'s own internal use.
    constexpr std::span<const PixelFormat> ColorFormats() const noexcept
    {
        return std::span<const PixelFormat>(colorAttachmentFormats.data(), colorAttachmentCount);
    }
};

// Named presets cover every real hand-built-pipeline case in this engine -
// no caller should hand-fill PipelineDesc field by field for a common case.
namespace PipelinePreset {

constexpr PipelineDesc Opaque(PixelFormat color, PixelFormat depth) noexcept
{
    PipelineDesc desc{};
    desc.blendMode = BlendMode::Opaque;
    desc.depthTest = true;
    desc.depthWrite = true;
    desc.depthCompare = CompareOp::Less;
    if (color != PixelFormat::Undefined) {
        desc.colorAttachmentFormats[0] = color;
        desc.colorAttachmentCount = 1;
    }
    desc.depthAttachmentFormat = depth;
    return desc;
}

constexpr PipelineDesc AlphaBlend(PixelFormat color, PixelFormat depth) noexcept
{
    PipelineDesc desc = Opaque(color, depth);
    desc.blendMode = BlendMode::AlphaBlend;
    desc.depthWrite = false;
    return desc;
}

constexpr PipelineDesc DepthEqualNoWrite(PixelFormat color, PixelFormat depth) noexcept
{
    PipelineDesc desc = Opaque(color, depth);
    desc.depthCompare = CompareOp::Equal;
    desc.depthWrite = false;
    return desc;
}

} // namespace PipelinePreset

} // namespace gte
