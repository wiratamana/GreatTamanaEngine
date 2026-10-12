#include "ShadowCompositeRenderer.h"

#include "../../Core/PipelineDesc.h"
#include "../../Renderer/ComputeDescriptorSet.h"
#include "../../Renderer/Renderer.h"
#include "../../Renderer/RenderGraph/RenderGraphBuilder.h"
#include "../../Renderer/RenderGraph/RenderGraph.h"
#include "../../Renderer/RenderGraph/RenderFeatureScope.h"

#include <cstring>
#include <fstream>
#include <stdexcept>
#include <vector>

namespace gte {

namespace {

std::vector<std::byte> ReadShaderFileBytes(const std::string& path)
{
    std::ifstream file(path, std::ios::ate | std::ios::binary);
    if (!file.is_open()) {
        throw std::runtime_error("ShadowCompositeRenderer: failed to open shader file '" + path + "'.");
    }
    const std::size_t size = static_cast<std::size_t>(file.tellg());
    std::vector<std::byte> buffer(size);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(buffer.data()), static_cast<std::streamsize>(size));
    return buffer;
}

// Must match Shaders/ShadowComposite.frag's push-constant block exactly (4 bytes).
struct PushConstants {
    float strength;
};
} // namespace

ShadowCompositeRenderer::~ShadowCompositeRenderer()
{
    Reset();
}

void ShadowCompositeRenderer::Reset()
{
    if (m_renderer != nullptr && m_pipeline) {
        m_renderer->WaitForGpuIdle();
    }
    if (m_renderer) {
        m_renderer->DestroyDescriptorSetLayout(m_compositeSetLayoutHandle);
    }
    m_compositeSetLayoutHandle = DescriptorSetLayoutHandle::Invalid;
    m_pipeline.reset();
    m_descriptorSetsByView.clear(); // Every entry is pool-owned - never individually freed.
}

void ShadowCompositeRenderer::EnsurePipeline(Renderer& renderer)
{
    if (m_pipeline) {
        return;
    }
    m_renderer = &renderer;

    // Opaque-handle layout (binding 0 = sceneColor, binding 1 = shadowMask,
    // fragment stage - see ShadowComposite.frag) - this SAME layout handle
    // is used both to allocate every per-view descriptor set
    // (EnsureViewDescriptorSet()) and to build the pipeline's own layout,
    // so there is exactly one definition of this binding set, never a
    // hand-built duplicate.
    constexpr DescriptorBindingDesc kCompositeBindings[] = {
        { .binding = 0, .kind = DescriptorKind::CombinedImageSampler, .stages = ShaderStageMask::Fragment },
        { .binding = 1, .kind = DescriptorKind::CombinedImageSampler, .stages = ShaderStageMask::Fragment },
    };
    m_compositeSetLayoutHandle = renderer.CreateDescriptorSetLayout(kCompositeBindings);

    // Color-only private target, no depth attachment - screen-space pass
    // into its own private accumulation target. No vertex input either -
    // ShadowComposite.vert synthesizes 3 triangle vertices from gl_VertexIndex.
    PipelineDesc desc = PipelinePreset::Opaque(renderer.ColorPixelFormat(), PixelFormat::Undefined);
    desc.vertexLayout = VertexLayout::None;
    desc.depthTest = false;
    desc.depthWrite = false;
    desc.layout.extraSetLayouts = std::span{ &m_compositeSetLayoutHandle, 1 };
    desc.layout.pushConstantByteSize = sizeof(PushConstants);
    desc.layout.pushConstantStages = ShaderStageMask::Fragment;

    const std::vector<std::byte> vertSpirv = ReadShaderFileBytes("shaders/ShadowComposite.vert.spv");
    const std::vector<std::byte> fragSpirv = ReadShaderFileBytes("shaders/ShadowComposite.frag.spv");
    m_pipeline = std::make_unique<Pipeline>(renderer.CreatePipeline(desc, vertSpirv, fragSpirv));
}

VkDescriptorSet ShadowCompositeRenderer::EnsureViewDescriptorSet(Renderer& renderer, const char* viewKey)
{
    const auto existing = m_descriptorSetsByView.find(viewKey);
    if (existing != m_descriptorSetsByView.end()) {
        return existing->second;
    }
    const VkDescriptorSet descriptorSet = renderer.AllocateComputeDescriptorSet(m_compositeSetLayoutHandle);
    return m_descriptorSetsByView.emplace(viewKey, descriptorSet).first->second;
}

void ShadowCompositeRenderer::AddCompositePass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    const char* viewKey, rg::TextureHandle privateTarget, VkExtent2D extent, rg::TextureHandle sceneColorHandle,
    VkSampler sceneColorSampler, rg::TextureHandle maskHandle, rg::TextureHandle sceneDepthHandle, float strength)
{
    const rg::RenderFeatureScope scope(builder, "Shadow");
    EnsurePipeline(renderer);
    const VkDescriptorSet descriptorSet = EnsureViewDescriptorSet(renderer, viewKey);

    builder.AddRenderPass(
        "Shadow.Composite.Draw", rg::PassKind::Graphics, rg::ViewScope::Shared, rg::RenderPassCategory::General,
        [privateTarget, sceneColorHandle, maskHandle, sceneDepthHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(sceneColorHandle, rg::ResourceAccess::ShaderRead);
            pass.ReadTexture(maskHandle, rg::ResourceAccess::ShaderRead);
            // Declare-only: this pass never samples scene depth, but
            // RenderFeatureCompositor requires every PostComposite feature to
            // acknowledge the view's depth sub-resource it was handed.
            pass.ReadTexture(sceneDepthHandle, rg::ResourceAccess::ShaderRead, /*isDepthResource=*/true);
            pass.WriteColorAttachment(privateTarget);
        },
        [this, descriptorSet, sceneColorHandle, sceneColorSampler, maskHandle, extent, strength](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture sceneColor = ctx.resolveTexture(sceneColorHandle);
            const rg::PassContext::ResolvedTexture mask = ctx.resolveTexture(maskHandle);

            ComputeDescriptorSet wrapped(descriptorSet);
            wrapped.Rewrite(m_renderer->GetVulkanContextInfo().device,
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::CombinedImageSampler(0, sceneColor.view, sceneColorSampler),
                    ComputeDescriptorWrite::CombinedImageSampler(1, mask.view, mask.sampler),
                });

            PushConstants pushConstants{};
            pushConstants.strength = strength;

            auto cmd = ctx.Cmd();
            cmd.BindPipeline(*m_pipeline);
            cmd.SetViewportAndScissor(extent.width, extent.height);
            cmd.BindDescriptorSet(/*setIndex=*/0, descriptorSet);
            cmd.PushConstants(pushConstants, ShaderStageMask::Fragment);
            cmd.Draw(/*vertexCount=*/3);
        },
        rg::RenderPassDrawKind::DrawQuad, rg::RenderPassEvent::AfterEverything);
}

} // namespace gte
