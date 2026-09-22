#include "GBufferValidation.h"

#include "../Renderer/ComputeDispatch.h"
#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Renderer/Vertex.h"
#include "../Renderer/Vulkan/DescriptorSetLayoutBuilder.h"

#include <array>
#include <cstdint>
#include <vector>

namespace gte {

namespace {

// task_manager/mrt-1 campaign, PHASE4 - color format every one of this
// class's three outputs is created with (mirrors ComputeBlurValidation's
// own explicit VK_FORMAT_R8G8B8A8_UNORM choice - see GBufferValidation.h's
// own header comment for the full reasoning: none of these textures are
// ever bound to the SAME Pipeline as the swapchain/Game/Scene views, so
// there is no reason to inherit the swapchain's own negotiated format).
constexpr VkFormat kGBufferFormat = VK_FORMAT_R8G8B8A8_UNORM;

// A plain, arbitrary clear depth for the pass's own scratch/unused depth
// attachment - never actually read back by anything (see this file's own
// header comment on why a real depth attachment is required at all here).
constexpr float kGBufferScratchClearDepth = 1.0f;

// MUST match Shaders/GBufferCopy.comp's own `layout(local_size_x = 16,
// local_size_y = 16) in;` exactly - see ComputeDispatch.h's own header
// comment on why this pairing is a hand-maintained, per-shader convention
// rather than something the build system enforces.
constexpr std::uint32_t kGBufferCopyLocalSizeX = 16;
constexpr std::uint32_t kGBufferCopyLocalSizeY = 16;

} // namespace

GBufferValidation::~GBufferValidation()
{
    // m_*Output/m_gbufferPipeline/m_copyPipeline/m_dummyTriangle are RAII
    // types and clean up themselves; m_copyDescriptorSetLayout is a plain
    // Vulkan handle this class owns directly (mirrors
    // ComputeBlurValidation::~ComputeBlurValidation()'s own identical
    // pattern). Safe to call unconditionally - the device is already idle
    // by this point (ImGuiEditorLayer's own destructor calls
    // vkDeviceWaitIdle() before any of its members, including this one,
    // are destroyed).
    if (m_copyDescriptorSetLayout != VK_NULL_HANDLE) {
        vkDestroyDescriptorSetLayout(m_device, m_copyDescriptorSetLayout, nullptr);
    }
}

void GBufferValidation::EnsureInitialized(Renderer& renderer, VkExtent2D initialExtent)
{
    if (m_gbufferPipeline.has_value()) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    const int width = initialExtent.width > 0 ? static_cast<int>(initialExtent.width) : 1;
    const int height = initialExtent.height > 0 ? static_cast<int>(initialExtent.height) : 1;

    m_albedoOutput.emplace(
        renderer.CreateRenderTexture(width, height, kGBufferFormat, "GBufferAlbedo", "GBufferAlbedoDepth"));
    m_normalOutput.emplace(
        renderer.CreateRenderTexture(width, height, kGBufferFormat, "GBufferNormal", "GBufferNormalDepth"));
    m_visualizedOutput.emplace(renderer.CreateRenderTexture(width, height, kGBufferFormat, "GBufferVisualized",
        "GBufferVisualizedDepth", /*allowStorageImageAccess=*/true));

    // PHASE3's new N-format Renderer::CreatePipeline() overload - this
    // pass's own real, first-ever consumer.
    const std::array<VkFormat, 2> colorFormats{ kGBufferFormat, kGBufferFormat };
    m_gbufferPipeline.emplace(renderer.CreatePipeline(colorFormats, "shaders/GBufferValidation.vert.spv",
        "shaders/GBufferValidation.frag.spv", VertexLayout::PositionColor,
        /*useMaterialTexture=*/false, "GBufferValidation.vert/.frag (outAlbedo/outNormal)"));

    // See this class's own header comment - a real, but throwaway/unused,
    // 3-vertex Mesh, purely so Pipeline's mandatory vertex binding (see
    // Pipeline.h) has SOMETHING real bound at draw time. Values are never
    // read by Shaders/GBufferValidation.vert (a pure gl_VertexIndex
    // full-screen triangle, mirroring AtmosphereSkyBackground.vert).
    const Vertex dummyVertices[3] = {
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
        { { 0.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 0.0f } },
    };
    m_dummyTriangle.emplace(
        renderer.CreateMesh(dummyVertices, sizeof(dummyVertices), 3, "GBufferValidationDummyTriangle"));

    // The second pass's own compute copy pipeline - binding 0 is the
    // read-only GBufferAlbedo input, binding 1 is the RWTexture
    // GBufferVisualized output - MUST match Shaders/GBufferCopy.comp's own
    // layout(binding = ...) declarations exactly (see
    // Vulkan/DescriptorSetLayoutBuilder.h's own binding-number convention).
    DescriptorSetLayoutBuilder layoutBuilder(m_device);
    m_copyDescriptorSetLayout =
        layoutBuilder.AddCombinedImageSampler(/*binding=*/0).AddStorageImage(/*binding=*/1).Build();

    // Plain, per-shader push-constant convention (see ComputePipeline.h) -
    // two uint32s (width, height), matching Shaders/GBufferCopy.comp's own
    // PushConstants block exactly.
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(std::uint32_t) * 2;

    m_copyPipeline.emplace(renderer.CreateComputePipeline("shaders/GBufferCopy.comp.spv",
        std::vector<VkDescriptorSetLayout>{ m_copyDescriptorSetLayout }, pushConstantRange));

    m_copyDescriptorSet = ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_copyDescriptorSetLayout));
}

