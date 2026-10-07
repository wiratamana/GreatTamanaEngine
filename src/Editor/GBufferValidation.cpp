#include "GBufferValidation.h"

#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"
#include "../Renderer/RenderGraph/RenderPassToggleRegistry.h"
#include "../Renderer/Vertex.h"

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

} // namespace

GBufferValidation::~GBufferValidation()
{
    // m_*Output/m_gbufferPipeline/m_copyPipeline/m_dummyTriangle are RAII
    // types and clean up themselves. m_copyDescriptorSetLayout is now
    // BORROWED from m_copyPipeline's own ReflectedDescriptorSetLayout(0) -
    // owned and destroyed by m_copyPipeline itself (ComputePipeline::
    // Destroy()'s own m_ownedReflectedLayouts cleanup, PHASE2 of
    // task_manager/better-render-pass-1). Keeping the old
    // vkDestroyDescriptorSetLayout() call here would be a genuine
    // double-free the instant this destructor ran - see
    // task_manager/better-render-pass-1/PHASE4_COMPLETION_REPORT.md's own
    // identical fix for the first-ever precedent of this exact bug class.
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
    // pass's own real, first-ever consumer. This is the GRAPHICS pipeline
    // path (Pipeline, not ComputePipeline) - genuinely out of scope for this
    // whole campaign (see PHASE0_MASTER_STRATEGY.md's own Step 2.1/task doc's
    // Step 3 item 2 - "the graphics Pipeline/CreatePipeline() path is
    // untouched by this whole campaign") - left completely unmodified.
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

    // task_manager/better-render-pass-1 campaign, PHASE6 - the second pass's
    // own compute copy pipeline, now built via path-only, reflection-based
    // pipeline creation (PHASE2), replacing the old hand-built
    // DescriptorSetLayoutBuilder + manual VkPushConstantRange entirely.
    // Binding 0 (read-only GBufferAlbedo input) / binding 1 (RWTexture
    // GBufferVisualized output) and the 2x uint32 (width, height)
    // push-constant block are both now read directly from
    // Shaders/GBufferCopy.comp.spv's own compiled reflection data.
    m_copyPipeline.emplace(renderer.CreateComputePipeline("shaders/GBufferCopy.comp.spv"));
    m_copyDescriptorSetLayout = m_copyPipeline->ReflectedDescriptorSetLayout(/*set=*/0);

    m_copyDescriptorSet = ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_copyDescriptorSetLayout));
}

