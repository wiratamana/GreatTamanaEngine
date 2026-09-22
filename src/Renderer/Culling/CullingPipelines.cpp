#include "CullingPipelines.h"

#include "../Renderer.h"
#include "../Vulkan/DescriptorSetLayoutBuilder.h"

#include <cstdint>

namespace gte {

CullingPipelines::~CullingPipelines()
{
    // m_pipeline is a RAII type (ComputePipeline) and cleans itself up; the
    // descriptor-set layout is a plain Vulkan handle this class owns
    // directly - mirrors GpuSkinningPipelines::~GpuSkinningPipelines()
    // exactly (see that class's own comment on why this is safe to call
    // unconditionally: the device is already idle by the time any owning
    // object's members are destroyed).
    if (m_layout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_layout, nullptr);
    }
}

void CullingPipelines::EnsureInitialized(Renderer& renderer)
{
    if (IsInitialized()) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // Push-constant convention (see Shaders/FrustumCull.comp's own
    // PushConstants block): 6 vec4 planes + 2 uint = 104 bytes total,
    // matching kCullingPushConstantSize exactly.
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = kCullingPushConstantSize;

    // Bindings 0-2, per Shaders/FrustumCull.comp's own documented binding
    // table (per-instance input / output indirect-command array / atomic
    // visible-count buffer).
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_layout = layoutBuilder.AddStorageBuffer(/*binding=*/0) // per-instance input
                   .AddStorageBuffer(/*binding=*/1) // output indirect-command array
                   .AddStorageBuffer(/*binding=*/2) // atomic visible-count buffer
                   .Build();

    m_pipeline.emplace(renderer.CreateComputePipeline(
        "shaders/FrustumCull.comp.spv", std::vector<VkDescriptorSetLayout>{ m_layout }, pushConstantRange));
}

} // namespace gte