GBufferValidationHandles GBufferValidation::AddPass(
    rg::RenderGraphBuilder& builder, Renderer& renderer, VkExtent2D sceneExtent)
{
    EnsureInitialized(renderer, sceneExtent);

    const VkExtent2D currentExtent = m_albedoOutput->Extent();
    if (currentExtent.width != sceneExtent.width || currentExtent.height != sceneExtent.height) {
        // Resizes are rare/user-driven (dragging the "Scene" panel's
        // border) - a full device stall here is the simplest correct
        // thing, mirroring ComputeBlurValidation::AddPass()'s own
        // identical discipline.
        vkDeviceWaitIdle(m_device);
        m_albedoOutput->Resize(static_cast<int>(sceneExtent.width), static_cast<int>(sceneExtent.height));
        m_normalOutput->Resize(static_cast<int>(sceneExtent.width), static_cast<int>(sceneExtent.height));
        m_visualizedOutput->Resize(static_cast<int>(sceneExtent.width), static_cast<int>(sceneExtent.height));
    }

    // Always imported as VK_IMAGE_LAYOUT_UNDEFINED - mirrors
    // ComputeBlurValidation::AddPass()'s own identical reasoning: every one
    // of this pass's own writes fully overwrites every in-bounds pixel
    // every time it runs, so the previous frame's actual contents/layout
    // never need to be preserved.
    const rg::TextureHandle albedoHandle =
        builder.ImportTexture("GBufferAlbedo", m_albedoOutput->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
    const rg::TextureHandle normalHandle =
        builder.ImportTexture("GBufferNormal", m_normalOutput->Target(), VK_IMAGE_LAYOUT_UNDEFINED);
    const rg::TextureHandle visualizedHandle =
        builder.ImportTexture("GBufferVisualized", m_visualizedOutput->Target(), VK_IMAGE_LAYOUT_UNDEFINED);

    // The MRT graphics pass itself - PHASE1-3's own mechanism's first real
    // consumer. Reuses m_albedoOutput's own companion DepthBuffer (every
    // RenderTexture already owns one - see RenderTexture.h) as this pass's
    // real (scratch/unused) depth attachment, mirroring
    // AddRenderOpaquePass()'s own exact "one shared TextureHandle used for
    // both a color write AND the depth write" pattern
    // (src/Application/RenderPasses.cpp).
    builder.AddRenderPass(
        "GBufferValidation", rg::PassKind::Graphics, rg::ViewScope::SceneView, rg::RenderPassCategory::Debug,
        [albedoHandle, normalHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.WriteColorAttachment(albedoHandle, std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f });
            pass.WriteColorAttachment(normalHandle, std::array<float, 4>{ 0.0f, 0.0f, 0.0f, 1.0f });
            pass.WriteDepthStencilAttachment(albedoHandle, kGBufferScratchClearDepth);
        },
        [this, &renderer](rg::PassContext& ctx) {
            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            vkCmdBindPipeline(ctx.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, m_gbufferPipeline->Native());
            const VkBuffer vertexBuffer = m_dummyTriangle->VertexBuffer();
            const VkDeviceSize offset = 0;
            vkCmdBindVertexBuffers(ctx.cmd, 0, 1, &vertexBuffer, &offset);
            vkCmdDraw(ctx.cmd, 3, 1, 0, 0);
            renderer.EndGraphPassRecording();
        },
        rg::RenderPassDrawKind::DrawQuad);
    // Default RenderPassEvent::Opaques (last parameter, left at its
    // default above) is safe here since, unlike ComputeBlurValidation,
    // this pass reads no Scene-View texture at all (see this class's own
    // header comment) - there is no cross-pass ordering hazard a future
    // render-pass-4-style effective-order change could ever expose.

    // The second, small compute pass - proves "a later pass reading one of
    // N MRT outputs, cross-pass, barrier-synchronized automatically by the
    // existing RenderGraphBarrierPlanner, with zero new barrier code". A
    // trivial imageLoad/imageStore copy (Shaders/GBufferCopy.comp) -
    // mirrors ComputeBlurValidation's own compute-pass shape almost
    // verbatim.
    builder.AddRenderPass(
        "GBufferValidationCopy", rg::PassKind::Compute, rg::ViewScope::SceneView, rg::RenderPassCategory::Debug,
        [albedoHandle, visualizedHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(albedoHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(visualizedHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        [this, &renderer, albedoHandle, visualizedHandle, sceneExtent](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture source = ctx.resolveTexture(albedoHandle);
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(visualizedHandle);

            m_copyDescriptorSet.Rewrite(m_device,
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::CombinedImageSampler(0, source.view, m_albedoOutput->Sampler()),
                    ComputeDescriptorWrite::StorageImage(1, dest.view),
                });

            const std::uint32_t pushConstants[2] = { sceneExtent.width, sceneExtent.height };
            const Extent3D groupCounts = ComputeGroupCount3D(Extent3D{ sceneExtent.width, sceneExtent.height, 1 },
                Extent3D{ kGBufferCopyLocalSizeX, kGBufferCopyLocalSizeY, 1 });

            renderer.BeginGraphPassRecording(ctx.cmd, ctx.recordDraw);
            renderer.Dispatch(*m_copyPipeline, m_copyDescriptorSet.Native(), pushConstants, sizeof(pushConstants),
                groupCounts.width, groupCounts.height, groupCounts.depth);
            renderer.EndGraphPassRecording();
        });
    // Default drawKind (unused/meaningless for a Compute-kind pass) and
    // default RenderPassEvent::Opaques - this pass's own real dependency
    // on "GBufferValidation" having already written albedoHandle is a
    // structural RAW resource dependency the compiler already tracks
    // (exactly like every other pass in this engine), not something a
    // RenderPassEvent tag needs to additionally encode - both passes are
    // also always declared back-to-back, in this fixed order, every frame.

    m_writtenThisFrame = true;
    return GBufferValidationHandles{ albedoHandle, normalHandle, visualizedHandle };
}

void GBufferValidation::FinalizeForSampling(VkCommandBuffer cmd)
{
    if (!m_writtenThisFrame) {
        return;
    }
    m_writtenThisFrame = false;

    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };

    // albedo/normal: written by a real graphics pass (ColorAttachmentWrite)
    // - mirrors RenderPasses.h's own
    // FinalizeRenderTextureForExternalSampling().
    {
        const rg::ResourceState previous = rg::RequiredStateFor(rg::ResourceAccess::ColorAttachmentWrite, false);
        const rg::ResourceState next = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
        rg::EmitImageBarrier(cmd, m_albedoOutput->Image(), range, previous, next);
        rg::EmitImageBarrier(cmd, m_normalOutput->Image(), range, previous, next);
    }

    // visualized: written by the compute copy pass (ComputeShaderWrite) -
    // mirrors ComputeBlurValidation::FinalizeForSampling() exactly.
    {
        const rg::ResourceState previous = rg::RequiredStateFor(rg::ResourceAccess::ComputeShaderWrite, false);
        const rg::ResourceState next = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
        rg::EmitImageBarrier(cmd, m_visualizedOutput->Image(), range, previous, next);
    }
}

} // namespace gte