GBufferValidationHandles GBufferValidation::AddPass(
    rg::RenderGraphBuilder& builder, Renderer& renderer, VkExtent2D sceneExtent, rg::RenderPassToggleRegistry* toggleRegistry)
{
    // editor-core-separation-21 campaign, PHASE4 (fixing PHASE3's
    // confirmed-lie finding #22) - the graphics half's own toggle gates
    // BOTH passes: the compute half below has no valid `albedoHandle` to
    // read at all if this one never declares. Checked BEFORE
    // EnsureInitialized()/any resource creation, mirroring
    // AtmosphereLutRenderer's own five-method early-return precedent.
    const bool graphicsEnabled =
        toggleRegistry == nullptr || toggleRegistry->NoteDeclaredAndCheckEnabled("GBufferValidation");
    if (!graphicsEnabled) {
        return GBufferValidationHandles{};
    }

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
    const rg::TextureHandle albedoHandle = builder.ImportTexture("GBufferAlbedo", m_albedoOutput->Target(),
        VK_IMAGE_LAYOUT_UNDEFINED, m_albedoOutput->Sampler(), m_albedoOutput->DepthSampler(), &*m_albedoOutput);
    const rg::TextureHandle normalHandle = builder.ImportTexture("GBufferNormal", m_normalOutput->Target(),
        VK_IMAGE_LAYOUT_UNDEFINED, m_normalOutput->Sampler(), m_normalOutput->DepthSampler(), &*m_normalOutput);

    // The MRT graphics pass itself - PHASE1-3's own mechanism's first real
    // consumer. Reuses m_albedoOutput's own companion DepthBuffer (every
    // RenderTexture already owns one - see RenderTexture.h) as this pass's
    // real (scratch/unused) depth attachment, mirroring
    // AddRenderOpaquePass()'s own exact "one shared TextureHandle used for
    // both a color write AND the depth write" pattern
    // (src/Application/RenderPasses.cpp). This GRAPHICS pass's own dispatch
    // is untouched by this campaign (see EnsureInitialized()'s own comment
    // above) - still a plain renderer.BeginGraphPassRecording()/
    // vkCmdBindPipeline()/vkCmdDraw()/renderer.EndGraphPassRecording()
    // sequence, never rg::CommandBuffer (which this phase's own task doc
    // scopes to the COMPUTE half only - see PHASE6_MIGRATE_EDITOR_DEBUG_COMPUTE_TOOLING.md's
    // Step 3 item 2).
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
    m_graphicsWrittenThisFrame = true;

    // The second, small compute pass - proves "a later pass reading one of
    // N MRT outputs, cross-pass, barrier-synchronized automatically by the
    // existing RenderGraphBarrierPlanner, with zero new barrier code". A
    // trivial imageLoad/imageStore copy (Shaders/GBufferCopy.comp) -
    // mirrors ComputeBlurValidation's own compute-pass shape almost
    // verbatim - now migrated onto rg::CommandBuffer too (task_manager/
    // better-render-pass-1 campaign, PHASE6).
    //
    // editor-core-separation-21 campaign, PHASE4 (fixing PHASE3's
    // confirmed-lie finding #23) - INDEPENDENTLY gated from the graphics
    // half above: disabling ONLY "GBufferValidationCopy" still lets
    // "GBufferValidation" declare/write albedo/normal normally; only
    // `visualized` comes back invalid/unwritten this frame.
    rg::TextureHandle visualizedHandle{};
    const bool copyEnabled =
        toggleRegistry == nullptr || toggleRegistry->NoteDeclaredAndCheckEnabled("GBufferValidationCopy");
    if (copyEnabled) {
        visualizedHandle = builder.ImportTexture("GBufferVisualized", m_visualizedOutput->Target(),
            VK_IMAGE_LAYOUT_UNDEFINED, m_visualizedOutput->Sampler(), m_visualizedOutput->DepthSampler(),
            &*m_visualizedOutput);

        builder.AddRenderPass(
            "GBufferValidationCopy", rg::PassKind::Compute, rg::ViewScope::SceneView, rg::RenderPassCategory::Debug,
            [albedoHandle, visualizedHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
                pass.ReadTexture(albedoHandle, rg::ResourceAccess::ShaderRead);
                pass.WriteTexture(visualizedHandle, rg::ResourceAccess::ComputeShaderWrite);
            },
            [this, albedoHandle, visualizedHandle, sceneExtent](rg::PassContext& ctx) {
                const rg::PassContext::ResolvedTexture source = ctx.resolveTexture(albedoHandle);
                const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(visualizedHandle);

                m_copyDescriptorSet.Rewrite(m_device,
                    std::vector<ComputeDescriptorWrite>{
                        ComputeDescriptorWrite::CombinedImageSampler(0, source.view, m_albedoOutput->Sampler()),
                        ComputeDescriptorWrite::StorageImage(1, dest.view),
                    });

                const std::uint32_t pushConstants[2] = { sceneExtent.width, sceneExtent.height };

                rg::CommandBuffer cmd = ctx.Cmd();
                cmd.BindComputePipeline(*m_copyPipeline);
                cmd.BindDescriptorSet(m_copyDescriptorSet.Native());
                cmd.SetPushConstants(pushConstants, sizeof(pushConstants));
                cmd.DispatchOverSize(sceneExtent.width, sceneExtent.height);
            });
        // Default drawKind (unused/meaningless for a Compute-kind pass) and
        // default RenderPassEvent::Opaques - this pass's own real dependency
        // on "GBufferValidation" having already written albedoHandle is a
        // structural RAW resource dependency the compiler already tracks
        // (exactly like every other pass in this engine), not something a
        // RenderPassEvent tag needs to additionally encode - both passes are
        // also always declared back-to-back, in this fixed order, every frame.
        m_copyWrittenThisFrame = true;
    }

    return GBufferValidationHandles{ albedoHandle, normalHandle, visualizedHandle };
}

void GBufferValidation::FinalizeForSampling(VkCommandBuffer cmd)
{
    // albedo/normal: written by a real graphics pass. Only transitioned
    // when this half genuinely wrote something THIS frame (editor-core-
    // separation-21 campaign, PHASE4 - independently toggle-gated now).
    if (m_graphicsWrittenThisFrame) {
        m_graphicsWrittenThisFrame = false;
        m_albedoOutput->FinalizeForExternalSampling(cmd);
        m_normalOutput->FinalizeForExternalSampling(cmd);
    }

    // visualized: written by the compute copy pass. Only transitioned when
    // this half genuinely wrote something THIS frame.
    if (m_copyWrittenThisFrame) {
        m_copyWrittenThisFrame = false;
        m_visualizedOutput->FinalizeForExternalSampling(cmd);
    }
}

} // namespace gte
