#pragma once

#include <volk.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include "VertexLayout.h"

namespace gte {

// Local copy of rg::kMaxColorAttachments: Pipeline is a lower-level primitive and must not include the graph.
// Keep the two values equal.
inline constexpr std::size_t kPipelineMaxColorAttachments = 8;

// Blend state with blending off and a full RGBA write mask.
inline VkPipelineColorBlendAttachmentState OpaqueBlendAttachment() noexcept
{
    VkPipelineColorBlendAttachmentState state{};
    state.blendEnable = VK_FALSE;
    state.colorWriteMask =
        VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    return state;
}

// Fixed-function state of the path-based pipeline form. Defaults: no cull, LESS, depth write on, opaque.
struct PipelineFixedState {
    VkCullModeFlags cullMode = VK_CULL_MODE_NONE;
    VkCompareOp depthCompare = VK_COMPARE_OP_LESS;
    bool depthWrite = true;
    VkPipelineColorBlendAttachmentState blend = OpaqueBlendAttachment();
};

// RAII owner of a VkPipeline and its layout for dynamic rendering; it only draws into targets of the exact
// formats it was built for. Create through Renderer::CreatePipeline(). Depth test is always on.
// Push constants: "model" then "viewProj" matrices (vertex stage, offset 0, 128 bytes).
class Pipeline {
public:
    // Single-format form; forwards to the span constructor. SPIR-V paths are "<exe dir>/shaders/*.spv".
    // Sets: 0 material, 1 scene services, 2 buffers; unused sets below the highest used one get a filler.
    Pipeline(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
        VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE, const char* debugName = nullptr,
        VkDescriptorSetLayout bufferSetLayout = VK_NULL_HANDLE,
        VkDescriptorSetLayout sceneServicesSetLayout = VK_NULL_HANDLE);

    // Multi-render-target form: the same blend state on every entry. `colorFormats` may be empty (depth-only)
    // and at most kPipelineMaxColorAttachments. Opaque, depth LESS + write; `cullMode` defaults to none.
    Pipeline(VkDevice device, std::span<const VkFormat> colorFormats, VkFormat depthFormat,
        const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath,
        VertexLayout vertexLayout = VertexLayout::PositionColor,
        VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE, const char* debugName = nullptr,
        VkDescriptorSetLayout bufferSetLayout = VK_NULL_HANDLE,
        VkDescriptorSetLayout sceneServicesSetLayout = VK_NULL_HANDLE,
        VkCullModeFlags cullMode = VK_CULL_MODE_NONE);

    // Path-based form with caller-chosen cull, depth compare/write and blend. Other parameters as above.
    Pipeline(VkDevice device, std::span<const VkFormat> colorFormats, VkFormat depthFormat,
        const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath,
        VertexLayout vertexLayout, VkDescriptorSetLayout materialSetLayout, const char* debugName,
        VkDescriptorSetLayout bufferSetLayout, VkDescriptorSetLayout sceneServicesSetLayout,
        const PipelineFixedState& fixedState);

    // Configurable form built from in-memory SPIR-V with caller-resolved blend, cull, compare, topology, extra
    // set layouts and an optional push-constant range. HasSceneServicesSet()/HasMaterialSet() stay false: the
    // pass binds its own sets through CommandBuffer.
    Pipeline(VkDevice device, std::span<const VkFormat> colorFormats, VkFormat depthFormat,
        std::span<const std::byte> vertexSpirv, std::span<const std::byte> fragmentSpirv, VertexLayout vertexLayout,
        VkPrimitiveTopology topology, VkCullModeFlags cullMode, bool depthTest, bool depthWrite,
        VkCompareOp depthCompare, VkPipelineColorBlendAttachmentState blendState,
        std::span<const VkDescriptorSetLayout> setLayouts, VkShaderStageFlags pushConstantStages,
        std::uint32_t pushConstantByteSize, const char* debugName = nullptr);

    ~Pipeline();

    Pipeline(const Pipeline&) = delete;
    Pipeline& operator=(const Pipeline&) = delete;

    Pipeline(Pipeline&& other) noexcept;
    Pipeline& operator=(Pipeline&& other) noexcept;

    VkPipeline Native() const noexcept { return m_pipeline; }
    VkPipelineLayout Layout() const noexcept { return m_layout; }

    // Empty when built without a `debugName`.
    const std::string& DebugName() const noexcept { return m_debugName; }

    // The vertex layout this pipeline was built with.
    VertexLayout VertexLayoutKind() const noexcept { return m_vertexLayout; }

    // True when the layout carries a real set 1 (a non-null `sceneServicesSetLayout`).
    bool HasSceneServicesSet() const noexcept { return m_hasSceneServicesSet; }

    // True when the layout declares a material texture at set 0; false for depth-only pipelines, so Submit()
    // can drop a material set instead of binding it against a layout without set 0.
    bool HasMaterialSet() const noexcept { return m_hasMaterialSet; }

    // True when set 0 is a zero-binding filler; draws must bind the renderer's empty set there.
    bool HasSyntheticSetZero() const noexcept { return m_fillerSetLayout != VK_NULL_HANDLE && !m_hasMaterialSet; }

private:
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::string m_debugName;
    VertexLayout m_vertexLayout = VertexLayout::PositionColor;

    bool m_hasSceneServicesSet = false;
    bool m_hasMaterialSet = false;
    // Zero-binding layout filling the unused sets below the highest used one; VK_NULL_HANDLE when no gap exists.
    VkDescriptorSetLayout m_fillerSetLayout = VK_NULL_HANDLE;
};

} // namespace gte
