#include "ComputeBlurValidation.h"

#include "../Renderer/RenderGraph/RenderGraph.h"
#include "../Renderer/Renderer.h"
#include "../Renderer/RenderGraph/RenderGraphBarrierPlanner.h"

#include <cstdint>
#include <vector>

namespace gte {

ComputeBlurValidation::~ComputeBlurValidation()
{
    // m_blurredOutput/m_pipeline are RAII types and clean up themselves;
    // m_descriptorSetLayout is now BORROWED from m_pipeline's own
    // ReflectedDescriptorSetLayout(0) - owned and destroyed by m_pipeline
    // itself (ComputePipeline::Destroy()'s own m_ownedReflectedLayouts
    // cleanup, PHASE2 of task_manager/better-render-pass-1). Keeping the old
    // vkDestroyDescriptorSetLayout() call here would be a genuine
    // double-free the instant this destructor ran - see
    // task_manager/better-render-pass-1/PHASE4_COMPLETION_REPORT.md's own
    // identical fix for the first-ever precedent of this exact bug class.
}

void ComputeBlurValidation::EnsureInitialized(Renderer& renderer, VkExtent2D initialExtent)
{
    if (m_pipeline.has_value()) {
        return;
    }

    const Renderer::VulkanContextInfo context = renderer.GetVulkanContextInfo();
    m_device = context.device;

    // task_manager/better-render-pass-1 campaign, PHASE6 - path-only,
    // reflection-based pipeline creation (PHASE2) replaces the old hand-built
    // DescriptorSetLayoutBuilder + manual VkPushConstantRange entirely.
    // Binding 0 (read-only Texture input) / binding 1 (RWTexture output) and
    // the 2x uint32 (width, height) push-constant block are both now read
    // directly from Shaders/BoxBlur.comp.spv's own compiled reflection data.
    m_pipeline.emplace(renderer.CreateComputePipeline("shaders/BoxBlur.comp.spv"));
    m_descriptorSetLayout = m_pipeline->ReflectedDescriptorSetLayout(/*set=*/0);

    m_descriptorSet = ComputeDescriptorSet(renderer.AllocateComputeDescriptorSet(m_descriptorSetLayout));

    // Explicit VK_FORMAT_R8G8B8A8_UNORM (never VK_FORMAT_UNDEFINED/
    // Renderer::ColorFormat()) - see this class's own header comment for
    // the full reasoning: this texture is never bound to the SAME
    // Pipeline as the swapchain/Game/Scene views (it's only ever sampled
    // via ImGui::Image()), so there is no reason to inherit the
    // swapchain's own negotiated color format - whose storage-image
    // support is NOT guaranteed on every driver/GPU (see
    // COMPUTE_PHASE7_VALIDATION_TESTING_TOOLING_STRATEGY_v2.md's own Step
    // 6, and COMPUTE_PHASE1_RESOURCE_VOCABULARY_STRATEGY_v2.md's own Step
    // 6, Finding 2/4) - while VK_FORMAT_R8G8B8A8_UNORM (Texture2D's own
    // already-proven format) is broadly supported.
    const int width = initialExtent.width > 0 ? static_cast<int>(initialExtent.width) : 1;
    const int height = initialExtent.height > 0 ? static_cast<int>(initialExtent.height) : 1;
    m_blurredOutput.emplace(renderer.CreateRenderTexture(width, height, VK_FORMAT_R8G8B8A8_UNORM, "BlurredSceneOutput",
        "BlurredSceneOutputDepth", /*allowStorageImageAccess=*/true));
}

rg::TextureHandle ComputeBlurValidation::AddPass(rg::RenderGraphBuilder& builder, Renderer& renderer,
    rg::TextureHandle sceneViewHandle, VkSampler sceneViewSampler, VkExtent2D sceneExtent)
{
    EnsureInitialized(renderer, sceneExtent);

    const VkExtent2D currentExtent = m_blurredOutput->Extent();
    if (currentExtent.width != sceneExtent.width || currentExtent.height != sceneExtent.height) {
        // Resizes are rare/user-driven (dragging the "Scene" panel's
        // border) - a full device stall here is the simplest correct
        // thing, mirroring ImGuiEditorLayer::GameViewTarget()/
        // SceneViewTarget()'s own identical discipline for their own
        // RenderTextures.
        vkDeviceWaitIdle(m_device);
        m_blurredOutput->Resize(static_cast<int>(sceneExtent.width), static_cast<int>(sceneExtent.height));
    }

    // Always imported as VK_IMAGE_LAYOUT_UNDEFINED - mirrors
    // RenderPasses.h's own AddGameViewPass()/AddSceneViewPass() call sites
    // (Application::Run()) exactly: this pass's own compute write fully
    // overwrites every in-bounds pixel every time it runs, so the
    // previous frame's actual contents/layout never need to be preserved.
    const rg::TextureHandle outputHandle = builder.ImportTexture("BlurredSceneOutput", m_blurredOutput->Target(),
        VK_IMAGE_LAYOUT_UNDEFINED, m_blurredOutput->Sampler(), m_blurredOutput->DepthSampler());

    builder.AddRenderPass(
        "ComputeBlurValidation", rg::PassKind::Compute, rg::ViewScope::SceneView, rg::RenderPassCategory::Debug, // Confirmed Scene-View-only - see Application.cpp's own AddBlurValidationPass() call site (frame-debugger-6, PHASE1). Debug category (render-pass-1 campaign, PHASE5) - this pass's invisibility from the Frame Debugger's Game-View-only tree comes ENTIRELY from the ViewScope::SceneView filter above; RenderPassCategory::Debug itself no longer implies any hiding at all (editor-core-separation-22 campaign, PHASE4 corrected its meaning to "a real, visible, optional/debug-flavored FEATURE pass" - see RenderGraphTypes.h).
        [sceneViewHandle, outputHandle](rg::RenderGraphBuilder::PassBuilder& pass) {
            pass.ReadTexture(sceneViewHandle, rg::ResourceAccess::ShaderRead);
            pass.WriteTexture(outputHandle, rg::ResourceAccess::ComputeShaderWrite);
        },
        // Every captured handle/value here is captured BY VALUE, never by
        // reference to a `build`-lambda-local variable - see
        // COMPUTE_PHASE6_COMPLETION_REPORT.md's own "Handoff notes" for
        // exactly why (a dangling-reference bug this phase's own
        // predecessor already found and fixed once). `this` is long-lived
        // (this object outlives the whole Execute() call) - safe to
        // capture by pointer. `&renderer` is no longer captured -
        // task_manager/better-render-pass-1 campaign, PHASE6 migrated this
        // dispatch onto rg::CommandBuffer (obtained from ctx.Cmd()), which
        // needs no direct Renderer& reference of its own.
        [this, sceneViewHandle, outputHandle, sceneViewSampler, sceneExtent](rg::PassContext& ctx) {
            const rg::PassContext::ResolvedTexture source = ctx.resolveTexture(sceneViewHandle);
            const rg::PassContext::ResolvedTexture dest = ctx.resolveTexture(outputHandle);

            m_descriptorSet.Rewrite(m_device,
                std::vector<ComputeDescriptorWrite>{
                    ComputeDescriptorWrite::CombinedImageSampler(0, source.view, sceneViewSampler),
                    ComputeDescriptorWrite::StorageImage(1, dest.view),
                });

            const std::uint32_t pushConstants[2] = { sceneExtent.width, sceneExtent.height };

            rg::CommandBuffer cmd = ctx.Cmd();
            cmd.BindComputePipeline(*m_pipeline);
            cmd.BindDescriptorSet(m_descriptorSet.Native());
            cmd.SetPushConstants(pushConstants, sizeof(pushConstants));
            cmd.DispatchOverSize(sceneExtent.width, sceneExtent.height);
        },
        rg::RenderPassDrawKind::DrawMesh,
        // render-pass-4 campaign, PHASE2
        // (task_manager/render-pass-4/PHASE2_REAL_RENDERPASSEVENT_ORDERING_ENFORCEMENT.md)
        // - explicitly tagged AfterTransparents (never left at the default
        // Opaques) because this pass has a REAL, confirmed dependency on
        // "RenderTransparent" (Scene View): it reads `sceneViewHandle`, the
        // SAME handle "RenderOpaque"/"DrawSkyBackground"/"RenderTransparent"
        // (Scene View) all write, and must resolve to the LATEST of those
        // three writes. Left at the default Opaques, RenderGraphCompiler::
        // Compile()'s new RenderPassEvent-sorted effective order (PHASE2)
        // would have silently bound this read to "RenderOpaque"'s own write
        // instead, potentially scheduling this pass before the sky
        // background/ground-grid overlay was even drawn - a real, confirmed
        // regression this explicit tag prevents (found by this phase's own
        // required audit, confirmed with the user via ask_questions before
        // fixing here rather than leaving it for a later phase/campaign).
        // AfterTransparents (rather than the same Transparents tier
        // "RenderTransparent" itself uses) is deliberately used so this
        // never depends on stable-sort tie-break-by-declaration-order
        // subtlety at all - it is unambiguously scheduled after the whole
        // real Scene View draw chain regardless.
        rg::RenderPassEvent::AfterTransparents);

    m_writtenThisFrame = true;
    return outputHandle;
}

void ComputeBlurValidation::FinalizeForSampling(VkCommandBuffer cmd)
{
    if (!m_writtenThisFrame) {
        return;
    }
    m_writtenThisFrame = false;

    const rg::ResourceState previous = rg::RequiredStateFor(rg::ResourceAccess::ComputeShaderWrite, false);
    const rg::ResourceState next = rg::RequiredStateFor(rg::ResourceAccess::ShaderRead, false);
    const VkImageSubresourceRange range{ VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    rg::EmitImageBarrier(cmd, m_blurredOutput->Image(), range, previous, next);
}

} // namespace gte
