#include "PluginRenderOperationRegistry.h"

#include "../../Renderer/Renderer.h"
#include "../../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <cassert>
#include <utility>

namespace gte {

PluginRenderOperationRegistry::PluginRenderOperationRegistry(Renderer& renderer)
    : m_renderer(renderer)
{
}

void PluginRenderOperationRegistry::InsertOp(PluginRenderOpInfo info)
{
    assert(info.slots.size() <= 8
        && "PluginRenderOperationRegistry::InsertOp: a registered operation's own slot table must never exceed 8 "
           "entries - IPluginCommandRecorder::BindTexture()/BindBuffer() bind into a fixed 8-entry scratch array "
           "(PluginRenderPassBuilderAdapter_v3).");
    const std::string id = info.id;
    m_ops.emplace(id, std::move(info));
}

void PluginRenderOperationRegistry::RegisterUberOp(const char* id, std::uint32_t opCode)
{
    PluginRenderOpInfo info;
    info.id = id;
    info.kind = PluginRenderOpKind::Compute;
    info.slots = { PluginRenderOpSlot{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, false } };
    info.opCode = opCode;
    info.maxParamBytes = sizeof(RenderFeatureOpsPushConstants);
    info.computePipeline = &(*m_opsPipeline);
    info.descriptorSetLayout = m_opsDescriptorSetLayout;
    InsertOp(std::move(info));
}

void PluginRenderOperationRegistry::RegisterBoxBlur()
{
    // Binding convention (matches Shaders/BoxBlur.comp/ComputeBlurValidation.cpp
    // exactly): binding 0 = read-only sampler2D sourceTexture (CombinedImageSampler),
    // binding 1 = writeonly rgba8 image2D destinationImage (StorageImage), a
    // (width, height) uint32 push-constant pair (8 bytes).
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_boxBlurDescriptorSetLayout =
        layoutBuilder.AddCombinedImageSampler(/*binding=*/0).AddStorageImage(/*binding=*/1).Build();

    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(std::uint32_t) * 2;

    m_boxBlurPipeline.emplace(m_renderer.CreateComputePipeline("shaders/BoxBlur.comp.spv",
        std::vector<VkDescriptorSetLayout>{ m_boxBlurDescriptorSetLayout }, pushConstantRange));

    PluginRenderOpInfo info;
    info.id = "gte.builtin.box_blur";
    info.kind = PluginRenderOpKind::Compute;
    info.slots = {
        PluginRenderOpSlot{ VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, false },
        PluginRenderOpSlot{ VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, false },
    };
    info.opCode = 0; // unused - gte.builtin.box_blur has its own dedicated pipeline, never the shared uber-ops one.
    info.maxParamBytes = sizeof(std::uint32_t) * 2;
    info.computePipeline = &(*m_boxBlurPipeline);
    info.descriptorSetLayout = m_boxBlurDescriptorSetLayout;
    InsertOp(std::move(info));
}

void PluginRenderOperationRegistry::EnsureBuiltinsRegistered()
{
    if (m_builtinsRegistered) {
        return;
    }
    m_builtinsRegistered = true;

    const Renderer::VulkanContextInfo context = m_renderer.GetVulkanContextInfo();
    m_device = context.device;

    // --- The shared "uber ops" pipeline (RenderFeatureOps.comp) - migrated
    // verbatim from RenderFeatureCompositor::EnsureOpsInitialized() (this
    // phase's own Step 3.2) - byte-for-byte identical construction, just
    // relocated. Binding convention (matches Shaders/RenderFeatureOps.comp
    // exactly): binding 0 = privateTarget, the ONLY binding - a single
    // read-write storage image. ---
    DescriptorSetLayoutBuilder opsLayoutBuilder(m_device);
    m_opsDescriptorSetLayout = opsLayoutBuilder.AddStorageImage(/*binding=*/0).Build();

    VkPushConstantRange opsPushConstantRange{};
    opsPushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    opsPushConstantRange.offset = 0;
    opsPushConstantRange.size = sizeof(RenderFeatureOpsPushConstants);

    m_opsPipeline.emplace(m_renderer.CreateComputePipeline("shaders/RenderFeatureOps.comp.spv",
        std::vector<VkDescriptorSetLayout>{ m_opsDescriptorSetLayout }, opsPushConstantRange));

    RegisterUberOp("gte.builtin.solid_fill", /*opCode=*/0);
    RegisterUberOp("gte.builtin.radial_vignette", /*opCode=*/1);
    RegisterUberOp("gte.builtin.color_grade", /*opCode=*/2);

    // --- The shared blend pipeline (RenderFeatureBlend.comp) - migrated
    // verbatim from RenderFeatureCompositor::EnsureBlendPipelineInitialized() -
    // NOT itself a PluginRenderOpInfo entry (no plugin ever Dispatch()es this
    // directly - it is purely RenderFeatureCompositor's own internal
    // compositing mechanism, unreachable from any _v3 opId). Binding
    // convention (matches Shaders/RenderFeatureBlend.comp exactly): binding 0
    // = dstIn, binding 1 = srcIn (both read-only combined image samplers),
    // binding 2 = destinationImage (a write-only storage image). ---
    DescriptorSetLayoutBuilder blendLayoutBuilder(m_device);
    m_blendDescriptorSetLayout = blendLayoutBuilder.AddCombinedImageSampler(/*binding=*/0)
                                      .AddCombinedImageSampler(/*binding=*/1)
                                      .AddStorageImage(/*binding=*/2)
                                      .Build();

    VkPushConstantRange blendPushConstantRange{};
    blendPushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    blendPushConstantRange.offset = 0;
    blendPushConstantRange.size = sizeof(RenderFeatureBlendPushConstants);

    m_blendPipeline.emplace(m_renderer.CreateComputePipeline("shaders/RenderFeatureBlend.comp.spv",
        std::vector<VkDescriptorSetLayout>{ m_blendDescriptorSetLayout }, blendPushConstantRange));

    // --- gte.builtin.box_blur - the genuinely NEW operation this phase adds
    // (Design Doc R13's central proof), reusing the ALREADY-EXISTING,
    // ALREADY-SHIPPED shaders/BoxBlur.comp.spv verbatim - its own, separate
    // pipeline/layout, with zero IPluginRenderPassBuilder_v3 interface
    // change. ---
    RegisterBoxBlur();
}

const PluginRenderOpInfo* PluginRenderOperationRegistry::Find(const std::string& id) const noexcept
{
    const auto it = m_ops.find(id);
    return it == m_ops.end() ? nullptr : &it->second;
}

const ComputePipeline& PluginRenderOperationRegistry::OpsPipeline() const noexcept
{
    assert(m_opsPipeline.has_value() && "PluginRenderOperationRegistry::OpsPipeline: EnsureBuiltinsRegistered() was never called");
    return *m_opsPipeline;
}

VkDescriptorSetLayout PluginRenderOperationRegistry::OpsDescriptorSetLayout() const noexcept
{
    return m_opsDescriptorSetLayout;
}

const ComputePipeline& PluginRenderOperationRegistry::BlendPipeline() const noexcept
{
    assert(m_blendPipeline.has_value() && "PluginRenderOperationRegistry::BlendPipeline: EnsureBuiltinsRegistered() was never called");
    return *m_blendPipeline;
}

VkDescriptorSetLayout PluginRenderOperationRegistry::BlendDescriptorSetLayout() const noexcept
{
    return m_blendDescriptorSetLayout;
}

} // namespace gte
