#include "GpuSkinningPipelines.h"

#include "../Renderer.h"
#include "../Vulkan/DescriptorSetLayoutBuilder.h"

#include <cstdint>

namespace gte {

GpuSkinningPipelines::~GpuSkinningPipelines()
{
    // m_positionNormalPipeline/m_positionNormalUvPipeline are RAII types
    // (ComputePipeline) and clean up themselves; the two descriptor-set
    // layouts are plain Vulkan handles this class owns directly - mirrors
    // ComputeBlurValidation's own destructor exactly (see that class's own
    // comment on why this is safe to call unconditionally: the device is
    // already idle by the time any owning object's members are destroyed).
    if (m_positionNormalLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_positionNormalLayout, nullptr);
    }
    if (m_positionNormalUvLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_positionNormalUvLayout, nullptr);
    }
}

void GpuSkinningPipelines::EnsureInitialized(Renderer& renderer)
{
    if (IsInitialized()) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Plain, per-shader push-constant convention (see ComputePipeline.h) -
    // a single uint32 (vertexCount), matching both .comp files' own
    // `PushConstants` block exactly.
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(std::uint32_t);

    // PositionNormal variant - bindings 0-3, per GpuSkinningTypes.h's own
    // documented table (see SkinVerticesPositionNormal.comp's own header
    // comment for the full per-binding reasoning).
    {
        DescriptorSetLayoutBuilder layoutBuilder(m_device);
        m_positionNormalLayout = layoutBuilder.AddStorageBuffer(/*binding=*/0) // bind pose
                                      .AddStorageBuffer(/*binding=*/1) // skin weights
                                      .AddStorageBuffer(/*binding=*/2) // bone matrices
                                      .AddStorageBuffer(/*binding=*/3) // output
                                      .Build();

        m_positionNormalPipeline.emplace(renderer.CreateComputePipeline("shaders/SkinVerticesPositionNormal.comp.spv",
            std::vector<VkDescriptorSetLayout>{ m_positionNormalLayout }, pushConstantRange));
    }

    // PositionNormalUv variant - bindings 0-4, adding the bind-pose UV
    // buffer at binding 4 (a Phase 2 addition on top of GpuSkinningTypes.h's
    // own 4-binding table - see SkinVerticesPositionNormalUv.comp's own
    // header comment for why this is additive, not a redefinition).
    {
        DescriptorSetLayoutBuilder layoutBuilder(m_device);
        m_positionNormalUvLayout = layoutBuilder.AddStorageBuffer(/*binding=*/0) // bind pose
                                        .AddStorageBuffer(/*binding=*/1) // skin weights
                                        .AddStorageBuffer(/*binding=*/2) // bone matrices
                                        .AddStorageBuffer(/*binding=*/3) // output
                                        .AddStorageBuffer(/*binding=*/4) // bind-pose UVs (Uv variant only)
                                        .Build();

        m_positionNormalUvPipeline.emplace(
            renderer.CreateComputePipeline("shaders/SkinVerticesPositionNormalUv.comp.spv",
                std::vector<VkDescriptorSetLayout>{ m_positionNormalUvLayout }, pushConstantRange));
    }
}

} // namespace gte
