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

// RAII owner of a VkPipeline and its layout for dynamic rendering; it only draws into targets of the exact
// formats it was built for. Create through Renderer::CreatePipeline(). Depth test and write are always on (LESS).
// Push constants: "model" then "viewProj" matrices (vertex stage, offset 0, 128 bytes).
class Pipeline {
public:
    // The SPIR-V paths point at compiled binaries in "<exe dir>/shaders/*.spv", not GLSL source.
    // `debugName` is a cosmetic label, never part of a cache key.
    // `sceneServicesSetLayout` (set 1) must be Core's SceneServicesDescriptorSet::Layout().

    // `materialSetLayout` (set 0): only for PositionNormalUv, must be GpuResourceFactory::MaterialDescriptorSetLayout().
    // `instanceBufferSetLayout` (set 0): only for PositionNormalInstanced, never combined with a material set.

    // When set 0 would be empty but set 1 exists, the pipeline owns a zero-binding filler (no gap allowed).
    // This single-format constructor forwards to the span constructor below.
    Pipeline(VkDevice device, VkFormat colorFormat, VkFormat depthFormat, const std::string& vertexShaderSpirvPath,
        const std::string& fragmentShaderSpirvPath, VertexLayout vertexLayout = VertexLayout::PositionColor,
        VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE, const char* debugName = nullptr,
        VkDescriptorSetLayout instanceBufferSetLayout = VK_NULL_HANDLE,
        VkDescriptorSetLayout sceneServicesSetLayout = VK_NULL_HANDLE);

    // Multi-render-target form: one opaque, full-write blend state per entry. `colorFormats` must be non-empty
    // and at most kPipelineMaxColorAttachments. Other parameters behave as above.
    Pipeline(VkDevice device, std::span<const VkFormat> colorFormats, VkFormat depthFormat,
        const std::string& vertexShaderSpirvPath, const std::string& fragmentShaderSpirvPath,
        VertexLayout vertexLayout = VertexLayout::PositionColor,
        VkDescriptorSetLayout materialSetLayout = VK_NULL_HANDLE, const char* debugName = nullptr,
        VkDescriptorSetLayout instanceBufferSetLayout = VK_NULL_HANDLE,
        VkDescriptorSetLayout sceneServicesSetLayout = VK_NULL_HANDLE);

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

private:
    void Destroy() noexcept;

    VkDevice m_device = VK_NULL_HANDLE;
    VkPipelineLayout m_layout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;
    std::string m_debugName;
    VertexLayout m_vertexLayout = VertexLayout::PositionColor;

    bool m_hasSceneServicesSet = false;
    bool m_hasMaterialSet = false;
    // Owned only when a zero-binding filler was needed at set 0; VK_NULL_HANDLE otherwise.
    VkDescriptorSetLayout m_syntheticSetZeroLayout = VK_NULL_HANDLE;
};

} // namespace gte
